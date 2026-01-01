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
 */
static void
onwinch(int sig)
{
	(void)sig;
	needresize = 1;
}

/*
 * onexitrestore is an atexit(3) handler that restores terminal settings.
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
 */
void
terminit(Term *t)
{
	struct sigaction sa;
	struct termios tio;

	t->fdin = 0;
	t->fdout = 1;
	t->out = nil;
	t->outn = 0;
	t->outcap = 0;

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
 * termbufensure ensures that the terminal output buffer has at least need bytes.
 */
static void
termbufensure(Term *t, long need)
{
	char *p;
	long cap;

	if (t == nil)
		return;
	if (need <= t->outcap)
		return;
	cap = t->outcap > 0 ? t->outcap : 4096;
	for (; cap < need; cap *= 2) {
		if (cap > (1L << 26))
			break;
	}
	p = realloc(t->out, (size_t)cap);
	if (p == nil)
		return;
	t->out = p;
	t->outcap = cap;
}

void
termwrite(Term *t, const void *data, long n)
{
	if (t == nil || data == nil || n <= 0)
		return;
	if (t->outn + n < t->outn)
		return;
	termbufensure(t, t->outn + n);
	if (t->outcap < t->outn + n)
		return;
	memcpy(t->out + t->outn, data, (size_t)n);
	t->outn += n;
}

void
termputc(Term *t, char c)
{
	termwrite(t, &c, 1);
}

void
termrepeat(Term *t, char c, int n)
{
	char buf[64];
	int chunk;
	int i;

	if (n <= 0)
		return;
	for (i = 0; i < (int)sizeof buf; i++)
		buf[i] = c;
	for (; n > 0; ) {
		chunk = n > (int)sizeof buf ? (int)sizeof buf : n;
		termwrite(t, buf, chunk);
		n -= chunk;
	}
}

/*
 * termresized reports whether a SIGWINCH has occurred since the last call.
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
 */
void
termclear(Term *t)
{
	termwrite(t, "\x1b[2J", 4);
	termwrite(t, "\x1b[H", 3);
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

	n = snprintf(buf, sizeof buf, "\x1b[%d;%dH", r + 1, c + 1);
	if (n > 0)
		termwrite(t, buf, n);
}

/*
 * termflush is a placeholder for buffered terminal backends.
 *
 * Parameters:
 *  t: terminal state (currently unused).
 */
void
termflush(Term *t)
{
	long off;
	ssize_t w;

	if (t == nil)
		return;
	if (t->out == nil || t->outn <= 0) {
		t->outn = 0;
		return;
	}
	off = 0;
	for (; off < t->outn; ) {
		w = write(t->fdout, t->out + off, (size_t)(t->outn - off));
		if (w <= 0)
			break;
		off += (long)w;
	}
	/* Drop the frame even if partial; next draw will repaint anyway. */
	t->outn = 0;
}
