#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "util.h"

static int bufgrow(Buf *b, long need);

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

static long
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
linemovegap(Line *l, long at)
{
	long d;
	long gl;

	if (l == nil)
		return;
	if (at < 0)
		at = 0;
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
	if (l->start < 0)
		l->start = 0;
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
lineensuregap(Line *l, long need)
{
	char *ns;
	long ncap;
	long rlen;
	long newend;

	if (l == nil)
		return -1;
	if (need < 0)
		need = 0;
	if (linegaplen(l) >= need)
		return 0;

	ncap = l->cap ? l->cap : 32;
	while (ncap - l->n < need)
		ncap *= 2;

	ns = malloc((size_t)ncap);
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
	const char *p;

	lineinit(dst);
	if (src == nil)
		return 0;
	if (src->n <= 0)
		return 0;

	/* Copy via contiguous view of src. */
	p = linebytes(src);
	if (p == nil)
		return 0;
	dst->cap = src->n;
	if (dst->cap < 32)
		dst->cap = 32;
	dst->s = malloc((size_t)dst->cap);
	if (dst->s == nil) {
		lineinit(dst);
		return -1;
	}
	memcpy(dst->s, p, (size_t)src->n);
	dst->n = src->n;
	dst->start = src->n;
	dst->end = dst->cap;
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
	long i;

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
	long i;

	if (dst == nil || src == nil)
		return -1;

	buffree(dst);
	dst->line = nil;
	dst->nline = 0;
	dst->cap = 0;

	if (bufgrow(dst, src->nline) < 0)
		return -1;
	for (i = 0; i < src->nline; i++) {
		if (linecopy(&dst->line[i], &src->line[i]) < 0) {
			buffree(dst);
			return -1;
		}
	}
	dst->nline = src->nline;
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
	if (i < 0 || i >= b->nline)
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
bufgrow(Buf *b, long need)
{
	Line *nl;
	long ncap;

	if (need <= b->cap)
		return 0;

	ncap = b->cap ? b->cap : 8;
	while (ncap < need)
		ncap *= 2;

	nl = realloc(b->line, (size_t)ncap * sizeof *nl);
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
bufinsertline(Buf *b, long at, const char *s, long n)
{
	long i;
	Line *l;
	long cap;

	if (at < 0)
		at = 0;
	if (at > b->nline)
		at = b->nline;

	if (bufgrow(b, b->nline + 1) < 0)
		return -1;

	for (i = b->nline; i > at; i--)
		b->line[i] = b->line[i - 1];

	l = &b->line[at];
	lineinit(l);
	if (n < 0)
		n = 0;
	if (n > 0 && s == nil)
		return -1;
	cap = n;
	if (cap < 32)
		cap = 32;
	l->s = malloc((size_t)cap);
	if (l->s == nil) {
		lineinit(l);
		return -1;
	}
	if (n > 0)
		memcpy(l->s, s, (size_t)n);
	l->n = n;
	l->cap = cap;
	l->start = n;
	l->end = cap;
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
	long i;

	if (at < 0 || at >= b->nline)
		return -1;

	linefree(&b->line[at]);
	for (i = at; i + 1 < b->nline; i++)
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
lineinsert(Line *l, long at, const char *s, long n)
{
	if (n <= 0)
		return 0;
	if (l == nil || (s == nil && n > 0))
		return -1;
	if (at < 0)
		at = 0;
	if (at > l->n)
		at = l->n;

	linemovegap(l, at);
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
linedelrange(Line *l, long at, long n)
{
	if (n <= 0)
		return 0;
	if (l == nil)
		return -1;
	if (at < 0 || at >= l->n)
		return -1;
	if (at + n > l->n)
		n = l->n - at;
	linemovegap(l, at);
	l->end += n;
	l->n -= n;
	return 0;
}

const char *
linebytes(Line *l)
{
	if (l == nil)
		return nil;
	if (l->n <= 0)
		return l->s;
	linemovegap(l, l->n);
	return l->s;
}

int
linetake(Line *l, char *s, long n)
{
	if (l == nil)
		return -1;
	if (n < 0)
		n = 0;
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
		if (bufinsertline(b, b->nline, line, (long)n) < 0) {
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
	long i;
	Line *l;
	const char *p;

	fp = fopen(path, "w");
	if (fp == nil)
		return -1;

	for (i = 0; i < b->nline; i++) {
		l = &b->line[i];
		p = linebytes(l);
		if (l->n > 0 && fwrite(p, 1, (size_t)l->n, fp) != (size_t)l->n) {
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
