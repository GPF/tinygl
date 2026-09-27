# Prompt for the Next TinyGL PVR Strip Investigation Agent

## Objective

Investigate whether TinyGL can preserve `GL_TRIANGLE_STRIP` topology through
its Dreamcast PVR path and submit an eligible strip as one native PVR polygon.
Produce a concise, source-backed design and validation plan. This is a
**research-only task**: do not implement the optimization.

The central question is whether TinyGL can keep its existing transforms and
semantics while avoiding per-triangle assembly and PVR submission for a narrow,
safe class of strips. Do not restructure TinyGL's primitive architecture or
broaden scope into unrelated backend work.

## Project constraints and verified baseline

- The Dreamcast backend uses KOS PVR directly. SDL2 and GLdc are read-only
  references; do not modify either project or the KOS reference sample.
- Preserve TinyGL's software renderer and public API behavior where practical.
- The PVR path already has hardware-verified filled triangles, Gouraud colors,
  transforms/clipping, culling, depth testing/writes, textures, supported
  blending, points/lines, polygon `LINE`/`POINT`, alternate viewport sizes, and
  submission-error handling. Do not re-investigate these milestones except as
  needed to determine strip eligibility or fallback behavior.
- Existing physical Dreamcast benchmark readings, KOS 2.3.0, 640x480 VGA
  (the benchmark labels these values triangles/frame; actual TinyGL strip output
  has not yet been reconciled with the source's vertex accumulation):

  | Configuration | Triangles/frame | FPS | Triangles/sec |
  |---|---:|---:|---:|
  | TinyGL, SH4ZAM disabled | 2,100 | 59.08 | 124,078 |
  | TinyGL, SH4ZAM enabled | 2,500 | 56.10 | 140,248 |

  These are single runs. FPS varied substantially across search loads, so they
  do not establish a statistically reliable speedup. Treat the displayed
  triangle counts and triangles/sec as benchmark-reported loads until actual
  emitted triangles are counted and verified.
- The benchmark is `tests/dreamcast/pvrmark_strips/`. It compares TinyGL against
  an unmodified KOS direct PVR strip reference and uses five-second windows,
  the KOS per-frame FPS EMA, a 55 FPS target, and +2500/-200 search steps. The
  TinyGL benchmark intends to compare the same strip workload, but inspect its
  emitted geometry before assuming it submits the same triangles as the KOS
  reference. The current `glopVertex` strip branch appears to reset its vertex
  window after each group of three without retaining the preceding two
  vertices; this may cause both semantic and benchmark-count mismatches.
- Follow the project workflow in `AGENTS.md`, `NEXT_TASKS.md`, and `TESTING.md`.
  Keep investigation targeted: before opening another large file, state the
  concrete question that read answers. Read at most 120 lines per command unless
  there is a clear reason. Separate observed facts from deductions and
  unverified hypotheses.

## Initial source trail to verify

Use this as a source trail, not as a claim that the current strip semantics are
correct. Confirm every step against the current sources, especially the noted
vertex-window behavior:

```text
glBegin(GL_TRIANGLE_STRIP)
  -> src/api.c: glBegin queues OP_Begin
  -> begin setup in src/vertex.c (matrix/viewport and triangle callbacks)
  -> glopVertex in src/vertex.c transforms each vertex, sets color/texture
     coordinates/clip code, and stores it in GLContext.vertex[]
  -> GL_TRIANGLE_STRIP case increments vertex_cnt, forms a triangle when its
     local vertex_n reaches 3, resets vertex_n to 0, and parity-reorders the
     three stored vertices before gl_draw_triangle(...); verify whether any
     code retains/shifts the prior two strip vertices (the visible branch
     appears not to)
  -> src/clip.c: gl_draw_triangle performs trivial clip accept/reject,
     computes facing, culls, or invokes recursive triangle clipping
  -> selected fill callback -> gl_draw_triangle_fill
  -> Dreamcast build: tgl_pvr_draw_triangle in src/pvr_dc.c
  -> one PVR header followed by three pvr_vertex_t records, ending in EOL
```

The strip identity is lost at or before `gl_draw_triangle`, but investigate
whether the existing strip branch also fails to construct the overlapping
triangle sequence required by `GL_TRIANGLE_STRIP`. For V input vertices, a
valid strip contains `max(V-2, 0)` triangles; count the actual `gl_draw_triangle`
calls and PVR submissions from the current code for V=0..8, including parity.
Check whether any other code changes `vertex_n` or shifts vertices before
concluding. Do not describe the current implementation as a correct rolling
window unless that retention is demonstrated.

## Investigation requirements

### 1. Trace strip assembly and topology loss

Follow `glBegin(GL_TRIANGLE_STRIP)` through immediate-mode accumulation,
transform, clipping, facing/culling, polygon-mode dispatch, and final PVR calls.
Identify where vertices are retained, where parity/winding is resolved, where
each logical triangle is formed, where clipping/culling happens, and exactly
when `tgl_pvr_draw_triangle()` is called. Include a compact call-flow diagram.

### 2. Build a strip-safety matrix

For each stage below, classify it as **strip-safe without change**,
**strip-safe with metadata**, **forces breakup**, or **uncertain / requires
experiment**. Explain the classification from current TinyGL behavior:

- model/projection and viewport transforms;
- trivial accept/reject and clipping;
- front/back-face determination and culling, including how TinyGL's parity
  reorder affects facing and whether PVR applies strip parity for culling;
- flat versus Gouraud colors;
- depth state and per-vertex depth;
- texture coordinates and textured vertex formats;
- blending and opaque/translucent PVR list selection;
- polygon `FILL`, `LINE`, and `POINT` modes;
- per-primitive PVR state/header generation and state changes.

Do not presume clipping or culling makes preservation impossible. In
particular, do not assume that a single PVR culling mode cannot handle a strip:
TinyGL's parity reorder is intended to preserve triangle orientation, and
KOS's `pvr_poly_cxt_col` defaults to `PVR_CULLING_CCW`. Establish the actual
per-triangle behavior from local evidence or mark it uncertain and propose a
focused hardware test. Determine whether an unclipped strip can be submitted
directly and whether clipping could be represented as multiple strips;
distinguish feasibility from the safest first implementation choice.

### 3. Establish native PVR strip semantics from local evidence

Use the KOS version actually installed for this project (KOS 2.3.0) and inspect
only the relevant headers/source and targeted examples. Check the PVR direct
`pvrmark_strips` sample, GLdc `pvrmark_strips_gldc`, GLdc primitive submission,
and other KOS examples only when they answer a specific unresolved question.
Do not assume desktop OpenGL or modern API semantics.

Confirm and cite local file/function evidence for:

- how often a polygon header is emitted for a strip;
- `PVR_CMD_VERTEX` versus final `PVR_CMD_VERTEX_EOL` usage;
- minimum vertex count and the representation of a strip with N triangles;
- winding and alternating index parity, how PVR culling treats each triangle in
  a strip, and whether the caller must account for parity; do not infer that
  culling is unsupported just because one header covers the strip;
- any documented or observed effect of degenerate vertices;
- Gouraud and textured strip vertex stream layout;
- opaque/translucent list behavior and whether it changes strip submission;
- whether TinyGL's current depth, blend, culling, and texture header state can
  apply to one entire strip.

If source does not establish a behavior, label it unknown and propose a focused
Dreamcast experiment; do not present an assumption as a KOS guarantee.

### 4. Propose the smallest practical fast path

Evaluate an internal, opt-in Dreamcast-only route that retains the original
strip vertices through normal TinyGL vertex transform, checks eligibility,
then submits one PVR header and a single vertex stream with EOL on the last
vertex. Determine whether transformed `GLVertex` values can be converted
directly, where the new path should branch, and whether a backend entry point
such as `tgl_pvr_draw_strip(...)` is warranted. Identify the minimal assembly
metadata needed (at least original ordered vertices/count and parity context).

Consider selective clipping into multiple strips, but choose explicitly whether
the initial version should instead fall back whenever any vertex has a nonzero
clip code. Explain how culling/facing parity would be preserved; treat culling
as an evidence question rather than an assumed fallback. Address textured
strips, blend/list selection, header reuse, submission errors, and stable state
across a `glBegin`/`glEnd` block. If the current strip assembly is semantically
incorrect, state whether repairing it is a prerequisite or a separate
correctness change; do not present a behavior-changing fix as a pure speedup.

### 5. Define the narrow first optimization slice and fallback rules

Prefer this initial target unless source evidence shows it is unsafe:

- `GL_TRIANGLE_STRIP` with at least three vertices;
- `GL_FILL` polygon mode and normal render mode;
- unlit, initially untextured geometry (discuss whether the benchmark requires
  a different minimum slice);
- no clipping required;
- one stable PVR state/list/header for the whole strip;
- no unsupported state transition inside the primitive;
- the existing independent-triangle route remains the universal fallback.

State exact eligibility checks and fallback conditions. At minimum consider
clipped vertices, selection mode, non-fill polygon mode, unsupported culling or
winding combinations, lighting/flat shading, incomplete texture/state, strip
too short, primitive limits/capacity constraints, and any PVR submission error
or state change that prevents safe whole-strip submission. Avoid adding a
fallback condition unless it follows from code or PVR behavior; mark open
questions clearly.

### 6. Quantify structural submission work

Give two separate counts for a strip containing N intended triangles (V=N+2
input vertices): (a) what current TinyGL actually emits, after verifying its
assembly behavior, and (b) a correct independent-triangle expansion. Compare
both with one native PVR strip. For the currently visible `vertex_n` reset
behavior, if no other code retains vertices, the expected raw callback count is
`floor(V/3)` rather than N; state conditions such as clipping, degenerates, and
culling that can reduce backend submissions further.

| Work item | Current code (verify) | Correct expanded reference | Native strip candidate |
|---|---:|---:|---:|
| PVR polygon headers | based on actual emitted triangles | N | 1 |
| PVR vertex records | based on actual emitted triangles | 3N | N+2 |
| primitive boundaries / EOLs | based on actual emitted triangles | N | 1 |
| backend draw dispatches | based on actual emitted triangles | N | 1 |
| scene/list begin/finish calls | count from code; likely scene/list scoped | count from code | count from proposed path |
| header compilation vs submission | distinguish state-cached compilation from per-primitive submission | same distinction | same distinction |

Separate confirmed counts from any assumptions about batching inside KOS. The
correct expanded reference is a structural comparison, not necessarily the
current TinyGL behavior. Do not use benchmark-reported `polycnt` as actual
triangle count without a source-level or instrumented submission count.
Describe likely effects on SH-4 CPU work, store-queue/PVR command traffic, TA
processing, and memory/cache traffic without inventing cycle counts or claiming
unmeasured gains.

### 7. Define correctness and performance validation

Recommend a small dedicated hardware-visible strip correctness test before
performance acceptance. It must cover:

- strips with 3, 4, 5, 6, and at least 8 vertices, checking expected triangle
  counts (`max(V-2, 0)`) and overlap between consecutive triangles;
- a triangle-list oracle that submits the exact triangles expected from each
  strip, for image/count comparison; do not use the current strip path as the
  correctness oracle until its assembly is verified;
- alternating winding/parity and culling disabled, front culling, and back
  culling where eligible, including a test that determines whether PVR strip
  culling matches TinyGL's parity-adjusted per-triangle facing;
- distinct Gouraud colors;
- textured strip if texture submission is included in the first slice;
- transformed geometry;
- a strip crossing the clip volume that demonstrably takes the existing
  fallback path if clipping is excluded;
- at least one other explicit fallback case.

Use captured pixels or visible hardware output as final correctness evidence;
an emulator-only result is insufficient for PVR behavior.

Keep `pvrmark_strips` as the primary performance test and do not modify the KOS
direct sample. First verify the number and geometry of PVR triangles each
variant actually submits. Compare these five variants on physical Dreamcast, KOS 2.3.0,
640x480 VGA, with the existing five-second windows, per-frame PVR FPS EMA,
55 FPS target, and +2500/-200 search:

1. KOS direct PVR reference;
2. TinyGL current strip implementation, SH4ZAM off (label as-is; count actual
   submitted triangles and do not assume equivalent geometry);
3. TinyGL current strip implementation, SH4ZAM on (same count caveat);
4. TinyGL strip path, SH4ZAM off;
5. TinyGL strip path, SH4ZAM on.

If current TinyGL output is not geometrically/count equivalent to the KOS
reference, add a semantics-matched expanded-triangle control (for example,
submit the exact expected triangles with `GL_TRIANGLES`) before attributing
differences to strip submission. Keep the existing KOS reference unchanged.
Clean rebuild between compiler-flag variants. Record actual PVR triangle/vertex
counts alongside benchmark-reported load, final load threshold, FPS at
threshold, actual triangles/sec, API-build time, draw-pipeline time,
scene-submit time, PVR render/transfer stats, and PVR vertex count. Recommend
and use at least five independent runs per variant as an initial repeat count
to characterize the already observed run-to-run variability; report
individual runs plus median and range, not just the best run. Increase repeats
if the spread obscures the comparison. Do not invent or silently change the
KOS benchmark methodology.

## Deliverable

Write a concise investigation report containing:

1. current pipeline trace, exact strip-topology loss point, and verified
   current emitted-triangle behavior;
2. strip-safety matrix;
3. local KOS PVR strip requirements and references;
4. current-versus-proposed submission-cost table;
5. recommended first implementation slice and exact fallback rules;
6. correctness test plan and performance test plan;
7. likely TinyGL files to change, without editing them;
8. recommendation on whether evidence supports implementing this fast path next.

Keep the report evidence-backed and clearly mark observed facts, deductions,
and unresolved questions. The final recommendation must distinguish structural
promise from demonstrated performance.

## Scope guardrails

- Do not implement the optimization or modify TinyGL source during this task.
- Do not rewrite primitive assembly or remove the existing triangle route.
- Do not modify KOS, GLdc, SDL2, or the KOS direct benchmark sample.
- Do not add unrelated APIs or optimize clipping, lighting, textures, or
  blending except to answer strip-safety questions.
- Do not claim a performance gain until the matched physical-hardware runs
  support it.
- Do not commit changes.
