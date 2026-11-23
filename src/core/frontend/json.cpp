#include "core/frontend/json.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

namespace gmax::frontend {

const JsonValue* JsonValue::find(const std::string_view key) const {
    if (kind != Kind::Object) {
        return nullptr;
    }
    const auto it = object.find(std::string(key));
    return it == object.end() ? nullptr : it->second.get();
}

namespace {

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(JsonValue& out, std::string& error) {
        if (!value(out, 0)) {
            error = error_;
            return false;
        }
        skip_space();
        if (pos_ != text_.size()) {
            error = message("unexpected trailing content");
            return false;
        }
        return true;
    }

  private:
    bool value(JsonValue& out, int depth) {
        if (depth > 256) {
            return fail("JSON nesting is too deep");
        }
        skip_space();
        if (pos_ >= text_.size()) {
            return fail("expected a JSON value");
        }
        switch (text_[pos_]) {
        case '{':
            return object(out, depth);
        case '[':
            return array(out, depth);
        case '"':
            out.kind = JsonValue::Kind::String;
            return string(out.text);
        case 't':
            out.kind = JsonValue::Kind::Bool;
            out.boolean = true;
            return literal("true");
        case 'f':
            out.kind = JsonValue::Kind::Bool;
            out.boolean = false;
            return literal("false");
        case 'n':
            out.kind = JsonValue::Kind::Null;
            return literal("null");
        default:
            if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
                out.kind = JsonValue::Kind::Number;
                return number(out.number);
            }
            return fail("expected a JSON value");
        }
    }

    bool object(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::Object;
        ++pos_;
        skip_space();
        if (take('}')) {
            return true;
        }
        while (true) {
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return fail("expected an object key");
            }
            std::string key;
            if (!string(key)) {
                return false;
            }
            skip_space();
            if (!take(':')) {
                return fail("expected ':' after an object key");
            }
            auto child = std::make_shared<JsonValue>();
            if (!value(*child, depth + 1)) {
                return false;
            }
            out.object.emplace(std::move(key), std::move(child));
            skip_space();
            if (take('}')) {
                return true;
            }
            if (!take(',')) {
                return fail("expected ',' or '}'");
            }
        }
    }

    bool array(JsonValue& out, int depth) {
        out.kind = JsonValue::Kind::Array;
        ++pos_;
        skip_space();
        if (take(']')) {
            return true;
        }
        while (true) {
            auto child = std::make_shared<JsonValue>();
            if (!value(*child, depth + 1)) {
                return false;
            }
            out.array.push_back(std::move(child));
            skip_space();
            if (take(']')) {
                return true;
            }
            if (!take(',')) {
                return fail("expected ',' or ']'");
            }
        }
    }

    bool string(std::string& out) {
        if (!take('"')) {
            return fail("expected a string");
        }
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return fail("control character in a string");
            }
            if (c != '\\') {
                out.push_back(c);
                advance();
                continue;
            }
            ++pos_;
            if (pos_ >= text_.size()) {
                return fail("unterminated escape");
            }
            const char escaped = text_[pos_];
            ++pos_;
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                if (!unicode(out)) {
                    return false;
                }
                break;
            default:
                return fail("unknown string escape");
            }
        }
        return fail("unterminated string");
    }

    bool unicode(std::string& out) {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= text_.size()) {
                return fail("short unicode escape");
            }
            const char c = text_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code += static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code += static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code += static_cast<unsigned>(c - 'A' + 10);
            } else {
                return fail("invalid unicode escape");
            }
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        return true;
    }

    bool number(double& out) {
        const char* begin = text_.data() + pos_;
        char* end = nullptr;
        errno = 0;
        const double parsed = std::strtod(begin, &end);
        if (end == begin || errno == ERANGE || !std::isfinite(parsed)) {
            return fail("invalid number");
        }
        const std::size_t consumed = static_cast<std::size_t>(end - begin);
        if (consumed > text_.size() - pos_) {
            return fail("invalid number");
        }
        for (std::size_t i = 0; i < consumed; ++i) {
            advance();
        }
        out = parsed;
        return true;
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return fail("invalid literal");
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            advance();
        }
        return true;
    }

    bool take(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            advance();
            return true;
        }
        return false;
    }

    void advance() {
        if (pos_ >= text_.size()) {
            return;
        }
        if (text_[pos_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++pos_;
    }

    void skip_space() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                return;
            }
            advance();
        }
    }

    bool fail(std::string_view what) {
        error_ = message(what);
        return false;
    }

    std::string message(std::string_view what) const {
        return "json: " + std::string(what) + " at line " + std::to_string(line_) + ", column " +
               std::to_string(column_);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
    std::string error_;
};

}  // namespace

bool parse_json(const std::string_view text, JsonValue& out, std::string& error) {
    Parser parser(text);
    return parser.parse(out, error);
}

}  // namespace gmax::frontend
