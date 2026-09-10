#pragma once

// Internal parser API.  Not part of the public citsy ABI.

#include "src/model/game.hpp"
#include <citsy/engine.hpp>   // ParseError

#include <string_view>

namespace citsy {

/// Parse a complete .bitsy file from @p text.
///
/// @throws ParseError on any malformed input.
Game parse(std::string_view text);

} // namespace citsy
