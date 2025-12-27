
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "buf.h"
#include "eek.h"
#include "util.h"

typedef struct Eek Eek;

typedef struct Undo Undo;
struct Undo {
	Buf b;       /* Snapshot of the full text buffer. */
	long cx;     /* Cursor x (byte offset within line). */
	long cy;     /* Cursor y (line index). */
	long rowoff; /* Topmost visible line (scroll offset). */
	long dirty;  /* Dirty flag at time of snapshot. */
};

enum {
	Synnone,
	Sync,
};

enum {
	Hlnone,
	Hlcomment,
	Hlstring,
	Hlnumber,
	Hlkeyword,
	Hltype,
	Hlpreproc,
	Hlspecial,
};

#include "syntax.h"

typedef struct SynState SynState;
struct SynState {
	int inblock; /* Whether the scanner is inside a block comment. */
};

enum {
	Modenormal,
	Modeinsert,
	Modecmd,
	Modevisual,
};

struct Eek {
	Term t;              /* Terminal I/O state and dimensions. */
	Buf b;               /* Text buffer (array of lines). */
	char *fname;         /* Current file name (may be nil). */
	int ownfname;        /* Whether fname is heap-owned and must be freed. */
	int mode;            /* Current editor mode (Modenormal/Modeinsert/...). */
	int synenabled;      /* User toggle for syntax highlighting. */
	int syntax;          /* Active syntax language (Syn*). */
	int cursorshape;     /* Current cursor shape (DECSCUSR value). */
	int linenumbers;     /* Show absolute line numbers. */
	int relativenumbers; /* Show relative line numbers. */
	long lastnormalrune; /* Previous rune in NORMAL for multi-key sequences (e.g. 'g'). */
	long lastmotioncount;/* Previous motion count (used by some sequences). */
	long seqcount;       /* Count captured for sequences like 'gg'. */
	long count;          /* Current numeric prefix being parsed. */
	long opcount;        /* Operator count multiplier (e.g. 3dw => opcount=3). */
	char *ybuf;          /* Yank/delete register contents (raw bytes). */
	long ylen;           /* Length of ybuf in bytes. */
	int yline;           /* Non-zero if ybuf is linewise. */
	long cx;             /* Cursor x: byte offset in current line. */
	long cy;             /* Cursor y: current line index. */
	long rowoff;         /* Vertical scroll offset (topmost visible line). */
	long dirty;          /* Non-zero if buffer has unsaved modifications. */
	long dpending;       /* Pending delete operator ('d' has been typed). */
	long cpending;       /* Pending change operator ('c' has been typed). */
	long ypending;       /* Pending yank operator ('y' has been typed). */
	long fpending;       /* Pending find-char motion ('f' has been typed). */
	long fcount;         /* Count for pending find motion (nth occurrence). */
	long tipending;      /* Pending text-object modifier (e.g. 'di' waiting for delimiter). */
	long tiop;           /* Text-object operator that is pending ('d' or 'c'). */
	long vax;            /* VISUAL anchor x (byte offset). */
	long vay;            /* VISUAL anchor y (line index). */
	long vtipending;     /* VISUAL pending text-object modifier. */
	char cmd[256];       /* Command-line buffer (for ':' and '/' prompts). */
	long cmdn;           /* Number of bytes used in cmd. */
	char cmdprefix;      /* Prompt prefix character (':' or '/'). */
	char *lastsearch;    /* Last search pattern (heap-owned) or nil. */
	char msg[256];       /* Status message shown in the status line. */
	long quit;           /* Non-zero requests program exit. */
	Undo *undo;          /* Undo snapshot stack (dynamic array). */
	long nundo;          /* Number of undo entries currently stored. */
	long capundo;        /* Allocated capacity of undo[] in entries. */
	int undopending;     /* Groups multiple edits into a single undo step (e.g. INSERT session). */
	int inundo;          /* Non-zero while restoring undo (prevents recursive snapshotting). */
};

static long linelen(Eek *e, long y);
static long prevutf8(Eek *e, long y, long at);
static long nextutf8(Eek *e, long y, long at);
static long utf8enc(long r, char *s);
static int insertbytes(Eek *e, const char *s, long n);
static int insertnl(Eek *e);
static void wordtarget(Eek *e, long *ty, long *tx);
static void endwordtarget(Eek *e, long *ty, long *tx);
static void setmode(Eek *e, int mode);
static int yankrange(Eek *e, long y0, long x0, long y1, long x1);
static int delimpair(long c, char *open, char *close);
static int findopen(Eek *e, char open, char close, long *oy, long *ox);
static int findclosefrom(Eek *e, long sy, long sx, char open, char close, long *cy, long *cx);

static void setsyn(Eek *e);
static void syninit(SynState *s);
static void synscanuntil(Eek *e, long upto, SynState *s);
static void synscanline(Line *l, SynState *s);
static const char *synesc(int hl);
static void drawattrs(Eek *e, int inv, int hl);

static int findfwd(Eek *e, long r, long n);

static int undopush(Eek *e);
static void undofree(Eek *e);
static void undopop(Eek *e);

/*
 * findfwd moves the cursor to the next occurrence of rune r on the current
 * line, searching forward.
 *
 * The search is UTF-8 aware: r is encoded to UTF-8 bytes and matched against
 * the line buffer at codepoint boundaries.
 *
 * Parameters:
 *  e: editor state.
 *  r: rune to search for.
 *  n: occurrence count (1 means the next match).
 *
 * Returns:
 *  0 if a match is found and the cursor is moved, -1 otherwise.
 */
static int
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

/*
 * poslt compares two (y, x) positions.
 *
 * Parameters:
 *  - ay, ax: first position.
 *  - by, bx: second position.
 *
 * Returns:
 *  - 1 if (ay,ax) < (by,bx), else 0.
 */
static int
poslt(long ay, long ax, long by, long bx)
{
	if (ay < by)
		return 1;
	if (ay > by)
		return 0;
	return ax < bx;
}

/*
 * vselbounds computes the inclusive start and exclusive end bounds of the
 * current VISUAL selection in buffer coordinates.
 *
 * Parameters:
 *  - e: editor state.
 *  - sy, sx: output start position (inclusive).
 *  - ey, ex: output end position (exclusive, UTF-8 advanced by one codepoint).
 *
 * Returns:
 *  - void.
 */
static void
vselbounds(Eek *e, long *sy, long *sx, long *ey, long *ex)
{
	long ay, ax;
	long by, bx;
	long ty, tx;

	ay = e->vay;
	ax = e->vax;
	by = e->cy;
	bx = e->cx;
	if (poslt(by, bx, ay, ax)) {
		ty = ay;
		tx = ax;
		ay = by;
		ax = bx;
		by = ty;
		bx = tx;
	}
	*sy = ay;
	*sx = ax;
	*ey = by;
	*ex = nextutf8(e, by, bx);
}

/*
 * invsel reports whether the byte position (y, x) lies inside the current
 * VISUAL selection.
 *
 * Parameters:
 *  - e: editor state.
 *  - y, x: position to test.
 *
 * Returns:
 *  - non-zero if selected, 0 otherwise.
 */
static int
invsel(Eek *e, long y, long x)
{
	long sy, sx, ey, ex;

	if (e->mode != Modevisual)
		return 0;
	vselbounds(e, &sy, &sx, &ey, &ex);
	if (y < sy || y > ey)
		return 0;
	if (sy == ey)
		return x >= sx && x < ex;
	if (y == sy)
		return x >= sx;
	if (y == ey)
		return x < ex;
	return 1;
}

/*
 * vselectinside updates the VISUAL selection to the inside of a delimiter pair
 * surrounding the cursor (e.g. i(, i{, i[).
 *
 * Parameters:
 *  - e: editor state.
 *  - c: delimiter character identifying the pair.
 *
 * Returns:
 *  - 0 on success (including empty selection).
 *  - -1 if no matching pair is found.
 */
static int
vselectinside(Eek *e, long c)
{
	char open, close;
	long oy, ox;
	long cy, cx;
	long starty, startx;
	long endy, endx;

	if (!delimpair(c, &open, &close))
		return -1;
	if (findopen(e, open, close, &oy, &ox) < 0)
		return -1;
	if (findclosefrom(e, oy, ox, open, close, &cy, &cx) < 0)
		return -1;
	starty = oy;
	startx = ox + 1;
	endy = cy;
	endx = prevutf8(e, cy, cx);
	if (poslt(endy, endx, starty, startx)) {
		/* empty; keep cursor where it is */
		return 0;
	}
	e->vay = starty;
	e->vax = startx;
	e->cy = endy;
	e->cx = endx;
	return 0;
}

/*
 * delimpair maps a delimiter character to its opening and closing pair.
 *
 * Parameters:
 *  - c: delimiter character.
 *  - open, close: output pair characters.
 *
 * Returns:
 *  - 1 if c is a supported delimiter, 0 otherwise.
 */
static int
delimpair(long c, char *open, char *close)
{
	switch (c) {
	case '(':
	case ')':
		*open = '(';
		*close = ')';
		return 1;
	case '[':
	case ']':
		*open = '[';
		*close = ']';
		return 1;
	case '{':
	case '}':
		*open = '{';
		*close = '}';
		return 1;
	case '<':
	case '>':
		*open = '<';
		*close = '>';
		return 1;
	default:
		return 0;
	}
}

/*
 * findopen searches backward for the matching opening delimiter, respecting
 * nesting.
 *
 * Parameters:
 *  - e: editor state.
 *  - open, close: delimiter pair.
 *  - oy, ox: output position of the opening delimiter.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 if not found.
 */
static int
findopen(Eek *e, char open, char close, long *oy, long *ox)
{
	long y, x;
	Line *l;
	long depth;

	depth = 0;
	for (y = e->cy; y >= 0; y--) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		if (l->n == 0)
			continue;
		x = l->n - 1;
		if (y == e->cy) {
			x = e->cx;
			if (x >= l->n)
				x = l->n - 1;
		}
		for (; x >= 0; x--) {
			unsigned char c;

			c = (unsigned char)l->s[x];
			if (c == (unsigned char)close)
				depth++;
			else if (c == (unsigned char)open) {
				if (depth == 0) {
					*oy = y;
					*ox = x;
					return 0;
				}
				depth--;
			}
		}
	}
	return -1;
}

