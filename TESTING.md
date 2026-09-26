# Testing TinyGL

This guide covers the current software renderer, the opt-in SH4ZAM math path,
and the Dreamcast PVR smoke test. The PVR smoke test currently verifies filled
and textured triangles, culling, polygon modes, depth state, and blend modes.
Point/line PVR emission is still not covered.

## Host build and examples

The default configuration builds TinyGL and its X11 examples. From the
repository root:

```bash
make clean
make
```

The example build needs the X11 development headers and libraries configured
in `config.mk`. Run the examples from the repository root:

```bash
./examples/gears
./examples/spin
./examples/texobj
./examples/mech
```

Check that each opens a window and renders. `gears` and `spin` exercise
transforms and smooth-shaded triangles; `texobj` exercises texture objects;
`mech` exercises lighting and display lists. These are visual smoke checks,
not automated conformance tests.

## Dreamcast SH4ZAM build

SH4ZAM use is optional. Source the KOS environment in each fresh shell, then
build the TinyGL library with SH4ZAM enabled:

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_SH4ZAM=y
```

The SH4ZAM port must be installed in `KOS_PORTS`. This command builds the
library only; it does not produce a Dreamcast executable. The current
`examples/` targets use the X11 interface and are not KOS examples.

When comparing the optional path with the normal path, verify rotation,
nonzero vector normalization, and zero-vector normalization. The zero-vector
case must leave the input unchanged and return the same status as the software
implementation. Record the KOS and SH4ZAM versions with the result.

## Dreamcast PVR smoke test

Build the standalone KOS smoke target after sourcing the KOS environment:

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= \
  TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke
```

Set `DC_IP` to the dc-load-ip address of the console, then load the ELF:

```bash
export DC_IP=192.168.0.128  # change this if the console address differs
kos-tool -m /tmp -t "$DC_IP" -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

The smoke program exercises TinyGL's GL path: it calls `glInitPVR`, clears to
black, renders labeled 150-frame phases, and flushes each frame. The phases
cover solid and Gouraud triangles, transforms, clipping, culling, polygon
modes, and depth behavior. It reports progress and
`pvr_smoke: PASS` over the loader log, shuts PVR down through `glClose`, and
exits. A nonzero exit or missing PASS line is a failure.
At the end of each 150-frame phase it writes a PPM screenshot through KOS's
`vid_screen_shot()` to `/pc/tinygl-pvr-phase-N.ppm`. With the `-m /tmp` option,
these files are available on the host running `kos-tool` as
`/tmp/tinygl-pvr-phase-N.ppm`. The loader log reports each capture or its
failure.

For any hardware run, record the console video mode/cable, KOS version, build
options, test scene, observed output, and loader log. Mark a path untested if it
was only compiled or inspected; emulator-only success is not hardware
acceptance.

## Latest PVR hardware result

2026-09-25: KOS 2.3.0 on 640x480 VGA, built with
`TINYGL_USE_DREAMCAST_PVR=y` and `TINYGL_USE_SH4ZAM=y`. The centered red
triangle was visible on black for the 600-frame GL-driven run; the loader
reported `pvr_smoke: PASS` and `Program returned 0`.

2026-09-26: KOS 2.3.0 on 640x480 VGA (RGB565), built with
`TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y`. The smoke
scene now runs four 150-frame phases on a `GL_TRIANGLES` GL path:

- P1 baseline: solid red centered triangle (all vertices one color).
- P2 Gouraud: red/green/blue per-vertex triangle.
- P3 transform: green triangle drawn after `glTranslatef` + `glRotatef` on the
  modelview matrix (`glOrtho` is a no-op in this TinyGL, so transforms are
  modelview only).
- P4 clipping: four one-color triangles, each crossing one canonical screen
  edge (left/right/top/bottom) with two vertices beyond that edge and one
  inside. Positive Y is the top edge. TinyGL clips before viewport mapping.

Load command (console at `192.168.0.128` via dc-load-ip):

```bash
source /opt/toolchains/dc/kos/environ.sh
export DC_IP=192.168.0.128
kos-tool -m /tmp -t "$DC_IP" -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

KOS `vid_screen_shot()` saves a 640x480 PPM after each phase to the host
running `kos-tool`. The current test saves phases 1–24 (24 phases, 150 frames
each). Inspect these captures (they map to `/tmp`) to prove on-screen pixels;
the loader log alone does not prove pixels.

