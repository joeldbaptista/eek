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
	- Long lines are supported via automatic horizontal scrolling.

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

## Commands

eek implements a small subset of vi/ex style command-line commands.

## VISUAL selection

eek supports three selection styles:

- Character-wise selection: `v`
- Linewise selection: `V`
- Block/column (vertical) selection: `Ctrl-v`

While in VISUAL, common operators apply to the selection:

- `y` yank
- `d` / `D` delete (also yanks)
- `c` / `C` change (delete + enter INSERT)

VISUAL text objects:

- `iw` select inside word (`viw`)

Pasting in VISUAL replaces the selection:

- `p` / `P` delete the selection (without overwriting the yank buffer) and then paste.

Block/column insert:

- In block VISUAL (`Ctrl-v`), press `I` to enter a block insert.
- Type the text to insert, then press `Esc`.
- The inserted text is applied to every line in the selected block at the block's left edge.
- Newlines are not supported during block insert.

### Windows (`:split`, `:vsplit`)

- `:split` creates a horizontal split.
- `:vsplit` creates a vertical split.
- `Ctrl-w` then `w` cycles the active window.
- When multiple windows exist, `:q` closes the active window.

### Tabs (`:tabnew`, `gt`)

- `:tabnew [file]` opens a new tab (optionally loading a file into that tab).
- `:tabn` / `:tabp` switch to next/previous tab (aliases: `:tabnext` / `:tabprevious`).
- `gt` / `gT` switch to next/previous tab in NORMAL mode.
- `:tabclose` closes the current tab (use `:tabclose!` to force-close if dirty).
- `:tabonly` closes all other tabs (use `:tabonly!` to force-close if dirty).
- When only one window exists, `:q` closes the current tab if multiple tabs exist.

### Edit file (`:e`)

- `:e filename` opens `filename` in the current tab.
- If `filename` does not exist, `:e filename` opens an empty buffer and sets the name.
- Use `:e! filename` to discard unsaved changes in the current tab.

### Run a shell command (`:run`)

- `:run <command>` executes `<command>` (via the shell) and inserts its **stdout** into the buffer.
- Output is inserted at the cursor position; multi-line output becomes multiple lines.

### Mappings (`:map`, `:unmap`)

eek supports a minimal mapping mechanism intended as a foundation for richer command systems.

- `:map <lhs> <rhs>` maps a single UTF-8 character `<lhs>` to an injected key sequence `<rhs>`.
	- Applies in NORMAL and VISUAL.
	- The injected keys are non-remappable to avoid recursive mappings.
- `:unmap <lhs>` removes the mapping.

### Paging (`(`, `)`)

In NORMAL mode:

- `)` pages down by one window height.
- `(` pages up by one window height.
- Counts work: `n)` / `n(` (example: `3)` pages down 3 pages).

### Substitute (`:s`)

Syntax:

- `:[address]s/old_text/new_text/`
- `:[addr1],[addr2]s/old_text/new_text/`
- `:%s/old_text/new_text/`

Notes:

- `old_text` is a POSIX extended regular expression.
- Add `g` at the end to replace every match on each addressed line: `.../g`.

Address forms:

- `.` current line
- `n` line number `n`
- `.+m` / `.-m` current line plus/minus `m`
- `$` last line
- `/string/` a line that contains `string` (searches forward with wrap)
- `%` entire file
- `[addr1],[addr2]` a range (inclusive)

Examples:

- Replace the first `Banana` with `Kumquat` in each of 11 lines starting at the current line:
	- `:.,.+10s/Banana/Kumquat/`
- Replace every occurrence of `apple` with `pear` in the entire file:
	- `:%s/apple/pear/g`
- Remove the last character from every line (useful to strip CR when lines end in `^M`):
	- `:%s/.$//`

## Comments

eek follows the suckless approach: comments should be sparse and useful. The code should be readable without narration; when something *isn't* obvious (or has sharp edges), we write a small, consistent comment instead of a large explanation.

### General rules

- Prefer code clarity first.
	- If a small refactor can remove the need for a comment, do that instead.
	- Avoid comments that restate what the code already says.

- Use comments to capture intent and constraints.
	- Non-obvious invariants (cursor is a byte offset; buffer stores UTF-8 bytes).
	- Any “why” that would not survive a superficial rewrite.
	- Protocol/terminal quirks (escape sequences, timeouts, etc.).

- Keep comments short.
	- If a topic needs paragraphs, it probably belongs in this README (or a doc) instead.

### Function header blocks

Most functions use a small block comment directly above the definition. Keep it compact and predictable:

- One sentence for what it does (and any important side effects).
- A `Parameters:` list when it’s not trivial.
- A `Returns:` line describing success/failure.

Example shape:

```c
/*
 * thing does X and updates Y.
 *
 * Parameters:
 *  ...
 *
 * Returns:
 *  0 on success, -1 on failure.
 */
```

### Struct members

Each struct member gets a short comment if its purpose is not self-evident. Prefer describing units/meaning (e.g. “byte offset”, “line index”, “capacity in bytes”) over repeating the name.

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
	long coloff; /* horizontal scroll offset */
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
- Horizontal scroll position (`coloff`).
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

## Future developments

eek is intentionally minimal, but there are several improvements that fit the “small vi-clone” goal and can be implemented without turning the project into a full Vim replacement. This section is a roadmap, not a compatibility promise.

### Editing features

- Redo (`Ctrl-r`)
	- With snapshot-based undo, redo is typically implemented as a second stack.
	- New edits should clear the redo stack (like most editors).

- Indentation
	- `>>` / `<<` (and counts) for simple line indentation shifting.
	- Keep it compile-time configurable (tabs vs spaces) to stay suckless-style.

### Motions and text objects

- Better line navigation: `^` (first non-blank), `H`/`M`/`L` (top/middle/bottom of the viewport).
- Paragraph movement: `{` and `}` (blank-line separated).
- Word text objects: `iw` / `aw` so `diw`, `ciw`, `yiw` become available.

### Search

- Search is currently plain forward search (`/pattern`) plus repeats (`n`/`N`).
- Potential improvements that still keep things small:
	- Backward search prompt (`?pattern`).
	- Optional match highlighting for the current match only (not a full multi-match UI).
	- Configurable wrap behavior (wrapscan on/off).
	- Whole-word search as a simple toggle.

### Internal improvements

- Undo performance
	- Current undo is full-buffer snapshots, which is simple but $O(\text{file size})$ per snapshot.
	- A future improvement is a diff/inverse-operation log to reduce memory and time for large files.

- Robustness and correctness
	- More edge-case hardening around UTF-8 boundaries and multi-line operations.
	- More consistent error propagation on allocation failures.

- Test harness
	- Add a small non-interactive test layer around `buf.c` primitives (line insert/delete, load/save, copies).
	- This helps keep refactors safe without requiring full terminal-driven integration tests.

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
