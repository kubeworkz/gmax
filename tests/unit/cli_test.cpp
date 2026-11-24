// WP-010: the command schema, its generated help, and the architecture 19.3
// exit codes. Framework-free for the same reason as the other tests:
// third_party stays empty until TBD-11 closes.

#include <cstddef>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shell/cli.h"

namespace shell = gmax::shell;

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

bool contains(const std::string_view text, const std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

shell::Handler success_handler() {
    return [](const shell::Invocation&, std::ostream&, std::ostream&) {
        return shell::ExitCode::Success;
    };
}

shell::CommandSpec command(std::string name, std::string summary) {
    shell::CommandSpec spec;
    spec.name = std::move(name);
    spec.summary = std::move(summary);
    return spec;
}

void expect_invalid(const shell::CommandSpec& spec, const std::string_view needle,
                    const std::string_view what) {
    shell::Cli cli("0.0.0");
    std::string error;
    const bool added = cli.add_command(spec, success_handler(), error);
    if (added || !contains(error, needle)) {
        std::cerr << "FAIL " << what << ": expected rejection containing '" << needle
                  << "', got added=" << added << " error='" << error << "'" << std::endl;
        ++failures;
    }
}

void add_command(shell::Cli& cli, shell::CommandSpec spec, shell::Handler handler) {
    std::string error;
    const bool added = cli.add_command(std::move(spec), std::move(handler), error);
    expect(added, "test command registers");
    if (!added) {
        std::cerr << "  " << error << std::endl;
    }
}

struct RunResult {
    shell::ExitCode code = shell::ExitCode::Success;
    std::string out;
    std::string err;
};

RunResult run(const shell::Cli& cli, const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const shell::ExitCode code = cli.run(std::span<const std::string>(args), out, err);
    return {code, out.str(), err.str()};
}

void test_exit_codes() {
    expect(static_cast<int>(shell::ExitCode::Success) == 0, "success is exit code 0");
    expect(static_cast<int>(shell::ExitCode::Usage) == 1, "usage is exit code 1");
    expect(static_cast<int>(shell::ExitCode::Design) == 2, "design is exit code 2");
    expect(static_cast<int>(shell::ExitCode::Drc) == 3, "drc is exit code 3");
    expect(static_cast<int>(shell::ExitCode::AtpgIncomplete) == 4,
           "atpg-incomplete is exit code 4");
    expect(static_cast<int>(shell::ExitCode::Internal) == 5, "internal is exit code 5");
}

void test_schema_validation() {
    {
        shell::Cli cli("0.0.0");
        std::string error;
        expect(cli.add_command(command("demo", "demo command"), success_handler(), error),
               "a valid command registers");
        expect(error.empty(), "a valid command leaves no error");
    }
    expect_invalid(command("help", "duplicate"), "already registered",
                   "a built-in command name is reserved");
    expect_invalid(command("", "empty"), "empty", "an empty command name is rejected");
    expect_invalid(command("-bad", "leading dash"), "start with",
                   "a command name with a leading dash is rejected");
    {
        shell::CommandSpec spec = command("optional-then-required", "bad");
        spec.positionals = {{"optional", "optional", false}, {"required", "required", true}};
        expect_invalid(spec, "after an optional one",
                       "a required positional after an optional one is rejected");
    }
    {
        shell::Cli cli("0.0.0");
        std::string error;
        expect(!cli.add_command(command("no-handler", "bad"), shell::Handler{}, error),
               "a command without a handler is rejected");
        expect(contains(error, "handler"), "a missing handler is named");
    }
    {
        shell::CommandSpec spec = command("bad-option", "bad");
        spec.options = {{"", "N", "empty name"}};
        expect_invalid(spec, "invalid option name", "an empty option name is rejected");
    }
    {
        shell::CommandSpec spec = command("bad-option-value", "bad");
        spec.options = {{"level", "", "no value name"}};
        expect_invalid(spec, "no value name", "an option without a value name is rejected");
    }
    {
        shell::CommandSpec spec = command("duplicate-option", "bad");
        spec.options = {{"level", "N", "one"}, {"level", "M", "two"}};
        expect_invalid(spec, "twice", "a duplicate option is rejected");
    }
}

shell::CommandSpec parser_spec() {
    shell::CommandSpec spec = command("demo", "demo command");
    spec.positionals = {{"input", "input file", true}, {"output", "output file", false}};
    return spec;
}

shell::CommandSpec option_parser_spec() {
    shell::CommandSpec spec = parser_spec();
    spec.options = {{"level", "N", "level to use"}};
    return spec;
}

void test_parsing() {
    const shell::CommandSpec spec = parser_spec();

    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"in.v"});
        expect(parsed.ok && !parsed.help, "a required positional parses");
        expect(parsed.positionals == std::vector<std::string>{"in.v"}, "the positional is kept");
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"in.v", "out.v"});
        expect(parsed.ok, "an optional positional may be supplied");
        expect(parsed.positionals.size() == 2, "both positionals are kept");
    }
    {
        const shell::ParseResult parsed = shell::parse_arguments(spec, {});
        expect(!parsed.ok && contains(parsed.error, "missing argument"),
               "a missing required positional is rejected");
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"in.v", "out.v", "extra"});
        expect(!parsed.ok && contains(parsed.error, "unexpected argument"),
               "an extra positional is rejected");
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"-top", "chip"});
        expect(!parsed.ok && contains(parsed.error, "unknown option"),
               "an option is rejected until the schema has one");
    }
    {
        const shell::CommandSpec with_option = option_parser_spec();
        const shell::ParseResult parsed =
            shell::parse_arguments(with_option, std::vector<std::string>{"in.v", "--level", "3"});
        expect(parsed.ok, "an option after a positional parses");
        expect(parsed.positionals == std::vector<std::string>{"in.v"},
               "the positional survives an option");
        expect(parsed.options.size() == 1 && parsed.options[0].name == "level" &&
                   parsed.options[0].value == "3",
               "the option name and value are kept");
    }
    {
        const shell::CommandSpec with_option = option_parser_spec();
        const shell::ParseResult parsed = shell::parse_arguments(
            with_option, std::vector<std::string>{"--level", "3", "in.v", "out.v"});
        expect(parsed.ok && parsed.positionals.size() == 2,
               "an option between positionals keeps both");
    }
    {
        const shell::CommandSpec with_option = option_parser_spec();
        const shell::ParseResult parsed = shell::parse_arguments(
            with_option, std::vector<std::string>{"--level", "1", "--level", "2", "in.v"});
        expect(!parsed.ok && contains(parsed.error, "more than once"),
               "a repeated option is rejected");
    }
    {
        const shell::CommandSpec with_option = option_parser_spec();
        const shell::ParseResult parsed =
            shell::parse_arguments(with_option, std::vector<std::string>{"in.v", "--level"});
        expect(!parsed.ok && contains(parsed.error, "requires a value"),
               "an option without a value is rejected");
    }
    {
        const shell::CommandSpec with_option = option_parser_spec();
        const shell::ParseResult parsed =
            shell::parse_arguments(with_option, std::vector<std::string>{"--other", "1", "in.v"});
        expect(!parsed.ok && contains(parsed.error, "unknown option"),
               "an option outside the schema is still rejected");
    }
    {
        for (const std::string help : {"-help", "--help", "-h"}) {
            const shell::ParseResult parsed =
                shell::parse_arguments(spec, std::vector<std::string>{help});
            expect(parsed.ok && parsed.help, "every help spelling short-circuits parsing");
        }
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"in.v", "-help"});
        expect(parsed.ok && parsed.help, "help wins even after a positional");
    }
    {
        const shell::CommandSpec empty = command("empty", "empty command");
        const shell::ParseResult parsed = shell::parse_arguments(empty, {});
        expect(parsed.ok && parsed.positionals.empty(), "a command with no arguments parses");
    }
}

