#include "core/frontend/sdc.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gmax::frontend {
namespace {

struct Token {
    enum class Kind : std::uint8_t { Word, Brace, Quote, Subcommand };
    Kind kind = Kind::Word;
    std::string text;
    std::uint32_t line = 1;
};

struct Command {
    std::vector<Token> tokens;
    std::uint32_t line = 1;
};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

void add_diagnostic(std::vector<Diagnostic>& diagnostics, DiagnosticCode code,
                    DiagnosticSeverity severity, std::string message, std::string object,
                    std::string_view source, std::uint32_t line) {
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    diagnostic.object = std::move(object);
    diagnostic.source.file = std::string(source);
    diagnostic.source.line = line;
    diagnostics.push_back(std::move(diagnostic));
}

void add_error(std::vector<Diagnostic>& diagnostics, DiagnosticCode code, std::string message,
               std::string object, std::string_view source, std::uint32_t line) {
    add_diagnostic(diagnostics, code, DiagnosticSeverity::Error, std::move(message),
                   std::move(object), source, line);
}

void add_warning(std::vector<Diagnostic>& diagnostics, DiagnosticCode code, std::string message,
                 std::string object, std::string_view source, std::uint32_t line) {
    add_diagnostic(diagnostics, code, DiagnosticSeverity::Warning, std::move(message),
                   std::move(object), source, line);
}

class Lexer {
  public:
    Lexer(std::string_view text, std::string_view source, std::vector<Diagnostic>& diagnostics)
        : text_(text), source_(source), diagnostics_(diagnostics) {}

    bool run(std::vector<Command>& commands) {
        std::vector<Token> current;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '\n') {
                ++line_;
                ++pos_;
                flush(current, commands);
                continue;
            }
            if (c == ';') {
                ++pos_;
                flush(current, commands);
                continue;
            }
            if (is_space(c)) {
                ++pos_;
                continue;
            }
            if (c == '#') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            if (at_line_continuation()) {
                consume_line_continuation();
                continue;
            }

            Token token;
            token.line = line_;
            if (c == '{') {
                if (!read_braced(token)) {
                    return false;
                }
            } else if (c == '"') {
                if (!read_quoted(token)) {
                    return false;
                }
            } else if (c == '[') {
                if (!read_subcommand(token)) {
                    return false;
                }
            } else {
                if (!read_word(token)) {
                    return false;
                }
            }
            current.push_back(std::move(token));
        }
        flush(current, commands);
        return true;
    }

  private:
    void flush(std::vector<Token>& current, std::vector<Command>& commands) {
        if (current.empty()) {
            return;
        }
        Command command;
        command.line = current.front().line;
        command.tokens = std::move(current);
        commands.push_back(std::move(command));
        current.clear();
    }

    bool at_line_continuation() const {
        if (pos_ >= text_.size() || text_[pos_] != '\\') {
            return false;
        }
        if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '\n') {
            return true;
        }
        return pos_ + 2 < text_.size() && text_[pos_ + 1] == '\r' && text_[pos_ + 2] == '\n';
    }

    void consume_line_continuation() {
        pos_ += text_[pos_ + 1] == '\r' ? 3 : 2;
        ++line_;
    }

    bool fail(std::string message) {
        add_error(diagnostics_, DiagnosticCode::SdcSyntax, std::move(message), {}, source_, line_);
        return false;
    }

    bool read_word(Token& token) {
        token.kind = Token::Kind::Word;
        const std::uint32_t start_line = line_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (is_space(c) || c == '\n' || c == ';' || c == '[' || c == ']') {
                break;
            }
            if (at_line_continuation()) {
                break;
            }
            if (c == '\\' && pos_ + 1 < text_.size()) {
                token.text.push_back(text_[pos_ + 1]);
                pos_ += 2;
                continue;
            }
            token.text.push_back(c);
            ++pos_;
        }
        token.line = start_line;
        if (token.text.empty()) {
            return fail("unexpected character");
        }
        return true;
    }

    bool read_braced(Token& token) {
        token.kind = Token::Kind::Brace;
        token.line = line_;
        ++pos_;
        std::uint32_t depth = 1;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (at_line_continuation()) {
                token.text.push_back(' ');
                consume_line_continuation();
                continue;
            }
            if (c == '{') {
                ++depth;
                token.text.push_back(c);
                ++pos_;
            } else if (c == '}') {
                --depth;
                ++pos_;
                if (depth == 0) {
                    return true;
                }
                token.text.push_back(c);
            } else {
                if (c == '\n') {
                    ++line_;
                }
                token.text.push_back(c);
                ++pos_;
            }
        }
        return fail("unterminated braced value");
    }

    bool read_quoted(Token& token) {
        token.kind = Token::Kind::Quote;
        token.line = line_;
        ++pos_;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (at_line_continuation()) {
                token.text.push_back(' ');
                consume_line_continuation();
                continue;
            }
            if (c == '\\' && pos_ + 1 < text_.size()) {
                const char escaped = text_[pos_ + 1];
                if (escaped == '\n') {
                    ++line_;
                } else if (escaped == 'n') {
                    token.text.push_back('\n');
                } else if (escaped == 't') {
                    token.text.push_back('\t');
                } else if (escaped == 'r') {
                    token.text.push_back('\r');
                } else {
                    token.text.push_back(escaped);
                }
                pos_ += 2;
                continue;
            }
            if (c == '\n') {
                ++line_;
            }
            token.text.push_back(c);
            ++pos_;
        }
        return fail("unterminated quoted value");
    }

    bool read_subcommand(Token& token) {
        token.kind = Token::Kind::Subcommand;
        token.line = line_;
        ++pos_;
        std::uint32_t bracket_depth = 1;
        std::uint32_t brace_depth = 0;
        bool quoted = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (at_line_continuation()) {
                token.text.push_back(' ');
                consume_line_continuation();
                continue;
            }
            if (c == '\\' && pos_ + 1 < text_.size()) {
                token.text.push_back(c);
                token.text.push_back(text_[pos_ + 1]);
                pos_ += 2;
                continue;
            }
            if (c == '"' && brace_depth == 0) {
                quoted = !quoted;
                token.text.push_back(c);
                ++pos_;
                continue;
            }
            if (!quoted) {
                if (c == '{') {
                    ++brace_depth;
                } else if (c == '}' && brace_depth > 0) {
                    --brace_depth;
                } else if (c == '[' && brace_depth == 0) {
                    ++bracket_depth;
                } else if (c == ']' && brace_depth == 0) {
                    --bracket_depth;
                    if (bracket_depth == 0) {
                        ++pos_;
                        return true;
                    }
                }
            }
            if (c == '\n') {
                ++line_;
            }
            token.text.push_back(c);
            ++pos_;
        }
        return fail("unterminated command substitution");
    }

    std::string_view text_;
    std::string_view source_;
    std::vector<Diagnostic>& diagnostics_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
};

