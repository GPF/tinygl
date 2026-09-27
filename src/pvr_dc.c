#include "zgl.h"

#include <kos.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <kos/cache.h>

static const pvr_init_params_t tgl_pvr_params = {
  /* Opaque geometry uses OP_POLY; blended geometry uses TR_POLY. */
  { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16,
    PVR_BINSIZE_0, PVR_BINSIZE_0 },
  512 * 1024,
  0,  /* Direct submission. */
  0,  /* No FSAA. */
  0,  /* Translucent autosort is unused. */
  3,  /* Extra OPBs prevent tile-bin overflow artifacts in dense scenes. */
  0   /* Use KOS's default vertex-buffer configuration. */
};

static pvr_poly_hdr_t tgl_pvr_header;
static int tgl_pvr_initialized;
static int tgl_pvr_scene_active;
static int tgl_pvr_list_active;
static pvr_list_t tgl_pvr_list_type;
static int tgl_pvr_submission_errors;  /* PVR submission failures this session */
static int tgl_pvr_header_depth_test = -1;
static int tgl_pvr_header_depth_func = -1;
static int tgl_pvr_header_depth_write = -1;
static int tgl_pvr_header_blend = -1;
static int tgl_pvr_header_blend_src = -1;
static int tgl_pvr_header_blend_dst = -1;
#ifdef TGL_PVR_TEST_DUMP_STRIP
static unsigned int tgl_pvr_debug_strip_id;
#endif

/* Textured-triangle support.
 *
 * TinyGL stores textures as a 256x256 RGB565 pixmap in system RAM
 * (see texture.c: glTexImage2D resizes to 256x256 and converts to
 * TGL_FEATURE_RENDER_BITS, which is 16-bit RGB565 on this build). The PVR
 * wants a twiddled texture, so the pixmap is copied into a 2048-byte
 * aligned system-RAM buffer, cache-synced, then twiddled into VRAM via
 * pvr_txr_load_ex. The load is cached per pixmap pointer so repeated frames
 * do not re-twiddle. */
#define TGL_PVR_TEXTURE_SIZE (256 * 256 * 2)

static pvr_poly_hdr_t tgl_pvr_txr_header;
static int tgl_pvr_txr_header_valid;
static int tgl_pvr_txr_depth_test = -1;
static int tgl_pvr_txr_depth_func = -1;
static int tgl_pvr_txr_depth_write = -1;
static int tgl_pvr_txr_blend = -1;
static int tgl_pvr_txr_blend_src = -1;
static int tgl_pvr_txr_blend_dst = -1;
static void *tgl_pvr_txr_header_pixmap;
static pvr_ptr_t tgl_pvr_txr_vram;
static uint16_t tgl_pvr_txr_src[256 * 256] __attribute__((aligned(2048)));
static void *tgl_pvr_last_pixmap;
static int tgl_pvr_txr_src_ready;

static int tgl_pvr_prim(const void *cmd, size_t size);
static void tgl_pvr_update_header(GLContext *c);
static void tgl_pvr_begin_scene(void);
static void tgl_pvr_select_list(int blend_enabled);

int tgl_pvr_init(void) {
  pvr_poly_cxt_t context;

  if (tgl_pvr_initialized) return -1;
  if (pvr_init(&tgl_pvr_params) < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: pvr_init failed\n");
    return -1;
  }

  pvr_set_bg_color(0.0f, 0.0f, 0.0f);
  pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
  context.gen.shading = PVR_SHADE_GOURAUD;
  context.gen.culling = PVR_CULLING_NONE;
  context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
  context.depth.write = PVR_DEPTHWRITE_DISABLE;
  pvr_poly_compile(&tgl_pvr_header, &context);
  tgl_pvr_header_depth_test = -1;
  tgl_pvr_header_depth_func = -1;
  tgl_pvr_header_depth_write = -1;
  tgl_pvr_header_blend = -1;
  tgl_pvr_header_blend_src = -1;
  tgl_pvr_header_blend_dst = -1;

  tgl_pvr_initialized = 1;
  return 0;
}

void tgl_pvr_set_clear_color(float r, float g, float b) {
  if (tgl_pvr_initialized) pvr_set_bg_color(r, g, b);
}

static uint32_t tgl_pvr_color_component(float value) {
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  return (uint32_t)(value * 255.0f + 0.5f);
}

