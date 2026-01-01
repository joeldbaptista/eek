#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "buf.h"
#include "config.h"
#include "util.h"

static int bufgrow(Buf *b, size_t need);

static int
dblsz(size_t *v)
{
	if (v == nil)
		return -1;
	if (*v > SIZE_MAX / 2)
		return -1;
	*v *= 2;
	return 0;
}

static int
mulsz(size_t a, size_t b, size_t *out)
{
	if (out == nil)
		return -1;
	if (a != 0 && b != 0 && a > SIZE_MAX / b)
		return -1;
	*out = a * b;
	return 0;
}

static void
bufswap(Buf *a, Buf *b)
{
	Buf t;

	if (a == nil || b == nil)
		return;
	t = *a;
	*a = *b;
	*b = t;
}

/*
 * lineinit initializes a Line to an empty state.
 *
 * Parameters:
 *  - l: line to initialize.
 *
 * Returns:
 *  - void.
 */
static void
lineinit(Line *l)
{
	l->s = nil;
	l->n = 0;
	l->cap = 0;
	l->start = 0;
	l->end = 0;
}

/*
 * linefree releases memory owned by a Line and reinitializes it.
 *
 * Parameters:
 *  - l: line to free.
 *
 * Returns:
 *  - void.
 */
static void
linefree(Line *l)
{
	free(l->s);
	lineinit(l);
}

static size_t
linegaplen(const Line *l)
{
	if (l == nil)
		return 0;
	if (l->end < l->start)
		return 0;
	return l->end - l->start;
}

/*
 * linemovegap moves the gap to the given logical byte offset.
 * After return, l->start == at.
 */
static void
linemovegap(Line *l, size_t at)
{
	size_t d;
	size_t gl;

	if (l == nil)
		return;
	if (at > l->n)
		at = l->n;
	if (l->s == nil || l->cap <= 0) {
		l->start = 0;
		l->end = 0;
		return;
	}
	gl = linegaplen(l);
	if (at < l->start) {
		d = l->start - at;
		memmove(l->s + (l->end - d), l->s + at, (size_t)d);
		l->start -= d;
		l->end -= d;
	} else if (at > l->start) {
		d = at - l->start;
		memmove(l->s + l->start, l->s + l->end, (size_t)d);
		l->start += d;
		l->end += d;
	}
	/* Keep invariants sane if something went wrong. */
	if (l->start > l->n)
		l->start = l->n;
	if (l->end < l->start)
		l->end = l->start;
	if (l->end > l->cap)
		l->end = l->cap;
	(void)gl;
}

/*
 * lineensuregap ensures the gap has at least need bytes available.
 */
static int
lineensuregap(Line *l, size_t need)
{
	char *ns;
	size_t ncap;
	size_t rlen;
	size_t newend;
	size_t nbytes;

	if (l == nil)
		return -1;
	if (linegaplen(l) >= need)
		return 0;

	ncap = l->cap ? l->cap : (size_t)LINE_MIN_CAP;
	while (ncap - l->n < need) {
		if (dblsz(&ncap) < 0)
			return -1;
	}
	nbytes = ncap;

	ns = malloc(nbytes);
	if (ns == nil)
		return -1;
	/* Copy left side. */
	if (l->start > 0)
		memcpy(ns, l->s, (size_t)l->start);
	/* Copy right side to the end of the new buffer. */
	rlen = l->n - l->start;
	newend = ncap - rlen;
	if (rlen > 0)
		memcpy(ns + newend, l->s + l->end, (size_t)rlen);

	free(l->s);
	l->s = ns;
	l->cap = ncap;
	l->end = newend;
	if (l->end < l->start)
		l->end = l->start;
	return 0;
}

/*
 * linecopy deep-copies src into dst.
 *
 * Parameters:
 *  - dst: destination line (initialized by this function).
 *  - src: source line.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
linecopy(Line *dst, Line *src)
{
	size_t llen;
	size_t rlen;
	size_t cap;

	lineinit(dst);
	if (src == nil)
		return 0;
	if (src->n == 0)
		return 0;

	/* Copy without mutating src (don't move its gap). */
	llen = src->start;
	if (llen > src->n)
		llen = src->n;
	rlen = src->n - llen;

	cap = src->n;
	if (cap < (size_t)LINE_MIN_CAP)
		cap = (size_t)LINE_MIN_CAP;
	dst->s = malloc((size_t)cap);
	if (dst->s == nil) {
		lineinit(dst);
		return -1;
	}
	if (llen > 0)
		memcpy(dst->s, src->s, (size_t)llen);
	if (rlen > 0)
		memcpy(dst->s + llen, src->s + src->end, (size_t)rlen);
	dst->n = src->n;
	dst->cap = cap;
	dst->start = src->n;
	dst->end = cap;
	return 0;
}

