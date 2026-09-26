#include "zgl.h"

#include <kos.h>
#include <stdio.h>
#include <string.h>
#include <kos/cache.h>

static const pvr_init_params_t tgl_pvr_params = {
  { PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_0,
    PVR_BINSIZE_0, PVR_BINSIZE_0 },
  512 * 1024,
  0,  /* Direct submission. */
  0,  /* No FSAA. */
  0,  /* Translucent autosort is unused. */
  0,  /* No OPB overflow. */
  0   /* Use KOS's default vertex-buffer configuration. */
};

static pvr_poly_hdr_t tgl_pvr_header;
static int tgl_pvr_initialized;
static int tgl_pvr_scene_active;
static int tgl_pvr_list_active;
static int tgl_pvr_header_depth_test = -1;
static int tgl_pvr_header_depth_func = -1;
static int tgl_pvr_header_depth_write = -1;
static int tgl_pvr_header_blend = -1;
static int tgl_pvr_header_blend_src = -1;
static int tgl_pvr_header_blend_dst = -1;

/* Textured-triangle support.
 *
 * TinyGL stores textures as a 256x256 RGB565 pixmap in system RAM
 * (see texture.c: glTexImage2D resizes to 256x256 and converts to
 * TGL_FEATURE_RENDER_BITS, which is 16-bit RGB565 on this build). The PVR
 * wants a twiddled texture, so the pixmap is copied into a 2048-byte
 * aligned system-RAM buffer, cache-synced, then twiddled into VRAM via
 * pvr_txr_load (the same pattern KOS's plasma example uses). The load is
 * cached per pixmap pointer so repeated frames do not re-twiddle. */
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

int tgl_pvr_init(void) {
  pvr_poly_cxt_t context;

  if (tgl_pvr_initialized) return -1;
  if (pvr_init(&tgl_pvr_params) < 0) {
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

  pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
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
    context.blend.src_enable = 1;
    context.blend.dst_enable = 1;
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
    fprintf(stderr, "TinyGL PVR: pvr_list_begin failed\n");
    pvr_scene_finish();
    return;
  }
  tgl_pvr_scene_active = 1;
  tgl_pvr_list_active = 1;
}

/* Load the current texture into VRAM (twiddled RGB565), caching by pixmap
 * pointer. Returns the VRAM base address to use in the polygon context. */
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
    /* pvr_txr_load DMA-reads the source; commit CPU writes to RAM first. */
    arch_dcache_wback_range((uintptr_t)tgl_pvr_txr_src, TGL_PVR_TEXTURE_SIZE);
    pvr_txr_load(tgl_pvr_txr_src, tgl_pvr_txr_vram, TGL_PVR_TEXTURE_SIZE);
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
  pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                   256, 256, tgl_pvr_texture_load(c), PVR_FILTER_NEAREST);
  context.gen.alpha = blend_enabled;
  if (blend_enabled) {
    context.blend.src = tgl_pvr_blend_factor(c->blend_src);
    context.blend.dst = tgl_pvr_blend_factor(c->blend_dst);
    context.blend.src_enable = 1;
    context.blend.dst_enable = 1;
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

void tgl_pvr_draw_triangle(GLContext *c, GLVertex *p0, GLVertex *p1,
                           GLVertex *p2) {
  GLVertex *vertices[3];
  int i;

  vertices[0] = p0;
  vertices[1] = p1;
  vertices[2] = p2;

  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;

  /* Textured path only when a texture is enabled AND its pixmap is actually
   * present. If the pixmap is missing (e.g. the texture image was never
   * populated), fall through to the solid-color path rather than emitting a
   * textured polygon with an empty VRAM base. */
  if (c->texture_2d_enabled && c->current_texture &&
      c->current_texture->images[0].pixmap) {
    tgl_pvr_update_txr_header(c);

    if (pvr_prim(&tgl_pvr_txr_header, sizeof(tgl_pvr_header)) < 0) {
      fprintf(stderr, "TinyGL PVR: texture polygon header submission failed\n");
      return;
    }

    for (i = 0; i < 3; ++i) {
      pvr_vertex_t vertex = { 0 };
      vertex.flags = (i == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
      vertex.x = (float)vertices[i]->zp.x;
      vertex.y = (float)vertices[i]->zp.y;
      vertex.z = (float)vertices[i]->zp.z / (float)(1u << 30);
      if (vertex.z < 0.0f) vertex.z = 0.0f;
      if (vertex.z > 1.0f) vertex.z = 1.0f;
      /* TinyGL only supports GL_DECAL; use white vertex color so the
       * MODULATE texture environment yields the texel exactly. */
      vertex.u = vertices[i]->tex_coord.X;
      vertex.v = vertices[i]->tex_coord.Y;
      vertex.argb = 0xFFFFFFFF;

      if (pvr_prim(&vertex, sizeof(vertex)) < 0) {
        fprintf(stderr, "TinyGL PVR: vertex submission failed\n");
        return;
      }
    }
    return;
  }

  tgl_pvr_update_header(c);

  if (pvr_prim(&tgl_pvr_header, sizeof(tgl_pvr_header)) < 0) {
    fprintf(stderr, "TinyGL PVR: polygon header submission failed\n");
    return;
  }

  for (i = 0; i < 3; ++i) {
    pvr_vertex_t vertex = { 0 };
    uint32_t r = tgl_pvr_color_component(vertices[i]->color.v[0]);
    uint32_t g = tgl_pvr_color_component(vertices[i]->color.v[1]);
    uint32_t b = tgl_pvr_color_component(vertices[i]->color.v[2]);
    uint32_t a = tgl_pvr_color_component(vertices[i]->color.v[3]);

    vertex.flags = (i == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vertex.x = (float)vertices[i]->zp.x;
    vertex.y = (float)vertices[i]->zp.y;
    vertex.z = (float)vertices[i]->zp.z / (float)(1u << 30);
    if (vertex.z < 0.0f) vertex.z = 0.0f;
    if (vertex.z > 1.0f) vertex.z = 1.0f;
    vertex.argb = (a << 24) | (r << 16) | (g << 8) | b;

    if (pvr_prim(&vertex, sizeof(vertex)) < 0) {
      fprintf(stderr, "TinyGL PVR: vertex submission failed\n");
      return;
    }
  }
}

void tgl_pvr_flush(void) {
  if (tgl_pvr_list_active) {
    if (pvr_list_finish() < 0) {
      fprintf(stderr, "TinyGL PVR: pvr_list_finish failed\n");
    }
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
    fprintf(stderr, "TinyGL PVR: pvr_scene_finish failed\n");
  }
  tgl_pvr_scene_active = 0;
}

void tgl_pvr_shutdown(void) {
  if (!tgl_pvr_initialized) return;

  tgl_pvr_flush();
  if (pvr_shutdown() < 0) {
    fprintf(stderr, "TinyGL PVR: pvr_shutdown failed\n");
  }
  tgl_pvr_initialized = 0;
}
