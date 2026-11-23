#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gmax::frontend {

// A small JSON tree, just enough for Yosys write_json. Object members are kept
// in key order, which is also how Yosys writes them, so importing the same
// design twice walks cells and ports in the same order.
struct JsonValue {
    enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };

    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<std::shared_ptr<JsonValue>> array;
    std::map<std::string, std::shared_ptr<JsonValue>> object;

    const JsonValue* find(std::string_view key) const;

    bool is_object() const { return kind == Kind::Object; }
    bool is_array() const { return kind == Kind::Array; }
    bool is_string() const { return kind == Kind::String; }
    bool is_number() const { return kind == Kind::Number; }
};

// Parses one JSON document. On failure `error` names the line and column and
// `out` is not used.
bool parse_json(std::string_view text, JsonValue& out, std::string& error);

}  // namespace gmax::frontend
