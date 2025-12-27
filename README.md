# eek

A suckless vi-clone.

## Design

eek is a small vi-like editor built in the suckless spirit: keep the system simple, keep the surface area small, and make it easy to understand and modify. The goal is not to be a feature-complete Vim replacement; the goal is a minimal vi-clone with a codebase that you can realistically read end-to-end and confidently change.

Design principles:

- Small, inspectable codebase
	- Prefer straightforward code paths over clever abstractions.
	- Keep data structures simple and explicit.
	- Make “what happens on a keypress” easy to follow.

- Suckless-style configuration
	- Configuration lives in a single `config.h` (copied from `config.def.h`).
	- The intent is to compile your preferences in, not build a plugin system.
	- If you want a behavior change, it should be feasible to add it directly.

- No third-party dependencies
	- eek targets a POSIX-ish environment and uses the standard C library.
	- Terminal control is done via termios and ANSI escape sequences.

- Stick to vi’s core model
	- Modal editing (NORMAL / INSERT) with a small command-line mode for `:`.
	- Motions and operators are implemented directly, prioritizing the basics.
	- Counts are first-class: motions and operators accept numeric prefixes.

- Text as bytes, editing as text
	- The buffer stores UTF-8 bytes.
	- Movement and rendering are UTF-8 aware (cursor steps by codepoint boundaries).
	- The editor aims to behave sensibly on real-world UTF-8 files without pulling in heavy Unicode machinery.

Non-goals:

- Full Vim compatibility.
- A large configuration language, plugin system, or runtime scripting.
- A dependency stack beyond “a C compiler and a terminal”.

## Build

- Requirements: a C compiler and a POSIX-ish system (Linux, *BSD).

```sh
cp config.def.h config.h
make
./eek
```

## Install

```sh
sudo make install
```

## Configure

Configuration lives in `config.h`.

- Start from `config.def.h`.
- Edit `config.h` and rebuild.

## Internals

This section describes the main data structures in the editor and how undo works.

### Text storage (buffer model)

eek keeps the edited text in an in-memory buffer called `Buf` (see `buf.h` / `buf.c`).

At a high level:

- A file is represented as an array of lines.
- Each line is a growable byte array.
- The editor stores and edits UTF-8 as raw bytes, but cursor movement and rendering step by UTF-8 codepoint boundaries.

Concretely, the data structures are:

```c
struct Line {
	char *s;   /* UTF-8 bytes */
	long n;    /* number of bytes currently used */
	long cap;  /* allocated capacity in bytes */
};

struct Buf {
	Line *line;   /* dynamic array of lines */
	long nline;   /* number of lines */
	long cap;     /* allocated capacity (number of Line slots) */
};
```

Key properties:

- The buffer is line-oriented for simplicity (like many small editors): newline boundaries are represented by separate `Line` entries, not by embedding `\n` bytes into `Line.s`.
- `bufinit()` ensures there is always at least one line (even for an empty file).
- Inserting/deleting bytes within a line uses `lineinsert()` and `linedelrange()`.
- Inserting/deleting whole lines uses `bufinsertline()` and `bufdelline()`.
- Cursor positions (`cx`, `cy`) are stored in *byte offsets*:
	- `cy` is the line index in `Buf.line[]`.
	- `cx` is the byte offset within `Line.s`.
	- Helpers like `nextutf8()` / `prevutf8()` ensure the cursor lands on UTF-8 codepoint boundaries when moving.

This “UTF-8 bytes, codepoint-aware movement” approach keeps the storage and editing primitives small, while still behaving sanely on UTF-8 text.

### Undo (snapshot stack)

eek implements undo as a simple snapshot stack.

Each undo entry stores a *deep copy* of the entire buffer along with enough editor state to restore a consistent view:

```c
struct Undo {
	Buf b;       /* deep-copied buffer */
	long cx;     /* cursor x (byte offset) */
	long cy;     /* cursor y (line index) */
	long rowoff; /* vertical scroll offset */
	long dirty;  /* whether the buffer is considered modified */
};
```

