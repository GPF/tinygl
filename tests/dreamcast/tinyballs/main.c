/* TinyBalls: a TinyGL counterpart to SH4ZAM's direct-PVR bruces_balls.
 * The geometry and GL calls are identical in SH4ZAM-off/on builds. */

#include <kos.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "GL/gl.h"
#include "GL/tglprofile.h"

#ifdef TGL_PVR_TEST_DUMP_STRIP_TRI_ALL
/* Diagnostic hook (NEXT_TASKS.md item 8): dump every submitted strip
 * triangle's screen coords for one exact frame instead of guessing a
 * strip-call window. Declared directly (not through a public GL header)
 * since it is a backend test-only symbol, like tgl_pvr_test_arm_fail_next(). */
void tgl_pvr_test_set_dump_frame(int frame);
#endif

#ifndef TINYBALLS_SPHERE_SLICES
#define TINYBALLS_SPHERE_SLICES 20
#endif
#ifndef TINYBALLS_CAPTURE_ROTATIONS
#define TINYBALLS_CAPTURE_ROTATIONS 0
#endif
#ifndef TINYBALLS_SEARCH_DELAY
#define TINYBALLS_SEARCH_DELAY 5
#endif
#ifndef TINYBALLS_DISABLE_LIGHTING
#define TINYBALLS_DISABLE_LIGHTING 0
#endif
#ifndef TINYBALLS_CONTINUOUS_STRIPS
#define TINYBALLS_CONTINUOUS_STRIPS 0
#endif
#ifndef TINYBALLS_INITIAL_BALLS
#define TINYBALLS_INITIAL_BALLS 12
#endif
#ifndef TINYBALLS_EXACT_SEAM
#define TINYBALLS_EXACT_SEAM 0
#endif

enum {
    SPHERE_STACKS = 20,
    SPHERE_SLICES = TINYBALLS_SPHERE_SLICES,
    TRIANGLES_PER_BALL = SPHERE_STACKS * SPHERE_SLICES * 2,
    MAX_BALLS = 12,
    INITIAL_BALLS = TINYBALLS_INITIAL_BALLS,
    BALL_STEP = 2
};
enum { SEARCH_HALVE, SEARCH_INCREASE, SEARCH_REFINE, SEARCH_FINAL };

typedef struct {
    float position[3];
    float normal[3];
} SphereVertex;

static SphereVertex sphere[SPHERE_STACKS + 1][SPHERE_SLICES + 1];
static const float ball_colors[6][3] = {
    { 1.0f, 0.22f, 0.12f }, { 0.12f, 0.68f, 1.0f },
    { 1.0f, 0.70f, 0.14f }, { 0.38f, 0.20f, 1.0f },
    { 0.20f, 1.0f, 0.34f }, { 1.0f, 0.24f, 0.70f }
};

static int ball_count = INITIAL_BALLS;
static int best_ball_count;
static int search_phase = SEARCH_HALVE;
static int refresh_rate = 60;
static int frame_number;
static time_t search_started;
static time_t search_final_at;
static float fps_ema = -1.0f;
static float best_fps;
static uint64_t transform_us;
static uint64_t geometry_us;
static uint64_t frame_us;
static uint32_t profile_frames;
static const float target_fps = 55.0f;
static int screenshots_saved;

static void capture_screenshot(int frame) {
    char path[48];
    snprintf(path, sizeof(path), "/pc/tinyballs_%03d.ppm", frame);
    if (vid_screen_shot(path) < 0) {
        printf("TinyBalls: screenshot failed: %s\n", path);
        return;
    }
    ++screenshots_saved;
    printf("TinyBalls: screenshot saved: %s\n", path);
    fflush(stdout);
}

static int start_pressed(void) {
    maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    cont_state_t *state = controller
        ? (cont_state_t *)maple_dev_status(controller) : NULL;
    return state && (state->buttons & CONT_START);
}

static void build_sphere_mesh(void) {
    const float pi = 3.14159265358979323846f;
    int stack, slice;

    for (stack = 0; stack <= SPHERE_STACKS; ++stack) {
        float latitude = pi * 0.5f - pi * stack / SPHERE_STACKS;
        float ring = cosf(latitude);
        float y = sinf(latitude);
        for (slice = 0; slice <= SPHERE_SLICES; ++slice) {
            float longitude = 2.0f * pi * slice / SPHERE_SLICES;
            SphereVertex *v = &sphere[stack][slice];
#if TINYBALLS_EXACT_SEAM
            if (slice == SPHERE_SLICES) {
                *v = sphere[stack][0];
                continue;
            }
#endif
            v->position[0] = ring * cosf(longitude);
            v->position[1] = y;
            v->position[2] = ring * sinf(longitude);
            v->normal[0] = v->position[0];
            v->normal[1] = v->position[1];
            v->normal[2] = v->position[2];
        }
    }
}