std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> words;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && is_space(text[pos])) {
            ++pos;
        }
        if (pos >= text.size()) {
            break;
        }
        const std::size_t begin = pos;
        while (pos < text.size() && !is_space(text[pos])) {
            ++pos;
        }
        words.emplace_back(text.substr(begin, pos - begin));
    }
    return words;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && is_space(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string strip_outer(std::string_view text) {
    if (text.size() >= 2 && ((text.front() == '{' && text.back() == '}') ||
                             (text.front() == '"' && text.back() == '"'))) {
        return std::string(text.substr(1, text.size() - 2));
    }
    return std::string(text);
}

std::vector<std::string> resolve_token(const Token& token, std::vector<Diagnostic>& diagnostics,
                                       std::string_view source) {
    if (token.kind != Token::Kind::Subcommand) {
        if (token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '$') {
            add_warning(diagnostics, DiagnosticCode::SdcUnsupportedCommand,
                        "Tcl variable expansion is not supported", token.text, source, token.line);
            return {};
        }
        if (token.kind == Token::Kind::Brace || token.kind == Token::Kind::Quote) {
            return split_list(token.text);
        }
        return {token.text};
    }

    std::string inner = trim(token.text);
    const std::size_t split = inner.find_first_of(" \t\r\n");
    const std::string query = split == std::string::npos ? inner : inner.substr(0, split);
    if (query != "get_ports" && query != "get_pins" && query != "get_clocks" &&
        query != "get_nets" && query != "get_cells") {
        add_warning(diagnostics, DiagnosticCode::SdcUnsupportedCommand,
                    "unsupported object query '" + query + "'; no objects returned", query, source,
                    token.line);
        return {};
    }
    if (split == std::string::npos) {
        return {};
    }
    std::string rest = trim(inner.substr(split));
    if (!rest.empty() && rest.front() == '-') {
        add_warning(diagnostics, DiagnosticCode::SdcUnsupportedOption,
                    "object-query options are not supported; using the remaining literal "
                    "objects",
                    query, source, token.line);
    }
    return split_list(strip_outer(rest));
}

// A member names a clock, not the port or pin that clock is defined on. One
// source can carry more than one clock, so the source is not a key that picks a
// single member; the declared name is.
bool is_clock_source(const SdcConstraints& constraints, const std::string_view name) {
    for (const SdcClock& clock : constraints.clocks) {
        if (clock.source == name) {
            return true;
        }
    }
    return false;
}

// A master is validated after the whole file is read for the same reason a
// group is: SDC does not require a declaration to precede its use.
void validate_generated_clocks(const SdcConstraints& constraints,
                               std::vector<Diagnostic>& diagnostics,
                               const std::string_view source) {
    for (const SdcClock& clock : constraints.clocks) {
        if (!clock.generated) {
            continue;
        }
        const SdcGeneratedClockIssues issues = sdc_generated_clock_issues(constraints, clock);
        for (const std::string& master : issues.unknown) {
            std::string message = "generated clock '" + clock.name + "' takes -master_clock '" +
                                  master + "', which is not a declared clock";
            if (is_clock_source(constraints, master)) {
                message +=
                    " (it is a clock source; create_generated_clock -master_clock takes clock "
                    "names)";
            }
            add_warning(diagnostics, DiagnosticCode::SdcGeneratedClockUnknownMaster,
                        std::move(message), master, source, clock.line);
        }
        for (const std::string& master : issues.conflicting) {
            std::string message;
            if (master == clock.name) {
                message = "generated clock '" + clock.name + "' names itself as its master";
            } else {
                const SdcClock* resolved = constraints.find_clock(master);
                const std::string on = resolved == nullptr ? std::string{} : resolved->source;
                message = "generated clock '" + clock.name + "' takes -source '" +
                          clock.source_pin + "' but its master '" + master + "' is defined on '" +
                          on + "'";
            }
            add_warning(diagnostics, DiagnosticCode::SdcGeneratedClockConflictingMaster,
                        std::move(message), master, source, clock.line);
        }
    }
}

// A group is validated after the whole file is read, not as the command is
// parsed: SDC does not require a clock declaration to precede the group that
// names it.
void validate_clock_groups(const SdcConstraints& constraints, std::vector<Diagnostic>& diagnostics,
                           const std::string_view source) {
    for (const SdcClockGroup& group : constraints.clock_groups) {
        const SdcClockGroupIssues issues = sdc_clock_group_issues(constraints, group);
        for (const std::string& member : issues.unknown) {
            std::string message = "clock group member '" + member + "' is not a declared clock";
            if (is_clock_source(constraints, member)) {
                // The source form is the one a reader is most likely to write by
                // mistake, so the warning names the reason rather than only the
                // member.
                message += " (it is a clock source; set_clock_groups takes clock names)";
            }
            add_warning(diagnostics, DiagnosticCode::SdcClockGroupUnknownMember, std::move(message),
                        member, source, group.line);
        }
        for (const std::string& member : issues.conflicting) {
            add_warning(diagnostics, DiagnosticCode::SdcClockGroupConflictingMember,
                        "clock '" + member + "' is a member of more than one -group", member,
                        source, group.line);
        }
    }
}

bool parse_double(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool is_number(const std::string& text) {
    double ignored = 0.0;
    return parse_double(text, ignored);
}

bool parse_positive_int(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed <= 0) {
        return false;
    }
    value = parsed;
    return true;
}

class Parser {
  public:
    Parser(SdcConstraints& constraints, std::vector<Diagnostic>& diagnostics,
           std::string_view source)
        : constraints_(constraints), diagnostics_(diagnostics), source_(source) {}

    bool parse(const Command& command);

  private:
    bool error(DiagnosticCode code, std::string message, std::uint32_t line,
               std::string object = {});
    bool take_value(const Command& command, std::size_t& index, Token& value,
                    std::string_view option);
    bool one_object(const Token& token, std::string& object, std::string_view what);
    bool many_objects(const Token& token, std::vector<std::string>& objects, std::string_view what);
    bool add_clock(SdcClock clock);
    bool parse_create_clock(const Command& command);
    bool parse_create_generated_clock(const Command& command);
    bool parse_set_clock_groups(const Command& command);
    bool parse_set_false_path(const Command& command);
    bool parse_set_case_analysis(const Command& command);
    bool parse_delay(const Command& command, SdcDelayKind kind);
    bool parse_set_driving_cell(const Command& command);
    bool parse_set_load(const Command& command);

    SdcConstraints& constraints_;
    std::vector<Diagnostic>& diagnostics_;
    std::string_view source_;
};

bool Parser::error(const DiagnosticCode code, std::string message, const std::uint32_t line,
                   std::string object) {
    add_error(diagnostics_, code, std::move(message), std::move(object), source_, line);
    return false;
}

bool Parser::take_value(const Command& command, std::size_t& index, Token& value,
                        const std::string_view option) {
    if (index + 1 >= command.tokens.size()) {
        return error(DiagnosticCode::SdcSyntax,
                     "option '" + std::string(option) + "' requires a value",
                     command.tokens[index].line, std::string(option));
    }
    value = command.tokens[++index];
    return true;
}

bool Parser::one_object(const Token& token, std::string& object, const std::string_view what) {
    std::vector<std::string> objects = resolve_token(token, diagnostics_, source_);
    if (objects.size() != 1 || objects.front().empty()) {
        return error(DiagnosticCode::SdcInvalidValue,
                     std::string(what) + " requires exactly one object", token.line);
    }
    object = std::move(objects.front());
    return true;
}

bool Parser::many_objects(const Token& token, std::vector<std::string>& objects,
                          const std::string_view what) {
    objects = resolve_token(token, diagnostics_, source_);
    if (objects.empty()) {
        return error(DiagnosticCode::SdcInvalidValue,
                     std::string(what) + " requires at least one object", token.line);
    }
    return true;
}

bool Parser::add_clock(SdcClock clock) {
    if (constraints_.find_clock(clock.name) != nullptr) {
        return error(DiagnosticCode::SdcDuplicateClock,
                     "clock '" + clock.name + "' is defined more than once", clock.line,
                     clock.name);
    }
    constraints_.clocks.push_back(std::move(clock));
    return true;
}

bool Parser::parse(const Command& command) {
    if (command.tokens.empty()) {
        return true;
    }
    if (command.tokens.front().kind != Token::Kind::Word) {
        return error(DiagnosticCode::SdcSyntax, "expected a command name",
                     command.tokens.front().line);
    }
    const std::string& name = command.tokens.front().text;
    if (name == "create_clock") {
        return parse_create_clock(command);
    }
    if (name == "create_generated_clock") {
        return parse_create_generated_clock(command);
    }
    if (name == "set_clock_groups") {
        return parse_set_clock_groups(command);
    }
    if (name == "set_false_path") {
        return parse_set_false_path(command);
    }
    if (name == "set_case_analysis") {
        return parse_set_case_analysis(command);
    }
    if (name == "set_input_delay") {
        return parse_delay(command, SdcDelayKind::Input);
    }
    if (name == "set_output_delay") {
        return parse_delay(command, SdcDelayKind::Output);
    }
    if (name == "set_driving_cell") {
        return parse_set_driving_cell(command);
    }
    if (name == "set_load") {
        return parse_set_load(command);
    }
    add_warning(diagnostics_, DiagnosticCode::SdcUnsupportedCommand,
                "unsupported SDC command '" + name + "'; ignored", name, source_,
                command.tokens.front().line);
    return true;
}

bool Parser::parse_create_clock(const Command& command) {
    std::string name;
    std::string source;
    double period = 0.0;
    bool has_period = false;
    bool has_waveform = false;
    double rise = 0.0;
    double fall = 0.0;
    std::vector<Token> sources;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        if (token.kind == Token::Kind::Word && token.text == "-add") {
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-name") {
            Token value;
            if (!take_value(command, i, value, "-name")) {
                return false;
            }
            name = value.text;
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-period") {
            Token value;
            if (!take_value(command, i, value, "-period")) {
                return false;
            }
            if (!parse_double(value.text, period) || period <= 0.0) {
                return error(DiagnosticCode::SdcInvalidValue,
                             "create_clock -period must be a positive number", value.line,
                             value.text);
            }
            has_period = true;
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-waveform") {
            Token value;
            if (!take_value(command, i, value, "-waveform")) {
                return false;
            }
            const std::vector<std::string> parts = split_list(value.text);
            if (parts.size() != 2 || !parse_double(parts[0], rise) ||
                !parse_double(parts[1], fall)) {
                return error(DiagnosticCode::SdcInvalidValue,
                             "create_clock -waveform requires two numbers", value.line);
            }
            has_waveform = true;
            continue;
        }
        if (token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-') {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on create_clock", token.line,
                         token.text);
        }
        sources.push_back(token);
    }

    if (!has_period) {
        return error(DiagnosticCode::SdcSyntax, "create_clock requires -period",
                     command.tokens.front().line);
    }
    if (sources.size() != 1) {
        return error(DiagnosticCode::SdcInvalidValue,
                     "create_clock requires exactly one source object",
                     command.tokens.front().line);
    }
    if (!one_object(sources.front(), source, "create_clock source")) {
        return false;
    }
    if (name.empty()) {
        name = source;
    }
    SdcClock clock;
    clock.name = name;
    clock.period = period;
    clock.source = source;
    clock.line = command.tokens.front().line;
    if (has_waveform) {
        clock.has_waveform = true;
        clock.rise = rise;
        clock.fall = fall;
    } else {
        clock.rise = 0.0;
        clock.fall = period / 2.0;
    }
    return add_clock(std::move(clock));
}