static void tgl_pvr_set_vertex(pvr_vertex_t *vertex, const GLVertex *source,
                               float x, float y, int last) {
  uint32_t r = tgl_pvr_color_component(source->color.v[0]);
  uint32_t g = tgl_pvr_color_component(source->color.v[1]);
  uint32_t b = tgl_pvr_color_component(source->color.v[2]);
  uint32_t a = tgl_pvr_color_component(source->color.v[3]);

  vertex->flags = last ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
  vertex->x = x;
  vertex->y = y;
  vertex->z = (float)source->zp.z / (float)(1u << 30);
  if (vertex->z < 0.0f) vertex->z = 0.0f;
  if (vertex->z > 1.0f) vertex->z = 1.0f;
  vertex->argb = (a << 24) | (r << 16) | (g << 8) | b;
}

static int tgl_pvr_submit_header(GLContext *c) {
  tgl_pvr_update_header(c);
  if (tgl_pvr_prim(&tgl_pvr_header, sizeof(tgl_pvr_header)) < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: polygon header submission failed\n");
    return -1;
  }
  return 0;
}

static void tgl_pvr_submit_quad(const GLVertex *sources[4],
                                float xy[4][2]) {
  int i;
  for (i = 0; i < 4; ++i) {
    pvr_vertex_t vertex = { 0 };
    tgl_pvr_set_vertex(&vertex, sources[i], xy[i][0], xy[i][1], i == 3);
    if (pvr_prim(&vertex, sizeof(vertex)) < 0) {
      tgl_pvr_submission_errors++;
      fprintf(stderr, "TinyGL PVR: vertex submission failed\n");
      return;
    }
  }
}

void tgl_pvr_draw_point(GLContext *c, GLVertex *p0) {
  const GLVertex *sources[4] = { p0, p0, p0, p0 };
  const float x = (float)p0->zp.x;
  const float y = (float)p0->zp.y;
  float xy[4][2] = {
    { x,     y     }, { x + 1, y     },
    { x + 1, y + 1 }, { x,     y + 1 }
  };

  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;
  tgl_pvr_select_list(c->blend_enabled);
  if (!tgl_pvr_list_active || tgl_pvr_submit_header(c) < 0) return;
  tgl_pvr_submit_quad(sources, xy);
}

void tgl_pvr_draw_line(GLContext *c, GLVertex *p0, GLVertex *p1) {
  const float x0 = (float)p0->zp.x;
  const float y0 = (float)p0->zp.y;
  const float x1 = (float)p1->zp.x;
  const float y1 = (float)p1->zp.y;
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float length_sq = dx * dx + dy * dy;
  float nx, ny;
  const GLVertex *sources[4] = { p0, p0, p1, p1 };
  float xy[4][2];

  if (length_sq == 0.0f) {
    tgl_pvr_draw_point(c, p0);
    return;
  }
  {
    const float half_width = 0.5f / sqrtf(length_sq);
    nx = -dy * half_width;
    ny =  dx * half_width;
  }
  xy[0][0] = x0 + nx; xy[0][1] = y0 + ny;
  xy[1][0] = x0 - nx; xy[1][1] = y0 - ny;
  xy[2][0] = x1 + nx; xy[2][1] = y1 + ny;
  xy[3][0] = x1 - nx; xy[3][1] = y1 - ny;

  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;
  tgl_pvr_select_list(c->blend_enabled);
  if (!tgl_pvr_list_active || tgl_pvr_submit_header(c) < 0) return;
  tgl_pvr_submit_quad(sources, xy);
}

/* Map a GL blend factor to a PVR blend mode. The PVR has no mode for
 * GL_SRC_COLOR / GL_ONE_MINUS_SRC_COLOR / GL_SRC_ALPHA_SATURATE, so those
 * fall back to a source-only factor (no blending) rather than mis-producing.
 * TinyGL only supports GL_FUNC_ADD, which the PVR's additive blend does. */
static pvr_blend_mode_t tgl_pvr_blend_factor(int gl_factor) {
  switch (gl_factor) {
  case GL_ZERO:                  return PVR_BLEND_ZERO;
  case GL_ONE:                   return PVR_BLEND_ONE;
  case GL_SRC_ALPHA:             return PVR_BLEND_SRCALPHA;
  case GL_ONE_MINUS_SRC_ALPHA:   return PVR_BLEND_INVSRCALPHA;
  case GL_DST_ALPHA:             return PVR_BLEND_DESTALPHA;
  case GL_ONE_MINUS_DST_ALPHA:   return PVR_BLEND_INVDESTALPHA;
  case GL_DST_COLOR:             return PVR_BLEND_DESTCOLOR;
  case GL_ONE_MINUS_DST_COLOR:   return PVR_BLEND_INVDESTCOLOR;
  default:                       return PVR_BLEND_ONE;
  }
}

