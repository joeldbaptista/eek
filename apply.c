#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

/*
 * Optional example apply function.
 *
 * Uncomment its entry in applytab[] in config.h/config.def.h to enable.
 */
int
apply_space_between(const char *in, long inlen, int argc, char **argv, char **out, long *outlen)
{
	const char *delim;
	long delimlen;
	long i;
	long cap;
	long n;
	char *buf;
	int needspace;

	if (out)
		*out = nil;
	if (outlen)
		*outlen = 0;
	if (in == nil || inlen < 0 || out == nil || outlen == nil)
		return -1;

	delim = nil;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			delim = argv[i + 1];
			break;
		}
	}
	if (delim == nil || delim[0] == 0)
		return -1;
	delimlen = (long)strlen(delim);
	if (delimlen <= 0)
		return -1;

	cap = inlen + 16;
	if (cap < 16)
		cap = 16;
	buf = malloc((size_t)cap);
	if (buf == nil)
		return -1;
	n = 0;

	for (i = 0; i < inlen; ) {
		if (i + delimlen <= inlen && memcmp(in + i, delim, (size_t)delimlen) == 0) {
			/* Remove any spaces/tabs immediately before the delimiter. */
			for (; n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'); n--)
				;

			/* Add one space before delimiter unless at start or after newline. */
			needspace = (n > 0 && buf[n - 1] != '\n');
			if (needspace) {
				if (n + 1 > cap) {
					char *p;
					cap *= 2;
					p = realloc(buf, (size_t)cap);
					if (p == nil) {
						free(buf);
						return -1;
					}
					buf = p;
				}
				buf[n++] = ' ';
			}

			if (n + delimlen + 1 > cap) {
				char *p;
				for (; n + delimlen + 1 > cap; cap *= 2)
					;
				p = realloc(buf, (size_t)cap);
				if (p == nil) {
					free(buf);
					return -1;
				}
				buf = p;
			}
			memcpy(buf + n, delim, (size_t)delimlen);
			n += delimlen;

			/* Skip whitespace after delimiter in input. */
			i += delimlen;
			for (; i < inlen && (in[i] == ' ' || in[i] == '\t'); i++)
				;

			/* Add one space after delimiter unless end-of-input or newline. */
			if (i < inlen && in[i] != '\n') {
				if (n + 1 > cap) {
					char *p;
					cap *= 2;
					p = realloc(buf, (size_t)cap);
					if (p == nil) {
						free(buf);
						return -1;
					}
					buf = p;
				}
				buf[n++] = ' ';
			}
			continue;
		}

		/* Copy one byte; this keeps the example byte-oriented. */
		if (n + 1 > cap) {
			char *p;
			cap *= 2;
			p = realloc(buf, (size_t)cap);
			if (p == nil) {
				free(buf);
				return -1;
			}
			buf = p;
		}
		buf[n++] = in[i++];
	}

	*out = buf;
	*outlen = n;
	return 0;
}
