
#include <errno.h>
#include <regex.h>
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

typedef struct KeyEvent KeyEvent;
struct KeyEvent {
	Key k;
	int nomap;
	int src;
};

enum {
	Keysrcuser,
	Keysrcmap,
	Keysrcdot,
};

typedef struct Args Args;
struct Args {
	long v[8];
	int n;
	long *heap;
	int heapcap;
};

static void argsinit(Args *a);
static void argsfree(Args *a);
static int argspush(Args *a, long v);
static long argsat(const Args *a, int i, long def);

typedef int (*MoveFn)(Eek *e, Args *a);

typedef struct Move Move;
struct Move {
	unsigned int modes; /* bitmask of Mode* values */
	int kind;           /* Key kind to match (Keyrune, Keyesc, ...) */
	long value;         /* for Keyrune: rune value; -1 matches any rune */
	const char *pattern;/* human-readable pattern (e.g. "d{n}w", "{n}j") */
	MoveFn fn;
};

static int movematch(const Move *m, int mode, const Key *k);
static int movedispatch(Eek *e, int mode, const Move *moves, long nmoves, const Key *k, Args *a);

static int cmdkey(Eek *e, const Key *k);
static int inskey(Eek *e, const Key *k);
static int nvkey(Eek *e, const Key *k);

typedef struct Win Win;
struct Win {
	long cx;        /* Cursor x (byte offset within line). */
	long cy;        /* Cursor y (line index). */
	long rowoff;    /* Topmost visible line (scroll offset). */
	long coloff;    /* Leftmost visible column (render column scroll offset). */
	long vax;       /* VISUAL anchor x (byte offset). */
	long vay;       /* VISUAL anchor y (line index). */
	long vtipending;/* VISUAL pending text-object modifier. */
};

typedef struct Node Node;
struct Node {
	int split; /* 0=leaf, 1=horizontal, 2=vertical */
	Node *a;
	Node *b;
	Win *w;
};

typedef struct Rect Rect;
struct Rect {
	int x;
	int y;
	int w;
	int h;
};

enum {
	Dirleft,
	Dirdown,
	Dirup,
	Dirright,
};

typedef struct Undo Undo;
struct Undo {
	Buf b;       /* Snapshot of the full text buffer. */
	long cx;     /* Cursor x (byte offset within line). */
	long cy;     /* Cursor y (line index). */
	long rowoff; /* Topmost visible line (scroll offset). */
	long coloff; /* Leftmost visible column (render column scroll offset). */
	long dirty;  /* Dirty flag at time of snapshot. */
};

typedef struct Tab Tab;
struct Tab {
	Buf b;         /* Tab-local buffer. */
	char *fname;   /* File name for the tab (may be nil). */
	int ownfname;  /* Whether fname is heap-owned. */
	long dirty;    /* Dirty flag for the tab. */
	int syntax;    /* Tab-local syntax selection (Syn*). */
	Node *layout;  /* Tab-local window layout tree. */
	Win *curwin;   /* Tab-local active window. */
	char *lastsearch; /* Tab-local last search pattern (heap-owned) or nil. */
	Undo *undo;    /* Tab-local undo stack. */
	long nundo;
	long capundo;
	int undopending;
	int inundo;
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
	long coloff;         /* Horizontal scroll offset (leftmost visible render column). */
	long dirty;          /* Non-zero if buffer has unsaved modifications. */
	long dpending;       /* Pending delete operator ('d' has been typed). */
	long cpending;       /* Pending change operator ('c' has been typed). */
	long ypending;       /* Pending yank operator ('y' has been typed). */
	long fpending;       /* Pending find-char motion ('f' has been typed). */
	long fcount;         /* Count for pending find motion (nth occurrence). */
	long fmode;          /* Pending find mode: 'f','F','t','T'. */
	long fop;            /* Pending operator for find: 0,'d','c','y'. */
	long lastfindr;      /* Last successful find target rune. */
	long lastfindmode;   /* Last successful find mode: 'f','F','t','T'. */
	long rpending;       /* Pending replace-char command ('r' waiting for a rune). */
	long rcount;         /* Repeat count for pending replace. */
	long tipending;      /* Pending text-object modifier (e.g. 'di' waiting for delimiter). */
	long tiop;           /* Text-object operator that is pending ('d' or 'c'). */
	long vax;            /* VISUAL anchor x (byte offset). */
	long vay;            /* VISUAL anchor y (line index). */
	long vtipending;     /* VISUAL pending text-object modifier. */
	Node *layout;        /* Window layout tree (leaves are windows). */
	Win *curwin;         /* Active window (mirrored into cx/cy/rowoff/v* fields). */
	char cmd[256];       /* Command-line buffer (for ':' and '/' prompts). */
	long cmdn;           /* Number of bytes used in cmd. */
	char cmdprefix;      /* Prompt prefix character (':' or '/'). */
	int cmdkeepvisual;   /* Non-zero to keep VISUAL selection highlighted while in Modecmd. */
	int cmdrange;        /* Non-zero if command should default to a line range. */
	long cmdy0;          /* Range start line index (0-based, inclusive). */
	long cmdy1;          /* Range end line index (0-based, inclusive). */
	char *lastsearch;    /* Last search pattern (heap-owned) or nil. */
	char msg[256];       /* Status message shown in the status line. */
	long quit;           /* Non-zero requests program exit. */
	Undo *undo;          /* Undo snapshot stack (dynamic array). */
	long nundo;          /* Number of undo entries currently stored. */
	long capundo;        /* Allocated capacity of undo[] in entries. */
	int undopending;     /* Groups multiple edits into a single undo step (e.g. INSERT session). */
	int inundo;          /* Non-zero while restoring undo (prevents recursive snapshotting). */
	Tab *tab;            /* Tabs (inactive tabs stored here). */
	long ntab;           /* Number of tabs (tab slots). */
	long captab;         /* Allocated capacity of tab[] in entries. */
	long curtab;         /* Active tab index (tab[curtab] slot is empty). */
	KeyEvent feed[512];  /* Injected key events (for maps/macros). */
	int feedhead;        /* Index of first valid event in feed[]. */
	int feedlen;         /* Number of valid events in feed[]. */
	KeyEvent dotbuf[512]; /* Last change (NORMAL '.') replay buffer. */
	int dotlen;
	KeyEvent dotrecbuf[512]; /* Recording buffer for in-progress change. */
	int dotreclen;
	int dotrec;
	long dotnundo0;
	int dotreplayleft;
	struct {
		unsigned modes; /* bitmask of Mode* values */
		long lhs;       /* single rune key */
		char *rhs;      /* UTF-8 string to inject */
	} *maps;
	long nmaps;
	long capmaps;
};

static long linelen(Eek *e, long y);
static long clamp(long v, long lo, long hi);
static long prevutf8(Eek *e, long y, long at);
static long nextutf8(Eek *e, long y, long at);
static long countval(long n);
static void setmsg(Eek *e, const char *fmt, ...);
static long utf8enc(long r, char *s);
static void yclear(Eek *e);
static int yset(Eek *e, const char *s, long n, int linewise);
static int yappend(Eek *e, const char *s, long n);
static int yanklines(Eek *e, long at, long n);
static int insertbytes(Eek *e, const char *s, long n);
static int insertnl(Eek *e);
static int delat_yank(Eek *e, long n);
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

static Win *winnewfrom(Eek *e);
static void winload(Eek *e, Win *w);
static void winstore(Eek *e);
static void winclamp(Eek *e, Win *w);
static long curwinrows(Eek *e);
static long nwins(Node *n);
static void collectwins(Node *n, Win **out, long *i);
static int findrect(Node *n, Win *w, Rect r, Rect *out);
static Node *findleaf(Node *n, Win *w, Node **parent, int *isleft);
static int splitcur(Eek *e, int vertical);
static int closecur(Eek *e);
static int nextwin(Eek *e);
static int focusdir(Eek *e, int dir);

static void tabfree(Tab *t);
static Tab tabtake(Eek *e);
static void tabapply(Eek *e, Tab *t);
static int tabsensure(Eek *e, long n);
static int tabinit1(Eek *e);
static int tabnew(Eek *e, const char *path);
static int tabswitch(Eek *e, long idx);
static int tabclose(Eek *e, int force);
static int tabmove(Eek *e, long to);

static int findfwd(Eek *e, long r, long n);
static int findbwd(Eek *e, long r, long n);
static int subexec(Eek *e, char *line);
static void vsellines(Eek *e, long *y0, long *y1);

static int undopush(Eek *e);
static void undofree(Eek *e);
static void undopop(Eek *e);

static void normalfixcursor(Eek *e);

static long utf8dec1(const char *s, long n, long *adv);

static int feedpop(Eek *e, KeyEvent *ev);
static int feedpushfront(Eek *e, const KeyEvent *ev);

static int mapset(Eek *e, unsigned modes, long lhs, const char *rhs);
static int mapdel(Eek *e, unsigned modes, long lhs);
static int mapapply(Eek *e, int mode, long lhs);
static void mapfreeall(Eek *e);

static int dotstartkey(long r);
static void dotrecstart(Eek *e);
static void dotrecsave(Eek *e);
static void dotreccancel(Eek *e);
static void dotrecadd(Eek *e, const KeyEvent *ev);
static int dotrepeat(Eek *e, Args *a);
static int findagain(Eek *e, Args *a);
static int findagainrev(Eek *e, Args *a);
static void replchars(Eek *e, long r, long n);
static int replbegin(Eek *e, Args *a);
static int subchar(Eek *e, Args *a);
static int delend(Eek *e, Args *a);
static int sublinecmd(Eek *e, Args *a);

static void
argsinit(Args *a)
{
	if (a == nil)
		return;
	memset(a, 0, sizeof *a);
}

static long
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

static int
feedpop(Eek *e, KeyEvent *ev)
{
	KeyEvent out;

	if (e == nil || ev == nil)
		return 0;
	if (e->feedlen <= 0)
		return 0;
	out = e->feed[e->feedhead];
	e->feedhead = (e->feedhead + 1) % (int)(sizeof e->feed / sizeof e->feed[0]);
	e->feedlen--;
	*ev = out;
	return 1;
}

static int
feedpushfront(Eek *e, const KeyEvent *ev)
{
	int cap;

	if (e == nil || ev == nil)
		return -1;
	cap = (int)(sizeof e->feed / sizeof e->feed[0]);
	if (e->feedlen >= cap)
		return -1;
	e->feedhead = (e->feedhead - 1 + cap) % cap;
	e->feed[e->feedhead] = *ev;
	e->feedlen++;
	return 0;
}

static int
dotstartkey(long r)
{
	switch (r) {
	case 'v':
	case 'd':
	case 'c':
	case 's':
	case 'r':
	case 'x':
	case 'p':
	case 'P':
	case 'C':
	case 'D':
	case 'S':
	case 'i':
	case 'a':
	case 'A':
	case 'o':
	case 'O':
		return 1;
	default:
		return 0;
	}
}

static void
replchars(Eek *e, long r, long n)
{
	Line *l;
	char s[8];
	long nb;
	long i;
	long x1;
	long len;

	if (e == nil)
		return;
	if (n <= 0)
		return;
	if (undopush(e) < 0)
		return;

	nb = utf8enc(r, s);
	if (nb <= 0)
		return;

	for (i = 0; i < n; i++) {
		l = bufgetline(&e->b, e->cy);
		if (l == nil)
			break;
		len = l->n;
		if (e->cx < 0)
			e->cx = 0;
		if (e->cx >= len)
			break;
		x1 = nextutf8(e, e->cy, e->cx);
		if (x1 <= e->cx)
			break;
		if (linedelrange(l, e->cx, x1 - e->cx) < 0)
			break;
		if (lineinsert(l, e->cx, s, nb) < 0)
			break;
		e->dirty = 1;
		if (i + 1 < n)
			e->cx = nextutf8(e, e->cy, e->cx);
	}
}

