# Testing TinyGL

This guide covers the current software renderer, the opt-in SH4ZAM math path,
and the Dreamcast PVR smoke test. The PVR smoke test currently verifies
untextured filled triangles; texture mapping, point/line output, and GL depth
state are not covered yet.

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
kos-tool -t "$DC_IP" -x tests/dreamcast/pvr_smoke/tinygl-pvr-smoke.elf
```

The smoke program exercises TinyGL's GL path: it calls `glInitPVR`, clears to
black, emits a red `GL_TRIANGLES` primitive, and flushes each frame. The
expected screen is a red triangle centered on black. It renders 600 frames,
reports progress and `pvr_smoke: PASS` over the loader log, shuts PVR down
through `glClose`, and exits. A nonzero exit or missing PASS line is a failure.

For any hardware run, record the console video mode/cable, KOS version, build
options, test scene, observed output, and loader log. Mark a path untested if it
was only compiled or inspected; emulator-only success is not hardware
acceptance.

## Latest PVR hardware result

2026-09-25: KOS 2.3.0 on 640x480 VGA, built with
`TINYGL_USE_DREAMCAST_PVR=y` and `TINYGL_USE_SH4ZAM=y`. The centered red
triangle was visible on black for the 600-frame GL-driven run; the loader
reported `pvr_smoke: PASS` and `Program returned 0`.
