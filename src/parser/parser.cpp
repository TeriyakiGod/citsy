#include "src/parser/parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace citsy {

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

// Trim ASCII whitespace from both ends.
std::string_view trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// Split @p s by @p delim; each token is trimmed.
std::vector<std::string_view> split(std::string_view s, char delim) {
    std::vector<std::string_view> out;
    while (true) {
        auto pos = s.find(delim);
        out.push_back(trim(s.substr(0, pos)));
        if (pos == std::string_view::npos) break;
        s = s.substr(pos + 1);
    }
    return out;
}

// Split on first occurrence of whitespace; returns {head, rest}.
std::pair<std::string_view, std::string_view> split_once(std::string_view s) {
    s = trim(s);
    auto pos = s.find_first_of(" \t");
    if (pos == std::string_view::npos) return {s, {}};
    return {s.substr(0, pos), trim(s.substr(pos + 1))};
}

// Parse integer; throws ParseError on failure.
int parse_int(std::string_view s, int line) {
    s = trim(s);
    int result = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    if (ec != std::errc{} || ptr != s.data() + s.size())
        throw ParseError("expected integer, got '" + std::string(s) + "'", line);
    return result;
}

// Parse "x,y" pair; throws ParseError on failure.
std::pair<int, int> parse_xy(std::string_view s, int line) {
    auto parts = split(s, ',');
    if (parts.size() != 2)
        throw ParseError("expected 'x,y', got '" + std::string(s) + "'", line);
    return {parse_int(parts[0], line), parse_int(parts[1], line)};
}

// Parse one 8×8 pixel frame from exactly 8 lines of 8 binary chars.
// @p lines must point to ≥8 elements; returns number of lines consumed (8).
TileFrame parse_tile_frame(const std::vector<std::string>& lines,
                           std::size_t start, int base_line) {
    TileFrame frame{};
    if (start + kTileSize > lines.size())
        throw ParseError("incomplete tile frame (need 8 pixel rows)",
                         base_line + static_cast<int>(start));
    for (int row = 0; row < kTileSize; ++row) {
        std::string_view rowstr = trim(lines[start + row]);
        if (rowstr.size() != kTileSize)
            throw ParseError("tile row must be exactly 8 characters, got '"
                             + std::string(rowstr) + "'",
                             base_line + static_cast<int>(start) + row);
        for (int col = 0; col < kTileSize; ++col) {
            char c = rowstr[col];
            if (c != '0' && c != '1')
                throw ParseError("tile pixel must be '0' or '1'",
                                 base_line + static_cast<int>(start) + row);
            frame[row * kTileSize + col] = (c == '1') ? 1u : 0u;
        }
    }
    return frame;
}

// Parse "r,g,b" into a Color.
Color parse_color(std::string_view s, int line) {
    auto parts = split(s, ',');
    if (parts.size() != 3)
        throw ParseError("expected 'r,g,b', got '" + std::string(s) + "'", line);
    auto clamp = [&](std::string_view v) -> std::uint8_t {
        int n = parse_int(v, line);
        if (n < 0 || n > 255)
            throw ParseError("color component out of range: " + std::string(v), line);
        return static_cast<std::uint8_t>(n);
    };
    return {clamp(parts[0]), clamp(parts[1]), clamp(parts[2])};
}

// Return true if @p s looks like "r,g,b" (three comma-separated numbers).
bool looks_like_color(std::string_view s) {
    auto parts = split(s, ',');
    if (parts.size() != 3) return false;
    for (auto p : parts) {
        int dummy{};
        auto [ptr, ec] = std::from_chars(p.data(), p.data() + p.size(), dummy);
        if (ec != std::errc{} || ptr != p.data() + p.size()) return false;
    }
    return true;
}

} // anonymous namespace

// ===========================================================================
// Parser class
// ===========================================================================

class Parser {
public:
    explicit Parser(std::string_view text) {
        // Split text into lines; keep them as owned strings for lifetime safety.
        std::istringstream ss{std::string(text)};
        std::string ln;
        while (std::getline(ss, ln)) {
            // Strip trailing CR (Windows line endings).
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            lines_.push_back(std::move(ln));
        }
    }