/*
 * findclosefrom searches forward for the matching closing delimiter starting
 * just after (sy, sx), respecting nesting.
 *
 * Parameters:
 *  - e: editor state.
 *  - sy, sx: starting position of an opening delimiter.
 *  - open, close: delimiter pair.
 *  - cy, cx: output position of the closing delimiter.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 if not found.
 */
static int
findclosefrom(Eek *e, long sy, long sx, char open, char close, long *cy, long *cx)
{
	long y, x;
	Line *l;
	long depth;

	depth = 0;
	for (y = sy; y < e->b.nline; y++) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		x = 0;
		if (y == sy)
			x = sx + 1;
		for (; x < l->n; x++) {
			unsigned char c;

			c = (unsigned char)l->s[x];
			if (c == (unsigned char)open)
				depth++;
			else if (c == (unsigned char)close) {
				if (depth == 0) {
					*cy = y;
					*cx = x;
					return 0;
				}
				depth--;
			}
		}
	}
	return -1;
}

/*
 * delrange deletes text from (y0, x0) to (y1, x1). If yank is non-zero, it
 * yanks the deleted text into the yank register before deletion.
 *
 * Parameters:
 *  - e: editor state.
 *  - y0, x0: one endpoint.
 *  - y1, x1: other endpoint.
 *  - yank: whether to yank the range prior to deleting.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
static int
delrange(Eek *e, long y0, long x0, long y1, long x1, int yank)
{
	Line *l0, *l1;
	long i;

	if (undopush(e) < 0)
		return -1;

	if (y1 < y0 || (y1 == y0 && x1 < x0)) {
		long ty, tx;
		ty = y0;
		tx = x0;
		y0 = y1;
		x0 = x1;
		y1 = ty;
		x1 = tx;
	}
	if (y0 < 0)
		y0 = 0;
	if (y1 >= e->b.nline)
		y1 = e->b.nline - 1;
	if (y0 > y1)
		return 0;

	if (yank)
		(void)yankrange(e, y0, x0, y1, x1);

	if (y0 == y1) {
		l0 = bufgetline(&e->b, y0);
		if (l0 == nil)
			return -1;
		if (x0 < 0)
			x0 = 0;
		if (x0 > l0->n)
			x0 = l0->n;
		if (x1 < 0)
			x1 = 0;
		if (x1 > l0->n)
			x1 = l0->n;
		if (x1 > x0) {
			if (x0 < l0->n && linedelrange(l0, x0, x1 - x0) < 0)
				return -1;
			e->cy = y0;
			e->cx = x0;
			e->dirty = 1;
		}
		return 0;
	}

	/* delete middle lines */
	for (i = y0 + 1; i < y1; i++)
		(void)bufdelline(&e->b, y0 + 1);

	l0 = bufgetline(&e->b, y0);
	l1 = bufgetline(&e->b, y0 + 1);
	if (l0 == nil || l1 == nil)
		return -1;

	/* truncate start of l1 */
	if (x1 < 0)
		x1 = 0;
	if (x1 > l1->n)
		x1 = l1->n;
	if (x1 > 0 && l1->n > 0)
		(void)linedelrange(l1, 0, x1);

	/* truncate end of l0 */
	if (x0 < 0)
		x0 = 0;
	if (x0 > l0->n)
		x0 = l0->n;
	if (x0 < l0->n)
		(void)linedelrange(l0, x0, l0->n - x0);

	if (l1->n > 0)
		(void)lineinsert(l0, l0->n, l1->s, l1->n);
	(void)bufdelline(&e->b, y0 + 1);

	e->cy = y0;
	e->cx = x0;
	e->dirty = 1;
	return 0;
}

/*
 * delinside deletes inside a delimiter pair around the cursor, optionally
 * entering INSERT mode for change operations.
 *
 * Parameters:
 *  - e: editor state.
 *  - op: operator rune (e.g. 'd' or 'c').
 *  - c: delimiter character identifying the pair.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 if no matching pair is found.
 */
static int
delinside(Eek *e, long op, long c)
{
	char open, close;
	long oy, ox;
	long cy, cx;
	long y0, x0;

	if (!delimpair(c, &open, &close))
		return -1;
	if (findopen(e, open, close, &oy, &ox) < 0)
		return -1;
	if (findclosefrom(e, oy, ox, open, close, &cy, &cx) < 0)
		return -1;
	y0 = oy;
	x0 = ox + 1;
	if (delrange(e, y0, x0, cy, cx, 1) < 0)
		return -1;
	if (op == 'c')
		setmode(e, Modeinsert);
	return 0;
}

/*
 * countval converts a parsed numeric prefix to a repeat count.
 *
 * Parameters:
 *  - n: parsed count (may be 0 meaning "no count").
 *
 * Returns:
 *  - n if n > 0, otherwise 1.
 */
static long
countval(long n)
{
	return n > 0 ? n : 1;
}

/*
 * repeat calls a motion/editor function n times.
 *
 * Parameters:
 *  - e: editor state.
 *  - fn: function to invoke.
 *  - n: repetition count.
 *
 * Returns:
 *  - void.
 */
static void
repeat(Eek *e, void (*fn)(Eek *), long n)
{
	long i;

	for (i = 0; i < n; i++)
		fn(e);
}

/*
 * yclear clears the yank register.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
yclear(Eek *e)
{
	free(e->ybuf);
	e->ybuf = nil;
	e->ylen = 0;
	e->yline = 0;
}

/*
 * yset replaces the yank register with the given bytes.
 *
 * Parameters:
 *  - e: editor state.
 *  - s: bytes to copy.
 *  - n: number of bytes.
 *  - linewise: whether the register is linewise.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
yset(Eek *e, const char *s, long n, int linewise)
{
	char *p;

	if (n < 0)
		n = 0;
	p = nil;
	if (n > 0) {
		p = malloc((size_t)n);
		if (p == nil)
			return -1;
		memcpy(p, s, (size_t)n);
	}
	free(e->ybuf);
	e->ybuf = p;
	e->ylen = n;
	e->yline = linewise;
	return 0;
}

/*
 * yappend appends bytes to the yank register.
 *
 * Parameters:
 *  - e: editor state.
 *  - s: bytes to append.
 *  - n: number of bytes to append.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
yappend(Eek *e, const char *s, long n)
{
	char *p;

	if (n <= 0)
		return 0;
	if (e->ybuf == nil || e->ylen == 0)
		return yset(e, s, n, e->yline);
	p = realloc(e->ybuf, (size_t)(e->ylen + n));
	if (p == nil)
		return -1;
	memcpy(p + e->ylen, s, (size_t)n);
	e->ybuf = p;
	e->ylen += n;
	return 0;
}

/*
 * yankrange yanks a range of text into the yank register.
 * The range is interpreted in byte coordinates; multi-line yanks include '\n'
 * separators between lines.
 *
 * Parameters:
 *  - e: editor state.
 *  - y0, x0: one endpoint.
 *  - y1, x1: other endpoint.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
yankrange(Eek *e, long y0, long x0, long y1, long x1)
{
	Line *l;
	long y;
	long start;
	long end;

	if (y1 < y0 || (y1 == y0 && x1 < x0)) {
		long ty, tx;
		ty = y0;
		tx = x0;
		y0 = y1;
		x0 = x1;
		y1 = ty;
		x1 = tx;
	}

	if (y0 < 0)
		y0 = 0;
	if (y1 >= e->b.nline)
		y1 = e->b.nline - 1;
	if (y0 > y1)
		return 0;

	yclear(e);
	e->yline = 0;
	for (y = y0; y <= y1; y++) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			break;
		start = (y == y0) ? x0 : 0;
		end = (y == y1) ? x1 : l->n;
		if (start < 0)
			start = 0;
		if (start > l->n)
			start = l->n;
		if (end < 0)
			end = 0;
		if (end > l->n)
			end = l->n;
		if (end > start) {
			if (yappend(e, l->s + start, end - start) < 0)
				return -1;
		}
		if (y < y1) {
			if (yappend(e, "\n", 1) < 0)
				return -1;
		}
	}
	return 0;
}

/*
 * yanklines yanks n whole lines starting at at into the yank register.
 *
 * Parameters:
 *  - e: editor state.
 *  - at: starting line index.
 *  - n: number of lines.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
yanklines(Eek *e, long at, long n)
{
	long i;
	Line *l;

	yclear(e);
	e->yline = 1;
	for (i = 0; i < n; i++) {
		if (at + i >= e->b.nline)
			break;
		l = bufgetline(&e->b, at + i);
		if (l == nil)
			break;
		if (i > 0) {
			if (yappend(e, "\n", 1) < 0)
				return -1;
		}
		if (l->n > 0 && yappend(e, l->s, l->n) < 0)
			return -1;
	}
	return 0;
}

/*
 * pastecharwise pastes the yank register as characters at the cursor.
 *
 * Parameters:
 *  - e: editor state.
 *  - before: non-zero to paste before cursor (P), 0 to paste after (p).
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
static int
pastecharwise(Eek *e, int before)
{
	long i, j;
	long pos;
	long starty;
	long startx;
	long len;

	if (e->ybuf == nil || e->ylen <= 0)
		return 0;
	starty = e->cy;
	startx = e->cx;
	len = linelen(e, e->cy);
	pos = before ? e->cx : nextutf8(e, e->cy, e->cx);
	if (pos > len)
		pos = len;
	e->cx = pos;

	for (i = 0; i < e->ylen; ) {
		for (j = i; j < e->ylen && e->ybuf[j] != '\n'; j++)
			;
		if (j > i)
			(void)insertbytes(e, e->ybuf + i, j - i);
		if (j < e->ylen && e->ybuf[j] == '\n')
			(void)insertnl(e);
		i = j + 1;
	}

	if (e->cy == starty && e->cx == startx)
		return 0;
	if (e->cx > 0)
		e->cx = prevutf8(e, e->cy, e->cx);
	return 0;
}

/*
 * pastelinewise pastes the yank register as whole lines.
 *
 * Parameters:
 *  - e: editor state.
 *  - before: non-zero to paste above current line (P), 0 to paste below (p).
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
static int
pastelinewise(Eek *e, int before)
{
	long at;
	long i, n;
	long start;
	long end;

	if (e->ybuf == nil || e->ylen <= 0)
		return 0;
	if (!e->yline)
		return pastecharwise(e, before);
	if (undopush(e) < 0)
		return -1;

	at = before ? e->cy : e->cy + 1;
	start = 0;
	n = 0;
	for (i = 0; i <= e->ylen; i++) {
		if (i == e->ylen || e->ybuf[i] == '\n') {
			end = i;
			if (bufinsertline(&e->b, at + n, e->ybuf + start, end - start) < 0)
				return -1;
			n++;
			start = i + 1;
		}
	}
	if (n > 0) {
		e->cy = at;
		e->cx = 0;
	}
	e->dirty = 1;
	return 0;
}

/*
 * ndigits returns the number of decimal digits needed to print n.
 *
 * Parameters:
 *  - n: integer.
 *
 * Returns:
 *  - number of digits (>= 1).
 */