static pvr_depthcmp_mode_t tgl_pvr_depth_comparison(int func) {
  /* TinyGL maps nearer fragments to larger Z values (reverse-Z). */
  switch (func) {
  case GL_NEVER: return PVR_DEPTHCMP_NEVER;
  case GL_LESS: return PVR_DEPTHCMP_GREATER;
  case GL_EQUAL: return PVR_DEPTHCMP_EQUAL;
  case GL_LEQUAL: return PVR_DEPTHCMP_GEQUAL;
  case GL_GREATER: return PVR_DEPTHCMP_LESS;
  case GL_NOTEQUAL: return PVR_DEPTHCMP_NOTEQUAL;
  case GL_GEQUAL: return PVR_DEPTHCMP_LEQUAL;
  case GL_ALWAYS: return PVR_DEPTHCMP_ALWAYS;
  default: return PVR_DEPTHCMP_ALWAYS;
  }
}

static void tgl_pvr_update_header(GLContext *c) {
  pvr_poly_cxt_t context;
  int depth_write = c->depth_test && c->depth_mask;
  int blend_enabled = c->blend_enabled;

  if (tgl_pvr_header_depth_test == c->depth_test &&
      tgl_pvr_header_depth_func == c->depth_func &&
      tgl_pvr_header_depth_write == depth_write &&
      tgl_pvr_header_blend == blend_enabled &&
      tgl_pvr_header_blend_src == c->blend_src &&
      tgl_pvr_header_blend_dst == c->blend_dst) return;

  pvr_poly_cxt_col(&context, blend_enabled ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY);
  context.gen.shading = PVR_SHADE_GOURAUD;
  context.gen.culling = PVR_CULLING_NONE;
  context.depth.comparison = c->depth_test ?
      tgl_pvr_depth_comparison(c->depth_func) : PVR_DEPTHCMP_ALWAYS;
  context.depth.write = depth_write ?
      PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE;
  /* Enable vertex-color alpha and set the blend factors. When blending is
   * off the factors reduce to source-only, which is a no-op. */
  context.gen.alpha = blend_enabled;
  if (blend_enabled) {
    context.blend.src = tgl_pvr_blend_factor(c->blend_src);
    context.blend.dst = tgl_pvr_blend_factor(c->blend_dst);
    /* KOS maps these flags to the second accumulation buffers; keep them off. */
    context.blend.src_enable = 0;
    context.blend.dst_enable = 0;
  } else {
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    context.blend.src_enable = 0;
    context.blend.dst_enable = 0;
  }
  pvr_poly_compile(&tgl_pvr_header, &context);

  tgl_pvr_header_depth_test = c->depth_test;
  tgl_pvr_header_depth_func = c->depth_func;
  tgl_pvr_header_depth_write = depth_write;
  tgl_pvr_header_blend = blend_enabled;
  tgl_pvr_header_blend_src = c->blend_src;
  tgl_pvr_header_blend_dst = c->blend_dst;
}

static void tgl_pvr_begin_scene(void) {
  if (tgl_pvr_scene_active || !tgl_pvr_initialized) return;

  pvr_scene_begin();
  if (pvr_list_begin(PVR_LIST_OP_POLY) < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: pvr_list_begin failed\n");
    if (pvr_scene_finish() < 0) {
      tgl_pvr_submission_errors++;
      fprintf(stderr, "TinyGL PVR: pvr_scene_finish failed\n");
    }
    return;
  }
  tgl_pvr_scene_active = 1;
  tgl_pvr_list_active = 1;
  tgl_pvr_list_type = PVR_LIST_OP_POLY;
}

