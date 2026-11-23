#include "shell/cli.h"

#include <algorithm>
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

bool is_reserved_option(const std::string_view name) { return is_help(name) || name == kVersion; }

bool has_space(const std::string_view text) {
    return text.find_first_of(" \t\r\n") != std::string_view::npos;
}

std::string positional_signature(const PositionalSpec& positional) {
    return positional.required ? "<" + positional.name + ">" : "[<" + positional.name + ">]";
}

std::string positional_usage(const CommandSpec& spec) {
    std::string out;
    for (const PositionalSpec& positional : spec.positionals) {
        out += ' ';
        out += positional_signature(positional);
    }
    return out;
}

std::string option_signature(const OptionSpec& option) {
    std::string out = option.name;
    if (!option.value_name.empty()) {
        out += " <" + option.value_name + ">";
    }
    return out;
}

std::string option_label(const OptionSpec& option) {
    std::string out = option_signature(option);
    if (option.required) {
        out += " (required)";
    }
    return out;
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
    if (!spec.options.empty()) {
        usage += " [options]";
    }
    usage += positional_usage(spec);
    return usage;
}

}  // namespace

std::string_view exit_code_name(const ExitCode code) {
    switch (code) {
    case ExitCode::Success:
        return "success";
    case ExitCode::Usage:
        return "usage";
    case ExitCode::Design:
        return "design";
    case ExitCode::Drc:
        return "drc";
    case ExitCode::AtpgIncomplete:
        return "atpg-incomplete";
    case ExitCode::Internal:
        return "internal";
    }
    return "unknown";
}

bool ParsedArgs::has(const std::string_view name) const {
    return options.find(name) != options.end();
}

const std::string* ParsedArgs::value(const std::string_view name) const {
    const auto it = options.find(name);
    if (it == options.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second.front();
}

std::span<const std::string> ParsedArgs::values(const std::string_view name) const {
    const auto it = options.find(name);
    if (it == options.end()) {
        return {};
    }
    return it->second;
}

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

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.size() > 1 && arg.front() == '-') {
            const auto option =
                std::find_if(spec.options.begin(), spec.options.end(),
                             [&arg](const OptionSpec& candidate) { return candidate.name == arg; });
            if (option == spec.options.end()) {
                result.error = "unknown option '" + arg + "' for command '" + spec.name + "'";
                return result;
            }
            if (result.args.has(option->name) && !option->repeatable) {
                result.error = "option '" + option->name + "' may be given only once";
                return result;
            }
            if (!option->value_name.empty()) {
                if (i + 1 >= args.size()) {
                    result.error =
                        "option '" + option->name + "' needs a value <" + option->value_name + ">";
                    return result;
                }
                result.args.options[option->name].push_back(args[++i]);
            } else {
                result.args.options.try_emplace(option->name);
            }
        } else {
            result.args.positionals.push_back(arg);
        }
    }

    for (const OptionSpec& option : spec.options) {
        if (option.required && !result.args.has(option.name)) {
            result.error = "missing required option '" + option.name + "'";
            if (!option.value_name.empty()) {
                result.error += " <" + option.value_name + ">";
            }
            return result;
        }
    }

    // add_command() requires required positionals to precede optional ones, so
    // the first N positionals are exactly the required set.
    std::size_t required_positionals = 0;
    for (const PositionalSpec& positional : spec.positionals) {
        if (positional.required) {
            ++required_positionals;
        }
    }
    const std::size_t given_positionals = result.args.positionals.size();
    if (given_positionals < required_positionals) {
        const std::string name = given_positionals < spec.positionals.size()
                                     ? spec.positionals[given_positionals].name
                                     : std::string("argument");
        result.error = "missing argument '<" + name + ">'";
        return result;
    }
    if (given_positionals > spec.positionals.size()) {
        result.error =
            "unexpected argument '" + result.args.positionals[spec.positionals.size()] + "'";
        return result;
    }

    result.ok = true;
    return result;
}