static int
ndigits(long n)
{
	int d;

	if (n < 0)
		n = -n;
	d = 1;
	while (n >= 10) {
		n /= 10;
		d++;
	}
	return d;
}

/*
 * gutterwidth computes the width in columns of the line-number gutter.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - gutter width in terminal columns (0 if disabled or too narrow).
 */
static int
gutterwidth(Eek *e)
{
	int w;

	if (!e->linenumbers && !e->relativenumbers)
		return 0;
	w = ndigits(e->b.nline > 0 ? e->b.nline : 1) + 1;
	if (w >= e->t.col)
		return 0;
	return w;
}

/*
 * setcursorshape updates the terminal cursor shape using DECSCUSR.
 *
 * Parameters:
 *  - e: editor state.
 *  - shape: DECSCUSR shape code.
 *
 * Returns:
 *  - void.
 */
static void
setcursorshape(Eek *e, int shape)
{
	char buf[32];
	int n;

	if (shape <= 0)
		return;
	if (e->cursorshape == shape)
		return;
	n = snprintf(buf, sizeof buf, "\x1b[%d q", shape);
	if (n > 0)
		write(e->t.fdout, buf, (size_t)n);
	e->cursorshape = shape;
}

/*
 * setmode switches the editor's mode and updates cursor shape.
 *
 * Parameters:
 *  - e: editor state.
 *  - mode: one of the Mode* enum values.
 *
 * Returns:
 *  - void.
 */
static void
setmode(Eek *e, int mode)
{
	e->mode = mode;
	switch (mode) {
	case Modenormal:
		setcursorshape(e, Cursornormal);
		break;
	case Modeinsert:
		setcursorshape(e, Cursorinsert);
		break;
	case Modecmd:
		setcursorshape(e, Cursorcmd);
		break;
	case Modevisual:
		setcursorshape(e, Cursornormal);
		break;
	default:
		break;
	}
}

/*
 * normalfixcursor clamps the cursor to a valid location in the current line.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
normalfixcursor(Eek *e)
{
	long len;

	len = linelen(e, e->cy);
	if (len <= 0) {
		e->cx = 0;
		return;
	}
	if (e->cx >= len)
		e->cx = prevutf8(e, e->cy, len);
}

/*
 * clamp clamps v into [lo, hi].
 *
 * Parameters:
 *  - v: value to clamp.
 *  - lo: lower bound.
 *  - hi: upper bound.
 *
 * Returns:
 *  - clamped value.
 */
static long
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
 *
 * Parameters:
 *  - e: editor state.
 *  - y: line index.
 *
 * Returns:
 *  - length in bytes, or 0 if y is out of range.
 */
static long
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
 *
 * Parameters:
 *  - e: editor state.
 *  - y: line index.
 *  - at: byte offset.
 *
 * Returns:
 *  - byte offset of the previous codepoint boundary.
 */
static long
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

/*
 * nextutf8 returns the next UTF-8 codepoint boundary after at.
 *
 * Parameters:
 *  - e: editor state.
 *  - y: line index.
 *  - at: byte offset.
 *
 * Returns:
 *  - byte offset of the next codepoint boundary.
 */
static long
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

/*
 * isword reports whether c is considered a "word" character for word motions.
 *
 * Parameters:
 *  - c: byte/rune value.
 *
 * Returns:
 *  - non-zero if word character, 0 otherwise.
 */
static int
isword(long c)
{
	if (c < 0)
		return 0;
	if (c > 0x7f)
		return 1;
	if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		return 0;
	if (c == '.' || c == ',' || c == ':' || c == ';' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '!')
		return 0;
	return 1;
}

/*
 * isws reports whether c is considered whitespace.
 *
 * Parameters:
 *  - c: byte/rune value.
 *
 * Returns:
 *  - non-zero if whitespace, 0 otherwise.
 */
