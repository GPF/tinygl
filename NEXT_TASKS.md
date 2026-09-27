# Next Tasks

## Completed milestones

- **Gouraud colors / transform / clipping (smoke scene):** Done. The PVR
  smoke test now runs four labeled 150-frame phases (baseline red triangle,
  red/green/blue Gouraud triangle, translate+rotate modelview triangle, and
  four edge-crossing clipping triangles). Built and run on real hardware
  (KOS 2.3.0, 640x480 VGA, `TINYGL_USE_DREAMCAST_PVR=y`,
  `TINYGL_USE_SH4ZAM=y`): loader reported `pvr_smoke: PASS` and
  `Program returned 0`. Only `tests/dreamcast/pvr_smoke/pvr_smoke.c` and
  `TESTING.md` changed; no backend or SDL2 files touched. Backend was already
  correct for these three behaviors (Gouraud header + per-vertex colors,
  modelview transform honored before dispatch, clipping in the canonical
  view volume in `clip.c`), so no `src/` backend change was required. The test
  captures each phase with KOS `vid_screen_shot()` to host `/tmp`; all four
  captures were inspected and showed the expected baseline, color gradient,
  transformed triangle, and clipped edge triangles.

## Current PVR coverage

The hardware-confirmed path initializes/shuts down KOS PVR, updates the PVR
background clear color, flushes scenes, and draws filled triangles with
Gouraud colors, depth, and RGB565 textures. TinyGL transforms and clips
triangle vertices before submission. The smoke test captures baseline,
transformed, clipped, culling, polygon-mode, depth, and texture phases on
hardware.

The backend currently has these known gaps:

- Points, lines, and GL polygon LINE/POINT modes now emit PVR triangle strips
  in `src/pvr_dc.c`. The one-pixel point/line paths, clipped direct lines,
  line loops, and polygon modes are hardware-tested in `TESTING.md`. Configurable
  `glPointSize`/`glLineWidth` state is not part of TinyGL's current API.
- GL culling and polygon FILL are translated to PVR behavior and tested on
  hardware (see `TESTING.md`). `pvr_dc.c::tgl_pvr_flush()` now commits the
  background on no-op frames so culled/no-op phases render black.
- GL depth function, vertex depth, and depth write mask are mapped to PVR state
  and tested on hardware (see `TESTING.md`).
- GL blend modes and per-vertex alpha are hardware-tested for the supported
  SRC_ALPHA/ONE_MINUS_SRC_ALPHA case (see `TESTING.md`).
  Unsupported GL blend factors
  (`GL_SRC_COLOR`, `GL_ONE_MINUS_SRC_COLOR`, `GL_SRC_ALPHA_SATURATE`) fall back
  to a source-only factor; non-`GL_FUNC_ADD` blend equations and
  `GL_BLEND_COLOR` are unhandled and documented as unsupported.
- Video-mode/viewport sizing is hardware-tested at 320x240, 640x480, and
  768x480. The 768x576 PAL mode was selected but produced black captures on the
  NTSC/VGA test console; visible PAL output and the non-VGA refresh picker still
  need PAL/non-VGA hardware.
- PVR submission-error reporting and recovery are hardware-tested with the
  build-time one-shot simulated failure. The injected header failure was
  followed by successful rendering through all 24 smoke phases and exactly one
  submission error at shutdown (see `TESTING.md`). Real KOS-generated failure
  behavior is not exercised; the hook simulates the `pvr_prim()` error return.

TinyGL's core PVR milestones are implemented. GLdc sample compatibility has
started with the `tests/dreamcast/nehe06/` port. Its first texture run exposed
a real backend issue: the PVR header expected twiddled RGB565 data, but the
upload path only copied linear pixels. The backend now uses KOS
`pvr_txr_load_ex()` for twiddling. The sample's texture is visually verified
in Flycast and on physical Dreamcast hardware.

