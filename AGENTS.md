# TinyGL Dreamcast Backend Project Notes

## Goal

Develop a TinyGL-based OpenGL replacement path for the Dreamcast. The hardware
backend should use KOS's PVR directly. SDL2 Dreamcast renderer and video-driver
code may be read as reference material for PVR setup and video-mode hints; do
not modify the SDL2 project as part of TinyGL work.

The longer-term direction may include replacing the GLdc backend, but preserve
TinyGL's existing software-renderer behavior and public API where practical.

## Implementation preferences

- Keep changes small and independently testable.
- Build with the KOS toolchain and validate behavior on real Dreamcast hardware
  when the change affects PVR output. Hardware results take precedence over
  emulator output.
- Use `kos-port/sh4zam` for suitable SH4 math and memory-copy operations when
  available. Keep that integration optional so the normal software build does
  not depend on it.
- Be careful with PVR vertex formats/flags, color packing, depth mapping,
  alignment, cache handling, and state lifetime.
- Keep project direction, architecture notes, and roadmap in project guidance
  and `NEXT_TASKS.md`. Keep `TESTING.md` focused on repeatable build, test, and
  debugging procedures and observed test results.

## Reference code

The local SDL2 Dreamcast code is reference-only:

- Renderer: `<SDL2 checkout>/src/render/dreamcast/`
- Video driver and PVR video-mode hint path:
  `<SDL2 checkout>/src/video/dreamcast/`

Use it to understand KOS/PVR setup behavior; implement the TinyGL backend in
this repository.
