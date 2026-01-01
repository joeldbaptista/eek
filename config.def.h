/* default configuration for eek; copy to config.h and edit */

enum {
	TABSTOP = 8,
	LINE_MIN_CAP = 32,
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