    Game parse() {
        Game game;

        while (pos_ < lines_.size()) {
            // Skip blank lines between segments.
            if (trim(lines_[pos_]).empty()) {
                ++pos_;
                continue;
            }

            std::string_view line = trim(lines_[pos_]);

            // ----------------------------------------------------------------
            // Global header directives
            // ----------------------------------------------------------------
            if (line.starts_with("# BITSY VERSION")) {
                parse_version_comment(line, game);
                ++pos_;
                continue;
            }
            if (line.starts_with("! ROOM_FORMAT")) {
                auto [directive, value] = split_once(split_once(line).second);
                (void)directive;
                game.room_format = parse_int(value, current_line());
                ++pos_;
                continue;
            }
            if (line.starts_with("!")) {
                // Unknown directive — skip.
                ++pos_;
                continue;
            }
            if (line.starts_with("#")) {
                // Comment — skip.
                ++pos_;
                continue;
            }

            // ----------------------------------------------------------------
            // Named segments
            // ----------------------------------------------------------------
            auto [keyword, id] = split_once(line);

            if (keyword == "PAL") {
                parse_palette(std::string(id), game);
            } else if (keyword == "TIL") {
                parse_tile(std::string(id), game);
            } else if (keyword == "SPR") {
                parse_sprite(std::string(id), game);
            } else if (keyword == "ITM") {
                parse_item(std::string(id), game);
            } else if (keyword == "ROOM") {
                parse_room(std::string(id), game);
            } else if (keyword == "DLG") {
                parse_dialogue(std::string(id), game);
            } else if (keyword == "VAR") {
                parse_variable(std::string(id), game);
            } else if (keyword == "END") {
                parse_ending(std::string(id), game);
            } else if (keyword == "EXT") {
                // Top-level EXT (older format): skip for now.
                skip_segment();
            } else if (keyword == "FONT") {
                // Custom font: skip for now (Phase 3).
                skip_segment();
            } else if (keyword == "NAME") {
                // Top-level NAME after BITSY VERSION comment.
                game.title = std::string(id);
                ++pos_;
            } else {
                // Unknown segment — skip to next blank line.
                skip_segment();
            }
        }

        return game;
    }

private:
    std::vector<std::string> lines_;
    std::size_t pos_ = 0;

    [[nodiscard]] int current_line() const {
        return static_cast<int>(pos_) + 1;  // 1-based
    }

    // Consume lines until (but not including) the next blank line or EOF.
    std::vector<std::string> read_body() {
        std::vector<std::string> body;
        while (pos_ < lines_.size() && !trim(lines_[pos_]).empty()) {
            body.push_back(lines_[pos_]);
            ++pos_;
        }
        return body;
    }

    // Count `"""` tokens in a line (used to keep blank lines inside triple quotes).
    static int count_triple_quotes(std::string_view s) {
        int n = 0;
        for (std::size_t i = 0; i + 2 < s.size();) {
            if (s[i] == '"' && s[i + 1] == '"' && s[i + 2] == '"') {
                ++n;
                i += 3;
            } else {
                ++i;
            }
        }
        return n;
    }

    // DLG / END bodies may contain triple-quoted blocks with blank lines
    // (page breaks). Keep reading while a `"""` pair is still open; once
    // closed, stop at the next blank line so NAME and the next segment work.
    std::vector<std::string> read_dialog_body() {
        std::vector<std::string> body;
        bool in_triple = false;
        while (pos_ < lines_.size()) {
            const std::string& raw = lines_[pos_];
            if (!in_triple && trim(raw).empty()) break;
            if (count_triple_quotes(raw) % 2 != 0) in_triple = !in_triple;
            body.push_back(raw);
            ++pos_;
        }
        return body;
    }

    // Skip the current segment header + body.
    void skip_segment() {
        ++pos_;  // skip header
        while (pos_ < lines_.size() && !trim(lines_[pos_]).empty())
            ++pos_;
    }

    // Parse "# BITSY VERSION major.minor" or "# BITSY VERSION major"
    void parse_version_comment(std::string_view line, Game& game) {
        // "# BITSY VERSION 8.12" — extract the version token
        // Find the last token after "VERSION"
        auto pos = line.rfind("VERSION");
        if (pos == std::string_view::npos) return;
        std::string_view ver = trim(line.substr(pos + 7));
        auto dot = ver.find('.');
        if (dot == std::string_view::npos) {
            game.version.major = parse_int(ver, current_line());
        } else {
            game.version.major = parse_int(ver.substr(0, dot), current_line());
            game.version.minor = parse_int(ver.substr(dot + 1), current_line());
        }
    }

