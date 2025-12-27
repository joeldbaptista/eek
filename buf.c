#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "util.h"

static int bufgrow(Buf *b, long need);

static void
lineinit(Line *l)
{
	l->s = nil;
	l->n = 0;
	l->cap = 0;
}

static void
linefree(Line *l)
{
	free(l->s);
	lineinit(l);
}

static int
linecopy(Line *dst, Line *src)
{
	lineinit(dst);
	if (src == nil)
		return 0;
	if (src->n <= 0)
		return 0;
	dst->s = malloc((size_t)src->n);
	if (dst->s == nil)
		return -1;
	memcpy(dst->s, src->s, (size_t)src->n);
	dst->n = src->n;
	dst->cap = src->n;
	return 0;
}

void
bufinit(Buf *b)
{
	b->line = nil;
	b->nline = 0;
	b->cap = 0;

	(void)bufinsertline(b, 0, "", 0);
}

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

Line *
bufgetline(Buf *b, long i)
{
	if (i < 0 || i >= b->nline)
		return nil;
	return &b->line[i];
}

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

int
bufinsertline(Buf *b, long at, const char *s, long n)
{
	long i;
	Line *l;

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
	if (n > 0) {
		l->s = malloc((size_t)n);
		if (l->s == nil)
			return -1;
		memcpy(l->s, s, (size_t)n);
		l->n = n;
		l->cap = n;
	}
	b->nline++;
	return 0;
}

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

static int
linegrow(Line *l, long need)
{
	char *ns;
	long ncap;

	if (need <= l->cap)
		return 0;

	ncap = l->cap ? l->cap : 32;
	while (ncap < need)
		ncap *= 2;

	ns = realloc(l->s, (size_t)ncap);
	if (ns == nil)
		return -1;
	l->s = ns;
	l->cap = ncap;
	return 0;
}

int
lineinsert(Line *l, long at, const char *s, long n)
{
	long i;

	if (n <= 0)
		return 0;
	if (at < 0)
		at = 0;
	if (at > l->n)
		at = l->n;

	if (linegrow(l, l->n + n) < 0)
		return -1;

	for (i = l->n; i > at; i--)
		l->s[i + n - 1] = l->s[i - 1];

	memcpy(l->s + at, s, (size_t)n);
	l->n += n;
	return 0;
}

int
linedelrange(Line *l, long at, long n)
{
	long i;

	if (n <= 0)
		return 0;
	if (at < 0 || at >= l->n)
		return -1;
	if (at + n > l->n)
		n = l->n - at;

	for (i = at + n; i < l->n; i++)
		l->s[i - n] = l->s[i];
	l->n -= n;
	return 0;
}

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

int
bufsave(Buf *b, const char *path)
{
	FILE *fp;
	long i;
	Line *l;

	fp = fopen(path, "w");
	if (fp == nil)
		return -1;

	for (i = 0; i < b->nline; i++) {
		l = &b->line[i];
		if (l->n > 0 && fwrite(l->s, 1, (size_t)l->n, fp) != (size_t)l->n) {
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
