#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/db/ids.h"

namespace gmax::db {

// One copy of every name or path used by the design (architecture 6.6). Ids
// ascend in first-intern order and are never reused, so a serialized table is
// already in sorted key order.
class StringTable {
  public:
    StringId intern(std::string_view text);
    std::string_view at(StringId id) const;
    bool contains(StringId id) const { return id < entries_.size(); }

    std::uint32_t size() const { return static_cast<std::uint32_t>(entries_.size()); }
    const std::vector<std::string>& entries() const { return entries_; }
    void clear();

    friend bool operator==(const StringTable&, const StringTable&) = default;

  private:
    std::vector<std::string> entries_;
    std::unordered_map<std::string, StringId> index_;
};

}  // namespace gmax::db