static int
replbegin(Eek *e, Args *a)
{
	(void)a;
	if (e == nil)
		return 0;
	e->rpending = 1;
	e->rcount = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
subchar(Eek *e, Args *a)
{
	long n;

	(void)a;
	if (e == nil)
		return 0;
	n = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	(void)delat_yank(e, n);
	setmode(e, Modeinsert);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
delend(Eek *e, Args *a)
{
	Line *l;
	long len;
	long nlines;
	long i;

	(void)a;
	if (e == nil)
		return 0;
	nlines = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	if (undopush(e) < 0)
		return 0;
	l = bufgetline(&e->b, e->cy);
	if (l != nil) {
		len = l->n;
		if (e->cx < len)
			(void)yset(e, l->s + e->cx, len - e->cx, 0);
		else
			yclear(e);
		if (e->cx < len)
			(void)linedelrange(l, e->cx, len - e->cx);
	}
	for (i = 1; i < nlines; i++) {
		Line *nl;
		char nlsep;

		nl = bufgetline(&e->b, e->cy + 1);
		if (nl == nil)
			break;
		nlsep = '\n';
		(void)yappend(e, &nlsep, 1);
		if (nl->n > 0)
			(void)yappend(e, nl->s, nl->n);
		(void)bufdelline(&e->b, e->cy + 1);
	}
	e->dirty = 1;
	if (e->mode == Modenormal)
		normalfixcursor(e);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
sublinecmd(Eek *e, Args *a)
{
	Line *l;
	long n;
	long i;

	(void)a;
	if (e == nil)
		return 0;
	n = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	if (undopush(e) < 0)
		return 0;
	(void)yanklines(e, e->cy, n);

	l = bufgetline(&e->b, e->cy);
	if (l == nil)
		return 0;
	if (l->n > 0)
		(void)linedelrange(l, 0, l->n);
	for (i = 1; i < n; i++) {
		if (e->cy + 1 >= e->b.nline)
			break;
		(void)bufdelline(&e->b, e->cy + 1);
	}
	e->cx = 0;
	e->dirty = 1;
	setmode(e, Modeinsert);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static void
dotrecstart(Eek *e)
{
	if (e == nil)
		return;
	e->dotrec = 1;
	e->dotreclen = 0;
	e->dotnundo0 = e->nundo;
}

static void
dotrecsave(Eek *e)
{
	int i;

	if (e == nil)
		return;
	if (e->nundo <= e->dotnundo0) {
		e->dotrec = 0;
		e->dotreclen = 0;
		return;
	}
	e->dotlen = 0;
	for (i = 0; i < e->dotreclen && i < (int)(sizeof e->dotbuf / sizeof e->dotbuf[0]); i++)
		e->dotbuf[e->dotlen++] = e->dotrecbuf[i];
	e->dotrec = 0;
	e->dotreclen = 0;
}

static void
dotreccancel(Eek *e)
{
	if (e == nil)
		return;
	e->dotrec = 0;
	e->dotreclen = 0;
}

static void
dotrecadd(Eek *e, const KeyEvent *ev)
{
	if (e == nil || ev == nil)
		return;
	if (!e->dotrec)
		return;
	if (e->dotreclen >= (int)(sizeof e->dotrecbuf / sizeof e->dotrecbuf[0])) {
		dotreccancel(e);
		setmsg(e, "dot buffer overflow");
		return;
	}
	e->dotrecbuf[e->dotreclen++] = *ev;
}

static int
mapset(Eek *e, unsigned modes, long lhs, const char *rhs)
{
	long i;
	char *p;

	if (e == nil || rhs == nil)
		return -1;
	if (lhs <= 0)
		return -1;
	if (*rhs == 0)
		return -1;

	for (i = 0; i < e->nmaps; i++) {
		if (e->maps[i].lhs == lhs && e->maps[i].modes == modes) {
			p = strdup(rhs);
			if (p == nil)
				return -1;
			free(e->maps[i].rhs);
			e->maps[i].rhs = p;
			return 0;
		}
	}

	if (e->nmaps + 1 > e->capmaps) {
		long ncap;
		void *np;

		ncap = e->capmaps > 0 ? e->capmaps * 2 : 16;
		np = realloc(e->maps, (size_t)ncap * sizeof e->maps[0]);
		if (np == nil)
			return -1;
		e->maps = np;
		e->capmaps = ncap;
	}

	p = strdup(rhs);
	if (p == nil)
		return -1;
	e->maps[e->nmaps].modes = modes;
	e->maps[e->nmaps].lhs = lhs;
	e->maps[e->nmaps].rhs = p;
	e->nmaps++;
	return 0;
}

static int
mapdel(Eek *e, unsigned modes, long lhs)
{
	long i;

	if (e == nil)
		return -1;
	for (i = 0; i < e->nmaps; i++) {
		if (e->maps[i].lhs == lhs && e->maps[i].modes == modes) {
			free(e->maps[i].rhs);
			memmove(&e->maps[i], &e->maps[i + 1], (size_t)(e->nmaps - i - 1) * sizeof e->maps[0]);
			e->nmaps--;
			return 0;
		}
	}
	return -1;
}

static int
mapapply(Eek *e, int mode, long lhs)
{
	unsigned bit;
	long i;
	const char *s;
	long n;
	long adv;
	long r;
	KeyEvent ev;

	if (e == nil)
		return 0;
	if (mode < 0)
		return 0;
	bit = 1u << (unsigned)mode;
	for (i = 0; i < e->nmaps; i++) {
		if (e->maps[i].lhs != lhs)
			continue;
		if ((e->maps[i].modes & bit) == 0)
			continue;

		/* Inject rhs as non-remappable runes (push front, in reverse). */
		s = e->maps[i].rhs;
		n = (long)strlen(s);
		/* Collect runes first so we can push in reverse order. */
		{
			long cap;
			long *rv;
			long nr;

			cap = 64;
			rv = malloc((size_t)cap * sizeof rv[0]);
			if (rv == nil)
				return 0;
			nr = 0;
			while (n > 0) {
				r = utf8dec1(s, n, &adv);
				if (adv <= 0)
					break;
				if (nr + 1 > cap) {
					long ncap;
					void *np;

					ncap = cap * 2;
					np = realloc(rv, (size_t)ncap * sizeof rv[0]);
					if (np == nil) {
						free(rv);
						return 0;
					}
					rv = np;
					cap = ncap;
				}
				rv[nr++] = r;
				s += adv;
				n -= adv;
			}
			while (nr > 0) {
				nr--;
				ev.k.kind = Keyrune;
				ev.k.value = rv[nr];
				ev.nomap = 1;
				ev.src = Keysrcmap;
				if (feedpushfront(e, &ev) < 0) {
					free(rv);
					setmsg(e, "map feed overflow");
					return 1;
				}
			}
			free(rv);
		}
		return 1;
	}
	return 0;
}

static void
mapfreeall(Eek *e)
{
	long i;

	if (e == nil)
		return;
	for (i = 0; i < e->nmaps; i++)
		free(e->maps[i].rhs);
	free(e->maps);
	e->maps = nil;
	e->nmaps = 0;
	e->capmaps = 0;
}

static void
argsfree(Args *a)
{
	if (a == nil)
		return;
	free(a->heap);
	a->heap = nil;
	a->heapcap = 0;
	a->n = 0;
}

static int
argspush(Args *a, long v)
{
	long *p;
	int cap;

	if (a == nil)
		return -1;
	if (a->n < (int)(sizeof a->v / sizeof a->v[0])) {
		a->v[a->n++] = v;
		return 0;
	}
	cap = a->heapcap > 0 ? a->heapcap : 16;
	while (cap < a->n - (int)(sizeof a->v / sizeof a->v[0]) + 1) {
		if (cap > 1<<20)
			break;
		cap *= 2;
	}
	if (cap < a->n - (int)(sizeof a->v / sizeof a->v[0]) + 1)
		return -1;
	p = realloc(a->heap, (size_t)cap * sizeof a->heap[0]);
	if (p == nil)
		return -1;
	a->heap = p;
	a->heapcap = cap;
	a->heap[a->n - (int)(sizeof a->v / sizeof a->v[0])] = v;
	a->n++;
	return 0;
}

static long
argsat(const Args *a, int i, long def)
{
	int base;
	int hi;

	if (a == nil)
		return def;
	if (i < 0)
		return def;
	if (i >= a->n)
		return def;
	hi = (int)(sizeof a->v / sizeof a->v[0]);
	if (i < hi)
		return a->v[i];
	base = hi;
	if (a->heap == nil)
		return def;
	return a->heap[i - base];
}

static int
movematch(const Move *m, int mode, const Key *k)
{
	if (m == nil || k == nil)
		return 0;
	if ((m->modes & (1u << (unsigned)mode)) == 0)
		return 0;
	if (m->kind != k->kind)
		return 0;
	if (m->kind == Keyrune) {
		if (m->value == -1)
			return 1;
		return m->value == k->value;
	}
	return 1;
}

static int
movedispatch(Eek *e, int mode, const Move *moves, long nmoves, const Key *k, Args *a)
{
	long i;

	if (moves == nil || nmoves <= 0)
		return 0;
	for (i = 0; i < nmoves; i++) {
		if (!movematch(&moves[i], mode, k))
			continue;
		if (moves[i].fn == nil)
			return 1;
		(void)moves[i].fn(e, a);
		return 1;
	}
	return 0;
}

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
 * findbwd moves the cursor to the previous occurrence of rune r on the current
 * line, searching backward.
 */
static int
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

/*
 * bytesfind finds the first occurrence of needle in haystack.
 *
 * Parameters:
 *  hay: haystack bytes.
 *  hayn: length of haystack.
 *  needle: needle bytes.
 *  needlen: length of needle.
 *
 * Returns:
 *  Byte offset of first match, or -1 if not found.
 */
static long
bytesfind(const char *hay, long hayn, const char *needle, long needlen)
{
	long i;

	if (hay == nil || needle == nil)
		return -1;
	if (needlen <= 0)
		return 0;
	if (hayn < needlen)
		return -1;

	for (i = 0; i + needlen <= hayn; i++) {
		if (memcmp(hay + i, needle, (size_t)needlen) == 0)
			return i;
	}
	return -1;
}

/*
 * findlinecontains finds a line whose bytes contain the given substring.
 *
 * The search starts at the current line and wraps.
 *
 * Parameters:
 *  e: editor state.
 *  s: substring bytes.
 *  n: length of substring.
 *
 * Returns:
 *  0-based line index on success, -1 on failure.
 */
static long
findlinecontains(Eek *e, const char *s, long n)
{
	long y;
	Line *l;

	if (e == nil || s == nil)
		return -1;
	if (e->b.nline <= 0)
		return -1;

	for (y = e->cy; y < e->b.nline; y++) {
		l = bufgetline(&e->b, y);
		if (l != nil && bytesfind(l->s, l->n, s, n) >= 0)
			return y;
	}
	for (y = 0; y < e->cy; y++) {
		l = bufgetline(&e->b, y);
		if (l != nil && bytesfind(l->s, l->n, s, n) >= 0)
			return y;
	}
	return -1;
}

/*
 * vsellines computes the selected line range for the current VISUAL selection.
 *
 * Parameters:
 *  e: editor state.
 *  y0: output start line (inclusive).
 *  y1: output end line (inclusive).
 *
 * Returns:
 *  None.
 */
static void
vsellines(Eek *e, long *y0, long *y1)
{
	long a, b;

	if (y0)
		*y0 = 0;
	if (y1)
		*y1 = 0;
	if (e == nil || e->b.nline <= 0)
		return;

	a = e->vay;
	b = e->cy;
	if (a > b) {
		long t;
		t = a;
		a = b;
		b = t;
	}
	if (a < 0)
		a = 0;
	if (b < 0)
		b = 0;
	if (a >= e->b.nline)
		a = e->b.nline - 1;
	if (b >= e->b.nline)
		b = e->b.nline - 1;
	if (y0)
		*y0 = a;
	if (y1)
		*y1 = b;
}

/*
 * parseaddr parses a single ex address from *pp.
 *
 * Supported forms:
 *  .            current line
 *  n            absolute line number
 *  $            last line
 *  /string/     a line containing "string" (search forward with wrap)
 *
 * An address may be followed by one or more +n / -n offsets.
 *
 * Parameters:
 *  e: editor state.
 *  pp: in/out pointer to parse cursor.
 *  out: output 0-based line index.
 *
 * Returns:
 *  1 if an address was parsed, 0 if none is present, -1 on syntax/error.
 */
static int
parseaddr(Eek *e, char **pp, long *out)
{
	char *p;
	long base;

	if (e == nil || pp == nil || *pp == nil || out == nil)
		return -1;
	p = *pp;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == 0)
		return 0;

	base = -1;
	if (*p == '.') {
		base = e->cy;
		p++;
	} else if (*p == '$') {
		base = e->b.nline > 0 ? e->b.nline - 1 : 0;
		p++;
	} else if (*p >= '0' && *p <= '9') {
		char *end;
		long n;

		errno = 0;
		n = strtol(p, &end, 10);
		if (end == p || errno)
			return -1;
		base = n - 1;
		p = end;
	} else if (*p == '/') {
		char *q;
		long n;
		char *pat;
		long y;

		p++;
		q = p;
		while (*q && *q != '/')
			q++;
		if (*q != '/')
			return -1;
		n = (long)(q - p);
		pat = malloc((size_t)n + 1);
		if (pat == nil)
			return -1;
		memcpy(pat, p, (size_t)n);
		pat[n] = 0;
		y = findlinecontains(e, pat, n);
		free(pat);
		if (y < 0)
			return -1;
		base = y;
		p = q + 1;
	} else {
		return 0;
	}

	for (;;) {
		long sign;
		char *end;
		long off;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p != '+' && *p != '-')
			break;
		sign = (*p == '-') ? -1 : 1;
		p++;
		errno = 0;
		off = strtol(p, &end, 10);
		if (end == p || errno)
			return -1;
		base += sign * off;
		p = end;
	}

	if (base < 0)
		base = 0;
	if (base >= e->b.nline)
		base = e->b.nline > 0 ? e->b.nline - 1 : 0;
	*out = base;
	*pp = p;
	return 1;
}

/*
 * subline applies a compiled regex substitution to a single line.
 *
 * Parameters:
 *  re: compiled regex for the pattern.
 *  repl: replacement string.
 *  global: non-zero replaces all matches on the line.
 *  l: line to update.
 *  nsub: output number of substitutions performed on this line.
 *
 * Returns:
 *  0 on success, -1 on allocation failure.
 */
static int
subline(regex_t *re, const char *repl, int global, Line *l, long *nsub)
{
	char *in;
	char *out;
	long outn;
	long outcap;
	regmatch_t m[1];
	long at;
	int rc;

	if (nsub)
		*nsub = 0;
	if (re == nil || repl == nil || l == nil)
		return -1;

	in = malloc((size_t)l->n + 1);
	if (in == nil)
		return -1;
	memcpy(in, l->s, (size_t)l->n);
	in[l->n] = 0;

	out = nil;
	outn = 0;
	outcap = 0;
	at = 0;

	for (;;) {
		rc = regexec(re, in + at, 1, m, 0);
		if (rc != 0)
			break;
		if (m[0].rm_so < 0 || m[0].rm_eo < 0)
			break;
		{
			long so, eo;
			long need;
			long matchlen;
			long repln;

			so = at + (long)m[0].rm_so;
			eo = at + (long)m[0].rm_eo;
			if (so < at)
				so = at;
			if (eo < so)
				eo = so;
			matchlen = eo - so;
			repln = (long)strlen(repl);

			need = outn + (so - at) + repln + (l->n - eo) + 1;
			if (need > outcap) {
				long ncap;
				char *p;

				ncap = outcap > 0 ? outcap * 2 : 64;
				while (ncap < need)
					ncap *= 2;
				p = realloc(out, (size_t)ncap);
				if (p == nil) {
					free(out);
					free(in);
					return -1;
				}
				out = p;
				outcap = ncap;
			}

			memcpy(out + outn, in + at, (size_t)(so - at));
			outn += so - at;
			memcpy(out + outn, repl, (size_t)repln);
			outn += repln;
			if (nsub)
				(*nsub)++;

			if (!global) {
				memcpy(out + outn, in + eo, (size_t)(l->n - eo));
				outn += l->n - eo;
				at = l->n;
				break;
			}

			if (matchlen == 0) {
				if (eo < l->n) {
					out[outn++] = in[eo];
					at = eo + 1;
				} else {
					at = eo;
				}
			} else {
				at = eo;
			}
			if (at >= l->n)
				break;
		}
	}

	if (out == nil) {
		free(in);
		return 0;
	}
	if (at < l->n) {
		long need;
		char *p;

		need = outn + (l->n - at) + 1;
		if (need > outcap) {
			p = realloc(out, (size_t)need);
			if (p == nil) {
				free(out);
				free(in);
				return -1;
			}
			out = p;
			outcap = need;
		}
		memcpy(out + outn, in + at, (size_t)(l->n - at));
		outn += l->n - at;
	}

	if (outn == l->n && memcmp(out, l->s, (size_t)l->n) == 0) {
		free(out);
		free(in);
		if (nsub)
			*nsub = 0;
		return 0;
	}

	free(l->s);
	l->s = out;
	l->n = outn;
	l->cap = outcap;
	free(in);
	return 0;
}

/*
 * subexec executes an ex-style substitute command.
 *
 * Syntax:
 *  :[address]s/old/new/flags
 *  :[addr1],[addr2]s/old/new/flags
 *  :%s/old/new/flags
 *
 * Supported address forms are implemented by parseaddr(). Supported flags:
 *  g  replace all matches on each line.
 *
 * Parameters:
 *  e: editor state.
 *  line: mutable command line (no leading ':').
 *
 * Returns:
 *  1 if the command was recognized (even on error), 0 if not a substitute
 *  command.
 */
static int
subexec(Eek *e, char *line)
{
	char *p;
	long a0, a1;
	int havea0;
	int havea1;
	regex_t re;
	int reok;
	char *old;
	char *new;
	int global;
	long y;
	long nsub;
	long nline;

	if (e == nil || line == nil)
		return 0;

	p = line;
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == 0)
		return 0;

	global = 0;
	a0 = e->cy;
	a1 = e->cy;
	havea0 = 0;
	havea1 = 0;

	if (*p == '%') {
		a0 = 0;
		a1 = e->b.nline > 0 ? e->b.nline - 1 : 0;
		havea0 = havea1 = 1;
		p++;
	} else {
		int r;

		r = parseaddr(e, &p, &a0);
		if (r < 0)
			return 1;
		if (r > 0)
			havea0 = 1;
		a1 = a0;
		if (*p == ',') {
			p++;
			r = parseaddr(e, &p, &a1);
			if (r < 0)
				return 1;
			if (r == 0) {
				a1 = e->b.nline > 0 ? e->b.nline - 1 : 0;
			} else {
				havea1 = 1;
			}
			if (!havea0)
				a0 = e->cy;
		}
	}

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != 's')
		return 0;
	p++;

	/*
	 * If no explicit address/range was provided, allow VISUAL ':' to supply a
	 * default line range (like Vim's :'<,'>).
	 */
	if (!havea0 && !havea1 && e->cmdrange) {
		a0 = e->cmdy0;
		a1 = e->cmdy1;
	}
	if (*p != '/')
		return 0;
	p++;

	old = p;
	while (*p && *p != '/')
		p++;
	if (*p != '/')
		return 1;
	*p++ = 0;

	new = p;
	while (*p && *p != '/')
		p++;
	if (*p != '/')
		return 1;
	*p++ = 0;

	while (*p) {
		if (*p == 'g')
			global = 1;
		p++;
	}

	reok = 0;
	if (regcomp(&re, old, REG_EXTENDED) != 0) {
		setmsg(e, "Bad regex");
		return 1;
	}
	reok = 1;

	if (a0 > a1) {
		long t;
		t = a0;
		a0 = a1;
		a1 = t;
	}
	if (a0 < 0)
		a0 = 0;
	if (a1 >= e->b.nline)
		a1 = e->b.nline > 0 ? e->b.nline - 1 : 0;

	if (undopush(e) < 0) {
		setmsg(e, "Out of memory");
		if (reok)
			regfree(&re);
		return 1;
	}

	nsub = 0;
	nline = 0;
	for (y = a0; y <= a1 && y < e->b.nline; y++) {
		Line *l;
		long nsl;

		l = bufgetline(&e->b, y);
		if (l == nil)
			continue;
		nsl = 0;
		if (subline(&re, new, global, l, &nsl) < 0) {
			setmsg(e, "Out of memory");
			regfree(&re);
			return 1;
		}
		if (nsl > 0) {
			nsub += nsl;
			nline++;
		}
	}
	regfree(&re);

	if (nsub == 0) {
		setmsg(e, "Pattern not found");
		return 1;
	}
	e->dirty = 1;
	setmsg(e, "%ld substitutions on %ld lines", nsub, nline);
	return 1;
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
	if (e->mode != Modevisual && !(e->mode == Modecmd && e->cmdkeepvisual)) {
		*sy = 0;
		*sx = 0;
		*ey = 0;
		*ex = 0;
		return;
	}
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

	if (e->mode != Modevisual && !(e->mode == Modecmd && e->cmdkeepvisual))
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
gutterwidth(Eek *e, int cols)
{
	int w;

	if (!e->linenumbers && !e->relativenumbers)
		return 0;
	w = ndigits(e->b.nline > 0 ? e->b.nline : 1) + 1;
	if (w >= cols)
		return 0;
	return w;
}

/*
 * winnewfrom allocates a window and initializes it from the editor view state.
 */
static Win *
winnewfrom(Eek *e)
{
	Win *w;

	w = malloc(sizeof *w);
	if (w == nil)
		return nil;
	memset(w, 0, sizeof *w);
	w->cx = e->cx;
	w->cy = e->cy;
	w->rowoff = e->rowoff;
	w->coloff = e->coloff;
	w->vax = e->vax;
	w->vay = e->vay;
	w->vtipending = e->vtipending;
	return w;
}

/*
 * winload loads window state into the editor view fields.
 */
static void
winload(Eek *e, Win *w)
{
	if (e == nil || w == nil)
		return;
	e->cx = w->cx;
	e->cy = w->cy;
	e->rowoff = w->rowoff;
	e->coloff = w->coloff;
	e->vax = w->vax;
	e->vay = w->vay;
	e->vtipending = w->vtipending;
}

/*
 * winstore writes the current editor view fields back into the active window.
 */
static void
winstore(Eek *e)
{
	Win *w;

	if (e == nil)
		return;
	w = e->curwin;
	if (w == nil)
		return;
	w->cx = e->cx;
	w->cy = e->cy;
	w->rowoff = e->rowoff;
	w->coloff = e->coloff;
	w->vax = e->vax;
	w->vay = e->vay;
	w->vtipending = e->vtipending;
}

/*
 * winclamp clamps a window cursor/scroll state into valid buffer bounds.
 */
static void
winclamp(Eek *e, Win *w)
{
	long maxy;
	long len;

	if (e == nil || w == nil)
		return;
	maxy = e->b.nline > 0 ? e->b.nline - 1 : 0;
	w->cy = clamp(w->cy, 0, maxy);
	len = linelen(e, w->cy);
	if (w->cx < 0)
		w->cx = 0;
	if (w->cx > len)
		w->cx = len;
	if (w->rowoff < 0)
		w->rowoff = 0;
	if (w->coloff < 0)
		w->coloff = 0;
}

static long
curwinrows(Eek *e)
{
	Rect root;
	Rect cur;
	long textrows;

	if (e == nil)
		return 1;
	textrows = e->t.row - 1;
	if (textrows < 1)
		textrows = 1;
	root = (Rect){ 0, 0, e->t.col, (int)textrows };
	cur = root;
	if (!findrect(e->layout, e->curwin, root, &cur))
		cur = root;
	if (cur.h < 1)
		return 1;
	return cur.h;
}

static Node *
nodeleaf(Win *w)
{
	Node *n;

	n = malloc(sizeof *n);
	if (n == nil)
		return nil;
	memset(n, 0, sizeof *n);
	n->split = 0;
	n->w = w;
	return n;
}

static Node *
nodesplit(int split, Node *a, Node *b)
{
	Node *n;

	n = malloc(sizeof *n);
	if (n == nil)
		return nil;
	memset(n, 0, sizeof *n);
	n->split = split;
	n->a = a;
	n->b = b;
	return n;
}

static Win *
firstwin(Node *n)
{
	if (n == nil)
		return nil;
	if (n->split == 0)
		return n->w;
	if (n->a)
		return firstwin(n->a);
	return firstwin(n->b);
}

static Node *
removeleaf(Node *n, Win *target)
{
	Node *keep;

	if (n == nil)
		return nil;
	if (n->split == 0) {
		if (n->w == target) {
			free(n->w);
			free(n);
			return nil;
		}
		return n;
	}
	n->a = removeleaf(n->a, target);
	n->b = removeleaf(n->b, target);
	if (n->a == nil && n->b == nil) {
		free(n);
		return nil;
	}
	if (n->a == nil) {
		keep = n->b;
		free(n);
		return keep;
	}
	if (n->b == nil) {
		keep = n->a;
		free(n);
		return keep;
	}
	return n;
}

static void
nodefree(Node *n)
{
	if (n == nil)
		return;
	if (n->split == 0) {
		free(n->w);
		free(n);
		return;
	}
	nodefree(n->a);
	nodefree(n->b);
	free(n);
}

static void
tabfree(Tab *t)
{
	long i;

	if (t == nil)
		return;
	for (i = 0; i < t->nundo; i++)
		buffree(&t->undo[i].b);
	free(t->undo);
	t->undo = nil;
	t->nundo = 0;
	t->capundo = 0;
	t->undopending = 0;
	t->inundo = 0;

	free(t->lastsearch);
	t->lastsearch = nil;

	if (t->ownfname)
		free(t->fname);
	t->fname = nil;
	t->ownfname = 0;

	nodefree(t->layout);
	t->layout = nil;
	t->curwin = nil;

	buffree(&t->b);
	memset(&t->b, 0, sizeof t->b);
	t->dirty = 0;
	t->syntax = Synnone;
}

static Tab
tabtake(Eek *e)
{
	Tab t;

	memset(&t, 0, sizeof t);
	if (e == nil)
		return t;
	/* Caller should have called winstore(e) already. */
	t.b = e->b;
	e->b = (Buf){0};
	t.fname = e->fname;
	t.ownfname = e->ownfname;
	e->fname = nil;
	e->ownfname = 0;
	t.dirty = e->dirty;
	e->dirty = 0;
	t.syntax = e->syntax;
	e->syntax = Synnone;
	t.layout = e->layout;
	t.curwin = e->curwin;
	e->layout = nil;
	e->curwin = nil;
	t.lastsearch = e->lastsearch;
	e->lastsearch = nil;
	t.undo = e->undo;
	t.nundo = e->nundo;
	t.capundo = e->capundo;
	t.undopending = e->undopending;
	t.inundo = e->inundo;
	e->undo = nil;
	e->nundo = 0;
	e->capundo = 0;
	e->undopending = 0;
	e->inundo = 0;
	return t;
}

static void
tabapply(Eek *e, Tab *t)
{
	if (e == nil || t == nil)
		return;
	e->b = t->b;
	e->fname = t->fname;
	e->ownfname = t->ownfname;
	e->dirty = t->dirty;
	e->syntax = t->syntax;
	e->layout = t->layout;
	e->curwin = t->curwin;
	e->lastsearch = t->lastsearch;
	e->undo = t->undo;
	e->nundo = t->nundo;
	e->capundo = t->capundo;
	e->undopending = t->undopending;
	e->inundo = t->inundo;
	memset(t, 0, sizeof *t);
}

static int
tabsensure(Eek *e, long n)
{
	Tab *p;
	long cap;

	if (e == nil)
		return -1;
	if (e->captab >= n)
		return 0;
	cap = e->captab > 0 ? e->captab : 4;
	while (cap < n)
		cap *= 2;
	p = realloc(e->tab, (size_t)cap * sizeof e->tab[0]);
	if (p == nil)
		return -1;
	/* Zero new memory. */
	if (cap > e->captab)
		memset(p + e->captab, 0, (size_t)(cap - e->captab) * sizeof e->tab[0]);
	e->tab = p;
	e->captab = cap;
	return 0;
}

static int
tabinit1(Eek *e)
{
	if (e == nil)
		return -1;
	if (tabsensure(e, 1) < 0)
		return -1;
	e->ntab = 1;
	e->curtab = 0;
	memset(&e->tab[0], 0, sizeof e->tab[0]);
	return 0;
}

static int
tabswitch(Eek *e, long idx)
{
	Tab t;
	long max;

	if (e == nil)
		return -1;
	if (idx < 0 || idx >= e->ntab)
		return -1;
	if (idx == e->curtab)
		return 0;

	winstore(e);
	/* Store current tab into its slot. */
	e->tab[e->curtab] = tabtake(e);
	/* Load target tab from its slot and clear it. */
	t = e->tab[idx];
	memset(&e->tab[idx], 0, sizeof e->tab[idx]);
	tabapply(e, &t);
	e->curtab = idx;

	/* Clamp after switching. */
	max = e->b.nline > 0 ? e->b.nline - 1 : 0;
	e->cy = clamp(e->cy, 0, max);
	if (e->layout == nil || e->curwin == nil) {
		Win *w;
		w = malloc(sizeof *w);
		if (w == nil)
			return -1;
		memset(w, 0, sizeof *w);
		e->layout = nodeleaf(w);
		e->curwin = w;
	}
	winload(e, e->curwin);
	if (e->synenabled)
		setsyn(e);
	else
		e->syntax = Synnone;
	normalfixcursor(e);
	return 0;
}

static int
tabnew(Eek *e, const char *path)
{
	long idx;
	Win *w;

	if (e == nil)
		return -1;
	idx = e->ntab;
	if (tabsensure(e, idx + 1) < 0)
		return -1;
	memset(&e->tab[idx], 0, sizeof e->tab[idx]);
	e->ntab++;

	/* Park current tab into its slot. */
	winstore(e);
	e->tab[e->curtab] = tabtake(e);

	/* Build new tab state directly into editor fields. */
	bufinit(&e->b);
	e->fname = nil;
	e->ownfname = 0;
	e->dirty = 0;
	e->syntax = Synnone;
	free(e->lastsearch);
	e->lastsearch = nil;
	undofree(e);

	w = malloc(sizeof *w);
	if (w == nil)
		return -1;
	memset(w, 0, sizeof *w);
	e->layout = nodeleaf(w);
	e->curwin = w;
	winload(e, e->curwin);

	if (path && *path) {
		e->fname = strdup(path);
		if (e->fname == nil)
			return -1;
		e->ownfname = 1;
		(void)bufload(&e->b, e->fname);
		e->dirty = 0;
		if (e->synenabled)
			setsyn(e);
	}

	/* Clear the new tab slot to mark it active. */
	memset(&e->tab[idx], 0, sizeof e->tab[idx]);
	e->curtab = idx;
	return 0;
}

static int
tabclose(Eek *e, int force)
{
	long i;
	Tab cur;
	Tab next;
	long newcur;

	if (e == nil)
		return -1;
	if (e->ntab <= 1)
		return -1;
	if (e->dirty && !force)
		return -1;

	/* Free the active tab state. */
	winstore(e);
	cur = tabtake(e);
	tabfree(&cur);

	/* Remove the empty active slot from the tab array. */
	for (i = e->curtab; i + 1 < e->ntab; i++)
		e->tab[i] = e->tab[i + 1];
	memset(&e->tab[e->ntab - 1], 0, sizeof e->tab[e->ntab - 1]);
	e->ntab--;

	newcur = e->curtab > 0 ? e->curtab - 1 : 0;
	if (newcur >= e->ntab)
		newcur = e->ntab - 1;

	/* Load the new active tab from its slot and clear it. */
	next = e->tab[newcur];
	memset(&e->tab[newcur], 0, sizeof e->tab[newcur]);
	tabapply(e, &next);
	e->curtab = newcur;
	winload(e, e->curwin);
	if (e->synenabled)
		setsyn(e);
	else
		e->syntax = Synnone;
	return 0;
}

static int
tabmove(Eek *e, long to)
{
	Tab cur;
	long from;
	long i;

	if (e == nil)
		return -1;
	if (e->ntab <= 1)
		return 0;
	from = e->curtab;
	if (to < 0)
		to = 0;
	if (to >= e->ntab)
		to = e->ntab - 1;
	if (to == from)
		return 0;

	/* Materialize the active tab into its slot, reorder, then re-activate. */
	winstore(e);
	e->tab[from] = tabtake(e);
	cur = e->tab[from];
	memset(&e->tab[from], 0, sizeof e->tab[from]);

	if (to < from) {
		for (i = from; i > to; i--)
			e->tab[i] = e->tab[i - 1];
		e->tab[to] = cur;
		from = to;
	} else {
		for (i = from; i < to; i++)
			e->tab[i] = e->tab[i + 1];
		e->tab[to] = cur;
		from = to;
	}

	/* Load the moved tab as active. */
	cur = e->tab[from];
	memset(&e->tab[from], 0, sizeof e->tab[from]);
	tabapply(e, &cur);
	e->curtab = from;
	winload(e, e->curwin);
	return 0;
}

static long
nwins(Node *n)
{
	if (n == nil)
		return 0;
	if (n->split == 0)
		return 1;
	return nwins(n->a) + nwins(n->b);
}

static void
collectwins(Node *n, Win **out, long *i)
{
	if (n == nil || out == nil || i == nil)
		return;
	if (n->split == 0) {
		out[(*i)++] = n->w;
		return;
	}
	collectwins(n->a, out, i);
	collectwins(n->b, out, i);
}

static int
findrect(Node *n, Win *w, Rect r, Rect *out)
{
	int sep;
	int aW, bW;
	int aH, bH;
	Rect ra, rb;

	if (n == nil)
		return 0;
	if (n->split == 0) {
		if (n->w == w) {
			if (out)
				*out = r;
			return 1;
		}
		return 0;
	}
	sep = 0;
	if (n->split == 2)
		sep = (r.w >= 3);
	else if (n->split == 1)
		sep = (r.h >= 3);

	if (n->split == 2) {
		aW = (r.w - sep) / 2;
		bW = r.w - sep - aW;
		if (aW < 1) aW = 1;
		if (bW < 1) bW = 1;
		ra = (Rect){ r.x, r.y, aW, r.h };
		rb = (Rect){ r.x + aW + sep, r.y, bW, r.h };
	} else {
		aH = (r.h - sep) / 2;
		bH = r.h - sep - aH;
		if (aH < 1) aH = 1;
		if (bH < 1) bH = 1;
		ra = (Rect){ r.x, r.y, r.w, aH };
		rb = (Rect){ r.x, r.y + aH + sep, r.w, bH };
	}
	if (findrect(n->a, w, ra, out))
		return 1;
	if (findrect(n->b, w, rb, out))
		return 1;
	return 0;
}

static Node *
findleaf(Node *n, Win *w, Node **parent, int *isleft)
{
	Node *res;

	if (n == nil)
		return nil;
	if (n->split == 0)
		return n->w == w ? n : nil;
	if (n->a) {
		if (n->a->split == 0 && n->a->w == w) {
			if (parent) *parent = n;
			if (isleft) *isleft = 1;
			return n->a;
		}
		res = findleaf(n->a, w, parent, isleft);
		if (res)
			return res;
	}
	if (n->b) {
		if (n->b->split == 0 && n->b->w == w) {
			if (parent) *parent = n;
			if (isleft) *isleft = 0;
			return n->b;
		}
		res = findleaf(n->b, w, parent, isleft);
		if (res)
			return res;
	}
	return nil;
}

static int
splitcur(Eek *e, int vertical)
{
	Node *parent;
	Node *leaf;
	Node *a;
	Node *b;
	Node *split;
	Win *nw;
	int isleft;

	if (e == nil || e->layout == nil || e->curwin == nil)
		return -1;

	winstore(e);
	nw = winnewfrom(e);
	if (nw == nil)
		return -1;
	winclamp(e, nw);

	parent = nil;
	isleft = 0;
	leaf = findleaf(e->layout, e->curwin, &parent, &isleft);
	if (leaf == nil) {
		free(nw);
		return -1;
	}

	a = nodeleaf(e->curwin);
	b = nodeleaf(nw);
	if (a == nil || b == nil) {
		if (a) free(a);
		if (b) nodefree(b);
		return -1;
	}
	split = nodesplit(vertical ? 2 : 1, a, b);
	if (split == nil) {
		free(a);
		nodefree(b);
		return -1;
	}

	/* Replace the leaf node with the new split node. */
	if (parent == nil) {
		/* Root leaf. */
		free(leaf);
		e->layout = split;
	} else {
		if (isleft) {
			free(parent->a);
			parent->a = split;
		} else {
			free(parent->b);
			parent->b = split;
		}
	}

	/* Switch focus to the newly created window. */
	e->curwin = nw;
	winload(e, e->curwin);
	return 0;
}

static int
closecur(Eek *e)
{
	Win *w;

	if (e == nil || e->layout == nil || e->curwin == nil)
		return -1;
	if (nwins(e->layout) <= 1)
		return -1;

	winstore(e);
	e->layout = removeleaf(e->layout, e->curwin);
	w = firstwin(e->layout);
	if (w == nil)
		return -1;
	e->curwin = w;
	winload(e, e->curwin);
	return 0;
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
		termwrite(&e->t, buf, n);
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

static int isws(long c);

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
scroll(Eek *e, long textrows)
{
	if (textrows < 1)
		textrows = 1;

	if (e->cy < e->rowoff)
		e->rowoff = e->cy;
	if (e->cy >= e->rowoff + textrows)
		e->rowoff = e->cy - textrows + 1;
}

/*
 * hscroll adjusts e->coloff so the cursor column is visible.
 *
 * textcols is the width (in terminal columns) available for text, excluding
 * the line-number gutter.
 */
static void
hscroll(Eek *e, long textcols)
{
	long rx;

	if (e == nil)
		return;
	if (textcols < 1)
		textcols = 1;
	if (e->coloff < 0)
		e->coloff = 0;

	rx = rxfromcx(e, e->cy, e->cx);
	if (rx < e->coloff)
		e->coloff = rx;
	if (rx >= e->coloff + textcols)
		e->coloff = rx - textcols + 1;
	if (e->coloff < 0)
		e->coloff = 0;
}

static int
nextwin(Eek *e)
{
	long n;
	Win **arr;
	long i;
	long at;

	if (e == nil || e->layout == nil || e->curwin == nil)
		return -1;
	n = nwins(e->layout);
	if (n <= 1)
		return 0;

	winstore(e);
	arr = malloc((size_t)n * sizeof arr[0]);
	if (arr == nil)
		return -1;
	i = 0;
	collectwins(e->layout, arr, &i);
	at = 0;
	for (at = 0; at < i; at++) {
		if (arr[at] == e->curwin)
			break;
	}
	if (at >= i)
		at = 0;
	at = (at + 1) % i;
	e->curwin = arr[at];
	winload(e, e->curwin);
	free(arr);
	return 0;
}

static int
overlap1d(int a0, int a1, int b0, int b1)
{
	int lo;
	int hi;

	if (a0 > a1) { int t = a0; a0 = a1; a1 = t; }
	if (b0 > b1) { int t = b0; b0 = b1; b1 = t; }
	lo = a0 > b0 ? a0 : b0;
	hi = a1 < b1 ? a1 : b1;
	if (hi <= lo)
		return 0;
	return hi - lo;
}

static int
focusdir(Eek *e, int dir)
{
	Rect root;
	Rect cur;
	long textrows;
	long n;
	Win **arr;
	long i;
	Win *best;
	long bestdist;
	int bestov;
	int pass;

	if (e == nil || e->layout == nil || e->curwin == nil)
		return -1;
	if (nwins(e->layout) <= 1)
		return 0;

	textrows = e->t.row - 1;
	if (textrows < 1)
		textrows = 1;
	root = (Rect){ 0, 0, e->t.col, (int)textrows };
	cur = root;
	if (!findrect(e->layout, e->curwin, root, &cur))
		cur = root;

	n = nwins(e->layout);
	arr = malloc((size_t)n * sizeof arr[0]);
	if (arr == nil)
		return -1;
	i = 0;
	collectwins(e->layout, arr, &i);

	best = nil;
	bestdist = 0;
	bestov = -1;

	for (pass = 0; pass < 2; pass++) {
		best = nil;
		bestov = -1;
		for (n = 0; n < i; n++) {
			Rect r;
			long dist;
			int ov;
			int ok;
			int cx0, cx1, cy0, cy1;
			int rx0, rx1, ry0, ry1;

			if (arr[n] == e->curwin)
				continue;
			r = root;
			if (!findrect(e->layout, arr[n], root, &r))
				continue;
			if (r.w <= 0 || r.h <= 0)
				continue;

			cx0 = cur.x;
			cx1 = cur.x + cur.w;
			cy0 = cur.y;
			cy1 = cur.y + cur.h;
			rx0 = r.x;
			rx1 = r.x + r.w;
			ry0 = r.y;
			ry1 = r.y + r.h;

			ok = 0;
			dist = 0;
			ov = 0;
			switch (dir) {
			case Dirleft:
				if (rx1 <= cx0) {
					ok = 1;
					dist = cx0 - rx1;
					ov = overlap1d(cy0, cy1, ry0, ry1);
				}
				break;
			case Dirright:
				if (rx0 >= cx1) {
					ok = 1;
					dist = rx0 - cx1;
					ov = overlap1d(cy0, cy1, ry0, ry1);
				}
				break;
			case Dirup:
				if (ry1 <= cy0) {
					ok = 1;
					dist = cy0 - ry1;
					ov = overlap1d(cx0, cx1, rx0, rx1);
				}
				break;
			case Dirdown:
				if (ry0 >= cy1) {
					ok = 1;
					dist = ry0 - cy1;
					ov = overlap1d(cx0, cx1, rx0, rx1);
				}
				break;
			default:
				break;
			}
			if (!ok)
				continue;
			if (pass == 0 && ov <= 0)
				continue;
			if (best == nil || dist < bestdist || (dist == bestdist && ov > bestov)) {
				best = arr[n];
				bestdist = dist;
				bestov = ov;
			}
		}
		if (best != nil)
			break;
	}

	free(arr);
	if (best == nil)
		return 0;
	winstore(e);
	e->curwin = best;
	winload(e, e->curwin);
	return 0;
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
	char tbuf[64];
	int n;
	const char *m;

	if (e->mode == Modecmd) {
		char pfx;

		pfx = e->cmdprefix ? e->cmdprefix : ':';
		n = snprintf(buf, sizeof buf, "%c%.*s", pfx, (int)e->cmdn, e->cmd);
	} else {
		m = e->mode == Modeinsert ? "INSERT" : (e->mode == Modevisual ? "VISUAL" : "NORMAL");
		if (e->ntab > 1)
			snprintf(tbuf, sizeof tbuf, " tab %ld/%ld", e->curtab + 1, e->ntab);
		else
			tbuf[0] = 0;
		if (e->msg[0] != 0)
			n = snprintf(buf, sizeof buf, " %s  %s%s ", m, e->msg, tbuf);
		else
			n = snprintf(buf, sizeof buf, " %s  %s%s%s  %ld:%ld ", m,
				e->fname ? e->fname : "[No Name]", e->dirty ? " [+]" : "", tbuf, e->cy + 1, e->cx + 1);
	}
	if (n < 0)
		n = 0;
	termwrite(&e->t, "\x1b[7m", 4);
	termwrite(&e->t, buf, n);
	while (n++ < e->t.col)
		termwrite(&e->t, " ", 1);
	termwrite(&e->t, "\x1b[m", 3);
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
	termwrite(&e->t, "\x1b[m", 3);
	if (inv)
		termwrite(&e->t, "\x1b[7m", 4);
	if (hl != Hlnone)
		termwrite(&e->t, synesc(hl), (long)strlen(synesc(hl)));
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
 * runinsert executes a shell command and inserts its stdout at the cursor.
 *
 * If stdout has multiple lines, they are inserted as multiple buffer lines,
 * and any existing text to the right of the cursor is moved to the end of the
 * last inserted line.
 */
static long
runinsert(Eek *e, const char *cmd)
{
	FILE *fp;
	char *line;
	size_t cap;
	ssize_t n;
	long nins;
	Line *l;
	char *tail;
	long tailn;

	if (e == nil || cmd == nil || *cmd == 0)
		return -1;

	/* Ensure there is at least one line to insert into. */
	if (e->b.nline <= 0) {
		if (bufinsertline(&e->b, 0, "", 0) < 0)
			return -1;
		e->cy = 0;
		e->cx = 0;
	}

	fp = popen(cmd, "r");
	if (fp == nil)
		return -1;

	line = nil;
	cap = 0;
	/* Peek first line; if there's no output, do nothing. */
	n = getline(&line, &cap, fp);
	if (n < 0) {
		free(line);
		(void)pclose(fp);
		return 0;
	}

	/* Strip newline(s). */
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		n--;

	/* Save and remove the tail to the right of the cursor. */
	l = bufgetline(&e->b, e->cy);
	if (l == nil) {
		free(line);
		(void)pclose(fp);
		return -1;
	}
	if (e->cx > l->n)
		e->cx = l->n;
	tailn = l->n - e->cx;
	tail = nil;
	if (tailn > 0) {
		tail = malloc((size_t)tailn);
		if (tail == nil) {
			free(line);
			(void)pclose(fp);
			return -1;
		}
		memcpy(tail, l->s + e->cx, (size_t)tailn);
		if (linedelrange(l, e->cx, tailn) < 0) {
			free(tail);
			free(line);
			(void)pclose(fp);
			return -1;
		}
	}

	nins = 0;
	/* Insert first stdout line into the current line at the cursor. */
	if (n > 0) {
		if (lineinsert(l, e->cx, line, (long)n) < 0) {
			free(tail);
			free(line);
			(void)pclose(fp);
			return -1;
		}
		e->cx += (long)n;
	}
	nins++;

	/* Insert remaining stdout lines as new buffer lines. */
	while ((n = getline(&line, &cap, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			n--;
		if (insertnl(e) < 0) {
			free(tail);
			free(line);
			(void)pclose(fp);
			return -1;
		}
		l = bufgetline(&e->b, e->cy);
		if (l == nil) {
			free(tail);
			free(line);
			(void)pclose(fp);
			return -1;
		}
		if (n > 0) {
			if (lineinsert(l, 0, line, (long)n) < 0) {
				free(tail);
				free(line);
				(void)pclose(fp);
				return -1;
			}
			e->cx = (long)n;
		} else {
			e->cx = 0;
		}
		nins++;
	}

	free(line);
	(void)pclose(fp);

	/* Re-attach original tail to the end of the last inserted line. */
	if (tailn > 0 && tail != nil) {
		l = bufgetline(&e->b, e->cy);
		if (l == nil) {
			free(tail);
			return -1;
		}
		if (lineinsert(l, e->cx, tail, tailn) < 0) {
			free(tail);
			return -1;
		}
		/* Keep cursor before the tail (after inserted stdout). */
		free(tail);
	}

	e->dirty = 1;
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
	char *end;
	unsigned mapmodes;

	memset(cmd, 0, sizeof cmd);
	memcpy(cmd, e->cmd, (size_t)e->cmdn);
	cmd[e->cmdn] = 0;

	p = cmd;
	while (*p == ' ' || *p == '\t')
		p++;
	if (subexec(e, p))
		return 0;
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

	mapmodes = (1u << Modenormal) | (1u << Modevisual);

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

	if (strcmp(p, "map") == 0) {
		char *s;
		char *lhs;
		char *rhs;
		long adv;
		long r;
		long n;

		if (arg == nil || *arg == 0) {
			setmsg(e, "Usage: map <lhs> <rhs>");
			return -1;
		}
		s = arg;
		while (*s == ' ' || *s == '\t')
			s++;
		lhs = s;
		while (*s && *s != ' ' && *s != '\t')
			s++;
		if (*s)
			*s++ = 0;
		while (*s == ' ' || *s == '\t')
			s++;
		rhs = s;
		if (lhs == nil || *lhs == 0 || rhs == nil || *rhs == 0) {
			setmsg(e, "Usage: map <lhs> <rhs>");
			return -1;
		}
		n = (long)strlen(lhs);
		r = utf8dec1(lhs, n, &adv);
		if (adv <= 0 || adv != n) {
			setmsg(e, "map lhs must be a single character");
			return -1;
		}
		if (mapset(e, mapmodes, r, rhs) < 0) {
			setmsg(e, "Cannot set map");
			return -1;
		}
		setmsg(e, "mapped");
		return 0;
	}

	if (strcmp(p, "unmap") == 0) {
		char *s;
		char *lhs;
		long adv;
		long r;
		long n;

		if (arg == nil || *arg == 0) {
			setmsg(e, "Usage: unmap <lhs>");
			return -1;
		}
		s = arg;
		while (*s == ' ' || *s == '\t')
			s++;
		lhs = s;
		while (*s && *s != ' ' && *s != '\t')
			s++;
		*s = 0;
		if (lhs == nil || *lhs == 0) {
			setmsg(e, "Usage: unmap <lhs>");
			return -1;
		}
		n = (long)strlen(lhs);
		r = utf8dec1(lhs, n, &adv);
		if (adv <= 0 || adv != n) {
			setmsg(e, "unmap lhs must be a single character");
			return -1;
		}
		if (mapdel(e, mapmodes, r) < 0) {
			setmsg(e, "not mapped");
			return -1;
		}
		setmsg(e, "unmapped");
		return 0;
	}

	if (strcmp(p, "q") == 0) {
		if (nwins(e->layout) > 1) {
			if (closecur(e) < 0) {
				setmsg(e, "Cannot close window");
				return -1;
			}
			return 0;
		}
		if (e->ntab > 1) {
			if (tabclose(e, force) < 0) {
				if (e->dirty && !force)
					setmsg(e, "No write since last change (add !)");
				else
					setmsg(e, "Cannot close tab");
				return -1;
			}
			return 0;
		}
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
		if (e->fname == nil || e->fname[0] == 0) {
			setmsg(e, "No file name");
			return -1;
		}
		if (bufsave(&e->b, e->fname) < 0) {
			setmsg(e, "Write failed");
			return -1;
		}
		e->dirty = 0;
		if (nwins(e->layout) > 1) {
			if (closecur(e) < 0) {
				setmsg(e, "Cannot close window");
				return -1;
			}
			return 0;
		}
		if (e->ntab > 1) {
			if (tabclose(e, 0) < 0) {
				setmsg(e, "Cannot close tab");
				return -1;
			}
			return 0;
		}
		e->quit = 1;
		return 0;
	}

	if (strcmp(p, "tabnew") == 0) {
		if (tabnew(e, (arg && *arg) ? arg : nil) < 0) {
			setmsg(e, "Cannot open tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabn") == 0 || strcmp(p, "tabnext") == 0) {
		long idx;

		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		idx = -1;
		if (arg && *arg) {
			idx = strtol(arg, &end, 10);
			if (end == nil || *end != 0) {
				setmsg(e, "Bad tab index");
				return -1;
			}
			idx = idx - 1;
		} else {
			idx = (e->curtab + 1) % e->ntab;
		}
		if (idx < 0)
			idx = 0;
		if (idx >= e->ntab)
			idx = e->ntab - 1;
		if (tabswitch(e, idx) < 0) {
			setmsg(e, "Cannot switch tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabp") == 0 || strcmp(p, "tabprevious") == 0) {
		long idx;

		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		idx = -1;
		if (arg && *arg) {
			idx = strtol(arg, &end, 10);
			if (end == nil || *end != 0) {
				setmsg(e, "Bad tab index");
				return -1;
			}
			idx = idx - 1;
		} else {
			idx = e->curtab - 1;
			if (idx < 0)
				idx = e->ntab - 1;
		}
		if (idx < 0)
			idx = 0;
		if (idx >= e->ntab)
			idx = e->ntab - 1;
		if (tabswitch(e, idx) < 0) {
			setmsg(e, "Cannot switch tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabfirst") == 0) {
		if (tabswitch(e, 0) < 0) {
			setmsg(e, "Cannot switch tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tablast") == 0) {
		if (tabswitch(e, e->ntab - 1) < 0) {
			setmsg(e, "Cannot switch tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabclose") == 0 || strcmp(p, "tabc") == 0) {
		if (tabclose(e, force) < 0) {
			if (e->dirty && !force)
				setmsg(e, "No write since last change (add !)");
			else
				setmsg(e, "Cannot close tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabm") == 0 || strcmp(p, "tabmove") == 0) {
		long to;

		if (arg == nil || *arg == 0) {
			setmsg(e, "No tab index");
			return -1;
		}
		to = strtol(arg, &end, 10);
		if (end == nil || *end != 0) {
			setmsg(e, "Bad tab index");
			return -1;
		}
		to = to - 1;
		if (tabmove(e, to) < 0) {
			setmsg(e, "Cannot move tab");
			return -1;
		}
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabs") == 0) {
		char out[256];
		long i;
		long n;

		memset(out, 0, sizeof out);
		n = 0;
		for (i = 0; i < e->ntab; i++) {
			const char *name;
			char mark;
			int w;

			mark = (i == e->curtab) ? '*' : ' ';
			if (i == e->curtab)
				name = (e->fname && e->fname[0]) ? e->fname : "[No Name]";
			else
				name = (e->tab[i].fname && e->tab[i].fname[0]) ? e->tab[i].fname : "[No Name]";
			w = snprintf(out + n, sizeof out - (size_t)n, "%c%ld:%s ", mark, i + 1, name);
			if (w < 0 || w >= (int)(sizeof out - (size_t)n))
				break;
			n += w;
		}
		setmsg(e, "%s", out);
		return 0;
	}

	if (strcmp(p, "split") == 0) {
		if (splitcur(e, 0) < 0) {
			setmsg(e, "Cannot split");
			return -1;
		}
		return 0;
	}
	if (strcmp(p, "vsplit") == 0) {
		if (e->ntab > 1) {
			if (tabclose(e, force) < 0) {
				if (e->dirty && !force)
					setmsg(e, "No write since last change (add !)");
				else
					setmsg(e, "Cannot close tab");
				return -1;
			}
			return 0;
		}
		if (splitcur(e, 1) < 0) {
			setmsg(e, "Cannot vsplit");
			return -1;
		}
		return 0;
	}

	if (strcmp(p, "e") == 0 || strcmp(p, "edit") == 0) {
		int exists;
		long n;
		Win **arr;
		long i;

		if (arg == nil || *arg == 0) {
			setmsg(e, "No file name");
			return -1;
		}
		if (e->dirty && !force) {
			setmsg(e, "No write since last change (add !)");
			return -1;
		}

		/* Replace buffer contents; if missing, start a new empty buffer. */
		exists = access(arg, F_OK) == 0;
		if (exists) {
			if (bufload(&e->b, arg) < 0) {
				setmsg(e, "Open failed");
				return -1;
			}
		} else {
			buffree(&e->b);
			bufinit(&e->b);
		}

		/* Update file name. */
		if (e->ownfname)
			free(e->fname);
		e->fname = strdup(arg);
		if (e->fname == nil) {
			setmsg(e, "Out of memory");
			return -1;
		}
		e->ownfname = 1;
		e->dirty = 0;
		undofree(e);

		/* Reset all window/view state for the new buffer. */
		e->cx = 0;
		e->cy = 0;
		e->rowoff = 0;
		e->vax = 0;
		e->vay = 0;
		e->vtipending = 0;
		e->tipending = 0;
		e->dpending = 0;
		e->cpending = 0;
		e->ypending = 0;
		e->fpending = 0;
		e->fcount = 0;
		e->count = 0;
		e->opcount = 0;
		e->seqcount = 0;
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		free(e->lastsearch);
		e->lastsearch = nil;

		n = nwins(e->layout);
		arr = n > 0 ? malloc((size_t)n * sizeof arr[0]) : nil;
		if (arr != nil) {
			i = 0;
			collectwins(e->layout, arr, &i);
			while (i-- > 0) {
				arr[i]->cx = 0;
				arr[i]->cy = 0;
				arr[i]->rowoff = 0;
				arr[i]->vax = 0;
				arr[i]->vay = 0;
				arr[i]->vtipending = 0;
			}
			free(arr);
		}
		winload(e, e->curwin);
		if (e->synenabled)
			setsyn(e);
		else
			e->syntax = Synnone;
		normalfixcursor(e);
		return 0;
	}
	if (strcmp(p, "tabnew") == 0) {
		if (tabnew(e, (arg && *arg) ? arg : nil) < 0) {
			setmsg(e, "Cannot open tab");
			return -1;
		}
		return 0;
	}
	if (strcmp(p, "tabn") == 0 || strcmp(p, "tabnext") == 0) {
		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		if (tabswitch(e, (e->curtab + 1) % e->ntab) < 0)
			return -1;
		return 0;
	}
	if (strcmp(p, "tabp") == 0 || strcmp(p, "tabprevious") == 0) {
		long idx;

		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		idx = e->curtab - 1;
		if (idx < 0)
			idx = e->ntab - 1;
		if (tabswitch(e, idx) < 0)
			return -1;
		return 0;
	}
	if (strcmp(p, "tabfirst") == 0) {
		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		if (tabswitch(e, 0) < 0)
			return -1;
		return 0;
	}
	if (strcmp(p, "tablast") == 0) {
		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			return 0;
		}
		if (tabswitch(e, e->ntab - 1) < 0)
			return -1;
		return 0;
	}
	if (strcmp(p, "tabs") == 0) {
		setmsg(e, "tab %ld/%ld", e->curtab + 1, e->ntab);
		return 0;
	}
	if (strcmp(p, "tabclose") == 0 || strcmp(p, "tabc") == 0) {
		if (tabclose(e, force) < 0) {
			if (e->ntab <= 1)
				setmsg(e, "Cannot close tab");
			else if (e->dirty && !force)
				setmsg(e, "No write since last change (add !)");
			else
				setmsg(e, "Cannot close tab");
			return -1;
		}
		return 0;
	}
	if (strcmp(p, "tabm") == 0 || strcmp(p, "tabmove") == 0) {
		long n;

		if (arg == nil || *arg == 0) {
			setmsg(e, "tabmove: missing index");
			return -1;
		}
		n = strtol(arg, nil, 10);
		if (n <= 0)
			n = 1;
		if (tabmove(e, n - 1) < 0)
			return -1;
		return 0;
	}
	if (strcmp(p, "tabonly") == 0 || strcmp(p, "tabo") == 0) {
		long i;
		long ndirty;

		if (e->ntab <= 1)
			return 0;
		ndirty = 0;
		for (i = 0; i < e->ntab; i++) {
			if (i == e->curtab)
				continue;
			if (e->tab[i].dirty)
				ndirty++;
		}
		if (ndirty > 0 && !force) {
			setmsg(e, "Other tab(s) modified (add !)");
			return -1;
		}
		for (i = 0; i < e->ntab; i++) {
			if (i == e->curtab)
				continue;
			tabfree(&e->tab[i]);
		}
		free(e->tab);
		e->tab = nil;
		e->ntab = 0;
		e->captab = 0;
		e->curtab = 0;
		if (tabinit1(e) < 0)
			return -1;
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

	if (strcmp(p, "run") == 0) {
		long nins;

		if (arg == nil || *arg == 0) {
			setmsg(e, "run: missing command");
			return -1;
		}
		if (undopush(e) < 0) {
			setmsg(e, "Out of memory");
			return -1;
		}
		nins = runinsert(e, arg);
		if (nins < 0) {
			setmsg(e, "Run failed");
			return -1;
		}
		setmsg(e, "%ld lines inserted", nins);
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
	Line *l;
	long i, rx;
	long tx;
	long coloff;
	long ni;
	long n;
	int gutter;
	int numw;
	char nbuf[64];
	Rect root;
	Rect cur;
	long textrows;

	if (e == nil)
		return;

	termwrite(&e->t, "\x1b[?25l", 6);
	textrows = e->t.row - 1;
	if (textrows < 1)
		textrows = 1;
	root = (Rect){ 0, 0, e->t.col, (int)textrows };

	/* Ensure the active window state is up to date before drawing. */
	winstore(e);

	/* Draw layout recursively. */
	{
		Node *stack[256];
		Rect rstack[256];
		int sp;

		sp = 0;
		stack[sp] = e->layout;
		rstack[sp] = root;
		sp++;
		while (sp-- > 0) {
			Node *nd;
			Rect rr;
			int sep;
			int aW, bW;
			int aH, bH;
			Rect ra, rb;

			nd = stack[sp];
			rr = rstack[sp];
			if (nd == nil)
				continue;
			if (nd->split == 0) {
				winclamp(e, nd->w);
				winload(e, nd->w);
				gutter = gutterwidth(e, rr.w);
				numw = gutter ? gutter - 1 : 0;
				for (y = 0; y < rr.h; y++) {
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
					int collim;

					filerow = e->rowoff + y;
					termmoveto(&e->t, rr.y + (int)y, rr.x);
					collim = rr.w;
					rx = 0;
					if (filerow >= e->b.nline) {
						if (collim > 0) {
							termwrite(&e->t, "~", 1);
							rx = 1;
						}
						while (rx++ < collim)
							termwrite(&e->t, " ", 1);
						continue;
					}
					if (gutter && collim > 0) {
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
						if (nn > 0) {
							if (nn > collim)
								nn = collim;
							termwrite(&e->t, nbuf, nn);
							rx += nn;
						}
					}
					l = bufgetline(&e->b, filerow);
					if (l == nil || l->n == 0) {
						while (rx++ < collim)
							termwrite(&e->t, " ", 1);
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

					coloff = e->coloff;
					if (coloff < 0)
						coloff = 0;
					rx = gutter;
					tx = 0;
					for (i = 0; i < l->n && rx < collim; ) {
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
							while (nsp-- > 0 && rx < collim) {
								if (tx >= coloff && rx < collim) {
									termwrite(&e->t, " ", 1);
									rx++;
								}
								tx++;
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
						if (tx >= coloff && rx < collim) {
							termwrite(&e->t, &l->s[i], n);
							rx++;
						}
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
						i += n;
					}
					if (curinv || curhl != Hlnone)
						termwrite(&e->t, "\x1b[m", 3);
					while (rx++ < collim)
						termwrite(&e->t, " ", 1);
				}
				continue;
			}

			/* Internal node: compute child rects and push children. */
			sep = 0;
			if (nd->split == 2)
				sep = (rr.w >= 3);
			else if (nd->split == 1)
				sep = (rr.h >= 3);
			if (nd->split == 2) {
				aW = (rr.w - sep) / 2;
				bW = rr.w - sep - aW;
				if (aW < 1) aW = 1;
				if (bW < 1) bW = 1;
				ra = (Rect){ rr.x, rr.y, aW, rr.h };
				rb = (Rect){ rr.x + aW + sep, rr.y, bW, rr.h };
				if (sep) {
					int yy;
					for (yy = 0; yy < rr.h; yy++) {
						termmoveto(&e->t, rr.y + yy, rr.x + aW);
						termwrite(&e->t, "|", 1);
					}
				}
			} else {
				aH = (rr.h - sep) / 2;
				bH = rr.h - sep - aH;
				if (aH < 1) aH = 1;
				if (bH < 1) bH = 1;
				ra = (Rect){ rr.x, rr.y, rr.w, aH };
				rb = (Rect){ rr.x, rr.y + aH + sep, rr.w, bH };
				if (sep) {
					int xx;
					termmoveto(&e->t, rr.y + aH, rr.x);
					for (xx = 0; xx < rr.w; xx++)
						termwrite(&e->t, "-", 1);
				}
			}

			/* Draw children (push b then a so a is processed first). */
			if (nd->b && sp + 1 < (int)(sizeof stack / sizeof stack[0])) {
				stack[sp] = nd->b;
				rstack[sp] = rb;
				sp++;
			}
			if (nd->a && sp + 1 < (int)(sizeof stack / sizeof stack[0])) {
				stack[sp] = nd->a;
				rstack[sp] = ra;
				sp++;
			}
		}
	}

	/* Restore active view state for status line and cursor placement. */
	winload(e, e->curwin);
	termmoveto(&e->t, (int)textrows, 0);
	termwrite(&e->t, "\x1b[K", 3);
	drawstatus(e);

	cur = root;
	if (findrect(e->layout, e->curwin, root, &cur)) {
		long cyrel;
		long cxcol;
		int cxabs;
		int cyabs;

		gutter = gutterwidth(e, cur.w);
		cyrel = e->cy - e->rowoff;
		if (cyrel < 0)
			cyrel = 0;
		if (cyrel >= cur.h)
			cyrel = cur.h - 1;
		cxcol = (rxfromcx(e, e->cy, e->cx) - e->coloff) + gutter;
		if (cxcol < 0)
			cxcol = 0;
		if (cxcol >= cur.w)
			cxcol = cur.w > 0 ? cur.w - 1 : 0;
		cxabs = cur.x + (int)cxcol;
		cyabs = cur.y + (int)cyrel;
		termmoveto(&e->t, cyabs, cxabs);
	}
	termwrite(&e->t, "\x1b[?25h", 6);
	termflush(&e->t);
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

static int
cmdesc(Eek *e, Args *a)
{
	(void)a;
	if (e == nil)
		return 0;
	if (e->cmdkeepvisual)
		setmode(e, Modevisual);
	else
		setmode(e, Modenormal);
	e->cmdrange = 0;
	e->cmdkeepvisual = 0;
	cmdclear(e);
	e->cmdprefix = ':';
	return 0;
}

static int
cmdenter(Eek *e, Args *a)
{
	(void)a;
	if (e == nil)
		return 0;
	if (e->cmdprefix == '/')
		(void)searchexec(e);
	else
		(void)cmdexec(e);
	e->cmdrange = 0;
	e->cmdkeepvisual = 0;
	setmode(e, Modenormal);
	cmdclear(e);
	e->cmdprefix = ':';
	return 0;
}

static int
cmdbs(Eek *e, Args *a)
{
	(void)a;
	if (e == nil)
		return 0;
	if (e->cmdn > 0)
		e->cmdn--;
	return 0;
}

static int
cmdrune(Eek *e, Args *a)
{
	long r;

	if (e == nil)
		return 0;
	r = argsat(a, 0, 0);
	if (r == '\n')
		return cmdenter(e, a);
	if (r >= 0x20 && r < 0x7f) {
		if (e->cmdn + 1 < (long)sizeof e->cmd)
			e->cmd[e->cmdn++] = (char)r;
	}
	return 0;
}

static const Move cmdkeys[] = {
	{ 1u << Modecmd, Keyesc, 0, "<Esc>", cmdesc },
	{ 1u << Modecmd, Keyenter, 0, "<Enter>", cmdenter },
	{ 1u << Modecmd, Keybackspace, 0, "<BS>", cmdbs },
	{ 1u << Modecmd, Keyrune, -1, "{c}", cmdrune },
};

static int
cmdkey(Eek *e, const Key *k)
{
	Args a;

	if (e == nil || k == nil)
		return 0;
	argsinit(&a);
	if (k->kind == Keyrune)
		(void)argspush(&a, k->value);
	(void)movedispatch(e, Modecmd, cmdkeys, (long)(sizeof cmdkeys / sizeof cmdkeys[0]), k, &a);
	argsfree(&a);
	return 1;
}

static int
insesc(Eek *e, Args *a)
{
	(void)a;
	if (e == nil)
		return 0;
	if (e->cx > 0)
		e->cx = prevutf8(e, e->cy, e->cx);
	setmode(e, Modenormal);
	e->undopending = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	e->dpending = 0;
	e->cpending = 0;
	e->ypending = 0;
	e->cmdrange = 0;
	e->fcount = 0;
	e->fpending = 0;
	e->tipending = 0;
	e->vtipending = 0;
	normalfixcursor(e);
	return 0;
}

static int
insleft(Eek *e, Args *a)
{
	(void)a;
	movel(e);
	return 0;
}

static int
insright(Eek *e, Args *a)
{
	(void)a;
	mover(e);
	return 0;
}

static int
insup(Eek *e, Args *a)
{
	(void)a;
	moveu(e);
	return 0;
}

static int
insdown(Eek *e, Args *a)
{
	(void)a;
	moved(e);
	return 0;
}

static int
insbs(Eek *e, Args *a)
{
	(void)a;
	(void)delback(e);
	return 0;
}

static int
insenter(Eek *e, Args *a)
{
	(void)a;
	(void)insertnl(e);
	return 0;
}

static int
insrune(Eek *e, Args *a)
{
	char s[8];
	long n;
	long r;

	if (e == nil)
		return 0;
	r = argsat(a, 0, 0);
	if (r == '\n') {
		(void)insertnl(e);
		return 0;
	}
	if (r == '\t') {
		s[0] = '\t';
		(void)insertbytes(e, s, 1);
		return 0;
	}
	if (r < 0x20)
		return 0;
	n = utf8enc(r, s);
	(void)insertbytes(e, s, n);
	return 0;
}

static const Move inskeys[] = {
	{ 1u << Modeinsert, Keyesc, 0, "<Esc>", insesc },
	{ 1u << Modeinsert, Keyleft, 0, "<Left>", insleft },
	{ 1u << Modeinsert, Keyright, 0, "<Right>", insright },
	{ 1u << Modeinsert, Keyup, 0, "<Up>", insup },
	{ 1u << Modeinsert, Keydown, 0, "<Down>", insdown },
	{ 1u << Modeinsert, Keybackspace, 0, "<BS>", insbs },
	{ 1u << Modeinsert, Keyenter, 0, "<Enter>", insenter },
	{ 1u << Modeinsert, Keyrune, -1, "{c}", insrune },
};

static int
inskey(Eek *e, const Key *k)
{
	Args a;

	if (e == nil || k == nil)
		return 0;
	argsinit(&a);
	if (k->kind == Keyrune)
		(void)argspush(&a, k->value);
	(void)movedispatch(e, Modeinsert, inskeys, (long)(sizeof inskeys / sizeof inskeys[0]), k, &a);
	argsfree(&a);
	return 1;
}

/* NORMAL/VISUAL move implementations (table-driven). */
static int
quit(Eek *e, Args *a)
{
	(void)a;
	e->quit = 1;
	return 0;
}

static int
undo(Eek *e, Args *a)
{
	(void)a;
	undopop(e);
	e->count = 0;
	e->opcount = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
searchnext(Eek *e, Args *a)
{
	(void)a;
	if (e->lastsearch == nil || e->lastsearch[0] == 0) {
		setmsg(e, "No previous search");
		return 0;
	}
	if (searchforward(e, e->lastsearch) < 0)
		setmsg(e, "Pattern not found: %s", e->lastsearch);
	e->count = 0;
	e->opcount = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
searchprev(Eek *e, Args *a)
{
	(void)a;
	if (e->lastsearch == nil || e->lastsearch[0] == 0) {
		setmsg(e, "No previous search");
		return 0;
	}
	if (searchbackward(e, e->lastsearch) < 0)
		setmsg(e, "Pattern not found: %s", e->lastsearch);
	e->count = 0;
	e->opcount = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
vistoggle(Eek *e, Args *a)
{
	(void)a;
	if (e->mode == Modevisual) {
		e->dpending = 0;
		e->cpending = 0;
		e->ypending = 0;
		e->fpending = 0;
		e->fcount = 0;
		e->tipending = 0;
		e->vtipending = 0;
		setmode(e, Modenormal);
	} else {
		e->dpending = 0;
		e->cpending = 0;
		e->ypending = 0;
		e->fpending = 0;
		e->fcount = 0;
		e->tipending = 0;
		e->vay = e->cy;
		e->vax = e->cx;
		e->vtipending = 0;
		setmode(e, Modevisual);
	}
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
exline(Eek *e, Args *a)
{
	int wasvisual;

	(void)a;
	wasvisual = (e->mode == Modevisual);
	setmode(e, Modecmd);
	cmdclear(e);
	e->cmdprefix = ':';
	e->cmdkeepvisual = wasvisual;
	if (wasvisual) {
		vsellines(e, &e->cmdy0, &e->cmdy1);
		e->cmdrange = 1;
	} else {
		e->cmdrange = 0;
	}
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
searchline(Eek *e, Args *a)
{
	(void)a;
	setmode(e, Modecmd);
	cmdclear(e);
	e->cmdprefix = '/';
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
opdel(Eek *e, Args *a)
{
	(void)a;
	e->opcount = countval(e->count);
	e->count = 0;
	e->dpending = 1;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
opchg(Eek *e, Args *a)
{
	(void)a;
	e->opcount = countval(e->count);
	e->count = 0;
	e->cpending = 1;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
opyank(Eek *e, Args *a)
{
	(void)a;
	e->opcount = countval(e->count);
	e->count = 0;
	e->ypending = 1;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
findbegin(Eek *e, Args *a)
{
	long mode;

	mode = argsat(a, 0, 0);
	e->fpending = 1;
	e->fcount = countval(e->count);
	e->fmode = mode;
	e->fop = 0;
	e->count = 0;
	e->opcount = 0;
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
paste(Eek *e, Args *a)
{
	(void)a;
	if (e->yline)
		(void)pastelinewise(e, 0);
	else
		(void)pastecharwise(e, 0);
	e->count = 0;
	return 0;
}

static int
pastebefore(Eek *e, Args *a)
{
	(void)a;
	if (e->yline)
		(void)pastelinewise(e, 1);
	else
		(void)pastecharwise(e, 1);
	e->count = 0;
	return 0;
}

static int
chgend(Eek *e, Args *a)
{
	Line *l;
	long len;
	long nlines;
	long i;

	(void)a;
	nlines = countval(e->count);
	e->count = 0;
	if (undopush(e) < 0)
		return 0;
	l = bufgetline(&e->b, e->cy);
	if (l != nil) {
		len = l->n;
		if (e->cx < len)
			(void)yset(e, l->s + e->cx, len - e->cx, 0);
		else
			yclear(e);
		if (e->cx < len)
			(void)linedelrange(l, e->cx, len - e->cx);
	}
	for (i = 1; i < nlines; i++) {
		Line *nl;
		char nlsep;

		nl = bufgetline(&e->b, e->cy + 1);
		if (nl == nil)
			break;
		nlsep = '\n';
		(void)yappend(e, &nlsep, 1);
		if (nl->n > 0)
			(void)yappend(e, nl->s, nl->n);
		(void)bufdelline(&e->b, e->cy + 1);
	}
	e->dirty = 1;
	setmode(e, Modeinsert);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	return 0;
}

static int
insafter(Eek *e, Args *a)
{
	(void)a;
	e->cx = nextutf8(e, e->cy, e->cx);
	setmode(e, Modeinsert);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
insend(Eek *e, Args *a)
{
	(void)a;
	e->cx = linelen(e, e->cy);
	setmode(e, Modeinsert);
	e->lastnormalrune = 0;
	e->lastmotioncount = 0;
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
openbelow(Eek *e, Args *a)
{
	(void)a;
	(void)openlinebelow(e);
	e->lastnormalrune = 0;
	return 0;
}

static int
openabove(Eek *e, Args *a)
{
	(void)a;
	(void)openlineabove(e);
	e->lastnormalrune = 0;
	return 0;
}

static int
curleft(Eek *e, Args *a)
{
	(void)a;
	repeat(e, movel, countval(e->count));
	e->count = 0;
	return 0;
}

static int
curdown(Eek *e, Args *a)
{
	(void)a;
	repeat(e, moved, countval(e->count));
	e->count = 0;
	return 0;
}

static int
curup(Eek *e, Args *a)
{
	(void)a;
	repeat(e, moveu, countval(e->count));
	e->count = 0;
	return 0;
}

static int
curright(Eek *e, Args *a)
{
	(void)a;
	repeat(e, mover, countval(e->count));
	e->count = 0;
	return 0;
}

static int
page(Eek *e, Args *a)
{
	long r;
	long npage;
	long rows;
	long delta;

	r = argsat(a, 0, 0);
	npage = countval(e->count);
	rows = curwinrows(e);
	delta = rows * npage;
	if (r == '(')
		delta = -delta;
	e->cy = clamp(e->cy + delta, 0, e->b.nline > 0 ? e->b.nline - 1 : 0);
	e->rowoff = e->rowoff + delta;
	if (e->rowoff < 0)
		e->rowoff = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
bol(Eek *e, Args *a)
{
	(void)a;
	e->cx = 0;
	return 0;
}

static int
eol(Eek *e, Args *a)
{
	(void)a;
	e->cx = linelen(e, e->cy);
	return 0;
}

static int
wordnext(Eek *e, Args *a)
{
	(void)a;
	repeat(e, movew, countval(e->count));
	e->count = 0;
	return 0;
}

static int
wordprev(Eek *e, Args *a)
{
	(void)a;
	repeat(e, moveb, countval(e->count));
	e->count = 0;
	return 0;
}

static int
insbefore(Eek *e, Args *a)
{
	(void)a;
	setmode(e, Modeinsert);
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
delchar(Eek *e, Args *a)
{
	(void)a;
	(void)delat_yank(e, countval(e->count));
	e->count = 0;
	return 0;
}

static int
gotoline(Eek *e, Args *a)
{
	(void)a;
	if (e->count > 0)
		e->cy = clamp(e->count - 1, 0, e->b.nline - 1);
	else
		e->cy = e->b.nline - 1;
	e->cx = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
gprefix(Eek *e, Args *a)
{
	(void)a;
	e->lastnormalrune = 'g';
	e->seqcount = e->count;
	e->count = 0;
	return 0;
}

static int
leader(Eek *e, Args *a)
{
	(void)a;
	e->lastnormalrune = ' ';
	e->seqcount = 0;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
ctrlw(Eek *e, Args *a)
{
	(void)a;
	e->lastnormalrune = 0x17;
	e->count = 0;
	e->opcount = 0;
	return 0;
}

static int
findagain(Eek *e, Args *a)
{
	long n;
	long mode;
	long r;
	long pos;
	long posend;
	long origcx;
	long len;

	(void)a;
	if (e == nil)
		return 0;
	if (e->lastfindmode == 0 || e->lastfindr == 0) {
		setmsg(e, "No previous find");
		e->count = 0;
		e->opcount = 0;
		return 0;
	}
	n = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	if (n < 1)
		n = 1;

	mode = e->lastfindmode;
	r = e->lastfindr;
	origcx = e->cx;
	len = linelen(e, e->cy);
	pos = -1;
	switch (mode) {
	case 'f':
	case 't':
		if (findfwd(e, r, n) < 0)
			pos = -1;
		else
			pos = e->cx;
		break;
	case 'F':
	case 'T':
		if (findbwd(e, r, n) < 0)
			pos = -1;
		else
			pos = e->cx;
		break;
	default:
		pos = -1;
		break;
	}
	if (pos < 0) {
		setmsg(e, "Not found: %lc", (long)r);
		e->cx = origcx;
		return 0;
	}
	posend = nextutf8(e, e->cy, pos);
	if (mode == 't')
		e->cx = prevutf8(e, e->cy, pos);
	else if (mode == 'T')
		e->cx = posend;
	else
		e->cx = pos;
	if (e->cx > len)
		e->cx = len;
	return 0;
}

static int
findagainrev(Eek *e, Args *a)
{
	long mode;

	(void)a;
	if (e == nil)
		return 0;
	mode = e->lastfindmode;
	if (mode == 'f')
		e->lastfindmode = 'F';
	else if (mode == 'F')
		e->lastfindmode = 'f';
	else if (mode == 't')
		e->lastfindmode = 'T';
	else if (mode == 'T')
		e->lastfindmode = 't';
	(void)findagain(e, a);
	/* restore original mode for subsequent ';' */
	e->lastfindmode = mode;
	return 0;
}

static int
dotrepeat(Eek *e, Args *a)
{
	long n;
	long i;
	int j;
	KeyEvent ev;

	(void)a;
	if (e == nil)
		return 0;
	if (e->dotlen <= 0) {
		setmsg(e, "No previous change");
		e->count = 0;
		e->opcount = 0;
		return 0;
	}
	n = countval(e->count);
	e->count = 0;
	e->opcount = 0;
	if (n < 1)
		n = 1;

	for (i = 0; i < n; i++) {
		for (j = e->dotlen - 1; j >= 0; j--) {
			ev = e->dotbuf[j];
			ev.nomap = 1;
			ev.src = Keysrcdot;
			if (feedpushfront(e, &ev) < 0) {
				setmsg(e, "dot feed overflow");
				return 0;
			}
		}
		e->dotreplayleft += e->dotlen;
	}
	return 0;
}

static const Move nvkeys[] = {
	/* motion */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'h', "{n}h", curleft },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'j', "{n}j", curdown },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'k', "{n}k", curup },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'l', "{n}l", curright },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, '(', "{n}<PgUp>", page },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, ')', "{n}<PgDn>", page },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, '0', "0", bol },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, '$', "$", eol },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'w', "{n}w", wordnext },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'b', "{n}b", wordprev },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'G', "{n}G", gotoline },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'g', "gf", gprefix },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'f', "f{c}", findbegin },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'F', "F{c}", findbegin },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 't', "t{c}", findbegin },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'T', "T{c}", findbegin },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, ';', ";", findagain },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, ',', ",", findagainrev },

	/* mode */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'v', "v", vistoggle },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, ':', ":", exline },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, '/', "/", searchline },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'i', "i", insbefore },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'a', "a", insafter },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'A', "A", insend },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'o', "o", openbelow },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'O', "O", openabove },

	/* operators */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'd', "d{n}f", opdel },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'c', "c{n}f", opchg },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'y', "y{n}f", opyank },

	/* edits */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'x', "{n}x", delchar },
	{ 1u << Modenormal, Keyrune, 's', "{n}s", subchar },
	{ 1u << Modenormal, Keyrune, 'r', "r{c}", replbegin },
	{ 1u << Modenormal, Keyrune, 'D', "{n}D", delend },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'C', "C", chgend },
	{ 1u << Modenormal, Keyrune, 'S', "{n}S", sublinecmd },

	/* paste */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'p', "p", paste },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'P', "P", pastebefore },

	/* search */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'n', "n", searchnext },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'N', "N", searchprev },

	/* meta */
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'u', "u", undo },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, '.', ".", dotrepeat },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 'q', "q", quit },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, ' ', " <leader>", leader },
	{ (1u << Modenormal) | (1u << Modevisual), Keyrune, 0x17, "<C-w>", ctrlw },
};