**PPM parsing note:** KOS writes a PPM with a comment line (`#KallistiOS
Screen Shot`) in the header, so plain ImageMagick `convert` mis-parses it.
Use a header-aware parser (skip whitespace and `#`-comment lines before reading
`W H MAXVAL`, then read the binary payload) to sample pixels.

### Culling, polygon-mode, and depth phase results (17 phases, hardware, 2026-09-26)

Build + run command (console `192.168.0.128`, **no `timeout` wrapper around
`kos-tool`**):

```bash
source /opt/toolchains/dc/kos/environ.sh
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

`pvr_smoke: PASS`, `Program returned 0`. Each PPM was parsed for non-black
pixel count, color count, and bounding box. Results:

| Phase | GL state | Non-black px | Verdict |
|---|---|---|---|
| 1 | baseline fill (red) | 38400 | ✅ filled red triangle |
| 2 | R/G/B gouraud | 34560, 4202 colors | ✅ per-vertex interpolation |
| 3 | translate+rotate | 13872 green | ✅ transformed, shifted upper-right |
| 4 | clip 4 edges | 13983, 5 colors | ✅ 4 edge-crossing triangles |
| 5 | cull disabled | 9600 green | ✅ drawn |
| 6 | cull-back + CCW | 9600 green | ✅ front face drawn |
| 7 | cull-back + CW | 0 (black) | ✅ back face culled |
| 8 | cull-front + CCW | 0 (black) | ✅ front face culled |
| 9 | cull-front + CW | 9600 green | ✅ front face drawn |
| 10 | cull front+back, CCW + CW | 0 (black) | ✅ both polygon orientations culled |
| 11 | polygon FILL | 9600 green | ✅ filled |
| 12 | polygon LINE | 0 (black) | ✅ no-op on PVR |
| 13 | polygon POINT | 0 (black) | ✅ no-op on PVR |
| 14 | depth LESS, far red then near green, writes enabled | 9600 green | ✅ near fragment wins |
| 15 | depth LESS, near green then far red, writes enabled | 9600 green | ✅ result independent of draw order |
| 16 | depth LESS, near green then far red, writes disabled | 9600 red | ✅ later far fragment overwrites |
| 17 | depth NEVER, green triangle | 0 (black) | ✅ all fragments rejected |

Culling is handled in `clip.c` geometry code (front-face sign from the vertex
coordinates, then filtered by `current_cull_face`), so it works on the PVR
backend regardless of the fill/line/point dispatch. With culling enabled,
`GL_FRONT_AND_BACK` discards polygons of both windings, as the API specifies.

**Fix for no-op frames:** the PVR backend previously only began a scene when
geometry existed, and `tgl_pvr_flush()` returned early otherwise. KOS requires a
scene boundary every frame (`pvr_scene_begin()`), so a no-op frame (culled
triangle, or LINE/POINT mode) left the previous frame retained on the display.
`tgl_pvr_flush()` now begins and finishes an empty scene when nothing was
drawn, committing the background color. After this fix, phases 7, 8, 12, 13
render black as expected. (See `src/pvr_dc.c`.)

Depth testing also passed on hardware. TinyGL maps nearer vertices to larger
depth values, so the PVR comparison is reversed for ordered comparisons
(`GL_LESS` maps to `PVR_DEPTHCMP_GREATER`, for example). KOS's
`PVR_DEPTHWRITE_ENABLE` and `PVR_DEPTHWRITE_DISABLE` constants are not boolean
1/0 in the intuitive order; the backend explicitly selects the enum. The four
phases above confirm draw-order behavior, depth-mask behavior, and `GL_NEVER`
rejection from captured pixels.

### Textured-triangle results (milestone 3, hardware, 2026-09-26)

The PVR backend now submits textured triangles. `src/clip.c::gl_draw_triangle_fill`
routes all triangles to `src/pvr_dc.c::tgl_pvr_draw_triangle`, which switches to
the textured path when `c->texture_2d_enabled` and the current texture has a
non-NULL pixmap. The path:

- Builds a `pvr_poly_cxt_txr` context (`PVR_TXRFMT_RGB565`, 256x256, `PVR_FILTER_NEAREST`).
- Loads the texture into twiddled VRAM via `tgl_pvr_texture_load`: copies the
  256x256 RGB565 pixmap into a 2048-byte aligned system-RAM buffer, cache-flushes
  it (`arch_dcache_wback_range`), then `pvr_txr_load`s it to VRAM. The load is
  cached per pixmap pointer.
- Overrides the compiled txr header's `depth_cmp`, `depth_write_dis`, and
  `culling` post-compile, because `pvr_poly_cxt_txr` hardcodes depth GREATER+write
  ENABLE and CCW culling. Culling is forced to `PVR_CULLING_NONE` (TinyGL handles
  winding in `clip.c`; CCW culling would wrongly drop CW triangles).
- Emits `u/v` from `vertices[i]->tex_coord.X/Y` with a white vertex `argb` so
  GL_DECAL yields the texel exactly.

The smoke test adds phases 18 (quadrant texture, flat), 19 (gradient texture,
flat, to reveal UV interpolation), and 20 (gradient texture under a modelview
translate+scale). It uses only RGB 256x256 REPEAT, the only supported combination
in this TinyGL (`glTexImage2D` and `glTexParameteri` otherwise `gl_fatal_error`).

**Build + run command (console `192.168.0.128`, no `timeout` wrapper):**

```bash
source /opt/toolchains/dc/kos/environ.sh
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

