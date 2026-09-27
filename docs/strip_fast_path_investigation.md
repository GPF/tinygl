# Research-only investigation: native `GL_TRIANGLE_STRIP` fast path in the Dreamcast PVR backend

**Status:** Initial gated implementation is in `src/vertex.c` and `src/pvr_dc.c`.
Real Dreamcast correctness and performance validation are still pending.
Everything below is source-verified with line references on KOS 2.3.0 and TinyGL commit
`7fceb72`. A host simulation of `glopVertex`'s strip branch was compiled and run to *prove* the
current emitted-triangle behavior empirically (see §1.3). Assumptions are flagged *open question*.

**Author:** agent investigation handoff for Troy.

---

## 0. Summary verdict (TL;DR)

- **The current strip branch is *semantically correct*.** It emits exactly `max(V-2, 0)` triangles
  for `V` input vertices, each with the correct vertex **set** and correct **orientation** vs the
  GL reference (host-simulated, §1.3, and hand-traced in §1.2). The prompt's initial hypothesis that
  the branch "resets its vertex window without retaining the preceding two vertices" is **not
  supported by the source**: the branch resets the store index `n` to 0 but never clears slots 1 and
  2, so the two prior vertices are implicitly retained. The prior report's framing ("strip destroyed
  / count mismatch") conflated *topology loss* with *semantic incorrectness*.
- **The topology IS lost, structurally.** By the time `src/pvr_dc.c::tgl_pvr_draw_triangle` runs,
  each triangle is a **separate** PVR polygon-header + 3-vertex + 1-EOL submission. A strip of `N`
  triangles becomes `N` independent PVR triangles. The *output* is correct; the *submission* is
  un-batched. So this is a **pure performance optimization**, not a correctness fix. (If the
  benchmark's `polycnt` was read as "actual emitted triangles," it was already correct: the
  benchmark submits `polycnt + 2` vertices, so it emits exactly `polycnt` triangles — no reconciliation
  is needed.)
- **The PVR hardware natively supports strips.** `PVR_CMD_POLYHDR = 0x80840000` already has
  `auto_strip_len` (bit 23) set, so a strip is `poly header + (N+2) vertices + 1 EOL on the last
  vertex` with **no header change**. `bruces_balls.c` and GLdc's `genTriangleStrip` are canonical
  raw-strip submits. The header TinyGL already compiles is usable for a native strip.
- **A native strip is structurally cheaper and currently gated to a restricted class:**
  `GL_TRIANGLE_STRIP`, `GL_FILL`, no culling (`GL_BACK`/`GL_FRONT`/`GL_FRONT_AND_BACK`), unlit,
  Gouraud/solid colors, **all vertices unclipped** (`clip_code == 0`), and — conservatively — the
  hard gate is culling (see §2/§3). Everything else falls back to the existing per-triangle path.
- **Recommendation:** the evidence supports implementing the fast path **next**, but only behind a
  single eligibility gate that defaults to the safe per-triangle fallback, and validated strip-only
  correctness on real hardware before widening the gate. The win is command/assembly overhead, not
  rasterization, so do not claim a performance gain until matched physical runs support it.

---

## 1. Pipeline trace (verified)

Full call chain, with line refs on the current sources:

| Stage | File:line | What happens | Strip identity |
|---|---|---|---|
| `glBegin(GL_TRIANGLE_STRIP)` | `api.c:254-258` | pushes `OP_Begin`; sets `begin_type`, zeroes `vertex_n`/`vertex_cnt` | **preserved** |
| `gl*Vertex3f` | `api.c:23,28…` | pushes a `glVertex` op | **preserved** |
| `glopVertex` (immediate-mode vertex assembly) | `vertex.c:230`, strip case `319-335` | transforms, bakes color/tex/clip_code, stores vertex in `vertex[]`, **emits one triangle per step via a 3-slot window + `cnt & 1` parity reorder** | **topology lost; semantics kept** |
| `glEnd()` | `api.c:264-268` | pushes `OP_End`; triggers `tgl_vtx_flush()` → `tgl_vtx_finish()` | — |
| `gl_draw_triangle` | `clip.c:258` | trivial accept/reject, facing, culling, degenerate guard, clip split | 3 verts only |
| `gl_draw_triangle_fill` | `clip.c:423` | routes to backend | 3 verts only |
| `tgl_pvr_draw_triangle` | `pvr_dc.c:423` | textured or solid path; submits **one PVR triangle** | 3 verts only |

