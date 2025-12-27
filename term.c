#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"
#include "eek.h"
#include "util.h"

static struct termios oldtio;
static int haveoldtio;
static volatile sig_atomic_t needresize;

/*
 * onwinch handles SIGWINCH (terminal resize) by setting a flag.
 *
 * Parameters:
 *  sig: signal number (unused).
 *
 * Returns:
 *  None.
 */
static void
onwinch(int sig)
{
	(void)sig;
	needresize = 1;
}

/*
 * onexitrestore is an atexit(3) handler that restores terminal settings.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 */
static void
onexitrestore(void)
{
	termrestore();
}

/*
 * terminit puts the terminal into raw mode, installs the SIGWINCH handler,
 * and populates the initial terminal size.
 *
 * Parameters:
 *  t: terminal state to initialize.
 *
 * Returns:
 *  None.
 */
void
terminit(Term *t)
{
	struct sigaction sa;
	struct termios tio;

	t->fdin = 0;
	t->fdout = 1;

	if (tcgetattr(t->fdin, &oldtio) < 0)
		die("tcgetattr: %s", strerror(errno));
	haveoldtio = 1;
	atexit(onexitrestore);

	tio = oldtio;
	cfmakeraw(&tio);
	if (tcsetattr(t->fdin, TCSAFLUSH, &tio) < 0)
		die("tcsetattr: %s", strerror(errno));

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onwinch;
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGWINCH, &sa, nil) < 0)
		die("sigaction(SIGWINCH): %s", strerror(errno));

	termgetwinsz(t);
}

/*
 * termresized reports whether a SIGWINCH has occurred since the last call.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  1 if a resize was observed, 0 otherwise.
 */
int
termresized(void)
{
	int r;

	r = needresize;
	needresize = 0;
	return r;
}

/*
 * termrestore restores the original terminal attributes if they were saved.
 *
 * Parameters:
 *  None.
 *
 * Returns:
 *  None.
 */
void
termrestore(void)
{
	if (haveoldtio)
		(void)tcsetattr(0, TCSAFLUSH, &oldtio);
}

/*
 * termgetwinsz queries the current terminal dimensions.
 *
 * Parameters:
 *  t: terminal state to update.
 *
 * Returns:
 *  None.
 */
void
termgetwinsz(Term *t)
{
	struct winsize ws;

	if (ioctl(t->fdout, TIOCGWINSZ, &ws) < 0)
		die("ioctl(TIOCGWINSZ): %s", strerror(errno));
	t->row = ws.ws_row;
	t->col = ws.ws_col;
}

/*
 * termclear clears the screen and homes the cursor.
 *
 * Parameters:
 *  t: terminal state (currently unused).
 *
 * Returns:
 *  None.
 */
void
termclear(Term *t)
{
	(void)t;
	write(1, "\x1b[2J", 4);
	write(1, "\x1b[H", 3);
}

/*
 * termmoveto moves the cursor to (r, c) (0-based) using ANSI escape codes.
 *
 * Parameters:
 *  t: terminal state (currently unused).
 *  r: destination row (0-based).
 *  c: destination column (0-based).
 *
 * Returns:
 *  None.
 */
void
termmoveto(Term *t, int r, int c)
{
	char buf[64];
	int n;

	(void)t;
	n = snprintf(buf, sizeof buf, "\x1b[%d;%dH", r + 1, c + 1);
	if (n > 0)
		write(1, buf, (size_t)n);
}

/*
 * termflush is a placeholder for buffered terminal backends.
 *
 * Parameters:
 *  t: terminal state (currently unused).
 *
 * Returns:
 *  None.
 */
void
termflush(Term *t)
{
	(void)t;
}
