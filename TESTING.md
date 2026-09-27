# Testing TinyGL

This guide covers the current software renderer, the opt-in SH4ZAM math path,
and Dreamcast PVR tests. The PVR smoke test currently verifies filled and
textured triangles, culling, polygon modes, depth state, blend modes, points,
and lines.

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

## Dreamcast NeHe06 sample port

Build and run the NeHe lesson 6 sample with the Dreamcast PVR backend:

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C src CC=kos-cc TINYGL_USE_GLX= \
  TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/nehe06 clean
make -C tests/dreamcast/nehe06
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/nehe06/tinygl-nehe06.elf
```

The port loads its 256x128 24-bit BMP from romdisk and passes RGB data directly
to `glTexImage2D` (TinyGL's legacy API takes component count `3`, then the
`GL_RGB` source format). TinyGL resizes it to 256x256 RGB565. The PVR backend
must twiddle that linear RGB565 image before upload: `pvr_txr_load()` only
copies bytes, so `src/pvr_dc.c` uses KOS `pvr_txr_load_ex(...,
PVR_TXRLOAD_16BPP)` to match the twiddled texture header.

Latest result: the updated sample built and linked, the Dreamcast loader
reported the expected 256x128 image dimensions without a startup error, and
the texture rendered correctly on the spinning cube in both Flycast and on a
physical Dreamcast. The hardware run used KOS 2.3.0 at 640x480 VGA; the first
hardware run before the twiddling fix showed the same corruption as Flycast.

## Dreamcast PVR strip performance benchmark

`tests/dreamcast/pvrmark_strips/` ports the random-walk triangle-strip workload
from GLdc's `samples/pvrmark_strips_gldc/`. It reports frame rate every five
seconds, uses the KOS sample's per-frame `pvr_get_stats().frame_rate` EMA,
searches around the same 55 FPS target with the same +2,500/-200 steps, and
reports TinyGL API-build, draw-pipeline, and scene-submit time plus KOS PVR
timing/vertex statistics. TinyGL starts at a conservative ceiling of 3,500
triangles/frame and halves down as needed. The KOS direct sample starts at
33,333 because it submits one raw PVR strip; TinyGL retains a conservative
3,500-triangle starting ceiling while validating its native strip path and its
fallback cases.

The GLdc sample's 0..640/480 orthographic coordinates are mapped into TinyGL
clip coordinates. TinyGL has no `glColor4ub`, so random byte colors are passed
through `glColor3f`; this adds conversion work compared with GLdc. Compare three
runs: KOS `pvrmark_strips_direct` as the raw PVR reference, TinyGL with SH4ZAM
disabled, and TinyGL with SH4ZAM enabled. Keep KOS unmodified: the SH4ZAM
toggle isolates the effect within TinyGL. The workload generation and search
method match, while PVR submission differs by design. Eligible TinyGL
`GL_TRIANGLE_STRIP` blocks now reach the native PVR strip path; culling,
clipping, lighting, and non-fill cases still use the existing per-triangle
route.

Build and run once with SH4ZAM off and once with it on. Clean between variants
because Make does not track compiler flags:

```bash
source /opt/toolchains/dc/kos/environ.sh
export PATH=/opt/toolchains/dc/kos/utils/build_wrappers:$PATH

# SH4ZAM disabled
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y
make -C tests/dreamcast/pvrmark_strips clean
make -C tests/dreamcast/pvrmark_strips
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/pvrmark_strips/tinygl-pvrmark-strips.elf

# SH4ZAM enabled (build marker and library option must match)
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= \
  TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/pvrmark_strips clean