static void tgl_pvr_select_list(int blend_enabled) {
  pvr_list_t wanted = blend_enabled ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;

  if (!tgl_pvr_scene_active || !tgl_pvr_list_active ||
      wanted == tgl_pvr_list_type) return;

  /* Lists cannot be reopened within a scene. Once a translucent primitive
   * appears, subsequent primitives stay on TR until the next scene. */
  if (tgl_pvr_list_type == PVR_LIST_TR_POLY) return;
  if (pvr_list_finish() < 0 || pvr_list_begin(wanted) < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: list transition failed\n");
    tgl_pvr_list_active = 0;
    tgl_pvr_list_type = -1;
    return;
  }
  tgl_pvr_list_type = wanted;
}

/* Load the current texture into VRAM as twiddled RGB565, caching by pixmap
 * pointer. pvr_txr_load() only copies bytes; pvr_txr_load_ex() performs the
 * twiddle required by the texture header. Returns the VRAM base address. */
static pvr_ptr_t tgl_pvr_texture_load(GLContext *c) {
  GLImage *im = &c->current_texture->images[0];
  void *pixmap = im->pixmap;

  if (!tgl_pvr_txr_src_ready) {
    tgl_pvr_txr_vram = pvr_mem_malloc(TGL_PVR_TEXTURE_SIZE);
    tgl_pvr_last_pixmap = NULL;
    tgl_pvr_txr_src_ready = 1;
  }

  if (tgl_pvr_last_pixmap != pixmap) {
    memcpy(tgl_pvr_txr_src, pixmap, TGL_PVR_TEXTURE_SIZE);
    /* The aligned scratch copy keeps the source stable for the upload. */
    arch_dcache_wback_range((uintptr_t)tgl_pvr_txr_src, TGL_PVR_TEXTURE_SIZE);
    pvr_txr_load_ex(tgl_pvr_txr_src, tgl_pvr_txr_vram, 256, 256,
                    PVR_TXRLOAD_16BPP);
    tgl_pvr_last_pixmap = pixmap;
  }

  return tgl_pvr_txr_vram;
}

/* Build the textured polygon header (and reload the texture) when the
 * texture pixmap or the depth/culling state changes. pvr_poly_cxt_txr()
 * hardcodes depth and culling, so override them here to match TinyGL's
 * current state. */
static void tgl_pvr_update_txr_header(GLContext *c) {
  pvr_poly_cxt_t context;
  int depth_write = c->depth_test && c->depth_mask;
  int blend_enabled = c->blend_enabled;
  void *pixmap = c->current_texture->images[0].pixmap;
  int need_rebuild = !tgl_pvr_txr_header_valid ||
      tgl_pvr_txr_header_pixmap != pixmap ||
      tgl_pvr_txr_depth_test != c->depth_test ||
      tgl_pvr_txr_depth_func != c->depth_func ||
      tgl_pvr_txr_depth_write != depth_write ||
      tgl_pvr_txr_blend != blend_enabled ||
      tgl_pvr_txr_blend_src != c->blend_src ||
      tgl_pvr_txr_blend_dst != c->blend_dst;

  if (!need_rebuild) return;

  /* cxt_txr sets the blend factors to a source-only default, so override the
   * context after cxt_txr and before compile so the baked-in header matches
   * TinyGL's blend state. */
  pvr_poly_cxt_txr(&context, blend_enabled ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                   256, 256, tgl_pvr_texture_load(c), PVR_FILTER_NEAREST);
  context.gen.alpha = blend_enabled;
  if (blend_enabled) {
    context.blend.src = tgl_pvr_blend_factor(c->blend_src);
    context.blend.dst = tgl_pvr_blend_factor(c->blend_dst);
    /* KOS maps these flags to the second accumulation buffers; keep them off. */
    context.blend.src_enable = 0;
    context.blend.dst_enable = 0;
  } else {
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    context.blend.src_enable = 0;
    context.blend.dst_enable = 0;
  }
  pvr_poly_compile(&tgl_pvr_txr_header, &context);

  /* Override the context's hardcoded depth/culling to match TinyGL. */
  tgl_pvr_txr_header.m1.depth_cmp = c->depth_test ?
      tgl_pvr_depth_comparison(c->depth_func) : PVR_DEPTHCMP_ALWAYS;
  tgl_pvr_txr_header.m1.depth_write_dis = !depth_write;
  tgl_pvr_txr_header.m1.culling = PVR_CULLING_NONE;

  tgl_pvr_txr_header_valid = 1;
  tgl_pvr_txr_depth_test = c->depth_test;
  tgl_pvr_txr_depth_func = c->depth_func;
  tgl_pvr_txr_depth_write = depth_write;
  tgl_pvr_txr_blend = blend_enabled;
  tgl_pvr_txr_blend_src = c->blend_src;
  tgl_pvr_txr_blend_dst = c->blend_dst;
  tgl_pvr_txr_header_pixmap = pixmap;
}