The current performance work has two complementary targets. The strip test at
`tests/dreamcast/pvrmark_strips/` measures backend submission; eligible
unlit `GL_TRIANGLE_STRIP` blocks use the native PVR strip path. The math test
at `tests/dreamcast/tinyballs/` is a TinyGL counterpart to SH4ZAM's direct-PVR
`bruces_balls`: rotating, lit spheres exercise transforms, normals, and
`GL_NORMALIZE` identically in SH4ZAM-off/on builds. The Dreamcast rotation
breakup was traced to zero PVR OPB overflow blocks; using KOS's default of
three fixed the missing patches, confirmed in a six-sphere hardware capture.
Lit strips remain on TinyGL's per-triangle fallback. The first post-fix off/on runs both reached two spheres at the 55 FPS target
(off 60.09 FPS, on 56.10 FPS). SH4ZAM reduced the transform interval from
0.070 ms to 0.036 ms, while the one-run overall FPS did not improve. Repeat
alternating variants before drawing a performance conclusion or comparing
object counts with `bruces_balls`. The test captures `/pc/tinyballs.ppm` after
60 frames. Build steps and results are in `TESTING.md`.

## Recommended work order

1. ~~Map GL culling and polygon mode state to PVR behavior; add front/back
   winding and fill/line/point mode tests.~~ **DONE (2026-09-26).** Culling
   works via `clip.c` geometry; FILL, LINE, POINT, and `GL_FRONT_AND_BACK`
   culling for both windings are verified on hardware in `TESTING.md`. PVR
   point/line emission uses 1x1 and one-pixel-wide triangle strips; direct
   points, direct lines, clipped lines, line loops, and triangle polygon modes
   pass the 640x480 smoke test.
2. ~~Implement GL depth state: map transformed Z to PVR Z, honor depth
   function and depth mask, and test overlapping triangles in both draw
   orders.~~ **DONE (2026-09-26).** Four visible cases passed on real hardware:
   LESS in both draw orders, disabled depth writes, and NEVER. See `TESTING.md`.
3. ~~Textured-triangle submission.~~ **DONE (2026-09-26).**
   The PVR backend now submits textured triangles (`src/pvr_dc.c`:
   `tgl_pvr_texture_load`, `tgl_pvr_update_txr_header`, textured branch of
   `tgl_pvr_draw_triangle`), and `src/clip.c::gl_draw_triangle_fill` routes
   all triangles to the backend. TinyGL textures are 256x256 RGB565 pixmaps in
   system RAM (see `texture.c`); the backend caches a twiddled VRAM copy keyed
   by pixmap pointer and emits `u/v` from `tex_coord.X/Y` with a white vertex
   color (GL_DECAL). Because `pvr_poly_cxt_txr` hardcodes depth/culling, the
   compiled txr header is overridden post-compile (matching the color path).
   - A defensive guard was added: if the texture's `pixmap` is NULL at draw
     time, the triangle falls back to the solid-color path instead of emitting
     a textured polygon with an empty VRAM base.
   - Hardware-confirmed on KOS 2.3.0 at 640x480 VGA: phase 18 quadrant colors
     and phases 19–20 gradients were captured and parsed from the PPMs.
   - A first rerun showed phases 19–20 reusing the quadrant texture. Root cause:
     the PVR polygon-header cache did not track the active texture pixmap. The
     header cache now rebuilds when that pointer changes; after the fix,
     phases 19–20 each contained over 1,300 colors and phase 20 was translated.
   - An earlier diagnostic reported a NULL pixmap, but it was not reproduced
     in the successful run; the new captures prove the tested texture images
     reached the PVR.
   - **Unsupported TinyGL texture behaviors to document/avoid:** non-
     RGB format, non-256 size, non-REPEAT wrap, GL_MODULATE env, and
     mipmaps are all `gl_fatal_error`/unhandled in TinyGL. The smoke test uses
     only RGB 256x256 REPEAT, which is supported.
