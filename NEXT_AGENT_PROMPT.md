# Prompt for the Next TinyGL Dreamcast PVR Agent

You are taking over a small, early Dreamcast PVR backend integration in TinyGL.
Work carefully: this is real Dreamcast hardware code, and a successful build or
emulator image does not prove the PVR path is correct.

## Objective

Advance the existing PVR backend by extending its hardware smoke test to cover
three behaviors that are not yet validated:

1. Per-vertex Gouraud color interpolation.
2. A TinyGL transform applied before vertices reach the PVR.
3. Clipping for triangles crossing each screen edge.

Make only the smallest safe code changes needed to make those behaviors visible
and testable. Do not start implementing textures, depth testing, GL culling,
blend modes, or point/line output in this task. Those are later milestones.

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

Read targeted sections only. Before opening additional files, state what
question that read is meant to answer. Do not repeatedly scan the whole repo.

## Known starting point

- `glInitPVR(width, height)` initializes the PVR backend.
- TinyGL performs its ordinary transform and clip path before dispatching
  filled, untextured triangles to `tgl_pvr_draw_triangle`.
- `src/pvr_dc.c` currently emits screen-space X/Y, fixed Z=0.5, and per-vertex
  ARGB values in a Gouraud PVR polygon context.
- Points and lines are currently no-ops on the PVR path. Textured triangles
  are skipped. PVR depth comparison is always, so GL depth behavior is not
  implemented.
- The existing smoke test displays a centered red triangle on black for 600
  frames on the user's Dreamcast. The user visually confirmed it looked right;
  dc-load reported `pvr_smoke: PASS` and `Program returned 0`.
- The target environment used KOS 2.3.0, 640x480 VGA, `kos-cc`, and the local
  KOS port of SH4ZAM. The console is available through dc-load-ip at
  `192.168.0.128` (verify that the address is still right before loading).
- The SDL2 Dreamcast directories are reference-only. Do not edit anything in
  `/home/gpf/code/dreamcast/SDL2/`.

## Investigation requirements

Before editing, trace the actual code path from the smoke test's GL calls to
PVR vertex submission. Confirm the smoke target links the TinyGL source/library
you are inspecting. Check how TinyGL represents current/per-vertex colors,
matrix transforms, viewport coordinates, and clipped/generated vertices.
Identify what `glBegin`/`glEnd`, `glColor*`, and matrix calls are supported by
this TinyGL version instead of assuming modern OpenGL behavior.

Separate verified facts from hypotheses. In particular, do not assume the PVR
Gouraud state is correct just because the polygon header requests Gouraud
shading: verify that the test passes distinct colors per vertex and that the
hardware shows interpolation. Do not assume clipping is covered merely because
the triangle is partly off screen; design cases that cross each viewport edge
and check that the resulting visible geometry is bounded and correctly colored.

## Implementation requirements

- Extend the existing smoke scene in clear, labeled phases that are easy to
  identify visually. Keep the black background and retain the original solid
  red centered triangle as a baseline if practical.
- Add a triangle with three distinct vertex colors. Use obvious colors (for
  example red, green, and blue) so interpolation can be seen on a CRT/display.
- Add a transformed triangle using TinyGL matrix operations that this codebase
  actually supports. The expected location/rotation should be easy to recognize
  and should not accidentally depend on unimplemented GL state.
- Add cases crossing left, right, top, and bottom screen edges. Keep enough
  geometry on screen to identify each case. If TinyGL clips to canonical view
  volume before viewport conversion, test that path rather than working around
  it in the PVR driver.
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

Load with `kos-tool -t "$DC_IP" -x
tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf` after setting `DC_IP` to the
Dreamcast's current dc-load-ip address. Observe the physical console output;
do not claim hardware success from compilation or emulator output alone.

If the console is unavailable, complete the implementation and build, clearly
mark hardware validation as pending, and give the exact command needed to run
it. Do not claim that color interpolation, transform, or clipping passed on
hardware without an observed result.

## Completion checklist

Before handing back:

1. Review the final diff and ensure no SDL2 files changed.
2. Run `git diff --check`.
3. Report the exact files changed and why.
4. Report build results and hardware observations separately.
5. List any unresolved issues and the precise next task.

Keep the final report concise and factual. Agent proposes; agent verifies;
Dreamcast hardware decides.
