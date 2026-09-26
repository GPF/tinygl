#include <kos.h>
#include <dc/biosfont.h>
#include <dc/flashrom.h>
#include <GL/gl.h>
#include <stdio.h>

#define PHASE_FRAMES 150
#define NUM_PHASES 24
#define FRAME_LIMIT (PHASE_FRAMES * NUM_PHASES)

/* Select the requested resolution family. Override on the build command line
 * to validate another size, e.g. -DPVR_SMOKE_VIDEO_MODE=DM_320x240. Startup
 * then picks a supported KOS refresh mode and derives the viewport from it. */
#ifndef PVR_SMOKE_VIDEO_MODE
#  define PVR_SMOKE_VIDEO_MODE DM_640x480
#endif

/* Map KOS display modes to pixel dimensions. vid_set_mode() returns void, so
 * dimensions cannot be read back; this table keeps mode and viewport in sync. */
static int smoke_mode_dim(int dm, int *w, int *h) {
  switch (dm) {
    case DM_320x240:
    case DM_320x240_VGA:
    case DM_320x240_NTSC:
    case DM_320x240_PAL: *w = 320; *h = 240; return 0;
    case DM_640x480:
    case DM_640x480_VGA:
    case DM_640x480_NTSC_IL:
    case DM_640x480_PAL_IL: *w = 640; *h = 480; return 0;
    case DM_256x256:
    case DM_256x256_PAL_IL: *w = 256; *h = 256; return 0;
    case DM_768x480:
    case DM_768x480_NTSC_IL:
    case DM_768x480_PAL_IL: *w = 768; *h = 480; return 0;
    case DM_768x576:
    case DM_768x576_PAL_IL: *w = 768; *h = 576; return 0;
    default: *w = 0; *h = 0; return -1;
  }
}

static int smoke_mode_for_refresh(int width, int use_60hz, int is_vga) {
  if (width == 320) {
    if (!use_60hz) return DM_320x240_PAL;
    return is_vga ? DM_320x240_VGA : DM_320x240_NTSC;
  }
  if (width == 640) {
    if (!use_60hz) return DM_640x480_PAL_IL;
    return is_vga ? DM_640x480_VGA : DM_640x480_NTSC_IL;
  }
  if (width == 768) {
    return use_60hz ? DM_768x480_NTSC_IL : DM_768x576_PAL_IL;
  }
  return -1;
}

/* Offer the 50/60 Hz choice before initializing TinyGL. KOS owns the display
 * timing; TinyGL receives the selected mode's actual dimensions as its
 * viewport. Like the SDL Dreamcast driver, VGA is 60 Hz only. */
static int smoke_choose_video_mode(int requested_mode) {
  int width, height;
  int is_vga = (vid_check_cable() == CT_VGA);
  int region = flashrom_get_region();
  int default_60hz = (region != FLASHROM_REGION_EUROPE);
  int selected_60hz;
  int selected_mode;
  maple_device_t *controller;
  cont_state_t *state;
  int ticks;
  int i;

  if (smoke_mode_dim(requested_mode, &width, &height) < 0) return -1;
  if (width != 320 && width != 640 && width != 768) return requested_mode;

  if (is_vga) {
    return smoke_mode_for_refresh(width, 1, 1);
  }

  /* Draw the choice on a known 640x480 surface before changing the mode. */
  vid_set_mode(default_60hz ? DM_640x480_NTSC_IL : DM_640x480_PAL_IL,
               PM_RGB565);
  for (i = 0; i < 640 * 480; ++i) vram_s[i] = 0;
  bfont_draw_str(vram_s + 640 * 5 + 32, 640, true,
                 "Choose video refresh rate");
  bfont_draw_str(vram_s + 640 * 8 + 32, 640, true,
                 "A: 60 Hz       B: 50 Hz");
  bfont_draw_str(vram_s + 640 * 11 + 32, 640, true,
                 "START: regional default");
  bfont_draw_str(vram_s + 640 * 14 + 32, 640, true,
                 default_60hz ? "Auto-selecting 60 Hz in 5 seconds" :
                                "Auto-selecting 50 Hz in 5 seconds");

  controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
  state = controller ? (cont_state_t *)maple_dev_status(controller) : NULL;
  selected_60hz = default_60hz;
  for (ticks = 0; ticks < 500; ++ticks) {
    uint32_t buttons = state ? state->buttons : 0;
    if (buttons & CONT_A) {
      selected_60hz = 1;
      break;
    }
    if (buttons & CONT_B) {
      selected_60hz = 0;
      break;
    }
    if (buttons & CONT_START) break;
    thd_sleep(10);
  }

  selected_mode = smoke_mode_for_refresh(width, selected_60hz, 0);
  return selected_mode >= 0 ? selected_mode : requested_mode;
}