4. ~~Implement and test supported blend modes and alpha handling.~~ **DONE
   (2026-09-26).** TinyGL tracks blend state
   (`GLContext.blend_enabled/src/dst`, `OP_BlendFunc`, `GLBlendFunc`,
   `glopBlendFunc`, `GL_BLEND` enable) and maps GL factors to `pvr_blend_mode_t`
   in `src/pvr_dc.c`. The solid and textured header caches rebuild on blend
   change and enable vertex-color alpha. Smoke phases 21–23 test alpha 0.5,
   alpha 0.25, and blending disabled. Hardware captures confirm distinct
   outputs: P21 center `(128,128,0)`, P22 `(192,64,0)`, P23 `(0,252,0)`;
   P17 depth NEVER remains black. The root cause was using translucent
   accumulation-buffer enable flags as ordinary blend enables, combined with
   submitting blended primitives through the opaque list. Those flags are
   now off; opaque geometry uses OP and the scene transitions once to TR when
   blending begins. The `glBlendFunc` parameter-array overflow and smoke
   frame-limit omission found during verification are fixed. Unsupported GL
   factors and non-`GL_FUNC_ADD` equations remain documented as unsupported.
5. ~~Validate initialization/viewport sizing across KOS modes and improve PVR
   submission error reporting/recovery.~~ **IMPLEMENTED; PAL CUSTOMER CHECK OPTIONAL.**
   - Video modes/viewport (`tests/dreamcast/pvr_smoke/pvr_smoke.c`): the
     smoke/demo startup now offers a 5-second on-screen A=60Hz / B=50Hz picker
     on non-VGA cables, with Start/timeout using the flashrom-region default.
     VGA selects 60Hz directly, matching the SDL Dreamcast driver's policy.
     `PVR_SMOKE_VIDEO_MODE` remains the build-time requested resolution family
     (default `DM_640x480`), and `smoke_mode_dim()` maps the chosen KOS mode to
     its pixel size. The viewport is passed to `glInitPVR`
     from the mode dimensions, keeping the two in sync (the old 640x480 is
     the default, so prior phases are unchanged). New phase 24 draws a
     full-screen red polygon plus a top-left blue ~20% reference square as the
     visible viewport-sizing check. Alternate modes are selectable via
     `make SMOKE_CFLAGS="-DPVR_SMOKE_VIDEO_MODE=DM_320x240"` (the smoke
     `Makefile` forwards `$(SMOKE_CFLAGS)` on `CFLAGS`, which the `kos-cc`
     wrapper passes to the compiler). Built clean for 320x240, 640x480,
     768x480, and 768x576. The PAL picker path is not exercised by this
     NTSC/VGA setup.
   - Submission errors (`src/pvr_dc.c`): a `tgl_pvr_submission_errors` counter
     is incremented at every failure site (init, list begin, list transition,
     polygon-header/vertex submissions, list/scene finish) and a session
     summary prints at shutdown. The list-transition failure now also resets
     `tgl_pvr_list_type` to a sentinel so a failed transition cannot leave a
     stale list type driving the next draw; `tgl_pvr_flush()` still finishes
     the half-open list so the next scene begins fresh.
   - **Hardware results (KOS 2.3.0, VGA cable):** full 24-phase runs at
     320x240 and 640x480 passed with no submission-error messages. Phase 24
     captures have the expected red field and top-left blue square (about 20%
     width); rasterization leaves a one-pixel black edge at the bottom/right.
     The 768x480 NTSC mode also passed; its P24 capture is 768x480 with a
     153x95 blue square at the top-left and a one-pixel bottom/right black
     edge. The 768x576 PAL mode returned PASS but its P1 and P24 captures are
     entirely black on this NTSC/VGA console. This is expected when the display
     cannot sync to PAL timing, not evidence of a PVR backend defect. Leave
     confirmation on PAL-compatible hardware as a future customer test. The
     default 640x480 ELF is built.
   - **Submission errors:** all four hardware runs (320x240,
     640x480, 768x480, and 768x576) completed without submission-error
     messages. The VGA startup path selected 60Hz directly. The non-VGA picker
     still needs a real-hardware check. Forced-failure behavior is now covered
     by a build-time one-shot injection test (see
     "PVR submission-error one-shot failure injection test" in
     `TESTING.md`): a `TGL_PVR_TEST_INJECT_FAIL`-gated wrapper in
     `src/pvr_dc.c` simulates one KOS `pvr_prim()` returning `-1` on the solid
     polygon-header path, exercises the `tgl_pvr_submission_errors` counting,
     half-open list cleanup, and fresh-scene recovery, and reports exactly
     `1 submission error(s)` at shutdown. The injection is a **simulated** KOS
     error contract (no PVR command queue is touched, so the console cannot
     hang); it is not a hardware-generated failure and is disabled in normal
     builds. Run the **test** ELF on real non-VGA hardware to confirm the
     recovery path.
   - **Flycast picker check:** the non-VGA picker was exercised in Flycast and
     selected `DM_320x240_PAL` (50 Hz), confirming the 50 Hz choice reaches
     `vid_set_mode()`. Flycast reported `flashrom_get_region: unknown code
     '00001'`; this emulator region code is not recognized, so its regional
     default is not a reliable test. KOS could not create the `/pc/...` PPM
     screenshots, so this run verifies mode selection from the log, not visible
     rendering. Real non-VGA hardware validation remains useful.
