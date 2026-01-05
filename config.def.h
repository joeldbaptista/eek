/* default configuration for eek; copy to config.h and edit */

enum {
	TABSTOP = 8,
	LINE_MIN_CAP = 32,
	RESIZE_PAUSE_MS = 50,
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

/* :apply registry types + executor */
#include "apply.h"

/* Optional example apply function (implemented in apply.c). */
int apply_space_between(const char *in, long inlen, int argc, char **argv,
			char **out, long *outlen);

static const Apply applytab[] = {
	{ "space-between", apply_space_between },
	{ 0, 0 },
};