static int
nvkey(Eek *e, const Key *k)
{
	Args a;
	long r;
	int did;
	int dir;
	int handled;

	if (e == nil || k == nil)
		return 0;

	/* Ctrl+hjkl window navigation: keep as a hard override for now. */
	if (nwins(e->layout) > 1) {
		did = 0;
		dir = -1;
		if (k->kind == Keybackspace) {
			/* Many terminals send DEL for Ctrl-H / Backspace. */
			did = 1;
			dir = Dirleft;
		}
		if (k->kind == Keyrune) {
			if (k->value == 0x08) { did = 1; dir = Dirleft; }
			else if (k->value == '\n') { did = 1; dir = Dirdown; }
			else if (k->value == 0x0b) { did = 1; dir = Dirup; }
			else if (k->value == 0x0c) { did = 1; dir = Dirright; }
		}
		if (did && dir >= 0) {
			(void)focusdir(e, dir);
			e->dpending = 0;
			e->cpending = 0;
			e->ypending = 0;
			e->fpending = 0;
			e->fcount = 0;
			e->rpending = 0;
			e->rcount = 0;
			e->tipending = 0;
			e->vtipending = 0;
			e->lastnormalrune = 0;
			e->lastmotioncount = 0;
			e->seqcount = 0;
			e->count = 0;
			e->opcount = 0;
			return 1;
		}
	}

	/* ESC cancels pending operators / exits visual like before. */
	if (k->kind == Keyesc) {
		e->dpending = 0;
		e->cpending = 0;
		e->ypending = 0;
		e->fpending = 0;
		e->fcount = 0;
		e->rpending = 0;
		e->rcount = 0;
		e->tipending = 0;
		e->vtipending = 0;
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		if (e->mode == Modevisual)
			setmode(e, Modenormal);
		return 1;
	}

	if (e->rpending) {
		e->rpending = 0;
		if (k->kind == Keyrune) {
			long rr;
			long n;

			rr = k->value;
			n = e->rcount;
			e->rcount = 0;
			if (rr == '\n' || rr == '\r')
				goto repldone;
			if (rr == '\t')
				replchars(e, rr, n);
			else if (rr >= 0x20)
				replchars(e, rr, n);
		}
	repldone:
		e->count = 0;
		e->opcount = 0;
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		if (e->mode == Modenormal)
			normalfixcursor(e);
		return 1;
	}

	if (e->fpending) {
		e->fpending = 0;
		if (k->kind == Keyrune) {
			long n;
			long mode;
			long op;
			long origcx;
			long pos;
			long posend;
			long curend;
			long x0, x1;

			n = e->fcount;
			mode = e->fmode;
			op = e->fop;
			e->fcount = 0;
			e->fmode = 0;
			e->fop = 0;

			origcx = e->cx;
			pos = -1;
			switch (mode) {
			case 'f':
			case 't':
				if (findfwd(e, k->value, n) < 0)
					pos = -1;
				else
					pos = e->cx;
				break;
			case 'F':
			case 'T':
				if (findbwd(e, k->value, n) < 0)
					pos = -1;
				else
					pos = e->cx;
				break;
			default:
				pos = -1;
				break;
			}

			if (pos < 0) {
				setmsg(e, "Not found: %lc", (long)k->value);
				e->cx = origcx;
				goto finddone;
			}
			e->lastfindmode = mode;
			e->lastfindr = k->value;
			posend = nextutf8(e, e->cy, pos);
			curend = nextutf8(e, e->cy, origcx);

			if (op == 0) {
				switch (mode) {
				case 'f':
				case 'F':
					e->cx = pos;
					break;
				case 't':
					e->cx = prevutf8(e, e->cy, pos);
					break;
				case 'T':
					e->cx = posend;
					break;
				default:
					e->cx = origcx;
					break;
				}
			} else {
				/* Operator-pending applies to current line only. */
				e->cx = origcx;
				x0 = origcx;
				x1 = origcx;
				switch (mode) {
				case 'f': /* through */
					x0 = origcx;
					x1 = posend;
					break;
				case 't': /* until */
					x0 = origcx;
					x1 = pos;
					break;
				case 'F': /* backward through */
					x0 = pos;
					x1 = curend;
					break;
				case 'T': /* backward until */
					x0 = posend;
					x1 = curend;
					break;
				default:
					break;
				}
				if (x0 != x1) {
					if (op == 'y') {
						(void)yankrange(e, e->cy, x0, e->cy, x1);
						/* Preserve cursor. */
						e->cx = origcx;
					} else {
						(void)delrange(e, e->cy, x0, e->cy, x1, 1);
						if (op == 'c')
							setmode(e, Modeinsert);
					}
				}
			}
		finddone:
			;
		}
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		return 1;
	}

	if (e->dpending) {
		if (k->kind == Keyrune && k->value >= '0' && k->value <= '9') {
			if (e->count > 0 || k->value != '0') {
				e->count = e->count * 10 + (k->value - '0');
				return 1;
			}
		}

		e->dpending = 0;
		if (k->kind == Keyrune) {
			long total;

			total = countval(e->opcount) * countval(e->count);
			e->opcount = 0;
			e->count = 0;
			switch (k->value) {
			case 'd':
				(void)dellines(e, total);
				break;
			case 'i':
				e->tipending = 1;
				e->tiop = 'd';
				e->opcount = total;
				return 1;
			case 'w':
				(void)delwords(e, total);
				break;
			case 'e':
				(void)delendwords(e, total);
				break;
			case 'f':
			case 't':
			case 'F':
			case 'T':
				e->fpending = 1;
				e->fcount = total;
				e->fmode = k->value;
				e->fop = 'd';
				return 1;
			default:
				setmsg(e, "Unknown d%lc", (long)k->value);
				break;
			}
		}
		if (e->mode == Modenormal)
			normalfixcursor(e);
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		return 1;
	}

	if (e->tipending) {
		e->tipending = 0;
		if (k->kind == Keyrune)
			(void)delinside(e, e->tiop, k->value);
		e->tiop = 0;
		e->opcount = 0;
		e->count = 0;
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		return 1;
	}

	if (e->ypending) {
		if (k->kind == Keyrune && k->value >= '0' && k->value <= '9') {
			if (e->count > 0 || k->value != '0') {
				e->count = e->count * 10 + (k->value - '0');
				return 1;
			}
		}

		e->ypending = 0;
		if (k->kind == Keyrune) {
			long total;
			long sy, sx;
			long ty, tx;

			total = countval(e->opcount) * countval(e->count);
			e->opcount = 0;
			e->count = 0;
			sy = e->cy;
			sx = e->cx;

			switch (k->value) {
			case 'y':
				(void)yanklines(e, e->cy, total);
				break;
			case 'w':
				{
					long i;
					long cy, cx;

					cy = sy;
					cx = sx;
					for (i = 0; i < total; i++) {
						e->cy = cy;
						e->cx = cx;
						wordtarget(e, &ty, &tx);
						if (ty == cy && tx <= cx)
							break;
						cy = ty;
						cx = tx;
					}
					e->cy = sy;
					e->cx = sx;
					(void)yankrange(e, sy, sx, cy, cx);
				}
				break;
			case 'e':
				endwordtarget(e, &ty, &tx);
				(void)yankrange(e, e->cy, e->cx, e->cy, tx);
				break;
			case '$': {
				long len;

				len = linelen(e, e->cy);
				(void)yankrange(e, e->cy, e->cx, e->cy, len);
				break;
			}
			case 'f':
			case 't':
			case 'F':
			case 'T':
				e->fpending = 1;
				e->fcount = total;
				e->fmode = k->value;
				e->fop = 'y';
				return 1;
			default:
				setmsg(e, "Unknown y%lc", (long)k->value);
				break;
			}
			e->cy = sy;
			e->cx = sx;
		}
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		return 1;
	}

	if (e->cpending) {
		if (k->kind == Keyrune && k->value >= '0' && k->value <= '9') {
			if (e->count > 0 || k->value != '0') {
				e->count = e->count * 10 + (k->value - '0');
				return 1;
			}
		}

		e->cpending = 0;
		if (k->kind == Keyrune) {
			long total;

			total = countval(e->opcount) * countval(e->count);
			e->opcount = 0;
			e->count = 0;
			switch (k->value) {
			case 'w':
				(void)delwords(e, total);
				setmode(e, Modeinsert);
				break;
			case 'f':
			case 't':
			case 'F':
			case 'T':
				e->fpending = 1;
				e->fcount = total;
				e->fmode = k->value;
				e->fop = 'c';
				return 1;
			case 'i':
				e->tipending = 1;
				e->tiop = 'c';
				e->opcount = total;
				return 1;
			default:
				setmsg(e, "Unknown c%lc", (long)k->value);
				break;
			}
		}
		if (e->mode == Modenormal)
			normalfixcursor(e);
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		return 1;
	}

	/* Arrow key motions work regardless of counts. */
	if (k->kind == Keyup) { moveu(e); return 1; }
	if (k->kind == Keydown) { moved(e); return 1; }
	if (k->kind == Keyleft) { movel(e); return 1; }
	if (k->kind == Keyright) { mover(e); return 1; }

	if (k->kind != Keyrune)
		return 0;

	/* Ctrl-W w: next window. */
	if (e->lastnormalrune == 0x17 && k->value == 'w') {
		(void)nextwin(e);
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		goto afterkey;
	}

	/* VISUAL extra keys: v, yi{obj}, di{obj} etc. */
	if (e->mode == Modevisual) {
		if (e->vtipending) {
			e->vtipending = 0;
			(void)vselectinside(e, k->value);
			goto afterkey;
		}
		if (k->value == 'i') {
			e->vtipending = 1;
			goto afterkey;
		}
		if (k->value == 'v') {
			e->vtipending = 0;
			setmode(e, Modenormal);
			goto afterkey;
		}
		if (k->value == 'y') {
			long sy, sx, ey, ex;

			vselbounds(e, &sy, &sx, &ey, &ex);
			(void)yankrange(e, sy, sx, ey, ex);
			setmsg(e, "yanked");
			e->vtipending = 0;
			setmode(e, Modenormal);
			goto afterkey;
		}
		if (k->value == 'd') {
			long sy, sx, ey, ex;

			vselbounds(e, &sy, &sx, &ey, &ex);
			(void)delrange(e, sy, sx, ey, ex, 1);
			setmsg(e, "deleted");
			e->vtipending = 0;
			setmode(e, Modenormal);
			goto afterkey;
		}
		if (k->value == 'c' || k->value == 's' || k->value == 'S') {
			long sy, sx, ey, ex;

			vselbounds(e, &sy, &sx, &ey, &ex);
			(void)delrange(e, sy, sx, ey, ex, 1);
			e->vtipending = 0;
			setmode(e, Modeinsert);
			goto afterkey;
		}
	}

	/* Count parsing (vim-like): digits accumulate unless leading '0'. */
	if (k->value >= '0' && k->value <= '9') {
		if (e->count > 0 || k->value != '0') {
			e->count = e->count * 10 + (k->value - '0');
			e->lastnormalrune = 0;
			e->lastmotioncount = 0;
			e->seqcount = 0;
			return 1;
		}
	}

	/* Multi-key sequences handled before table dispatch (gg, gt/gT, leader). */
	if (e->lastnormalrune == 'g' && k->value == 'g') {
		long line;

		line = e->seqcount > 0 ? e->seqcount - 1 : 0;
		e->cy = clamp(line, 0, e->b.nline - 1);
		e->cx = 0;
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		return 1;
	}
	if (e->lastnormalrune == 'g' && (k->value == 't' || k->value == 'T')) {
		long idx;

		if (e->ntab <= 1) {
			setmsg(e, "Only one tab");
			e->lastnormalrune = 0;
			e->lastmotioncount = 0;
			e->seqcount = 0;
			e->count = 0;
			e->opcount = 0;
			return 1;
		}
		if (e->seqcount > 0) {
			idx = e->seqcount - 1;
		} else if (k->value == 't') {
			idx = (e->curtab + 1) % e->ntab;
		} else {
			idx = e->curtab - 1;
			if (idx < 0)
				idx = e->ntab - 1;
		}
		if (idx < 0)
			idx = 0;
		if (idx >= e->ntab)
			idx = e->ntab - 1;
		if (tabswitch(e, idx) < 0)
			setmsg(e, "Cannot switch tab");
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		return 1;
	}
	if (e->mode == Modenormal && e->lastnormalrune == ' ' && (k->value == 'n' || k->value == 'h' || k->value == 'j' || k->value == 'k' || k->value == 'l')) {
		long idx;

		idx = e->curtab;
		if (k->value == 'n') {
			if (tabnew(e, nil) < 0)
				setmsg(e, "Cannot open tab");
		} else {
			if (e->ntab <= 1) {
				setmsg(e, "Only one tab");
			} else {
				if (k->value == 'h')
					idx = 0;
				else if (k->value == 'l')
					idx = e->ntab - 1;
				else if (k->value == 'j') {
					idx = e->curtab - 1;
					if (idx < 0)
						idx = e->ntab - 1;
				} else if (k->value == 'k') {
					idx = (e->curtab + 1) % e->ntab;
				}
				if (tabswitch(e, idx) < 0)
					setmsg(e, "Cannot switch tab");
			}
		}
		e->lastnormalrune = 0;
		e->lastmotioncount = 0;
		e->seqcount = 0;
		e->count = 0;
		e->opcount = 0;
		return 1;
	}

	argsinit(&a);
	if (k->value == 'f' || k->value == 'F' || k->value == 't' || k->value == 'T' || k->value == '(' || k->value == ')')
		(void)argspush(&a, k->value);
	handled = movedispatch(e, e->mode, nvkeys, (long)(sizeof nvkeys / sizeof nvkeys[0]), k, &a);
	argsfree(&a);

	/* Apply the original afterkey state cleanup rules when a rune move fired. */
	if (handled)
		goto afterkey;

	return 0;

afterkey:
	r = k->value;
	if (r != 'l' && r != 'r' && r != 'g' && r != ' ' && r != 0x17)
		e->lastnormalrune = 0;
	if (r != 'l')
		e->lastmotioncount = 0;
	if (r != 'g')
		e->seqcount = 0;
	if (e->mode == Modenormal || e->mode == Modevisual)
		normalfixcursor(e);
	return 1;
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
	KeyEvent kev;

	memset(&e, 0, sizeof e);
	bufinit(&e.b);
	e.synenabled = Syntaxhighlight;
	e.cmdprefix = ':';
	if (tabinit1(&e) < 0)
		die("Out of memory");

	if (argc > 1) {
		e.fname = argv[1];
		e.ownfname = 0;
		(void)bufload(&e.b, e.fname);
		setsyn(&e);
	}

	terminit(&e.t);
	/* Initialize the window layout with a single window/view. */
	e.layout = nodeleaf(winnewfrom(&e));
	if (e.layout == nil || e.layout->w == nil)
		die("Out of memory");
	e.curwin = e.layout->w;
	setmode(&e, Modenormal);
	cmdclear(&e);
	draw(&e);

	for (;;) {
		if (termresized()) {
			termgetwinsz(&e.t);
			/* Clamp the active window cursor after a resize. */
			winclamp(&e, e.curwin);
			normalfixcursor(&e);
		}
		/* Scroll the active window based on its viewport height. */
		{
			Rect root;
			Rect cur;
			long textrows;

			textrows = e.t.row - 1;
			if (textrows < 1)
				textrows = 1;
			root = (Rect){ 0, 0, e.t.col, (int)textrows };
			cur = root;
			if (!findrect(e.layout, e.curwin, root, &cur))
				cur = root;
			scroll(&e, cur.h);
			{
				int gut;
				long textcols;

				gut = gutterwidth(&e, cur.w);
				textcols = cur.w - gut;
				hscroll(&e, textcols);
			}
		}
		winstore(&e);
		winload(&e, e.curwin);
		draw(&e);
		if (e.quit)
			break;
		if (!feedpop(&e, &kev)) {
			memset(&kev, 0, sizeof kev);
			if (keyread(&e.t, &kev.k) < 0)
				break;
			kev.nomap = 0;
			kev.src = Keysrcuser;
		}
		if (kev.src == Keysrcdot && e.dotreplayleft > 0)
			e.dotreplayleft--;
		if (e.mode != Modeinsert)
			e.undopending = 0;
		if (e.msg[0] != 0 && e.mode != Modecmd)
			e.msg[0] = 0;

		/* Apply maps (NORMAL/VISUAL only) before dispatch. */
		if (!kev.nomap && (e.mode == Modenormal || e.mode == Modevisual) && kev.k.kind == Keyrune) {
			if (mapapply(&e, e.mode, kev.k.value))
				continue;
		}

		/* '.' recording: start on change keys in NORMAL; record effective keys. */
		if (e.dotreplayleft <= 0) {
			if (!e.dotrec && e.mode == Modenormal && kev.k.kind == Keyrune && dotstartkey(kev.k.value))
				dotrecstart(&e);
			if (e.dotrec) {
				if (!(e.mode == Modenormal && kev.k.kind == Keyrune && kev.k.value == '.'))
					dotrecadd(&e, &kev);
			}
		}

		if (e.mode == Modecmd) {
			(void)cmdkey(&e, &kev.k);
			continue;
		}
		if (e.mode == Modeinsert) {
			(void)inskey(&e, &kev.k);
			goto afterdispatch;
		}
		(void)nvkey(&e, &kev.k);
	afterdispatch:
		;

		if (e.dotrec) {
			if (e.mode == Modenormal && !e.dpending && !e.cpending && !e.ypending &&
			    !e.fpending && !e.rpending && !e.tipending && !e.vtipending)
				dotrecsave(&e);
		}
		if (e.quit)
			break;
	}

	/* Free inactive tabs (the active tab lives in e.* fields). */
	if (e.tab != nil) {
		long i;
		for (i = 0; i < e.ntab; i++) {
			if (i == e.curtab)
				continue;
			tabfree(&e.tab[i]);
		}
		free(e.tab);
		e.tab = nil;
		e.ntab = 0;
		e.captab = 0;
		e.curtab = 0;
	}
	yclear(&e);
	undofree(&e);
	nodefree(e.layout);
	mapfreeall(&e);
	free(e.lastsearch);
	setcursorshape(&e, Cursornormal);
	termclear(&e.t);
	termmoveto(&e.t, 0, 0);
	termflush(&e.t);
	free(e.t.out);
	e.t.out = nil;
	e.t.outn = 0;
	e.t.outcap = 0;
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
	u->coloff = e->coloff;
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
	e->coloff = u.coloff;
	if (e->coloff < 0)
		e->coloff = 0;
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