6. **Complete the PVR triangle-strip performance comparison.** The TinyGL
   port is in `tests/dreamcast/pvrmark_strips/`; repeat its hardware A/B runs
   with SH4ZAM enabled and disabled, then run KOS's unmodified
   `pvrmark_strips_direct` and compare threshold FPS/stage timings with TinyGL
   and GLdc's `pvrmark_strips_gldc`. Profile before expanding SH4ZAM use.
   A gated native PVR strip path is implemented for eligible `GL_TRIANGLE_STRIP`
   blocks. It buffers transformed vertices until `glEnd`; fill, unlit, uncullled,
   unclipped strips use one PVR header and one EOL, while ineligible strips
   replay the original parity-ordered triangle route. Ordinary `GL_TRIANGLES`
   behavior is unchanged. See `docs/strip_fast_path_investigation.md` for the
   design and hardware validation matrix. Real Dreamcast pixel comparison and
   matched benchmark runs remain pending; do not claim a performance gain yet.
7. **Repeat the TinyBalls SH4ZAM performance comparison.** The rotation
   breakup was fixed by enabling KOS's default three PVR OPB overflow blocks;
   six-sphere captures are visually correct in both variants. The first off/on
   runs held two spheres at 60.09 / 56.10 FPS, despite about half the transform
   time with SH4ZAM. Collect at least five alternating runs, compare medians,
   and keep `bruces_balls` as a separate direct-PVR reference.