Cli::Cli(std::string version) : version_(std::move(version)) {
    CommandSpec help;
    help.name = "help";
    help.summary = "print this message or the help of one command";
    help.positionals = {{"command", "command to describe", false}};

    std::string error;
    add_command(
        std::move(help),
        [this](const ParsedArgs& args, std::ostream& out, std::ostream& err) {
            if (args.positionals.empty()) {
                out << global_help();
                return ExitCode::Success;
            }
            if (args.positionals.size() > 1) {
                err << "gmax: help takes at most one command name\n";
                return ExitCode::Usage;
            }
            const Entry* entry = find(args.positionals.front());
            if (entry == nullptr) {
                err << "gmax: unknown command '" << args.positionals.front() << "'\n";
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
    if (has_space(spec.name)) {
        error = "command '" + spec.name + "' contains whitespace";
        return false;
    }
    if (spec.summary.empty()) {
        error = "command '" + spec.name + "' has no summary";
        return false;
    }
    if (find(spec.name) != nullptr) {
        error = "command '" + spec.name + "' is already registered";
        return false;
    }

    bool optional_seen = false;
    std::vector<std::string> positional_names;
    for (const PositionalSpec& positional : spec.positionals) {
        if (positional.name.empty()) {
            error = "command '" + spec.name + "' has a positional without a name";
            return false;
        }
        if (has_space(positional.name)) {
            error = "command '" + spec.name + "' has a positional name with whitespace";
            return false;
        }
        if (positional.help.empty()) {
            error = "command '" + spec.name + "' has a positional without help text";
            return false;
        }
        if (std::find(positional_names.begin(), positional_names.end(), positional.name) !=
            positional_names.end()) {
            error =
                "command '" + spec.name + "' defines positional '" + positional.name + "' twice";
            return false;
        }
        if (optional_seen && positional.required) {
            error = "command '" + spec.name + "' has required positional '" + positional.name +
                    "' after an optional one";
            return false;
        }
        optional_seen = optional_seen || !positional.required;
        positional_names.push_back(positional.name);
    }

    std::vector<std::string> option_names;
    for (const OptionSpec& option : spec.options) {
        if (option.name.size() < 2 || option.name.front() != '-') {
            error = "command '" + spec.name + "' has option '" + option.name +
                    "' without a leading '-'";
            return false;
        }
        if (has_space(option.name)) {
            error =
                "command '" + spec.name + "' has option name '" + option.name + "' with whitespace";
            return false;
        }
        if (is_reserved_option(option.name)) {
            error = "command '" + spec.name + "' reserves option '" + option.name + "'";
            return false;
        }
        if (option.help.empty()) {
            error =
                "command '" + spec.name + "' has option '" + option.name + "' without help text";
            return false;
        }
        if (std::find(option_names.begin(), option_names.end(), option.name) !=
            option_names.end()) {
            error = "command '" + spec.name + "' defines option '" + option.name + "' twice";
            return false;
        }
        option_names.push_back(option.name);
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
    std::string out = "usage: gmax <command> [options]\n\n";
    out += "DFT/ATPG platform for grx930 and G100.\n\n";
    out += "Commands:\n";

    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        rows.emplace_back(entry.spec.name + positional_usage(entry.spec), entry.spec.summary);
    }
    append_table(out, rows);

    out += "\nGlobal options:\n";
    const std::vector<std::pair<std::string, std::string>> globals = {
        {"-help", "print this message and exit"},
        {"--version", "print the tool version and exit"},
    };
    append_table(out, globals);
    out += "\nRun 'gmax <command> -help' for command options.\n";
    return out;
}

std::string Cli::command_help(const CommandSpec& spec) const {
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
    std::vector<std::pair<std::string, std::string>> rows;
    rows.reserve(spec.options.size() + 1);
    for (const OptionSpec& option : spec.options) {
        rows.emplace_back(option_label(option), option.help);
    }
    rows.emplace_back("-help", "print this help and exit");
    append_table(out, rows);
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
        return entry->handler(parsed.args, out, err);
    } catch (const std::exception& exception) {
        err << "gmax: internal error: " << exception.what() << '\n';
        return ExitCode::Internal;
    } catch (...) {
        err << "gmax: internal error: unknown exception\n";
        return ExitCode::Internal;
    }
}

}  // namespace gmax::shell