**Topology-loss point:** `glopVertex` → `gl_draw_triangle`. The strip's count and "this is
triangle *i* of *N*" information is gone the moment each triangle is dispatched independently.
The PVR backend never sees a strip. Reclaiming it requires a new strip-aware assembly stage that
intercepts the vertex stream at `glopVertex` (or expands it at `glEnd`).

### 1.1 Call-flow diagram
```
glBegin(TRIANGLE_STRIP)
  glopVertex (per input vertex):
    gl_vertex_transform        -> clip_code, pc
    bake color/tex             -> v->color, v->tex_coord
    clip_code==0 ? gl_transform_to_viewport : not     (deferred until accepted)
    strip window: slot[n]=v; n++;
    if (cnt>=3) { if (n==3) n=0;
                  switch(cnt&1):
                    0 -> gl_draw_triangle(slot2,slot1,slot0)
                    1 -> gl_draw_triangle(slot0,slot1,slot2) }
  glEnd()
gl_draw_triangle (per emitted triangle):
    co = cc0|cc1|cc2
    if co==0:  norm, degenerate guard, front = (norm<0) ^ current_front_face,
               culling decision -> draw_triangle_front/back  (== gl_draw_triangle_fill)
    else:      gl_draw_triangle_clip -> sub-triangles -> gl_draw_triangle
gl_draw_triangle_fill -> tgl_pvr_draw_triangle (one PVR triangle each)
```

### 1.2 Hand-trace of the strip window (V=0..8)
`n` (= `vertex_n`) cycles `0,1,2,0,1,2,…` because it increments each vertex and resets at 3.
The three slots always hold the **three most recent** input vertices; slots 1 and 2 are only
overwritten once slot 0 fills. The `cnt & 1` switch is a parity swap that preserves orientation.
Each emitted triangle is a cyclic rotation and/or parity swap of the GL reference triangle — both
preserve vertex set and signed-area sign. (Detailed in §1.3.)

### 1.3 Host simulation — the current strip is correct (empirical)
A standalone model of the strip branch (`switch(cnt&1)` + `if(n==3)n=0` + 3-slot storage) was
compiled and run over `V=0..8`. For every `V` it verified three properties against the GL reference
(`k` even → `(k,k+1,k+2)`, `k` odd → `(k+1,k,k+2)`) using a canonical alternating staircase strip so
orientation is well-defined:

| V | `gl_draw_triangle` calls | ref triangles | set match | orientation match |
|---|---:|---:|---:|---:|
| 0,1,2 | 0 | 0 | — | — |
| 3 | 1 | 1 | yes | same sign |
| 4 | 2 | 2 | yes | same sign |
| 5 | 3 | 3 | yes | same sign |
| 6 | 4 | 4 | yes | same sign |
| 7 | 5 | 5 | yes | same sign |
| 8 | 6 | 6 | yes | same sign |

**Verdict:** `gl_draw_triangle` is called exactly `max(V-2,0)` times; each emitted triangle has the
correct vertex **set** and correct **orientation**. The current branch is semantically correct. The
prior report's claim of a count/winding bug is **not supported**.

### 1.4 Benchmark reconciliation (important)
`tests/dreamcast/pvrmark_strips/main.c:do_frame` submits `1 + polycnt + 1 = polycnt + 2` vertices
(one before the loop, `polycnt` inside, one after). A strip with `V = polycnt + 2` vertices emits
`V - 2 = polycnt` triangles. So the benchmark's `polycnt` **is** the actual emitted triangle count.
The displayed "triangles/frame" labels are accurate for the current (correct) code — **no count
reconciliation is required**, contrary to the prompt's open question. (This does not change the
benchmark's throughput numbers; it only removes a suspected source-level mismatch.)

---

## 2. Strip-safety matrix

Each stage is tagged **SS-without-change**, **SS-with-metadata**, **breaks**, or **uncertain**.

