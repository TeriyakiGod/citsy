#pragma once

// Linear dialog helpers for Phase 1.
// Extracts sequential text pages from raw Bitsy DLG source.
// Script tags other than {br} / {p} are stripped; full evaluation is Phase 2.

#include <string>
#include <string_view>
#include <vector>

namespace citsy {

/// Split Bitsy dialog source into display pages.
///
/// - `"quoted"` strings and `"""` blocks become text
/// - `{p}` starts a new page
/// - `{br}` becomes a newline
/// - other `{tags}` are omitted
/// - if there are no quotes, the trimmed remaining text is one page
[[nodiscard]] std::vector<std::string> extract_dialog_pages(std::string_view content);

} // namespace citsy
