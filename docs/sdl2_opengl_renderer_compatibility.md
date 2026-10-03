# TinyGL → SDL2 OpenGL Renderer Compatibility Audit

## Purpose

SDL2's `SDL_render_gl.c` renderer (the default OpenGL software renderer) is the
primary render path for SDL2 on the Dreamcast. It resolves all GL functions
dynamically via `SDL_GL_GetProcAddress`, which on Dreamcast calls
`DREAMCAST_GL_GetProcAddress` from `SDL_dreamcastopengl.c` (GLdc).

This document lists every function SDL2's OpenGL renderer backend requires,
maps it to TinyGL's current state, and prioritizes implementation effort.

## Architecture notes

- TinyGL is an **immediate-mode** GL 1.1 implementation. All geometry goes
  through `glBegin`/`glEnd` → `glopEnd` → `gl_draw_triangle` → PVR backend.
- Vertex arrays (`glEnableClientState`, `glVertexPointer`, `glColorPointer`,
  `glTexCoordPointer`, `glNormalPointer`, `glArrayElement`) exist in
  `src/arrays.c` but **no `glDrawArrays` / `glDrawElements`** exists.
- The PVR backend lives in `src/pvr_dc.c`. All state plumbing goes through
  `GLContext` in `src/zgl.h` and the opcode dispatch in `src/opinfo.h`.
- The Dreamcast video driver (`SDL_dreamcastopengl.c`) is **GLdc**, a separate
  library (`libGLdc.a`). It already implements many missing functions (ortho,
  scissor, readpixels, texsubimage, framebuffers, blend functions, etc.).
  TinyGL either needs its own implementation or can defer to GLdc.

## SDL2 OpenGL renderer function requirements

Derived from `SDL_render_gl.c` (2544 lines, SDL2 2.x) and the Dreamcast
video driver's `glfuncs` table (70 entries).

### Legend

| Tag | Meaning |
|-----|---------|
| ✅ | TinyGL already implements |
| ⚠️ | TinyGL has a stub (empty inline) in `include/GL/gl.h` |
| ❌ | TinyGL does not implement |
| 🟢 | GLdc provides (SDL2 can resolve via `SDL_GL_GetProcAddress`) |
| 🔴 | GLdc stubs (does nothing, SDL2 may crash or misbehave) |

---

### Tier 1: Easy — 1–2 functions, trivial to add

