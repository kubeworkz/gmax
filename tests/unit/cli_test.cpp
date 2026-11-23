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
    return [](const shell::ParsedArgs&, std::ostream&, std::ostream&) {
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

    expect(shell::exit_code_name(shell::ExitCode::Success) == "success",
           "success has a diagnostic name");
    expect(shell::exit_code_name(shell::ExitCode::Usage) == "usage", "usage has a diagnostic name");
    expect(shell::exit_code_name(shell::ExitCode::Design) == "design",
           "design has a diagnostic name");
    expect(shell::exit_code_name(shell::ExitCode::Drc) == "drc", "drc has a diagnostic name");
    expect(shell::exit_code_name(shell::ExitCode::AtpgIncomplete) == "atpg-incomplete",
           "atpg-incomplete has a diagnostic name");
    expect(shell::exit_code_name(shell::ExitCode::Internal) == "internal",
           "internal has a diagnostic name");
}

void test_schema_validation() {
    shell::CommandSpec valid = command("demo", "demo command");
    valid.positionals = {{"input", "input file", true}, {"output", "output file", false}};
    valid.options = {
        {"-top", "module", "top module", true, false},
        {"-lib", "library", "library file", false, true},
        {"-verbose", "", "verbose output", false, false},
    };
    {
        shell::Cli cli("0.0.0");
        std::string error;
        expect(cli.add_command(valid, success_handler(), error), "a valid command registers");
        expect(error.empty(), "a valid command leaves no error");
    }

    expect_invalid(command("help", "duplicate"), "already registered",
                   "a built-in command name is reserved");
    expect_invalid(command("", "empty"), "empty", "an empty command name is rejected");
    expect_invalid(command("-bad", "leading dash"), "start with",
                   "a command name with a leading dash is rejected");
    expect_invalid(command("bad name", "space"), "whitespace",
                   "a command name with whitespace is rejected");
    expect_invalid(command("no-summary", ""), "summary", "a command without a summary is rejected");

    {
        shell::CommandSpec spec = command("bad-positional", "bad");
        spec.positionals = {{"", "help", true}};
        expect_invalid(spec, "without a name", "a positional without a name is rejected");
    }
    {
        shell::CommandSpec spec = command("bad-positional-help", "bad");
        spec.positionals = {{"file", "", true}};
        expect_invalid(spec, "without help text", "a positional without help is rejected");
    }
    {
        shell::CommandSpec spec = command("duplicate-positional", "bad");
        spec.positionals = {{"file", "first", true}, {"file", "second", false}};
        expect_invalid(spec, "twice", "a duplicate positional is rejected");
    }
    {
        shell::CommandSpec spec = command("optional-then-required", "bad");
        spec.positionals = {{"optional", "optional", false}, {"required", "required", true}};
        expect_invalid(spec, "after an optional one",
                       "a required positional after an optional one is rejected");
    }
    {
        shell::CommandSpec spec = command("bad-option", "bad");
        spec.options = {{"top", "module", "top module", false, false}};
        expect_invalid(spec, "leading '-'", "an option without a dash is rejected");
    }
    {
        shell::CommandSpec spec = command("bad-option-name", "bad");
        spec.options = {{"-bad name", "value", "help", false, false}};
        expect_invalid(spec, "whitespace", "an option name with whitespace is rejected");
    }
    {
        shell::CommandSpec spec = command("reserved-help", "bad");
        spec.options = {{"-help", "", "help", false, false}};
        expect_invalid(spec, "reserves option", "a command may not redeclare -help");
    }
    {
        shell::CommandSpec spec = command("reserved-version", "bad");
        spec.options = {{"--version", "", "version", false, false}};
        expect_invalid(spec, "reserves option", "a command may not redeclare --version");
    }
    {
        shell::CommandSpec spec = command("bad-option-help", "bad");
        spec.options = {{"-top", "module", "", false, false}};
        expect_invalid(spec, "without help text", "an option without help is rejected");
    }
    {
        shell::CommandSpec spec = command("duplicate-option", "bad");
        spec.options = {{"-top", "module", "first", false, false},
                        {"-top", "module", "second", false, false}};
        expect_invalid(spec, "twice", "a duplicate option is rejected");
    }
    {
        shell::Cli cli("0.0.0");
        std::string error;
        expect(!cli.add_command(command("no-handler", "bad"), shell::Handler{}, error),
               "a command without a handler is rejected");
        expect(contains(error, "handler"), "a missing handler is named");
    }
}

