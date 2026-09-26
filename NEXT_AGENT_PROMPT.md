# Prompt for the Next TinyGL Dreamcast PVR Agent

You are taking over a small, early Dreamcast PVR backend integration in TinyGL.
Work carefully: this is real Dreamcast hardware code, and a successful build or
emulator image does not prove the PVR path is correct.

## Objective

Work-order item 5 is implemented. The smoke/demo startup now offers A=60Hz and
B=50Hz on non-VGA outputs, using the flashrom region as the default; VGA follows
the SDL policy and selects 60Hz directly. Viewport captures and normal-run
submission reporting pass at 320x240, 640x480, and 768x480 on NTSC/VGA hardware.
`DM_768x576` captured black on this NTSC/VGA console, which is expected when
the display cannot sync to PAL timing and is not evidence of a backend defect.
PAL-compatible hardware confirmation is an optional future customer test.
Flycast did not provide a usable screenshot in this run. The non-VGA picker
path and forced PVR failure recovery remain unverified. Blend/alpha results are
in `TESTING.md`.

The existing smoke test covers solid fill, Gouraud colors, a modelview
transform, clipping, culling, polygon-mode behavior, depth state, and texture
upload/interpolation. The Dreamcast run saves a PPM screenshot for each phase,
so inspect captures while validating modes and viewports. Make the smallest safe
changes.

## Read these first

1. Read `AGENTS.md` for project rules and the SDL2 reference-only boundary.
2. Read `NEXT_TASKS.md` for the current backend gaps and roadmap.
3. Read `TESTING.md` for the KOS build and dc-load procedure.
4. Inspect the smoke test and the backend/dispatch code that it exercises:
   - `tests/dreamcast/pvr_smoke/pvr_smoke.c`
   - `tests/dreamcast/pvr_smoke/Makefile`
   - `src/pvr_dc.c`
   - `src/clip.c`
   - `src/init.c`
   - texture API and rasterization files identified by targeted searches

Read targeted sections only. Before opening additional files, state what
question that read is meant to answer. Do not repeatedly scan the whole repo.

## Known starting point

- `glInitPVR(width, height)` initializes the PVR backend.
- TinyGL performs its ordinary transform and clip path before dispatching
  filled triangles to `tgl_pvr_draw_triangle`.
- `src/pvr_dc.c` emits screen-space X/Y, transformed depth, per-vertex ARGB,
  and textured UVs. GL depth function/write mask and RGB565 texture upload are
  hardware-covered by phases 14–20.
- Points and lines are currently no-ops on the PVR path.
- The 23-phase smoke test includes baseline, culling, polygon mode, depth,
  texture, and blend cases. KOS saves PPMs as
  `/tmp/tinygl-pvr-phase-N.ppm` on the host running `kos-tool`.
- The latest run reported `pvr_smoke: PASS` and `Program returned 0`. Parsed
  pixel results confirmed phases 14–17 for depth and 18–20 for textures. The
  texture header cache tracks pixmap changes; phases 19–20 show over 1,300
  colors after the fix. Blend phases 21 and 22 have identical captures despite
  alpha 128 vs 64 reaching PVR; phase 23 is opaque green. Blend alpha remains
  under investigation.
- The target environment used KOS 2.3.0, 640x480 VGA, `kos-cc`, and the local
  KOS port of SH4ZAM. The console is available through dc-load-ip at
  `192.168.0.128` (verify that the address is still right before loading).
- The SDL2 Dreamcast directories are reference-only. Do not edit anything in
  `/home/gpf/code/dreamcast/SDL2/`.

## Investigation requirements

Before editing, trace per-vertex alpha from `glColor4f` through `GLVertex` and
PVR `argb`, and inspect the compiled PVR blend header/list semantics.
Confirm the smoke target links the TinyGL source/library you are inspecting.
Use installed KOS blend documentation/source; do not assume modern OpenGL or
PVR APIs. Keep facts and hypotheses distinct.

Do not infer correctness from a successful KOS call: capture and inspect the
resulting PPM images. Preserve existing smoke phases and compare exact pixels
for phases 21–23 while debugging alpha behavior.

## Implementation requirements

- Extend the existing smoke scene in clear, labeled phases that are easy to
  identify in screenshots. Keep the baseline, color, transform, and clipping
  phases intact.
- Keep the blend cases visible and vary alpha enough to distinguish P21 and P22
  pixels.
- Avoid adding a test framework or broad refactor. Keep the scene readable and
  use short labels over serial output to identify phases if the test structure
  supports that.
- Update `TESTING.md` only with repeatable build/run steps and observed results.
  Update `NEXT_TASKS.md` if this milestone changes the remaining work. Do not
  place general architecture notes or roadmap material in `TESTING.md`.
- If the investigation finds a backend defect needed for these cases, make the
  smallest targeted fix and explain the evidence. Do not silently expand scope.

## Build and hardware validation

Use the KOS environment and the documented target. The known-good build
sequence is:

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= \
  TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke
```

Load with `kos-tool -m /tmp -t "$DC_IP" -x
tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf` after setting `DC_IP` to the
Dreamcast's current dc-load-ip address. The `-m /tmp` option maps target
`/pc` to host `/tmp`, where KOS screenshots appear as
`tinygl-pvr-phase-N.ppm`. Open and inspect each new screenshot. `kos-tool -d`
dumps target memory; it is not the screenshot/file-download shortcut.
Do not claim hardware pixel success from compilation or loader logs alone.

If the console is unavailable, complete the implementation and build, clearly
mark hardware validation as pending, and give the exact command needed to run
it. Do not claim a mode or viewport passed without inspecting its captured pixels.

Blend milestone (work-order item 4) is complete and hardware-verified. P21
(alpha 0.5) captures center `(128,128,0)` and P22 (alpha 0.25) captures
`(192,64,0)`; P23 is opaque green `(0,252,0)`, and P17 (`GL_NEVER`) remains
black. The root cause was KOS accumulation-buffer enable bits being used as
ordinary blend enables, plus blend primitives submitted through OP. Those bits
are zero; opaque geometry uses OP and the backend transitions once to TR when
blending starts. Continue with a repeatable submission-error recovery test. If
PAL-compatible hardware becomes available, confirm `DM_768x576` and the non-VGA
picker there; black output on the current NTSC/VGA setup is expected for PAL
timing. Do not edit SDL2; it is reference-only. Keep hardware captures as the
final authority.

## Completion checklist

Before handing back:

1. Review the final diff and ensure no SDL2 files changed.
2. Run `git diff --check`.
3. Report the exact files changed and why.
4. Report build results and hardware observations separately.
5. List any unresolved issues and the precise next task.

Keep the final report concise and factual. Agent proposes; agent verifies;
Dreamcast hardware decides.
