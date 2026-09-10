#pragma once

#include <citsy/host.hpp>
#include <citsy/types.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace citsy {

// ---------------------------------------------------------------------------
// Parse error
// ---------------------------------------------------------------------------

/// Thrown by Engine constructors and citsy::parse() when the .bitsy input is
/// malformed or unsupported.
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg, int line = -1)
        : std::runtime_error(line >= 0
              ? "line " + std::to_string(line) + ": " + msg
              : msg)
        , line_(line)
    {}

    /// 1-based line number where the error was detected, or -1 if unknown.
    [[nodiscard]] int line() const noexcept { return line_; }

private:
    int line_;
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

/// The citsy game engine.
///
/// Owns a parsed Game, runs the simulation, and drives the Host on each
/// update.  Construct from a .bitsy text string or from a file path.
///
/// ```cpp
/// auto engine = citsy::Engine::from_file("game.bitsy");
/// engine.start();
/// while (engine.is_running()) {
///     engine.update(my_host);
/// }
/// ```
class Engine {
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Parse @p bitsy_text and construct the engine.
    ///
    /// @throws ParseError   if the data is malformed.
    explicit Engine(std::string_view bitsy_text);

    /// Load a .bitsy file from disk and construct the engine.
    ///
    /// @throws ParseError           if the data is malformed.
    /// @throws std::ios_base::failure if the file cannot be read.
    [[nodiscard]] static Engine from_file(const std::string& path);

    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Initialise runtime state and call Host::on_engine_ready().
    ///
    /// Must be called before the first update().
    void start(Host& host);

    /// Run one simulation step and call Host::present().
    ///
    /// @pre start() must have been called.
    void update(Host& host);

    /// True while the game is running (not yet ended or stopped).
    [[nodiscard]] bool is_running() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace citsy