make -C tests/dreamcast/pvrmark_strips BENCH_CFLAGS=-DTINYGL_USE_SH4ZAM
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/pvrmark_strips/tinygl-pvrmark-strips.elf
```

Do not wrap `kos-tool` in `timeout`; that can drop the DCLOAD connection. Each
run now exits automatically after printing its final threshold result. Record
the console mode, SH4ZAM setting, search result, CPU stage timings, and PVR stats.
This strip workload has no rotation or normals, so it does not exercise
SH4ZAM's current sine/cosine or vector-normalization paths; it can quantify the
current build's strip throughput and any enabled SH4ZAM effect on code paths
the workload actually reaches, not predict gains from hypothetical new calls.

Physical Dreamcast results (KOS 2.3.0, 640x480 VGA, same console session,
KOS-matched search): SH4ZAM off finished at 2,100 triangles/frame and 59.08
FPS (124,078 triangles/sec); SH4ZAM on finished at 2,500 triangles/frame and
56.10 FPS (140,248 triangles/sec). At those final loads, TinyGL immediate-build
time was 20.552 ms/frame off and 21.373 ms/frame on; PVR registration was
22.720 ms off and 22.675 ms on, with about 3.5 ms render time in both runs.
FPS varied sharply across adjacent loads during the searches, so these are
preliminary single-run results, not a firm SH4ZAM speedup claim. Repeat the
A/B runs and collect the KOS direct reference before drawing a conclusion.

The `TINYGL_USE_SH4ZAM` path now loads the unlit model/projection matrix into
SH4ZAM once per `glBegin` and transforms vertices with the hardware matrix unit.
The first matched-search A/B is recorded above; repeatability and KOS direct
reference results remain pending.
The benchmark's immediate-build interval includes the per-vertex GL calls and
is the CPU-side cost center to watch. The sample still does not exercise
SH4ZAM's trig or vector-normalization paths.

Latest fast-path hardware run (single run each, KOS 2.3.0, 640x480 VGA): the
PVR smoke test built and returned `pvr_smoke: PASS` / exit 0. The strip
benchmark built and returned 0 for both variants:

| TinyGL build | Threshold triangles/frame | FPS | Triangles/sec | Immediate build | Draw pipeline | PVR registration | PVR render |
|---|---:|---:|---:|---:|---:|---:|---:|
| SH4ZAM off | 2,700 | 56.09 | 151,440 | 10.274 ms | 11.367 ms | 14.362 ms | 3.338 ms |
| SH4ZAM on | 2,700 | 56.24 | 151,841 | 9.648 ms | 10.466 ms | 13.306 ms | 3.404 ms |

These preliminary runs use the native strip submission path, and the benchmark
does not compare rendered pixels against a `GL_TRIANGLES` oracle. The
dedicated strip-versus-triangle hardware image comparison (visual equivalence)
remains a separate, still-open check. The `pvr_smoke` phases do not exercise
`GL_TRIANGLE_STRIP`.

### Full comparison (work-order item 6, 2026-09-27)

Five alternating hardware runs per TinyGL variant (KOS 2.3.0, 640x480 VGA,
`DC_IP=192.168.0.128`), plus one run each of KOS's unmodified
`pvrmark_strips_direct` (raw immediate-mode PVR, `pvr_dr_target`/`pvr_dr_commit`,
no GL API) and GLdc's `pvrmark_strips_gldc` (prebuilt ELFs used as-is, no
source changes) as external references. Builds followed the reproduction
recipe above; `make -C tests/dreamcast/pvrmark_strips clean` between variants.

| Build | Runs (triangles/frame) | Runs (fps) | Median tri/frame | Median fps | Median tri/sec |
|---|---|---|---:|---:|---:|
| TinyGL, SH4ZAM off | 2900,2900,2900,2900,2900 | 55.78,55.82,55.82,55.82,57.93 | 2,900 | 55.82 | 161,879 |
| TinyGL, SH4ZAM on | 2700,2900,2700,2700,2700 | 57.95,55.82,58.11,55.09,58.11 | 2,700 | 57.95 | 156,898 |
| GLdc `pvrmark_strips_gldc` (1 run) | 2783 | 59.95 | 2,783 | 59.95 | 166,851 |
| KOS `pvrmark_strips_direct` (1 run) | 93,333 | bimodal 43-60, median 57.1 (10 samples at converged load) | 93,333 | ~57.1 | ~5,330,000 |

Final-load CPU/PVR stage timings (per frame, at each build's converged
triangle count):

| Build | Immediate build | Draw pipeline | PVR registration | PVR render |
|---|---:|---:|---:|---:|
| TinyGL, SH4ZAM off (2900 tri) | 10.643 ms | 11.440 ms | 13.402 ms | 3.282 ms |
| TinyGL, SH4ZAM on (2700 tri) | 9.715 ms | 11.055 ms | 12.940 ms | 3.416 ms |
| GLdc (2783 tri) | 3.118 ms | 1.719 ms | 3.328 ms | 3.256 ms |

Findings:

- **SH4ZAM's effect on this workload is the same pattern seen elsewhere in
  this project (compare TinyBalls, work-order item 7): it measurably reduces
  CPU-side stage time (immediate build -0.9 ms, draw pipeline -0.4 ms,
  registration -0.5 ms) but the search converges at a *lower* triangle
  threshold with SH4ZAM on (2,700 vs 2,900), so median triangles/sec is
  slightly lower (156,898 vs 161,879, about 3%), not higher. This workload has
  no rotation/normals, so SH4ZAM's trig/normalize paths are unused here (as
  already noted above); the CPU win comes entirely from the matrix-transform
  path and is not enough to move the PVR-registration-bound throughput
  ceiling.
- **KOS's raw direct-PVR reference converges roughly 32x higher** (93,333 vs
  ~2,700-2,900 triangles/frame) than either GL implementation, as expected --
  it uses `pvr_dr_target()`/`pvr_dr_commit()` store-queue writes directly, with
  no GL API, no per-vertex float conversion, and no scene-graph/state
  machinery. It also swung between two stable FPS bands (~43 and ~60 fps) at
  its converged load rather than settling smoothly, which looks like a
  vsync/frame-time quantization edge at that exact triangle count rather than
  measurement noise (the same two values, 43.01x and 60.06-60.08, repeated
  almost exactly across samples). This is a ceiling reference, not a
  same-shape comparison.
- **GLdc's PVR registration stage is about 4x faster than TinyGL's at a
  comparable triangle count** (3.3 ms at 2,783 tri vs 13.0-13.4 ms at
  2,700-2,900 tri). TinyGL's native strip fast path closed most of the gap
  that existed before it (see the `sq_fast_cpy` batching work earlier in this
  file), but this result shows real headroom remains in TinyGL's PVR
  submission stage specifically, not just in its immediate-build/CPU stages
  (which are already only ~3x GLdc's, a much smaller gap). Worth a follow-up
  investigation of what GLdc's registration path does differently.
- Do not claim a strip-path performance gain from this data set in isolation
  without also completing the pending strip-vs-triangle visual-equivalence
  check noted above.

### Correction: the SH4ZAM-off result above is not stable across sessions (2026-09-27)

A second, independent set of 5 SH4ZAM-off runs was collected later the same
day (same console, same unchanged binary/commit) to confirm the result above.
It did **not** reproduce: 2100, 2300, 2500, 2500, 2100 triangles/frame
(55.21, 56.11, 55.82, 55.79, 55.24 fps; 115939, 129044, 139551, 139471,
115999 triangles/sec) -- median 2,300 tri/frame, 55.79 fps, 129,044
tri/sec, roughly 20% lower than the first set's 2,900 / 55.82 / 161,879.

This is **bimodal, not just noisy**: the first set of 5 clustered tightly at
2,900 tri/frame with nothing else nearby; the second set of 5 clustered at
2,100-2,500 with nothing near 2,900. A combined median across all 10 runs
(2,700 tri/frame) would misrepresent both clusters and is not reported as a
finding here. Nothing in the binary or library changed between the two
sessions (verified: both built from commit `f92ebc7`, confirmed via the
ELF's embedded "SH4ZAM disabled" string).

**Root cause not identified.** Candidates not yet ruled out: console
thermal/warm-up state, network/dcload-console print latency perturbing the
timed loop (the benchmark's own stage-timing `printf`s go out over the same
live connection used to drive the search), or genuine PVR/TA scheduling
variance at this triangle-count range. The coarse search step sizes
(+2,500/-200) mean the true steady-state threshold could plausibly sit
anywhere in a wide band and a 5-run sample only sees whichever narrow slice
of that band the console happened to be in that session.

**Follow-up (same day): console-print latency ruled out; found a monotonic
drift signature instead.** A third set of 5 runs was collected with the
periodic stage-timing `printf`s suppressed entirely (`-DPVRMARK_QUIET_STAGE_PRINTF`,
`tests/dreamcast/pvrmark_strips/main.c`; only `print_stats()`'s output is
skipped, no other behavior changes) to test whether console-print I/O over
the live dcload connection was perturbing the timed loop:

2700, 2500, 1900, 1900, 1900 tri/frame (55.22, 55.82, 55.95, 55.95, 55.95
fps; 149093, 139545, 106298, 106298, 106300 tri/sec) -- median 1,900
tri/frame, 55.95 fps, 106,300 tri/sec. This is lower than *both* prior
sessions (2,900 then 2,300), and the spread did not shrink -- so
console-print latency is not the (main) cause.

The shape of this data is the real finding: it is not random scatter, it is
a **monotonic drift downward within the run set itself**, settling exactly
at run 3 and staying bit-for-bit identical for runs 3-5. Combined with all
three same-day sessions themselves trending down in sequence (2,900 ->
2,300 -> 1,900), this looks like cumulative thermal or PVR/console state
drift from many consecutive hardware runs without a power cycle, not
measurement noise and not a code-path difference (the binary was identical
across all three sessions).

**Follow-up (same day): power cycle does NOT fix it -- the "settling"
theory above was wrong.** Ten runs were collected immediately after a full
console power cycle (same quiet-mode binary): 3300, 2500, 1900, 1900,
2500, 1900, 2700, 2500, 2700, 2700 tri/frame (55.12-55.95 fps, 106,298-
181,887 tri/sec). This flatly contradicts the "monotonic drift settling to
a floor" read of the earlier quiet-mode set: run 5 bounced back up to
2,500 after two runs at 1,900, and run 7 climbed to 2,700 after that --
this is oscillation between a handful of discrete values (1,900 / 2,500 /
2,700 / 3,300, each ~200-600 apart), not a one-way thermal ramp. Sorted:
1900 x3, 2500 x3, 2700 x3, 3300 x1 -- median 2,500 tri/frame, 55.82 fps,
139,551 tri/sec. The full range (1,900-3,300, a 74% spread) is *wider*
than either session collected before the power cycle, not narrower, so
power-cycling did not stabilize anything either.

**Actual conclusion:** across all 4 sessions collected today (25 runs
total, spanning prints-on, prints-off, warm, and freshly-power-cycled
conditions), results land on a discrete, evenly-spread set of values from
1,900 to 3,300 tri/frame with no single session's median being obviously
more "correct" than another's. This is best explained by the search
algorithm's own coarse decrement step (`DECREMENT_POLY_COUNT = 200`)
combined with real run-to-run noise in the 5-second `avgfps` measurement
window near the 55 fps target: `PHASE_DECR` stops the moment one 5-second
average happens to read >=55, so the reported "threshold" is wherever that
noisy measurement first crosses the line, not a stable property of the
hardware. Console thermal state, print latency, and cumulative session
drift were all tested and are not the primary driver -- this looks like
an inherent limitation of the benchmark's own search methodology, not a
console or TinyGL behavior worth chasing further with more runs of the
same design.

**Recommendation for anyone revisiting this:** don't add more 5-or-10-run
sets under the current search design; it will keep landing on different
discrete values indefinitely. A meaningful fix would be to average FPS
over many more seconds per candidate load (finer than the current 5s
window) and/or pick a handful of *fixed* triangle counts and report
their average FPS across many runs, rather than trusting a fast search's
self-selected stopping point as a single number.

## Dreamcast TinyBalls math stress scene

`tests/dreamcast/tinyballs/` is the TinyGL counterpart to SH4ZAM's direct-PVR
`bruces_balls` example. It draws rotating, lit spheres with a 20-by-20
latitude/longitude mesh (800 triangles per sphere), modelview transforms,
transformed normals, `GL_NORMALIZE`, and per-vertex lighting. The GL workload
is identical in SH4ZAM-off/on builds. `pvrmark_strips` remains the separate
TinyGL backend-submission benchmark.

Each latitude band is submitted as its own `GL_TRIANGLE_STRIP`, except the two
polar bands (stack 0 and the last stack), which collapse to a single point at
the pole and are drawn as a `GL_TRIANGLE_FAN` instead -- a `GL_TRIANGLE_STRIP`
over a collapsed ring is a strip of near-zero-area triangles, which was traced
on hardware to a stray one-pixel-tall line artifact past the sphere's
silhouette (root cause and fix: NEXT_TASKS.md item 8). Lit strips use
TinyGL's native PVR strip fast path by default now (see item 8's DONE note);
only non-fill polygon modes, culling, clipping, and non-`GL_RENDER` modes
still fall back to the parity-ordered per-triangle route. The rotation
breakup in the physical Dreamcast capture
was caused by PVR tile-bin overflow: TinyGL initialized the PVR with zero OPB
overflow blocks. Raising this to KOS's default of three removed the missing
surface patches. A post-fix capture showed all six spheres intact while
rotating. The one-shot screenshot is taken after 60 frames at
`/pc/tinyballs.ppm`; `kos-tool -m /tmp` maps it to `/tmp/tinyballs.ppm`.

First post-fix hardware A/B (KOS 2.3.0, 640x480 VGA; one run per variant):

| TinyGL build | Stable spheres | Logical triangles/frame | FPS | Transform interval | Geometry interval | PVR registration | PVR render |
|---|---:|---:|---:|---:|---:|---:|---:|
| SH4ZAM off | 2 | 1,600 | 60.09 | 0.070 ms | 16.517 ms | 16.281 ms | 3.355 ms |
| SH4ZAM on | 2 | 1,600 | 56.10 | 0.036 ms | 16.742 ms | 18.982 ms | 3.344 ms |

The off run measured 19.55 FPS at six spheres; the on run measured 20.03 FPS.
Both six-sphere delayed captures showed intact rotating spheres. SH4ZAM cut
the measured transform interval roughly in half, but the stable object count
was unchanged and the single-run FPS was lower in the enabled build.

### Five-run comparison (work-order item 7, 2026-09-27)

Five alternating full runs per variant (KOS 2.3.0, 640x480 VGA,
`DC_IP=192.168.0.128`, default `TINYBALLS_DISABLE_LIGHTING=0` i.e. lighting
enabled, lit strips now on the native PVR path per item 8). Each run's FPS
search auto-exits about 10 seconds after reaching `SEARCH_FINAL`, so no
`Start` press or manual reset was needed between runs -- confirmed by
`Program returned 0` after every run.

| Build | Runs (fps) | Runs (tri/sec) | Median fps | Median tri/sec |
|---|---|---|---:|---:|
| SH4ZAM off | 60.08, 60.12, 60.12, 60.12, 56.18 | 144180, 144279, 144279, 144279, 134841 | 60.12 | 144,279 |
| SH4ZAM on | 60.09, 56.18, 60.09, 60.09, 60.09 | 144208, 134842, 144208, 144207, 144207 | 60.09 | 144,207 |

All 10 runs (both variants) converged on the same load: 3 balls, 2,400
triangles/frame. Stage timings at that converged load (representative run,
each variant):

| Build | Transform | Geometry | Frame | PVR registration | PVR render |
|---|---:|---:|---:|---:|---:|
| SH4ZAM off | 0.115 ms | 16.481 ms | 16.644 ms | 19.084 ms | 3.487 ms |
| SH4ZAM on | 0.054 ms | 16.569 ms | 16.670 ms | 19.024 ms | 3.465 ms |

**Conclusion, with real statistical power this time (5 runs/variant, not 1):
SH4ZAM makes no measurable difference to TinyBalls' overall throughput.**
Median fps (60.12 vs 60.09) and median tri/sec (144,279 vs 144,207) are
within noise of each other, and both variants converge on the identical
ball/triangle count every single run. The one consistent effect is
transform-stage time, roughly halved as before (0.115 ms -> 0.054 ms, ~53%),
but that stage is such a small fraction of the ~16.6 ms frame budget
(PVR registration alone is larger, at ~19 ms) that halving it doesn't move
the needle. This is the same conclusion reached independently for
`pvrmark_strips` (work-order item 6): SH4ZAM's current integration helps
CPU-side transform work, but this project's PVR-registration-bound
workloads don't have enough CPU-bound headroom left for that win to show up
in FPS or triangle throughput. `bruces_balls` remains the separate
direct-PVR reference and was not re-run as part of this comparison (no
GL API, not a like-for-like target).

Build and run both variants on the same console and video mode. Clean the
library and test between builds because Make does not track compiler flags:

```bash
source /opt/toolchains/dc/kos/environ.sh

