# Getting started

Build the library, run the tests, and embed `citsy::Engine` behind your own host.

## Requirements

| Tool | Minimum |
|---|---|
| CMake | 3.20 |
| C++ compiler | C++20 (GCC 11+, Clang 14+, MSVC 19.29+) |
| Git | Needed for Catch2 via `FetchContent` |

The core library has **no** runtime dependency on windowing, GPU, or audio libraries.

## Build and test

```bash
git clone https://github.com/TeriyakiGod/citsy.git
cd citsy
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

=== "Full build"

    ```bash
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build
    ```

=== "Library only"

    ```bash
    cmake -B build -DCITSY_BUILD_TESTS=OFF -DCITSY_BUILD_EXAMPLES=OFF
    cmake --build build
    ```

=== "Ninja"

    ```bash
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ```

Binaries land under `build/`:

| Target | Output | Description |
|---|---|---|
| `citsy` | `build/libcitsy.a` | Static core library |
| `citsy_tests` | `build/citsy_tests` | Catch2 unit tests |
| `minimal_example` | `build/minimal_example` | Load a `.bitsy` file with MockHost |

Try the example with a fixture:

```bash
./build/minimal_example tests/data/minimal.bitsy
```

CMake options, generators, and cross-compilation notes are in [Tools & build](tools.md).

## Embed the engine

Public headers live in `include/citsy/`. Link the `citsy` target and implement `citsy::Host`.

```cpp
#include <citsy/engine.hpp>
#include <citsy/host.hpp>

class MyHost : public citsy::Host {
public:
    double delta_time_ms() const override { return dt_; }
    bool button(citsy::Button b) const override { return keys_[static_cast<int>(b)]; }

    void present(
        citsy::GraphicsMode gfx_mode,
        citsy::TextMode txt_mode,
        std::span<const citsy::Color> palette,
        std::span<const std::uint8_t> video,
        std::span<const std::uint8_t> map1,
        std::span<const std::uint8_t> map2,
        citsy::TextboxView textbox,
        citsy::SoundChannel sound1,
        citsy::SoundChannel sound2
    ) override {
        // Blit video or map buffers to your display.
        // Play sound1 / sound2 if active.
        (void)gfx_mode;
        (void)txt_mode;
        (void)palette;
        (void)video;
        (void)map1;
        (void)map2;
        (void)textbox;
        (void)sound1;
        (void)sound2;
    }

    void set_delta(double dt) { dt_ = dt; }
    void set_key(citsy::Button b, bool down) {
        keys_[static_cast<int>(b)] = down;
    }

private:
    double dt_ = 16.667;
    bool keys_[6] = {};
};

int main() {
    auto engine = citsy::Engine::from_file("my_game.bitsy");
    MyHost host;

    engine.start(host);
    while (engine.is_running()) {
        host.set_delta(16.667);
        engine.update(host);  // calls host.present()
    }
}
```

CMake for a consumer project:

```cmake
add_subdirectory(path/to/citsy)
target_link_libraries(my_app PRIVATE citsy)
```

!!! tip "Start with MockHost"

    For tests or a first integration, use `backends/mock/mock_host.hpp`. It records every `present()` call so you can assert on buffers without a window. See [Testing](testing.md#mockhost).

The full host contract — buttons, memory blocks, graphics modes — is documented in the [Host API](host.md).

## What the engine does not do

- Open a window or talk to a GPU
- Mix audio samples (it only emits square-wave *parameters*)
- Author or export Bitsy games — use [bitsy.org](https://bitsy.org)

Those jobs belong in a backend. The optional raylib player is still on the [roadmap](roadmap.md).