    // -----------------------------------------------------------------------
    // PAL
    // -----------------------------------------------------------------------
    void parse_palette(std::string id, Game& game) {
        int header_line = current_line();
        ++pos_;  // skip "PAL <id>"

        Palette pal;
        pal.id = id;

        while (pos_ < lines_.size()) {
            std::string_view line = trim(lines_[pos_]);
            if (line.empty()) break;

            auto [kw, rest] = split_once(line);

            if (kw == "NAME") {
                pal.name = std::string(rest);
                ++pos_;
            } else if (looks_like_color(line)) {
                pal.colors.push_back(parse_color(line, current_line()));
                ++pos_;
            } else if (kw == "COL") {
                // Extended palette: COL n on its own is the index annotation
                // for the *next* color line (some Bitsy versions write this).
                // We just ignore the COL directive and parse the color lines.
                ++pos_;
            } else {
                // Unknown sub-key — skip.
                ++pos_;
            }
        }

        (void)header_line;
        game.palettes[pal.id] = std::move(pal);
    }

    // -----------------------------------------------------------------------
    // TIL
    // -----------------------------------------------------------------------
    void parse_tile(std::string id, Game& game) {
        int header_line = current_line();
        ++pos_;

        // Read the body lines (until blank line).
        std::vector<std::string> body = read_body();

        Tile tile;
        tile.id = id;

        std::size_t i = 0;
        // Each animation frame is 8 pixel rows, separated by ">" lines.
        while (i < body.size()) {
            std::string_view line = trim(body[i]);
            if (line == ">") {
                ++i;
                continue;
            }
            // Check if this could be a pixel row.
            if (line.size() == 8 &&
                line.find_first_not_of("01") == std::string_view::npos) {
                // Parse one frame.
                tile.frames.push_back(
                    parse_tile_frame(body, i, header_line));
                i += kTileSize;
            } else {
                // Sub-key.
                auto [kw, rest] = split_once(line);
                if (kw == "NAME") {
                    tile.name = std::string(rest);
                } else if (kw == "WAL") {
                    tile.is_wall = (trim(rest) == "true");
                } else if (kw == "COL") {
                    auto col_idx = parse_int(rest, header_line + static_cast<int>(i));
                    tile.color_index = static_cast<std::uint8_t>(col_idx);
                }
                ++i;
            }
        }

        if (tile.frames.empty())
            throw ParseError("tile '" + id + "' has no pixel data", header_line);

        game.tiles[tile.id] = std::move(tile);
    }

    // -----------------------------------------------------------------------
    // SPR
    // -----------------------------------------------------------------------
    void parse_sprite(std::string id, Game& game) {
        int header_line = current_line();
        ++pos_;

        std::vector<std::string> body = read_body();

        Sprite spr;
        spr.id = id;

        std::size_t i = 0;
        while (i < body.size()) {
            std::string_view line = trim(body[i]);
            if (line == ">") { ++i; continue; }

            if (line.size() == 8 &&
                line.find_first_not_of("01") == std::string_view::npos) {
                spr.frames.push_back(parse_tile_frame(body, i, header_line));
                i += kTileSize;
            } else {
                auto [kw, rest] = split_once(line);
                if (kw == "NAME") {
                    spr.name = std::string(rest);
                } else if (kw == "POS") {
                    // POS <room_id> <x>,<y>
                    auto [room_id, xy] = split_once(rest);
                    SpritePos p;
                    p.room_id = std::string(room_id);
                    auto [x, y] = parse_xy(xy, header_line + static_cast<int>(i));
                    p.x = x;
                    p.y = y;
                    spr.position = p;
                } else if (kw == "DLG") {
                    spr.dialog_id = std::string(rest);
                } else if (kw == "COL") {
                    spr.color_index = static_cast<std::uint8_t>(
                        parse_int(rest, header_line + static_cast<int>(i)));
                }
                ++i;
            }
        }

        if (spr.frames.empty())
            throw ParseError("sprite '" + id + "' has no pixel data", header_line);

        game.sprites[spr.id] = std::move(spr);
    }

