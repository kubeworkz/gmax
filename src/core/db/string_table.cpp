#include "core/db/string_table.h"

namespace gmax::db {

StringId StringTable::intern(std::string_view text) {
    const std::string key(text);
    if (const auto it = index_.find(key); it != index_.end()) {
        return it->second;
    }
    const auto id = static_cast<StringId>(entries_.size());
    entries_.push_back(key);
    index_.emplace(key, id);
    return id;
}

std::string_view StringTable::at(StringId id) const {
    if (!contains(id)) {
        return {};
    }
    return entries_[id];
}

void StringTable::clear() {
    entries_.clear();
    index_.clear();
}

}  // namespace gmax::db