/* Reset the modelview matrix so phases do not leak transforms into each other.
 * glOrtho is a no-op in this TinyGL version, so transforms use modelview only. */
static void reset_modelview(void) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void draw_baseline_red_triangle(void) {
    /* Baseline: centered solid red triangle (all vertices same color). */
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.5f, -0.5f);
    glVertex2f( 0.5f, -0.5f);
    glVertex2f( 0.0f,  0.5f);
    glEnd();
}

static void draw_gouraud_rgb_triangle(void) {
    /* Per-vertex colors so the PVR Gouraud interpolation is visible. */
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); glVertex2f(-0.5f, -0.4f);
    glColor3f(0.0f, 1.0f, 0.0f); glVertex2f( 0.5f, -0.4f);
    glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.0f,  0.5f);
    glEnd();
}

static void draw_transformed_triangle(void) {
    /* TinyGL modelview transform (translate + rotate) before the PVR. */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.3f, 0.2f, 0.0f);
    glRotatef(45.0f, 0.0f, 0.0f, 1.0f);

    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.3f, -0.3f);
    glVertex2f( 0.3f, -0.3f);
    glVertex2f( 0.0f,  0.3f);
    glEnd();
}

static void draw_edge_crossing(int horizontal, float edge, float color[3]) {
    float x0, y0, x1, y1, x2, y2;

    /* Two vertices cross the indicated edge; the third stays in view.
     * TinyGL clips to the canonical view volume before viewport mapping. */
    if (horizontal) {
        x0 = edge; y0 = -0.2f;
        x1 = edge; y1 =  0.2f;
        x2 = (edge < 0.0f) ? -0.6f : 0.6f; y2 = 0.0f;
    } else {
        x0 = -0.2f; y0 = edge;
        x1 =  0.2f; y1 = edge;
        x2 = 0.0f; y2 = (edge < 0.0f) ? -0.6f : 0.6f;
    }

    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_TRIANGLES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

static void draw_clipping_edges(void) {
    /* Distinct colors identify each triangle. A triangle pokes one vertex past
     * a canonical screen edge and the rest stay on screen.
     * GL canonical Y is flipped by the viewport, so Y<0 is the physical top
     * and Y>0 is the physical bottom. */
    float c_left[3]   = {0.0f, 1.0f, 1.0f};   /* cyan  : crosses left   (x < -1) */
    float c_right[3]  = {1.0f, 0.0f, 1.0f};   /* magenta: crosses right (x >  1) */
    float c_top[3]    = {1.0f, 1.0f, 0.0f};   /* yellow : crosses top    (Y<0 physical top) */
    float c_bottom[3] = {1.0f, 1.0f, 1.0f};   /* white  : crosses bottom (Y>0 physical bottom) */

    draw_edge_crossing(1, -1.3f, c_left);
    draw_edge_crossing(1,  1.3f, c_right);
    draw_edge_crossing(0, -1.3f, c_top);
    draw_edge_crossing(0,  1.3f, c_bottom);
}

/* CCW-wound filled triangle (front face under the default GL_CCW). */
static void draw_ccw_triangle(float color[3]) {
    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.25f, -0.25f);
    glVertex2f( 0.25f, -0.25f);
    glVertex2f( 0.00f,  0.25f);
    glEnd();
}