Hardware run result: `pvr_smoke: PASS`, `Program returned 0`. PPM captures
were parsed with the KOS comment-aware header reader. Phase results:

| Phase | Image | Pixel evidence | Result |
|---|---|---|---|
| 18 | Four-quadrant texture | 150,528 non-black pixels; exactly 5 colors including black and the four RGB565 quadrant colors | ✅ orientation and upload visible |
| 19 | Gradient texture | 55,296 non-black pixels; 1,388 colors | ✅ UV interpolation visible |
| 20 | Gradient plus modelview transform | 75,264 non-black pixels; 1,387 colors; bounds shifted right versus phase 19 | ✅ texture survives transform |

The first hardware run showed phases 19–20 still displaying phase 18's
quadrants. Inspection found that the textured polygon-header cache was keyed
only by depth state, not by the active pixmap. The header now rebuilds when the
pixmap pointer changes. The successful rerun showed the expected gradient
colors. An earlier diagnostic reported a NULL pixmap, but that did not recur;
the successful captures confirm texture data reached the PVR.

### Blend modes and alpha (milestone 4, hardware investigation, 2026-09-26)

TinyGL did not track blend state: `glBlendFunc` was only a commented-out stub,
and `glEnable(GL_BLEND)` fell through `glopEnableDisable` with no handler. This
milestone adds blend plumbing and maps it to the KOS PVR per-polygon blend.

**TinyGL state plumbing (so the backend can see the blend request):**

- `src/zgl.h`: added `blend_enabled`, `blend_src`, `blend_dst` to `GLContext`.
- `src/opinfo.h`: added `ADD_OP(BlendFunc,2,"%C %C")` (also regenerates the
  `OP_BlendFunc` enum entry, op name string, and dispatch table slot).
- `src/api.c`: implemented `GLBlendFunc(GLenum, GLenum)` to emit `OP_BlendFunc`.
- `include/GL/gl.h`: gave `glBlendFunc` a real prototype (it was inside the
  "not implemented" comment block).
- `src/misc.c`: `glopEnableDisable` now handles `GL_BLEND` (`c->blend_enabled`);
  added `glopBlendFunc` to store the src/dst factors.

**PVR backend mapping (`src/pvr_dc.c`):**

- `tgl_pvr_blend_factor()` maps GL factors to `pvr_blend_mode_t`:
  `GL_ZERO/ONE`, `GL_SRC_ALPHA/ONE_MINUS_SRC_ALPHA`,
  `GL_DST_ALPHA/ONE_MINUS_DST_ALPHA`, `GL_DST_COLOR/ONE_MINUS_DST_COLOR`.
  The PVR has no mode for `GL_SRC_COLOR`, `GL_ONE_MINUS_SRC_COLOR`, or
  `GL_SRC_ALPHA_SATURATE`, so those fall back to a source-only factor (no
  blending) instead of producing a wrong result. `GL_FUNC_ADD` (the only blend
  equation TinyGL supports) is the PVR's additive blend, so no equation work is
  needed.
