#include "src/dialog/linear.hpp"

#include <cctype>

namespace citsy {
namespace {

std::string_view trim_sv(std::string_view s) {
    auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string_view trim_tag(std::string_view tag) {
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front())))
        tag.remove_prefix(1);
    while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back())))
        tag.remove_suffix(1);
    return tag;
}

// Replace {br} with newline and drop other {tags}. {p} should already be split out.
std::string apply_inline_tags(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] == '{') {
            const auto end = in.find('}', i + 1);
            if (end == std::string_view::npos) {
                out.push_back(in[i++]);
                continue;
            }
            const auto tag = trim_tag(in.substr(i + 1, end - (i + 1)));
            if (tag == "br") out.push_back('\n');
            i = end + 1;
        } else {
            out.push_back(in[i++]);
        }
    }
    return out;
}

void split_on_pagebreak(std::string_view s, std::vector<std::string>& pages) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '{') {
            const auto end = s.find('}', i + 1);
            if (end != std::string_view::npos) {
                const auto tag = trim_tag(s.substr(i + 1, end - (i + 1)));
                if (tag == "p") {
                    auto page = apply_inline_tags(s.substr(start, i - start));
                    if (!trim_sv(page).empty()) pages.push_back(std::move(page));
                    start = end + 1;
                    i = end + 1;
                    continue;
                }
            }
        }
        ++i;
    }
    auto page = apply_inline_tags(s.substr(start));
    if (!trim_sv(page).empty()) pages.push_back(std::move(page));
}

} // namespace

std::vector<std::string> extract_dialog_pages(std::string_view content) {
    std::vector<std::string> quoted;
    std::size_t i = 0;
    const std::size_t n = content.size();

    auto skip_ws = [&] {
        while (i < n && (content[i] == ' ' || content[i] == '\t' ||
                         content[i] == '\n' || content[i] == '\r')) {
            ++i;
        }
    };

    while (i < n) {
        skip_ws();
        if (i >= n) break;

        if (i + 2 < n && content[i] == '"' && content[i + 1] == '"' &&
            content[i + 2] == '"') {
            i += 3;
            if (i < n && (content[i] == '\n' || content[i] == '\r')) {
                if (content[i] == '\r' && i + 1 < n && content[i + 1] == '\n') i += 2;
                else ++i;
            }
            const std::size_t start = i;
            while (i + 2 < n &&
                   !(content[i] == '"' && content[i + 1] == '"' && content[i + 2] == '"')) {
                ++i;
            }
            auto body = content.substr(start, i - start);
            if (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
                body.remove_suffix(1);
                if (!body.empty() && body.back() == '\r') body.remove_suffix(1);
            }
            quoted.emplace_back(body);
            if (i + 2 < n) i += 3;
            else i = n;
            continue;
        }

        if (content[i] == '"') {
            ++i;
            const std::size_t start = i;
            while (i < n && content[i] != '"') ++i;
            quoted.emplace_back(content.substr(start, i - start));
            if (i < n) ++i;
            continue;
        }

        // Skip a {tag} in unquoted source so it is not treated as leftover text.
        if (content[i] == '{') {
            const auto end = content.find('}', i + 1);
            i = (end == std::string_view::npos) ? n : end + 1;
            continue;
        }

        const std::size_t start = i;
        while (i < n && content[i] != '"' && content[i] != '{') ++i;
        auto run = trim_sv(content.substr(start, i - start));
        if (!run.empty()) quoted.emplace_back(run);
    }

    std::vector<std::string> pages;
    if (quoted.empty()) {
        auto fallback = trim_sv(content);
        if (!fallback.empty()) split_on_pagebreak(fallback, pages);
        return pages;
    }

    for (const auto& q : quoted) split_on_pagebreak(q, pages);
    return pages;
}

} // namespace citsy