/* CW-wound filled triangle (reversed vertex order -> back face under GL_CCW). */
static void draw_cw_triangle(float color[3]) {
    glColor3f(color[0], color[1], color[2]);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.25f, -0.25f);
    glVertex2f( 0.00f,  0.25f);
    glVertex2f( 0.25f, -0.25f);
    glEnd();
}

/* Culling reference: culling disabled, both faces draw. */
static void draw_cull_off(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    draw_ccw_triangle((float[]){1.0f, 1.0f, 1.0f}); /* white */
}

/* GL_BACK, CCW wound = front face -> not culled (draws on real GL and PVR). */
static void draw_cull_back_ccw(void) {
    reset_modelview();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    draw_ccw_triangle((float[]){0.0f, 1.0f, 0.0f}); /* green */
}

/* GL_BACK, CW wound = back face culled before PVR submission. */
static void draw_cull_back_cw(void) {
    reset_modelview();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    draw_cw_triangle((float[]){0.0f, 1.0f, 1.0f});  /* cyan */
}

/* GL_FRONT, CCW wound = front face culled before PVR submission. */
static void draw_cull_front_ccw(void) {
    reset_modelview();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    draw_ccw_triangle((float[]){1.0f, 0.0f, 1.0f}); /* magenta */
}

/* GL_FRONT, CW wound = back face -> not culled (draws on real GL and PVR). */
static void draw_cull_front_cw(void) {
    reset_modelview();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    draw_cw_triangle((float[]){1.0f, 1.0f, 0.0f});  /* yellow */
}

/* GL_FRONT_AND_BACK culls polygons with either winding. */
static void draw_cull_front_back(void) {
    reset_modelview();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT_AND_BACK);
    draw_ccw_triangle((float[]){0.5f, 0.5f, 1.0f}); /* CCW light blue */
    draw_cw_triangle((float[]){1.0f, 0.5f, 0.5f});  /* CW light red */
}

/* Polygon mode FILL: triangle is filled (the only working mode on PVR). */
static void draw_poly_fill(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    draw_ccw_triangle((float[]){0.0f, 1.0f, 0.0f}); /* green */
}

/* Polygon mode LINE: no-op on the PVR backend -> blank screen (documented). */
static void draw_poly_line(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    draw_ccw_triangle((float[]){0.0f, 1.0f, 0.0f}); /* green */
}

/* Polygon mode POINT: no-op on the PVR backend -> blank screen (documented). */
static void draw_poly_point(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    draw_ccw_triangle((float[]){0.0f, 1.0f, 1.0f}); /* cyan */
}

static void draw_depth_triangle(float z, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.25f, -0.25f, z);
    glVertex3f( 0.25f, -0.25f, z);
    glVertex3f( 0.00f,  0.25f, z);
    glEnd();
}

static void draw_depth_far_then_near(void) {
    reset_modelview();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    draw_depth_triangle( 0.5f, 1.0f, 0.0f, 0.0f); /* far red */
    draw_depth_triangle(-0.5f, 0.0f, 1.0f, 0.0f); /* near green */
}

static void draw_depth_near_then_far(void) {
    reset_modelview();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    draw_depth_triangle(-0.5f, 0.0f, 1.0f, 0.0f); /* near green */
    draw_depth_triangle( 0.5f, 1.0f, 0.0f, 0.0f); /* far red */
}

static void draw_depth_mask_disabled(void) {
    reset_modelview();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
    draw_depth_triangle(-0.5f, 0.0f, 1.0f, 0.0f); /* near green */
    draw_depth_triangle( 0.5f, 1.0f, 0.0f, 0.0f); /* far red */
}

static void draw_depth_never(void) {
    reset_modelview();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glDepthMask(GL_TRUE);
    draw_depth_triangle(-0.5f, 0.0f, 1.0f, 0.0f);
}

/* Over-blend test: an opaque base triangle with a translucent triangle drawn
 * on top. With GL_SRC_ALPHA * src + GL_ONE_MINUS_SRC_ALPHA * dst the overlap
 * mixes the two colors by the source alpha, so the result is visibly different
 * from either solid color. A smaller alpha leaves more of the red base showing.
 * Depth writes are disabled so the translucent pass does not corrupt depth. */