/* Test-only one-shot PVR submission failure injection.
 *
 * Only active when the smoke test is built with -DTGL_PVR_TEST_INJECT_FAIL.
 * Normal builds define neither the state nor the arming hook.
 */
#ifdef TGL_PVR_TEST_INJECT_FAIL
static int tgl_pvr_test_fail_next;  /* armed by tgl_pvr_test_arm_fail_next() */
static int tgl_pvr_test_fail_fired; /* records that the injection fired     */

void tgl_pvr_test_arm_fail_next(void) {
  tgl_pvr_test_fail_next = 1;
}
#endif

/* Primitive submission entry point.
 *
 * Normal builds: a thin pass-through to the KOS API, so submission behavior is
 * unchanged. When TGL_PVR_TEST_INJECT_FAIL is defined, this wrapper simulates
 * one KOS pvr_prim() returning -1 so the backend's error accounting, list
 * cleanup, and recovery can be exercised. The simulation does NOT enqueue the
 * command and leaves the list open, matching KOS's contract when pvr_prim()
 * rejects a primitive (e.g. arena full). This is a controlled, one-shot,
 * non-destructive test simulation -- it is not a hardware-generated failure.
 */
static int tgl_pvr_prim(const void *cmd, size_t size) {
#ifdef TGL_PVR_TEST_INJECT_FAIL
  if (tgl_pvr_test_fail_next) {
    tgl_pvr_test_fail_next = 0;
    tgl_pvr_test_fail_fired = 1;
    fprintf(stderr,
            "TinyGL PVR TEST: simulating one polygon-header submission failure\n");
    return -1;
  }
#endif
  return pvr_prim(cmd, size);
}

#ifdef TINYGL_PROFILE_STAGES
static void tgl_pvr_draw_triangle_impl(GLContext *c, GLVertex *p0, GLVertex *p1,
                                        GLVertex *p2);

void tgl_pvr_draw_triangle(GLContext *c, GLVertex *p0, GLVertex *p1,
                           GLVertex *p2) {
  uint64_t t0 = tgl_profile_clock ? tgl_profile_clock() : 0;
  tgl_pvr_draw_triangle_impl(c, p0, p1, p2);
  if (tgl_profile_clock) tgl_profile_submit_us += tgl_profile_clock() - t0;
}