static int
isws(long c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/*
 * ispunctword reports whether c is considered punctuation for "punctuation word"
 * motions.
 *
 * Parameters:
 *  - c: byte/rune value.
 *
 * Returns:
 *  - non-zero if punctuation, 0 otherwise.
 */
static int
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
 *
 * Parameters:
 *  - e: editor state.
 *  - y: line index.
 *  - at: byte offset.
 *
 * Returns:
 *  - byte value (0..255) on success.
 *  - -1 if y/at is out of range.
 */
static long
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

/*
 * movel moves cursor left by one UTF-8 codepoint.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
movel(Eek *e)
{
	e->cx = prevutf8(e, e->cy, e->cx);
}

/*
 * mover moves cursor right by one UTF-8 codepoint.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
mover(Eek *e)
{
	e->cx = nextutf8(e, e->cy, e->cx);
}

/*
 * moveu moves cursor up one line, clamping to file bounds.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
moveu(Eek *e)
{
	e->cy = clamp(e->cy - 1, 0, e->b.nline - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

/*
 * moved moves cursor down one line, clamping to file bounds.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
moved(Eek *e)
{
	e->cy = clamp(e->cy + 1, 0, e->b.nline - 1);
	e->cx = clamp(e->cx, 0, linelen(e, e->cy));
}

/*
 * movew implements the vi-like 'w' motion using simple word classes.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
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

/*
 * moveb implements the vi-like 'b' motion (backward word).
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
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
 * rxfromcx converts a byte offset (cx) to a render column (rx), expanding tabs.
 *
 * Parameters:
 *  - e: editor state.
 *  - y: line index.
 *  - cx: byte offset within the line.
 *
 * Returns:
 *  - render x position in terminal columns.
 */
static long
rxfromcx(Eek *e, long y, long cx)
{
	Line *l;
	long i, tx;
	unsigned char c;

	l = bufgetline(&e->b, y);
	if (l == nil)
		return 0;
	tx = 0;
	for (i = 0; i < cx && i < l->n; ) {
		c = (unsigned char)l->s[i];
		if (c == '\t') {
			tx += TABSTOP - (tx % TABSTOP);
			i++;
			continue;
		}
		tx++;
		i = nextutf8(e, y, i);
	}
	return tx;
}

/*
 * scroll adjusts e->rowoff so the cursor line is visible.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
scroll(Eek *e)
{
	long textrows;

	textrows = e->t.row - 1;
	if (textrows < 1)
		textrows = 1;

	if (e->cy < e->rowoff)
		e->rowoff = e->cy;
	if (e->cy >= e->rowoff + textrows)
		e->rowoff = e->cy - textrows + 1;
}

/*
 * drawstatus draws the status line (mode, messages, filename, cursor).
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
drawstatus(Eek *e)
{
	char buf[256];
	int n;
	const char *m;

	if (e->mode == Modecmd) {
		char pfx;

		pfx = e->cmdprefix ? e->cmdprefix : ':';
		n = snprintf(buf, sizeof buf, "%c%.*s", pfx, (int)e->cmdn, e->cmd);
	} else {
		m = e->mode == Modeinsert ? "INSERT" : (e->mode == Modevisual ? "VISUAL" : "NORMAL");
		if (e->msg[0] != 0)
			n = snprintf(buf, sizeof buf, " %s  %s ", m, e->msg);
		else
			n = snprintf(buf, sizeof buf, " %s  %s%s  %ld:%ld ", m,
				e->fname ? e->fname : "[No Name]", e->dirty ? " [+]" : "", e->cy + 1, e->cx + 1);
	}
	if (n < 0)
		n = 0;
	write(e->t.fdout, "\x1b[7m", 4);
	write(e->t.fdout, buf, (size_t)n);
	while (n++ < e->t.col)
		write(e->t.fdout, " ", 1);
	write(e->t.fdout, "\x1b[m", 3);
}

/*
 * setmsg formats a status message into e->msg.
 *
 * Parameters:
 *  - e: editor state.
 *  - fmt: printf-style format.
 *  - ...: format arguments.
 *
 * Returns:
 *  - void.
 */
static void
setmsg(Eek *e, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(e->msg, sizeof e->msg, fmt, ap);
	va_end(ap);
}

/*
 * utf8enc encodes a Unicode codepoint into UTF-8 bytes.
 *
 * Parameters:
 *  - r: Unicode codepoint.
 *  - s: output buffer of at least 4 bytes.
 *
 * Returns:
 *  - number of bytes written (1..4).
 */
static long
utf8enc(long r, char *s)
{
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
 * setsyn sets the active syntax language based on e->fname and synenabled.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
setsyn(Eek *e)
{
	e->syntax = Synnone;
	if (!e->synenabled)
		return;
	e->syntax = synlangfromfname(e->fname);
}

/*
 * syninit initializes the syntax scanner state.
 *
 * Parameters:
 *  - s: scanner state.
 *
 * Returns:
 *  - void.
 */
static void
syninit(SynState *s)
{
	s->inblock = 0;
}

/*
 * synscanline advances the syntax scanner state across a line.
 * This is used to know whether later lines start inside a block comment.
 *
 * Parameters:
 *  - l: line to scan.
 *  - s: scanner state (updated).
 *
 * Returns:
 *  - void.
 */
static void
synscanline(Line *l, SynState *s)
{
	long i;
	int instr;
	unsigned char delim;

	if (l == nil || l->n <= 0)
		return;

	instr = 0;
	delim = 0;
	for (i = 0; i < l->n; i++) {
		unsigned char c, n;

		c = (unsigned char)l->s[i];
		n = (i + 1 < l->n) ? (unsigned char)l->s[i + 1] : 0;

		if (s->inblock) {
			if (c == '*' && n == '/') {
				s->inblock = 0;
				i++;
			}
			continue;
		}
		if (instr) {
			if (c == '\\') {
				if (i + 1 < l->n)
					i++;
				continue;
			}
			if (c == delim) {
				instr = 0;
				delim = 0;
			}
			continue;
		}

		if (c == '"' || c == '\'') {
			instr = 1;
			delim = c;
			continue;
		}
		if (c == '/' && n == '/')
			break;
		if (c == '/' && n == '*') {
			s->inblock = 1;
			i++;
			continue;
		}
	}
}

/*
 * synscanuntil advances the syntax scanner from the start of the file up to
 * (but not including) line index upto.
 *
 * Parameters:
 *  - e: editor state.
 *  - upto: line index limit.
 *  - s: scanner state (updated).
 *
 * Returns:
 *  - void.
 */
static void
synscanuntil(Eek *e, long upto, SynState *s)
{
	long y;
	Line *l;

	if (e->syntax != Sync)
		return;
	if (upto <= 0)
		return;
	if (upto > e->b.nline)
		upto = e->b.nline;
	for (y = 0; y < upto; y++) {
		l = bufgetline(&e->b, y);
		synscanline(l, s);
	}
}

/*
 * synesc maps a highlight class (Hl*) to an ANSI SGR escape string.
 *
 * Parameters:
 *  - hl: highlight class.
 *
 * Returns:
 *  - pointer to a NUL-terminated ANSI escape string.
 */
static const char *
synesc(int hl)
{
	switch (hl) {
	case Hlcomment: return SYN_COMMENT;
	case Hlstring: return SYN_STRING;
	case Hlnumber: return SYN_NUMBER;
	case Hlkeyword: return SYN_KEYWORD;
	case Hltype: return SYN_TYPE;
	case Hlpreproc: return SYN_PREPROC;
	case Hlspecial: return SYN_SPECIAL;
	default: return SYN_NORMAL;
	}
}

/*
 * drawattrs writes terminal attributes for a cell: reset, optional inverse,
 * and optional syntax highlight color.
 *
 * Parameters:
 *  - e: editor state (uses output fd).
 *  - inv: non-zero to apply inverse video.
 *  - hl: highlight class.
 *
 * Returns:
 *  - void.
 */
static void
drawattrs(Eek *e, int inv, int hl)
{
	write(e->t.fdout, "\x1b[m", 3);
	if (inv)
		write(e->t.fdout, "\x1b[7m", 4);
	if (hl != Hlnone)
		write(e->t.fdout, synesc(hl), strlen(synesc(hl)));
}

/*
 * cmdclear resets the command/search input buffer.
 *
 * Parameters:
 *  - e: editor state.
 *
 * Returns:
 *  - void.
 */
static void
cmdclear(Eek *e)
{
	e->cmdn = 0;
	memset(e->cmd, 0, sizeof e->cmd);
}

/*
 * searchforward searches for pat starting just after the cursor and moves the
 * cursor to the next match. This performs a simple byte substring search.
 *
 * Parameters:
 *  - e: editor state (cursor updated on success).
 *  - pat: NUL-terminated search pattern.
 *
 * Returns:
 *  - 0 if a match was found (cursor updated).
 *  - -1 if no match was found or on invalid input.
 */
static int
searchforward(Eek *e, const char *pat)
{
	long y;
	long x;
	long startx;
	long patn;
	Line *l;

	if (e == nil || pat == nil)
		return -1;
	patn = (long)strlen(pat);
	if (patn <= 0)
		return -1;

	y = e->cy;
	startx = nextutf8(e, e->cy, e->cx);
	for (; y < e->b.nline; y++) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		x = (y == e->cy) ? startx : 0;
		if (x < 0)
			x = 0;
		if (x > l->n)
			x = l->n;
		for (; x + patn <= l->n; x++) {
			if (memcmp(l->s + x, pat, (size_t)patn) == 0) {
				e->cy = y;
				e->cx = x;
				return 0;
			}
		}
	}

	/* wrapscan: continue at top */
	for (y = 0; y <= e->cy && y < e->b.nline; y++) {
		long lim;

		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		x = 0;
		lim = l->n;
		if (y == e->cy) {
			lim = e->cx;
			if (lim < 0)
				lim = 0;
			if (lim > l->n)
				lim = l->n;
		}
		if (lim < patn)
			continue;
		for (; x + patn <= lim; x++) {
			if (memcmp(l->s + x, pat, (size_t)patn) == 0) {
				e->cy = y;
				e->cx = x;
				return 0;
			}
		}
	}

	return -1;
}

/*
 * searchbackward searches for pat before the cursor and moves the cursor to
 * the previous match. This performs a simple byte substring search.
 *
 * Parameters:
 *  - e: editor state (cursor updated on success).
 *  - pat: NUL-terminated search pattern.
 *
 * Returns:
 *  - 0 if a match was found (cursor updated).
 *  - -1 if no match was found or on invalid input.
 */

static int
searchbackward(Eek *e, const char *pat)
{
	long y;
	long x;
	long startx;
	long patn;
	Line *l;

	if (e == nil || pat == nil)
		return -1;
	patn = (long)strlen(pat);
	if (patn <= 0)
		return -1;

	for (y = e->cy; y >= 0; y--) {
		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		if (y == e->cy) {
			if (e->cx > 0)
				startx = prevutf8(e, e->cy, e->cx);
			else
				startx = l->n;
		} else {
			startx = l->n;
		}
		if (startx > l->n)
			startx = l->n;
		if (l->n < patn)
			continue;
		if (startx > l->n - patn)
			startx = l->n - patn;
		for (x = startx; x >= 0; x--) {
			if (memcmp(l->s + x, pat, (size_t)patn) == 0) {
				e->cy = y;
				e->cx = x;
				return 0;
			}
		}
	}

	/* wrapscan: continue at bottom */
	for (y = e->b.nline - 1; y >= e->cy && y >= 0; y--) {
		long lim;

		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		lim = l->n;
		if (lim < patn)
			continue;
		startx = lim - patn;
		if (y == e->cy) {
			lim = e->cx;
			if (lim < 0)
				lim = 0;
			if (lim > l->n)
				lim = l->n;
			if (lim < patn)
				continue;
			startx = lim - patn;
		}
		for (x = startx; x >= 0; x--) {
			if (memcmp(l->s + x, pat, (size_t)patn) == 0) {
				e->cy = y;
				e->cx = x;
				return 0;
			}
		}
	}

	return -1;
}

/*
 * searchexec executes the current "/" search command in e->cmd.
 *
 * If the current pattern is empty, it repeats the previous search stored in
 * e->lastsearch.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success (cursor moved to match), -1 on failure.
 */
static int
searchexec(Eek *e)
{
	char pat[256];

	if (e == nil)
		return -1;
	memset(pat, 0, sizeof pat);
	memcpy(pat, e->cmd, (size_t)e->cmdn);
	pat[e->cmdn] = 0;

	if (pat[0] == 0) {
		if (e->lastsearch == nil || e->lastsearch[0] == 0) {
			setmsg(e, "No previous search");
			return -1;
		}
		if (searchforward(e, e->lastsearch) < 0) {
			setmsg(e, "Pattern not found: %s", e->lastsearch);
			return -1;
		}
		return 0;
	}

	free(e->lastsearch);
	e->lastsearch = strdup(pat);
	if (e->lastsearch == nil) {
		setmsg(e, "Out of memory");
		return -1;
	}
	if (searchforward(e, pat) < 0) {
		setmsg(e, "Pattern not found: %s", pat);
		return -1;
	}
	return 0;
}

/*
 * readfileinsert reads a file and inserts its contents as lines into the
 * buffer.
 *
 * Each input line has trailing newlines stripped before insertion.
 *
 * Parameters:
 *  e: editor state.
 *  path: path to the file to read.
 *  at: 0-based line index at which to insert.
 *
 * Returns:
 *  Number of lines inserted on success, -1 on failure.
 */
static long
readfileinsert(Eek *e, const char *path, long at)
{
	FILE *fp;
	char *line;
	size_t cap;
	ssize_t n;
	long nins;
	long pos;

	if (e == nil || path == nil || *path == 0)
		return -1;

	fp = fopen(path, "r");
	if (fp == nil)
		return -1;

	line = nil;
	cap = 0;
	nins = 0;
	pos = at;
	while ((n = getline(&line, &cap, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			n--;
		if (bufinsertline(&e->b, pos, line, (long)n) < 0) {
			free(line);
			(void)fclose(fp);
			return -1;
		}
		pos++;
		nins++;
	}
	free(line);
	(void)fclose(fp);
	return nins;
}

/*
 * setopt applies a single :set option token.
 *
 * Parameters:
 *  e: editor state.
 *  opt: option token (e.g. "syntax", "nosyntax", "numbers", "nonumbers",
 *       "relativenumbers", "norelativenumbers").
 *
 * Returns:
 *  0 on success, -1 if the option is unknown.
 */
static int
setopt(Eek *e, const char *opt)
{
	if (opt == nil || *opt == 0)
		return 0;

	if (strcmp(opt, "syntax") == 0 || strcmp(opt, "syn") == 0) {
		e->synenabled = 1;
		setsyn(e);
		return 0;
	}
	if (strcmp(opt, "nosyntax") == 0 || strcmp(opt, "nosyn") == 0) {
		e->synenabled = 0;
		e->syntax = Synnone;
		return 0;
	}

	if (strcmp(opt, "numbers") == 0 || strcmp(opt, "number") == 0 || strcmp(opt, "nu") == 0) {
		e->linenumbers = 1;
		return 0;
	}
	if (strcmp(opt, "nonumbers") == 0 || strcmp(opt, "nonumber") == 0 || strcmp(opt, "nonu") == 0) {
		e->linenumbers = 0;
		return 0;
	}
	if (strcmp(opt, "relativenumbers") == 0 || strcmp(opt, "relativenumber") == 0 || strcmp(opt, "rnu") == 0) {
		e->relativenumbers = 1;
		return 0;
	}
	if (strcmp(opt, "norelativenumbers") == 0 || strcmp(opt, "norelativenumber") == 0 || strcmp(opt, "nornu") == 0) {
		e->relativenumbers = 0;
		return 0;
	}

	setmsg(e, "Unknown option: %s", opt);
	return -1;
}

/*
 * cmdexec executes the current ":" command line in e->cmd.
 *
 * Supported commands include: set, q, w, wq, r/read.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
cmdexec(Eek *e)
{
	char cmd[256];
	char *p, *arg;
	int force;

	memset(cmd, 0, sizeof cmd);
	memcpy(cmd, e->cmd, (size_t)e->cmdn);
	cmd[e->cmdn] = 0;

	p = cmd;
	while (*p == ' ' || *p == '\t')
		p++;
	arg = p;
	while (*arg && *arg != ' ' && *arg != '\t')
		arg++;
	if (*arg) {
		*arg++ = 0;
		while (*arg == ' ' || *arg == '\t')
			arg++;
	}

	force = 0;
	if (arg > p && arg[-1] == '!') {
		arg[-1] = 0;
		force = 1;
	}

	if (strcmp(p, "set") == 0 || strcmp(p, "se") == 0) {
		char *s;
		char *tok;
		int changed;

		if (arg == nil || *arg == 0) {
			setmsg(e, "%s %s %s", e->linenumbers ? "numbers" : "nonumbers",
				e->relativenumbers ? "relativenumbers" : "norelativenumbers",
				e->synenabled ? "syntax" : "nosyntax");
			return 0;
		}

		changed = 0;
		s = arg;
		for (;;) {
			while (*s == ' ' || *s == '\t')
				s++;
			if (*s == 0)
				break;
			tok = s;
			while (*s && *s != ' ' && *s != '\t')
				s++;
			if (*s)
				*s++ = 0;
			if (setopt(e, tok) < 0)
				return -1;
			changed = 1;
		}
		if (changed)
			setmsg(e, "%s %s %s", e->linenumbers ? "numbers" : "nonumbers",
				e->relativenumbers ? "relativenumbers" : "norelativenumbers",
				e->synenabled ? "syntax" : "nosyntax");
		return 0;
	}

	if (strcmp(p, "q") == 0) {
		if (e->dirty && !force) {
			setmsg(e, "No write since last change (add !)");
			return -1;
		}
		e->quit = 1;
		return 0;
	}
	if (strcmp(p, "w") == 0) {
		if (arg && *arg) {
			if (e->ownfname)
				free(e->fname);
			e->fname = strdup(arg);
			e->ownfname = 1;
			setsyn(e);
		}
		if (e->fname == nil || e->fname[0] == 0) {
			setmsg(e, "No file name");
			return -1;
		}
		if (bufsave(&e->b, e->fname) < 0) {
			setmsg(e, "Write failed");
			return -1;
		}
		e->dirty = 0;
		setmsg(e, "Written %s", e->fname);
		return 0;
	}
	if (strcmp(p, "wq") == 0) {
		if (arg > p && arg[-1] == '!')
			arg[-1] = 0;
		if (e->fname == nil || e->fname[0] == 0) {
			setmsg(e, "No file name");
			return -1;
		}
		if (bufsave(&e->b, e->fname) < 0) {
			setmsg(e, "Write failed");
			return -1;
		}
		e->dirty = 0;
		e->quit = 1;
		return 0;
	}

	if (strcmp(p, "r") == 0 || strcmp(p, "read") == 0) {
		long nins;
		long at;

		if (arg == nil || *arg == 0) {
			setmsg(e, "No file name");
			return -1;
		}
		if (undopush(e) < 0) {
			setmsg(e, "Out of memory");
			return -1;
		}
		at = e->cy + 1;
		nins = readfileinsert(e, arg, at);
		if (nins < 0) {
			setmsg(e, "Read failed");
			return -1;
		}
		e->dirty = 1;
		setmsg(e, "%ld lines read", nins);
		return 0;
	}

	setmsg(e, "Not an editor command: %s", p);
	return -1;
}

/*
 * draw redraws the entire editor UI (buffer contents + status line).
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  None.
 */
static void
draw(Eek *e)
{
	long y;
	long filerow;
	long textrows;
	Line *l;
	long i, rx;
	long tx;
	long ni;
	long n;
	int gutter;
	int numw;
	char nbuf[64];

	write(e->t.fdout, "\x1b[?25l", 6);
	write(e->t.fdout, "\x1b[H", 3);
	textrows = e->t.row - 1;
	if (textrows < 1)
		textrows = 1;
	gutter = gutterwidth(e);
	numw = gutter ? gutter - 1 : 0;

	for (y = 0; y < textrows; y++) {
		SynState syn;
		int curinv;
		int curhl;
		int instr;
		unsigned char delim;
		int inlinecomment;
		int preproc;
		int include;
		int inangle;
		int blockendpending;
		long idrem;
		int idhl;
		long numrem;
		long tmp;
		long j;
		filerow = e->rowoff + y;
		termmoveto(&e->t, (int)y, 0);
		write(e->t.fdout, "\x1b[K", 3);
		if (filerow >= e->b.nline) {
			write(e->t.fdout, "~", 1);
			continue;
		}
		if (gutter) {
			long ln;
			int nn;

			ln = 0;
			if (e->relativenumbers) {
				if (filerow == e->cy)
					ln = e->linenumbers ? filerow + 1 : 0;
				else
					ln = filerow > e->cy ? filerow - e->cy : e->cy - filerow;
			} else {
				ln = filerow + 1;
			}
			nn = snprintf(nbuf, sizeof nbuf, "%*ld ", numw, ln);
			if (nn > 0)
				write(e->t.fdout, nbuf, (size_t)nn);
		}
		l = bufgetline(&e->b, filerow);
		if (l == nil || l->n == 0) {
			continue;
		}

		syninit(&syn);
		synscanuntil(e, filerow, &syn);
		curinv = 0;
		curhl = Hlnone;
		instr = 0;
		delim = 0;
		inlinecomment = 0;
		preproc = 0;
		include = 0;
		inangle = 0;
		blockendpending = 0;
		idrem = 0;
		idhl = Hlnone;
		numrem = 0;
		if (e->syntax == Sync) {
			for (tmp = 0; tmp < l->n; tmp++) {
				unsigned char c;
				c = (unsigned char)l->s[tmp];
				if (c == ' ' || c == '\t')
					continue;
				if (c == '#')
					preproc = 1;
				break;
			}
			if (preproc) {
				long p;
				p = 0;
				while (p < l->n && (l->s[p] == ' ' || l->s[p] == '\t'))
					p++;
				if (p < l->n && l->s[p] == '#')
					p++;
				while (p < l->n && (l->s[p] == ' ' || l->s[p] == '\t'))
					p++;
				if (p + 7 <= l->n && memcmp(l->s + p, "include", 7) == 0) {
					unsigned char c;

					c = (p + 7 < l->n) ? (unsigned char)l->s[p + 7] : 0;
					if (c == 0 || c == ' ' || c == '\t')
						include = 1;
				}
			}
		}

		rx = gutter;
		tx = 0;
		{
		for (i = 0; i < l->n && rx < e->t.col; ) {
			int wantinv;
			int wanthl;
			int basehl;
			int openstring;
			int openangle;
			unsigned char c, n1;

			wantinv = invsel(e, filerow, i);
			basehl = preproc ? Hlpreproc : Hlnone;
			wanthl = basehl;
			openstring = 0;
			openangle = 0;
			c = (unsigned char)l->s[i];
			n1 = (i + 1 < l->n) ? (unsigned char)l->s[i + 1] : 0;

			if (e->syntax == Sync) {
				if (inlinecomment || syn.inblock)
					wanthl = Hlcomment;
				else if (instr || inangle)
					wanthl = Hlstring;
				else if (numrem > 0)
					wanthl = Hlnumber;
				else if (idrem > 0) {
					if (idhl != Hlnone)
						wanthl = idhl;
					else
						wanthl = basehl;
				}

				if (!inlinecomment && !syn.inblock && !instr && !inangle) {
					if (c == '/' && n1 == '/') {
						inlinecomment = 1;
						wanthl = Hlcomment;
					}
					if (!inlinecomment && c == '/' && n1 == '*') {
						syn.inblock = 1;
						blockendpending = 0;
						wanthl = Hlcomment;
					}
					if (!inlinecomment && !syn.inblock && (c == '"' || c == '\'')) {
						openstring = 1;
						wanthl = Hlstring;
					}
					if (include && c == '<') {
						openangle = 1;
						wanthl = Hlstring;
					}
					if (!openstring && !openangle && !inlinecomment && !syn.inblock) {
						if ((c >= '0' && c <= '9')) {
							for (j = i; j < l->n; j++) {
								unsigned char d;

								d = (unsigned char)l->s[j];
								if (!((d >= '0' && d <= '9') || d == '.' || d == 'x' || d == 'X' ||
									(d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))
									break;
							}
							numrem = j - i;
							wanthl = Hlnumber;
						}
						if ((c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) &&
							(i == 0 || !isword((unsigned char)l->s[i - 1]))) {
							for (j = i; j < l->n; j++) {
								unsigned char d;

								d = (unsigned char)l->s[j];
								if (!(d == '_' || (d >= 'A' && d <= 'Z') || (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9')))
									break;
							}
							idhl = synwordkind_lang(e->syntax, l->s + i, j - i);
							idrem = j - i;
							if (idhl != Hlnone)
								wanthl = idhl;
							else
								wanthl = basehl;
						}
					}
				}
			}

			if (wantinv != curinv || wanthl != curhl) {
				drawattrs(e, wantinv, wanthl);
				curinv = wantinv;
				curhl = wanthl;
			}

			if (l->s[i] == '\t') {
				long nsp;

				nsp = TABSTOP - (tx % TABSTOP);
				while (nsp-- > 0 && rx < e->t.col) {
					write(e->t.fdout, " ", 1);
					tx++;
					rx++;
				}
				i++;
				if (idrem > 0)
					idrem--;
				if (numrem > 0)
					numrem--;
				continue;
			}
			ni = nextutf8(e, filerow, i);
			n = ni - i;
			if (n <= 0)
				n = 1;
			if (i + n > l->n)
				n = l->n - i;
			write(e->t.fdout, &l->s[i], (size_t)n);
			if (e->syntax == Sync) {
				if (openstring) {
					instr = 1;
					delim = c;
				}
				if (openangle) {
					inangle = 1;
				}
				if (instr && !openstring) {
					if (c == '\\') {
						if (i + 1 < l->n)
							i++;
					} else if (c == delim) {
						instr = 0;
						delim = 0;
					}
				}
				if (inangle && !openangle) {
					if (c == '>')
						inangle = 0;
				}
				if (syn.inblock) {
					if (blockendpending && c == '/') {
						syn.inblock = 0;
						blockendpending = 0;
					} else if (c == '*' && n1 == '/') {
						blockendpending = 1;
					}
				}
				if (idrem > 0)
					idrem--;
				if (numrem > 0)
					numrem--;
			}
			tx++;
			rx++;
			i += n;
		}
		if (curinv || curhl != Hlnone)
			write(e->t.fdout, "\x1b[m", 3);
		}
	}
	termmoveto(&e->t, (int)textrows, 0);
	write(e->t.fdout, "\x1b[K", 3);
	drawstatus(e);

	rx = rxfromcx(e, e->cy, e->cx) + gutter;
	termmoveto(&e->t, (int)(e->cy - e->rowoff), (int)rx);
	write(e->t.fdout, "\x1b[?25h", 6);
}

/*
 * insertbytes inserts raw bytes at the current cursor position.
 *
 * This pushes an undo snapshot (unless one is already pending).
 *
 * Parameters:
 *  e: editor state.
 *  s: bytes to insert.
 *  n: number of bytes to insert.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
insertbytes(Eek *e, const char *s, long n)
{
	Line *l;

	if (undopush(e) < 0)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	if (lineinsert(l, e->cx, s, n) < 0)
		return -1;
	e->cx += n;
	e->dirty = 1;
	return 0;
}

/*
 * insertnl inserts a newline at the cursor by splitting the current line.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
insertnl(Eek *e)
{
	Line *l;
	long tailn;

	if (undopush(e) < 0)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	if (e->cx > l->n)
		e->cx = l->n;

	tailn = l->n - e->cx;
	if (bufinsertline(&e->b, e->cy + 1, l->s + e->cx, tailn) < 0)
		return -1;
	if (tailn > 0 && linedelrange(l, e->cx, tailn) < 0)
		return -1;
	e->cy++;
	e->cx = 0;
	e->dirty = 1;
	return 0;
}

/*
 * delat deletes the UTF-8 codepoint starting at the cursor.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delat(Eek *e)
{
	Line *l;
	long nx;
	long n;

	if (undopush(e) < 0)
		return -1;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	if (e->cx >= l->n)
		return 0;

	nx = nextutf8(e, e->cy, e->cx);
	n = nx - e->cx;
	if (n <= 0)
		return 0;
	if (linedelrange(l, e->cx, n) < 0)
		return -1;
	e->dirty = 1;
	return 0;
}

/*
 * delats deletes n codepoints starting at the cursor.
 *
 * Parameters:
 *  e: editor state.
 *  n: number of codepoints to delete.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delats(Eek *e, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		if (delat(e) < 0)
			return -1;
	}
	return 0;
}

/*
 * delat_yank deletes n codepoints starting at the cursor and yanks them.
 *
 * Parameters:
 *  e: editor state.
 *  n: number of codepoints to delete/yank (defaults to 1 if <= 0).
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delat_yank(Eek *e, long n)
{
	Line *l;
	long i;
	long nx;
	long nb;
	char *tmp;

	if (n <= 0)
		n = 1;
	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	if (e->cx >= l->n)
		return 0;

	yclear(e);
	e->yline = 0;
	for (i = 0; i < n; i++) {
		if (e->cx >= l->n)
			break;
		nx = nextutf8(e, e->cy, e->cx);
		nb = nx - e->cx;
		if (nb <= 0)
			break;
		tmp = l->s + e->cx;
		if (yappend(e, tmp, nb) < 0)
			return -1;
		(void)delat(e);
		l = bufgetline(&e->b, e->cy);
		if (l == nil)
			break;
	}
	return 0;
}

/*
 * delback implements backspace in insert mode.
 *
 * If at column 0, it joins the current line into the previous line.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delback(Eek *e)
{
	Line *l;
	long px;
	long n;
	Line *pl;

	if (undopush(e) < 0)
		return -1;

	if (e->cx == 0) {
		if (e->cy == 0)
			return 0;
		pl = bufgetline(&e->b, e->cy - 1);
		l = bufgetline(&e->b, e->cy);
		if (pl == nil || l == nil)
			return -1;
		px = pl->n;
		if (lineinsert(pl, pl->n, l->s, l->n) < 0)
			return -1;
		(void)bufdelline(&e->b, e->cy);
		e->cy--;
		e->cx = px;
		e->dirty = 1;
		return 0;
	}

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	px = prevutf8(e, e->cy, e->cx);
	n = e->cx - px;
	if (n <= 0)
		return 0;
	if (linedelrange(l, px, n) < 0)
		return -1;
	e->cx = px;
	e->dirty = 1;
	return 0;
}

/*
 * delline deletes the current line.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delline(Eek *e)
{
	if (undopush(e) < 0)
		return -1;
	if (bufdelline(&e->b, e->cy) < 0)
		return -1;
	if (e->cy >= e->b.nline)
		e->cy = e->b.nline - 1;
	e->cx = 0;
	e->dirty = 1;
	return 0;
}

/*
 * dellines deletes n lines starting at the current line.
 *
 * Parameters:
 *  e: editor state.
 *  n: number of lines to delete.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
dellines(Eek *e, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		if (delline(e) < 0)
			return -1;
	}
	return 0;
}

/*
 * wordtarget computes the target position for a "w"-style motion.
 *
 * Parameters:
 *  e: editor state.
 *  ty: output target line.
 *  tx: output target column.
 *
 * Returns:
 *  None.
 */
static void
wordtarget(Eek *e, long *ty, long *tx)
{
	long y, x;
	long c;
	long len;
	int cls;

	y = e->cy;
	x = e->cx;

	len = linelen(e, y);
	if (x >= len) {
		if (y + 1 < e->b.nline) {
			*ty = y + 1;
			*tx = 0;
			return;
		}
		*ty = y;
		*tx = len;
		return;
	}

	c = peekbyte(e, y, x);
	cls = isword(c) ? 1 : (ispunctword(c) ? 2 : 0);
	if (cls == 1) {
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !isword(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
		/* vi: after a word, only skip blanks (not punctuation) */
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !isws(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
	} else if (cls == 2) {
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !ispunctword(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !isws(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
	} else {
		/* on blanks or unknown: skip blanks, then stop at next nonblank */
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !isws(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
	}
	if (x >= len && y + 1 < e->b.nline) {
		y++;
		x = 0;
	}

	*ty = y;
	*tx = x;
}

/*
 * delword deletes from the cursor to the "w" motion target.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delword(Eek *e)
{
	long ty, tx;
	Line *l;
	Line *nl;
	long len;
	long n;

	if (undopush(e) < 0)
		return -1;

	wordtarget(e, &ty, &tx);
	if (ty == e->cy && tx <= e->cx)
		return 0;

	if (ty == e->cy) {
		l = bufgetline(&e->b, e->cy);
		if (l == nil)
			return -1;
		n = tx - e->cx;
		if (n <= 0)
			return 0;
		if (linedelrange(l, e->cx, n) < 0)
			return -1;
		e->dirty = 1;
		return 0;
	}

	/* delete to end of line, then delete newline (join with next) */
	l = bufgetline(&e->b, e->cy);
	nl = bufgetline(&e->b, e->cy + 1);
	if (l == nil || nl == nil)
		return -1;
	len = l->n;
	if (e->cx < len) {
		if (linedelrange(l, e->cx, len - e->cx) < 0)
			return -1;
	}
	if (lineinsert(l, l->n, nl->s, nl->n) < 0)
		return -1;
	(void)bufdelline(&e->b, e->cy + 1);
	e->dirty = 1;
	return 0;
}

/*
 * delwords repeats delword n times.
 *
 * Parameters:
 *  e: editor state.
 *  n: repeat count.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delwords(Eek *e, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		if (delword(e) < 0)
			return -1;
	}
	return 0;
}

/*
 * endwordtarget computes the target position for an "e"-style motion.
 *
 * Parameters:
 *  e: editor state.
 *  ty: output target line.
 *  tx: output target column.
 *
 * Returns:
 *  None.
 */
static void
endwordtarget(Eek *e, long *ty, long *tx)
{
	long y, x;
	long c;
	long len;
	int cls;

	y = e->cy;
	x = e->cx;

	len = linelen(e, y);
	if (x >= len) {
		*ty = y;
		*tx = len;
		return;
	}

	for (;;) {
		c = peekbyte(e, y, x);
		if (c < 0)
			break;
		if (!isws(c))
			break;
		x = nextutf8(e, y, x);
		if (x >= len)
			break;
	}
	c = peekbyte(e, y, x);
	cls = isword(c) ? 1 : (ispunctword(c) ? 2 : 0);
	if (cls == 1) {
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !isword(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
	} else if (cls == 2) {
		for (;;) {
			c = peekbyte(e, y, x);
			if (c < 0 || !ispunctword(c))
				break;
			x = nextutf8(e, y, x);
			if (x >= len)
				break;
		}
	}

	*ty = y;
	*tx = x;
}

/*
 * delendword deletes from the cursor to the end of the current word.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delendword(Eek *e)
{
	long ty, tx;
	Line *l;
	long n;

	if (undopush(e) < 0)
		return -1;

	endwordtarget(e, &ty, &tx);
	if (ty != e->cy)
		return 0;
	if (tx <= e->cx)
		return 0;

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return -1;
	n = tx - e->cx;
	if (linedelrange(l, e->cx, n) < 0)
		return -1;
	e->dirty = 1;
	return 0;
}

/*
 * delendwords repeats delendword n times.
 *
 * Parameters:
 *  e: editor state.
 *  n: repeat count.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
delendwords(Eek *e, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		if (delendword(e) < 0)
			return -1;
	}
	return 0;
}

/*
 * openlinebelow inserts a new empty line below the current line and enters
 * insert mode.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
openlinebelow(Eek *e)
{
	if (undopush(e) < 0)
		return -1;
	if (bufinsertline(&e->b, e->cy + 1, "", 0) < 0)
		return -1;
	e->cy++;
	e->cx = 0;
	e->dirty = 1;
	setmode(e, Modeinsert);
	return 0;
}

/*
 * openlineabove inserts a new empty line above the current line and enters
 * insert mode.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
static int
openlineabove(Eek *e)
{
	if (undopush(e) < 0)
		return -1;
	if (bufinsertline(&e->b, e->cy, "", 0) < 0)
		return -1;
	e->cx = 0;
	e->dirty = 1;
	setmode(e, Modeinsert);
	return 0;
}

/*
 * main starts the editor.
 *
 * Parameters:
 *  argc: argument count.
 *  argv: argument vector; argv[1] (if present) is the file to open.
 *
 * Returns:
 *  Process exit status.
 */
int
main(int argc, char **argv)
{
	Eek e;
	Key k;

	memset(&e, 0, sizeof e);
	bufinit(&e.b);
	e.synenabled = Syntaxhighlight;
	e.cmdprefix = ':';

	if (argc > 1) {
		e.fname = argv[1];
		e.ownfname = 0;
		(void)bufload(&e.b, e.fname);
		setsyn(&e);
	}

	terminit(&e.t);
	setmode(&e, Modenormal);
	cmdclear(&e);
	draw(&e);

	for (;;) {
		if (termresized()) {
			termgetwinsz(&e.t);
			normalfixcursor(&e);
		}
		scroll(&e);
		draw(&e);
		if (e.quit)
			break;
		if (keyread(&e.t, &k) < 0)
			break;
		if (e.mode != Modeinsert)
			e.undopending = 0;
		if (e.msg[0] != 0 && e.mode != Modecmd)
			e.msg[0] = 0;

		if (e.mode == Modecmd) {
			if (k.kind == Keyesc) {
				setmode(&e, Modenormal);
				cmdclear(&e);
				e.cmdprefix = ':';
				continue;
			}
			if (k.kind == Keyenter) {
				if (e.cmdprefix == '/')
					(void)searchexec(&e);
				else
					(void)cmdexec(&e);
				setmode(&e, Modenormal);
				cmdclear(&e);
				e.cmdprefix = ':';
				continue;
			}
			if (k.kind == Keybackspace) {
				if (e.cmdn > 0)
					e.cmdn--;
				continue;
			}
			if (k.kind == Keyrune) {
				if (k.value >= 0x20 && k.value < 0x7f) {
					if (e.cmdn + 1 < (long)sizeof e.cmd)
						e.cmd[e.cmdn++] = (char)k.value;
				}
				continue;
			}
			continue;
		}

		if (e.mode == Modeinsert) {
			if (k.kind == Keyesc) {
				if (e.cx > 0)
					e.cx = prevutf8(&e, e.cy, e.cx);
				setmode(&e, Modenormal);
				e.undopending = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				e.dpending = 0;
				e.cpending = 0;
				e.ypending = 0;
				e.fpending = 0;
				e.fcount = 0;
				e.tipending = 0;
				e.vtipending = 0;
				normalfixcursor(&e);
				continue;
			}
			switch (k.kind) {
			case Keyleft: movel(&e); break;
			case Keyright: mover(&e); break;
			case Keyup: moveu(&e); break;
			case Keydown: moved(&e); break;
			case Keybackspace: (void)delback(&e); break;
			case Keyenter: (void)insertnl(&e); break;
			case Keyrune: {
				char s[8];
				long n;

				if (k.value == '\t') {
					s[0] = '\t';
					(void)insertbytes(&e, s, 1);
					break;
				}
				if (k.value < 0x20)
					break;
				n = utf8enc(k.value, s);
				(void)insertbytes(&e, s, n);
				break;
			}
			default:
				break;
			}
			continue;
		}

		if (k.kind == Keyesc) {
			e.dpending = 0;
			e.cpending = 0;
			e.ypending = 0;
			e.fpending = 0;
			e.fcount = 0;
			e.tipending = 0;
			e.vtipending = 0;
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			e.count = 0;
			e.opcount = 0;
			if (e.mode == Modevisual)
				setmode(&e, Modenormal);
			continue;
		}

		if (e.fpending) {
			e.fpending = 0;
			if (k.kind == Keyrune) {
				long n;

				n = e.fcount;
				e.fcount = 0;
				if (findfwd(&e, k.value, n) < 0)
					setmsg(&e, "Not found: %lc", (long)k.value);
			}
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			e.count = 0;
			e.opcount = 0;
			continue;
		}

		if (e.dpending) {
			if (k.kind == Keyrune && k.value >= '0' && k.value <= '9') {
				if (e.count > 0 || k.value != '0') {
					e.count = e.count * 10 + (k.value - '0');
					continue;
				}
			}

			e.dpending = 0;
			if (k.kind == Keyrune) {
				long total;

				total = countval(e.opcount) * countval(e.count);
				e.opcount = 0;
				e.count = 0;
				switch (k.value) {
				case 'd':
					(void)dellines(&e, total);
					break;
				case 'i':
					e.tipending = 1;
					e.tiop = 'd';
					e.opcount = total;
					continue;
				case 'w':
					(void)delwords(&e, total);
					break;
				case 'e':
					(void)delendwords(&e, total);
					break;
				default:
					setmsg(&e, "Unknown d%lc", (long)k.value);
					break;
				}
			}
			if (e.mode == Modenormal)
				normalfixcursor(&e);
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			continue;
		}

		if (e.tipending) {
			e.tipending = 0;
			if (k.kind == Keyrune) {
				(void)delinside(&e, e.tiop, k.value);
			}
			e.tiop = 0;
			e.opcount = 0;
			e.count = 0;
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			continue;
		}

		if (e.ypending) {
			if (k.kind == Keyrune && k.value >= '0' && k.value <= '9') {
				if (e.count > 0 || k.value != '0') {
					e.count = e.count * 10 + (k.value - '0');
					continue;
				}
			}

			e.ypending = 0;
			if (k.kind == Keyrune) {
				long total;
				long sy, sx;
				long ty, tx;

				total = countval(e.opcount) * countval(e.count);
				e.opcount = 0;
				e.count = 0;
				sy = e.cy;
				sx = e.cx;

				switch (k.value) {
				case 'y':
					(void)yanklines(&e, e.cy, total);
					break;
				case 'w':
					{
						long i;
						long cy, cx;

						cy = sy;
						cx = sx;
						for (i = 0; i < total; i++) {
							e.cy = cy;
							e.cx = cx;
							wordtarget(&e, &ty, &tx);
							if (ty == cy && tx <= cx)
								break;
							cy = ty;
							cx = tx;
						}
						e.cy = sy;
						e.cx = sx;
						(void)yankrange(&e, sy, sx, cy, cx);
					}
					break;
				case 'e':
					endwordtarget(&e, &ty, &tx);
					(void)yankrange(&e, e.cy, e.cx, e.cy, tx);
					break;
				case '$': {
					long len;

					len = linelen(&e, e.cy);
					(void)yankrange(&e, e.cy, e.cx, e.cy, len);
					break;
				}
				default:
					setmsg(&e, "Unknown y%lc", (long)k.value);
					break;
				}
				e.cy = sy;
				e.cx = sx;
			}
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			continue;
		}

		if (e.cpending) {
			if (k.kind == Keyrune && k.value >= '0' && k.value <= '9') {
				if (e.count > 0 || k.value != '0') {
					e.count = e.count * 10 + (k.value - '0');
					continue;
				}
			}

			e.cpending = 0;
			if (k.kind == Keyrune) {
				long total;

				total = countval(e.opcount) * countval(e.count);
				e.opcount = 0;
				e.count = 0;
				switch (k.value) {
				case 'w':
					(void)delwords(&e, total);
					setmode(&e, Modeinsert);
					break;
				case 'i':
					e.tipending = 1;
					e.tiop = 'c';
					e.opcount = total;
					continue;
				default:
					setmsg(&e, "Unknown c%lc", (long)k.value);
					break;
				}
			}
			if (e.mode == Modenormal)
				normalfixcursor(&e);
			e.lastnormalrune = 0;
			e.lastmotioncount = 0;
			e.seqcount = 0;
			continue;
		}

		if (k.kind == Keyup)
			moveu(&e);
		else if (k.kind == Keydown)
			moved(&e);
		else if (k.kind == Keyleft)
			movel(&e);
		else if (k.kind == Keyright)
			mover(&e);
		else if (k.kind == Keyrune) {
			if (e.mode == Modevisual) {
				if (e.vtipending) {
					e.vtipending = 0;
					(void)vselectinside(&e, k.value);
					goto afterkey;
				}
				if (k.value == 'i') {
					e.vtipending = 1;
					goto afterkey;
				}
				if (k.value == 'v') {
					e.vtipending = 0;
					setmode(&e, Modenormal);
					goto afterkey;
				}
				if (k.value == 'y') {
					long sy, sx, ey, ex;

					vselbounds(&e, &sy, &sx, &ey, &ex);
					(void)yankrange(&e, sy, sx, ey, ex);
					setmsg(&e, "yanked");
					e.vtipending = 0;
					setmode(&e, Modenormal);
					goto afterkey;
				}
				if (k.value == 'd') {
					long sy, sx, ey, ex;

					vselbounds(&e, &sy, &sx, &ey, &ex);
					(void)delrange(&e, sy, sx, ey, ex, 1);
					setmsg(&e, "deleted");
					e.vtipending = 0;
					setmode(&e, Modenormal);
					goto afterkey;
				}
			}

			if (k.value >= '0' && k.value <= '9') {
				if (e.count > 0 || k.value != '0') {
					e.count = e.count * 10 + (k.value - '0');
					e.lastnormalrune = 0;
					e.lastmotioncount = 0;
					e.seqcount = 0;
					continue;
				}
			}

			if (e.lastnormalrune == 'g' && k.value == 'g') {
				long line;

				line = e.seqcount > 0 ? e.seqcount - 1 : 0;
				e.cy = clamp(line, 0, e.b.nline - 1);
				e.cx = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				continue;
			}

			switch (k.value) {
			case 'q':
				goto done;
			case 'u':
				undopop(&e);
				e.count = 0;
				e.opcount = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'n':
				if (e.lastsearch == nil || e.lastsearch[0] == 0) {
					setmsg(&e, "No previous search");
					break;
				}
				if (searchforward(&e, e.lastsearch) < 0)
					setmsg(&e, "Pattern not found: %s", e.lastsearch);
				e.count = 0;
				e.opcount = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'N':
				if (e.lastsearch == nil || e.lastsearch[0] == 0) {
					setmsg(&e, "No previous search");
					break;
				}
				if (searchbackward(&e, e.lastsearch) < 0)
					setmsg(&e, "Pattern not found: %s", e.lastsearch);
				e.count = 0;
				e.opcount = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'v':
				if (e.mode == Modevisual) {
					e.dpending = 0;
					e.cpending = 0;
					e.ypending = 0;
					e.fpending = 0;
					e.fcount = 0;
					e.tipending = 0;
					e.vtipending = 0;
					setmode(&e, Modenormal);
				} else {
					e.dpending = 0;
					e.cpending = 0;
					e.ypending = 0;
					e.fpending = 0;
					e.fcount = 0;
					e.tipending = 0;
					e.vay = e.cy;
					e.vax = e.cx;
					e.vtipending = 0;
					setmode(&e, Modevisual);
				}
				e.count = 0;
				e.opcount = 0;
				break;
			case ':':
				setmode(&e, Modecmd);
				cmdclear(&e);
				e.cmdprefix = ':';
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				break;
			case '/':
				setmode(&e, Modecmd);
				cmdclear(&e);
				e.cmdprefix = '/';
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				break;
			case 'd':
				e.opcount = countval(e.count);
				e.count = 0;
				e.dpending = 1;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'c':
				e.opcount = countval(e.count);
				e.count = 0;
				e.cpending = 1;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'y':
				e.opcount = countval(e.count);
				e.count = 0;
				e.ypending = 1;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'f':
				e.fpending = 1;
				e.fcount = countval(e.count);
				e.count = 0;
				e.opcount = 0;
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			case 'p':
				if (e.yline)
					(void)pastelinewise(&e, 0);
				else
					(void)pastecharwise(&e, 0);
				e.count = 0;
				break;
			case 'P':
				if (e.yline)
					(void)pastelinewise(&e, 1);
				else
					(void)pastecharwise(&e, 1);
				e.count = 0;
				break;
			case 'C': {
				Line *l;
				long len;
				long nlines;
				long i;

				nlines = countval(e.count);
				e.count = 0;
				if (undopush(&e) < 0)
					break;
				l = bufgetline(&e.b, e.cy);
				if (l != nil) {
					len = l->n;
					if (e.cx < len)
						(void)yset(&e, l->s + e.cx, len - e.cx, 0);
					else
						yclear(&e);
					if (e.cx < len)
						(void)linedelrange(l, e.cx, len - e.cx);
				}
				for (i = 1; i < nlines; i++) {
					Line *nl;
					char nlsep;

					nl = bufgetline(&e.b, e.cy + 1);
					if (nl == nil)
						break;
					nlsep = '\n';
					(void)yappend(&e, &nlsep, 1);
					if (nl->n > 0)
						(void)yappend(&e, nl->s, nl->n);
					(void)bufdelline(&e.b, e.cy + 1);
				}
				e.dirty = 1;
				setmode(&e, Modeinsert);
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				break;
			}
			case 'a':
				e.cx = nextutf8(&e, e.cy, e.cx);
				setmode(&e, Modeinsert);
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				break;
			case 'A':
				e.cx = linelen(&e, e.cy);
				setmode(&e, Modeinsert);
				e.lastnormalrune = 0;
				e.lastmotioncount = 0;
				e.seqcount = 0;
				e.count = 0;
				e.opcount = 0;
				break;
			case 'o':
				(void)openlinebelow(&e);
				e.lastnormalrune = 0;
				break;
			case 'O':
				(void)openlineabove(&e);
				e.lastnormalrune = 0;
				break;
			case 'h': repeat(&e, movel, countval(e.count)); e.count = 0; break;
			case 'j': repeat(&e, moved, countval(e.count)); e.count = 0; break;
			case 'k': repeat(&e, moveu, countval(e.count)); e.count = 0; break;
			case 'l':
				repeat(&e, mover, countval(e.count));
				e.count = 0;
				break;
			case '0': e.cx = 0; break;
			case '$': e.cx = linelen(&e, e.cy); break;
			case 'w': repeat(&e, movew, countval(e.count)); e.count = 0; break;
			case 'b': repeat(&e, moveb, countval(e.count)); e.count = 0; break;
			case 'i': setmode(&e, Modeinsert); e.count = 0; e.opcount = 0; break;
			case 'x': (void)delat_yank(&e, countval(e.count)); e.count = 0; break;
			case 'G':
				if (e.count > 0)
					e.cy = clamp(e.count - 1, 0, e.b.nline - 1);
				else
					e.cy = e.b.nline - 1;
				e.cx = 0;
				e.count = 0;
				e.opcount = 0;
				break;
			case 'g':
				e.lastnormalrune = 'g';
				e.seqcount = e.count;
				e.count = 0;
				break;
			default:
				break;
			}
			afterkey:
			if (k.value != 'l' && k.value != 'r' && k.value != 'g')
				e.lastnormalrune = 0;
			if (k.value != 'l')
				e.lastmotioncount = 0;
			if (k.value != 'g')
				e.seqcount = 0;
		}
		if (e.mode == Modenormal || e.mode == Modevisual)
			normalfixcursor(&e);
	}

done:
	yclear(&e);
	undofree(&e);
	free(e.lastsearch);
	setcursorshape(&e, Cursornormal);
	termclear(&e.t);
	termmoveto(&e.t, 0, 0);
	termrestore();
	if (e.ownfname)
		free(e.fname);
	buffree(&e.b);

	return 0;
}

/*
 * undopush records an undo snapshot of the current buffer and view state.
 *
 * This is a snapshot-based undo; it deep-copies the buffer. To group insert-mode
 * edits into a single undo step, callers rely on e->undopending.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  0 on success, -1 on allocation failure.
 */
static int
undopush(Eek *e)
{
	Undo *p;
	Undo *u;
	enum { Undomax = 128 };

	if (e == nil)
		return -1;
	if (e->inundo)
		return 0;
	if (e->undopending)
		return 0;

	if (e->nundo >= Undomax) {
		buffree(&e->undo[0].b);
		memmove(&e->undo[0], &e->undo[1], (size_t)(e->nundo - 1) * sizeof e->undo[0]);
		e->nundo--;
	}

	if (e->nundo + 1 > e->capundo) {
		long ncap;

		ncap = e->capundo > 0 ? e->capundo * 2 : 32;
		p = realloc(e->undo, (size_t)ncap * sizeof e->undo[0]);
		if (p == nil)
			return -1;
		e->undo = p;
		e->capundo = ncap;
	}

	u = &e->undo[e->nundo++];
	bufinit(&u->b);
	if (bufcopy(&u->b, &e->b) < 0) {
		buffree(&u->b);
		e->nundo--;
		return -1;
	}
	u->cx = e->cx;
	u->cy = e->cy;
	u->rowoff = e->rowoff;
	u->dirty = e->dirty;
	e->undopending = 1;
	return 0;
}

/*
 * undopop restores the most recent undo snapshot.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  None.
 */
static void
undopop(Eek *e)
{
	Undo u;

	if (e == nil)
		return;
	if (e->nundo <= 0)
		return;

	u = e->undo[--e->nundo];
	e->inundo = 1;
	buffree(&e->b);
	e->b = u.b;
	e->cx = u.cx;
	e->cy = clamp(u.cy, 0, e->b.nline > 0 ? e->b.nline - 1 : 0);
	e->rowoff = clamp(u.rowoff, 0, e->b.nline > 0 ? e->b.nline - 1 : 0);
	e->dirty = u.dirty;
	if (e->mode != Modenormal)
		setmode(e, Modenormal);
	normalfixcursor(e);
	e->undopending = 0;
	e->inundo = 0;
}

/*
 * undofree releases all undo snapshots.
 *
 * Parameters:
 *  e: editor state.
 *
 * Returns:
 *  None.
 */
static void
undofree(Eek *e)
{
	long i;

	if (e == nil)
		return;
	for (i = 0; i < e->nundo; i++)
		buffree(&e->undo[i].b);
	free(e->undo);
	e->undo = nil;
	e->nundo = 0;
	e->capundo = 0;
	e->undopending = 0;
	e->inundo = 0;
}
