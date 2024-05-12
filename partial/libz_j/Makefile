CC=gcc

CFLAGS=-O3 -D_LARGEFILE64_SOURCE=1 -DHAVE_HIDDEN -w
#CFLAGS=-O -DMAX_WBITS=14 -DMAX_MEM_LEVEL=7
#CFLAGS=-g -DZLIB_DEBUG -DGUNZIP
#CFLAGS=-O3 -Wall -Wwrite-strings -Wpointer-arith -Wconversion \
#           -Wstrict-prototypes -Wmissing-prototypes
STATICLIB = libz_j.a

AR=ar
ARFLAGS=rc
RANLIB=ranlib

ZINCOUT=-I.

OBJZ =   deflate.o inffast.o inflate.o inftrees.o trees.o zutil.o adler32.o crc32.o
all: $(STATICLIB)
	
adler32.o: $(SRCDIR)adler32.c
	$(CC) $(CFLAGS) $(ZINC) -c -o $@ $(SRCDIR)adler32.c

crc32.o: $(SRCDIR)crc32.c
	$(CC) $(CFLAGS) $(ZINC) -c -o $@ $(SRCDIR)crc32.c

libz_j.a: $(OBJZ)
	$(AR) $(ARFLAGS) $@ $(OBJZ)
	-@ ($(RANLIB) $@ || true) >/dev/null 2>&1
deflate.o: deflate.c
	$(CC) $(CFLAGS)  -c -o $@ deflate.c

inffast.o: inffast.c
	$(CC) $(CFLAGS)  -c -o $@ inffast.c

inflate.o: inflate.c
	$(CC) $(CFLAGS)  -c -o $@ inflate.c

inftrees.o: inftrees.c
	$(CC) $(CFLAGS)  -c -o $@ inftrees.c

trees.o: trees.c
	$(CC) $(CFLAGS)  -c -o $@ trees.c

zutil.o: zutil.c
	$(CC) $(CFLAGS)  -c -o $@ zutil.c

clean:
	rm -f *.o  *.a