| Stage | Class | Evidence |
|---|---|---|
| Model/projection + viewport transform | **SS-without-change** | Each vertex is already transformed to clip space and (if `clip_code==0`) viewport-mapped in `glopVertex`/`clip.c:gl_transform_to_viewport`. A native strip reuses the same transformed vertices — no extra math. |
| Trivial accept/reject + degenerate guard | **SS-without-change** (per triangle) | `clip.c:258` computes `norm`; `norm==0` returns (degenerate). For an unclipped strip the TA also skips zero-area triangles. Behavior can differ for *separating* degenerates — see §3. |
| Clipping (non-zero clip_code) | **breaks** (for the single-strip path) | `clip.c:258` `co!=0` → `gl_draw_triangle_clip`, which splits a triangle into sub-triangles. A single PVR strip cannot represent a clipped strip. Must fall back (or split into sub-strips at the clipped vertex — deferred, §5). |
| Facing + culling | **uncertain / requires experiment** | TinyGL resolves facing per triangle in `clip.c:273-303`; PVR contexts currently use `PVR_CULLING_NONE`. The initial path conservatively falls back whenever culling is enabled. Whether PVR native strip culling accounts for strip parity is unestablished; test `CCW`/`CW` culling on both strip windings against an expanded-triangle oracle before widening eligibility. `GL_FRONT_AND_BACK` remains a no-draw case in TinyGL. |
| Flat vs Gouraud colors | **SS-without-change** | Per-vertex `color` interpolates natively in the PVR. **Note:** the unlit PVR backend ignores `GL_FLAT` (only `clip.c` clipping interpolation consults `current_shade_model`; the unclipped fill path at `clip.c:423` → `pvr_dc.c` bakes per-vertex colors). So under `GL_FLAT` the current backend already emits per-vertex colors (Gouraud), and a native strip does the same → **pixel-identical regardless of shade model**. The "exclude `GL_FLAT`" guardrail (§5) is therefore conservative, not required by this backend. |
| Depth state + per-vertex depth | **SS-without-change** | A single header covers the whole strip; per-vertex `zp.z` is baked. Fine if depth state is stable across the `glBegin`/`glEnd` (it always is). |
| Texture coordinates / textured format | **SS-without-change** (if unclipped) | Per-vertex `tex_coord` + one shared textured header. Requires `texture_2d_enabled` + non-NULL pixmap (else falls to solid). See §5. |
| Blending / opaque vs translucent list | **SS-without-change** | One `glBegin`/`glEnd` strip has stable blend state → one header. `tgl_pvr_select_list` transitions OP→TR once per scene when blending appears; a list cannot be reopened in a scene. Mixing opaque/translucent *within* one strip is impossible (and cannot happen inside one `glBegin`). |
| Polygon FILL / LINE / POINT | **breaks** for LINE/POINT | `GL_FILL` renders the strip directly. `GL_LINE`/`GL_POINT` need edge/point emission per vertex/edge (`gl_draw_triangle_line/point`, `pvr_dc.c` line/point paths) — not a fill strip. Must fall back. |
| Per-primitive PVR state / header gen | **SS-without-change** | `tgl_pvr_update_header` rebuilds only on depth/blend change; culling is always `NONE`. One header covers the whole strip. |

Clipped strips fall back in this first slice; splitting them into sub-strips is deferred. Culling is
also conservatively excluded until PVR strip parity behavior is checked on hardware.

---

## 3. Local KOS 2.3.0 PVR strip requirements (evidence)

- **Strip support is native.** `pvr_header.h:245-248`: `strip_len:2` (19-18), `auto_strip_len:1`
  (bit 23), `strip_end:1` (28), `list_type:3` (26-24). `pvr.h:580`: `PVR_CMD_POLYHDR = 0x80840000`
  → bit 23 `auto_strip_len` is **set**, bits 31-29 = 4 (`PVR_HDR_POLY`). So the TA **auto-selects**
  strip length; we need not set `strip_len`. `pvr.h:582-583`: `PVR_CMD_VERTEX = 0xe0000000`,
  `PVR_CMD_VERTEX_EOL = 0xf0000000` (end of strip).
- **A strip of N triangles = `N+2` vertices, only the last carrying EOL.** `bruces_balls.c:444`
  compiles **one** `pvr_poly_hdr` once and `465` submits it once per frame; `297-298` put
  `PVR_CMD_VERTEX_EOL` on the final vertex of a streamed vertex strip. GLdc `GL/draw.c:134`
  `genTriangleStrip` writes input vertices in order and sets EOL only on `output[count-1]`. Both are
  canonical and require **no header change** for a native strip.
