#pragma once

#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gmax::shell {

// Architecture 19.3 fixes the process exit codes. A handler returns one of
// these values; main() turns it into the process status.
enum class ExitCode : int {
    Success = 0,
    Usage = 1,
    Design = 2,
    Drc = 3,
    AtpgIncomplete = 4,
    Internal = 5,
};

struct PositionalSpec {
    std::string name;  // "command", without angle brackets
    std::string help;
    bool required = true;
};

// The schema is the single source of truth for both parsing and -help. It
// currently describes positional arguments only; option support lands with the
// first command that needs it.
struct CommandSpec {
    std::string name;  // no leading '-'
    std::string summary;
    std::vector<PositionalSpec> positionals;
};

struct ParseResult {
    bool ok = false;
    bool help = false;
    std::vector<std::string> positionals;
    std::string error;
};

// Parses the tokens after a command name. The help flags (-help, --help, -h)
// are recognized anywhere and take precedence, so a malformed command can
// still show its help.
ParseResult parse_arguments(const CommandSpec& spec, std::span<const std::string> args);

using Handler = std::function<ExitCode(const std::vector<std::string>& positionals,
                                       std::ostream& out, std::ostream& err)>;

std::string command_help(const CommandSpec& spec);

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

    // Returns false and fills `error` when the name is taken or the schema
    // cannot be parsed as written.
    bool add_command(CommandSpec spec, Handler handler, std::string& error);

    // Runs the global flags or the named command. Command handlers run inside a
    // catch-all so an unexpected exception becomes ExitCode::Internal rather
    // than an unhandled termination.
    ExitCode run(std::span<const std::string> args, std::ostream& out, std::ostream& err) const;

    std::string global_help() const;

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
