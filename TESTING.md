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
running `kos-tool`. The current test saves phases 1–20. Inspect these captures
(they map to `/tmp`) to prove on-screen pixels; the loader log alone does not
prove pixels.

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

## Next steps

- Proceed to work-order item 5 in `NEXT_TASKS.md`: validate alternate video
  modes and viewport sizing, then exercise PVR scene/list/primitive submission
  error reporting and recovery.