    // -----------------------------------------------------------------------
    // ITM
    // -----------------------------------------------------------------------
    void parse_item(std::string id, Game& game) {
        int header_line = current_line();
        ++pos_;

        std::vector<std::string> body = read_body();

        Item itm;
        itm.id = id;

        std::size_t i = 0;
        while (i < body.size()) {
            std::string_view line = trim(body[i]);
            if (line == ">") { ++i; continue; }

            if (line.size() == 8 &&
                line.find_first_not_of("01") == std::string_view::npos) {
                itm.frames.push_back(parse_tile_frame(body, i, header_line));
                i += kTileSize;
            } else {
                auto [kw, rest] = split_once(line);
                if (kw == "NAME") {
                    itm.name = std::string(rest);
                } else if (kw == "DLG") {
                    itm.dialog_id = std::string(rest);
                } else if (kw == "COL") {
                    itm.color_index = static_cast<std::uint8_t>(
                        parse_int(rest, header_line + static_cast<int>(i)));
                }
                ++i;
            }
        }

        if (itm.frames.empty())
            throw ParseError("item '" + id + "' has no pixel data", header_line);

        game.items[itm.id] = std::move(itm);
    }

    // -----------------------------------------------------------------------
    // ROOM
    // -----------------------------------------------------------------------
    void parse_room(std::string id, Game& game) {
        int header_line = current_line();
        ++pos_;

        std::vector<std::string> body = read_body();

        Room room;
        room.id = id;

        // Tile rows come first (kMapSize of them), before any sub-keys.
        int tile_rows_parsed = 0;
        std::size_t i = 0;

        // First pass: parse tile grid rows.
        while (i < body.size() && tile_rows_parsed < kMapSize) {
            std::string_view line = trim(body[i]);
            // Detect a tile row: either comma-separated or legacy single-char run.
            // A line is a tile row if it starts with a hex digit / letter AND
            // doesn't start with an alphabetic sub-key keyword (NAME, PAL, …).
            bool is_tile_row = false;

            // If the line contains a comma it's definitely comma-sep.
            if (line.find(',') != std::string_view::npos) {
                is_tile_row = true;
            } else if (!line.empty()) {
                // Legacy: check if ALL chars are valid hex-ish tile IDs.
                // Distinguish from keywords by checking line length vs kMapSize.
                char first = line[0];
                bool starts_alpha_keyword =
                    (first >= 'A' && first <= 'Z') &&
                    line.find_first_of(" \t") != std::string_view::npos;
                if (!starts_alpha_keyword && line.size() == kMapSize) {
                    is_tile_row = true;
                }
            }

            if (is_tile_row) {
                parse_room_tile_row(line, room, tile_rows_parsed, header_line + static_cast<int>(i));
                ++tile_rows_parsed;
                ++i;
            } else {
                break;
            }
        }

        if (tile_rows_parsed != kMapSize)
            throw ParseError(
                "room '" + id + "' has " + std::to_string(tile_rows_parsed) +
                " tile rows (expected " + std::to_string(kMapSize) + ")",
                header_line);

        // Second pass: parse sub-keys.
        while (i < body.size()) {
            std::string_view line = trim(body[i]);
            auto [kw, rest] = split_once(line);

            if (kw == "NAME") {
                room.name = std::string(rest);
            } else if (kw == "PAL") {
                room.palette_id = std::string(rest);
            } else if (kw == "ITM") {
                // ITM <item_id> <x>,<y>
                auto [item_id, xy] = split_once(rest);
                RoomItem ri;
                ri.item_id = std::string(item_id);
                auto [x, y] = parse_xy(xy, header_line + static_cast<int>(i));
                ri.x = x;
                ri.y = y;
                // Optional: DLG <dlg_id> after position
                // (some items carry a per-instance dialog override)
                room.items.push_back(std::move(ri));
            } else if (kw == "EXT") {
                room.exits.push_back(
                    parse_exit(rest, header_line + static_cast<int>(i)));
            } else if (kw == "END") {
                // END <ending_id> <x>,<y>
                auto [end_id, xy] = split_once(rest);
                EndingRef er;
                er.ending_id = std::string(end_id);
                auto [x, y] = parse_xy(xy, header_line + static_cast<int>(i));
                er.x = x;
                er.y = y;
                room.endings.push_back(std::move(er));
            } else if (kw == "WAL") {
                // Legacy wall list: WAL a,b,c,…  (mark tiles as walls)
                // We handle WAL per-tile in parse_tile; here it's a noop for
                // Phase 0 (wall data is on the Tile, not the Room).
            }
            ++i;
        }

        game.rooms[room.id] = std::move(room);
    }

