#include "shell/cli.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <ostream>
#include <utility>
#include <vector>

namespace gmax::shell {
namespace {

constexpr std::string_view kHelp = "-help";
constexpr std::string_view kLongHelp = "--help";
constexpr std::string_view kShortHelp = "-h";
constexpr std::string_view kVersion = "--version";

bool is_help(const std::string_view arg) {
    return arg == kHelp || arg == kLongHelp || arg == kShortHelp;
}

std::string positional_signature(const PositionalSpec& positional) {
    return positional.required ? "<" + positional.name + ">" : "[<" + positional.name + ">]";
}

void append_table(std::string& out, const std::vector<std::pair<std::string, std::string>>& rows) {
    std::size_t width = 0;
    for (const auto& row : rows) {
        width = std::max(width, row.first.size());
    }
    for (const auto& row : rows) {
        out += "  ";
        out += row.first;
        out.append(width - row.first.size() + 2, ' ');
        out += row.second;
        out += '\n';
    }
}

std::string command_usage(const CommandSpec& spec) {
    std::string usage = "usage: gmax " + spec.name;
    for (const PositionalSpec& positional : spec.positionals) {
        usage += ' ';
        usage += positional_signature(positional);
    }
    return usage;
}

}  // namespace

ParseResult parse_arguments(const CommandSpec& spec, const std::span<const std::string> args) {
    ParseResult result;

    // Help wins over malformed arguments: the parser and the help renderer use
    // the same schema, so a user can always ask what a command expects.
    for (const std::string& arg : args) {
        if (is_help(arg)) {
            result.ok = true;
            result.help = true;
            return result;
        }
    }

    for (const std::string& arg : args) {
        if (arg.size() > 1 && arg.front() == '-') {
            result.error = "unknown option '" + arg + "' for command '" + spec.name + "'";
            return result;
        }
        result.positionals.push_back(arg);
    }

    // add_command() requires required positionals to precede optional ones, so
    // the first N positionals are exactly the required set.
    std::size_t required_positionals = 0;
    for (const PositionalSpec& positional : spec.positionals) {
        if (positional.required) {
            ++required_positionals;
        }
    }
    const std::size_t given_positionals = result.positionals.size();
    if (given_positionals < required_positionals) {
        const std::string name = given_positionals < spec.positionals.size()
                                     ? spec.positionals[given_positionals].name
                                     : std::string("argument");
        result.error = "missing argument '<" + name + ">'";
        return result;
    }
    if (given_positionals > spec.positionals.size()) {
        result.error = "unexpected argument '" + result.positionals[spec.positionals.size()] + "'";
        return result;
    }

    result.ok = true;
    return result;
}

std::string command_help(const CommandSpec& spec) {
    std::string out = command_usage(spec) + "\n\n" + spec.summary + "\n";

    if (!spec.positionals.empty()) {
        out += "\nArguments:\n";
        std::vector<std::pair<std::string, std::string>> rows;
        rows.reserve(spec.positionals.size());
        for (const PositionalSpec& positional : spec.positionals) {
            rows.emplace_back(positional_signature(positional), positional.help);
        }
        append_table(out, rows);
    }

    out += "\nOptions:\n";
    out += "  -help  print this help and exit\n";
    return out;
}

Cli::Cli(std::string version) : version_(std::move(version)) {
    CommandSpec help;
    help.name = "help";
    help.summary = "print this message or the help of one command";
    help.positionals = {{"command", "command to describe", false}};

    std::string error;
    add_command(
        std::move(help),
        [this](const std::vector<std::string>& positionals, std::ostream& out, std::ostream& err) {
            if (positionals.empty()) {
                out << global_help();
                return ExitCode::Success;
            }
            if (positionals.size() > 1) {
                err << "gmax: help takes at most one command name\n";
                return ExitCode::Usage;
            }
            const Entry* entry = find(positionals.front());
            if (entry == nullptr) {
                err << "gmax: unknown command '" << positionals.front() << "'\n";
                return ExitCode::Usage;
            }
            out << command_help(entry->spec);
            return ExitCode::Success;
        },
        error);
}

bool Cli::add_command(CommandSpec spec, Handler handler, std::string& error) {
    if (!handler) {
        error = "command '" + spec.name + "' has no handler";
        return false;
    }
    if (spec.name.empty()) {
        error = "command name is empty";
        return false;
    }
    if (spec.name.front() == '-') {
        error = "command '" + spec.name + "' must not start with '-'";
        return false;
    }
    if (find(spec.name) != nullptr) {
        error = "command '" + spec.name + "' is already registered";
        return false;
    }

    // parse_arguments() identifies a missing required positional by its index,
    // so optional arguments may not precede required ones.
    bool optional_seen = false;
    for (const PositionalSpec& positional : spec.positionals) {
        if (optional_seen && positional.required) {
            error = "command '" + spec.name + "' has required positional '" + positional.name +
                    "' after an optional one";
            return false;
        }
        optional_seen = optional_seen || !positional.required;
    }

    entries_.push_back(Entry{std::move(spec), std::move(handler)});
    return true;
}

const Cli::Entry* Cli::find(const std::string_view name) const {
    for (const Entry& entry : entries_) {
        if (entry.spec.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::string Cli::global_help() const {
    std::string out = "usage: gmax <command>\n\n";
    out += "DFT/ATPG platform for grx930 and G100.\n\n";
    out += "Commands:\n";

    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        std::string signature = entry.spec.name;
        for (const PositionalSpec& positional : entry.spec.positionals) {
            signature += ' ';
            signature += positional_signature(positional);
        }
        rows.emplace_back(std::move(signature), entry.spec.summary);
    }
    append_table(out, rows);

    out += "\nGlobal options:\n";
    const std::vector<std::pair<std::string, std::string>> globals = {
        {"-help", "print this message and exit"},
        {"--version", "print the tool version and exit"},
    };
    append_table(out, globals);
    out += "\nRun 'gmax <command> -help' for command help.\n";
    return out;
}

ExitCode Cli::run(const std::span<const std::string> args, std::ostream& out,
                  std::ostream& err) const {
    if (args.empty()) {
        err << global_help();
        return ExitCode::Usage;
    }

    const std::string_view first = args.front();
    if (first == kVersion) {
        if (args.size() != 1) {
            err << "gmax: --version takes no arguments\n";
            return ExitCode::Usage;
        }
        out << "gmax " << version_ << '\n';
        return ExitCode::Success;
    }
    if (is_help(first)) {
        if (args.size() != 1) {
            err << "gmax: " << first << " takes no arguments\n";
            return ExitCode::Usage;
        }
        out << global_help();
        return ExitCode::Success;
    }
    if (!first.empty() && first.front() == '-') {
        err << "gmax: unknown option '" << first << "'\n";
        err << "try 'gmax -help'\n";
        return ExitCode::Usage;
    }

    const Entry* entry = find(first);
    if (entry == nullptr) {
        err << "gmax: unknown command '" << first << "'\n";
        err << "try 'gmax -help'\n";
        return ExitCode::Usage;
    }

    const ParseResult parsed = parse_arguments(entry->spec, args.subspan(1));
    if (parsed.help) {
        out << command_help(entry->spec);
        return ExitCode::Success;
    }
    if (!parsed.ok) {
        err << "gmax: " << parsed.error << '\n';
        err << "try 'gmax " << entry->spec.name << " -help'\n";
        return ExitCode::Usage;
    }

    try {
        return entry->handler(parsed.positionals, out, err);
    } catch (const std::exception& exception) {
        err << "gmax: internal error: " << exception.what() << '\n';
        return ExitCode::Internal;
    } catch (...) {
        err << "gmax: internal error: unknown exception\n";
        return ExitCode::Internal;
    }
}

}  // namespace gmax::shell