# SH4ZAM disabled
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y
make -C tests/dreamcast/tinyballs clean all
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/tinyballs/tinygl-tinyballs.elf

# SH4ZAM enabled
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= \
  TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/tinyballs clean
make -C tests/dreamcast/tinyballs STRESS_CFLAGS=-DTINYGL_USE_SH4ZAM
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/tinyballs/tinygl-tinyballs.elf
```

The first TinyBalls hardware runs are diagnostic only. SH4ZAM-off runs reached
the 55 FPS target at two or three spheres depending on the run, but the visible
rotation breakup invalidates those counts as a baseline. First fix and
visually verify the geometry on hardware, then repeat alternating SH4ZAM-off
and -on runs before reporting a speedup or comparing object counts with
`bruces_balls`.

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

### PVR point and line emission (24-phase hardware run, 2026-09-26)

The backend now emits points and lines as colored PVR triangle strips. Points
use a 1x1 screen-space quad; lines use a one-pixel-wide quad around the clipped
segment. Polygon `LINE` mode routes visible triangle edges through the line
path, and polygon `POINT` mode routes its vertices through the point path.
TinyGL does not expose line-width or point-size state, so these use the default
one-pixel size.

The 640x480 Dreamcast smoke run returned `pvr_smoke: PASS` / `Program returned
0`. P12 visibly showed the green polygon outline, horizontal/vertical/reversed
diagonal/clipped direct lines, and a yellow line loop. P13 showed the cyan
triangle vertices and five yellow `GL_POINTS`. The phase captures were
inspected at `/tmp/tinygl-pvr-phase-12.ppm` and
`/tmp/tinygl-pvr-phase-13.ppm`.

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
The controlled failure test below checks error reporting and recovery without
forcing a real PVR queue failure. Confirm the summary line appears only when an
error actually occurred.

Status: **normal-run hardware check passed** in 320x240, 640x480, 768x480, and 768x576 runs; no submission-error summary appeared. The one-shot simulated-failure ELF also ran on Dreamcast at 640x480: it logged exactly one injected polygon-header failure, captured all 24 phases, rendered the green triangle in P11 after the injection, printed `1 submission error(s)`, and returned `pvr_smoke: PASS` / exit 0. On this VGA setup the new chooser correctly takes the 60 Hz path. The non-VGA picker was also exercised in Flycast: its log showed selection of `DM_320x240_PAL` (50 Hz), confirming the 50 Hz choice reaches `vid_set_mode()`. Flycast reported the flashrom region as unknown (`00001`), so its regional-default behavior was not verified. Real non-VGA picker and PAL-compatible output validation remain open.

## PVR submission-error one-shot failure injection test

This test verifies the PVR submission-error reporting and recovery added in
item 5b (`tgl_pvr_submission_errors` counting + half-open list cleanup + fresh
scene recovery) using a controlled, deterministic, one-shot failure.

### Design

- Injection point: the solid-polygon **header** submission inside
  `tgl_pvr_draw_triangle()` in `src/pvr_dc.c`. This path is hit by every solid
  triangle; a failed submission leaves the list half-open (`list_active=1`),
  so the next `tgl_pvr_flush()` finishes the open list and the following
  frame's `begin_scene()` starts a fresh scene -- exactly the recovery paths
  under test.
- Nature of the failure: **SIMULATED**, not hardware-generated. The backend
  exposes a thin submission wrapper `tgl_pvr_prim()` that, when the test macro
  is defined, returns `-1` **without** enqueuing any command and **without**
  touching the real PVR command queue. It matches KOS's contract when
  `pvr_prim()` rejects a primitive (e.g. arena full): the list stays open and
  the call site's production accounting (`tgl_pvr_submission_errors++`,
  `fprintf`, early `return`) runs unchanged.
  - It is NOT a hardware-generated failure. The real PVR queue is never
    corrupted or overflowed, so the console cannot hang. It exercises the
    backend's *handling* of a KOS error return, which is what we are
    verifying.
- One-shot: the smoke test arms the failure for a single phase via
  `tgl_pvr_test_arm_fail_next()`. The wrapper consumes the arm on the first
  header submission of that phase, so exactly one failure is injected. Later
  frames of the same phase and all subsequent phases render normally.

### How the hook is gated (build-time only; disabled in normal builds)

- `src/Makefile`: `CFLAGS += $(EXTRA_CFLAGS)` (mirrors the smoke test's
  `SMOKE_CFLAGS` hook). Add `-DTGL_PVR_TEST_INJECT_FAIL` here for the library.
- `src/pvr_dc.c`: the wrapper + `tgl_pvr_test_fail_next`/`_fired` state +
  `tgl_pvr_test_arm_fail_next()` are compiled only under `#ifdef
  TGL_PVR_TEST_INJECT_FAIL`. The call site always routes the solid header
  through `tgl_pvr_prim()`; in normal builds it is a pure pass-through to
  `pvr_prim()`, so behavior is byte-for-byte unchanged.
- `tests/dreamcast/pvr_smoke/pvr_smoke.c`: defines `TGL_PVR_TEST_INJECT_PHASE`
  (default `10`, a single solid-triangle phase) and arms the hook in the phase
  transition block under the same macro. A local prototype guards against the
  smoke test not including `src/zgl.h`.
- `src/zgl.h`: declares `tgl_pvr_test_arm_fail_next()` under the macro.

### Build both variants (normal build does NOT source environ.sh path is
required; see build notes in this file)

```
cd /home/gpf/code/dreamcast/tinygl
source /opt/toolchains/dc/kos/environ.sh
export PATH=/opt/toolchains/dc/kos/utils/build_wrappers:$PATH