The editor keeps these entries in a dynamic array (a stack):

- `undo` / `nundo` / `capundo` live in the main editor state (`struct Eek`).
- New snapshots are appended; undo (`u`) pops the last snapshot and restores it.

#### What gets snapshotted

Undo captures:

- The full text contents (`Buf`, all `Line` data).
- Cursor position (`cx`, `cy`).
- Scroll position (`rowoff`).
- The dirty flag (`dirty`).

Undo does *not* capture:

- The yank register contents.
- Configuration (`:set` options) or syntax highlighting state.
- Any state that is not required to recreate the text + view.

#### How snapshots are created (when undopush happens)

Snapshots are taken *before* a mutation.

Practically, the core editing helpers call an internal `undopush()` right at the start:

- byte insertion (`insertbytes()`)
- newline insertion (`insertnl()`)
- deletions (`delat()`, `delback()`, `delword()`, `delendword()`, `delline()`, range deletes via `delrange()`)
- line-opening commands (`openlinebelow()`, `openlineabove()`)
- linewise paste (`pastelinewise()`)

This design keeps undo correct even when multiple higher-level commands are composed of several lower-level buffer operations.

#### Grouping behavior ("one undo per INSERT")

To avoid creating an undo snapshot on every keystroke in INSERT mode, eek groups edits using a simple flag:

- On the *first* mutating edit after entering INSERT mode, `undopush()` takes a snapshot and sets `undopending = 1`.
- While `undopending` is set, further calls to `undopush()` become no-ops.
- When you leave INSERT mode (press ESC), `undopending` is cleared, so the next edit will create a new snapshot.

Effect:

- One INSERT session (typing, backspaces, newlines, etc.) is usually undone as a single unit.
- In NORMAL mode, each mutating command typically becomes a single undo step.

This is intentionally simple: it’s closer to “coarse” undo than Vim’s full undo tree, but it keeps the implementation small and predictable.

#### Limits and performance characteristics

- The undo history is capped at 128 snapshots. When the cap is exceeded, the oldest snapshot is dropped.
- Each snapshot deep-copies the whole buffer (every line’s bytes). This means:
	- Time cost per snapshot is $O(\text{file size})$.
	- Memory usage is roughly proportional to $\text{file size} \times \text{snapshots}$.

This tradeoff is deliberate: for a small editor, full snapshots are much simpler than maintaining an edit-log with inverse operations. If you routinely edit very large files, you may want to reduce the cap or switch to a diff-based undo design.

#### Redo

There is currently no redo stack. Only single-step undo via `u` is implemented.

## Code style

The code style follows Plan 9 / suckless conventions: keep formatting consistent, keep naming predictable, avoid cleverness, and prefer readability over compactness. In practice this means things like:

- C89/C99-flavored, small functions, explicit error returns.
- Tabs for indentation, spaces for alignment.
- Minimal commenting: comment intent when it isn’t obvious.

For example:

```c
void
delete(void)
{
	long c, last;

	if (cflag) {
		memset((char *) f, 0xff, sizeof f);
		while ((c = canon(&pfrom)) >= 0)
			CLEARBIT(f, c);
	} else {
		while ((c = canon(&pfrom)) >= 0)
			SETBIT(f, c);
	}
	if (sflag) {
		while ((c = canon(&pto)) >= 0)
			SETBIT(t, c);
	}

	last = 0x10000;
	while (readrune(0, &c) > 0) {
		if(!BITSET(f, c) && (c != last || !BITSET(t,c))) {
			last = c;
			writerune(1, (Rune) c);
		}
	}
	wflush(1);
}
```

## Reference

- [suckless](https://suckless.org/)
- [C Programming in Plan 9](https://doc.cat-v.org/plan_9/programming/c_programming_in_plan_9)
