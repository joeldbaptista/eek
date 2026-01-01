#include <string.h>

#include "eek_internal.h"

/*
 * utf8dec1 decodes a single UTF-8 codepoint from s.
 *
 * Parameters:
 *  - s: pointer to bytes.
 *  - n: number of bytes available at s.
 *  - adv: optional out-parameter set to the number of bytes consumed.
 *
 * Returns:
 *  - decoded rune value, or -1 if input is empty.
 */
long
utf8dec1(const char *s, long n, long *adv)
{
	unsigned char c;
	long r;
	long m;
	long b1, b2, b3;

	if (adv)
		*adv = 0;
	if (s == nil || n <= 0)
		return -1;
	c = (unsigned char)s[0];
	if (c < 0x80) {
		if (adv)
			*adv = 1;
		return (long)c;
	}
	/* 2-byte */
	if ((c & 0xe0) == 0xc0 && n >= 2) {
		m = (unsigned char)s[1];
		if ((m & 0xc0) != 0x80)
			goto bad;
		r = ((long)(c & 0x1f) << 6) | (long)(m & 0x3f);
		if (adv)
			*adv = 2;
		return r;
	}
	/* 3-byte */
	if ((c & 0xf0) == 0xe0 && n >= 3) {
		b1 = (unsigned char)s[1];
		b2 = (unsigned char)s[2];
		if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80)
			goto bad;
		r = ((long)(c & 0x0f) << 12) | ((long)(b1 & 0x3f) << 6) | (long)(b2 & 0x3f);
		if (adv)
			*adv = 3;
		return r;
	}
	/* 4-byte */
	if ((c & 0xf8) == 0xf0 && n >= 4) {
		b1 = (unsigned char)s[1];
		b2 = (unsigned char)s[2];
		b3 = (unsigned char)s[3];
		if ((b1 & 0xc0) != 0x80 || (b2 & 0xc0) != 0x80 || (b3 & 0xc0) != 0x80)
			goto bad;
		r = ((long)(c & 0x07) << 18) | ((long)(b1 & 0x3f) << 12) | ((long)(b2 & 0x3f) << 6) | (long)(b3 & 0x3f);
		if (adv)
			*adv = 4;
		return r;
	}

bad:
	if (adv)
		*adv = 1;
	return (long)c;
}

/*
 * utf8enc encodes rune r into UTF-8 bytes stored at s.
 *
 * Parameters:
 *  - r: rune value.
 *  - s: output buffer (must have space for up to 4 bytes).
 *
 * Returns:
 *  - number of bytes written (1..4).
 */
long
utf8enc(long r, char *s)
{
	if (s == nil)
		return 0;
	if (r < 0)
		r = 0xfffd;
	if (r < 0x80) {
		s[0] = (char)r;
		return 1;
	}
	if (r < 0x800) {
		s[0] = (char)(0xc0 | (r >> 6));
		s[1] = (char)(0x80 | (r & 0x3f));
		return 2;
	}
	if (r < 0x10000) {
		s[0] = (char)(0xe0 | (r >> 12));
		s[1] = (char)(0x80 | ((r >> 6) & 0x3f));
		s[2] = (char)(0x80 | (r & 0x3f));
		return 3;
	}
	if (r <= 0x10ffff) {
		s[0] = (char)(0xf0 | (r >> 18));
		s[1] = (char)(0x80 | ((r >> 12) & 0x3f));
		s[2] = (char)(0x80 | ((r >> 6) & 0x3f));
		s[3] = (char)(0x80 | (r & 0x3f));
		return 4;
	}
	s[0] = (char)0xef;
	s[1] = (char)0xbf;
	s[2] = (char)0xbd;
	return 3;
}

/*
 * clamp clamps v into [lo, hi].
 */
