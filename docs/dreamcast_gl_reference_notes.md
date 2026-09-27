# Reference notes: how other Dreamcast GL backends handle what's slow in TinyGL

Captured during the TinyBalls profiling pass (2026-09-26/27) so the findings
aren't lost in chat history. These are facts about other codebases, gathered
for comparison -- none of this is a claim about TinyGL's own code unless
explicitly said so.

## Sources checked

- **GLdc**, local checkout: `/home/gpf/code/dreamcast/GLdc` (your fork,
  `gitlab.com/gpferror/GLdc`, ahead of upstream `gitlab.com/simulant/GLdc`).
- **kos-ports `libGL`** (`/opt/toolchains/dc/kos-ports/libGL/dist/libGL-1.1.1`):
  this is *also* GLdc, same repo, just an older checkout pulled in as the
  kos-ports package. Not a separate implementation.
- **libKGL** (`https://github.com/KallistiOS/libkgl.git`), the old, explicitly
  deprecated KallistiGL project (per its kos-ports `pkg-descr`). Cloned to
  `/home/gpf/code/dreamcast/libkgl`, sibling to GLdc and SDL2, for future
  reference -- not part of any TinyGL build.
- **SDL2** (`/home/gpf/code/dreamcast/SDL2/src/render/dreamcast/`,
  `src/video/dreamcast/`): reference-only per project AGENTS.md, for PVR/video
  mode setup, not GL specifically.
- **DCSinge** (`/home/gpf/code/dreamcast/DCSinge`), specifically
  `src/dcfmv.c` (FMV frame-quad submission): a non-GL, hand-rolled PVR
  consumer -- a second data point on raw PVR submission style, outside any
  GL implementation.
- KOS PVR headers/source:
  `/opt/toolchains/dc/kos/kernel/arch/dreamcast/include/dc/pvr.h`,
  `.../hardware/pvr/pvr_scene.c`.

## 1. GLdc's immediate-mode architecture (the big structural difference)

TinyGL's `glVertex`/`glNormal`/etc. each go through `gl_add_op()`
(`src/list.c:135`): a global-context fetch + an indirect call through a
per-opcode function-pointer table (`op_table_func[]`), even in plain
immediate-mode (non-display-list) execution. Every GL call pays this, and
every triangle additionally gets its own call into
`gl_draw_triangle()`/`tgl_pvr_draw_triangle()`.

GLdc's `GL/immediate.c` has none of that. Its own header comment says it
outright:

> "This implements immediate mode over the top of glDrawArrays"

- `glVertex3f()` (`GL/immediate.c:225`) just writes floats directly into a
  plain growable array (`AlignedVector VERTICES`) at the current index. No
  opcode dispatch, no function-pointer table, no param marshaling struct.
- `glEnd()` (`GL/immediate.c:329`) calls `glDrawArrays()` **once** for the
  whole buffered primitive.
- `GL/draw.c:1084` `submitVertices()` transforms, lights, and clips the
  *entire* vertex array in one pass (`generate()`), loading the transform
  matrix once per draw call (`_glTnlLoadMatrix()`), not once per `glBegin`
  (TinyGL) and not per-vertex (as a naive SH4ZAM patch would do).
- There's a Dreamcast-specific "packed immediate strip" fast path
  (`can_generate_packed_immediate_strip` / `generate_packed_immediate_strip`)
  that skips the general vertex struct entirely for the common
  triangle-strip case.
- `GL/platforms/sh4.c:475` `SceneListSubmit()` pushes the *entire*
  transformed vertex array to the PVR in one function call / one strip loop,
  using `pvr_dr_init()` + `sq_fast_cpy()` (`_glPushVertex`, line 145).

**Takeaway:** GLdc's speed advantage over TinyGL's immediate mode isn't a
smarter PVR write -- see below -- it's that it never pays per-call/per-triangle
dispatch overhead in the first place. Batching the *whole* primitive before
transform/light/submit is the structural fix; a real port of this idea into
TinyGL is a genuine rewrite of `vertex.c`/`list.c`'s immediate-mode path, not
a small patch. Not attempted yet.