bool Parser::parse_create_generated_clock(const Command& command) {
    std::string name;
    std::string source_pin;
    std::string master_clock;
    int divide_by = 0;
    int multiply_by = 0;
    std::vector<Token> targets;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        if (token.kind == Token::Kind::Word &&
            (token.text == "-add" || token.text == "-combinational")) {
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-name") {
            Token value;
            if (!take_value(command, i, value, "-name")) {
                return false;
            }
            name = value.text;
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-source") {
            Token value;
            if (!take_value(command, i, value, "-source")) {
                return false;
            }
            if (!one_object(value, source_pin, "create_generated_clock -source")) {
                return false;
            }
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-master_clock") {
            Token value;
            if (!take_value(command, i, value, "-master_clock")) {
                return false;
            }
            master_clock = value.text;
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-divide_by") {
            Token value;
            if (!take_value(command, i, value, "-divide_by")) {
                return false;
            }
            if (!parse_positive_int(value.text, divide_by)) {
                return error(DiagnosticCode::SdcInvalidValue,
                             "create_generated_clock -divide_by must be a positive integer",
                             value.line, value.text);
            }
            continue;
        }
        if (token.kind == Token::Kind::Word && token.text == "-multiply_by") {
            Token value;
            if (!take_value(command, i, value, "-multiply_by")) {
                return false;
            }
            if (!parse_positive_int(value.text, multiply_by)) {
                return error(DiagnosticCode::SdcInvalidValue,
                             "create_generated_clock -multiply_by must be a positive integer",
                             value.line, value.text);
            }
            continue;
        }
        if (token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-') {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on create_generated_clock",
                         token.line, token.text);
        }
        targets.push_back(token);
    }

    if (name.empty()) {
        return error(DiagnosticCode::SdcSyntax, "create_generated_clock requires -name",
                     command.tokens.front().line);
    }
    if (source_pin.empty()) {
        return error(DiagnosticCode::SdcSyntax, "create_generated_clock requires -source",
                     command.tokens.front().line);
    }
    if (divide_by != 0 && multiply_by != 0) {
        return error(DiagnosticCode::SdcInvalidValue,
                     "create_generated_clock accepts -divide_by or -multiply_by, not both",
                     command.tokens.front().line);
    }
    if (targets.size() != 1) {
        return error(DiagnosticCode::SdcInvalidValue,
                     "create_generated_clock requires exactly one target object",
                     command.tokens.front().line);
    }
    std::string target;
    if (!one_object(targets.front(), target, "create_generated_clock target")) {
        return false;
    }

    SdcClock clock;
    clock.name = name;
    clock.source = target;
    clock.generated = true;
    clock.master_clock = master_clock;
    clock.source_pin = source_pin;
    clock.divide_by = divide_by;
    clock.multiply_by = multiply_by;
    clock.line = command.tokens.front().line;
    return add_clock(std::move(clock));
}