- **Culling enum.** `pvr_header.h:78-81`: `PVR_CULLING_NONE=0, SMALL=1, CCW=2, CW=3`.
  `pvr_prim.c:120` (`pvr_poly_cxt_col`) defaults `culling = PVR_CULLING_CCW`. TinyGL overrides to
  `PVR_CULLING_NONE` and handles facing in software. The initial native strip also uses `NONE`;
  parity-aware hardware culling remains an explicit experiment, not an established limitation.
- **Degenerate vertices.** The PVR TA skips zero-area triangles; TinyGL skips `norm==0`
  (`clip.c:276`). For simple repeated-vertex strip separations these mostly agree, but coplanar /
  separating degeneracies can diverge. **Open question** — verify on hardware (§6.1.4) before relying
  on it; treat divergent degeneracy as a fallback trigger.
- **Gouraud / textured stream layout.** Gouraud: per-vertex `argb` from `color`; textured: per-vertex
  `u/v` + white `argb` (GL_DECAL), header `PVR_TXRFMT_RGB565`, 256×256, `PVR_FILTER_NEAREST`. Both
  consume one vertex record each; the strip stream is the same shape.
- **Opaque/translucent lists.** `PVR_LIST_OP_POLY` vs `PVR_LIST_TR_POLY`; one transition per scene,
  no re-open. A strip stays on whichever list its (stable) blend state selects. Submission does not
  change for strips vs triangles.
- **Header reuse across a strip.** TinyGL's depth/blend header cache applies to one whole strip if
  state is stable (it always is within one `glBegin`/`glEnd`). **Open question:** reconfirm the exact
  `auto_strip_len` vs fixed `strip_len` behavior on hardware before relying on it (§0 / §6).

If any source does not establish a behavior, it is labeled open and proposed as a focused hardware
test rather than a KOS guarantee.

---

## 4. Current-versus-proposed submission-cost table

For a strip with `N` intended triangles (`V = N + 2` input vertices). Confirmed counts from source;
KOS internal batching assumptions are separated.

| Work item | Current code (verified) | Correct expanded reference (N triangles) | Native strip candidate |
|---|---:|---:|---:|
| PVR polygon headers | `N` (one per emitted triangle; `tgl_pvr_draw_triangle`) | `N` | **1** |
| PVR vertex records | `3N` (3 per emitted triangle) | `3N` | **`N+2`** |
| Primitive boundaries / EOLs | `N` (EOL on 3rd vertex of each triangle) | `N` | **1** |
| Backend draw dispatches (`tgl_pvr_draw_triangle`) | `N` | `N` | **1** |
| Scene/list begin·finish | scene-scoped, same both paths | scene-scoped | scene-scoped, same |
| Header compile vs submit | header state-cached (`tgl_pvr_update_header` early-returns when unchanged); submitted per triangle | same distinction | same single compile, submitted once |

**Separation from assumptions:** the table counts TinyGL-side PVR command *submissions*. Whether KOS
internally batches consecutive EOL-less vertices or coalesces cache lines is not counted here.

**Structural savings (N triangles):** EOLs `N→1`, dispatched headers `N→1`, backend dispatches
`N→1`. Vertex *bytes* are unchanged (`N+2 ≈ 3N`), so **rasterization cost is identical** — this is
purely an assembly/command-overhead win. Likely effects (no invented cycle counts): less SH-4 CPU
per-triangle facing/cull branch + dispatch, fewer store-queue/PVR command words, fewer TA command
words; TA still processes the same `N+2` vertices. Gains concentrate at high triangle counts; the
low end should be flat.

---

## 5. Recommended first implementation slice and exact fallback rules

**Implemented initial target (hardware validation pending):**
- `GL_TRIANGLE_STRIP`; `V >= 3`;
- `GL_FILL` polygon mode, normal (non-select) render mode;
- unlit (`GL_LIGHTING` off) — Gouraud or solid colors (per-vertex colors only);
- **no culling** (`cull_face_enabled == 0`);
- **all vertices `clip_code == 0`** (checked as vertices arrive, or at `glEnd`);
- stable single PVR state/list/header for the whole strip (depth, blend, texture fixed);
- no unsupported state transition inside the primitive.

