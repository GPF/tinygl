/* Optional per-stage CPU profiling hooks for the TinyGL immediate-mode
 * pipeline. Only meaningful when both the library and this header are built
 * with TINYGL_PROFILE_STAGES defined; otherwise these symbols do not exist
 * and must not be referenced. Diagnostic only -- no effect on rendering. */
#ifndef _tgl_profile_h_
#define _tgl_profile_h_

#ifdef TINYGL_PROFILE_STAGES

#include <stdint.h>

/* Set by the host app to a monotonic microsecond clock (e.g.
 * timer_us_gettime64 on KOS). Left NULL, no timing is collected. */
extern uint64_t (*tgl_profile_clock)(void);

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

#endif /* TINYGL_PROFILE_STAGES */

#endif