static void draw_blend_over(float src_alpha) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    /* Opaque red base */
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6f, -0.5f);
    glVertex2f( 0.6f, -0.5f);
    glVertex2f( 0.0f,  0.6f);
    glEnd();

    /* Translucent green on top */
    glColor4f(0.0f, 1.0f, 0.0f, src_alpha);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6f, -0.5f);
    glVertex2f( 0.6f, -0.5f);
    glVertex2f( 0.0f,  0.6f);
    glEnd();
}

/* Same geometry with blending disabled: the green triangle draws fully opaque
 * on top of the red base and covers it entirely (contrast with draw_blend_over). */
static void draw_blend_disabled(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);

    glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6f, -0.5f);
    glVertex2f( 0.6f, -0.5f);
    glVertex2f( 0.0f,  0.6f);
    glEnd();

    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6f, -0.5f);
    glVertex2f( 0.6f, -0.5f);
    glVertex2f( 0.0f,  0.6f);
    glEnd();
}

/* Full-screen validation scene for alternate video modes.
 *
 * A red polygon spanning -1..1 covers the entire display at any resolution,
 * and a blue reference square in the top-left corner spans 0.4 normalized
 * units (~20% of the screen). On hardware, the red polygon must fill the whole
 * screen and the blue square must sit at the top-left at about 20% of the
 * width. A viewport that is too large or too small (or wrong aspect) makes
 * either visibly wrong, so this is the visible check for viewport sizing. */
static void draw_viewport_fullscreen(void) {
    reset_modelview();
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    /* Full-screen red polygon (-1..1 covers the whole display). */
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_POLYGON);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f( 1.0f, -1.0f);
    glVertex2f( 1.0f,  1.0f);
    glVertex2f(-1.0f,  1.0f);
    glEnd();

    /* Top-left reference square (~20% of the screen). */
    glColor3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_POLYGON);
    glVertex2f(-1.0f,  1.0f);
    glVertex2f(-0.6f,  1.0f);
    glVertex2f(-0.6f,  0.6f);
    glVertex2f(-1.0f,  0.6f);
    glEnd();
}

/* 256x256 RGB source textures (TinyGL converts these to RGB565). */
static uint8_t tex_quadrant[256 * 256 * 3];
static uint8_t tex_gradient[256 * 256 * 3];

/* Four solid color quadrants. Unambiguous orientation: each corner is a
 * different color, so any flip/mirror is immediately visible. */
static void build_quadrant_texture(void) {
    for (int ty = 0; ty < 256; ty++) {
        for (int tx = 0; tx < 256; tx++) {
            int idx = (ty * 256 + tx) * 3;
            int q = (tx >= 128) | ((ty >= 128) << 1);
            uint8_t r, g, b;
            switch (q) {
            case 0: r = 255; g = 0;   b = 0;    break; /* top-left  red   */
            case 1: r = 0;   g = 255; b = 0;    break; /* top-right green   */
            case 2: r = 0;   g = 0;   b = 255;  break; /* bottom-left blue  */
            default: r = 255; g = 255; b = 255; break; /* bottom-right white*/
            }
            tex_quadrant[idx] = r;
            tex_quadrant[idx + 1] = g;
            tex_quadrant[idx + 2] = b;
        }
    }
}

/* Horizontal red gradient + vertical green gradient. Smooth interpolation is
 * visible across the triangle; the corner colors reveal orientation. */
static void build_gradient_texture(void) {
    for (int ty = 0; ty < 256; ty++) {
        for (int tx = 0; tx < 256; tx++) {
            int idx = (ty * 256 + tx) * 3;
            tex_gradient[idx] = (uint8_t)tx;        /* R: left -> right */
            tex_gradient[idx + 1] = (uint8_t)ty;    /* G: top  -> bottom*/
            tex_gradient[idx + 2] = 0;
        }
    }
}

