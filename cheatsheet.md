# eek cheat sheet

This is a quick reference for **eek**, a minimal vi-like editor.

Notes:

- eek is **modal**: NORMAL / INSERT / VISUAL / command-line (`:` and `/`).
- Many NORMAL-mode commands accept **counts** (e.g. `3j`, `12G`, `4dd`, `d3w`, `3f.`).
- Cursor positions are byte offsets in UTF-8 text; movement is UTF-8 aware.
- Long lines are supported: eek will **horizontally scroll** as needed to keep the cursor visible.

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
- Character-wise VISUAL: `v`
- Linewise VISUAL: `V`
- Block/column (vertical) VISUAL: `Ctrl-v`
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

Page navigation:

- Page down: `)`
- Page up: `(`
- With count: `n)` / `n(` (example: `3)`)

Find on current line:

- Find next character on the line: `f{char}` (example: `f.` / `f,`)
- Find nth occurrence: `nf{char}` (example: `3f.`)
- Find previous character on the line: `F{char}`
- Find until character (forward/backward): `t{char}` / `T{char}`

Operator + find/until (current line):

- Delete until/through (forward): `dt{char}` / `df{char}`
- Delete until/through (backward): `dT{char}` / `dF{char}`
- Change until/through (forward): `ct{char}` / `cf{char}`
- Change until/through (backward): `cT{char}` / `cF{char}`
- Yank until/through (forward): `yt{char}` / `yf{char}`
- Yank until/through (backward): `yT{char}` / `yF{char}`

---

## Insert / change / open lines

Enter INSERT:

- Insert at cursor: `i`
- Append (insert after cursor): `a`
- Append at end of line: `A`
- Open new line below (and enter INSERT): `o`
- Open new line above (and enter INSERT): `O`

Change:

- Substitute character(s): `s` / `ns` (deletes and enters INSERT)
- Replace character(s): `r{char}` (count-aware)
- Change to end-of-line: `C` (also yanks the deleted text)
- Delete to end-of-line: `D` (also yanks the deleted text)
- Substitute line(s): `S` / `nS` (like `cc`)
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

In VISUAL:

- `p` / `P` replace the selection (delete selection, then paste; does not overwrite the yank buffer).

Notes:

- Deletes generally **yank** into the same register, so `p` can paste what you just deleted.

---

## VISUAL mode

- Character-wise VISUAL: `v`
- Linewise VISUAL: `V`
- Block/column (vertical) VISUAL: `Ctrl-v`
- While in VISUAL:
  - Yank selection: `y`
  - Delete selection (and yank): `d` (also `D`)
  - Change selection (and yank, then enter INSERT): `c` (also `C`, `s` / `S`)
  - Paste replaces selection: `p` / `P`

Block/column VISUAL extras:

- Block insert: select a block with `Ctrl-v`, press `I`, type text, then `Esc`.
  - The typed text is inserted on every selected line at the block's left edge.

Delimiter text objects in VISUAL:

- Select inside delimiter: `i{char}` (example: `i(`, `i{`, `i"`, `i'`)

Word text objects in VISUAL:

- Select inside word: `iw` (example: `viw`)

Command-line with VISUAL selection:

- Press `:` in VISUAL to open the command line while keeping the selection highlighted.
- `:s/...` without an explicit address applies to the selected **line range**.

---

## Undo

- Undo last change: `u`

---

## Repeat

- Repeat last change: `.` (also works for VISUAL edits)

---

## Search

- Start search prompt: `/pattern` then `Enter`
- Search for word under cursor: `*`
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

Tabs:

- New tab (optionally open a file): `:tabnew [file]`
- Next/previous tab: `:tabn` / `:tabp` (aliases: `:tabnext` / `:tabprevious`)
- First/last tab: `:tabfirst` / `:tablast`
- Close current tab: `:tabclose` (alias: `:tabc`)
- Force-close current tab: `:tabclose!`
- Close all other tabs: `:tabonly` (force: `:tabonly!`)
- Move current tab to position *n* (1-based): `:tabm n` (alias: `:tabmove n`)
- List tabs: `:tabs`

NORMAL mode tab keys:

- Next tab: `gt`
- Previous tab: `gT`
- Go to tab *n*: `ngt` (example: `3gt`)

NORMAL mode Space shortcuts:

- New tab: `Space` then `n`
- First tab: `Space` then `h`
- Last tab: `Space` then `l`
- Previous tab: `Space` then `j`
- Next tab: `Space` then `k`

Write / quit:

- Write: `:w`
- Write as: `:w filename`
- Quit: `:q`
- Force quit: `:q!`
- Write and quit: `:wq`

Edit file:

- Edit/open: `:e filename` (alias: `:edit filename`)
- Force edit (discard changes): `:e! filename`

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

Run shell command (`:run`):

- `:run <command>` executes `<command>` (via the shell) and inserts **stdout** into the buffer.
- Insertion point: at the cursor position in the current line.
- Multi-line stdout is inserted as multiple lines; the original tail of the line is preserved after the inserted output.

Remaps (`:map`, `:unmap`):

- `:map <lhs> <rhs>` maps a **single character** `<lhs>` to an injected key sequence `<rhs>`.
  - Applies in NORMAL and VISUAL.
  - `<rhs>` is treated as UTF-8 text (runes) and is inserted as if you typed it.
  - The injected keys are **non-remappable** (prevents recursive maps).
- `:unmap <lhs>` removes a mapping.

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
