#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gmax::shell {

// Architecture 19.3 fixes the process exit codes. A handler returns one of
// these values; main() turns it into the process status. Keeping the classes in
// one enum is what stops a new command from inventing its own number.
enum class ExitCode : int {
    Success = 0,
    Usage = 1,
    Design = 2,
    Drc = 3,
    AtpgIncomplete = 4,
    Internal = 5,
};

std::string_view exit_code_name(ExitCode code);

struct PositionalSpec {
    std::string name;  // "netlist", without angle brackets
    std::string help;
    bool required = true;
};

struct OptionSpec {
    std::string name;        // includes the leading '-', e.g. "-top"
    std::string value_name;  // empty for a flag, otherwise e.g. "module"
    std::string help;
    bool required = false;
    bool repeatable = false;
};

// The schema is the single source of truth for both parsing and help text. A
// command is added to the Cli once; -help is rendered from these fields rather
// than written by hand, so the two cannot drift (architecture 19.1).
struct CommandSpec {
    std::string name;  // no leading '-'
    std::string summary;
    std::vector<PositionalSpec> positionals;
    std::vector<OptionSpec> options;
};

struct ParsedArgs {
    std::vector<std::string> positionals;
    std::map<std::string, std::vector<std::string>, std::less<>> options;

    bool has(std::string_view name) const;
    // The first value of a value option, or nullptr for an absent option or a
    // flag. A flag is present when has() is true.
    const std::string* value(std::string_view name) const;
    std::span<const std::string> values(std::string_view name) const;
};

struct ParseResult {
    bool ok = false;
    bool help = false;
    ParsedArgs args;
    std::string error;
};

// Parses the tokens after a command name against its schema. The help flags
// (-help, --help, -h) are recognized anywhere and take precedence, so a
// malformed command can still show its help. On failure `error` is a complete
// diagnostic and `ok` is false.
ParseResult parse_arguments(const CommandSpec& spec, std::span<const std::string> args);

using Handler =
    std::function<ExitCode(const ParsedArgs& args, std::ostream& out, std::ostream& err)>;

// The command registry and the global CLI contract. It installs the `help`
// command; later packages add their commands with add_command(). The class is
// non-copyable because the help handler refers back to its registry.
class Cli {
  public:
    explicit Cli(std::string version);

    Cli(const Cli&) = delete;
    Cli& operator=(const Cli&) = delete;
    Cli(Cli&&) = delete;
    Cli& operator=(Cli&&) = delete;

    // Returns false and fills `error` when the schema is malformed or the name
    // is already registered.
    bool add_command(CommandSpec spec, Handler handler, std::string& error);

    // Runs the global flags or the named command. Command handlers run inside a
    // catch-all so an unexpected exception becomes ExitCode::Internal rather
    // than an unhandled termination.
    ExitCode run(std::span<const std::string> args, std::ostream& out, std::ostream& err) const;

    std::string global_help() const;
    std::string command_help(const CommandSpec& spec) const;

  private:
    struct Entry {
        CommandSpec spec;
        Handler handler;
    };

    const Entry* find(std::string_view name) const;

    std::string version_;
    std::vector<Entry> entries_;
};

}  // namespace gmax::shell
