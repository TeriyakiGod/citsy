#pragma once

// Bitsy dialog script interpreter (Phase 2).
// Parses DLG / END source into an AST and evaluates it against a DialogWorld.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace citsy {

// ---------------------------------------------------------------------------
// Runtime value (number or string; Bitsy has no distinct bool type)
// ---------------------------------------------------------------------------

class Value {
public:
    enum class Kind { Null, Number, String };

    static Value null();
    static Value number(double n);
    static Value string(std::string s);

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool is_number() const noexcept { return kind_ == Kind::Number; }
    [[nodiscard]] bool is_string() const noexcept { return kind_ == Kind::String; }

    [[nodiscard]] double as_number() const;
    [[nodiscard]] std::string as_string() const;
    [[nodiscard]] bool is_truthy() const;

private:
    Kind        kind_   = Kind::Null;
    double      number_ = 0;
    std::string string_;
};

// ---------------------------------------------------------------------------
// Host-side world the script mutates (variables, inventory, room names)
// ---------------------------------------------------------------------------

class DialogWorld {
public:
    virtual ~DialogWorld() = default;

    virtual Value get_var(std::string_view name) const = 0;
    virtual void  set_var(std::string_view name, Value v) = 0;

    /// Lookup by item id or NAME. Missing items are 0.
    virtual int  get_item(std::string_view id_or_name) const = 0;
    virtual void set_item(std::string_view id_or_name, int count) = 0;

    /// Resolve a room id or NAME to a room id (for {exit}).
    virtual std::string resolve_room(std::string_view id_or_name) const = 0;

    /// Uniform integer in [0, n). n > 0.
    virtual int random_int(int n) = 0;
};

// ---------------------------------------------------------------------------
// Evaluation result
// ---------------------------------------------------------------------------

struct DialogExit {
    std::string room_id;
    int         x = 0;
    int         y = 0;
    std::string effect;
};

struct DialogResult {
    std::vector<std::string> pages;
    bool                     end_game = false;
    std::optional<DialogExit> exit;
};

// ---------------------------------------------------------------------------
// Parsed script (list nodes keep sequence / cycle / shuffle state)
// ---------------------------------------------------------------------------

class DialogScript {
public:
    DialogScript();
    DialogScript(DialogScript&&) noexcept;
    DialogScript& operator=(DialogScript&&) noexcept;
    ~DialogScript();

    DialogScript(const DialogScript&)            = delete;
    DialogScript& operator=(const DialogScript&) = delete;

    friend DialogScript parse_dialog_script(std::string_view source);
    friend DialogResult run_dialog_script(DialogScript& script, DialogWorld& world);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] DialogScript parse_dialog_script(std::string_view source);
[[nodiscard]] DialogResult run_dialog_script(DialogScript& script, DialogWorld& world);

} // namespace citsy