static void tgl_pvr_draw_triangle_impl(GLContext *c, GLVertex *p0, GLVertex *p1,
                                        GLVertex *p2) {
#else
void tgl_pvr_draw_triangle(GLContext *c, GLVertex *p0, GLVertex *p1,
                           GLVertex *p2) {
#endif
  GLVertex *vertices[3];
  int i;

  vertices[0] = p0;
  vertices[1] = p1;
  vertices[2] = p2;

  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;
  tgl_pvr_select_list(c->blend_enabled);
  if (!tgl_pvr_list_active) return;

  /* Textured path only when a texture is enabled AND its pixmap is actually
   * present. If the pixmap is missing (e.g. the texture image was never
   * populated), fall through to the solid-color path rather than emitting a
   * textured polygon with an empty VRAM base. */
  if (c->texture_2d_enabled && c->current_texture &&
      c->current_texture->images[0].pixmap) {
    tgl_pvr_update_txr_header(c);

    if (pvr_prim(&tgl_pvr_txr_header, sizeof(tgl_pvr_header)) < 0) {
      tgl_pvr_submission_errors++;
      fprintf(stderr, "TinyGL PVR: texture polygon header submission failed\n");
      return;
    }

    {
      /* Same sq_fast_cpy() batching as the solid path below: build all 3
       * vertices contiguously and push them in one call instead of one
       * pvr_prim() per vertex. */
      pvr_vertex_t verts[3] = { { 0 } };

      for (i = 0; i < 3; ++i) {
        verts[i].flags = (i == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        verts[i].x = (float)vertices[i]->zp.x;
        verts[i].y = (float)vertices[i]->zp.y;
        verts[i].z = (float)vertices[i]->zp.z / (float)(1u << 30);
        if (verts[i].z < 0.0f) verts[i].z = 0.0f;
        if (verts[i].z > 1.0f) verts[i].z = 1.0f;
        /* TinyGL only supports GL_DECAL; use white vertex color so the
         * MODULATE texture environment yields the texel exactly. */
        verts[i].u = vertices[i]->tex_coord.X;
        verts[i].v = vertices[i]->tex_coord.Y;
        verts[i].argb = 0xFFFFFFFF;
      }

      sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), verts, 3);
    }
    return;
  }

  tgl_pvr_update_header(c);

  /* Route the solid polygon header through tgl_pvr_prim(). Normal builds are
   * a thin pass-through to pvr_prim(); the smoke test builds with
   * -DTGL_PVR_TEST_INJECT_FAIL to inject one controlled failure here. */
  if (tgl_pvr_prim(&tgl_pvr_header, sizeof(tgl_pvr_header)) < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: polygon header submission failed\n");
    return;
  }

  {
    /* Build all 3 vertices contiguously, then push them to the TA in one
     * sq_fast_cpy() call instead of one pvr_prim() (== one sq_fast_cpy())
     * call per vertex. Safe: pvr_list_begin() already holds the SQ lock for
     * the whole list (see KOS's pvr_scene.c), and pvr_vertex_t is exactly
     * one 32-byte, 32-byte-aligned TA block, so 3 of them back-to-back are
     * exactly what sq_fast_cpy()'s "n 32-byte blocks" contract expects. */
    pvr_vertex_t verts[3] = { { 0 } };

    for (i = 0; i < 3; ++i) {
      uint32_t r = tgl_pvr_color_component(vertices[i]->color.v[0]);
      uint32_t g = tgl_pvr_color_component(vertices[i]->color.v[1]);
      uint32_t b = tgl_pvr_color_component(vertices[i]->color.v[2]);
      uint32_t a = tgl_pvr_color_component(vertices[i]->color.v[3]);

      verts[i].flags = (i == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
      verts[i].x = (float)vertices[i]->zp.x;
      verts[i].y = (float)vertices[i]->zp.y;
      verts[i].z = (float)vertices[i]->zp.z / (float)(1u << 30);
      if (verts[i].z < 0.0f) verts[i].z = 0.0f;
      if (verts[i].z > 1.0f) verts[i].z = 1.0f;
      verts[i].argb = (a << 24) | (r << 16) | (g << 8) | b;
    }

    sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), verts, 3);
  }
}

/* Submit a prevalidated, unclipped GL triangle strip as one native TA strip.
 * Lighting, when enabled, has already been evaluated into vertex colors. */