void test_generated_help() {
    shell::Cli cli("2.3.4");
    shell::CommandSpec read = command("read", "read a netlist");
    read.positionals = {{"file", "netlist file", true}};
    add_command(cli, read, success_handler());

    const std::string global = cli.global_help();
    expect(contains(global, "usage: gmax <command>"), "global help has a usage line");
    expect(contains(global, "read <file>"), "global help lists the command and its argument");
    expect(contains(global, "read a netlist"), "global help lists the command summary");
    expect(contains(global, "-help"), "global help lists -help");
    expect(contains(global, "--version"), "global help lists --version");
    expect(!contains(global, "[options]"), "global help does not advertise options");

    const std::string help = shell::command_help(read);
    expect(contains(help, "usage: gmax read <file>"), "command help generates its usage line");
    expect(contains(help, "read a netlist"), "command help carries the summary");
    expect(contains(help, "Arguments:"), "command help has an arguments section");
    expect(contains(help, "<file>"), "command help names its positional");
    expect(contains(help, "netlist file"), "command help carries positional help");
    expect(contains(help, "-help"), "command help lists the built-in help flag");

    shell::CommandSpec extended = read;
    extended.positionals.push_back({"format", "output format", false});
    const std::string extended_help = shell::command_help(extended);
    expect(contains(extended_help, "[<format>]"), "help is rendered from the supplied schema");
    expect(!contains(help, "<format>"), "help does not invent arguments");

    const std::string option_help = shell::command_help(option_parser_spec());
    expect(contains(option_help, "[--level <N>]"), "help renders an option in the usage line");
    expect(contains(option_help, "--level <N>"), "help lists the option and its value");
    expect(contains(option_help, "level to use"), "help carries the option help");
}

