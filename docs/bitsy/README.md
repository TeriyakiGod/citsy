# Bitsy engine reference

The documentation website is at **https://teriyakigod.github.io/citsy/bitsy/**.

Documentation for the Bitsy game format and how citsy represents it. These guides describe the **data model** and file format that the engine parses and simulates — not the Bitsy editor UI.

| Document | Contents |
|---|---|
| [Data model](data-model.md) | All entity types, properties, relationships, and C++ struct mapping |
| [Dialog scripting](dialog.md) | Variables, lists, inventory, `{end}` / `{exit}` |

### Planned sections

Future guides will cover simulation rules, rendering modes, and compatibility notes as those features land in citsy.

### External references

- [Bitsy](https://bitsy.org) — official editor and website
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host layer contract used by bitsybox and the web runtime
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust parser for cross-checking format behavior
- [Bitsy Wiki / FAQ](https://bitsy.fandom.com/wiki/FAQ) — community documentation on variables, colors, and scripting