- `tgl_pvr_update_header()` (solid path) and `tgl_pvr_update_txr_header()`
  (textured path) now rebuild their header when `blend_enabled`/src/dst change,
  set `context.gen.alpha`, and set the `blend` src/dst + enable bits. The solid
  path already packs per-vertex alpha into `vertex.argb`, so `GL_SRC_ALPHA`
  blends by vertex alpha. For the textured path `cxt_txr` is called first and
  the blend fields are set on the context before `pvr_poly_compile`, because
  `cxt_txr` resets the context's blend defaults.
- When blending is off the factors reduce to `PVR_BLEND_ONE`/`PVR_BLEND_ZERO`,
  so the existing phases (0–20) are unchanged.

**Smoke test (`tests/dreamcast/pvr_smoke/pvr_smoke.c`):** added phases 21–23
(`NUM_PHASES` 20 -> 23). `FRAME_LIMIT` is derived from
`NUM_PHASES * PHASE_FRAMES` so all 23 phases execute. The per-phase reset does `glDisable(GL_BLEND)` and
`glBlendFunc(GL_ONE, GL_ZERO)` so blend state does not leak between phases.

- P21 blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA, source alpha 0.5: opaque red base
  triangle with a translucent green triangle on top. The overlap should mix to
  an olive/yellow, not pure green or pure red. Depth writes disabled.
- P22 uses the same factors at source alpha 0.25; it is intended to show a
  lighter mix with more red base visible (this difference was not observed).
- P23 blend disabled, same geometry: green draws fully opaque over red and
  covers it (contrast vs P21/P22).

**Build + run command (console `192.168.0.128`, no `timeout` wrapper):**

```bash
source /opt/toolchains/dc/kos/environ.sh
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

Verification found two harness/API issues before pixel acceptance: the
`glBlendFunc` opcode wrote its second argument past a two-element local array,
and the frame limit stopped at phase 20. Both are fixed (`GLParam p[3]` and a
derived frame limit). The rebuilt 23-phase ELF ran on KOS 2.3.0 VGA and
reported `pvr_smoke: PASS`, `Program returned 0`.

PPM pixel results for phases 21–23:

| Phase | Non-black pixels | Center RGB | Evidence |
|---|---:|---|---|
| 21, source alpha 0.5 | 50,688 | `(128, 128, 0)` | red/green blend output |
| 22, source alpha 0.25 | 50,688 | `(192, 64, 0)` | distinct output; less green contribution |
| 23, blending disabled | 50,688 | `(0, 252, 0)` | opaque green covers red |

The baked-header diagnostic showed `m2.alpha=1` and vertex alpha bytes 128 and
64 for P21 and P22, ruling out the initial vertex-alpha-header theory. The
actual issue was that KOS `src_enable`/`dst_enable` select second accumulation
buffers, not ordinary alpha-factor enables. Setting them to zero and
submitting blend-enabled polygons through `PVR_LIST_TR_POLY` produced the
expected distinct pixels. Opaque geometry remains on `PVR_LIST_OP_POLY`; when
a scene first encounters blending, the backend closes the OP list and switches
to TR. P17 remains black after this list arrangement.

## Alternate video modes and viewport sizing (work-order item 5a)

`tests/dreamcast/pvr_smoke/pvr_smoke.c` now selects the KOS display mode via a
build-time macro `PVR_SMOKE_VIDEO_MODE` (default `DM_640x480`). Override it on
the build command line to validate a different resolution:

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke SMOKE_CFLAGS="-DPVR_SMOKE_VIDEO_MODE=DM_320x240"
```

`smoke_mode_dim()` maps each available mode (`DM_320x240`, `DM_640x480`,
`DM_256x256`, `DM_768x480`, `DM_768x576`) to its pixel size, and the viewport is
passed to `glInitPVR(vm_w, vm_h)` instead of a hardcoded 640x480. This keeps the
mode and viewport in sync. `vid_set_mode()` returns `void`, so dimensions cannot
be read back; the table is the single source of truth.

Before initializing TinyGL, the smoke/demo path now shows a 5-second refresh
picker on non-VGA cables: A selects 60 Hz, B selects 50 Hz, and Start or timeout
keeps the regional default (Europe defaults to 50 Hz; other/unknown regions to
60 Hz). It uses KOS's BIOS font and Maple controller state. For VGA, it selects
60 Hz directly because KOS provides VGA modes at 60 Hz only, matching the SDL
Dreamcast driver's policy. The selected KOS mode's actual width/height are then
passed to `glInitPVR`. This picker demonstrates application-owned mode
selection; the TinyGL library itself does not choose KOS video timing.

