.POSIX:

include config.mk

BIN = eek
OBJ = \
	eek.o \
	apply.o \
	motion.o \
	buf.o \
	term.o \
	key.o \
	util.o

all: options ${BIN}

options:
	@echo ${BIN} build options:
	@echo "CC      = ${CC}"
	@echo "CFLAGS  = ${CFLAGS}"
	@echo "CPPFLAGS= ${CPPFLAGS}"
	@echo "LDFLAGS = ${LDFLAGS}"

config.h:
	cp config.def.h config.h

${OBJ}: config.h eek.h eek_internal.h util.h buf.h

${BIN}: ${OBJ}
	${CC} ${LDFLAGS} -o $@ ${OBJ}

%.o: %.c
	${CC} ${CPPFLAGS} ${CFLAGS} -c -o $@ $<

clean:
	rm -f ${BIN} ${OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${BIN} ${DESTDIR}${PREFIX}/bin/${BIN}
	chmod 755 ${DESTDIR}${PREFIX}/bin/${BIN}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${BIN}

.PHONY: all options clean install uninstall
