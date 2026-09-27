include ../config.mk

OBJS= api.o list.o vertex.o init.o matrix.o texture.o \
      misc.o clear.o light.o clip.o select.o get.o error.o \
      zbuffer.o zline.o zdither.o ztriangle.o \
      zmath.o image_util.o oscontext.o msghandling.o \
      arrays.o specbuf.o memory.o
ifdef TINYGL_USE_GLX
OBJS += glx.o
endif
ifdef TINYGL_USE_NANOX
OBJS += nglx.o
endif

INCLUDES = -I../include
LIB = libTinyGL.a

ifdef TINYGL_USE_SH4ZAM
CFLAGS += -DTINYGL_USE_SH4ZAM
endif

ifdef TINYGL_USE_DREAMCAST_PVR
CFLAGS += -DTINYGL_USE_DREAMCAST_PVR
OBJS += pvr_dc.o
endif

# Extra compiler flags (e.g. -DTGL_PVR_TEST_INJECT_FAIL for the submission
# error-injection test). Appended so normal build flags are not clobbered.
CFLAGS += $(EXTRA_CFLAGS)

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $(LIB)
	ar rcs $(LIB) $(OBJS)
	cp $(LIB) ../lib

clean:
	rm -f *~ *.o *.a

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $*.c

clip.o: zgl.h zfeatures.h
vertex.o: zgl.h zfeatures.h
light.o: zgl.h zfeatures.h
matrix.o: zgl.h zfeatures.h
list.o: zgl.h opinfo.h zfeatures.h
arrays.c: zgl.h zfeatures.h
specbuf.o: zgl.h zfeatures.h
glx.o: zgl.h zfeatures.h
nglx.o: zgl.h zfeatures.h
zline.o: zgl.h zfeatures.h zline.h

ztriangle.o: ztriangle.c ztriangle.h zgl.h zfeatures.h
	$(CC) $(CFLAGS) -Wno-uninitialized $(INCLUDES) -c $*.c
