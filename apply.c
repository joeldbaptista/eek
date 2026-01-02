#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "apply.h"
#include "eek_internal.h"
#include "util.h"

static int
argvpush(char ***argv, int *argc, int *cap, const char *s, long n)
{
	char **v;
	char *t;
	int ncap;

	if (argv == nil || argc == nil || cap == nil)
		return -1;
	if (n < 0)
		n = 0;
	if (*argc + 1 > *cap) {
		ncap = *cap > 0 ? *cap * 2 : 8;
		for (; *argc + 1 > ncap; ncap *= 2)
			;
		v = realloc(*argv, (size_t)ncap * sizeof (*argv)[0]);
		if (v == nil)
			return -1;
		*argv = v;
		*cap = ncap;
	}
	t = malloc((size_t)n + 1);
	if (t == nil)
		return -1;
	if (n > 0)
		memcpy(t, s, (size_t)n);
	t[n] = 0;
	(*argv)[(*argc)++] = t;
	return 0;
}

static void
argvfree(char **argv, int argc)
{
	int i;

	if (argv == nil)
		return;
	for (i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

/*
 * parseargv parses POSIX/shell-like arguments from s.
 *
 * Supported:
 *  - whitespace separation
 *  - single quotes: '...'
 *  - double quotes: "..." (backslash escapes inside)
 *  - backslash escapes outside quotes
 */
static int
parseargv(const char *s, char ***argvout, int *argcout)
{
	const char *p;
	char **argv;
	int argc;
	int cap;
	char *tok;
	long tokn;
	long tokcap;
	char *tp;
	char c;
	char q;
	char *t;

	if (argvout)
		*argvout = nil;
	if (argcout)
		*argcout = 0;
	if (s == nil || argvout == nil || argcout == nil)
		return -1;

	argv = nil;
	argc = 0;
	cap = 0;
	p = s;

	for (;;) {
		for (; *p == ' ' || *p == '\t'; p++)
			;
		if (*p == 0)
			break;

		tok = nil;
		tokn = 0;
		tokcap = 0;
		q = 0;

		for (;;) {
			c = *p;
			if (q == 0 && (c == 0 || c == ' ' || c == '\t'))
				break;
			if (q == 0 && (c == '\'' || c == '"')) {
				q = c;
				p++;
				continue;
			}
			if (q != 0 && c == q) {
				q = 0;
				p++;
				continue;
			}
			if (c == '\\' && (q == 0 || q == '"')) {
				p++;
				if (*p == 0)
					break;
				c = *p;
			}

			if (tokn + 1 > tokcap) {
				long ncap;
				ncap = tokcap > 0 ? tokcap * 2 : 32;
				for (; tokn + 1 > ncap; ncap *= 2)
					;
				tp = realloc(tok, (size_t)ncap);
				if (tp == nil) {
					free(tok);
					argvfree(argv, argc);
					return -1;
				}
				tok = tp;
				tokcap = ncap;
			}
			tok[tokn++] = c;
			p++;
			if (*p == 0)
				break;
		}
		if (q != 0) {
			free(tok);
			argvfree(argv, argc);
			return -1;
		}

		if (tokn + 1 > tokcap) {
			tp = realloc(tok, (size_t)(tokn + 1));
			if (tp == nil) {
				free(tok);
				argvfree(argv, argc);
				return -1;
			}
			tok = tp;
			tokcap = tokn + 1;
		}
		tok[tokn] = 0;

		t = tok;
		if (argvpush(&argv, &argc, &cap, t, tokn) < 0) {
			free(tok);
			argvfree(argv, argc);
			return -1;
		}
		free(tok);
	}

	*argvout = argv;
	*argcout = argc;
	return 0;
}

static int
bufappend(char **buf, long *n, long *cap, const char *s, long sn)
{
	char *p;
	long need;
	long ncap;

	if (buf == nil || n == nil || cap == nil)
		return -1;
	if (sn <= 0)
		return 0;
	need = *n + sn;
	if (need > *cap) {
		ncap = *cap > 0 ? *cap * 2 : 64;
		for (; need > ncap; ncap *= 2)
			;
		p = realloc(*buf, (size_t)ncap);
		if (p == nil)
			return -1;
		*buf = p;
		*cap = ncap;
	}
	memcpy(*buf + *n, s, (size_t)sn);
	*n += sn;
	return 0;
}

static int
rangecopy(Eek *e, long y0, long x0, long y1, long x1, char **out, long *outn)
{
	Line *l;
	const char *ls;
	long y;
	long start;
	long end;
	long ln;
	long ty, tx;
	char *buf;
	long n;
	long cap;

	if (out)
		*out = nil;
	if (outn)
		*outn = 0;
	if (e == nil || out == nil || outn == nil)
		return -1;

	if (y1 < y0 || (y1 == y0 && x1 < x0)) {
		ty = y0;
		tx = x0;
		y0 = y1;
		x0 = x1;
		y1 = ty;
		x1 = tx;
	}
	if (y0 < 0)
		y0 = 0;
	if (y1 >= lsz(e->b.nline))
		y1 = lsz(e->b.nline) - 1;
	if (y0 > y1)
		return 0;

	buf = nil;
	n = 0;
	cap = 0;
	for (y = y0; y <= y1; y++) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			break;
		ls = linebytes(l);
		ln = lsz(l->n);
		start = (y == y0) ? x0 : 0;
		end = (y == y1) ? x1 : ln;
		if (start < 0)
			start = 0;
		if (start > ln)
			start = ln;
		if (end < 0)
			end = 0;
		if (end > ln)
			end = ln;
		if (end > start) {
			if (bufappend(&buf, &n, &cap, ls + start, end - start) < 0) {
				free(buf);
				return -1;
			}
		}
		if (y < y1) {
			if (bufappend(&buf, &n, &cap, "\n", 1) < 0) {
				free(buf);
				return -1;
			}
		}
	}

	*out = buf;
	*outn = n;
	return 0;
}

static int
inserttext(Eek *e, const char *s, long n)
{
	long i;
	long j;

	if (e == nil)
		return -1;
	if (s == nil || n <= 0)
		return 0;
	for (i = 0; i < n; ) {
		for (j = i; j < n && s[j] != '\n'; j++)
			;
		if (j > i) {
			if (insertbytes(e, s + i, j - i) < 0)
				return -1;
		}
		if (j < n && s[j] == '\n') {
			if (insertnl(e) < 0)
				return -1;
		}
		i = j + 1;
	}
	return 0;
}

static int
containsnl(const char *s, long n)
{
	long i;

	if (s == nil || n <= 0)
		return 0;
	for (i = 0; i < n; i++)
		if (s[i] == '\n')
			return 1;
	return 0;
}

static const Apply *
applylookup(const char *name)
{
	long i;

	if (name == nil || *name == 0)
		return nil;
	for (i = 0; i < (long)(sizeof applytab / sizeof applytab[0]); i++) {
		if (applytab[i].name && strcmp(applytab[i].name, name) == 0)
			return &applytab[i];
	}
	return nil;
}

int
applyexec(Eek *e, const char *argline)
{
	char **av;
	int ac;
	const Apply *ap;
	char *in;
	long inn;
	char *outbuf;
	long outn;
	long sy, sx, ey, ex;
	long lasty;
	long starty, startx;

	if (e == nil)
		return -1;

	if (argline == nil || *argline == 0) {
		setmsg(e, "Usage: apply <func-name> [args...]");
		return -1;
	}
	av = nil;
	ac = 0;
	if (parseargv(argline, &av, &ac) < 0) {
		setmsg(e, "Bad arguments");
		return -1;
	}
	if (ac <= 0) {
		argvfree(av, ac);
		setmsg(e, "Usage: apply <func-name> [args...]");
		return -1;
	}
	ap = applylookup(av[0]);
	if (ap == nil || ap->fn == nil) {
		setmsg(e, "No such apply function: %s", av[0]);
		argvfree(av, ac);
		return -1;
	}

	/* Block/column VISUAL: apply per-line within the selected rectangle. */
	if (e->cmdkeepvisual && e->vmode == Visualblock) {
		long y0, y1;
		long rx0, rx1;
		long y;
		Line *l;
		long cx0, cx1;
		long ln;
		const char *ls;
		char *seg;
		long segn;

		vselblockbounds(e, &y0, &y1, &rx0, &rx1);
		if (y0 < 0)
			y0 = 0;
		if (y1 >= lsz(e->b.nline))
			y1 = lsz(e->b.nline) - 1;
		if (y0 > y1) {
			argvfree(av, ac);
			return 0;
		}

		if (undopush(e) < 0) {
			setmsg(e, "Out of memory");
			argvfree(av, ac);
			return -1;
		}
		for (y = y0; y <= y1; y++) {
			l = bufgetline(&e->b, y);
			if (l == nil)
				continue;
			ln = lsz(l->n);
			cx0 = cxfromrx(e, y, rx0);
			cx1 = cxfromrx(e, y, rx1 + 1);
			if (cx0 < 0)
				cx0 = 0;
			if (cx1 < 0)
				cx1 = 0;
			if (cx0 > ln)
				cx0 = ln;
			if (cx1 > ln)
				cx1 = ln;
			if (cx1 <= cx0)
				continue;

			ls = linebytes(l);
			segn = cx1 - cx0;
			seg = malloc((size_t)segn);
			if (seg == nil) {
				setmsg(e, "Out of memory");
				argvfree(av, ac);
				return -1;
			}
			memcpy(seg, ls + cx0, (size_t)segn);

			outbuf = nil;
			outn = 0;
			if (ap->fn(seg, segn, ac, av, &outbuf, &outn) < 0) {
				free(seg);
				free(outbuf);
				setmsg(e, "apply failed: %s", av[0]);
				argvfree(av, ac);
				return -1;
			}
			free(seg);
			if (containsnl(outbuf, outn)) {
				free(outbuf);
				setmsg(e, "apply: block output may not contain newlines");
				argvfree(av, ac);
				return -1;
			}

			if (linedelrange(l, cx0, (size_t)(cx1 - cx0)) < 0) {
				free(outbuf);
				setmsg(e, "Out of memory");
				argvfree(av, ac);
				return -1;
			}
			if (outn > 0) {
				if (lineinsert(l, cx0, outbuf, (size_t)outn) < 0) {
					free(outbuf);
					setmsg(e, "Out of memory");
					argvfree(av, ac);
					return -1;
				}
			}
			free(outbuf);
			e->dirty = 1;
		}
		e->cy = y0;
		e->cx = cxfromrx(e, y0, rx0);
		normalfixcursor(e);
		setmsg(e, "applied %s", av[0]);
		argvfree(av, ac);
		return 0;
	}

	/* Determine target range; selection is treated as one contiguous string. */
	if (e->cmdkeepvisual) {
		vselbounds(e, &sy, &sx, &ey, &ex);
	} else {
		sy = 0;
		sx = 0;
		lasty = e->b.nline > 0 ? lsz(e->b.nline) - 1 : 0;
		ey = lasty;
		ex = linelen(e, lasty);
	}

	in = nil;
	inn = 0;
	outbuf = nil;
	outn = 0;
	if (rangecopy(e, sy, sx, ey, ex, &in, &inn) < 0) {
		setmsg(e, "Out of memory");
		argvfree(av, ac);
		return -1;
	}
	if (ap->fn(in ? in : "", inn, ac, av, &outbuf, &outn) < 0) {
		setmsg(e, "apply failed: %s", av[0]);
		free(in);
		free(outbuf);
		argvfree(av, ac);
		return -1;
	}

	if (outn == inn && (outn <= 0 || (outbuf && in && memcmp(outbuf, in, (size_t)outn) == 0))) {
		setmsg(e, "apply: no change");
		free(in);
		free(outbuf);
		argvfree(av, ac);
		return 0;
	}

	if (undopush(e) < 0) {
		setmsg(e, "Out of memory");
		free(in);
		free(outbuf);
		argvfree(av, ac);
		return -1;
	}
	starty = sy;
	startx = sx;
	if (delrange(e, sy, sx, ey, ex, 0) < 0) {
		setmsg(e, "Out of memory");
		free(in);
		free(outbuf);
		argvfree(av, ac);
		return -1;
	}
	if (inserttext(e, outbuf, outn) < 0) {
		setmsg(e, "Out of memory");
		free(in);
		free(outbuf);
		argvfree(av, ac);
		return -1;
	}
	e->dirty = 1;
	e->cy = starty;
	e->cx = startx;
	normalfixcursor(e);
	setmsg(e, "applied %s", av[0]);
	free(in);
	free(outbuf);
	argvfree(av, ac);
	return 0;
}

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
