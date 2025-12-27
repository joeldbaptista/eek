# eek build configuration

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

CC = cc

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Os
LDFLAGS =