| # | Function | TinyGL | Effort | Notes |
|---|----------|--------|--------|-------|
| 1 | `glGetError` | ❌ | **10 min** | Track `GLContext.error_code`, return it, clear on next call. Add `OP_GetError` to `opinfo.h`. |
| 2 | `glGetString` | ❌ | **15 min** | Return static strings: `GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`, `GL_EXTENSIONS`. Add to `GLContext`. |
| 3 | `glOrtho` | ⚠️ empty inline | **30 min** | Multiply current matrix by ortho matrix (same as GLdc's implementation). |
| 4 | `glScissor` | ❌ | **30 min** | Store in `GLContext.scissor`. PVR backend needs it for `PVR_SCISSOR_ENABLE` + `pvr_scissor()`. |
| 5 | `glReadBuffer` | ❌ | **10 min** | Trivial state store (`GLContext.read_buffer`). PVR doesn't support readback natively but store it for GLdc fallback. |
| 6 | `glLineWidth` (setter) | ⚠️ empty inline | **10 min** | Store in `GLContext.line_width`. GLdc already handles it. |
| 7 | `glPointSize` (setter) | ⚠️ empty inline | **10 min** | Store in `GLContext.point_size`. GLdc already handles it. |
| 8 | `glVertex2i` | ⚠️ empty inline | **10 min** | Convert int→float, call existing `glVertex2f`. |

### Tier 2: Medium — new opcodes + handler + PVR mapping

| # | Function | TinyGL | Effort | Notes |
|---|----------|--------|--------|-------|
| 9 | `glBlendFuncSeparate` | ❌ | **2–3 h** | New op + handler in `misc.c`. Map GL factors to `pvr_blend_mode_t` for RGB and alpha separately. PVR `pvr_poly_cxt_txr`/`pvr_poly_cxt_...` has separate src/dst for RGB and alpha. See `src/pvr_dc.c::tgl_pvr_blend_factor()`. |
| 10 | `glBlendEquation` | ❌ | **1–2 h** | New op + handler. PVR only supports additive blend (`PVR_BLEND_ADD`). Map `GL_FUNC_ADD` → PVR ADD, reject others with `gl_fatal_error`. |
| 11 | `glTexSubImage2D` | ❌ | **2–4 h** | New op + `texture.c::glTexSubImage2D`. Copies pixels into existing pixmap (256×256 RGB565). Needs to update the cached VRAM copy too (call `tgl_pvr_texture_load` or invalidate cache). The pixmap is 256×256 fixed-size in TinyGL. |
| 12 | `glRectf` | ❌ | **30 min** | Two triangles (or one quad). Map to `glBegin(GL_TRIANGLE_STRIP)` / `glEnd()`. Simple. |
| 13 | `glTexEnvf` | ⚠️ empty inline | **30 min** | Float variant of `glTexEnvi`. Reuse `glTexEnvi` logic with `GL_TEXTURE_ENV_MODE` → `PVR_FILTER_NEAREST`/`PVR_FILTER_LINEAR` mapping. |

### Tier 3: Hard — need significant infrastructure

| # | Function | TinyGL | Effort | Notes |
|---|----------|--------|--------|-------|
| 14 | `glDrawArrays` | ❌ | **4–8 h** | **Biggest single item.** TinyGL's entire draw path is immediate-mode (`glBegin`→`glopEnd`→`gl_draw_triangle`). Need a new `OP_DrawArrays` that loops over `glArrayElement`-style array reads and calls `gl_draw_triangle` (or a new batch path). Must handle vertex/color/texcoord arrays, normal arrays, and edge flags. The vertex-array state (`vertex_array`, `color_array`, `texcoord_array`, `normal_array`, sizes, strides) already exists in `GLContext`. |
| 15 | `glReadPixels` | ❌ | **6–12 h** | Dreamcast PVR has no hardware readback. Need to: (a) read PVR display memory (framebuffer is VRAM), or (b) route through GLdc's `glKosGetFramebuffer()` / `vid_get_mode()` path, or (c) implement a software rasterizer readback (expensive). SDL2 uses this for target→screen copy. The PVR is write-only; you must read from the display buffer via KOS APIs (`vid_get_mode()`, `vid_get_framebuffer()`). |

### Tier 4: GLdc-provided (SDL2 can resolve via `SDL_GL_GetProcAddress`)

| # | Function | GLdc | Notes |
|---|----------|------|-------|
| 16 | `glOrtho` | ✅ | GLdc already implements this. SDL2 can resolve it. |
| 17 | `glScissor` | ✅ | GLdc already implements this. |
| 18 | `glReadPixels` | ✅ | GLdc provides readback via `glKosGetFramebuffer()`. |
| 19 | `glTexSubImage2D` | ✅ | GLdc provides texture update. |
| 20 | `glBlendFuncSeparate` | ✅ | GLdc provides (stubbed but functional). |
| 21 | `glBlendEquation` | ✅ | GLdc provides (stubbed but functional). |
| 22 | `glGenFramebuffersEXT` | ✅ | GLdc provides FBO support. |
| 23 | `glDeleteFramebuffersEXT` | ✅ | GLdc provides. |
| 24 | `glFramebufferTexture2DEXT` | ✅ | GLdc provides. |
| 25 | `glBindFramebufferEXT` | ✅ | GLdc provides. |
| 26 | `glCheckFramebufferStatusEXT` | ✅ | GLdc provides. |
| 27 | `glLoadTransposeMatrixf` | ✅ | GLdc provides (rarely used). |
| 28 | `glMultTransposeMatrixf` | ✅ | GLdc provides. |
| 29 | `glGetString` | ✅ | GLdc provides (vendor/renderer strings). |
| 30 | `glGetError` | ✅ | GLdc provides. |
| 31 | `glRectf` | ✅ | GLdc provides. |
| 32 | `glReadBuffer` | ✅ | GLdc provides. |
| 33 | `glLineWidth` (setter) | ✅ | GLdc provides. |
| 34 | `glPointSize` (setter) | ✅ | GLdc provides. |
| 35 | `glVertex2i` | ✅ | GLdc provides. |
| 36 | `glTexEnvf` | ✅ | GLdc provides. |

> **Key insight:** GLdc already implements most of the "missing" functions.
> The question for TinyGL is: do we implement them ourselves (for correctness,
> consistency, and to not depend on GLdc's state machine), or do we rely on
> GLdc's implementations for the Dreamcast backend?

### Tier 5: GLdc stubs (SDL2 may break if these are called)

| # | Function | GLdc | Notes |
|---|----------|------|-------|
| 37 | `glRasterPos2i` | 🔴 stub (no-op) | SDL2 rarely calls this; if it does, raster position is wrong. |
| 38 | `glGetPointerv` | 🔴 stub (no-op) | SDL2 rarely calls this. |
| 39 | `glDrawPixels` | 🔴 stub (no-op) | SDL2 rarely calls this. |

---

## Implementation priority roadmap

### Phase 1: Essential for SDL2 render backend to work (Tier 1 + Tier 4)

These are the functions SDL2's `SDL_render_gl.c` **actually calls in its hot
path** (draw, texture update, clear, state setup). If SDL2's OpenGL renderer
is to function on TinyGL, these must work:

1. `glGetError` — called after every GL call in `GL_CheckError()`
2. `glGetString` — called at context creation (`glGetString(GL_VENDOR)`)
3. `glOrtho` — called for 2D render target projection (`glOrtho(0, w, h, 0, ...)`)
4. `glScissor` — called for render target clipping
5. `glReadPixels` — called for target→screen copy (`SDL_RenderReadPixels`)
6. `glTexSubImage2D` — called for texture updates (`SDL_UpdateTexture`)
7. `glBlendFuncSeparate` — called for blend state setup
8. `glBlendEquation` — called for blend equation setup
9. `glDrawArrays` — **the primary draw path** (SDL2 uses `glDrawArrays` for
   vertex-array rendering, which is its default path)
10. `glRectf` — called for 2D rectangle primitives

Since GLdc (Tier 4) already provides most of these, the **fastest path to
SDL2 compatibility** is:
- Implement TinyGL's own versions of the critical path functions (1–4, 6–7, 9)
- For the rest, either keep the stubs or defer to GLdc

### Phase 2: Nice-to-have (Tier 2 + Tier 5)

- `glTexEnvf` — texture env float variant
- `glVertex2i` — integer vertex variant
- `glReadBuffer` — read buffer selection
- `glLineWidth` / `glPointSize` setters
- Fix GLdc stubs (37–39) if needed

### Phase 3: Low priority (Tier 3, FBO path)

- `glDrawArrays` — big item, but GLdc provides it
- `glReadPixels` — big item, but GLdc provides it
- FBO functions (22–26) — GLdc provides, TinyGL doesn't need to implement

---

## GLdc vs TinyGL decision points

| Question | Recommendation |
|----------|---------------|
| Should TinyGL implement its own `glOrtho`/`glScissor`/`glTexSubImage2D`/`glBlendFuncSeparate`/`glBlendEquation`? | **Yes for correctness.** GLdc's implementations may have different state semantics (e.g. GLdc's `glBlendFuncSeparate` is a stub that calls `glBlendFunc` for RGB and sets alpha test for alpha). TinyGL's PVR backend needs exact factor mapping. |
| Should TinyGL implement `glDrawArrays`? | **Yes if SDL2's OpenGL renderer is the target.** SDL2's primary draw path is vertex arrays + `glDrawArrays`. Without it, TinyGL can only render via immediate mode (which SDL2 doesn't use). |
| Should TinyGL implement `glReadPixels`? | **Yes if SDL2's render target readback is needed.** GLdc provides it but TinyGL's PVR backend is write-only; you'd need KOS framebuffer read APIs. |
| Should TinyGL implement FBO functions? | **No — GLdc provides them.** TinyGL doesn't use FBOs in its PVR backend. |
| Should TinyGL implement transpose matrix functions? | **No — GLdc provides them.** Rarely used, not worth the effort. |
| Should TinyGL implement `glGetError`/`glGetString`? | **Yes — trivial and needed for SDL2 context creation.** |

---

## Summary table

| # | Function | TinyGL | GLdc | Priority |
|---|----------|--------|------|----------|
| 1 | `glGetError` | ❌ | ✅ | **P0** |
| 2 | `glGetString` | ❌ | ✅ | **P0** |
| 3 | `glOrtho` | ⚠️ stub | ✅ | **P0** |
| 4 | `glScissor` | ❌ | ✅ | **P0** |
| 5 | `glReadPixels` | ❌ | ✅ | **P0** |
| 6 | `glTexSubImage2D` | ❌ | ✅ | **P0** |
| 7 | `glBlendFuncSeparate` | ❌ | ✅ (stub) | **P0** |
| 8 | `glBlendEquation` | ❌ | ✅ (stub) | **P0** |
| 9 | `glDrawArrays` | ❌ | ✅ | **P0** |
| 10 | `glRectf` | ❌ | ✅ | **P0** |
| 11 | `glTexEnvf` | ⚠️ stub | ✅ | P1 |
| 12 | `glVertex2i` | ⚠️ stub | ✅ | P1 |
| 13 | `glReadBuffer` | ❌ | ✅ | P1 |
| 14 | `glLineWidth` setter | ⚠️ stub | ✅ | P1 |
| 15 | `glPointSize` setter | ⚠️ stub | ✅ | P1 |
| 16 | `glGenFramebuffersEXT` | ❌ | ✅ | P2 |
| 17 | `glDeleteFramebuffersEXT` | ❌ | ✅ | P2 |
| 18 | `glFramebufferTexture2DEXT` | ❌ | ✅ | P2 |
| 19 | `glBindFramebufferEXT` | ❌ | ✅ | P2 |
| 20 | `glCheckFramebufferStatusEXT` | ❌ | ✅ | P2 |
| 21 | `glLoadTransposeMatrixf` | ❌ | ✅ | P2 |
| 22 | `glMultTransposeMatrixf` | ❌ | ✅ | P2 |
| 23 | `glRasterPos2i` | ❌ | 🔴 stub | P2 |
| 24 | `glGetPointerv` | ❌ | 🔴 stub | P2 |
| 25 | `glDrawPixels` | ❌ | 🔴 stub | P2 |

**P0 = essential for SDL2 OpenGL renderer to function on Dreamcast.**
**P1 = nice-to-have, low effort.**
**P2 = GLdc provides, TinyGL can defer.**

---

## Implementation notes for P0 items

### `glGetError`
- Add `int error_code` to `GLContext` in `zgl.h`
- Set `error_code = GL_INVALID_ENUM` etc. in `gl_fatal_error()`
- `glGetError()` returns `c->error_code`, sets `c->error_code = GL_NO_ERROR`
- Add to `opinfo.h`: `ADD_OP(GetError, 0, "")`
- Add to `misc.c`: `void glGetError(int *p) { ... }`

### `glGetString`
- Add `const char *gl_vendor`, `gl_renderer`, `gl_version` to `GLContext`
- Set in `glInit()` (e.g. "TinyGL", "TinyGL PVR Backend", "1.1")
- Return from `glGetString(name)` for `GL_VENDOR`, `GL_RENDERER`,
  `GL_VERSION`, `GL_EXTENSIONS` ("GL_ARB_multitexture GL_EXT_framebuffer_object")

### `glOrtho`
- Same algorithm as GLdc: `glMatrixMode(GL_PROJECTION)`, `glPushMatrix()`,
  `glLoadIdentity()`, multiply by ortho matrix, `glPopMatrix()`
- Ortho matrix: `glOrtho(left, right, bottom, top, near, far)`
- Multiply current matrix by ortho matrix (post-multiply)

### `glScissor`
- Store in `GLContext.scissor` (x, y, width, height)
- In `tgl_pvr_flush()` or before draw, call `pvr_scissor()` if scissor enabled
- Add `ADD_OP(Scissor, 4, "%d %d %d %d")` to `opinfo.h`

### `glReadPixels`
- Use KOS `vid_get_mode()` + `vid_get_framebuffer()` to get framebuffer pointer
- Copy from VRAM to host buffer (need to handle RGB565↔RGBA conversion)
- This is the hardest P0 item — PVR is write-only, must read display VRAM

### `glTexSubImage2D`
- Update pixmap in `texture.c` (256×256 RGB565)
- Invalidate cached VRAM copy (next `glTexImage2D` will re-upload)
- Or: re-upload to VRAM directly with `pvr_txr_load_ex()`

### `glBlendFuncSeparate`
- Store `blend_src_rgb`, `blend_dst_rgb`, `blend_src_alpha`, `blend_dst_alpha`
- Map GL factors to `pvr_blend_mode_t` for each pair
- PVR `pvr_poly_cxt_txr` has `src_enable`, `dst_enable` (RGB) and
  `src_enable_alpha`, `dst_enable_alpha` (alpha)
- Rebuild header when these change (like `glBlendFunc` does)

### `glBlendEquation`
- PVR only supports additive (`PVR_BLEND_ADD`)
- Map `GL_FUNC_ADD` → PVR ADD, reject others with `gl_fatal_error`
- Store in `GLContext.blend_equation`

### `glDrawArrays`
- **Biggest item.** New `OP_DrawArrays` opcode.
- Loop over `vertex_array` (or other enabled arrays) calling `gl_draw_triangle`
  for each triangle (for `GL_TRIANGLES`) or building strips (for `GL_TRIANGLE_STRIP`)
- Must handle: vertex/color/texcoord/normal arrays, edge flags, stride, size
- Could reuse `glopArrayElement` logic in a loop
- Or: build a new batch path similar to `gl_draw_triangle` but from arrays

### `glRectf`
- Two triangles: `glBegin(GL_TRIANGLE_STRIP)`, 4 vertices, `glEnd()`
- Or: one quad with `GL_TRIANGLE_FAN`
- Simple, 10 lines

---

## Build/test plan

1. Implement P0 items (1–10) in TinyGL
2. Build TinyGL with `TINYGL_USE_DREAMCAST_PVR=y`
3. Build SDL2 with `--enable-video-dreamcast --enable-render-opengl`
4. Run an SDL2 OpenGL app (e.g. `SDL2-2.28.4/test/testgl2.c`)
5. Verify on hardware: windows, textures, blending, scissor, readback

---

## References

- SDL2 render backend: `SDL_render_gl.c` (2544 lines)
- Dreamcast video driver: `SDL_dreamcastopengl.c` (70 glfuncs entries)
- TinyGL public API: `include/GL/gl.h`
- TinyGL opcode dispatch: `src/opinfo.h`
- TinyGL vertex-array state: `src/zgl.h` (lines 251–262)
- TinyGL vertex-array impl: `src/arrays.c`
- TinyGL PVR backend: `src/pvr_dc.c`
- TinyGL texture: `src/texture.c`