8. **Extend the native PVR strip path (`tgl_pvr_draw_strip`) to lit
   geometry -- currently broken, root cause still open.** TinyBalls
   profiling (`TINYGL_PROFILE_STAGES`, see
   `docs/dreamcast_gl_reference_notes.md`) found PVR submit is the largest
   untimed-savings bucket (~40ms/frame at 12 balls). `glopEnd()`'s strip
   eligibility gate (`src/vertex.c`) excludes `lighting_enabled` --
   `docs/strip_fast_path_investigation.md` says this was deliberately
   deferred ("out of slice 1"), not disqualified for a correctness reason,
   and `GLVertex.color` is already fully lit by the time `glopEnd()` runs
   (`gl_shade_vertex()` writes it in `glopVertex`, before `glopEnd`).

   The gate is now toggleable for investigation only:
   `src/vertex.c`'s eligibility check drops `!c->lighting_enabled` when
   built with `-DTGL_PVR_TEST_ALLOW_LIT_STRIP` (same pattern as
   `TGL_PVR_TEST_INJECT_FAIL`); normal builds are unaffected and the gate
   stays on by default. Confirmed on hardware (2026-09-27, KOS 2.3.0,
   640x480 VGA):

   - **The performance win is real and large.** With the gate forced open,
     TinyBalls' 12-ball geometry-submit time dropped from 88.9 ms to
     62.6 ms/frame (~30% faster), and the FPS-search settled at 3 stable
     spheres at 60.09 FPS instead of 2 -- a genuine improvement, not noise.
   - **The artifact reproduces exactly as previously reported:** a thin
     line juts out to the right of 3 of 12 spheres (captured at
     `/tmp/tinyballs_litstrip_12balls.ppm` in this session; not committed).
     It sits at roughly equator height, does not connect to any other
     sphere, and terminates in open space a short, fixed distance from the
     sphere surface -- consistent with one real triangle in the mesh
     having a single mis-positioned vertex (a Gouraud-shaded sliver, not a
     rendering/list-transition glitch, since colors interpolate correctly
     along it).
   - **Batch-copy details are not the cause.** The 14-slice single-batch
     test produced larger missing wedges, so it was inconclusive about the
     original line. At 20 slices, increasing the batch size to 64 (one
     `sq_fast_cpy()` for all 42 vertices) still reproduced the line.
     Zero-initializing the temporary PVR vertex array and submitting every
     vertex individually with `pvr_prim()` also made no difference.
   - **Lighting and multiple objects are not required.** With lighting
     disabled and one sphere only, the line still appeared in a rotation
     capture. An exact longitude seam copy did not help; joining latitude
     bands into one strip made the artifacts worse.
   - **No suspicious source triangle was found in the captured frame.** A
     temporary logger checked adjacent screen-coordinate jumps and triangles
     with absolute doubled area <=64 and an edge longer than 10 pixels. It
     printed no matches for the captured rotation. This weakens the stale
     or malformed input-vertex theory, but does not rule out a PVR-side
     interpretation issue. The per-triangle fallback remains visually clean.
   - **Triangle-list diagnostic (2026-09-27, KOS 2.3.0, 640x480 VGA,
     1 ball, lighting disabled, `TGL_PVR_TEST_STRIP_AS_TRIANGLES`):**
     `tgl_pvr_draw_strip()` (`src/pvr_dc.c`) now has a build-time diagnostic
     that submits the same prevalidated strip vertices, under the identical
     header/list/blend state, as standalone EOL-terminated triangles with
     strip-matching winding parity, instead of one continuous TA strip.
     The artifact **did not disappear** -- it reproduced as two thin
     horizontal lines flanking the sphere near the equator (capture at
     `/tmp/claude-1000/.../scratchpad/striptri_060_crop.png` in this
     session; not committed), versus the single line previously reported
     for the native-strip path. This weakens the "PVR strip topology/raster
     interpretation" hypothesis: the same lines/near-lines appear even when
     the hardware never sees a multi-vertex strip primitive. It does not
     yet rule strip topology out cleanly, because the diagnostic's
     triangle-order parity swap (reversing every other triangle to match
     strip winding) is unverified against the original strip's exact vertex
     order and could itself be introducing a second, distinct sliver.
   - **Parity-swap re-test (2026-09-27):** re-ran the triangle-list
     diagnostic with the winding-parity swap removed
     (`TGL_PVR_TEST_STRIP_NO_PARITY_SWAP`, submitting `i, i+1, i+2`
     unconditionally). The result was pixel-identical to the parity-swapped
     version (same two lines, same position), so the diagnostic's own
     winding logic is not the cause of the double-line difference from the
     native-strip capture -- something about triangle-list *decomposition
     itself* (breaking one continuous strip into independent 3-vertex
     primitives) changes the render versus native strip mode, independent
     of vertex order. Re-running the **native** strip path (no
     decomposition) with a full 12-ball frame reproduced the original
     single-line-per-sphere symptom, confirming the double-line was an
     artifact of the decomposition diagnostic, not a second real bug.
   - **Exact-frame vertex dump (2026-09-27):** the `-DTGL_PVR_TEST_DUMP_STRIP`
     window-based capture (strip_id 1180-1199) never contained a match
     because that window's strip-id-to-frame arithmetic was a guess, not a
     real frame number. Replaced it with a real hook:
     `tgl_pvr_test_set_dump_frame(int)` (declared in `src/zgl.h`, gated by
     `-DTGL_PVR_TEST_DUMP_STRIP_TRI_ALL`) arms an exact target against a
     counter `tgl_pvr_flush()` advances once per application frame; the
     test caller (`tests/dreamcast/tinyballs/main.c`) arms it once at
     startup. This requires `-DTGL_PVR_TEST_ALLOW_LIT_STRIP` too, since
     TinyBalls' default build has lighting **enabled** and the strip gate
     otherwise routes every triangle through the unaffected per-triangle
     fallback (a first attempt without that flag produced zero native-strip
     calls at all -- confirmed via a temporary per-flush counter printout).
   - **Result: the line's far endpoint has no corresponding submitted
     vertex anywhere in the dumped frame.** With the hook correctly armed,
     a 12-ball, frame-60 hardware capture (`/tmp/claude-1000/.../scratchpad/
     exact_060.png`, not committed) was cross-referenced against all 9600
     dumped triangle vertices (12 balls x 800 tri) for that exact frame. The
     visible line on the top-right (purple) sphere runs from about
     (429,177) to (449,178); the dump does contain a real, expected
     degenerate cluster at the near end (the sphere's own pole vertices, all
     collapsed to ~(429,177), as expected for a UV-sphere pole stack) but
     **no vertex anywhere in the frame's data falls in x=[440,455],
     y=[172,184]** -- the far tip of the visible line does not correspond to
     any triangle TinyGL submitted that frame. The dumped mesh's overall
     bounding box (x:[209,430], y:[162,316]) does not even reach the line's
     far endpoint.
   - **This rules out a bad mesh vertex as the cause.** The line cannot be
     one real (if misplaced) vertex from this frame's geometry --  it has no
     source vertex in the CPU-side data at all. The leading hypothesis is
     now PVR-hardware-side state leakage across separate `tgl_pvr_draw_strip()`
     calls or ball boundaries (e.g. a stale SQ/TA-input value from a
     previous, unrelated primitive being interpreted as part of a strip),
     not a geometry/mesh-generation bug.
   - **Next:** log submission order/timing (ball index, stack index, and a
     monotonic submission counter) immediately around list-transition and
     per-ball boundaries to check whether the stray line's near endpoint
     ((429,177), the pole cluster) is being connected in hardware to a
     *leftover* vertex from a prior, unrelated draw call rather than to
     anything in the current one. Also worth checking whether `EOL` is
     reliably reaching the hardware at every stack-strip boundary.

   Do not re-enable this gate for non-diagnostic builds until a clean
   hardware screenshot is obtained across the full `SPHERE_SLICES` range
   used by TinyBalls (20).
