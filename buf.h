#include <stddef.h>

typedef struct Line Line;
typedef struct Buf Buf;

struct Line {
	char *s;    /* Backing storage (raw bytes, typically UTF-8). */
	size_t n;     /* Number of bytes in the line (excluding the gap). */
	size_t cap;   /* Allocated capacity of s in bytes. */
	size_t start; /* Gap start index in s (bytes). */
	size_t end;   /* Gap end index in s (bytes). */
};

struct Buf {
	Line *line;  /* Dynamic array of lines. */
	size_t nline; /* Number of lines currently in use (excluding the gap). */
	size_t cap;   /* Allocated capacity of line[] in elements (including the gap). */
	size_t start; /* Gap start index in line[] (elements). */
	size_t end;   /* Gap end index in line[] (elements). */
};

/*
 * bufinit initializes an empty buffer.
 *
 * Parameters:
 *  - b: buffer to initialize.
 *
 * Returns:
 *  - void.
 */
void bufinit(Buf *b);

/*
 * buffree releases all memory owned by the buffer and resets it.
 *
 * Parameters:
 *  - b: buffer to free.
 *
 * Returns:
 *  - void.
 */
void buffree(Buf *b);

/*
 * bufcopy deep-copies src into dst.
 *
 * Parameters:
 *  - dst: destination buffer (previous contents are freed).
 *  - src: source buffer.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure or invalid arguments.
 */
int bufcopy(Buf *dst, Buf *src);

/*
 * bufload loads a file into the buffer, replacing its previous contents.
 * Newlines are represented as separate Line entries (line text excludes '\n').
 *
 * Parameters:
 *  - b: destination buffer.
 *  - path: file path to read.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
int bufload(Buf *b, const char *path);

/*
 * bufsave writes the buffer to a file.
 * Each Line is written followed by a newline.
 *
 * Parameters:
 *  - b: buffer to write.
 *  - path: file path to write.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on failure.
 */
int bufsave(Buf *b, const char *path);

/*
 * bufgetline returns the address of the i-th line.
 *
 * Parameters:
 *  - b: buffer.
 *  - i: line index.
 *
 * Returns:
 *  - pointer to Line on success.
 *  - nil if i is out of range.
 */
Line *bufgetline(Buf *b, long i);

/*
 * buftrackgap moves the internal line-gap close to the given logical line index.
 *
 * This is a performance hint: it does not change the logical contents of the
 * buffer, but it may memmove internal storage and invalidate pointers returned
 * by bufgetline.
 */
void buftrackgap(Buf *b, long at);

/*
 * bufinsertline inserts a new line at index at.
 *
 * Parameters:
 *  - b: buffer.
 *  - at: insertion index (clamped to [0, nline]).
 *  - s: line bytes to copy (may be nil if n == 0).
 *  - n: number of bytes to copy.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on allocation failure.
 */
int bufinsertline(Buf *b, long at, const char *s, size_t n);

/*
 * bufdelline deletes the line at index at.
 *
 * Parameters:
 *  - b: buffer.
 *  - at: index to delete.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 if at is out of range.
 */
int bufdelline(Buf *b, long at);

/*
 * linebytes returns a contiguous view of the line's bytes.
 *
 * This function moves the gap to the end of the line (logical offset l->n)
 * so that the first l->n bytes of l->s are the line contents.
 *
 * Returns:
 *  - pointer to contiguous bytes (may be nil if l is nil or empty).
 */
const char *linebytes(Line *l);

/*
 * linetake replaces the contents of l with an owned byte buffer.
 *
 * Parameters:
 *  - l: line to replace.
 *  - s: heap-owned buffer (may be nil if n == 0).
 *  - n: number of bytes in s.
 *
 * Returns:
 *  - 0 on success.
 */
int linetake(Line *l, char *s, size_t n);

/*
 * lineinsert inserts bytes into a Line at a byte offset.
 *
 * Parameters:
 *  - l: line to modify.
 *  - at: byte offset in l->s.
 *  - s: bytes to insert.
 *  - n: number of bytes to insert.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on invalid offset or allocation failure.
 */
int lineinsert(Line *l, long at, const char *s, size_t n);

/*
 * linedelrange deletes a byte range from a Line.
 *
 * Parameters:
 *  - l: line to modify.
 *  - at: starting byte offset.
 *  - n: number of bytes to delete.
 *
 * Returns:
 *  - 0 on success.
 *  - -1 on invalid offset.
 */
int linedelrange(Line *l, long at, size_t n);