New phase 24 draws a full-screen red polygon (`-1..1`, covers the whole display
at any resolution) plus a blue reference square in the top-left corner spanning
0.4 normalized units (~20% of the width). On hardware the red polygon must fill
the screen and the blue square must sit at the top-left at about 20% of the
width; a viewport that is too large, too small, or wrong aspect makes either
visibly wrong. This is the visible viewport-sizing check.

Hardware captures (KOS 2.3.0, NTSC Dreamcast with VGA output):

| Mode | Run | Phase 24 capture | Result |
|---|---|---|---|
| `DM_320x240` | PASS, returned 0; no submission errors | 320x240; blue square 63x47 at top-left; red field; one-pixel bottom/right black edge | viewport matches selected dimensions |
| `DM_640x480` | PASS, returned 0; no submission errors | 640x480; blue square about 20% width; red field; one-pixel bottom/right black edge | default mode passes |
| `DM_768x480` | PASS, returned 0; no submission errors | 768x480; blue square 153x95 at top-left; red field; one-pixel bottom/right black edge | NTSC high-resolution control passes |
| `DM_768x576` | PASS, returned 0; no submission errors | P1 and P24 are entirely black | expected PAL timing mismatch on NTSC/VGA; PAL output not verified |

KOS defines `DM_768x576` as PAL 50 Hz interlaced and `DM_768x480` as NTSC
60 Hz interlaced. The console used here is NTSC with VGA output, so the picker
selects 60 Hz and uses `DM_768x480` for the 768-wide family. The PAL
`DM_768x576` capture was all black on this setup, as expected when the attached
NTSC/VGA display cannot sync to PAL timing; this is not evidence of a backend
defect. PAL-compatible hardware was not available, so keep PAL output
confirmation as an optional future customer test. Flycast 2.7
is installed and can load the PAL ELF with `Dreamcast.Broadcast` configured,
but this session did not produce a usable visual capture: KOS screenshots at
`/pc/...` cannot be opened by Flycast, and a desktop-wide screenshot was blocked
by the automatic approval review because it could include unrelated screen
contents. Emulator output remains unverified. For the passing runs, phase 24's one-pixel bottom/right
edge is rasterization coverage; the red field and correctly sized top-left blue
square are present. The default 640x480 build is restored locally.

## PVR submission error reporting and recovery (work-order item 5b)

`src/pvr_dc.c` now counts submission failures into `tgl_pvr_submission_errors` at
each failure site (init, `pvr_list_begin`, list transition, polygon-header and
vertex submissions, textured/solid polygon header and vertex submissions, `pvr_list_finish`,
`pvr_scene_finish`, and shutdown) and prints a
`TinyGL PVR: N submission error(s) this session` summary at shutdown. This turns
the previously transient per-error stderr messages into a durable session total.

Recovery: the PVR list state machine already tolerates a half-open list (the
`pvr_list_begin` auto-finishes a previously-open different list, and `finish`
returns -1 only when nothing was open). `tgl_pvr_flush()` still finishes the
half-open list from a failed primitive, so the next scene begins a fresh OP list.
The list-transition failure now also resets `tgl_pvr_list_type` to a sentinel so
a failed transition cannot leave a stale list type driving the next draw.

Hardware test plan: normal runs should print **no** submission-error summary.
Because KOS `pvr_prim`/`pvr_list` failures are hard to force deterministically,
this is verified by code review plus a clean build and a zero-error run rather
than by injecting failures. Confirm the summary line appears only when an error
actually occurred.

Status: **normal-run hardware check passed** in 320x240, 640x480, 768x480, and 768x576 runs; no submission-error summary appeared. On this VGA setup the new chooser correctly takes the 60 Hz path. The non-VGA on-screen choice and forced-failure recovery remain unverified on hardware.

## Next steps

- Work-order item 5 is implemented. Future hardware follow-up: verify 50 Hz on
  PAL-capable output, confirm the on-screen selector there, and find a safe,
  deterministic way to exercise PVR submission-error recovery. Normal-run
  error reporting and viewport captures pass on this NTSC/VGA setup at
  320x240, 640x480, and 768x480.
