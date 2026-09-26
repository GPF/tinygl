# Next Tasks

## Current PVR coverage

The hardware-confirmed path initializes/shuts down KOS PVR, updates the PVR
background clear color, flushes scenes, and draws untextured filled triangles.
TinyGL transforms and clips triangle vertices before submission. The current
smoke test displayed a centered solid red triangle on real hardware.

The backend currently has these known gaps:

- Textured triangles are skipped.
- Points and lines produce no PVR output.
- PVR vertices use fixed depth (`0.5`) and depth comparison is always, so GL
  depth testing and depth writes are not implemented.
- GL culling and polygon modes are not translated to PVR state.
- Video-mode and viewport behavior beyond the fixed 640x480 smoke setup has
  not been validated.
- PVR submission errors are logged, but failure recovery and state handling
  have not been exercised.

## Recommended work order

1. Extend the smoke test with Gouraud vertex colors, a transformed/rotating
   triangle, and clipped triangles at each screen edge. Verify color packing,
   viewport mapping, and clipping against the software renderer where behavior
   overlaps.
2. Map GL culling and polygon mode state to PVR behavior; add front/back winding
   and fill/line/point mode tests. Implement PVR points and lines if those modes
   are in scope.
3. Implement GL depth state: map depth range and transformed Z to PVR Z, honor
   depth function and depth mask, and test overlapping triangles in both draw
   orders.
4. Add textured-triangle submission with TinyGL texture coordinates and
   perspective behavior. Cover texture formats/filtering supported by TinyGL.
5. Implement and test supported blend modes and alpha handling.
6. Validate initialization and viewport sizing with alternate KOS video modes,
   and improve recovery/reporting for PVR scene/list/primitive submission errors.

Keep each change small and add a visible hardware test scene alongside it.
Inspect vertex flags, color packing, UV/depth mapping, alignment, cache
handling, and PVR state lifetime when output differs. Put repeatable commands
and observed test results in `TESTING.md`; keep implementation direction and
open work here.
