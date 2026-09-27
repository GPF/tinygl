/* Textured counterpart to ../pvrmark_strips/main.c.
 *
 * pvrmark_strips exercises tgl_pvr_draw_strip()'s batched SQ path but never
 * binds a texture, so it cannot demonstrate the tgl_pvr_set_vertex_tex()
 * dead-color-work removal from src/pvr_dc.c (NEXT_TASKS.md item 10): that
 * change only fires on the `textured` branch of the strip batch loop. This
 * test reuses the same random-walk GL_TRIANGLE_STRIP workload and five-
 * second search, but binds a small checkerboard texture and emits a UV per
 * vertex, so TINYGL_PROFILE_PVR's tgl_profile_pvr_pack_us is measured on the
 * actually-affected code path.
 *
 * Build with TINYGL_PROFILE_PVR (see TESTING.md's pvrmark_strips profiling
 * recipe) to print per-frame PVR pack/copy timing.
 */

#include <kos.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "GL/gl.h"
#include "GL/tglprofile.h"

enum { PHASE_HALVE, PHASE_INCR, PHASE_DECR, PHASE_FINAL };

enum {
    MAX_POLY_COUNT = 3500,
    INITIAL_POLY_COUNT = MAX_POLY_COUNT,
    INCREMENT_POLY_COUNT = 2500,
    DECREMENT_POLY_COUNT = 200
};

enum { TEX_SIZE = 32 };

static int polycnt;
static int phase = PHASE_HALVE;
static time_t test_begin;
static float avgfps = -1.0f;
static uint64_t immediate_build_time;
static uint64_t draw_pipeline_time;
static uint64_t scene_submit_time;
static uint32_t profile_frames;
static int refresh_rate = 60;
static const float target_fps = 55.0f;
static int oldseed = 0xdeadbeef;
static uint8_t tex_checker[TEX_SIZE * TEX_SIZE * 3];

static void running_stats(void) {
    pvr_stats_t stats;

    pvr_get_stats(&stats);
    if (avgfps == -1.0f) avgfps = stats.frame_rate;
    else avgfps = (avgfps + stats.frame_rate) / 2.0f;
}

static inline int getnum(int *seed, int mask) {
    int num = *seed & (mask - 1);
    *seed = *seed * 1164525 + 1013904223;
    return num;
}

static inline void get_vert(int *seed, int *x, int *y, int *col) {
    *x = (*x + (getnum(seed, 64) - 32)) & 1023;
    *y = (*y + (getnum(seed, 64) - 32)) & 511;
    *col = getnum(seed, INT32_MAX);
}

static void build_checker_texture(void) {
    int ty, tx;
    for (ty = 0; ty < TEX_SIZE; ty++) {
        for (tx = 0; tx < TEX_SIZE; tx++) {
            int idx = (ty * TEX_SIZE + tx) * 3;
            uint8_t v = ((tx / 4) ^ (ty / 4)) & 1 ? 255 : 64;
            tex_checker[idx] = v;
            tex_checker[idx + 1] = v;
            tex_checker[idx + 2] = 255 - v;
        }
    }
}

static void setup(void) {
    GLuint tex_id;

    vid_set_mode(DM_640x480_VGA, PM_RGB565);
    if (glInitPVR(640, 480) < 0) {
        printf("TinyGL PVR initialization failed\n");
        exit(1);
    }
#if defined(TINYGL_PROFILE_STAGES) || defined(TINYGL_PROFILE_PVR)
    tgl_profile_clock = timer_us_gettime64;
#endif

    refresh_rate = (vid_mode->flags & VID_PAL) ? 50 : 60;
    printf("TinyGL PVR textured-strip benchmark; display refresh: %d Hz; "
           "target: %.2f fps\n", refresh_rate, target_fps);
    printf("Workload: one textured GL_TRIANGLE_STRIP; search capped at %d "
           "triangles/frame\n", MAX_POLY_COUNT);

    build_checker_texture();
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, TEX_SIZE, TEX_SIZE, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, tex_checker);
    glEnable(GL_TEXTURE_2D);

    glShadeModel(GL_FLAT);
    glColor3f(1.0f, 1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
}

static void do_frame(void) {
    int x = 0;
    int y = 0;
    int z;
    int col;
    int seed = oldseed;
    uint64_t begin_immediate = timer_us_gettime64();
    uint64_t begin_draw;
    uint64_t begin_submit;

    glBegin(GL_TRIANGLE_STRIP);

    get_vert(&seed, &x, &y, &col);
    z = getnum(&seed, 128) + 1;
    glTexCoord2f((float)x / 1023.0f, (float)y / 511.0f);
    glVertex3f((float)x / 320.0f - 1.0f,
               (float)y / 240.0f - 1.0f,
               -(float)z / 128.0f);

    for (int i = 0; i < polycnt; ++i) {
        get_vert(&seed, &x, &y, &col);
        glTexCoord2f((float)x / 1023.0f, (float)y / 511.0f);
        glVertex3f((float)x / 320.0f - 1.0f,
                   (float)y / 240.0f - 1.0f,
                   -(float)z / 128.0f);
    }

    get_vert(&seed, &x, &y, &col);
    glTexCoord2f((float)x / 1023.0f, (float)y / 511.0f);
    glVertex3f((float)x / 320.0f - 1.0f,
               (float)y / 240.0f - 1.0f,
               -(float)z / 128.0f);

    begin_draw = timer_us_gettime64();
    glEnd();
    begin_submit = timer_us_gettime64();
    glFlush();

    immediate_build_time += begin_draw - begin_immediate;
    draw_pipeline_time += begin_submit - begin_draw;
    scene_submit_time += timer_us_gettime64() - begin_submit;
    ++profile_frames;
    oldseed = seed;
}

