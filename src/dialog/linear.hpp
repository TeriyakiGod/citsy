#pragma once

// Dialog helpers: split Bitsy DLG source into display pages.
// Full evaluation (variables, lists, item actions) lives in script.hpp;
// this wrapper runs a script with a no-op world so tests can inspect pages.

#include <string>
#include <string_view>
#include <vector>

namespace citsy {

/// Split Bitsy dialog source into display pages.
///
/// - `"quoted"` strings and `"""` blocks become text
/// - `{p}` starts a new page
/// - `{br}` becomes a newline
/// - consecutive quoted strings are separate pages
/// - blank lines inside lists / triple-quoted blocks start a new page
[[nodiscard]] std::vector<std::string> extract_dialog_pages(std::string_view content);

} // namespace citsy