static int tex_ready = 0;
static GLuint tex_quad_id, tex_grad_id;

static void setup_textures(void) {
    if (tex_ready) return;
    tex_ready = 1;
    glDisable(GL_TEXTURE_2D);

    glGenTextures(1, &tex_quad_id);
    glBindTexture(GL_TEXTURE_2D, tex_quad_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, tex_quadrant);

    glGenTextures(1, &tex_grad_id);

    glBindTexture(GL_TEXTURE_2D, tex_grad_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, tex_gradient);
}


/* Enable texturing and bind the given texture. Vertex colors are white so the
 * backend's MODULATE texture environment yields the texel exactly. */
static void enable_texture(GLuint id) {
    setup_textures();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, id);
    glColor3f(1.0f, 1.0f, 1.0f);
}

/* Flat-facing quad covering the screen with the quadrant texture. */
static void draw_texture_quadrant_flat(void) {
    enable_texture(tex_quad_id);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-0.7f, -0.7f); /* texture top-left   */
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 0.7f, -0.7f); /* texture top-right  */
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 0.7f,  0.7f); /* texture bottom-right*/
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-0.7f,  0.7f); /* texture bottom-left*/
    glEnd();
}

/* Flat-facing triangle with the gradient texture (smooth interpolation). */
static void draw_texture_gradient_flat(void) {
    enable_texture(tex_grad_id);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-0.6f, -0.6f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 0.6f, -0.6f);
    glTexCoord2f(0.5f, 0.0f); glVertex2f( 0.0f,  0.6f);
    glEnd();
}

/* Same gradient texture under a modelview translate + scale. UV interpolation
 * survives the transform (the transform moves the vertices, not the UVs). */
static void draw_texture_gradient_transform(void) {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.3f, 0.0f, 0.0f);
    glScalef(1.4f, 1.4f, 1.0f);
    enable_texture(tex_grad_id);
    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-0.5f, -0.5f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 0.5f, -0.5f);
    glTexCoord2f(0.5f, 0.0f); glVertex2f( 0.0f,  0.5f);
    glEnd();
}

static void draw_phase(int phase) {
    switch (phase) {
    case 0: draw_baseline_red_triangle();      break; /* FILL baseline */
    case 1: draw_gouraud_rgb_triangle();       break; /* Gouraud color  */
    case 2: draw_transformed_triangle();       break; /* modelview      */
    case 3: draw_clipping_edges();             break; /* clip 4 edges   */
    case 4: draw_cull_off();                   break; /* cull disabled  */
    case 5: draw_cull_back_ccw();              break; /* back, CCW      */
    case 6: draw_cull_back_cw();               break; /* back, CW       */
    case 7: draw_cull_front_ccw();             break; /* front, CCW     */
    case 8: draw_cull_front_cw();              break; /* front, CW      */
    case 9: draw_cull_front_back();            break; /* front+back     */
    case 10: draw_poly_fill();                 break; /* polygon FILL   */
    case 11: draw_poly_line();                 break; /* polygon LINE   */
    case 12: draw_poly_point();                break; /* polygon POINT  */
    case 13: draw_depth_far_then_near();       break; /* GL_LESS        */
    case 14: draw_depth_near_then_far();       break; /* order check    */
    case 15: draw_depth_mask_disabled();       break; /* write mask     */
    case 16: draw_depth_never();               break; /* GL_NEVER       */
    case 17: draw_texture_quadrant_flat();      break; /* texture quad   */
    case 18: draw_texture_gradient_flat();      break; /* interp + orient*/
    case 19: draw_texture_gradient_transform(); break; /* interp + transform */
    case 20: draw_blend_over(0.5f);              break; /* blend SRC_ALPHA .5   */
    case 21: draw_blend_over(0.25f);             break; /* blend SRC_ALPHA .25    */
    case 22: draw_blend_disabled();              break; /* blend disabled         */
    case 23: draw_viewport_fullscreen();         break; /* fullscreen viewport    */
    }
}