    // Parse one 16-element tile row (comma-separated or legacy single-char).
    void parse_room_tile_row(std::string_view line, Room& room,
                             int row, int lineno) {
        if (line.find(',') != std::string_view::npos) {
            // Comma-separated format.
            auto tokens = split(line, ',');
            if (static_cast<int>(tokens.size()) != kMapSize)
                throw ParseError(
                    "room row has " + std::to_string(tokens.size()) +
                    " columns (expected " + std::to_string(kMapSize) + ")",
                    lineno);
            for (int col = 0; col < kMapSize; ++col)
                room.tiles[row][col] = std::string(tokens[col]);
        } else {
            // Legacy single-character format.
            if (static_cast<int>(line.size()) < kMapSize)
                throw ParseError(
                    "legacy room row is too short (" +
                    std::to_string(line.size()) + " chars)", lineno);
            for (int col = 0; col < kMapSize; ++col)
                room.tiles[row][col] = std::string(1, line[col]);
        }
    }

    // Parse "x,y <dest_room> dx,dy [TRANSITION <fx>] [DLG <id>]"
    Exit parse_exit(std::string_view rest, int lineno) {
        Exit ex;
        // src position
        auto [src_xy, rest2] = split_once(rest);
        auto [sx, sy] = parse_xy(src_xy, lineno);
        ex.x = sx;
        ex.y = sy;
        // dest room
        auto [dest_room, rest3] = split_once(rest2);
        ex.dest_room_id = std::string(dest_room);
        // dest position
        auto [dest_xy, rest4] = split_once(rest3);
        auto [dx, dy] = parse_xy(dest_xy, lineno);
        ex.dest_x = dx;
        ex.dest_y = dy;
        // Optional tokens: TRANSITION <fx> DLG <id>
        while (!rest4.empty()) {
            auto [tok, remaining] = split_once(rest4);
            if (tok == "TRANSITION" || tok == "FX") {
                auto [fx, r2] = split_once(remaining);
                ex.transition_effect = std::string(fx);
                rest4 = r2;
            } else if (tok == "DLG") {
                auto [dlg_id, r2] = split_once(remaining);
                ex.dialog_id = std::string(dlg_id);
                rest4 = r2;
            } else {
                rest4 = remaining;  // skip unknown token
            }
        }
        return ex;
    }

    // -----------------------------------------------------------------------
    // DLG
    // -----------------------------------------------------------------------
    void parse_dialogue(std::string id, Game& game) {
        ++pos_;
        std::vector<std::string> body = read_dialog_body();

        Dialogue dlg;
        dlg.id = id;

        // Collect all lines as raw content.
        std::string content;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (i > 0) content += '\n';
            content += body[i];
        }
        dlg.content = std::move(content);

        game.dialogues[dlg.id] = std::move(dlg);
    }

    // -----------------------------------------------------------------------
    // VAR
    // -----------------------------------------------------------------------
    void parse_variable(std::string id, Game& game) {
        ++pos_;
        std::vector<std::string> body = read_body();

        Variable var;
        var.name = id;
        if (!body.empty()) var.value = trim(body[0]);
        game.variables[var.name] = std::move(var);
    }

    // -----------------------------------------------------------------------
    // END  (ending definition)
    // -----------------------------------------------------------------------
    void parse_ending(std::string id, Game& game) {
        ++pos_;
        std::vector<std::string> body = read_dialog_body();

        Ending ending;
        ending.id = id;

        // Collect text lines until a NAME sub-key.
        std::string text;
        for (std::size_t i = 0; i < body.size(); ++i) {
            std::string_view line = trim(body[i]);
            auto [kw, rest] = split_once(line);
            if (kw == "NAME") {
                ending.name = std::string(rest);
                // NAME is typically the last sub-key.
                break;
            }
            if (i > 0) text += '\n';
            text += body[i];
        }
        ending.text = std::move(text);

        game.endings[ending.id] = std::move(ending);
    }
};

// ===========================================================================
// Public entry point
// ===========================================================================

Game parse(std::string_view text) {
    return Parser(text).parse();
}

} // namespace citsy
