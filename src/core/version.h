#pragma once

#include <string_view>

namespace gmax {

// Semantic version of the tool (architecture 19.6). Schema versions for .dfdb,
// fault lists, the pattern IR and reports are reported alongside it as they land.
std::string_view version();

}  // namespace gmax