bool Parser::parse_set_clock_groups(const Command& command) {
    SdcClockGroup group;
    group.line = command.tokens.front().line;
    bool has_kind = false;
    std::string kind_flag;
    std::vector<std::vector<std::string>> groups;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        const bool is_option =
            token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-';
        if (is_option && (token.text == "-asynchronous" || token.text == "-logically_exclusive" ||
                          token.text == "-physically_exclusive")) {
            if (has_kind && kind_flag != token.text) {
                return error(DiagnosticCode::SdcInvalidValue,
                             "set_clock_groups accepts one group relationship", token.line);
            }
            has_kind = true;
            kind_flag = token.text;
            continue;
        }
        if (is_option && token.text == "-name") {
            Token value;
            if (!take_value(command, i, value, "-name")) {
                return false;
            }
            group.name = value.text;
            continue;
        }
        if (is_option && token.text == "-group") {
            Token value;
            if (!take_value(command, i, value, "-group")) {
                return false;
            }
            std::vector<std::string> clocks;
            if (!many_objects(value, clocks, "set_clock_groups -group")) {
                return false;
            }
            groups.push_back(std::move(clocks));
            continue;
        }
        if (is_option) {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on set_clock_groups", token.line,
                         token.text);
        }
        return error(DiagnosticCode::SdcSyntax,
                     "set_clock_groups expects -group options, got '" + token.text + "'",
                     token.line, token.text);
    }

    if (!has_kind) {
        return error(DiagnosticCode::SdcSyntax,
                     "set_clock_groups requires -asynchronous, -logically_exclusive or "
                     "-physically_exclusive",
                     command.tokens.front().line);
    }
    if (groups.size() < 2) {
        return error(DiagnosticCode::SdcInvalidValue,
                     "set_clock_groups requires at least two -group options",
                     command.tokens.front().line);
    }
    group.kind = kind_flag == "-asynchronous" ? SdcClockGroupKind::Asynchronous
                                              : (kind_flag == "-logically_exclusive"
                                                     ? SdcClockGroupKind::LogicallyExclusive
                                                     : SdcClockGroupKind::PhysicallyExclusive);
    group.groups = std::move(groups);
    constraints_.clock_groups.push_back(std::move(group));
    return true;
}