shell::CommandSpec parser_spec() {
    shell::CommandSpec spec = command("demo", "demo command");
    spec.positionals = {{"input", "input file", true}, {"output", "output file", false}};
    spec.options = {
        {"-top", "module", "top module", true, false},
        {"-lib", "library", "library file", false, true},
        {"-verbose", "", "verbose output", false, false},
    };
    return spec;
}

void test_parsing() {
    const shell::CommandSpec spec = parser_spec();

    {
        const std::vector<std::string> args = {"-top", "chip",  "-lib",     "a.lib",
                                               "-lib", "b.lib", "-verbose", "in.v"};
        const shell::ParseResult parsed = shell::parse_arguments(spec, args);
        expect(parsed.ok && !parsed.help, "options and a positional parse");
        expect(parsed.args.positionals == std::vector<std::string>{"in.v"},
               "the positional is kept");
        expect(parsed.args.value("-top") != nullptr && *parsed.args.value("-top") == "chip",
               "a value option keeps its value");
        expect(parsed.args.values("-lib").size() == 2 && parsed.args.values("-lib")[1] == "b.lib",
               "a repeatable option keeps every value");
        expect(parsed.args.has("-verbose"), "a flag is present");
        expect(parsed.args.value("-verbose") == nullptr, "a flag has no value");
    }

    {
        const std::vector<std::string> args = {"-top", "chip", "in.v", "out.v"};
        const shell::ParseResult parsed = shell::parse_arguments(spec, args);
        expect(parsed.ok, "an optional positional may be omitted or supplied");
        expect(parsed.args.positionals.size() == 2, "both positionals are kept");
    }

    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"in.v"});
        expect(!parsed.ok && contains(parsed.error, "missing required option"),
               "a missing required option is rejected");
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"-top"});
        expect(!parsed.ok && contains(parsed.error, "needs a value"),
               "an option without its value is rejected");
    }
    {
        const shell::ParseResult parsed = shell::parse_arguments(
            spec, std::vector<std::string>{"-top", "chip", "-bogus", "in.v"});
        expect(!parsed.ok && contains(parsed.error, "unknown option"),
               "an unknown option is rejected");
    }
    {
        const shell::ParseResult parsed = shell::parse_arguments(
            spec, std::vector<std::string>{"-top", "chip", "-top", "chip2", "in.v"});
        expect(!parsed.ok && contains(parsed.error, "only once"),
               "a non-repeatable option given twice is rejected");
    }
    {
        const shell::ParseResult parsed =
            shell::parse_arguments(spec, std::vector<std::string>{"-top", "chip"});
        expect(!parsed.ok && contains(parsed.error, "missing argument"),
               "a missing required positional is rejected");
    }
    {
        const shell::ParseResult parsed = shell::parse_arguments(
            spec, std::vector<std::string>{"-top", "chip", "in.v", "out.v", "extra"});
        expect(!parsed.ok && contains(parsed.error, "unexpected argument"),
               "an extra positional is rejected");
    }
    {
        const shell::ParseResult parsed = shell::parse_arguments(
            spec, std::vector<std::string>{"-verbose", "-verbose", "-top", "chip", "in.v"});
        expect(!parsed.ok && contains(parsed.error, "only once"),
               "a non-repeatable flag given twice is rejected");
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
            shell::parse_arguments(spec, std::vector<std::string>{"-top", "-help", "in.v"});
        expect(parsed.ok && parsed.help, "help wins even where an option value is expected");
    }

    {
        shell::CommandSpec value_spec = command("value", "value command");
        value_spec.options = {{"-name", "text", "name text", false, false}};
        const shell::ParseResult parsed =
            shell::parse_arguments(value_spec, std::vector<std::string>{"-name", "-x"});
        expect(parsed.ok && parsed.args.value("-name") != nullptr &&
                   *parsed.args.value("-name") == "-x",
               "a value may begin with a dash");
    }

    {
        shell::CommandSpec empty = command("empty", "empty command");
        const shell::ParseResult parsed = shell::parse_arguments(empty, {});
        expect(parsed.ok && parsed.args.positionals.empty(), "a command with no arguments parses");
    }
}