/*
 * bufinit initializes a buffer to contain a single empty line.
 *
 * Parameters:
 *  - b: buffer to initialize.
 *
 * Returns:
 *  - void.
 */
void
bufinit(Buf *b)
{
	b->line = nil;
	b->nline = 0;
	b->cap = 0;

	(void)bufinsertline(b, 0, "", 0);
}

/*
 * buffree releases all memory owned by the buffer and resets it.
 *
 * Parameters:
 *  - b: buffer to free.
 *
 * Returns:
 *  - void.
 */
void
buffree(Buf *b)
{
	size_t i;

	for (i = 0; i < b->nline; i++)
		linefree(&b->line[i]);
	free(b->line);
	b->line = nil;
	b->nline = 0;
	b->cap = 0;
}

/*
 * bufcopy deep-copies src into dst.
 *
 * Parameters:
 *  - dst: destination buffer (previous contents are freed).
 *  - src: source buffer.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on invalid arguments or allocation failure.
 */
int
bufcopy(Buf *dst, Buf *src)
{
	size_t i;
	Buf tmp;

	if (dst == nil || src == nil)
		return -1;

	tmp.line = nil;
	tmp.nline = 0;
	tmp.cap = 0;

	if (bufgrow(&tmp, src->nline) < 0)
		return -1;
	for (i = 0; i < src->nline; i++) {
		if (linecopy(&tmp.line[i], &src->line[i]) < 0) {
			buffree(&tmp);
			return -1;
		}
	}
	tmp.nline = src->nline;

	bufswap(dst, &tmp);
	buffree(&tmp);
	return 0;
}

/*
 * bufgetline returns a pointer to the i-th line in the buffer.
 *
 * Parameters:
 *  - b: buffer.
 *  - i: line index.
 *
 * Returns:
 *  - pointer to Line on success.
 *  - nil if out of range.
 */
Line *
bufgetline(Buf *b, long i)
{
	if (b == nil)
		return nil;
	if (i < 0 || (size_t)i >= b->nline)
		return nil;
	return &b->line[i];
}

/*
 * bufgrow ensures b has capacity for at least need lines.
 *
 * Parameters:
 *  - b: buffer.
 *  - need: required capacity in lines.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
bufgrow(Buf *b, size_t need)
{
	Line *nl;
	size_t ncap;
	size_t nbytes;

	if (need <= b->cap)
		return 0;

	ncap = b->cap ? b->cap : 8;
	while (ncap < need) {
		if (dblsz(&ncap) < 0)
			return -1;
	}
	if (mulsz(ncap, sizeof *nl, &nbytes) < 0)
		return -1;

	nl = realloc(b->line, nbytes);
	if (nl == nil)
		return -1;
	b->line = nl;
	b->cap = ncap;
	return 0;
}

/*
 * bufinsertline inserts a new line at index at.
 *
 * Parameters:
 *  - b: buffer.
 *  - at: index to insert at (clamped).
 *  - s: line contents bytes.
 *  - n: number of bytes to copy.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
int
bufinsertline(Buf *b, long at, const char *s, size_t n)
{
	size_t i;
	size_t uat;
	Line tmp;
	size_t cap;

	if (b == nil)
		return -1;

	if (at < 0)
		uat = 0;
	else
		uat = (size_t)at;
	if (uat > b->nline)
		uat = b->nline;
	if (n > 0 && s == nil)
		return -1;

	/* Prepare new element first so failures don't mutate b. */
	lineinit(&tmp);
	if (n > 0) {
		cap = n;
		if (cap < (size_t)LINE_MIN_CAP)
			cap = (size_t)LINE_MIN_CAP;
		tmp.s = malloc(cap);
		if (tmp.s == nil)
			return -1;
		memcpy(tmp.s, s, (size_t)n);
		tmp.n = n;
		tmp.cap = cap;
		tmp.start = n;
		tmp.end = cap;
	}

	if (bufgrow(b, b->nline + 1) < 0) {
		free(tmp.s);
		return -1;
	}

	for (i = b->nline; i > uat; i--)
		b->line[i] = b->line[i - 1];
	b->line[uat] = tmp;
	b->nline++;
	return 0;
}

