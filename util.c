#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "util.h"

/*
 * die prints a formatted error message to stderr and terminates the program.
 *
 * Parameters:
 *  fmt: printf-style format string.
 *  ...: format arguments.
 *
 * Returns:
 *  Does not return.
 */
void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	fputc('\n', stderr);
	exit(1);
}
