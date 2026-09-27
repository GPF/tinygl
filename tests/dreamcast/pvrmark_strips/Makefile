TARGET := tinygl-pvrmark-strips.elf
OBJS := main.o
TINYGL_LIB := ../../../src/libTinyGL.a

CFLAGS += -I../../../include $(BENCH_CFLAGS)

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
