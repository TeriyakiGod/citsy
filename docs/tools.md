# Tools & build

How to configure, build, and run citsy targets.

---

## Requirements

| Tool | Minimum version | Notes |
|---|---|---|
| **CMake** | 3.20 | Project generator and dependency fetching |
| **C++ compiler** | C++20 | GCC 11+, Clang 14+, or MSVC 19.29+ |
| **Git** | any recent | Required for CMake `FetchContent` (Catch2) |
| **Make or Ninja** | — | Any generator CMake supports |

The core library has **zero** runtime dependencies on windowing, GPU, or audio libraries.

---

## Quick start

```bash
# Configure (Release build, tests and examples on)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build

# Run the test suite
ctest --test-dir build
```

Binaries appear under `build/`:

| Target | Output | Description |
|---|---|---|
| `citsy` | `build/libcitsy.a` | Static core library |
| `citsy_tests` | `build/citsy_tests` | Unit test executable |
| `minimal_example` | `build/minimal_example` | Example that loads a `.bitsy` file |

---

## CMake options

Set these at configure time with `-DOPTION=value`:

| Option | Default | Description |
|---|---|---|
| `CITSY_BUILD_TESTS` | `ON` | Build `citsy_tests` and register CTest entries |
| `CITSY_BUILD_EXAMPLES` | `ON` | Build `minimal_example` |
| `CITSY_BUILD_RAYLIB_BACKEND` | `OFF` | Build the optional raylib reference player |

Examples:

```bash
# Library only — skip tests and examples
cmake -B build -DCITSY_BUILD_TESTS=OFF -DCITSY_BUILD_EXAMPLES=OFF

# Include the raylib backend (when implemented)
cmake -B build -DCITSY_BUILD_RAYLIB_BACKEND=ON
cmake --build build
```

---

## Build types

Use `-DCMAKE_BUILD_TYPE=` to select optimization level:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug      # symbols, no optimization
cmake -B build -DCMAKE_BUILD_TYPE=Release    # optimized (recommended)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

On multi-config generators (Visual Studio, Xcode), specify the config at build time:

```bash
cmake --build build --config Release
```

---

## Compiler flags

The core library enables strict warnings on GCC and Clang:

- `-Wall -Wextra -Wpedantic`

MSVC builds use `/W4`. No special flags are required from consumers linking against `citsy`.

---

## Dependencies

### Core library

None. The `citsy` target is a self-contained static library with only standard C++20 headers.

### Tests (when `CITSY_BUILD_TESTS=ON`)

[Catch2](https://github.com/catchorg/Catch2) v3.5.4 is fetched automatically via CMake `FetchContent`. No system install required — it lands in `build/_deps/catch2-src/`.

### Optional backends

| Backend | Dependency | When |
|---|---|---|
| raylib | [raylib](https://www.raylib.com/) | `CITSY_BUILD_RAYLIB_BACKEND=ON` |
| 32blit | 32blit SDK | Planned |

Backends link both `citsy` and their platform library. The core never links them.

---

## Build targets reference

### `citsy` (static library)

Source files (current):

- `src/parser/parser.cpp`
- `src/engine/engine.cpp`
- `src/dialog/linear.cpp`
- `src/dialog/script.cpp`
- `src/render/compose.cpp`

Public headers: `include/citsy/`

Link in your project:

```cmake
target_link_libraries(my_app PRIVATE citsy)
```

Include path is propagated automatically via `target_include_directories`.

### `citsy_tests`

Catch2 test runner with `Catch2WithMain` (provides `main()`). Tests are auto-discovered by CTest via `catch_discover_tests()`.

See [Testing](testing.md) for how to run and filter tests.

### `minimal_example`

Demonstrates loading a `.bitsy` file and stepping the engine with MockHost:

```bash
./build/minimal_example tests/data/minimal.bitsy
```

---

## Generator choice

Ninja is faster for iterative development:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The default Makefiles generator works equally well:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
make -C build -j$(nproc)
```

---

## Clean rebuild

```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The `build/` directory is gitignored. Catch2 and other fetched deps live inside it.

---

## IDE integration

### VS Code / Cursor

Configure once, then use the CMake Tools extension or run from the terminal:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### CLion

Open the project root; CLion detects `CMakeLists.txt` automatically.

---

## Cross-compilation notes

The core library is platform-agnostic C++20 with no OS-specific code. Backends will add platform constraints when implemented (e.g. raylib for desktop, 32blit for a specific handheld).

For embedded or cross targets, disable tests and examples to avoid pulling Catch2:

```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=path/to/toolchain.cmake \
  -DCITSY_BUILD_TESTS=OFF \
  -DCITSY_BUILD_EXAMPLES=OFF
```

---

## Documentation website

Guides in `docs/` are published with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/) to GitHub Pages.

```bash
python3 -m venv .venv-docs
source .venv-docs/bin/activate
pip install -r requirements-docs.txt
mkdocs serve
```

Open http://127.0.0.1:8000. `mkdocs build --strict` is what CI runs before deploying https://teriyakigod.github.io/citsy/.