void test_generated_help() {
    shell::Cli cli("2.3.4");
    shell::CommandSpec read = command("read", "read a netlist");
    read.positionals = {{"file", "netlist file", true}};
    read.options = {{"-top", "module", "top module", true, false}};
    add_command(cli, read, success_handler());

    const std::string global = cli.global_help();
    expect(contains(global, "usage: gmax <command> [options]"), "global help has a usage line");
    expect(contains(global, "read <file>"), "global help lists the command and its argument");
    expect(contains(global, "read a netlist"), "global help lists the command summary");
    expect(contains(global, "-help"), "global help lists -help");
    expect(contains(global, "--version"), "global help lists --version");

    const std::string help = cli.command_help(read);
    expect(contains(help, "usage: gmax read [options] <file>"),
           "command help generates its usage line");
    expect(contains(help, "read a netlist"), "command help carries the summary");
    expect(contains(help, "Arguments:"), "command help has an arguments section");
    expect(contains(help, "<file>"), "command help names its positional");
    expect(contains(help, "netlist file"), "command help carries positional help");
    expect(contains(help, "-top <module>"), "command help names the option and its value");
    expect(contains(help, "(required)"), "command help marks a required option");
    expect(contains(help, "-help"), "command help lists the built-in help flag");

    shell::CommandSpec extended = read;
    extended.options.push_back({"-extra", "", "an extra option", false, false});
    const std::string extended_help = cli.command_help(extended);
    expect(contains(extended_help, "-extra"), "help is rendered from the supplied schema");
    expect(!contains(help, "-extra"), "help does not invent options");
}

void test_run() {
    shell::Cli cli("0.1.0");

    shell::CommandSpec echo = command("echo", "print its argument");
    echo.positionals = {{"text", "text to print", true}};
    add_command(cli, echo, [](const shell::ParsedArgs& args, std::ostream& out, std::ostream&) {
        out << args.positionals.front() << '\n';
        return shell::ExitCode::Success;
    });

    shell::CommandSpec paired = command("paired", "echo an option and a positional");
    paired.positionals = {{"file", "file name", true}};
    paired.options = {{"-top", "module", "top module", true, false}};
    add_command(cli, paired, [](const shell::ParsedArgs& args, std::ostream& out, std::ostream&) {
        out << *args.value("-top") << ' ' << args.positionals.front() << '\n';
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
            [code](const shell::ParsedArgs&, std::ostream&, std::ostream&) { return code; });
    }

    shell::CommandSpec boom = command("boom", "throw from a handler");
    add_command(cli, boom,
                [](const shell::ParsedArgs&, std::ostream&, std::ostream&) -> shell::ExitCode {
                    throw std::runtime_error("boom");
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
        const RunResult result = run(cli, {"paired", "-top", "chip", "in.v"});
        expect(result.code == shell::ExitCode::Success, "a command with an option succeeds");
        expect(result.out == "chip in.v\n", "the handler sees its option and positional");
    }
    {
        const RunResult result = run(cli, {"paired", "in.v"});
        expect(result.code == shell::ExitCode::Usage, "a missing required option is a usage error");
        expect(contains(result.err, "missing required option"), "the missing option is named");
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