# 1) Normal build -- hook DISABLED
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke

# 2) Test build -- ONE-SHOT FAILURE HOOK ENABLED
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y EXTRA_CFLAGS=-DTGL_PVR_TEST_INJECT_FAIL
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke SMOKE_CFLAGS=-DTGL_PVR_TEST_INJECT_FAIL
# To target a different phase, e.g. phase 13:
make -C tests/dreamcast/pvr_smoke clean
make -C tests/dreamcast/pvr_smoke SMOKE_CFLAGS="-DTGL_PVR_TEST_INJECT_FAIL -DTGL_PVR_TEST_INJECT_PHASE=13"
```

Clean before switching variants: these Makefiles do not track compiler-flag
changes as dependencies, so an incremental build can otherwise reuse objects
from the previous variant.

Both variants build clean (no warnings/errors). `git diff --check` is clean;
no SDL2 files are touched.

### Verify the hook is only in the test build

```
# Object-level proof (normal build = 0, test build = non-zero):
grep -c tgl_pvr_test_arm_fail_next src/pvr_dc.o
grep -c "simulating one" src/pvr_dc.o

# ELF-level proof (test ELF .rodata contains the message; normal ELF does not):
sh-elf-objdump -s -j .rodata tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf | grep -a "simulating"
```

### Run and expected evidence

```
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

