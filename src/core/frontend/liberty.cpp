#include "core/frontend/liberty.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace gmax::frontend {
namespace {

struct Token {
    enum class Kind : std::uint8_t { Ident, String, Number, Punct, End };
    Kind kind = Kind::End;
    std::string text;
    std::uint32_t line = 0;
};

bool is_ident_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '$' || c == '[' || c == ']' ||
           c == '.';
}

bool is_punct(char c) {
    return c == '{' || c == '}' || c == '(' || c == ')' || c == ':' || c == ';' || c == ',';
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

class Lexer {
  public:
    explicit Lexer(std::string_view text) : text_(text) {}

    db::Status run(std::vector<Token>& out) {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                ++pos_;
                continue;
            }
            if (is_space(c)) {
                ++pos_;
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                db::Status status = skip_block_comment();
                if (!status.ok()) {
                    return status;
                }
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            if (c == '"') {
                db::Status status = read_string(out);
                if (!status.ok()) {
                    return status;
                }
                continue;
            }
            if (is_punct(c)) {
                Token token;
                token.kind = Token::Kind::Punct;
                token.text = std::string(1, c);
                token.line = line_;
                out.push_back(std::move(token));
                ++pos_;
                continue;
            }
            if ((c >= '0' && c <= '9') || (c == '-' && pos_ + 1 < text_.size() &&
                                           text_[pos_ + 1] >= '0' && text_[pos_ + 1] <= '9')) {
                const std::size_t begin = pos_;
                ++pos_;
                while (pos_ < text_.size() &&
                       ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.')) {
                    ++pos_;
                }
                Token token;
                token.kind = Token::Kind::Number;
                token.text = std::string(text_.substr(begin, pos_ - begin));
                token.line = line_;
                out.push_back(std::move(token));
                continue;
            }
            if (is_ident_start(c)) {
                const std::size_t begin = pos_;
                while (pos_ < text_.size() && is_ident_char(text_[pos_])) {
                    ++pos_;
                }
                Token token;
                token.kind = Token::Kind::Ident;
                token.text = std::string(text_.substr(begin, pos_ - begin));
                token.line = line_;
                out.push_back(std::move(token));
                continue;
            }
            return fail("unexpected character");
        }
        Token end;
        end.kind = Token::Kind::End;
        end.line = line_;
        out.push_back(std::move(end));
        return {};
    }

  private:
    db::Status fail(std::string_view what) const {
        return db::Status::failure(db::Error::BadPayload, "liberty: " + std::string(what) +
                                                              " on line " + std::to_string(line_));
    }

    db::Status skip_block_comment() {
        pos_ += 2;
        while (pos_ + 1 < text_.size()) {
            if (text_[pos_] == '*' && text_[pos_ + 1] == '/') {
                pos_ += 2;
                return {};
            }
            if (text_[pos_] == '\n') {
                ++line_;
            }
            ++pos_;
        }
        return fail("unterminated comment");
    }

    db::Status read_string(std::vector<Token>& out) {
        const std::uint32_t start_line = line_;
        ++pos_;
        const std::size_t begin = pos_;
        while (pos_ < text_.size() && text_[pos_] != '"') {
            if (text_[pos_] == '\n') {
                return fail("unterminated string");
            }
            ++pos_;
        }
        if (pos_ >= text_.size()) {
            return fail("unterminated string");
        }
        Token token;
        token.kind = Token::Kind::String;
        token.text = std::string(text_.substr(begin, pos_ - begin));
        token.line = start_line;
        out.push_back(std::move(token));
        ++pos_;
        return {};
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
};

class ItemParser {
  public:
    explicit ItemParser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    db::Status parse(std::vector<Item>& out) { return statements(false, out); }

  private:
    const Token& peek() const { return tokens_[pos_]; }

    bool is_punct(char c) const {
        const Token& token = peek();
        return token.kind == Token::Kind::Punct && !token.text.empty() && token.text[0] == c;
    }

    db::Status fail(std::string_view what) const {
        return db::Status::failure(db::Error::BadPayload, "liberty: " + std::string(what) +
                                                              " on line " +
                                                              std::to_string(peek().line));
    }

    db::Status statements(bool nested, std::vector<Item>& out) {
        while (true) {
            if (peek().kind == Token::Kind::End) {
                if (nested) {
                    return fail("unterminated group");
                }
                return {};
            }
            if (is_punct('}')) {
                if (!nested) {
                    return fail("unexpected '}'");
                }
                ++pos_;
                return {};
            }
            if (is_punct(';')) {
                ++pos_;
                continue;
            }
            if (peek().kind != Token::Kind::Ident) {
                return fail("expected an attribute or group name");
            }
            Item item;
            item.name = peek().text;
            item.line = peek().line;
            ++pos_;

            if (is_punct('(')) {
                ++pos_;
                while (!is_punct(')')) {
                    if (peek().kind == Token::Kind::End) {
                        return fail("unterminated argument list");
                    }
                    if (is_punct(',')) {
                        ++pos_;
                        continue;
                    }
                    if (peek().kind == Token::Kind::Punct) {
                        return fail("unexpected punctuation in an argument list");
                    }
                    item.args.push_back(peek().text);
                    ++pos_;
                }
                ++pos_;
            }

            if (is_punct(':')) {
                ++pos_;
                std::string value;
                while (!is_punct(';') && !is_punct('}') && peek().kind != Token::Kind::End) {
                    if (!value.empty()) {
                        value += ' ';
                    }
                    value += peek().text;
                    ++pos_;
                }
                if (value.empty()) {
                    return fail("attribute '" + item.name + "' has no value");
                }
                item.args.push_back(std::move(value));
            }

            if (is_punct('{')) {
                ++pos_;
                db::Status status = statements(true, item.children);
                if (!status.ok()) {
                    return status;
                }
            } else if (is_punct(';')) {
                ++pos_;
            } else {
                return fail("expected ';' or '{' after '" + item.name + "'");
            }
            out.push_back(std::move(item));
        }
    }

    const std::vector<Token>& tokens_;
    std::size_t pos_ = 0;
};

const Item* find_child(const Item& item, std::string_view name) {
    for (const Item& child : item.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

std::string first_arg(const Item& item) { return item.args.empty() ? std::string() : item.args[0]; }

bool is_space_char(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && is_space_char(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_space_char(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && is_space_char(text[pos])) {
            ++pos;
        }
        const std::size_t begin = pos;
        while (pos < text.size() && !is_space_char(text[pos])) {
            ++pos;
        }
        if (pos > begin) {
            out.push_back(std::string(text.substr(begin, pos - begin)));
        }
    }
    return out;
}

std::vector<std::string> split_rows(std::string_view text) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    for (std::size_t pos = 0; pos <= text.size(); ++pos) {
        if (pos == text.size() || text[pos] == ',' || text[pos] == ';') {
            const std::string row = trim(text.substr(begin, pos - begin));
            if (!row.empty()) {
                out.push_back(row);
            }
            begin = pos + 1;
        }
    }
    return out;
}

std::vector<std::string> split_fields(std::string_view text, char separator) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (true) {
        const std::size_t at = text.find(separator, begin);
        if (at == std::string_view::npos) {
            out.push_back(trim(text.substr(begin)));
            return out;
        }
        out.push_back(trim(text.substr(begin, at - begin)));
        begin = at + 1;
    }
}

db::Status read_ff(const Item& item, Ff& out) {
    if (out.kind != Ff::Kind::None) {
        return db::Status::failure(db::Error::BadPayload,
                                   "liberty: a pin cannot declare both ff and latch");
    }
    out = Ff{};
    out.kind = item.name == "ff" ? Ff::Kind::FlipFlop : Ff::Kind::Latch;
    out.state = first_arg(item);
    if (const Item* clocked_on = find_child(item, "clocked_on")) {
        out.clock = trim(first_arg(*clocked_on));
    }
    if (const Item* enable = find_child(item, "enable")) {
        out.enable = trim(first_arg(*enable));
    }
    if (const Item* next_state = find_child(item, "next_state")) {
        out.next_state = trim(first_arg(*next_state));
    } else if (const Item* data_in = find_child(item, "data_in")) {
        out.next_state = trim(first_arg(*data_in));
    }
    if (const Item* clear = find_child(item, "clear")) {
        out.clear = trim(first_arg(*clear));
    }
    if (const Item* preset = find_child(item, "preset")) {
        out.preset = trim(first_arg(*preset));
    }
    return {};
}

db::Status read_pin(const Item& item, Pin& out) {
    out = Pin{};
    out.name = first_arg(item);
    if (out.name.empty()) {
        return db::Status::failure(db::Error::BadPayload, "liberty: pin without a name");
    }
    const Item* direction = find_child(item, "direction");
    if (direction == nullptr) {
        return db::Status::failure(db::Error::BadPayload,
                                   "liberty: pin '" + out.name + "' has no direction");
    }
    const std::string text = trim(first_arg(*direction));
    if (text == "input") {
        out.direction = Direction::Input;
    } else if (text == "output") {
        out.direction = Direction::Output;
    } else if (text == "inout") {
        out.direction = Direction::Inout;
    } else if (text == "internal") {
        out.direction = Direction::Internal;
    } else {
        return db::Status::failure(db::Error::BadPayload, "liberty: pin '" + out.name +
                                                              "' has unknown direction '" + text +
                                                              "'");
    }
    if (const Item* function = find_child(item, "function")) {
        out.function = trim(first_arg(*function));
    }
    if (const Item* three_state = find_child(item, "three_state")) {
        out.three_state = trim(first_arg(*three_state)) == "true";
    }
    if (const Item* clock = find_child(item, "clock")) {
        out.clock = trim(first_arg(*clock)) == "true";
    }
    for (const Item& child : item.children) {
        if (child.name == "ff" || child.name == "latch") {
            db::Status status = read_ff(child, out.ff);
            if (!status.ok()) {
                return status;
            }
        } else if (child.name == "timing") {
            out.timing.push_back(child);
        }
    }
    return {};
}

db::Status read_statetable(const Item& item, StateTable& out) {
    if (item.args.size() < 2) {
        return db::Status::failure(
            db::Error::BadPayload,
            "liberty: statetable needs input and state variable names on line " +
                std::to_string(item.line));
    }
    out.input_names = split_words(item.args[0]);
    out.state_name = trim(item.args[1]);
    const Item* table = find_child(item, "table");
    if (table == nullptr) {
        return db::Status::failure(db::Error::BadPayload,
                                   "liberty: statetable has no table attribute");
    }
    const std::string text = first_arg(*table);
    for (const std::string& row_text : split_rows(text)) {
        const std::vector<std::string> fields = split_fields(row_text, ':');
        if (fields.size() != 3) {
            return db::Status::failure(db::Error::BadPayload, "liberty: statetable row '" +
                                                                  row_text +
                                                                  "' is not inputs:current:next");
        }
        StateRow row;
        row.inputs = split_words(fields[0]);
        row.current = fields[1];
        row.next = fields[2];
        if (row.inputs.size() != out.input_names.size()) {
            return db::Status::failure(db::Error::BadPayload,
                                       "liberty: statetable row '" + row_text + "' has " +
                                           std::to_string(row.inputs.size()) +
                                           " input values, expected " +
                                           std::to_string(out.input_names.size()));
        }
        out.rows.push_back(std::move(row));
    }
    if (out.rows.empty()) {
        return db::Status::failure(db::Error::BadPayload, "liberty: statetable has no rows");
    }
    return {};
}

db::Status read_test_cell(const Item& item, TestCell& out) {
    out = TestCell{};
    out.defined = true;
    for (const Item& child : item.children) {
        if (child.name != "pin") {
            continue;
        }
        const std::string pin_name = first_arg(child);
        if (pin_name.empty()) {
            return db::Status::failure(db::Error::BadPayload,
                                       "liberty: test_cell pin without a name");
        }
        if (const Item* signal = find_child(child, "signal_type")) {
            out.signals[trim(first_arg(*signal))] = pin_name;
        }
    }
    return {};
}

db::Status read_cell(const Item& item, Cell& out) {
    out = Cell{};
    out.name = first_arg(item);
    if (out.name.empty()) {
        return db::Status::failure(db::Error::BadPayload, "liberty: cell without a name on line " +
                                                              std::to_string(item.line));
    }
    if (const Item* area = find_child(item, "area")) {
        const std::string text = trim(first_arg(*area));
        char* end = nullptr;
        out.area = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            return db::Status::failure(db::Error::BadPayload, "liberty: cell '" + out.name +
                                                                  "' has unreadable area '" + text +
                                                                  "'");
        }
    }
    for (const Item& child : item.children) {
        db::Status status;
        if (child.name == "pin") {
            Pin pin;
            status = read_pin(child, pin);
            if (!status.ok()) {
                return status;
            }
            if (out.find_pin(pin.name) != nullptr) {
                return db::Status::failure(db::Error::BadPayload, "liberty: cell '" + out.name +
                                                                      "' declares pin '" +
                                                                      pin.name + "' twice");
            }
            out.pins.push_back(std::move(pin));
        } else if (child.name == "statetable") {
            status = read_statetable(child, out.statetable);
            if (!status.ok()) {
                return status;
            }
        } else if (child.name == "test_cell") {
            status = read_test_cell(child, out.test_cell);
            if (!status.ok()) {
                return status;
            }
        }
    }
    return {};
}

}  // namespace

