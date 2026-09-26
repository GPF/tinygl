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

- Points and lines produce no PVR output.
- GL polygon LINE/POINT modes produce no PVR output (documented no-op; real
  line/point emission would need new primitive submission in `pvr_dc.c`).
- GL culling and polygon FILL are translated to PVR behavior and tested on
  hardware (see `TESTING.md`). `pvr_dc.c::tgl_pvr_flush()` now commits the
  background on no-op frames so culled/no-op phases render black.
- GL depth function, vertex depth, and depth write mask are mapped to PVR state
  and tested on hardware (see `TESTING.md`).
- GL blend modes are mapped to PVR state and hardware-tested, but the
  different per-vertex alpha levels in smoke phases 21/22 produce identical
  pixels; alpha-dependent blending remains unresolved (see `TESTING.md`).
  Unsupported GL blend factors
  (`GL_SRC_COLOR`, `GL_ONE_MINUS_SRC_COLOR`, `GL_SRC_ALPHA_SATURATE`) fall back
  to a source-only factor; non-`GL_FUNC_ADD` blend equations and
  `GL_BLEND_COLOR` are unhandled and documented as unsupported.
- Video-mode and viewport behavior beyond the fixed 640x480 smoke setup has
  not been validated.
- PVR submission errors are logged, but failure recovery and state handling
  have not been exercised.

## Recommended work order

1. ~~Map GL culling and polygon mode state to PVR behavior; add front/back
   winding and fill/line/point mode tests.~~ **DONE (2026-09-26).** Culling
   works via `clip.c` geometry; FILL, LINE (no-op), POINT (no-op), and
   `GL_FRONT_AND_BACK` culling for both windings are verified on hardware in
   `TESTING.md`. Real LINE/POINT PVR emission is still a future item (needs new
   primitive submission in `pvr_dc.c`).
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
4. **Implement and test supported blend modes and alpha handling — IN
   PROGRESS.** TinyGL now tracks blend state
   (`GLContext.blend_enabled/src/dst`, `OP_BlendFunc`, `GLBlendFunc`,
   `glopBlendFunc`, `GL_BLEND` enable) and maps GL factors to `pvr_blend_mode_t`
   in `src/pvr_dc.c`. The solid and textured header caches rebuild on blend
   change and enable vertex-color alpha. Smoke phases 21–23 test over-blend at
   alpha 0.5 and 0.25 and blend-disabled. Built clean and ran on KOS 2.3.0,
   640x480 VGA. Hardware PPMs show blend output, but phases 21 and 22 are
   byte-identical despite diagnostic confirmation that vertex alpha reaches
   the backend as 128 and 64. Fix/test alpha-dependent blending before marking
   this complete. The `glBlendFunc` parameter array overflow and smoke
   frame-limit omission found during verification are fixed. Unsupported GL
   factors and non-`GL_FUNC_ADD` equations are documented as unsupported.
5. Validate initialization and viewport sizing with alternate KOS video modes,
   and improve recovery/reporting for PVR scene/list/primitive submission errors.

Keep each change small and add a visible hardware test scene alongside it.
Inspect vertex flags, color packing, UV/depth mapping, alignment, cache
handling, and PVR state lifetime when output differs. Put repeatable commands
and observed test results in `TESTING.md`; keep implementation direction and
open work here.
