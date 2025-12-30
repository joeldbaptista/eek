#include <string.h>

#include "eek_internal.h"

long
utf8dec1(const char *s, long n, long *adv)
{
	unsigned char c;
	long r;
	long m;

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
		long b1, b2;
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
		long b1, b2, b3;
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

long
clamp(long v, long lo, long hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

long
linelen(Eek *e, long y)
{
	Line *l;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	return l->n;
}

long
prevutf8(Eek *e, long y, long at)
{
	Line *l;
	unsigned char c;
	long i;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	if (at <= 0)
		return 0;

	i = at - 1;
	for (; i > 0; i--) {
		c = (unsigned char)l->s[i];
		if ((c & 0xc0) != 0x80)
			break;
	}
	return i;
}

long
nextutf8(Eek *e, long y, long at)
{
	Line *l;
	unsigned char c;
	long n;

	(void)e;
	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	if (at >= l->n)
		return l->n;

	c = (unsigned char)l->s[at];
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
	if (at + n > l->n)
		return l->n;
	return at + n;
}

int
isws(long c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

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

long
peekbyte(Eek *e, long y, long at)
{
	Line *l;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return -1;
	if (at < 0 || at >= l->n)
		return -1;
	return (unsigned char)l->s[at];
}

void
movel(Eek *e)
{
	e->cx = prevutf8(e, e->cy, e->cx);
}

void
mover(Eek *e)
{
	e->cx = nextutf8(e, e->cy, e->cx);
}

void
moveu(Eek *e)
{
	e->cy = clamp(e->cy - 1, 0, e->b.nline - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

void
moved(Eek *e)
{
	e->cy = clamp(e->cy + 1, 0, e->b.nline - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

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
		if (e->cy + 1 >= e->b.nline)
			break;
		e->cy++;
		e->cx = 0;
		c = peekbyte(e, e->cy, e->cx);
		if (c >= 0 && isws(c))
			continue;
		break;
	}
}

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

int
findfwd(Eek *e, long r, long n)
{
	Line *l;
	char pat[8];
	long patn;
	long x;

	if (e == nil)
		return -1;
	if (n <= 0)
		n = 1;
	if (r < 0x20)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	patn = utf8enc(r, pat);
	if (patn <= 0)
		return -1;
	if (patn > l->n)
		return -1;

	x = nextutf8(e, e->cy, e->cx);
	for (; x + patn <= l->n; x = nextutf8(e, e->cy, x)) {
		if (memcmp(l->s + x, pat, (size_t)patn) == 0) {
			n--;
			if (n == 0) {
				e->cx = x;
				return 0;
			}
		}
		if (x >= l->n)
			break;
	}
	return -1;
}

int
findbwd(Eek *e, long r, long n)
{
	Line *l;
	char pat[8];
	long patn;
	long x;

	if (e == nil)
		return -1;
	if (n <= 0)
		n = 1;
	if (r < 0x20)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	patn = utf8enc(r, pat);
	if (patn <= 0)
		return -1;
	if (patn > l->n)
		return -1;
	if (e->cx <= 0)
		return -1;

	x = prevutf8(e, e->cy, e->cx);
	for (;;) {
		if (x + patn <= l->n && memcmp(l->s + x, pat, (size_t)patn) == 0) {
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