bool Parser::parse_set_false_path(const Command& command) {
    SdcFalsePath path;
    path.line = command.tokens.front().line;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        const bool is_option =
            token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-';
        if (is_option && (token.text == "-setup" || token.text == "-hold")) {
            continue;
        }
        if (is_option &&
            (token.text == "-from" || token.text == "-to" || token.text == "-through")) {
            Token value;
            if (!take_value(command, i, value, token.text)) {
                return false;
            }
            std::vector<std::string> objects;
            if (!many_objects(value, objects, "set_false_path " + token.text)) {
                return false;
            }
            if (token.text == "-from") {
                path.from = std::move(objects);
            } else if (token.text == "-to") {
                path.to = std::move(objects);
            } else {
                path.through = std::move(objects);
            }
            continue;
        }
        if (is_option) {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on set_false_path", token.line,
                         token.text);
        }
        return error(DiagnosticCode::SdcSyntax,
                     "set_false_path expects -from, -through or -to options", token.line,
                     token.text);
    }

    if (path.from.empty() && path.to.empty()) {
        return error(DiagnosticCode::SdcInvalidValue, "set_false_path requires -from and/or -to",
                     command.tokens.front().line);
    }
    constraints_.false_paths.push_back(std::move(path));
    return true;
}

bool Parser::parse_set_case_analysis(const Command& command) {
    bool has_value = false;
    bool value = false;
    std::vector<Token> objects;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        if (token.kind == Token::Kind::Word && token.text == "-quiet") {
            continue;
        }
        if (token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-') {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on set_case_analysis", token.line,
                         token.text);
        }
        if (!has_value) {
            if (token.text != "0" && token.text != "1") {
                return error(DiagnosticCode::SdcInvalidValue,
                             "set_case_analysis value must be 0 or 1", token.line, token.text);
            }
            value = token.text == "1";
            has_value = true;
            continue;
        }
        objects.push_back(token);
    }

    if (!has_value) {
        return error(DiagnosticCode::SdcSyntax, "set_case_analysis requires a value",
                     command.tokens.front().line);
    }
    if (objects.empty()) {
        return error(DiagnosticCode::SdcSyntax, "set_case_analysis requires an object",
                     command.tokens.front().line);
    }

    for (const Token& token : objects) {
        std::vector<std::string> resolved;
        if (!many_objects(token, resolved, "set_case_analysis object")) {
            return false;
        }
        for (std::string& object : resolved) {
            const SdcCaseAnalysis* existing = constraints_.find_case(object);
            if (existing != nullptr) {
                if (existing->value != value) {
                    return error(DiagnosticCode::SdcInvalidValue,
                                 "conflicting case analysis for '" + object + "'", token.line,
                                 object);
                }
                continue;
            }
            constraints_.case_analysis.push_back(
                SdcCaseAnalysis{std::move(object), value, token.line});
        }
    }
    return true;
}

