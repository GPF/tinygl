/* TinyGL counterpart to KOS pvrmark_strips_direct and GLdc
 * pvrmark_strips_gldc. It retains the random-walk triangle-strip workload and
 * five-second search, while mapping GLdc's 640x480 ortho coordinates into
 * TinyGL's normalized clip space. */

#include <kos.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "GL/gl.h"

enum { PHASE_HALVE, PHASE_INCR, PHASE_DECR, PHASE_FINAL };

enum {
    /* Raised from the original 3,500 cap: fixing the workload's coordinate
       generation (see the /320,/240 -> /512,/256 change below) made every
       vertex land inside the clip volume, so the strip-eligibility gate in
       src/vertex.c now actually reaches tgl_pvr_draw_strip() every frame
       instead of falling back to per-triangle submission. The native strip
       path is fast enough that 3,500 no longer finds a threshold (the old
       cap was calibrated against fallback-path throughput). */
    MAX_POLY_COUNT = 20000,
    INITIAL_POLY_COUNT = 3500,
    INCREMENT_POLY_COUNT = 4000,
    DECREMENT_POLY_COUNT = 400
};

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

static inline void set_color(int col) {
    const float byte_to_float = 1.0f / 255.0f;
    glColor3f(((col >> 16) & 0xff) * byte_to_float,
              ((col >> 8) & 0xff) * byte_to_float,
              (col & 0xff) * byte_to_float);
}

static void setup(void) {
    vid_set_mode(DM_640x480_VGA, PM_RGB565);
    if (glInitPVR(640, 480) < 0) {
        printf("TinyGL PVR initialization failed\n");
        exit(1);
    }

    refresh_rate = (vid_mode->flags & VID_PAL) ? 50 : 60;
    printf("TinyGL PVR strip benchmark; display refresh: %d Hz; "
           "target: %.2f fps\n", refresh_rate, target_fps);
    printf("Workload: one GL_TRIANGLE_STRIP; search capped at %d triangles/frame\n",
           MAX_POLY_COUNT);

    glShadeModel(GL_FLAT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
}

static void do_frame(void) {
    int x = 0;
    int y = 0;
    int z;
    int col = 0;
    int seed = oldseed;
    uint64_t begin_immediate = timer_us_gettime64();
    uint64_t begin_draw;
    uint64_t begin_submit;

    glBegin(GL_TRIANGLE_STRIP);

    get_vert(&seed, &x, &y, &col);
    z = getnum(&seed, 128) + 1;
    set_color(col);
    glVertex3f((float)x / 512.0f - 1.0f,
               (float)y / 256.0f - 1.0f,
               -(float)z / 128.0f);

    for (int i = 0; i < polycnt; ++i) {
        get_vert(&seed, &x, &y, &col);
        set_color(col);
        glVertex3f((float)x / 512.0f - 1.0f,
                   (float)y / 256.0f - 1.0f,
                   -(float)z / 128.0f);
    }

    get_vert(&seed, &x, &y, &col);
    set_color(col);
    glVertex3f((float)x / 512.0f - 1.0f,
               (float)y / 256.0f - 1.0f,
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

    printf("Testing %d strip triangles/frame (%d triangles/sec at %d fps)\n",
           polycnt, polycnt * refresh_rate, refresh_rate);
    fflush(stdout);
}

static void print_stats(float avgfps) {
    pvr_stats_t stats;

#ifdef PVRMARK_QUIET_STAGE_PRINTF
    /* Diagnostic (NEXT_TASKS.md item 6 correction): suppress the periodic
     * stage-timing prints below to test whether console-print I/O over the
     * live dcload connection is perturbing the timed search loop. Search
     * control flow, pvr_get_stats() calls, and the "Testing"/"Final result"
     * transition prints are unchanged. */
    (void)avgfps;
    return;
#endif
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

#ifdef PVRMARK_FIXED_LOADS
/* Fixed-load mode (NEXT_TASKS.md item 6 redo): the threshold-search loop
 * above stops the instant one noisy 5-second avgfps average crosses the
 * target, which the 2026-09-27 investigation showed produces unstable,
 * non-representative single numbers. This mode instead runs a handful of
 * fixed triangle counts, each for a much longer fixed interval, and reports
 * the settled average FPS and stage timings at each -- enough runs at a
 * fixed load average out the same noise instead of letting it pick the
 * stopping point. Enable with `BENCH_CFLAGS=-DPVRMARK_FIXED_LOADS` (add
 * `-DPVRMARK_FIXED_LOAD_SEC=<n>` to override the default 20s-per-load
 * duration). */
#ifndef PVRMARK_FIXED_LOAD_SEC
#define PVRMARK_FIXED_LOAD_SEC 20
#endif

static const int fixed_loads[] = { 2000, 3000, 4000, 5000 };
enum { FIXED_LOAD_COUNT = sizeof(fixed_loads) / sizeof(fixed_loads[0]) };

int main(int argc, char **argv) {
    int load_index;

    (void)argc;
    (void)argv;

    setup();
#ifdef TINYGL_USE_SH4ZAM
    printf("TinyGL library build: SH4ZAM enabled\n");
#else
    printf("TinyGL library build: SH4ZAM disabled\n");
#endif
    printf("Fixed-load mode: %d loads, %d seconds each\n", FIXED_LOAD_COUNT,
           PVRMARK_FIXED_LOAD_SEC);

    for (load_index = 0; load_index < FIXED_LOAD_COUNT; ++load_index) {
        time_t load_begin;

        switch_tests(fixed_loads[load_index]);
        load_begin = time(NULL);

        while (time(NULL) < load_begin + PVRMARK_FIXED_LOAD_SEC) {
            do_frame();
            running_stats();
        }

        print_stats(avgfps);
        printf("Fixed load %d done: %d tri/frame, %.2f fps, %.0f tri/sec\n",
               load_index, polycnt, avgfps, polycnt * avgfps);
        fflush(stdout);
    }

    printf("All fixed loads complete. Exiting.\n");
    glClose();
    return 0;
}
#else
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
#endif