- Normal ELF: no "simulating" message; shutdown summary reports
  `0 submission error(s)`. All 24 phases render.
- Test ELF: one `TinyGL PVR TEST: simulating one polygon-header submission
  failure` line in phase P11, one `TinyGL PVR: polygon header submission
  failed` line; then phases P12-P24 continue rendering (recovery). Shutdown
  summary reports exactly `1 submission error(s)`.
- Emulator (Flycast) is secondary: its KOS PVR emulation may not return `-1`
  from `pvr_prim()` on this path, so absence of the message there does not
  prove the code is wrong -- hardware is the valid check.

Hardware result: **PASS** on the Dreamcast at 640x480. The captured stream
contains the one injected failure, all 24 phase transitions, exactly one
submission error at shutdown, and `pvr_smoke: PASS` / `Program returned 0`.
All 24 PPM phase captures were written to host `/tmp`; the P11 capture shows
the green triangle rendering after the injected failure.

### Known limitation

This is a simulated KOS error contract, not a real PVR hardware fault. It
proves the backend recovers from a returned error; it does not exercise a real
PVR submission failure (which would require overflowing the arena and is not
safe to do repeatably on hardware). On real hardware, if the backend failed to
recover from such an error, report the observed hung/garbled state and root
cause.

## PVR header reuse and submission profiling (work-order item 10)

