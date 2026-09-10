# Dialog scripting

Bitsy dialog is a small programming language, not just displayed text. citsy evaluates `DLG` and `END` source at runtime (`src/dialog/script.cpp`) against the engine's variables and inventory.

This is a compatibility subset of the Bitsy 7+ script language. Visual effects (`{wvy}`, `{shk}`, `{clr3}`, `{printItem}`, …) parse and run as no-ops until fonts land in Phase 3.

---

## Pages

| Source | Result |
|---|---|
| `"Hello!"` then `"Bye."` on the next line | Two pages |
| `{p}` / `{pg}` | New page |
| `{br}` | Newline on the current page |
| Blank line inside a list item or `"""` block | New page |

Unquoted text (common in lists) is one page unless a blank line or `{p}` splits it.

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