/*
 * bufdelline deletes the line at index at.
 *
 * Parameters:
 *  - b: buffer.
 *  - at: line index.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 if out of range.
 */
int
bufdelline(Buf *b, long at)
{
	size_t i;
	size_t uat;

	if (b == nil)
		return -1;
	if (at < 0)
		return -1;
	uat = (size_t)at;
	if (uat >= b->nline)
		return -1;

	linefree(&b->line[uat]);
	for (i = uat; i + 1 < b->nline; i++)
		b->line[i] = b->line[i + 1];
	b->nline--;
	if (b->nline == 0)
		(void)bufinsertline(b, 0, "", 0);
	return 0;
}

/*
 * linegrow ensures l has capacity for at least need bytes.
 *
 * Parameters:
 *  - l: line.
 *  - need: required capacity in bytes.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
/*
 * lineinsert inserts bytes into a line at byte offset at.
 *
 * Parameters:
 *  - l: line to modify.
 *  - at: byte offset.
 *  - s: bytes to insert.
 *  - n: number of bytes.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on invalid offset or allocation failure.
 */
int
lineinsert(Line *l, long at, const char *s, size_t n)
{
	size_t uat;

	if (n == 0)
		return 0;
	if (l == nil || (s == nil && n > 0))
		return -1;
	if (at < 0)
		uat = 0;
	else
		uat = (size_t)at;
	if (uat > l->n)
		uat = l->n;

	linemovegap(l, uat);
	if (lineensuregap(l, n) < 0)
		return -1;
	memcpy(l->s + l->start, s, (size_t)n);
	l->start += n;
	l->n += n;
	return 0;
}

/*
 * linedelrange deletes n bytes starting at at from a line.
 *
 * Parameters:
 *  - l: line to modify.
 *  - at: start offset.
 *  - n: number of bytes.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on invalid offset.
 */
int
linedelrange(Line *l, long at, size_t n)
{
	size_t uat;

	if (n == 0)
		return 0;
	if (l == nil)
		return -1;
	if (at < 0)
		return -1;
	uat = (size_t)at;
	if (uat >= l->n)
		return -1;
	if (n > l->n - uat)
		n = l->n - uat;
	linemovegap(l, uat);
	l->end += n;
	l->n -= n;
	return 0;
}

const char *
linebytes(Line *l)
{
	if (l == nil)
		return nil;
	if (l->n == 0)
		return l->s;
	linemovegap(l, l->n);
	return l->s;
}

int
linetake(Line *l, char *s, size_t n)
{
	if (l == nil)
		return -1;
	if (n > 0 && s == nil)
		return -1;
	free(l->s);
	l->s = s;
	l->n = n;
	l->cap = n;
	l->start = n;
	l->end = n;
	return 0;
}

/*
 * bufload loads a file into b (replacing existing contents).
 * Newlines are split into separate lines and not stored in Line.s.
 *
 * Parameters:
 *  - b: destination buffer.
 *  - path: file path.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
int
bufload(Buf *b, const char *path)
{
	FILE *fp;
	char *line;
	size_t cap;
	ssize_t n;

	fp = fopen(path, "r");
	if (fp == nil)
		return -1;

	buffree(b);
	b->line = nil;
	b->nline = 0;
	b->cap = 0;

	line = nil;
	cap = 0;
	while ((n = getline(&line, &cap, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			n--;
		if (n < 0) {
			free(line);
			(void)fclose(fp);
			return -1;
		}
		if (bufinsertline(b, (long)b->nline, line, (size_t)n) < 0) {
			free(line);
			(void)fclose(fp);
			return -1;
		}
	}
	free(line);
	(void)fclose(fp);

	if (b->nline == 0)
		(void)bufinsertline(b, 0, "", 0);
	return 0;
}

/*
 * bufsave writes the buffer to a file.
 * Each stored line is written followed by a newline.
 *
 * Parameters:
 *  - b: buffer to write.
 *  - path: file path.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
int
bufsave(Buf *b, const char *path)
{
	FILE *fp;
	size_t i;
	Line *l;
	const char *p;

	fp = fopen(path, "w");
	if (fp == nil)
		return -1;

	for (i = 0; i < b->nline; i++) {
		l = &b->line[i];
		p = linebytes(l);
		if (l->n > 0 && fwrite(p, 1, l->n, fp) != l->n) {
			(void)fclose(fp);
			return -1;
		}
		if (fputc('\n', fp) == EOF) {
			(void)fclose(fp);
			return -1;
		}
	}

	if (fclose(fp) != 0)
		return -1;
	return 0;
}