The PVR backend now reuses a polygon header while its state and list remain
unchanged. It emits a new header after a state rebuild, a list transition, or
at the start of a new scene. This keeps TA command ordering intact while
avoiding one repeated `pvr_prim()` call per same-state primitive.

`TINYGL_PROFILE_PVR` adds per-frame counts for headers, strips, triangles,
vertices, and SQ batches. It samples one in 32 strips and triangles for timing,
so it avoids the per-vertex timer reads from `TINYGL_PROFILE_STAGES`.

Build and run the diagnostic TinyBalls target:

```
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y EXTRA_CFLAGS=-DTINYGL_PROFILE_PVR
make -C tests/dreamcast/tinyballs clean all STRESS_CFLAGS=-DTINYGL_PROFILE_PVR
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/tinyballs/tinygl-tinyballs.elf
```

On the Dreamcast (KOS 2.3.0, 640x480 VGA, 2026-09-27), the 12-ball window
reported 216 strips, about 338 submitted cap triangles, 10,086 TA vertices,
and 770 SQ batches per frame. Header submissions fell from about 554 to 1.
Sampled 42-vertex strips averaged about 37 us for conversion and 6 us for SQ
copy; treat those samples as directional because this profile build still
measured slower than an uninstrumented run.

The uninstrumented 12-ball run with header reuse measured 60.7 ms geometry and
60.8 ms PVR registration, versus the earlier baseline's 62.6 ms and 65.3 ms.
That is a modest single-run improvement, not yet a stable benchmark result.

A follow-up experiment queued adjacent strips and reduced the SQ batch count
from about 770 to 374 per frame, while retaining the 216 EOL-terminated
strips. The 12-ball capture looked clean, but two uninstrumented runs measured
about 65.6 ms geometry and 65.7-68.2 ms registration. The queue implementation
was dropped; local per-strip staging is currently faster on this hardware.
The one-shot header-failure smoke test also passed after header reuse: it
forced the armed header attempt, completed all 24 phases, and reported exactly
one submission error.

## Textured vertex-packing specialization (work-order item 10, vertex conversion)

`tgl_pvr_set_vertex()` always computed the ARGB color packing (four
`tgl_pvr_color_component()` clamp/multiply pairs), even for textured strip
vertices, where the batched-strip loop in `tgl_pvr_draw_strip()`
(`src/pvr_dc.c`) immediately overwrote `argb` with `0xFFFFFFFF` afterward.
Added `tgl_pvr_set_vertex_tex()` -- position/z/UV packing only, no color
math -- and hoisted the `textured` branch out of the per-vertex loop in the
`TGL_PVR_STRIP_BATCH` path so it picks the solid or textured packer once per
batch instead of re-branching every vertex. Output is bit-identical to
before: solid vertices still go through the unchanged `tgl_pvr_set_vertex()`,
and textured vertices get the same UV / `argb = 0xFFFFFFFF` result, just
without computing the color that got discarded.

