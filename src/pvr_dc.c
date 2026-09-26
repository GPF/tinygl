#include "zgl.h"

#include <kos.h>
#include <stdio.h>

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
  pvr_poly_compile(&tgl_pvr_header, &context);

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

void tgl_pvr_draw_triangle(GLVertex *p0, GLVertex *p1, GLVertex *p2) {
  GLVertex *vertices[3];
  int i;

  vertices[0] = p0;
  vertices[1] = p1;
  vertices[2] = p2;

  tgl_pvr_begin_scene();
  if (!tgl_pvr_scene_active) return;

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
    vertex.z = 0.5f;
    vertex.argb = (a << 24) | (r << 16) | (g << 8) | b;

    if (pvr_prim(&vertex, sizeof(vertex)) < 0) {
      fprintf(stderr, "TinyGL PVR: vertex submission failed\n");
      return;
    }
  }
}

void tgl_pvr_flush(void) {
  if (!tgl_pvr_scene_active) return;

  if (tgl_pvr_list_active) {
    if (pvr_list_finish() < 0) {
      fprintf(stderr, "TinyGL PVR: pvr_list_finish failed\n");
    }
    tgl_pvr_list_active = 0;
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
