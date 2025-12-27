# eek cheat sheet

This is a quick reference for **eek**, a minimal vi-like editor.

Notes:

- eek is **modal**: NORMAL / INSERT / VISUAL / command-line (`:` and `/`).
- Many NORMAL-mode commands accept **counts** (e.g. `3j`, `12G`, `4dd`, `d3w`, `3f.`).
- Cursor positions are byte offsets in UTF-8 text; movement is UTF-8 aware.

---

## Start / exit

- Run: `./eek [file]`
- Quit:
  - `:q` (fails if there are unsaved changes)
  - `:q!` (force quit)
  - `q` (quick quit key)

---

## Modes

- NORMAL: default mode (navigation + operators)
- INSERT: insert text
- VISUAL: select text (highlighted)
- Command-line: `:` for ex commands, `/` for search prompt

Mode switching:

- Enter INSERT: `i`, `a`, `A`, `o`, `O`
- Leave INSERT: `Esc`
- Toggle VISUAL: `v`
- Enter command-line:
  - `:` (ex commands)
  - `/` (search prompt)
- Leave command-line: `Esc`

---

## Movement (NORMAL)

Basic movement:

- Left/Down/Up/Right: `h` `j` `k` `l`

Line positions:

- Start of line: `0`
- End of line: `$`

Word movement:

- Next word: `w`
- Previous word: `b`

File navigation:

- Go to last line: `G`
- Go to line *n*: `nG` (example: `12G`)
- Go to first line: `gg`
- Go to line *n* with `gg`: `ngg` (example: `12gg`)

Find on current line:

- Find next character on the line: `f{char}` (example: `f.` / `f,`)
- Find nth occurrence: `nf{char}` (example: `3f.`)

---

## Insert / change / open lines

Enter INSERT:

- Insert at cursor: `i`
- Append (insert after cursor): `a`
- Append at end of line: `A`
- Open new line below (and enter INSERT): `o`
- Open new line above (and enter INSERT): `O`

Change:

- Change to end-of-line: `C` (also yanks the deleted text)
- Change inside delimiter pair: `ci{char}` (example: `ci(`, `ci"`, `ci'`)

---

## Delete

Character/line deletes:

- Delete character(s) under cursor (yanks): `x` / `nx`
- Delete line(s) (yanks): `dd` / `ndd`

Word deletes:

- Delete word(s): `dw` / `d{n}w` (example: `d3w`)
- Delete to end of word(s): `de` / `d{n}e`

Text objects (delimiter pairs):

- Delete inside delimiter pair: `di{char}` (example: `di(`, `di{`, `di"`, `di'`)

---

## Yank / paste

Yank:

- Yank line(s): `yy` / `nyy`

Paste:

- Paste after cursor / below line: `p`
- Paste before cursor / above line: `P`

Notes:

- Deletes generally **yank** into the same register, so `p` can paste what you just deleted.

---

## VISUAL mode

- Enter/exit VISUAL: `v`
- While in VISUAL:
  - Yank selection: `y`
  - Delete selection (and yank): `d`

Delimiter text objects in VISUAL:

- Select inside delimiter: `i{char}` (example: `i(`, `i{`, `i"`, `i'`)

Command-line with VISUAL selection:

- Press `:` in VISUAL to open the command line while keeping the selection highlighted.
- `:s/...` without an explicit address applies to the selected **line range**.

---

## Undo

- Undo last change: `u`

---

## Search

- Start search prompt: `/pattern` then `Enter`
- Repeat search:
  - Next match: `n`
  - Previous match: `N`

Notes:

- Search wraps (wrapscan-like).

---

## Ex commands (`:`)

Windows:

- Horizontal split: `:split`
- Vertical split: `:vsplit`
- Move between splits: `Ctrl-h` `Ctrl-j` `Ctrl-k` `Ctrl-l`
- Cycle active window: `Ctrl-w` then `w`
- `:q` closes the active window when split

Write / quit:

- Write: `:w`
- Write as: `:w filename`
- Quit: `:q`
- Force quit: `:q!`
- Write and quit: `:wq`

Read file into buffer:

- Read file after current line: `:r filename` (alias: `:read filename`)

Options (`:set`):

- Show current options: `:set`
- Line numbers:
  - On: `:set numbers` (aliases: `number`, `nu`)
  - Off: `:set nonumbers` (aliases: `nonumber`, `nonu`)
- Relative numbers:
  - On: `:set relativenumbers` (aliases: `relativenumber`, `rnu`)
  - Off: `:set norelativenumbers` (aliases: `norelativenumber`, `nornu`)
- Syntax highlighting:
  - On: `:set syntax` (alias: `syn`)
  - Off: `:set nosyntax` (alias: `nosyn`)

---

## Substitute (`:s`)

Syntax:

- `:[address]s/old_text/new_text/`
- `:[addr1],[addr2]s/old_text/new_text/`
- `:%s/old_text/new_text/`

Flags:

- `g` — replace all matches on each addressed line.

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
- Remove the last character from every line:
  - `:%s/.$//`

VISUAL interaction:

- In VISUAL mode, `:` keeps the selection highlighted.
- A substitute without an explicit address (e.g. `:s/a/b/g`) applies to the selected **line range**.