## 2. PVR submission: TinyGL is already on the fast path

Checked whether there's a faster way to get vertices to the PVR than what
`src/pvr_dc.c` already does.

- `tgl_pvr_init()` (`src/pvr_dc.c:9-18`) configures **direct submission
  mode** (`dma_mode = 0`), so `pvr_prim()` calls
  (`kernel/.../pvr_scene.c:244`) take KOS's fastest path:
  `sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), data, size>>5)` -- straight into
  the TA's store-queue input, no DMA buffer, no RAM staging.
- GLdc's own SH4 backend (`GL/platforms/sh4.c`) uses `pvr_dr_init()` +
  `sq_fast_cpy()` for the same reason -- same underlying primitive, not a
  different/faster one.
- libKGL's `gl-pvr.c` batches into a RAM vertex buffer before a DMA submit --
  same "batch then push" idea as GLdc, nothing new there either.
- **DCSinge's `dcfmv_submit_current_video()`** (`src/dcfmv.c:2975-2985`) shows
  a detail the above missed: `sq_fast_cpy(dest, data, count)` takes a *count*
  of 32-byte blocks and copies them all in one call --
  `sq_fast_cpy(sq_dest_addr, fmv->vert, 4)` pushes all 4 vertices of a quad
  in a single call, not four. TinyGL's `pvr_dc.c` calls `pvr_prim()` (which
  reduces to one `sq_fast_cpy(..., 1)`) separately **per vertex**, in a loop,
  for both `tgl_pvr_draw_triangle()` and `tgl_pvr_draw_strip()`.

**Takeaway:** the store-queue write itself isn't the bottleneck (confirmed:
TinyGL already uses the fastest KOS primitive for a single write). But
TinyGL is issuing that primitive once per vertex when the vertices are
contiguous and could go out in one `sq_fast_cpy` call covering the whole
triangle or strip. That's a smaller, more surgical version of "batching"
than the full GLdc-style rewrite in §1 -- it doesn't touch the
transform/lighting/dispatch pipeline at all, just how the already-assembled
`pvr_vertex_t` array for a strip/triangle gets pushed to the TA. Worth
trying in `tgl_pvr_draw_strip()`/`tgl_pvr_draw_triangle()` before attempting
anything bigger. Not yet implemented or measured.

## 3. libKGL's SH4 lighting assembly (already exploited)

`gl-sh4-light.S` in the old libKGL hand-writes per-vertex lighting
(`_glKosSpotLight`, `_glKosSpecular`) using two single-instruction SH4
tricks:

- `fipr` -- 4-component dot product in one instruction (N.L, N.H, and
  magnitude-squared).
- `fsrra` -- reciprocal square root approximation in one instruction, used
  to normalize vectors without calling `sqrt()`.

This is exactly the math TinyGL's `gl_shade_vertex()` (`src/light.c`) was
doing in scalar C with a real `sqrt()` call. SH4ZAM already exposes the same
primitives portably (`shz_vec3_dot`, `shz_vec3_normalize`,
`shz_vec3_magnitude`), so no asm porting was needed.

**Status: done, not just noted.** Commit `510849b` added an
`TINYGL_USE_SH4ZAM` path to `gl_shade_vertex()` using these SH4ZAM calls.
Measured ~8% reduction in the lighting stage on real hardware (TinyBalls,
12 balls: 24.1ms -> 22.2ms), consistent across all tested loads, no
regression in any other profiled stage.

## 4. The native PVR strip path already in TinyGL -- deliberately unfinished

`src/vertex.c`'s `glopEnd()` has a native `GL_TRIANGLE_STRIP` fast path
(`tgl_pvr_draw_strip()` in `src/pvr_dc.c`) that submits a whole strip as one
PVR header + N vertices, instead of the fallback's one header + 3 vertices
*per triangle* with no vertex sharing. This was investigated and gated in
`docs/strip_fast_path_investigation.md`.

