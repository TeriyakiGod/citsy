# Testing

citsy uses [Catch2 v3](https://github.com/catchorg/Catch2) for unit tests. All core tests run headlessly with `MockHost` — no GPU, window, or audio device required.

---

## Running tests

### Full suite (recommended)

After building:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Add `--output-on-failure` to see assertion details when a test fails:

```bash
ctest --test-dir build --output-on-failure
```

### Run the test binary directly

Catch2 supports filtering by tag or test name:

```bash
# All tests
./build/citsy_tests

# Parser tests only
./build/citsy_tests "[parser]"

# Engine + mock tests
./build/citsy_tests "[engine]"

# Single test by name
./build/citsy_tests "parser: simple tile"

# List all registered tests
./build/citsy_tests --list-tests
```

### Parallel execution

CTest runs tests sequentially by default. For faster local runs:

```bash
ctest --test-dir build -j$(nproc)
```

---

## Test layout

```
tests/
├── unit/
│   ├── test_parser.cpp     # Parser and Game model tests
│   ├── test_engine.cpp     # Engine lifecycle and MockHost integration
│   ├── test_dialog.cpp     # Dialog page extraction and script interpreter
│   ├── test_simulation.cpp # Movement, collision, render, exits, dialog
│   └── test_phase3.cpp     # Animation, fonts, RTL, sound, transitions, dialog VM
└── data/
    ├── minimal.bitsy       # Full game with all entity types
    ├── animated.bitsy      # Multi-frame tile animation
    ├── two_rooms.bitsy     # Two rooms with exits and palettes
    ├── playable.bitsy      # Walls, NPC dialog, item, room exit
    ├── mossland.bitsy      # Real Bitsy 6.4 game
    ├── scripted.bitsy      # Variables, conditional dialog, inventory, ending
    └── phase3.bitsy        # Bitsy 8.15 tune/blip/AVA/FX sample
```

| File | Tags | What it covers |
|---|---|---|
| `test_parser.cpp` | `[parser]`, `[palette]`, `[tile]`, `[sprite]`, `[item]`, `[room]`, `[dialogue]`, `[variable]`, `[ending]`, `[model]`, `[errors]`, `[fixture]` | `.bitsy` parsing, entity fields, error handling, fixture files |
| `test_engine.cpp` | `[engine]`, `[mock]` | Engine construction, lifecycle, buffer sizes, palette output, MockHost behavior |
| `test_dialog.cpp` | `[dialog]`, `[script]` | Page extraction and script evaluation (variables, lists, items, `{end}`/`{exit}`) |
| `test_simulation.cpp` | `[engine]`, `[sim]`, `[render]`, `[dialog]`, `[fixture]` | Movement, walls, map/video compose, sprite/item drawing, exits, linear dialog |
| `test_phase3.cpp` | `[phase3]`, `[font]`, `[sound]`, `[transition]`, `[inventory]`, `[dialog]` | Animation, fonts, RTL, blips/tunes, `{item}`/`{property}`, titles, 8.15 extras |

As of Phase 3, the suite covers parser, simulation, dialog scripting, fonts, sound, and transitions.

---

## MockHost

Tests that exercise the engine use `MockHost` (`backends/mock/mock_host.hpp`), a header-only test double that implements `citsy::Host`.

MockHost records:

- Whether `on_engine_ready()` was called
- Every `present()` call as a `PresentSnapshot` (palette, video, map1, map2, textbox, sound channels)
- Log messages passed to `log()`

Basic usage in a test:

```cpp
#include "backends/mock/mock_host.hpp"

citsy::Engine engine(kMinimalGame);
citsy::MockHost host;

engine.start(host);
engine.update(host);

const auto* snap = host.last_snapshot();
REQUIRE(snap != nullptr);
CHECK(snap->video.size() == citsy::kVideoSize * citsy::kVideoSize);
CHECK(snap->gfx_mode == citsy::GraphicsMode::Map);
```

MockHost helpers:

| Member / method | Purpose |
|---|---|
| `snapshots` | Vector of every `present()` call |
| `last_snapshot()` | Pointer to most recent snapshot, or `nullptr` |
| `set_button(Button, bool)` | Simulate input |
| `dt_ms` | Configurable frame delta (default ≈ 16.667 ms) |
| `reset()` | Clear recorded state |

---

## Fixture files

Tests in `tests/data/` are real `.bitsy` files used for integration-style parser checks. The helper in `test_parser.cpp` resolves paths relative to the source tree:

```cpp
static std::string load_fixture(const char* name) {
    fs::path here = fs::path(__FILE__).parent_path();   // tests/unit/
    fs::path data = here.parent_path() / "data" / name; // tests/data/<name>
    // ...
}
```

This works regardless of the build directory because it anchors on `__FILE__`.

When adding a new fixture:

1. Place the `.bitsy` file in `tests/data/`
2. Add a `TEST_CASE` with tag `[fixture]` that loads and asserts on key entities
3. Keep fixtures small and focused on the feature under test

---

## Writing new tests

### Parser / model test (inline string)

White-box tests include the internal parser header directly:

```cpp
#include "src/parser/parser.hpp"

TEST_CASE("parser: my new feature", "[parser]") {
    constexpr std::string_view src = R"(
TIL x
00000000
...
)";
    auto game = citsy::parse(src);
    CHECK(/* assertions on game model */);
}
```

### Engine test (MockHost)

Use an inline `.bitsy` string or load a fixture:

```cpp
#include <citsy/engine.hpp>
#include "backends/mock/mock_host.hpp"

TEST_CASE("engine: my behavior", "[engine]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    // assert on host.last_snapshot()
}
```

### Error tests

Parser and engine constructors throw `citsy::ParseError` on bad input:

```cpp
CHECK_THROWS_AS(citsy::parse(bad_src), citsy::ParseError);
CHECK_THROWS_AS(citsy::Engine(bad_src), citsy::ParseError);
```

### Register a new test file

1. Create `tests/unit/test_myfeature.cpp`
2. Add it to the `citsy_tests` target in `CMakeLists.txt`:

```cmake
add_executable(citsy_tests
    tests/unit/test_parser.cpp
    tests/unit/test_engine.cpp
    tests/unit/test_dialog.cpp
    tests/unit/test_simulation.cpp
    tests/unit/test_myfeature.cpp   # add here
)
```

3. Reconfigure and rebuild — Catch2 auto-discovers new `TEST_CASE` macros

---

## Catch2 tags

Tests use tags for filtering. Current tags:

| Tag | Scope |
|---|---|
| `[parser]` | All parser tests |
| `[engine]` | Engine lifecycle and output |
| `[mock]` | MockHost-specific tests |
| `[dialog]` | Linear dialog extraction and playback |
| `[sim]` | Movement, collision, exits, room state |
| `[render]` | map1 / map2 / video composition |
| `[fixture]` | Tests loading `tests/data/*.bitsy` |
| `[errors]` | Parse error handling |
| Sub-tags like `[palette]`, `[tile]`, `[room]` | Entity-specific parser tests |

Run a subset:

```bash
./build/citsy_tests "[parser][tile]"
```

---

## CI considerations

The test suite is designed for headless CI:

- No display server required
- No audio device required
- Catch2 fetched at configure time (needs network on first build)
- All tests complete in well under one second

Minimal CI recipe:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

To skip tests in a CI job that only validates compilation:

```bash
cmake -B build -DCITSY_BUILD_TESTS=OFF
cmake --build build
```

---

## Debugging failed tests

1. Run with verbose output:

   ```bash
   ctest --test-dir build --output-on-failure -V
   ```

2. Run the specific test directly for a full Catch2 report:

   ```bash
   ./build/citsy_tests "exact test name" -s
   ```

   The `-s` flag shows successful assertions too (useful for diagnosing unexpected passes).

3. For parser issues, reproduce with the smallest inline `.bitsy` snippet before reaching for a fixture file.