**Exact eligibility gate (evaluated at assembly time; fail closed → existing per-triangle path):**

```
begin_type == GL_TRIANGLE_STRIP
  && polygon_mode_front == polygon_mode_back == FILL
  && render_mode != GL_SELECT
  && lighting_enabled == 0
  && cull_face_enabled == 0                      // GL_FRONT/BACK break single-strip culling
  && no modifier/fog/polyoffset/scanline
  && for every vertex in [glBegin,glEnd]: clip_code == 0
  && (!texture_2d_enabled || (current_texture && pixmap != NULL))
  && vertex_cnt >= 3
  && no state change (depth/blend/texture/mode) inside the block
```

If **any** clause fails, route to the existing per-triangle `glopVertex` path (unchanged).

**Fallback conditions (each follows from code or PVR behavior; not invented):**
- clipped vertex (`clip_code != 0`) → fallback (single strip can't represent clipping);
- `GL_BACK`/`GL_FRONT` culling → fallback (alternating winding; hardware culling can't match);
- `GL_FRONT_AND_BACK` → fallback (would draw nothing; emit via per-tri for consistency, or skip);
- `GL_LINE`/`GL_POINT` polygon mode → fallback;
- `GL_SELECT` render mode → fallback;
- lighting on → fallback (colors aren't lighting-derived in this TinyGL; out of slice 1);
- modifier/fog/poly offset → fallback (needs modifier headers);
- texture enabled but pixmap NULL → fallback to solid (matches existing per-tri guard);
- strip too short (`V < 3`) → per-tri (emits nothing anyway);
- capacity / primitive-limit uncertainty → fallback;
- any PVR submission error in the fast path → abort the strip, fall back for the remainder.

**Culling/facing:** the path submits input vertices in strip order with `PVR_CULLING_NONE`.
Culling-enabled cases use the existing per-triangle route until PVR strip parity behavior is measured.

**Correctness vs performance framing:** because §1.3 proves the current branch already emits correct
triangles, **repairing the assembly is NOT a prerequisite** — it is already correct. The fast path is
a performance optimization layered on top; it must be **pixel-identical** to the current path on the
captured cases before it is accepted. Do not present it as a correctness fix.

**Backend entry point:** a `tgl_pvr_draw_strip(GLContext *, const GLVertex *[N+2], int count)`
warranted: it compiles/one header, streams `N+2` vertices with EOL on the last, and returns on
submission error. The branch should sit where `gl_draw_triangle` is chosen — ideally intercept in
`glopVertex` when the gate passes, buffering the ordered transformed vertices, rather than expanding
at `glEnd` (same cost, but intercept keeps transform parity and avoids a second pass).

---

## 6. Correctness and performance validation

### 6.1 Hardware-visible correctness test (must run on real Dreamcast; emulator-only is insufficient)
A dedicated strip-correctness scene (before any performance acceptance). Pass criterion: output is
**pixel-identical** to the current per-triangle path for the same geometry in every case.

1. Strips with **V = 3, 4, 5, 6, 8**: assert emitted triangle count == `max(V-2,0)` and consecutive
   triangles share an edge (overlap of 2 vertices).
2. **Triangle-list oracle:** submit the exact triangles a GL strip defines
   (`(k,k+1,k+2)` / `(k+1,k,k+2)`) via `GL_TRIANGLES`, for image/count comparison. **Do not** use the
   current strip path as the oracle until its assembly is independently verified (§1.3 already does
   this on the host).
3. **Winding/parity + culling:** strips of both windings with culling **disabled** (both render),
   then `GL_BACK` and `GL_FRONT` (must fall back and match the per-tri oracle); a case that tests
   whether PVR strip culling would match TinyGL's parity-adjusted facing (expected: it does **not**,
   hence the fallback gate).
4. **Distinct Gouraud colors**; **textured strip** (if texture is in slice 1, nearest filter only);
   **transformed geometry** (modelview translate/rotate before `glEnd`).
5. **Clipping fallback:** a strip partly outside the view volume → must fall back and render
   identically to the current path (no missing/clipped artifacts).
6. **One extra explicit fallback** (e.g. `GL_LINE` mode strip, or culling enabled).
7. **Degenerate strip** (repeated vertex): confirm PVR skips the degenerate triangle and both
   sub-strips render, matching the oracle; divergent behavior → fallback trigger.

Captured pixels / visible hardware output are the final correctness evidence.

### 6.2 Performance test plan (`pvrmark_strips` unchanged methodology)
Keep `pvrmark_strips` as the primary perf test; **do not modify the KOS direct sample**. First verify
the number/geometry of PVR triangles each variant actually submits before attributing differences to
submission. Compare 5 variants on physical Dreamcast, KOS 2.3.0, 640×480 VGA, with the existing
5-second windows, per-frame PVR FPS EMA, 55 FPS target, and `+2500/-200` search:

1. KOS direct PVR reference (unchanged);
2. TinyGL current strip, SH4ZAM off (label as-is; count actual submitted triangles — should equal the
   reference geometry since §1.3 proves correctness);
3. TinyGL current strip, SH4ZAM on (same count caveat);
4. TinyGL **fast-path** strip, SH4ZAM off;
5. TinyGL **fast-path** strip, SH4ZAM on.

If variant 2/3 output is not geometrically/count equivalent to the KOS reference, add a
semantics-matched expanded-triangle control (submit the exact expected triangles with `GL_TRIANGLES`)
before attributing differences to strip submission.

**Record per variant:** actual PVR triangle + vertex counts (instrumented, not `polycnt`), final load
threshold, FPS at threshold, actual triangles/sec, API-build time, draw-pipeline time, scene-submit
time, PVR render/transfer stats, PVR vertex count. **≥5 independent runs per variant**; report
individual runs plus median and range, not just the best (FPS varies sharply — observed already).
Increase repeats if the spread obscures the comparison. Clean rebuild between compiler-flag variants
(`make -C src clean` + matching `TINYGL_USE_SH4ZAM`), because Make does not track flag changes. No
`timeout` wrapper on `kos-tool`. Keep the KOS reference unmodified.

---

## 7. Likely TinyGL files to change (not edited)

- `src/vertex.c` — intercept the strip in `glopVertex` (§5); buffer ordered transformed vertices when
  the gate passes; keep the existing per-triangle case as fallback.
- `src/clip.c` — only if the fast path needs a shared facing/culling pre-check; otherwise culling stays
  a fallback gate evaluated before interception.
- `src/pvr_dc.c` — add `tgl_pvr_draw_strip(...)` (one header + `N+2` vertices + 1 EOL, error handling
  mirroring `tgl_pvr_draw_triangle`); reuse `tgl_pvr_submit_header` + `tgl_pvr_set_vertex`.
- `src/zgl.h` — add any strip buffer/state fields and the backend strip prototype; optionally a
  compile-time opt-in guard macro.
- `src/api.c` / `src/opinfo.h` — only if a new opcode is needed (expected: **not**).
- `tests/dreamcast/pvrmark_strips/main.c` — for the semantics-matched `GL_TRIANGLES` control and the
  instrumented PVR triangle/vertex counts (the benchmark itself stays methodologically unchanged).
- `docs/` and `TESTING.md` — record the report and hardware results.

No SDL2, GLdc, KOS, or SH4ZAM source changes.

---

## 8. Recommendation

**Evidence supports implementing the fast path next — as a performance optimization, not a correctness
fix, gated tightly and validated on hardware first.**

- **Structural promise is real:** §1.3 proves the current strip is already correct, the PVR natively
  supports strips (auto strip length), one header is already usable, and the cost table (§4) shows
  genuine EOL/header/dispatch savings at the assembly layer. The vertex bytes (rasterization) are
  unchanged, so this is a command-overhead win that concentrates at high triangle counts.
- **Demonstrated performance is not yet shown.** The win is unmeasured; run-to-run FPS variability is
  already large. §6.2's ≥5-run A/B with the KOS reference is the gate for any gain claim.
- **First slice is narrow and safe:** strip + FILL + unlit + no culling + all-unclipped, falling back
  on everything else. Culling is the real ceiling; clipping is handled by fall-back (sub-strip splitting
  is deferred).
- **Do not commit** and do not broaden scope; this remains research/design until hardware agrees.

**Bottom line:** structural promise > demonstrated performance. Build the gated fast path, prove
pixel-identical strip correctness on real kit (§6.1), then decide on the §6.2 numbers.