static void setup(void) {
    GLfloat light_ambient[4] = { 0.16f, 0.18f, 0.23f, 1.0f };
    GLfloat light_diffuse[4] = { 0.95f, 0.96f, 1.0f, 1.0f };
    GLfloat light_position[4] = { -0.45f, 0.70f, 1.0f, 0.0f };

    build_sphere_mesh();
#ifdef TINYGL_PROFILE_STAGES
    tgl_profile_clock = timer_us_gettime64;
#endif
    vid_set_mode(DM_640x480_VGA, PM_RGB565);
    if (glInitPVR(640, 480) < 0) {
        printf("TinyBalls: PVR initialization failed\n");
        exit(1);
    }
    refresh_rate = (vid_mode->flags & VID_PAL) ? 50 : 60;

    glViewport(0, 0, 640, 480);
    glClearColor(0.015f, 0.025f, 0.055f, 1.0f);
    glClearDepth(1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.333333, 1.333333, -1.0, 1.0, 1.0, 24.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, light_position);
#if !TINYBALLS_DISABLE_LIGHTING
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
#endif

#ifdef TINYGL_USE_SH4ZAM
    printf("TinyBalls; TinyGL PVR; SH4ZAM enabled\n");
#else
    printf("TinyBalls; TinyGL PVR; SH4ZAM disabled\n");
#endif
    printf("Sphere mesh: %d stacks x %d slices, %d triangles/ball; "
           "55 FPS target, max %d balls, %d Hz\n",
           SPHERE_STACKS, SPHERE_SLICES, TRIANGLES_PER_BALL,
           MAX_BALLS, refresh_rate);
    printf("Lighting: %s\n", TINYBALLS_DISABLE_LIGHTING ? "disabled" : "enabled");
}

static void emit_sphere_vertex(const SphereVertex *v) {
    glNormal3f(v->normal[0], v->normal[1], v->normal[2]);
    glVertex3f(v->position[0], v->position[1], v->position[2]);
}

static void draw_ball(int index) {
    int stack, slice;

    glColor3f(ball_colors[index % 6][0], ball_colors[index % 6][1],
              ball_colors[index % 6][2]);
#if TINYBALLS_CONTINUOUS_STRIPS
    glBegin(GL_TRIANGLE_STRIP);
    for (stack = 0; stack < SPHERE_STACKS; ++stack) {
        int first_slice = 0;
        if (stack > 0) {
            /* The prior band's final upper vertex equals this band's first
             * lower vertex. Repeat that pair to bridge bands with two
             * degenerate triangles while preserving strip parity. */
            emit_sphere_vertex(&sphere[stack][0]);
            emit_sphere_vertex(&sphere[stack + 1][0]);
            first_slice = 1;
        }
        for (slice = first_slice; slice <= SPHERE_SLICES; ++slice) {
            emit_sphere_vertex(&sphere[stack][slice]);
            emit_sphere_vertex(&sphere[stack + 1][slice]);
        }
    }
    glEnd();
#else
    for (stack = 0; stack < SPHERE_STACKS; ++stack) {
        glBegin(GL_TRIANGLE_STRIP);
        for (slice = 0; slice <= SPHERE_SLICES; ++slice) {
            const SphereVertex *lower = &sphere[stack][slice];
            const SphereVertex *upper = &sphere[stack + 1][slice];
            emit_sphere_vertex(lower);
            emit_sphere_vertex(upper);
        }
        glEnd();
    }
#endif
}