long
clamp(long v, long lo, long hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

/*
 * linelen returns the length in bytes of line y.
 */
long
linelen(Eek *e, long y)
{
	Line *l;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	return l->n;
}

/*
 * prevutf8 returns the previous UTF-8 codepoint boundary at or before at.
 */
long
prevutf8(Eek *e, long y, long at)
{
	Line *l;
	unsigned char c;
	long i;
	const char *s;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	s = linebytes(l);
	if (at <= 0)
		return 0;

	i = at - 1;
	for (; i > 0; i--) {
		c = (unsigned char)s[i];
		if ((c & 0xc0) != 0x80)
			break;
	}
	return i;
}

/*
 * nextutf8 returns the next UTF-8 codepoint boundary after at.
 */
long
nextutf8(Eek *e, long y, long at)
{
	Line *l;
	unsigned char c;
	long n;
	const char *s;
	long ln;

	(void)e;
	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	s = linebytes(l);
	ln = lsz(l->n);
	if (at >= ln)
		return ln;

	c = (unsigned char)s[at];
	if (c < 0x80)
		n = 1;
	else if ((c & 0xe0) == 0xc0)
		n = 2;
	else if ((c & 0xf0) == 0xe0)
		n = 3;
	else if ((c & 0xf8) == 0xf0)
		n = 4;
	else
		n = 1;
	if (at + n > ln)
		return ln;
	return at + n;
}

/*
 * isws reports whether c is considered whitespace by motions.
 */
int
isws(long c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/*
 * isword reports whether c is considered a "word" character for word motions.
 */
int
isword(long c)
{
	if (c < 0)
		return 0;
	if (c > 0x7f)
		return 1;
	if (isws(c))
		return 0;
	if (c == '_')
		return 1;
	if (c >= '0' && c <= '9')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c >= 'a' && c <= 'z')
		return 1;
	return 0;
}

/*
 * ispunctword reports whether c is considered punctuation for "punctuation
 * word" motions.
 */
int
ispunctword(long c)
{
	if (c < 0)
		return 0;
	if (c > 0x7f)
		return 0;
	if (isws(c))
		return 0;
	return !isword(c);
}

/*
 * peekbyte returns the byte value at (y, at) or -1 if out of range.
 */
long
peekbyte(Eek *e, long y, long at)
{
	Line *l;
	const char *s;
	long ln;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return -1;
	s = linebytes(l);
	ln = lsz(l->n);
	if (at < 0 || at >= ln)
		return -1;
	return (unsigned char)s[at];
}

/*
 * movel moves the cursor left by one UTF-8 codepoint.
 */
void
movel(Eek *e)
{
	e->cx = prevutf8(e, e->cy, e->cx);
}

/*
 * mover moves the cursor right by one UTF-8 codepoint.
 */
void
mover(Eek *e)
{
	e->cx = nextutf8(e, e->cy, e->cx);
}

/*
 * moveu moves the cursor up one line, clamping to file bounds.
 */
void
moveu(Eek *e)
{
	e->cy = clamp(e->cy - 1, 0, lsz(e->b.nline) - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

/*
 * moved moves the cursor down one line, clamping to file bounds.
 */
void
moved(Eek *e)
{
	e->cy = clamp(e->cy + 1, 0, lsz(e->b.nline) - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

/*
 * movew implements the vi-like 'w' motion using simple word classes.
 */
void
movew(Eek *e)
{
	long c;
	long len;

	for (;;) {
		len = linelen(e, e->cy);
		c = peekbyte(e, e->cy, e->cx);
		if (c < 0 || e->cx >= len)
			break;
		if (isword(c)) {
			for (;;) {
				c = peekbyte(e, e->cy, e->cx);
				if (c < 0 || !isword(c))
					break;
				e->cx = nextutf8(e, e->cy, e->cx);
				if (e->cx >= len)
					break;
			}
			break;
		}
		if (ispunctword(c)) {
			for (;;) {
				c = peekbyte(e, e->cy, e->cx);
				if (c < 0 || !ispunctword(c))
					break;
				e->cx = nextutf8(e, e->cy, e->cx);
				if (e->cx >= len)
					break;
			}
			break;
		}
		break;
	}

	for (;;) {
		len = linelen(e, e->cy);
		c = peekbyte(e, e->cy, e->cx);
		if (c < 0 || e->cx >= len)
			break;
		if (!isws(c))
			break;
		e->cx = nextutf8(e, e->cy, e->cx);
	}
	for (;;) {
		len = linelen(e, e->cy);
		if (e->cx < len)
			break;
		if (e->cy + 1 >= lsz(e->b.nline))
			break;
		e->cy++;
		e->cx = 0;
		c = peekbyte(e, e->cy, e->cx);
		if (c >= 0 && isws(c))
			continue;
		break;
	}
}

/*
 * moveb implements the vi-like 'b' motion (backward word).
 */
void
moveb(Eek *e)
{
	long c;
	long nx;
	long len;
	int cls;

	for (;;) {
		len = linelen(e, e->cy);
		if (len == 0) {
			if (e->cy == 0)
				return;
			e->cy--;
			e->cx = linelen(e, e->cy);
			continue;
		}
		if (e->cx > len)
			e->cx = len;
		break;
	}

	for (;;) {
		nx = prevutf8(e, e->cy, e->cx);
		if (nx == e->cx) {
			if (e->cy == 0)
				return;
			e->cy--;
			e->cx = linelen(e, e->cy);
			continue;
		}
		e->cx = nx;
		c = peekbyte(e, e->cy, e->cx);
		if (c < 0)
			continue;
		if (!isws(c))
			break;
	}

	c = peekbyte(e, e->cy, e->cx);
	cls = isword(c) ? 1 : (ispunctword(c) ? 2 : 0);
	if (cls == 0)
		return;

	for (;;) {
		nx = prevutf8(e, e->cy, e->cx);
		if (nx == e->cx)
			break;
		c = peekbyte(e, e->cy, nx);
		if (c < 0)
			break;
		if (cls == 1 && !isword(c))
			break;
		if (cls == 2 && !ispunctword(c))
			break;
		e->cx = nx;
		if (e->cx == 0)
			break;
	}
}

/*
 * findfwd moves the cursor to the next occurrence of rune r on the current
 * line, searching forward.
 *
 * Returns:
 *  - 0 if a match is found and the cursor is moved, -1 otherwise.
 */
int
findfwd(Eek *e, long r, long n)
{
	Line *l;
	char pat[8];
	long patn;
	long x;
	const char *s;
	long ln;

	if (e == nil)
		return -1;
	if (n <= 0)
		n = 1;
	if (r < 0x20)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	s = linebytes(l);
	ln = lsz(l->n);
	patn = utf8enc(r, pat);
	if (patn <= 0)
		return -1;
	if (patn > ln)
		return -1;

	x = nextutf8(e, e->cy, e->cx);
	for (; x + patn <= ln; x = nextutf8(e, e->cy, x)) {
		if (memcmp(s + x, pat, (size_t)patn) == 0) {
			n--;
			if (n == 0) {
				e->cx = x;
				return 0;
			}
		}
		if (x >= ln)
			break;
	}
	return -1;
}

/*
 * findbwd moves the cursor to the previous occurrence of rune r on the current
 * line, searching backward.
 *
 * Returns:
 *  - 0 if a match is found and the cursor is moved, -1 otherwise.
 */
int
findbwd(Eek *e, long r, long n)
{
	Line *l;
	char pat[8];
	long patn;
	long x;
	const char *s;
	long ln;

	if (e == nil)
		return -1;
	if (n <= 0)
		n = 1;
	if (r < 0x20)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	s = linebytes(l);
	ln = lsz(l->n);
	patn = utf8enc(r, pat);
	if (patn <= 0)
		return -1;
	if (patn > ln)
		return -1;
	if (e->cx <= 0)
		return -1;

	x = prevutf8(e, e->cy, e->cx);
	for (;;) {
		if (x + patn <= ln && memcmp(s + x, pat, (size_t)patn) == 0) {
			n--;
			if (n == 0) {
				e->cx = x;
				return 0;
			}
		}
		if (x <= 0)
			break;
		x = prevutf8(e, e->cy, x);
	}
	return -1;
}
