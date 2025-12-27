typedef unsigned int uint;

typedef struct Term Term;

typedef struct Key Key;
struct Key {
	int kind;   /* Key kind (Keyrune, Keyesc, Keyup, ...). */
	long value; /* For Keyrune: Unicode codepoint value; otherwise 0. */
};

enum {
	Keynone,
	Keyrune,
	Keyesc,
	Keybackspace,
	Keyenter,
	Keyup,
	Keydown,
	Keyleft,
	Keyright,
	Keyhome,
	Keyend,
	Keypgup,
	Keypgdown,
};

struct Term {
	int fdin;  /* Input fd (usually stdin). */
	int fdout; /* Output fd (usually stdout). */
	int row;   /* Terminal rows. */
	int col;   /* Terminal columns. */
	char *out; /* Buffered output bytes (optional). */
	long outn; /* Number of bytes used in out[]. */
	long outcap; /* Capacity of out[] in bytes. */
};

/*
 * terminit configures the terminal for editor use (raw mode) and initializes Term.
 *
 * Parameters:
 *  - t: terminal state to initialize (fdin/fdout are set by the implementation).
 *
 * Returns:
 *  - void.
 */
void terminit(Term *t);

/*
 * termrestore restores the terminal state to what it was before terminit.
 *
 * Parameters:
 *  - none.
 *
 * Returns:
 *  - void.
 */
void termrestore(void);

/*
 * termgetwinsz updates t->row and t->col with the current terminal size.
 *
 * Parameters:
 *  - t: terminal state to update.
 *
 * Returns:
 *  - void.
 */
void termgetwinsz(Term *t);

/*
 * termresized reports whether the terminal was resized since the last check.
 *
 * Parameters:
 *  - none.
 *
 * Returns:
 *  - non-zero if resized, 0 otherwise.
 */
int termresized(void);

/*
 * termclear clears the visible terminal.
 *
 * Parameters:
 *  - t: terminal to clear.
 *
 * Returns:
 *  - void.
 */
void termclear(Term *t);

/*
 * termmoveto moves the cursor to (r, c) (0-based).
 *
 * Parameters:
 *  - t: terminal.
 *  - r: target row.
 *  - c: target column.
 *
 * Returns:
 *  - void.
 */
void termmoveto(Term *t, int r, int c);

/*
 * Buffered output helpers.
 * These append to t->out and are flushed by termflush().
 */
void termwrite(Term *t, const void *data, long n);
void termputc(Term *t, char c);
void termrepeat(Term *t, char c, int n);

/*
 * termflush flushes any buffered terminal output, if applicable.
 *
 * Parameters:
 *  - t: terminal.
 *
 * Returns:
 *  - void.
 */
void termflush(Term *t);

/*
 * keyread reads a single key event.
 * This decodes ESC-based sequences (arrows, etc.) and UTF-8 runes.
 *
 * Parameters:
 *  - t: terminal (input fd used).
 *  - k: output key event.
 *
 * Returns:
 *  - 0 on success (k is populated or Keynone if nothing ready).
 *  - -1 on EOF.
 */
int keyread(Term *t, Key *k);

/*
 * die prints a formatted message and terminates the program.
 *
 * Parameters:
 *  - fmt: printf-style format string.
 *  - ...: format arguments.
 *
 * Returns:
 *  - does not return.
 */
void die(const char *fmt, ...);