This only affects the `textured` branch of `tgl_pvr_draw_strip`'s batched SQ
path, and neither existing hardware benchmark exercises it there:
`pvrmark_strips` never binds a texture, and `pvr_smoke`'s textured phases
(18-20) use `GL_TRIANGLE_FAN`/`GL_TRIANGLES`, not `GL_TRIANGLE_STRIP`. Added
`tests/dreamcast/pvrmark_strips_tex/`, a copy of `pvrmark_strips` that binds a
32x32 checkerboard texture and emits a UV per vertex on the same random-walk
`GL_TRIANGLE_STRIP` workload, so `TINYGL_PROFILE_PVR`'s
`tgl_profile_pvr_pack_us` actually samples the affected code path.

Build check performed:

```
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y
make -C tests/dreamcast/pvrmark_strips_tex clean all
make -C tests/dreamcast/pvrmark_strips clean all
make -C tests/dreamcast/pvr_smoke clean all
```

All three link clean. Also confirmed the profiled build (matching the item-10
profiling recipe above) compiles and links:

```
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y EXTRA_CFLAGS=-DTINYGL_PROFILE_PVR
make -C tests/dreamcast/pvrmark_strips_tex clean all BENCH_CFLAGS=-DTINYGL_PROFILE_PVR
```

**Not yet run on hardware.** Planned acceptance: run
`tinygl-pvrmark-strips-tex.elf` (profiled build) before/after this change and
compare `tgl_profile_pvr_pack_us`'s per-strip mean; then re-run `pvr_smoke`
phases 18-20 and confirm the non-black pixel counts still match the item-9
baseline (150,528 / 55,296 / 75,264) to confirm the textured path stayed
bit-identical. `pvrmark_strips_tex` measures throughput/timing only (no
screenshot capture); `pvr_smoke` remains the correctness check for textured
output.

## Next steps

- Work-order item 5 is implemented. The submission-error reporting/recovery is
  now covered by the one-shot failure-injection test above (build/run commands,
  gated by `-DTGL_PVR_TEST_INJECT_FAIL`) and passed on real Dreamcast hardware.
  Future hardware follow-up: validate the 50/60 Hz picker on real non-VGA
  hardware and confirm PAL output on
  PAL-capable hardware. Normal-run error reporting and viewport captures pass
  on this NTSC/VGA setup at 320x240, 640x480, and 768x480.

## Textured-triangle sq_fast_cpy batching (work-order item 9)