db::Status parse_items(std::string_view text, std::vector<Item>& out) {
    std::vector<Token> tokens;
    Lexer lexer(text);
    db::Status status = lexer.run(tokens);
    if (!status.ok()) {
        return status;
    }
    ItemParser parser(tokens);
    return parser.parse(out);
}

std::string_view direction_name(Direction direction) {
    switch (direction) {
    case Direction::Input:
        return "input";
    case Direction::Output:
        return "output";
    case Direction::Inout:
        return "inout";
    case Direction::Internal:
        return "internal";
    }
    return "unknown";
}

db::Status read_library(std::string_view text, Library& out) {
    std::vector<Item> items;
    db::Status status = parse_items(text, items);
    if (!status.ok()) {
        return status;
    }

    const Item* root = nullptr;
    for (const Item& item : items) {
        if (item.name == "library") {
            root = &item;
            break;
        }
    }
    Library library;
    if (root != nullptr) {
        library.name = first_arg(*root);
    }
    const std::vector<Item>& scope = root != nullptr ? root->children : items;
    for (const Item& item : scope) {
        if (item.name != "cell") {
            continue;
        }
        Cell cell;
        status = read_cell(item, cell);
        if (!status.ok()) {
            return status;
        }
        for (const Cell& existing : library.cells) {
            if (existing.name == cell.name) {
                return db::Status::failure(db::Error::BadPayload,
                                           "liberty: duplicate cell '" + cell.name + "'");
            }
        }
        cell.index = static_cast<std::uint32_t>(library.cells.size());
        library.cells.push_back(std::move(cell));
    }
    if (library.cells.empty()) {
        return db::Status::failure(db::Error::BadPayload, "liberty: no cell definitions found");
    }
    out = std::move(library);
    return {};
}

db::Status read_library_file(const std::string& path, Library& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return db::Status::failure(db::Error::IoError, "cannot open '" + path + "'");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        return db::Status::failure(db::Error::IoError, "cannot read '" + path + "'");
    }
    return read_library(buffer.str(), out);
}

const Pin* Cell::find_pin(std::string_view pin_name) const {
    for (const Pin& pin : pins) {
        if (pin.name == pin_name) {
            return &pin;
        }
    }
    return nullptr;
}

const Cell* Library::find_cell(std::string_view cell_name) const {
    for (const Cell& cell : cells) {
        if (cell.name == cell_name) {
            return &cell;
        }
    }
    return nullptr;
}

}  // namespace gmax::frontend
