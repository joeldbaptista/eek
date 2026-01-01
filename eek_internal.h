#ifndef EEK_INTERNAL_H
#define EEK_INTERNAL_H

#include <limits.h>

#include "config.h"
#include "buf.h"
#include "eek.h"
#include "util.h"

typedef struct Eek Eek;

typedef struct KeyEvent KeyEvent;
struct KeyEvent {
	Key k;     /* The key event payload (kind/value/modifiers) as read or injected. */
	int nomap; /* Non-zero prevents :map remapping on this event (RHS is injected with nomap=1). */
	int src;   /* Event origin; one of Keysrc*. */
};

/* Key event sources used for tracing and controlling remaps/replay. */
enum {
	Keysrcuser, /* Physical user input from the terminal. */
	Keysrcmap,  /* Injected from :map expansion. */
	Keysrcdot,  /* Injected from '.' repeat replay. */
};

typedef struct Args Args;
struct Args {
	long v[8];    /* Inline small-args storage. */
	int n;        /* Total number of args currently stored. */
	long *heap;   /* Heap storage for args beyond v[]. */
	int heapcap;  /* Capacity of heap[] in longs. */
};

/*
 * MoveFn is the callable that implements a key/motion/edit command.
 *
 * Parameters:
 *  - e: editor state.
 *  - a: parsed argument list (counts, runes, etc).
 *
 * Returns:
 *  - 0 on success; negative on failure. (Most callers ignore the value.)
 */
typedef int (*MoveFn)(Eek *e, Args *a);

typedef struct Move Move;
struct Move {
	unsigned int modes;  /* Bitmask of Mode* values in which this entry is active. */
	int kind;            /* Key kind to match (Keyrune, Keyesc, ...). */
	long value;          /* For Keyrune: rune value; -1 matches any rune; otherwise ignored. */
	const char *pattern; /* Documentation-only pattern string (not used for matching). */
	MoveFn fn;           /* Implementation function (nil means “handled but no-op”). */
};

typedef struct Win Win;
struct Win {
	long cx;         /* Cursor x (byte offset within line). */
	long cy;         /* Cursor y (line index). */
	long rowoff;     /* Topmost visible line (scroll offset). */
	long coloff;     /* Leftmost visible column (render column scroll offset). */
	long vax;        /* VISUAL anchor x (byte offset). */
	long vay;        /* VISUAL anchor y (line index). */
	int vmode;       /* VISUAL selection kind (Visualchar/Visualblock). */
	long vbrx;       /* VISUAL block anchor render column (virtual). */
	long vrx;        /* VISUAL block cursor render column (virtual). */
	long vtipending; /* VISUAL pending text-object modifier. */
};

typedef struct Node Node;
struct Node {
	int split; /* Layout split kind: 0=leaf, 1=horizontal split, 2=vertical split. */
	Node *a;   /* First child node if split != 0, otherwise nil. */
	Node *b;   /* Second child node if split != 0, otherwise nil. */
	Win *w;    /* Leaf window if split == 0, otherwise nil. */
};

typedef struct Rect Rect;
struct Rect {
	int x; /* Left column (0-based, terminal coordinates). */
	int y; /* Top row (0-based, terminal coordinates). */
	int w; /* Width in terminal columns. */
	int h; /* Height in terminal rows. */
};

/* Cardinal directions used for window focus navigation. */
enum {
	Dirleft,  /* Focus window to the left. */
	Dirdown,  /* Focus window below. */
	Dirup,    /* Focus window above. */
	Dirright, /* Focus window to the right. */
};

typedef struct Undo Undo;
struct Undo {
	Buf b;       /* Snapshot of the full text buffer. */
	long cx;     /* Cursor x (byte offset within line) at snapshot time. */
	long cy;     /* Cursor y (line index) at snapshot time. */
	long rowoff; /* Topmost visible line (vertical scroll offset) at snapshot time. */
	long coloff; /* Leftmost visible column (horizontal scroll offset) at snapshot time. */
	long dirty;  /* Dirty flag at snapshot time. */
};

typedef struct Tab Tab;
struct Tab {
	Buf b;            /* Tab-local text buffer. */
	char *fname;      /* Tab-local file name (may be nil). */
	int ownfname;     /* Non-zero if fname is heap-owned and must be freed. */
	long dirty;       /* Tab-local dirty flag (unsaved edits). */
	int syntax;       /* Tab-local syntax selection (Syn*). */
	Node *layout;     /* Tab-local window layout tree. */
	Win *curwin;      /* Tab-local active window (leaf in layout). */
	char *lastsearch; /* Tab-local last search pattern (heap-owned) or nil. */
	Undo *undo;       /* Tab-local undo stack (dynamic array). */
	long nundo;       /* Number of undo snapshots currently stored. */
	long capundo;     /* Allocated capacity of undo[] in entries. */
	int undopending;  /* Groups multiple edits into one undo step when non-zero. */
	int inundo;       /* Non-zero while restoring an undo snapshot. */
};

