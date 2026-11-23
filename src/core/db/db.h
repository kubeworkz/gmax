#pragma once

#include <cstdint>
#include <string>

#include "core/db/design.h"
#include "core/db/flat.h"
#include "core/db/status.h"
#include "core/db/string_table.h"

namespace gmax::db {

// The design database: one hierarchical source of truth and the flattened view
// derived from it (architecture 6.1). flatten() also puts the design into
// canonical form — modules, instances and gates sorted by name — so that two
// designs that differ only in insertion order serialize to the same bytes.
class DesignDb {
  public:
    Status flatten();

    Status save(const std::string& path) const;
    static Status load(const std::string& path, DesignDb& out);

    const Design& design() const { return design_; }
    Design& design() { return design_; }
    const FlatNetlist& flat() const { return flat_; }
    const StringTable& strings() const { return strings_; }

  private:
    Design design_;
    FlatNetlist flat_;
    StringTable strings_;
};

}  // namespace gmax::db