9. ~~`sq_fast_cpy` vertex-submission batching for textured triangles.~~
   **DONE (2026-09-27).** `tgl_pvr_draw_triangle`'s untextured path and
   `tgl_pvr_draw_strip` already built their `pvr_vertex_t` entries into a
   local array and pushed them in one `sq_fast_cpy()` call instead of one
   `pvr_prim()` per vertex (pattern from DCSinge's `src/dcfmv.c` FMV quad
   submission); measured ~5% PVR-submit reduction on hardware (TinyBalls),
   screenshot-verified correct. The textured branch of
   `tgl_pvr_draw_triangle` now uses the same batching: it builds all 3
   `pvr_vertex_t` entries (still one `pvr_prim()` header submission first,
   since the header is a different, non-vertex TA command) and pushes them
   with a single `sq_fast_cpy()`. Builds clean with
   `TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y`; the `pvr_smoke` ELF
   (which exercises textured phases 18-20) links successfully.
   Hardware-confirmed on KOS 2.3.0, 640x480 VGA (2026-09-27): all 24 phases
   passed, and P18-P20 non-black pixel counts matched the pre-batching
   baseline exactly (150,528 / 55,296 / 75,264), confirming the batched
   submission places vertices identically. See `TESTING.md`.

Keep each change small and add a visible hardware test scene alongside it.
Inspect vertex flags, color packing, UV/depth mapping, alignment, cache
handling, and PVR state lifetime when output differs. Put repeatable commands
and observed test results in `TESTING.md`; keep implementation direction and
open work here.
