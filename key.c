#include <errno.h>
#include <sys/select.h>
#include <string.h>
#include <unistd.h>

#include "eek.h"
#include "util.h"

static unsigned char pushbuf[8];
static int pushn;

/*
 * readbyte reads one byte from fd (or from the internal pushback buffer).
 *
 * Parameters:
 *  - fd: file descriptor to read from.
 *  - b: output byte.
 *
 * Returns:
 *  - 0 on success (b is set).
 *  - -1 on EOF.
 */
static int
readbyte(int fd, unsigned char *b)
{
	long n;

	if (pushn > 0) {
		*b = pushbuf[--pushn];
		return 0;
	}

	for (;;) {
		n = read(fd, b, 1);
		if (n == 0)
			return -1;
		if (n > 0)
			break;
		if (errno == EINTR)
			continue;
		die("read: %s", strerror(errno));
	}
	return 0;
}

/*
 * unreadbyte pushes a byte back into the internal pushback buffer.
 *
 * Parameters:
 *  - b: byte to push back.
 *
 * Returns:
 *  - void.
 */
static void
unreadbyte(unsigned char b)
{
	if (pushn < (int)(sizeof pushbuf / sizeof pushbuf[0]))
		pushbuf[pushn++] = b;
}

/*
 * readbyte_timeout reads one byte from fd, waiting up to timeoutms.
 *
 * Parameters:
 *  - fd: file descriptor.
 *  - b: output byte.
 *  - timeoutms: timeout in milliseconds.
 *
 * Returns:
 *  - 0 on success (b is set).
 *  - 1 if interrupted by EINTR (treated as non-fatal by callers).
 *  - 2 on timeout.
 *  - -1 on EOF.
 */
static int
readbyte_timeout(int fd, unsigned char *b, int timeoutms)
{
	fd_set rfds;
	struct timeval tv;
	int r;
	int rc;

	/*
	 * Terminals encode special keys (arrows, Home/End, etc) as multi-byte
	 * escape sequences that start with ESC. The plain Esc key is just ESC.
	 * Without a short timeout, reading ESC would block waiting for the next
	 * byte of a sequence and a lone Esc could appear to require a second key
	 * press before it takes effect (e.g. Insert->Normal mode switch).
	 */

	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = timeoutms / 1000;
		tv.tv_usec = (timeoutms % 1000) * 1000;
		r = select(fd + 1, &rfds, nil, nil, &tv);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			die("select: %s", strerror(errno));
		}
		if (r == 0)
			return 2;
		break;
	}

	rc = readbyte(fd, b);
	if (rc == 1)
		return 0;
	return rc;
}

/*
 * keyread reads and decodes one key event from the terminal.
 *
 * Parameters:
 *  - t: terminal state (uses t->fdin).
 *  - k: output key event.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on EOF.
 */
int
keyread(Term *t, Key *k)
{
	unsigned char b, b1, b2, b3;
	long r;
	int rc;

	k->kind = Keynone;
	k->value = 0;

	rc = readbyte(t->fdin, &b);
	if (rc < 0)
		return -1;

	if (b == 0x1b) {
		rc = readbyte_timeout(t->fdin, &b1, 25);
		if (rc == 2) {
			k->kind = Keyesc;
			return 0;
		}
		if (rc < 0) {
			k->kind = Keyesc;
			return 0;
		}
		if (b1 != '[') {
			unreadbyte(b1);
			k->kind = Keyesc;
			return 0;
		}
		rc = readbyte_timeout(t->fdin, &b2, 25);
		if (rc == 2) {
			unreadbyte('[');
			k->kind = Keyesc;
			return 0;
		}
		if (rc < 0) {
			unreadbyte('[');
			k->kind = Keyesc;
			return 0;
		}
		switch (b2) {
		case 'A': k->kind = Keyup; return 0;
		case 'B': k->kind = Keydown; return 0;
		case 'C': k->kind = Keyright; return 0;
		case 'D': k->kind = Keyleft; return 0;
		case 'H': k->kind = Keyhome; return 0;
		case 'F': k->kind = Keyend; return 0;
		default:
			unreadbyte(b2);
			unreadbyte('[');
			k->kind = Keyesc;
			return 0;
		}
	}

	if (b == '\r') {
		k->kind = Keyenter;
		return 0;
	}
	if (b == 0x7f) {
		k->kind = Keybackspace;
		return 0;
	}

	if (b < 0x80) {
		k->kind = Keyrune;
		k->value = (unsigned char)b;
		return 0;
	}

	r = 0xfffd;
	if ((b & 0xe0) == 0xc0) {
		rc = readbyte(t->fdin, &b1);
		if (rc < 0)
			return -1;
		if ((b1 & 0xc0) == 0x80)
			r = ((b & 0x1f) << 6) | (b1 & 0x3f);
	} else if ((b & 0xf0) == 0xe0) {
		rc = readbyte(t->fdin, &b1);
		if (rc < 0)
			return -1;
		rc = readbyte(t->fdin, &b2);
		if (rc < 0)
			return -1;
		if ((b1 & 0xc0) == 0x80 && (b2 & 0xc0) == 0x80)
			r = ((b & 0x0f) << 12) | ((b1 & 0x3f) << 6) | (b2 & 0x3f);
	} else if ((b & 0xf8) == 0xf0) {
		rc = readbyte(t->fdin, &b1);
		if (rc < 0)
			return -1;
		rc = readbyte(t->fdin, &b2);
		if (rc < 0)
			return -1;
		rc = readbyte(t->fdin, &b3);
		if (rc < 0)
			return -1;
		if ((b1 & 0xc0) == 0x80 && (b2 & 0xc0) == 0x80 && (b3 & 0xc0) == 0x80)
			r = ((b & 0x07) << 18) | ((b1 & 0x3f) << 12) | ((b2 & 0x3f) << 6) | (b3 & 0x3f);
	}

	if (r > 0x10ffff || (r >= 0xd800 && r <= 0xdfff))
		r = 0xfffd;
	k->kind = Keyrune;
	k->value = r;
	return 0;
}
