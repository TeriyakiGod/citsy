# citsy documentation

The documentation website is published at **https://teriyakigod.github.io/citsy/**.

Supplementary documentation for the citsy project. For a high-level overview, design goals, and roadmap, see the [main README](../README.md).

| Document | Contents |
|---|---|
| [Getting started](getting-started.md) | Clone, build, and embed the engine |
| [Architecture](architecture.md) | Layering, modules, data flow, memory blocks, and the Host interface |
| [Host API](host.md) | Buttons, buffers, `present()`, MockHost |
| [Tools & build](tools.md) | CMake options, compilers, dependencies, and build targets |
| [Testing](testing.md) | Running tests, writing new tests, fixtures, and MockHost |
| [Roadmap](roadmap.md) | Phases, compatibility, and non-goals |

### Bitsy engine reference

| Document | Contents |
|---|---|
| [Bitsy overview](bitsy/index.md) | Index for format and engine reference docs |
| [Data model](bitsy/data-model.md) | Entity types, properties, file syntax, and C++ struct mapping |
| [Dialog scripting](bitsy/dialog.md) | Variables, lists, inventory, `{end}` / `{exit}` |

For agent-oriented conventions (boundaries, module placement, checklist), see [AGENTS.md](../AGENTS.md).
