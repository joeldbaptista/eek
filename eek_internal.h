#ifndef EEK_INTERNAL_H
#define EEK_INTERNAL_H

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

typedef int (*MoveFn)(Eek *e, Args *a);

typedef struct Move Move;
struct Move {
	unsigned int modes;  /* bitmask of Mode* values */
	int kind;            /* Key kind to match (Keyrune, Keyesc, ...) */
	long value;          /* for Keyrune: rune value; -1 matches any rune */
	const char *pattern; /* documentation only */
	MoveFn fn;
};

typedef struct Win Win;
struct Win {
	long cx;         /* Cursor x (byte offset within line). */
	long cy;         /* Cursor y (line index). */
	long rowoff;     /* Topmost visible line (scroll offset). */
	long coloff;     /* Leftmost visible column (render column scroll offset). */
	long vax;        /* VISUAL anchor x (byte offset). */
	long vay;        /* VISUAL anchor y (line index). */
	long vtipending; /* VISUAL pending text-object modifier. */
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
	Buf b;            /* Tab-local buffer. */
	char *fname;      /* File name for the tab (may be nil). */
	int ownfname;     /* Whether fname is heap-owned. */
	long dirty;       /* Dirty flag for the tab. */
	int syntax;       /* Tab-local syntax selection (Syn*). */
	Node *layout;     /* Tab-local window layout tree. */
	Win *curwin;      /* Tab-local active window. */
	char *lastsearch; /* Tab-local last search pattern (heap-owned) or nil. */
	Undo *undo;       /* Tab-local undo stack. */
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

/* motion.c: UTF-8, word classes, cursor motions, and find motions */
long utf8dec1(const char *s, long n, long *adv);
long utf8enc(long r, char *s);

long clamp(long v, long lo, long hi);
long linelen(Eek *e, long y);
long prevutf8(Eek *e, long y, long at);
long nextutf8(Eek *e, long y, long at);

int isword(long c);
int ispunctword(long c);
int isws(long c);

long peekbyte(Eek *e, long y, long at);

void movel(Eek *e);
void mover(Eek *e);
void moveu(Eek *e);
void moved(Eek *e);
void movew(Eek *e);
void moveb(Eek *e);

int findfwd(Eek *e, long r, long n);
int findbwd(Eek *e, long r, long n);

#endif