bool Parser::parse_delay(const Command& command, const SdcDelayKind kind) {
    std::string clock;
    bool has_max = false;
    bool has_min = false;
    std::vector<Token> positionals;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        const bool is_option = token.kind == Token::Kind::Word && !token.text.empty() &&
                               token.text.front() == '-' && !is_number(token.text);
        if (is_option && token.text == "-clock") {
            Token value;
            if (!take_value(command, i, value, "-clock")) {
                return false;
            }
            if (!one_object(value, clock, "delay -clock")) {
                return false;
            }
            continue;
        }
        if (is_option &&
            (token.text == "-max" || token.text == "-min" || token.text == "-add_delay" ||
             token.text == "-rise" || token.text == "-fall")) {
            if (token.text == "-max") {
                has_max = true;
            } else if (token.text == "-min") {
                has_min = true;
            }
            continue;
        }
        if (is_option) {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on " +
                             std::string(kind == SdcDelayKind::Input ? "set_input_delay"
                                                                     : "set_output_delay"),
                         token.line, token.text);
        }
        positionals.push_back(token);
    }

    if (positionals.size() < 2) {
        return error(
            DiagnosticCode::SdcSyntax,
            std::string(kind == SdcDelayKind::Input ? "set_input_delay" : "set_output_delay") +
                " requires a value and an object",
            command.tokens.front().line);
    }
    double value = 0.0;
    if (!parse_double(positionals.front().text, value)) {
        return error(DiagnosticCode::SdcInvalidValue, "delay value must be a number",
                     positionals.front().line, positionals.front().text);
    }

    for (std::size_t i = 1; i < positionals.size(); ++i) {
        std::vector<std::string> objects;
        if (!many_objects(positionals[i], objects, "delay object")) {
            return false;
        }
        for (std::string& object : objects) {
            SdcDelay delay;
            delay.kind = kind;
            delay.object = std::move(object);
            delay.clock = clock;
            delay.value = value;
            delay.max = has_max || !has_min;
            delay.min = has_min || !has_max;
            delay.line = command.tokens.front().line;
            constraints_.delays.push_back(std::move(delay));
        }
    }
    return true;
}