/* Syntax language identifiers used for highlighting. */
enum {
	Synnone, /* No highlighting. */
	Sync,    /* C-like highlighting (used for .c/.h). */
};

/* Highlight classes used by the syntax scanner for token categories. */
enum {
	Hlnone,    /* No special highlight. */
	Hlcomment, /* Comment text. */
	Hlstring,  /* String/character literals. */
	Hlnumber,  /* Numeric literals. */
	Hlkeyword, /* Language keywords. */
	Hltype,    /* Type names (builtins). */
	Hlpreproc, /* Preprocessor directives/markers. */
	Hlspecial, /* Special identifiers (e.g. stdio handles). */
};

/* Editor modes (vi-like). */
enum {
	Modenormal, /* NORMAL mode: motions/operators. */
	Modeinsert, /* INSERT mode: text entry. */
	Modecmd,    /* CMD mode: ':' and '/' prompts. */
	Modevisual, /* VISUAL mode: selection-based operations. */
};

/* VISUAL selection kinds. */
enum {
	Visualchar,  /* Character-wise selection (like 'v'). */
	Visualblock, /* Column-wise (block) selection. */
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
	int vmode;           /* VISUAL selection kind (Visualchar/Visualblock). */
	long vbrx;           /* VISUAL block anchor render column (virtual). */
	long vrx;            /* VISUAL block cursor render column (virtual). */
	long vtipending;     /* VISUAL pending text-object modifier. */
	int blockins;        /* Non-zero while doing VISUAL block insert (I). */
	long blocky0;        /* Block insert: first line index (inclusive). */
	long blocky1;        /* Block insert: last line index (inclusive). */
	long blockrx0;       /* Block insert: left edge render column. */
	char *blockbuf;      /* Block insert typed bytes to replicate. */
	long blockn;         /* Block insert length in bytes. */
	long blockcap;       /* Block insert capacity in bytes. */
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
		unsigned modes; /* Bitmask of Mode* values this mapping applies to. */
		long lhs;       /* Left-hand side trigger key (single rune). */
		char *rhs;      /* Right-hand side UTF-8 string to inject as key events. */
	} *maps;
	long nmaps;   /* Number of mappings currently installed. */
	long capmaps; /* Allocated capacity of maps[] in entries. */
};

/* motion.c: UTF-8, word classes, cursor motions, and find motions */
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
long utf8dec1(const char *s, long n, long *adv);

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
long utf8enc(long r, char *s);

/*
 * lsz converts a size_t length/count to long, clamping to LONG_MAX.
 * This keeps cursor/index math in signed longs while Buf/Line sizes use size_t.
 */
static inline long
lsz(size_t n)
{
	if (n > (size_t)LONG_MAX)
		return LONG_MAX;
	return (long)n;
}

/*
 * clamp clamps v into [lo, hi].
 */
long clamp(long v, long lo, long hi);

/*
 * linelen returns the length in bytes of line y.
 */
long linelen(Eek *e, long y);

/*
 * prevutf8 returns the previous UTF-8 codepoint boundary at or before at.
 */
long prevutf8(Eek *e, long y, long at);

/*
 * nextutf8 returns the next UTF-8 codepoint boundary after at.
 */
long nextutf8(Eek *e, long y, long at);

/*
 * isword reports whether c is considered a word character for word motions.
 */
int isword(long c);

/*
 * ispunctword reports whether c is considered a punctuation-word character.
 */
int ispunctword(long c);

/*
 * isws reports whether c is considered whitespace by motions.
 */
int isws(long c);

/*
 * peekbyte returns the byte value at (y, at) or -1 if out of range.
 */
long peekbyte(Eek *e, long y, long at);

/*
 * movel/mover/moveu/moved/movew/moveb mutate the cursor position according
 * to vi-like motions.
 */
void movel(Eek *e);
void mover(Eek *e);
void moveu(Eek *e);
void moved(Eek *e);
void movew(Eek *e);
void moveb(Eek *e);

/*
 * findfwd/findbwd implement f/F/t/T-style character searching on the
 * current line.
 *
 * Returns:
 *  - 0 if a match is found and the cursor is moved, -1 otherwise.
 */
int findfwd(Eek *e, long r, long n);
int findbwd(Eek *e, long r, long n);

#endif
