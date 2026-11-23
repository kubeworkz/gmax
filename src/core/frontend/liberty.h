#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/ids.h"
#include "core/db/status.h"

namespace gmax::frontend {

// One parsed Liberty statement: an attribute ("area : 1.0 ;") or a group
// ("cell (AND2) { ... }"). An attribute keeps its value as a single argument;
// a group keeps its children, so unknown groups can be skipped without losing
// the ability to retain the ones v0 stores but does not read (timing).
struct Item {
    std::string name;
    std::vector<std::string> args;
    std::vector<Item> children;
    std::uint32_t line = 0;
};

// Parses Liberty text into statement trees. Errors are BadPayload with the line.
db::Status parse_items(std::string_view text, std::vector<Item>& out);

enum class Direction : std::uint8_t { Input, Output, Inout, Internal };

std::string_view direction_name(Direction direction);

// The ff and latch groups are recorded with one shape; kind says which declared
// them. Standard Liberty gives enable/data_in to the latch group; an enable in
// an ff group is a vendor extension. state is the current-state variable (IQ);
// clock and enable hold clocked_on and enable as written in the library.
struct Ff {
    enum class Kind : std::uint8_t { None, FlipFlop, Latch };

    Kind kind = Kind::None;
    std::string state;
    std::string clock;
    std::string enable;
    std::string next_state;
    std::string clear;
    std::string preset;
};

struct StateRow {
    std::vector<std::string> inputs;
    std::string current;
    std::string next;
};

struct StateTable {
    std::vector<std::string> input_names;
    std::string state_name;
    std::vector<StateRow> rows;
};

struct Pin {
    std::string name;
    Direction direction = Direction::Input;
    std::string function;
    bool three_state = false;
    bool clock = false;
    Ff ff;
    std::vector<Item> timing;  // retained for later timing work, unread in v0
};

// test_cell's pin roles, keyed by signal_type ("test_scan_in" -> "SI"). WP-101
// turns this into scan mapping; WP-003 only preserves what the library declared.
struct TestCell {
    bool defined = false;
    std::map<std::string, std::string> signals;
};

struct Cell {
    std::uint32_t index = db::kInvalidId;  // position in Library::cells
    std::string name;
    double area = 0.0;
    std::vector<Pin> pins;
    StateTable statetable;
    TestCell test_cell;

    const Pin* find_pin(std::string_view pin_name) const;
};

struct Library {
    std::string name;
    std::vector<Cell> cells;

    const Cell* find_cell(std::string_view cell_name) const;
};

// Reads the ATPG/scan subset of a Liberty file (architecture 7.2). Unknown
// attributes and groups are ignored rather than rejected; a malformed known
// group is a BadPayload error naming the line.
db::Status read_library(std::string_view text, Library& out);
db::Status read_library_file(const std::string& path, Library& out);

}  // namespace gmax::frontend
