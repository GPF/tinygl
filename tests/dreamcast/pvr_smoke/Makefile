TARGET := tinygl-pvr-smoke.elf
OBJS := pvr_smoke.o
TINYGL_LIB := ../../../src/libTinyGL.a

CFLAGS += -I../../../include $(SMOKE_CFLAGS)

# Build-time overrides for the smoke test. Makefile.rules invokes the kos-cc
# wrapper with $(CFLAGS) $(CPPFLAGS), so the smoke define rides on CFLAGS.
# Example: make SMOKE_CFLAGS="-DPVR_SMOKE_VIDEO_MODE=DM_320x240"

all: clean-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: clean-elf
	-rm -f $(OBJS)

clean-elf:
	-rm -f $(TARGET)

$(TARGET): $(OBJS) $(TINYGL_LIB)
	kos-cc -o $(TARGET) $(OBJS) $(TINYGL_LIB)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

.PHONY: all clean clean-elf run