bool Parser::parse_set_driving_cell(const Command& command) {
    std::string lib_cell;
    std::string pin;
    std::vector<Token> objects;

    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        const bool is_option =
            token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-';
        if (is_option && token.text == "-lib_cell") {
            Token value;
            if (!take_value(command, i, value, "-lib_cell")) {
                return false;
            }
            lib_cell = value.text;
            continue;
        }
        if (is_option && token.text == "-pin") {
            Token value;
            if (!take_value(command, i, value, "-pin")) {
                return false;
            }
            pin = value.text;
            continue;
        }
        if (is_option && token.text == "-from_pin") {
            Token value;
            if (!take_value(command, i, value, "-from_pin")) {
                return false;
            }
            continue;
        }
        if (is_option && (token.text == "-dont_scale" || token.text == "-no_design_rule")) {
            continue;
        }
        if (is_option) {
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on set_driving_cell", token.line,
                         token.text);
        }
        objects.push_back(token);
    }

    if (lib_cell.empty()) {
        return error(DiagnosticCode::SdcSyntax, "set_driving_cell requires -lib_cell",
                     command.tokens.front().line);
    }
    if (objects.empty()) {
        return error(DiagnosticCode::SdcSyntax, "set_driving_cell requires an object",
                     command.tokens.front().line);
    }

    for (const Token& token : objects) {
        std::vector<std::string> resolved;
        if (!many_objects(token, resolved, "set_driving_cell object")) {
            return false;
        }
        for (std::string& object : resolved) {
            constraints_.driving_cells.push_back(
                SdcDrivingCell{std::move(object), lib_cell, pin, command.tokens.front().line});
        }
    }
    return true;
}

bool Parser::parse_set_load(const Command& command) {
    std::vector<Token> positionals;
    for (std::size_t i = 1; i < command.tokens.size(); ++i) {
        const Token& token = command.tokens[i];
        if (token.kind == Token::Kind::Word && !token.text.empty() && token.text.front() == '-') {
            if (token.text == "-pin_load" || token.text == "-wire_load") {
                continue;
            }
            return error(DiagnosticCode::SdcUnsupportedOption,
                         "unsupported option '" + token.text + "' on set_load", token.line,
                         token.text);
        }
        positionals.push_back(token);
    }
    if (positionals.size() < 2) {
        return error(DiagnosticCode::SdcSyntax, "set_load requires a value and an object",
                     command.tokens.front().line);
    }
    double value = 0.0;
    if (!parse_double(positionals.front().text, value)) {
        return error(DiagnosticCode::SdcInvalidValue, "set_load value must be a number",
                     positionals.front().line, positionals.front().text);
    }
    for (std::size_t i = 1; i < positionals.size(); ++i) {
        std::vector<std::string> objects;
        if (!many_objects(positionals[i], objects, "set_load object")) {
            return false;
        }
        for (std::string& object : objects) {
            constraints_.loads.push_back(
                SdcLoad{std::move(object), value, command.tokens.front().line});
        }
    }
    return true;
}

}  // namespace

