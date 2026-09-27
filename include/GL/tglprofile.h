/* Optional CPU profiling hooks for TinyGL's immediate-mode pipeline. */
#ifndef _tgl_profile_h_
#define _tgl_profile_h_

#if defined(TINYGL_PROFILE_STAGES) || defined(TINYGL_PROFILE_PVR)

#include <stdint.h>

/* Set by the host app to a monotonic microsecond clock (e.g.
 * timer_us_gettime64 on KOS). Left NULL, no timing is collected. */
extern uint64_t (*tgl_profile_clock)(void);
#endif

#ifdef TINYGL_PROFILE_STAGES
/* Cumulative microseconds since the last reset, split by pipeline stage:
 *   transform - per-vertex eye/projection matrix transform
 *   normal    - per-vertex normal transform + GL_NORMALIZE
 *   light     - per-vertex lighting (gl_shade_vertex)
 *   submit    - per-triangle PVR TA submission (tgl_pvr_draw_triangle)
 *   viewport  - per-vertex clip-space -> viewport mapping (gl_transform_to_viewport) */
extern uint64_t tgl_profile_transform_us;
extern uint64_t tgl_profile_normal_us;
extern uint64_t tgl_profile_light_us;
extern uint64_t tgl_profile_submit_us;
extern uint64_t tgl_profile_viewport_us;
#endif

#ifdef TINYGL_PROFILE_PVR
extern uint64_t tgl_profile_pvr_header_count;
extern uint64_t tgl_profile_pvr_strip_count;
extern uint64_t tgl_profile_pvr_triangle_count;
extern uint64_t tgl_profile_pvr_vertex_count;
extern uint64_t tgl_profile_pvr_sq_batch_count;
extern uint64_t tgl_profile_pvr_pack_us;
extern uint64_t tgl_profile_pvr_copy_us;
extern uint64_t tgl_profile_pvr_strip_us;
extern uint64_t tgl_profile_pvr_triangle_us;
extern uint64_t tgl_profile_pvr_strip_samples;
extern uint64_t tgl_profile_pvr_triangle_samples;
extern uint64_t tgl_profile_pvr_copy_samples;
#endif

#endif
