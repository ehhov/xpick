PREFIX = /opt/local
MANPREFIX = ${PREFIX}/share/man

CC = cc
CFLAGS = -std=c99 -Wall -pedantic -Os -D_POSIX_C_SOURCE=200809L
LIBS = -lX11

all: xpick

xpick: xpick.c
	${CC} ${CFLAGS} ${LIBS} $< -o $@

clean:
	rm -f xpick

install: all
	mkdir -p ${PREFIX}/bin
	cp -f xpick ${PREFIX}/bin
	chmod 755 ${PREFIX}/bin/xpick
	mkdir -p ${MANPREFIX}/man1
	cp -f xpick.1 ${MANPREFIX}/man1
	chmod 644 ${MANPREFIX}/man1/xpick.1

uninstall:
	rm -f ${PREFIX}/bin/xpick
	rm -f ${MANPREFIX}/man1/xpick.1

.PHONY: all clean install uninstall
