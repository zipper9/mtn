#CFLAGS=-W -Wall -O3 -s -D_FILE_OFFSET_BITS=64 -mtune=i686 -march=i386 -I/home/tuitfun/include -L/home/tuitfun/lib
#CFLAGS=-W -Wall `pkg-config --cflags libavformat`

# The DESTDIR is macro is used for staged builds, so that it gets installed
# under a fake root directory for the sake of packaging. Unlike PREFIX, it will
# not actually be run from this directory.
#DESTDIR - reserved for rpmbuild

# According to a convention, PREFIX macro is the prefix of instalation.
PREFIX = /usr/local

KERNELNAME=$(shell uname -s)
OSNAME=$(shell uname -o)
CFLAGS=-W -Wall -O3 $(USER_CFLAGS) -Werror=discarded-qualifiers -Werror=implicit-function-declaration
OUT=../bin
LIBSDIR=../lib

ifeq ($(KERNELNAME),FreeBSD)
LIBS+=-L/usr/local/lib
INCPATH+=-I/usr/local/include
endif

ifeq ($(KERNELNAME),Linux)
INCPATH=-I/usr/include/ffmpeg
endif

LIBS+=-lavcodec -lavformat -lavcodec -lswscale -lavutil -lgd -lm
S_INCPATH=-I$(LIBSDIR)/FFmpeg -I$(LIBSDIR)/libgd/src
S_LIBS= -static-libgcc -static \
	$(LIBSDIR)/FFmpeg/libavformat/libavformat.a \
	$(LIBSDIR)/FFmpeg/libavcodec/libavcodec.a \
	$(LIBSDIR)/FFmpeg/libswscale/libswscale.a \
	$(LIBSDIR)/FFmpeg/libavutil/libavutil.a \
	$(LIBSDIR)/libgd/Bin/libgd.a \
	-lfreetype -ljpeg -lpng16 -lz -lm

OBJ = mtn.c file_utils.c measure_time.c options.c scan_dir_posix.c string_buffer.c

mtn: $(OBJ) outdir
	$(CC) -o $(OUT)/mtn $(OBJ) $(INCPATH) $(CFLAGS) $(LIBS)

outdir:
	mkdir -p $(OUT)

static: $(OBJ) outdir
	$(CC) -o $(OUT)/mtn $(OBJ) $(S_INCPATH) $(CFLAGS) $(S_LIBS) -DMTN_STATIC

debug: $(OBJ) outdir
	$(CC) -o $(OUT)/mtn $(OBJ) $(INCPATH) $(CFLAGS) -W -Wall -g -DDEBUG $(LIBS)

clean:
	rm -f $(OUT)/mtn

distclean:
	rm -rf $(OUT)

rebuild:  clean mtn
rebuild_static: clean static

test_bin:
	$(OUT)/mtn -f ../test/font.ttf -h 50 -O /tmp /home/sample.avi
	test -s /tmp/sample_s.jpg

test:  rebuild  mtn  test_bin
static_test: rebuild_static static test_bin

install: mtn
	mkdir -p $(DESTDIR)$(PREFIX)/bin/
	install -m 755 $(OUT)/mtn $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/mtn
	cp -Rp ../doc/* $(DESTDIR)$(PREFIX)/share/doc/mtn
	cp -p ../LICENSE $(DESTDIR)$(PREFIX)/share/doc/mtn
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1
	cp -p ../man/mtn.1 $(DESTDIR)$(PREFIX)/share/man/man1/

uninstall:
	rm -f  $(DESTDIR)$(PREFIX)/share/man/man1/mtn.1
	rm -f  $(DESTDIR)$(PREFIX)/bin/mtn
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/mtn