void tgl_pvr_draw_strip(GLContext *c, GLVertex *vertices, int count) {
  int i;
  int textured;

  if (count < 3) return;
#ifdef TGL_PVR_TEST_DUMP_STRIP
  if (count == 42 && tgl_pvr_debug_strip_id < 1200) {
    unsigned int strip_id = tgl_pvr_debug_strip_id++;
    for (i = 1; i < count; ++i) {
      int dx = vertices[i].zp.x - vertices[i - 1].zp.x;
      int dy = vertices[i].zp.y - vertices[i - 1].zp.y;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      if (dx > 10 || dy > 10) {
        printf("STRIP %u VERTEX %d: (%d,%d,%d)->(%d,%d,%d)\n",
               strip_id, i, vertices[i - 1].zp.x, vertices[i - 1].zp.y,
               vertices[i - 1].zp.z, vertices[i].zp.x, vertices[i].zp.y,
               vertices[i].zp.z);
      }
    }
    if (strip_id >= 1180) {
      for (i = 0; i + 2 < count; ++i) {
        int x0 = vertices[i].zp.x, y0 = vertices[i].zp.y;
        int x1 = vertices[i + 1].zp.x, y1 = vertices[i + 1].zp.y;
        int x2 = vertices[i + 2].zp.x, y2 = vertices[i + 2].zp.y;
        int area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        int e0 = (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
        int e1 = (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1);
        int e2 = (x0 - x2) * (x0 - x2) + (y0 - y2) * (y0 - y2);
        int longest = e0 > e1 ? e0 : e1;
        if (e2 > longest) longest = e2;
        if (area >= -64 && area <= 64 && longest > 100) {
          printf("THIN STRIP %u TRI %d: area=%d (%d,%d) (%d,%d) (%d,%d)\n",
                 strip_id, i, area, x0, y0, x1, y1, x2, y2);
        }
      }
    }
  }
#endif
  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;
  tgl_pvr_select_list(c->blend_enabled);
  if (!tgl_pvr_list_active) return;

  textured = c->texture_2d_enabled && c->current_texture &&
             c->current_texture->images[0].pixmap;
  if (textured) {
    tgl_pvr_update_txr_header(c);
    if (tgl_pvr_prim(&tgl_pvr_txr_header, sizeof(tgl_pvr_header)) < 0) {
      tgl_pvr_submission_errors++;
      fprintf(stderr, "TinyGL PVR: strip header submission failed\n");
      return;
    }
  } else if (tgl_pvr_submit_header(c) < 0) {
    return;
  }

  /* Push vertices to the TA in fixed-size batches via one sq_fast_cpy() per
   * batch instead of one pvr_prim() (== one sq_fast_cpy()) call per vertex.
   * Safe: pvr_list_begin() already holds the SQ lock for the whole list
   * (see KOS's pvr_scene.c), and pvr_vertex_t is exactly one 32-byte,
   * 32-byte-aligned TA block. A fixed batch buffer (rather than sizing to
   * `count`) keeps stack use bounded regardless of strip length. */
#ifndef TGL_PVR_STRIP_BATCH
#define TGL_PVR_STRIP_BATCH 32
#endif
  {
#ifdef TGL_PVR_TEST_ZERO_STRIP_VERTICES
    pvr_vertex_t verts[TGL_PVR_STRIP_BATCH] = { { 0 } };
#else
    pvr_vertex_t verts[TGL_PVR_STRIP_BATCH];
#endif
    int batch_start;

    for (batch_start = 0; batch_start < count; batch_start += TGL_PVR_STRIP_BATCH) {
      int batch_count = count - batch_start;
      if (batch_count > TGL_PVR_STRIP_BATCH) batch_count = TGL_PVR_STRIP_BATCH;

      for (i = 0; i < batch_count; ++i) {
	int idx = batch_start + i;
	tgl_pvr_set_vertex(&verts[i], &vertices[idx], (float)vertices[idx].zp.x,
			   (float)vertices[idx].zp.y, idx == count - 1);
	if (textured) {
	  verts[i].u = vertices[idx].tex_coord.X;
	  verts[i].v = vertices[idx].tex_coord.Y;
	  verts[i].argb = 0xFFFFFFFF;
	}
      }

#ifdef TGL_PVR_TEST_STRIP_PVR_PRIM
      for (i = 0; i < batch_count; ++i) {
        if (tgl_pvr_prim(&verts[i], sizeof(verts[i])) < 0) {
          tgl_pvr_submission_errors++;
          fprintf(stderr, "TinyGL PVR: strip vertex submission failed\n");
          return;
        }
      }
#else
      sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), verts, batch_count);
#endif
    }
  }
#undef TGL_PVR_STRIP_BATCH
}

void tgl_pvr_flush(void) {
  if (tgl_pvr_list_active) {
    if (pvr_list_finish() < 0) {
      tgl_pvr_submission_errors++;
      fprintf(stderr, "TinyGL PVR: pvr_list_finish failed\n");
    }
    /* The half-open list is abandoned; the next begin_scene starts a fresh
     * OP list so a submission error does not corrupt the following frame. */
    tgl_pvr_list_active = 0;
  }

  /* KOS requires a scene boundary every frame. When nothing was drawn this
     frame there is no open scene to finish; begin and finish an empty scene
     so the background color is committed and the previous frame's geometry is
     not retained on the display. */
  if (!tgl_pvr_scene_active) {
    pvr_scene_begin();
  }
  if (pvr_scene_finish() < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: pvr_scene_finish failed\n");
  }
  tgl_pvr_scene_active = 0;
}

void tgl_pvr_shutdown(void) {
  if (!tgl_pvr_initialized) return;

  tgl_pvr_flush();
  if (pvr_shutdown() < 0) {
    tgl_pvr_submission_errors++;
    fprintf(stderr, "TinyGL PVR: pvr_shutdown failed\n");
  }
  /* Report any submission failures accumulated during the session. */
  if (tgl_pvr_submission_errors) {
    fprintf(stderr, "TinyGL PVR: %d submission error(s) this session\n",
            tgl_pvr_submission_errors);
  }
  tgl_pvr_initialized = 0;
}
