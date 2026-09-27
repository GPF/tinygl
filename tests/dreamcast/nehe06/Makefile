TARGET := tinygl-nehe06.elf
OBJS := main.o loadbmp.o glu.o romdisk.o
TINYGL_LIB := ../../../src/libTinyGL.a

# Bundle romdisk/NeHe.bmp into the ELF. Its contents are accessible at
# runtime via the /rd/ virtual directory (KOS Makefile.rules builds romdisk.o).
KOS_ROMDISK_DIR = romdisk

# GL/gl.h is found via the TinyGL public include directory.
CFLAGS += -I../../../include

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