static const char *phase_label(int phase) {
    switch (phase) {
    case 0: return "P1 baseline fill";
    case 1: return "P2 gouraud R/G/B";
    case 2: return "P3 transform";
    case 3: return "P4 clipping edges";
    case 4: return "P5 cull disabled";
    case 5: return "P6 cull back + CCW";
    case 6: return "P7 cull back + CW";
    case 7: return "P8 cull front + CCW";
    case 8: return "P9 cull front + CW";
    case 9: return "P10 cull front+back";
    case 10: return "P11 polygon fill";
    case 11: return "P12 polygon line (no-op PVR)";
    case 12: return "P13 polygon point (no-op PVR)";
    case 13: return "P14 depth LESS far then near";
    case 14: return "P15 depth LESS near then far";
    case 15: return "P16 depth writes disabled";
    case 16: return "P17 depth NEVER";
    case 17: return "P18 texture quadrant orientation";
    case 18: return "P19 texture gradient interpolation";
    case 19: return "P20 texture gradient + transform";
    case 20: return "P21 blend SRC_ALPHA .5 over red";
    case 21: return "P22 blend SRC_ALPHA .25 over red";
    case 22: return "P23 blend disabled (opaque green)";
    case 23: return "P24 fullscreen viewport";
    default: return "??";
    }
}

static int capture_phase_screenshot(int phase) {
    char path[64];

    /* kos-tool -m /tmp maps KOS's /pc root to the host's /tmp directory. */
    snprintf(path, sizeof(path), "/pc/tinygl-pvr-phase-%d.ppm", phase + 1);
    if (vid_screen_shot(path) < 0) {
        printf("pvr_smoke: screenshot failed: %s\n", path);
        return -1;
    }

    printf("pvr_smoke: screenshot saved: %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    int frame;
    int current_phase = -1;
    int selected_mode;
    int refresh_hz;

    (void)argc;
    (void)argv;

    int vm_w = 0, vm_h = 0;

    selected_mode = smoke_choose_video_mode(PVR_SMOKE_VIDEO_MODE);
    if (selected_mode < 0 || smoke_mode_dim(selected_mode, &vm_w, &vm_h) != 0) {
        printf("pvr_smoke: unsupported video mode 0x%X\n", PVR_SMOKE_VIDEO_MODE);
        return 1;
    }

    refresh_hz = (selected_mode == DM_320x240_PAL ||
                  selected_mode == DM_640x480_PAL_IL ||
                  selected_mode == DM_256x256_PAL_IL ||
                  selected_mode == DM_768x480_PAL_IL ||
                  selected_mode == DM_768x576_PAL_IL) ? 50 : 60;
    printf("pvr_smoke: setting %dx%d RGB565 %dHz video mode\n",
           vm_w, vm_h, refresh_hz);
    vid_set_mode(selected_mode, PM_RGB565);

    build_quadrant_texture();
    build_gradient_texture();

    /* Derive the viewport from the selected display mode so the two never
     * diverge; a mismatched viewport scales geometry incorrectly. */
    if(glInitPVR(vm_w, vm_h) < 0) {
        printf("pvr_smoke: TinyGL PVR initialization failed\n");
        return 1;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    printf("pvr_smoke: drawing %d phases for %d frames total\n",
           NUM_PHASES, FRAME_LIMIT);

    for(frame = 0; frame < FRAME_LIMIT; ++frame) {
        int phase = frame / PHASE_FRAMES;

        if (phase != current_phase) {
            current_phase = phase;
            reset_modelview();
            glDisable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ZERO);
            glDisable(GL_TEXTURE_2D);
            printf("pvr_smoke: --- %s (frame %d) ---\n",
                   phase_label(phase), frame + 1);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_phase(phase);
        glFlush();

        if ((frame + 1) % PHASE_FRAMES == 0) {
            capture_phase_screenshot(phase);
        }

        if((frame + 1) % 300 == 0) {
            printf("pvr_smoke: frame %d\n", frame + 1);
        }
    }

    glClose();
    printf("pvr_smoke: PASS\n");
    return 0;
}
