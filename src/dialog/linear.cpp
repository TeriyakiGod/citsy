#include "src/dialog/linear.hpp"
#include "src/dialog/script.hpp"

namespace citsy {
namespace {

class NullWorld final : public DialogWorld {
public:
    Value get_var(std::string_view) const override { return Value::number(0); }
    void  set_var(std::string_view, Value) override {}
    int   get_item(std::string_view) const override { return 0; }
    void  set_item(std::string_view, int) override {}
    std::string resolve_room(std::string_view id) const override {
        return std::string(id);
    }
    int random_int(int n) override { return n > 0 ? 0 : 0; }
};

} // namespace

std::vector<std::string> extract_dialog_pages(std::string_view content) {
    NullWorld world;
    DialogScript script = parse_dialog_script(content);
    return run_dialog_script(script, world).pages;
}

} // namespace citsy