static void switch_tests(int triangles_per_frame) {
    polycnt = triangles_per_frame;
    avgfps = -1.0f;
    immediate_build_time = 0;
    draw_pipeline_time = 0;
    scene_submit_time = 0;
    profile_frames = 0;
#ifdef TINYGL_PROFILE_PVR
    tgl_profile_pvr_header_count = 0;
    tgl_profile_pvr_strip_count = 0;
    tgl_profile_pvr_triangle_count = 0;
    tgl_profile_pvr_vertex_count = 0;
    tgl_profile_pvr_sq_batch_count = 0;
    tgl_profile_pvr_pack_us = 0;
    tgl_profile_pvr_copy_us = 0;
    tgl_profile_pvr_strip_us = 0;
    tgl_profile_pvr_triangle_us = 0;
    tgl_profile_pvr_strip_samples = 0;
    tgl_profile_pvr_triangle_samples = 0;
    tgl_profile_pvr_copy_samples = 0;
#endif

    printf("Testing %d strip triangles/frame (%d triangles/sec at %d fps)\n",
           polycnt, polycnt * refresh_rate, refresh_rate);
    fflush(stdout);
}

static void print_stats(float avgfps) {
    pvr_stats_t stats;

    if (pvr_get_stats(&stats) == 0) {
        if (stats.rnd_last_time < 1000000000ULL) {
            printf("  PVR last frame: registration %.3f ms, render %.3f ms, "
                   "%zu internal TA vertex bytes\n",
                   stats.reg_last_time / 1000000.0,
                   stats.rnd_last_time / 1000000.0,
                   stats.vtx_buffer_used);
        } else {
            printf("  PVR last frame: registration %.3f ms, render unavailable, "
                   "%zu internal TA vertex bytes\n",
                   stats.reg_last_time / 1000000.0,
                   stats.vtx_buffer_used);
        }
    }
    if (profile_frames) {
        double scale = 1000.0 * profile_frames;
        printf("  TinyGL stages: immediate build %.3f ms, draw pipeline %.3f ms, "
               "scene submit %.3f ms per frame\n",
               immediate_build_time / scale,
               draw_pipeline_time / scale,
               scene_submit_time / scale);
    }
#ifdef TINYGL_PROFILE_PVR
    printf("  PVR sample means: strip pack %.1f us (%.0f strips), copy batch "
           "%.1f us (%.0f copies)\n",
           tgl_profile_pvr_strip_samples ?
               (double)tgl_profile_pvr_pack_us / tgl_profile_pvr_strip_samples : 0.0,
           (double)tgl_profile_pvr_strip_samples,
           tgl_profile_pvr_copy_samples ?
               (double)tgl_profile_pvr_copy_us / tgl_profile_pvr_copy_samples : 0.0,
           (double)tgl_profile_pvr_copy_samples);
#endif
    printf("  Measured %.2f fps, %.0f triangles/sec%s\n", avgfps,
           polycnt * avgfps,
           avgfps >= target_fps ? " (target met)" : "");
}

static void finish_search(float avgfps, const char *reason) {
    printf("Final result: %d strip triangles/frame, %.2f fps, %.0f triangles/sec "
           "(%s). Exiting automatically.\n",
           polycnt, avgfps, polycnt * avgfps, reason);
    phase = PHASE_FINAL;
    fflush(stdout);
}

static void check_switch(void) {
    time_t now;
    int next_count = polycnt;

    if (phase == PHASE_FINAL) return;
    now = time(NULL);
    if (now < test_begin + 5) return;

    print_stats(avgfps);
    test_begin = now;

    switch (phase) {
    case PHASE_HALVE:
        if (avgfps < target_fps && polycnt > 1) {
            next_count = polycnt / 2;
        } else if (avgfps >= target_fps) {
            printf("  Entering PHASE_INCR\n");
            phase = PHASE_INCR;
        } else {
            finish_search(avgfps, "55 fps target not reached at minimum load");
            return;
        }
        break;
    case PHASE_INCR:
        if (avgfps >= target_fps && polycnt < MAX_POLY_COUNT) {
            next_count = polycnt + INCREMENT_POLY_COUNT;
            if (next_count > MAX_POLY_COUNT) next_count = MAX_POLY_COUNT;
        } else if (avgfps >= target_fps) {
            finish_search(avgfps, "search cap reached");
            return;
        } else {
            printf("  Entering PHASE_DECR\n");
            phase = PHASE_DECR;
        }
        break;
    case PHASE_DECR:
        if (avgfps < target_fps && polycnt > DECREMENT_POLY_COUNT) {
            next_count = polycnt - DECREMENT_POLY_COUNT;
        } else {
            finish_search(avgfps, "55 fps threshold found");
            return;
        }
        break;
    case PHASE_FINAL:
        return;
    }

    if (next_count != polycnt) switch_tests(next_count);
    fflush(stdout);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    setup();
#ifdef TINYGL_USE_SH4ZAM
    printf("TinyGL library build: SH4ZAM enabled\n");
#else
    printf("TinyGL library build: SH4ZAM disabled\n");
#endif
    switch_tests(INITIAL_POLY_COUNT);
    test_begin = time(NULL);

    while (phase != PHASE_FINAL) {
        do_frame();
        running_stats();
        check_switch();
    }

    glClose();
    return 0;
}
