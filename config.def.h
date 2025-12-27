/* default configuration for eek; copy to config.h and edit */

enum {
	TABSTOP = 8,
};

/* cursor shapes (DECSCUSR: ESC [ Ps SP q) */
enum {
	Cursorblinkingblock = 1,
	Cursorsteadyblock = 2,
	Cursorblinkingunderline = 3,
	Cursorsteadyunderline = 4,
	Cursorblinkingbar = 5,
	Cursorsteadybar = 6,
};

enum {
	Cursornormal = Cursorsteadyblock,
	Cursorinsert = Cursorsteadybar,
	Cursorcmd = Cursorsteadyunderline,
};

/* key bytes (terminal escape decoding is done in key.c) */
enum {
	Kesc = 0x1b,
	Kdel = 0x7f,
};

/* syntax highlighting */
enum {
	Syntaxhighlight = 0,
};

/* ANSI SGR sequences (foreground colors) */
#define SYN_NORMAL  "\x1b[39m"
#define SYN_COMMENT "\x1b[90m"
#define SYN_STRING  "\x1b[32m"
#define SYN_NUMBER  "\x1b[35m"
#define SYN_KEYWORD "\x1b[34m"
#define SYN_TYPE    "\x1b[36m"
#define SYN_PREPROC "\x1b[33m"
#define SYN_SPECIAL "\x1b[31m"