static void draw_scene(void) {
    int columns = ball_count < 4 ? ball_count : 4;
    int rows = (ball_count + columns - 1) / columns;
    int i;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (i = 0; i < ball_count; ++i) {
        int row = i / columns;
        int column = i % columns;
        int in_row = ball_count - row * columns;
        float x, y, angle;
        uint64_t start;

        if (in_row > columns) in_row = columns;
        x = ((float)column - ((float)in_row - 1.0f) * 0.5f) * 1.9f;
        y = (((float)rows - 1.0f) * 0.5f - (float)row) * 1.75f;
        angle = (float)(((frame_number % 180) * 2 + i * 37) % 360);

        start = timer_us_gettime64();
        glPushMatrix();
        glTranslatef(x, y, -8.0f);
        glRotatef(angle, 1.0f, 0.0f, 0.0f);
        glRotatef(angle * 0.73f + 11.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(angle * 0.41f + 23.0f, 0.0f, 0.0f, 1.0f);
        glScalef(0.70f, 0.78f, 0.66f);
        transform_us += timer_us_gettime64() - start;

        start = timer_us_gettime64();
        draw_ball(i);
        geometry_us += timer_us_gettime64() - start;
        glPopMatrix();
    }
    ++frame_number;
}

static void reset_window(void) {
    fps_ema = -1.0f;
    transform_us = 0;
    geometry_us = 0;
    frame_us = 0;
    profile_frames = 0;
#ifdef TINYGL_PROFILE_STAGES
    tgl_profile_transform_us = 0;
    tgl_profile_normal_us = 0;
    tgl_profile_light_us = 0;
    tgl_profile_submit_us = 0;
    tgl_profile_viewport_us = 0;
#endif
    search_started = time(NULL);
    printf("Testing %d balls: %d triangles/frame\n", ball_count,
           ball_count * TRIANGLES_PER_BALL);
    fflush(stdout);
}

static void print_window(const pvr_stats_t *stats) {
    double scale = profile_frames ? 1000.0 * profile_frames : 1.0;
    printf("  %.2f fps, %d balls, %.0f triangles/sec; transform %.3f ms, "
           "geometry %.3f ms, frame %.3f ms; PVR registration %.3f ms, "
           "render %.3f ms\n",
           fps_ema, ball_count, ball_count * TRIANGLES_PER_BALL * fps_ema,
           transform_us / scale, geometry_us / scale, frame_us / scale,
           stats->reg_last_time / 1000000.0,
           stats->rnd_last_time / 1000000.0);
#ifdef TINYGL_PROFILE_STAGES
    printf("  stage breakdown/frame: eye+proj xform %.3f ms, normal xform "
           "%.3f ms, lighting %.3f ms, viewport %.3f ms, PVR submit %.3f ms\n",
           tgl_profile_transform_us / scale, tgl_profile_normal_us / scale,
           tgl_profile_light_us / scale, tgl_profile_viewport_us / scale,
           tgl_profile_submit_us / scale);
#endif
}

static void advance_search(const pvr_stats_t *stats) {
    int next_count = ball_count;

    print_window(stats);
    if (search_phase == SEARCH_HALVE) {
        if (fps_ema >= target_fps) {
            best_ball_count = ball_count;
            best_fps = fps_ema;
            search_phase = SEARCH_INCREASE;
            next_count = ball_count + BALL_STEP;
            if (next_count > MAX_BALLS) next_count = MAX_BALLS;
            if (next_count == ball_count) search_phase = SEARCH_FINAL;
        } else if (ball_count > 1) {
            next_count = ball_count / 2;
            if (next_count < 1) next_count = 1;
        } else {
            search_phase = SEARCH_FINAL;
        }
    } else if (search_phase == SEARCH_INCREASE) {
        if (fps_ema >= target_fps) {
            best_ball_count = ball_count;
            best_fps = fps_ema;
            if (ball_count >= MAX_BALLS) {
                search_phase = SEARCH_FINAL;
            } else {
                next_count = ball_count + BALL_STEP;
                if (next_count > MAX_BALLS) next_count = MAX_BALLS;
            }
        } else {
            search_phase = SEARCH_REFINE;
            next_count = best_ball_count + 1;
        }
    } else if (search_phase == SEARCH_REFINE) {
        if (fps_ema >= target_fps) {
            best_ball_count = ball_count;
            best_fps = fps_ema;
        }
        search_phase = SEARCH_FINAL;
    }

    if (search_phase == SEARCH_FINAL) {
        search_final_at = time(NULL);
        if (best_ball_count > 0) {
            ball_count = best_ball_count;
            printf("Final stable load: %d balls, %d triangles/frame, "
                   "%.2f fps, %.0f triangles/sec. Exiting automatically "
                   "(temporary: not waiting for Start during profiling runs).\n",
                   ball_count, ball_count * TRIANGLES_PER_BALL, best_fps,
                   ball_count * TRIANGLES_PER_BALL * best_fps);
        } else {
            printf("55 FPS target not reached at one ball. Exiting "
                   "automatically (temporary: not waiting for Start during "
                   "profiling runs).\n");
        }
        fflush(stdout);
        return;
    }

    ball_count = next_count;
    reset_window();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    setup();
    reset_window();
#ifdef TGL_PVR_TEST_DUMP_STRIP_TRI_ALL
    /* tgl_pvr_flush()'s frame counter advances AFTER each draw_scene()'s
     * draws (at glFlush()), while frame_number here advances at the END of
     * draw_scene() but is only checked against 60/72/84 AFTER glFlush() --
     * so the draws that produce the frame_number==60 capture happen while
     * the backend's counter still reads 59. Target frame is capture - 1. */
    tgl_pvr_test_set_dump_frame(59);
#endif

    /* TODO: restore waiting on start_pressed() once profiling passes are
     * done; auto-exit a few seconds after the final result is printed so
     * runs don't need a controller press. */
    while (!start_pressed() &&
           !(search_phase == SEARCH_FINAL &&
             time(NULL) >= search_final_at + 10)) {
        pvr_stats_t stats = { 0 };
        uint64_t start = timer_us_gettime64();
        draw_scene();
        glFlush();
        /* Let the PVR display a stable scene before grabbing its framebuffer. */
#if TINYBALLS_CAPTURE_ROTATIONS
        if (frame_number == 60 || frame_number == 72 || frame_number == 84)
            capture_screenshot(frame_number);
#else
        if (!screenshots_saved && frame_number >= 60)
            capture_screenshot(frame_number);
#endif
        frame_us += timer_us_gettime64() - start;
        ++profile_frames;

        if (pvr_get_stats(&stats) == 0) {
            if (fps_ema < 0.0f) fps_ema = stats.frame_rate;
            else fps_ema = (fps_ema + stats.frame_rate) * 0.5f;
        }

        if (search_phase != SEARCH_FINAL &&
            time(NULL) >= search_started + TINYBALLS_SEARCH_DELAY) {
            advance_search(&stats);
        }
    }

    glClose();
    return 0;
}
