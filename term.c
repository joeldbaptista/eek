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

static void
onwinch(int sig)
{
	(void)sig;
	needresize = 1;
}

static void
onexitrestore(void)
{
	termrestore();
}

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

int
termresized(void)
{
	int r;

	r = needresize;
	needresize = 0;
	return r;
}

void
termrestore(void)
{
	if (haveoldtio)
		(void)tcsetattr(0, TCSAFLUSH, &oldtio);
}

void
termgetwinsz(Term *t)
{
	struct winsize ws;

	if (ioctl(t->fdout, TIOCGWINSZ, &ws) < 0)
		die("ioctl(TIOCGWINSZ): %s", strerror(errno));
	t->row = ws.ws_row;
	t->col = ws.ws_col;
}

void
termclear(Term *t)
{
	(void)t;
	write(1, "\x1b[2J", 4);
	write(1, "\x1b[H", 3);
}

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

void
termflush(Term *t)
{
	(void)t;
}