void test_run() {
    shell::Cli cli("0.1.0");

    shell::CommandSpec echo = command("echo", "print its argument");
    echo.positionals = {{"text", "text to print", true}};
    add_command(cli, echo,
                [](const shell::Invocation& invocation, std::ostream& out, std::ostream&) {
                    out << invocation.positionals.front() << '\n';
                    return shell::ExitCode::Success;
                });

    for (const auto& [name, code] : std::vector<std::pair<std::string, shell::ExitCode>>{
             {"exit-design", shell::ExitCode::Design},
             {"exit-drc", shell::ExitCode::Drc},
             {"exit-atpg", shell::ExitCode::AtpgIncomplete},
             {"exit-internal", shell::ExitCode::Internal},
         }) {
        add_command(
            cli, command(name, "return a fixed exit code"),
            [code](const shell::Invocation&, std::ostream&, std::ostream&) { return code; });
    }

    shell::CommandSpec boom = command("boom", "throw from a handler");
    add_command(cli, boom,
                [](const shell::Invocation&, std::ostream&, std::ostream&) -> shell::ExitCode {
                    throw std::runtime_error("boom");
                });

    shell::CommandSpec scaled = command("scaled", "print its option");
    scaled.options = {{"factor", "N", "factor to print"}};
    add_command(cli, scaled,
                [](const shell::Invocation& invocation, std::ostream& out, std::ostream&) {
                    out << invocation.options.front().value << '\n';
                    return shell::ExitCode::Success;
                });

    {
        const RunResult result = run(cli, {"--version"});
        expect(result.code == shell::ExitCode::Success, "--version succeeds");
        expect(result.out == "gmax 0.1.0\n", "--version prints the tool and version");
        expect(result.err.empty(), "--version writes nothing to stderr");
    }
    {
        const RunResult result = run(cli, {"--version", "extra"});
        expect(result.code == shell::ExitCode::Usage, "--version rejects extra arguments");
        expect(contains(result.err, "--version takes no arguments"),
               "--version names its usage error");
    }
    for (const std::string flag : {"-help", "--help", "-h"}) {
        const RunResult result = run(cli, {flag});
        expect(result.code == shell::ExitCode::Success, "a global help flag succeeds");
        expect(contains(result.out, "usage: gmax <command>"), "a global help flag prints help");
    }
    {
        const RunResult result = run(cli, {});
        expect(result.code == shell::ExitCode::Usage, "no arguments is a usage error");
        expect(contains(result.err, "usage: gmax <command>"), "no arguments prints help to stderr");
    }
    {
        const RunResult result = run(cli, {"no-such-command"});
        expect(result.code == shell::ExitCode::Usage, "an unknown command is a usage error");
        expect(contains(result.err, "unknown command"), "an unknown command is named");
    }
    {
        const RunResult result = run(cli, {"-bogus"});
        expect(result.code == shell::ExitCode::Usage, "an unknown global option is a usage error");
        expect(contains(result.err, "unknown option"), "an unknown global option is named");
    }
    {
        const RunResult result = run(cli, {"help"});
        expect(result.code == shell::ExitCode::Success, "help succeeds");
        expect(contains(result.out, "Commands:"), "help prints the command list");
    }
    {
        const RunResult result = run(cli, {"help", "echo"});
        expect(result.code == shell::ExitCode::Success, "help <command> succeeds");
        expect(contains(result.out, "usage: gmax echo <text>"), "help <command> prints its help");
    }
    {
        const RunResult result = run(cli, {"help", "nope"});
        expect(result.code == shell::ExitCode::Usage,
               "help for an unknown command is a usage error");
        expect(contains(result.err, "unknown command"), "help names the unknown command");
    }
    {
        const RunResult result = run(cli, {"help", "echo", "extra"});
        expect(result.code == shell::ExitCode::Usage, "help rejects extra arguments");
    }
    {
        const RunResult result = run(cli, {"echo", "-help"});
        expect(result.code == shell::ExitCode::Success, "command -help succeeds");
        expect(contains(result.out, "usage: gmax echo <text>"), "command -help prints its help");
    }
    {
        const RunResult result = run(cli, {"echo"});
        expect(result.code == shell::ExitCode::Usage, "a missing positional is a usage error");
        expect(contains(result.err, "missing argument"), "a missing positional is named");
    }
    {
        const RunResult result = run(cli, {"echo", "-bogus", "hi"});
        expect(result.code == shell::ExitCode::Usage, "an unknown command option is a usage error");
        expect(contains(result.err, "try 'gmax echo -help'"), "a parse error points at help");
    }
    {
        const RunResult result = run(cli, {"echo", "hi"});
        expect(result.code == shell::ExitCode::Success, "a command handler succeeds");
        expect(result.out == "hi\n", "a command handler writes to stdout");
    }
    {
        const RunResult result = run(cli, {"scaled", "--factor", "7"});
        expect(result.code == shell::ExitCode::Success, "an option reaches the handler");
        expect(result.out == "7\n", "the handler reads the option value");
    }
    {
        const RunResult design = run(cli, {"exit-design"});
        expect(design.code == shell::ExitCode::Design, "a handler can return design");
        const RunResult drc = run(cli, {"exit-drc"});
        expect(drc.code == shell::ExitCode::Drc, "a handler can return drc");
        const RunResult atpg = run(cli, {"exit-atpg"});
        expect(atpg.code == shell::ExitCode::AtpgIncomplete,
               "a handler can return atpg-incomplete");
        const RunResult internal = run(cli, {"exit-internal"});
        expect(internal.code == shell::ExitCode::Internal, "a handler can return internal");
    }
    {
        const RunResult result = run(cli, {"boom"});
        expect(result.code == shell::ExitCode::Internal, "an exception becomes internal");
        expect(contains(result.err, "internal error"), "an exception is reported as internal");
        expect(contains(result.err, "boom"), "the exception message is preserved");
    }
}

}  // namespace

int main() {
    test_exit_codes();
    test_schema_validation();
    test_parsing();
    test_generated_help();
    test_run();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "cli: all tests passed" << std::endl;
    return 0;
}