That doc's eligibility gate explicitly excludes `lighting_enabled` --
**but its own text says this was scoped out on purpose** ("lighting on ->
fallback (colors aren't lighting-derived in this TinyGL; out of slice 1)"),
not because of a discovered correctness bug. Checked the current code: by
the time `glopEnd()` runs, `gl_shade_vertex()` has already written the final
lit color into `GLVertex.color` (`src/vertex.c` glopVertex, before
`glopEnd`), and `tgl_pvr_draw_strip()`'s vertex packer
(`tgl_pvr_set_vertex()`, `src/pvr_dc.c:102`) just reads `source->color`
unconditionally -- it doesn't care whether that color came from lighting or
`glColor`. So the lit case looks structurally ready to route through the
strip path too; this was mid-investigation when the note above was written
and hasn't been tried on hardware yet.

**This is the most promising next step for the PVR-submit bottleneck**
(currently the largest untimed-savings target: ~40ms/frame at 12 balls in
TinyBalls' fallback path) -- likely smaller and lower-risk than the full
GLdc-style batch-immediate-mode rewrite in §1, since the strip submission
machinery already exists and is hardware-validated for the unlit case.

Update (2026-09-27): **done, promoted to default.** See `NEXT_TASKS.md`
item 8 -- the stray-line artifact that had blocked this was root-caused to
degenerate pole triangles in the TinyBalls test mesh, not a strip-path bug;
`glopEnd()`'s eligibility gate no longer excludes `lighting_enabled`.

## 5. SH4ZAM matrix-multiply port -- attempted and reverted (2026-09-27)

Tried porting `src/zmath.c`'s `gl_M4_Mul()`/`gl_M4_MulLeft()` (used by
`glTranslate`/`glRotate`/`glMultMatrix`/`glFrustum` via `matrix.c`, and by
`vertex.c`'s per-`glBegin` modelview*projection composition) to SH4ZAM,
mirroring the existing per-vertex-transform and lighting ports. The
approach: load operand A into XMTRX via the same
`shz_xmtrx_load_transpose_unaligned_4x4()` already used elsewhere, then
compute each column of the result as `shz_xmtrx_transform_vec4()` applied
to the matching column of operand B -- built entirely from primitives
already validated in production, no new assumption about SH4ZAM's
"apply"/multiply-order semantics.

**Reverted.** `pvr_smoke`'s P3 (translate+rotate transform) rendered fully
black with this change -- the triangle vanished, presumably clipped after
a corrupted transform. A debug build that dumped both the SH4ZAM result
and a parallel scalar-reference computation for the first several calls
showed **zero numeric difference** (`maxdiff=0` every time, including the
exact translate+rotate matrices from the failing phase) -- the multiply
itself is correct. Removing only the debug printf/reference-computation
code (keeping the exact same multiply logic) made the corruption
reappear; re-adding it made the image correct again, with no other change.

**This points at a compiler code-generation or SH4ZAM inline-assembly
clobber-list issue, not a logic bug in the multiply algorithm** --
something register-allocation-sensitive that the debug code's extra local
variables/calls happened to perturb out of the corrupted state. This is
the kind of bug that could pass or fail testing depending on unrelated
surrounding code changes, so it was reverted rather than shipped. The
change is NOT in `src/` -- `zmath.c`/`zmath.h` are back to their original
scalar-only state.

**If revisited:** don't trust a single hardware pass/fail without first
confirming the result is stable across at least two rebuilds with
different unrelated code nearby (the opposite of what happened here).
Worth checking whether `shz_xmtrx_load_transpose_unaligned_4x4()` and
`shz_xmtrx_transform_vec4()`'s inline-asm clobber lists (`sh4zam` port,
`shz_xmtrx_sh4.inl.h`) fully declare every FR/XF register they touch,
particularly around the `frchg` back-bank swap in the load path -- an
incomplete clobber list would let GCC keep a stale value live in a
register the assembly silently overwrites, exactly matching what was
observed.