std::string_view sdc_clock_group_kind_name(const SdcClockGroupKind kind) {
    switch (kind) {
    case SdcClockGroupKind::Asynchronous:
        return "asynchronous";
    case SdcClockGroupKind::LogicallyExclusive:
        return "logically-exclusive";
    case SdcClockGroupKind::PhysicallyExclusive:
        return "physically-exclusive";
    }
    return "unknown";
}

const SdcClock* SdcConstraints::find_clock(const std::string_view name) const {
    for (const SdcClock& clock : clocks) {
        if (clock.name == name) {
            return &clock;
        }
    }
    return nullptr;
}

const SdcCaseAnalysis* SdcConstraints::find_case(const std::string_view object) const {
    for (const SdcCaseAnalysis& item : case_analysis) {
        if (item.object == object) {
            return &item;
        }
    }
    return nullptr;
}

SdcClockGroupIssues sdc_clock_group_issues(const SdcConstraints& constraints,
                                           const SdcClockGroup& group) {
    SdcClockGroupIssues issues;
    std::vector<std::pair<std::string, std::size_t>> placed;
    for (std::size_t index = 0; index < group.groups.size(); ++index) {
        for (const std::string& member : group.groups[index]) {
            if (constraints.find_clock(member) == nullptr &&
                std::find(issues.unknown.begin(), issues.unknown.end(), member) ==
                    issues.unknown.end()) {
                issues.unknown.push_back(member);
            }
            const auto found =
                std::find_if(placed.begin(), placed.end(),
                             [&member](const std::pair<std::string, std::size_t>& entry) {
                                 return entry.first == member;
                             });
            if (found == placed.end()) {
                placed.emplace_back(member, index);
            } else if (found->second != index &&
                       std::find(issues.conflicting.begin(), issues.conflicting.end(), member) ==
                           issues.conflicting.end()) {
                // A clock in two -groups of one command would be on both sides of
                // the relationship, which no relationship kind can mean.
                issues.conflicting.push_back(member);
            }
        }
    }
    std::sort(issues.unknown.begin(), issues.unknown.end());
    std::sort(issues.conflicting.begin(), issues.conflicting.end());
    return issues;
}

SdcGeneratedClockIssues sdc_generated_clock_issues(const SdcConstraints& constraints,
                                                   const SdcClock& clock) {
    SdcGeneratedClockIssues issues;
    if (!clock.generated || clock.master_clock.empty()) {
        return issues;
    }
    const SdcClock* master = constraints.find_clock(clock.master_clock);
    if (master == nullptr) {
        issues.unknown.push_back(clock.master_clock);
        return issues;
    }
    if (master->name == clock.name) {
        // A clock derived from itself has no root to derive from.
        issues.conflicting.push_back(clock.name);
        return issues;
    }
    // -source may name the object the master is defined on or the master clock
    // object itself; either way the two ends describe the same link. A different
    // object means the command names two links at once.
    if (clock.source_pin != master->source && clock.source_pin != master->name) {
        issues.conflicting.push_back(master->name);
    }
    return issues;
}

SdcResult read_sdc(const std::string_view text, const std::string_view source) {
    SdcResult result;
    Lexer lexer(text, source, result.diagnostics);
    std::vector<Command> commands;
    if (!lexer.run(commands)) {
        return result;
    }
    Parser parser(result.constraints, result.diagnostics, source);
    for (const Command& command : commands) {
        static_cast<void>(parser.parse(command));
    }
    // Both checks are reported, not refused: generated clocks and clock groups
    // are retained but not enforced (ADR-012 section 4), so a finding here is a
    // warning that leaves the status and the hash alone.
    validate_generated_clocks(result.constraints, result.diagnostics, source);
    validate_clock_groups(result.constraints, result.diagnostics, source);
    result.ok = !has_errors(result.diagnostics);
    return result;
}

SdcResult read_sdc_file(const std::string& path) {
    SdcResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        add_error(result.diagnostics, DiagnosticCode::IoError,
                  "cannot open SDC file '" + path + "'", path, path, 0);
        return result;
    }
    std::string text;
    char buffer[4096];
    while (file.read(buffer, static_cast<std::streamsize>(sizeof(buffer))) || file.gcount() > 0) {
        text.append(buffer, static_cast<std::size_t>(file.gcount()));
    }
    return read_sdc(text, path);
}

}  // namespace gmax::frontend
