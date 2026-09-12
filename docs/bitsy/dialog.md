# Dialog scripting

Bitsy dialog is a small programming language, not just displayed text. citsy evaluates `DLG` and `END` source at runtime (`src/dialog/script.cpp`) against the engine's variables and inventory.

This is a compatibility subset of the Bitsy 7+ script language.

---

## Pages

| Source | Result |
|---|---|
| `"Hello!"` then `"Bye."` on the next line | Two pages |
| `{p}` / `{pg}` | New page |
| `{br}` | Newline on the current page |
| Blank line inside a list item or `"""` block | New page |
| Text that does not fit the 104×32 box | Extra **screens** (`Ok` advances) |

Unquoted text (common in lists) is one page unless a blank line or `{p}` splits it. A line that is too long **wraps whole words** onto the next row of the same box. Words are never split. If the box is full, leftover words start the next screen. `{br}` is an explicit newline.

---

## Textbox look

The dialog box is always a black rectangle (`kTextboxBlack`). Glyphs are white (`kTextboxWhite`) unless a colour tag or `{rbw}` is active. While dialog is open the engine installs those colours (and 16 rainbow hues at `kTextboxRainbow0`) into the palette passed to `present()`, so hosts can index the textbox buffer without a special case. Rainbow slots use Bitsy's three out-of-phase sines (`sin(phase)*127+128`).

Letters type in one by one (50 ms per printable glyph; spaces and newlines are free). Word wrap is computed against the full page so later characters do not reflow as they appear. The continue arrow shows only after the page is fully revealed; `Ok` (or any action button) skips remaining typing, then a second press advances.

## Text effects

Tags combine: `{wvy}{rbw}hello` is both wavy and rainbow. A close tag (`{/wvy}`) clears only that bit.

| Tag | Effect |
|---|---|
| `{wvy}` / `{/wvy}` | Vertical sine offset (animated) |
| `{shk}` / `{/shk}` | Jitter offset (animated) |
| `{rbw}` / `{/rbw}` | Rainbow ink per character (Bitsy sine RGB) |
| `{clr}` / `{clr1}` / `{clr2}` / `{clr3}` | Ink uses palette index 1, 2, or 3 |
| `{clr n}` | Ink uses palette index `n` |

Rainbow wins over `{clr}` when both are on. `{rbw}` picks one hue per character from Bitsy's `(time_ms / 100) - col * 0.5`.

---

## Values

Variables and expressions are **numbers** or **strings**. Missing variables read as `0`. `true` / `false` are `1` / `0`.

```
VAR score
0

VAR name
ada
```

Inspect at runtime with `Engine::variable_value("score")`.

---

## Functions

| Tag | Effect |
|---|---|
| `{print expr}` / `{say expr}` | Write the value into the current page |
| `{name = expr}` | Assign a variable (`+ - * /` and string `+`) |
| `{item "id-or-name"}` | Item count (id or `NAME`) |
| `{item "id-or-name" n}` | Set item count to `n` |
| `{end}` | Stop the game after this dialog closes |
| `{exit "room" x y}` | Warp after this dialog closes |

`{item "key" {{item "key"} - 1}}` decrements. Walking onto a room item increments inventory immediately, then plays the item dialog, then removes it from the room.

Comparisons: `== != < > <= >=`.

---

## Lists

```
{sequence
  - first visit
  - later visits (sticks on last)
}

{cycle
  - a
  - b
}

{shuffle
  - one
  - two
}

{
  - {item "key"} > 0 ?
    "You have the key."
  - else ?
    "The door is locked."
}
```

- **sequence** — next arm each time the dialog runs; stays on the last
- **cycle** — wraps around
- **shuffle** — random order, no repeats until every arm has played
- **branch** (bare `{`) — first arm whose condition is true; `else` always matches

Lists nest. A blank line inside an arm is a page break.

---

## Endings

Room sub-key `END <id> x,y` is a trigger tile. Stepping on it plays the matching top-level `END` text, then `is_running()` becomes false.

`{end}` inside a sprite or item dialog does the same after the box closes, without showing a named ending.

---

## Engine inspectors

```cpp
engine.item_count("key");        // by id or NAME
engine.variable_value("score");  // current value as text
engine.is_running();             // false after an ending
```
