# Roadmap

Development is staged toward practical compatibility with games made in current Bitsy versions.

## Phases

- [x] **Phase 0 — Foundation**
    - [x] `.bitsy` parser and in-memory `Game` model
    - [x] `Host` interface and `MockHost`
    - [x] Unit tests against sample game files

- [x] **Phase 1 — Playable core**
    - [x] Avatar movement and wall collision
    - [x] Room rendering into map buffers
    - [x] Sprite and item drawing
    - [x] Exit transitions between rooms
    - [x] Basic dialog (linear text)

- [x] **Phase 2 — Scripting & state**
    - [x] Variables (numbers and strings)
    - [x] Conditional dialog branches
    - [x] Item give/take and inventory
    - [x] Endings

- [x] **Phase 3 — Polish**
    - [x] Animation timing
    - [x] Transition effects (video mode)
    - [x] Custom fonts
    - [x] Sound channel output
    - [x] RTL text direction

- [ ] **Phase 4 — Backends**
    - [ ] raylib reference player
    - [ ] 32blit backend (community contribution welcome)

Compatibility fixtures are drawn from the [Bitsy community](https://bitsy.org) and existing open-source parsers such as [bitsy-parser](https://docs.rs/bitsy-parser).

## File format compatibility

citsy targets import of standard `.bitsy` files exported from the editor.

| Feature | Target support |
|---|---|
| `# BITSY VERSION` header | Yes |
| Comma-separated room format (`! ROOM_FORMAT`) | Yes |
| Legacy contiguous room format (single-char tile IDs) | Yes |
| `SET` vs `ROOM` (historical naming) | Yes |
| Multi-frame animation | Yes |
| Extended palettes (`COL n`) | Yes |
| Variables and dialog scripting | Yes |
| Custom fonts (`FONT` / `.bitsyfont`) | Yes |
| `TEXT_DIRECTION RTL` | Yes |
| Sound (engine generates channel params; host plays audio) | Yes |

Exact version targets are covered by fixtures in `tests/data/`. The [data model](bitsy/data-model.md#compatibility-notes) page has a parser-vs-simulation breakdown.

## Non-goals

- **Editor or authoring tools** — use [bitsy.org](https://bitsy.org) or compatible editors.
- **HTML export** — citsy produces frames and state, not a self-contained web page.
- **JavaScript embedding** — the reference engine runs on JS; citsy reimplements engine logic in native C++.
- **Full feature parity on day one** — compatibility is incremental.

## References

- [Bitsy](https://bitsy.org) — official editor and website
- [adamledoux/bitsy](https://codeberg.org/adamledoux/bitsy) — reference engine and editor source (JavaScript)
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host system layer specification
- [bitsybox](https://github.com/le-doux/bitsybox) — desktop runtime with SDL host implementation
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust parser useful for cross-checking file format behavior
- [Bitsy Wiki / FAQ](https://bitsy.fandom.com/wiki/FAQ) — community documentation on variables, colors, and data format