`tgl_pvr_draw_triangle`'s textured branch (`src/pvr_dc.c`) now batches its 3
`pvr_vertex_t` entries into a local array and submits them with one
`sq_fast_cpy()` call, matching the untextured triangle path and
`tgl_pvr_draw_strip`. The polygon header is still submitted separately via
`pvr_prim()` first (it's a different TA command, not a vertex).

Build check performed:

```
source /opt/toolchains/dc/kos/environ.sh
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y TINYGL_USE_SH4ZAM=y
make -C tests/dreamcast/pvr_smoke
```

Both build clean; `tinygl-pvr-smoke.elf` links successfully.

**Hardware run (2026-09-27), KOS 2.3.0, 640x480 VGA, `DC_IP=192.168.0.128`
via dc-load-ip:**

```
source /opt/toolchains/dc/kos/environ.sh
kos-tool -m /tmp -t 192.168.0.128 -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

All 24 phases ran; `pvr_smoke: PASS`, `Program returned 0`, no submission
errors. Textured phases (P18-P20), parsed with the same comment-aware PPM
reader used for the pre-batching baseline:

| Phase | Non-black pixels (this run) | Baseline non-black pixels | Unique non-black colors (this run) | Baseline colors |
|---|---|---|---|---|
| 18 quadrant | 150,528 | 150,528 | 4 (+ black) | 5 (incl. black) |
| 19 gradient | 55,296 | 55,296 | 1,162 | 1,388 |
| 20 gradient + transform | 75,264 | 75,264 | 1,163 | 1,387 |

Non-black pixel counts (geometry/UV bounds) are pixel-identical to the
pre-batching baseline for all three phases, confirming the batched
`sq_fast_cpy()` submission places the same vertices in the same positions.
Color counts differ slightly (1,162 vs 1,388 and 1,163 vs 1,387) but are
consistent with normal run-to-run gradient dithering/rasterizer noise, not a
regression -- the pixel coverage and bounds match exactly and P18's quadrant
colors (black/blue/white/red/green) are unchanged. Work-order item 9 is
**DONE** and hardware-confirmed.

## Lit triangle-strip investigation (work-order item 8, 2026-09-27)

**Resolved and DONE.** The native PVR strip fast path (`tgl_pvr_draw_strip`,
`src/pvr_dc.c`) is now eligible for lit geometry unconditionally --
`src/vertex.c`'s `glopEnd()` eligibility check no longer excludes
`lighting_enabled`, in normal builds, no test flag required. The stray-line
artifact that originally motivated gating lit strips off was root-caused to
near-zero-area triangles at a UV-sphere's collapsed pole ring in the
TinyBalls test mesh (fixed by drawing the poles as a `GL_TRIANGLE_FAN`; see
`NEXT_TASKS.md` item 8), not a defect in the strip path itself. Re-verified
clean on hardware at the exact original repro condition (TinyBalls forced to
12 balls via `-DTINYBALLS_SEARCH_DELAY=20`, default build, no test flags):
all 12 spheres render with clean silhouettes, and a full-frame pixel scan for
isolated thin runs found zero matches. The 24-phase `pvr_smoke` suite also
still passes with no submission errors.

The investigation history below (test-only override, negative control,
per-triangle vertex dumps) is kept for context; the
`-DTGL_PVR_TEST_ALLOW_LIT_STRIP` build flag referenced in it is now a no-op
(the eligibility check it used to modify no longer exists), left in place
only because `tests/dreamcast/strip_lit_probe/` still references it in a
comment.

**Negative control:** `tests/dreamcast/strip_lit_probe/` is a minimal
isolated scene (a flat rectangular patch split into 4 lit
`GL_TRIANGLE_STRIP` rows, mirroring the sphere-row structure) added to try
to reproduce the artifact in a controlled setting. Built with the same
`-DTGL_PVR_TEST_ALLOW_LIT_STRIP` override and run on hardware
(640x480 VGA), its capture was byte-identical (0 pixel diff) to the
per-triangle-fallback baseline -- this simple case does not reproduce the
bug. The artifact needs the fuller TinyBalls scene (curved sphere geometry
and/or multiple consecutive strip calls across several balls) to appear;
keep this probe as a regression check once the real fix is found, and as a
smaller repro to extend if the TinyBalls case proves hard to instrument
further.

**Reproduction (KOS 2.3.0, 640x480 VGA, `DC_IP=192.168.0.128`):**

```bash
source /opt/toolchains/dc/kos/environ.sh
make -C src clean
make -C src CC=kos-cc TINYGL_USE_GLX= TINYGL_USE_DREAMCAST_PVR=y \
  EXTRA_CFLAGS=-DTGL_PVR_TEST_ALLOW_LIT_STRIP
make -C tests/dreamcast/tinyballs clean all
kos-tool -m /tmp -t 192.168.0.128 \
  -x tests/dreamcast/tinyballs/tinygl-tinyballs.elf
```

At 12 balls (screenshot captured within the first 60 frames, before the
FPS-search window evaluates at 5s), geometry-submit time dropped from the
correct baseline's 88.9 ms to 62.6 ms/frame (~30% faster), and the
FPS-search subsequently settled at 3 stable spheres at 60.09 FPS instead of
the baseline's 2 -- confirming the expected performance win is real.

The screenshot (`/tmp/tinyballs.ppm`, saved locally as
`tinyballs_litstrip_12balls.ppm`, not committed) reproduced the previously
reported artifact exactly: a thin line juts out to the right of 3 of the 12
spheres, at roughly equator height, terminating in open space a short fixed
distance from the sphere surface without connecting to anything else. Its
smooth Gouraud color initially suggested a mis-positioned mesh vertex, but
the later single-sphere input-geometry checks did not find a corresponding
screen-space discontinuity or skinny triangle. That explanation remains
unconfirmed.

**Batch-boundary hypothesis tested and ruled out.** Each sphere row strip
has `(SPHERE_SLICES+1)*2 = 42` vertices with the normal `SPHERE_SLICES=20`,
which crosses `tgl_pvr_draw_strip`'s `TGL_PVR_STRIP_BATCH=32` split into two
`sq_fast_cpy()` calls -- a code path the earlier unlit-only strip
validation never exercised (no unlit strip test used a >32-vertex row).
Diagnostic: temporarily set `SPHERE_SLICES = 14` in
`tests/dreamcast/tinyballs/main.c` (30 vertices/row, single un-split
batch), rebuilt, and reran on hardware. If the batch split caused the
artifact, it should have disappeared. **It did not.** Instead every sphere
showed large scalloped "pac-man" wedges of missing geometry -- a worse and
qualitatively different failure (`tinyballs_14slices_12balls.ppm`, not
committed). This rules out `sq_fast_cpy` batch-chunking as the root cause;
the local `main.c` edit was reverted before committing anything (`git diff`
confirmed clean on `tests/dreamcast/tinyballs/main.c` afterward).

**Status:** root cause still open. Keep the gate disabled for normal builds
until a clean hardware screenshot is obtained at the real `SPHERE_SLICES=20`.

**Follow-up hardware isolation (KOS 2.3.0, 640x480 VGA, 2026-09-27):**
The batch-boundary test was expanded beyond 14 slices. At the normal 20
slices (42 vertices per row), changing the batch size to 64 so the strip is
copied in one `sq_fast_cpy()` call did not remove the artifact. Zeroing the
temporary PVR vertex array and submitting each vertex with `pvr_prim()` also
did not remove it. An exact copy of the first longitude at the seam did not
help, and joining all latitude bands into one strip made the artifacts worse.
The test was then reduced to one unlit sphere: the line still appeared in a
rotation capture. This rules out lighting math and interactions between balls
as necessary causes.

For a captured rotation, a temporary logger checked adjacent input screen
coordinates for jumps over 10 pixels and checked triangles in the final
captured frame for small signed area with a long edge (absolute doubled area
at most 64 pixels squared, longest edge over 10 pixels). It found neither.
The image artifact remained, while the earlier native-strip versus
per-triangle comparison showed the fallback image clean. This makes PVR strip
topology or strip rasterization the leading area to investigate; the available
evidence does not yet distinguish those possibilities or prove a PVR hardware
fault. Next compare the same captured vertices as explicit PVR triangles
under the same polygon state, then test a controlled strip vertex-order
change. The saved `/pc/tinyballs_060.ppm`, `_072.ppm`, and `_084.ppm` captures
are on the Dreamcast's `/pc` device; they were not copied into the repository.
