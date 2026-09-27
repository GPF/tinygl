/* strip_lit_probe: minimal isolated repro for NEXT_TASKS.md item 8 --
 * "extend the native PVR strip path to lit geometry, currently broken".
 *
 * TinyBalls found that dropping vertex.c's `!c->lighting_enabled` strip
 * eligibility gate produced stray thin connecting-line artifacts on some
 * spheres when many separate tgl_pvr_draw_strip() calls (separate rows,
 * separate balls) ran in the same lit scene. This test isolates that to the
 * simplest possible case: a single flat rectangular patch split into two
 * horizontal GL_TRIANGLE_STRIP rows (mirroring how the sphere mesh in
 * tinyballs/main.c is built one row per strip), lit with one directional
 * light so Gouraud colors vary across the seam between the two strips.
 *
 * Build with -DTGL_PVR_TEST_ALLOW_LIT_STRIP (see src/vertex.c) to force the
 * native lit-strip path; without it, the eligibility gate falls back to the
 * known-correct per-triangle path and this test's two runs can be diffed.
 */

#include <kos.h>

#include <math.h>
#include <stdio.h>

#include "GL/gl.h"

#define GRID_COLS 9
#define GRID_ROWS 4

static const float light_ambient[] = { 0.2f, 0.2f, 0.2f, 1.0f };
static const float light_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float light_position[] = { 0.0f, 2.0f, 4.0f, 1.0f };

static void init_gl(void) {
  glShadeModel(GL_SMOOTH);
  glEnable(GL_NORMALIZE);
  glEnable(GL_DEPTH_TEST);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-1.333333, 1.333333, -1.0, 1.0, 1.0, 24.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glLightfv(GL_LIGHT0, GL_AMBIENT, (float *)light_ambient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, (float *)light_diffuse);
  glLightfv(GL_LIGHT0, GL_POSITION, (float *)light_position);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
}

/* A flat patch in the XY plane, normal facing +Z (toward the light/camera),
 * (GRID_ROWS+1) x (GRID_COLS+1) vertices, drawn as GRID_ROWS separate
 * GL_TRIANGLE_STRIP rows -- same structure as tinyballs' sphere mesh. */
static void draw_patch(void) {
  int row, col;
  const float w = 3.0f, h = 1.6f;

  for (row = 0; row < GRID_ROWS; ++row) {
    glBegin(GL_TRIANGLE_STRIP);
    for (col = 0; col <= GRID_COLS; ++col) {
      float u = (float)col / GRID_COLS;
      int r;
      for (r = 0; r < 2; ++r) {
        float v = (float)(row + r) / GRID_ROWS;
        float x = (u - 0.5f) * w;
        float y = (v - 0.5f) * h;
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(x, y, 0.0f);
      }
    }
    glEnd();
  }
}

int main(int argc, char **argv) {
  int frame;

  vid_set_mode(DM_640x480_VGA, PM_RGB565);
  if (glInitPVR(640, 480) < 0) {
    printf("strip_lit_probe: glInitPVR failed\n");
    return 1;
  }

  init_gl();

  for (frame = 0; frame < 30; ++frame) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -6.0f);

    draw_patch();
    glFlush();

    if (frame == 20) {
      if (vid_screen_shot("/pc/strip_lit_probe.ppm") < 0) {
        printf("strip_lit_probe: screenshot failed\n");
      } else {
        printf("strip_lit_probe: screenshot saved\n");
      }
    }
  }

  glClose();
  printf("strip_lit_probe: PASS\n");
  return 0;
}
