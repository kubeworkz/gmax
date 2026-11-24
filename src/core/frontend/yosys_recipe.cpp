#include "core/frontend/yosys.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gmax::frontend {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' ||
                                   text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                           text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string quote_script(const std::string_view value) {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (const char c : value) {
        if (c == '\\' || c == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute.lexically_normal();
}

bool write_file(const std::filesystem::path& path, const std::string& contents,
                std::string& error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "cannot open '" + path.string() + "' for writing";
        return false;
    }
    out << contents;
    if (!out) {
        error = "cannot write '" + path.string() + "'";
        return false;
    }
    return true;
}

bool read_file(const std::filesystem::path& path, std::string& contents, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path.string() + "' for reading";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        error = "cannot read '" + path.string() + "'";
        return false;
    }
    contents = buffer.str();
    return true;
}

bool make_temp_directory(std::filesystem::path& out, std::string& error) {
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        error = "cannot find the system temporary directory: " + ec.message();
        return false;
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    for (std::uint32_t i = 0; i < 100; ++i) {
        const std::filesystem::path candidate =
            root / ("gmax-yosys-" + std::to_string(stamp) + "-" + std::to_string(i));
        ec.clear();
        if (std::filesystem::create_directory(candidate, ec)) {
            out = candidate;
            return true;
        }
        if (ec && ec != std::errc::file_exists) {
            error = "cannot create '" + candidate.string() + "': " + ec.message();
            return false;
        }
    }
    error = "cannot create a unique temporary work directory";
    return false;
}

struct ProcessResult {
    bool started = false;
    int exit_code = -1;
    std::string error;
};

#if defined(_WIN32)

std::string quote_windows(const std::string& value) {
    if (value.find_first_of(" \t\"") == std::string::npos) {
        return value;
    }
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (const char c : value) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
        } else {
            quoted.append(backslashes, '\\');
            backslashes = 0;
            quoted.push_back(c);
        }
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

ProcessResult run_process(const std::string& executable, const std::filesystem::path& script,
                          const std::filesystem::path& work_dir,
                          const std::filesystem::path& log_path) {
    ProcessResult result;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    const HANDLE log =
        CreateFileA(log_path.string().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        result.error = "cannot open the Yosys log";
        return result;
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const std::string command = quote_windows(executable) + " -s " + quote_windows(script.string());
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    const std::string working_directory = work_dir.string();
    if (!CreateProcessA(nullptr, mutable_command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, working_directory.c_str(), &startup, &process)) {
        result.error = "cannot start '" + executable + "'";
        CloseHandle(log);
        return result;
    }

    const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
    if (wait != WAIT_OBJECT_0) {
        result.error = "waiting for Yosys failed";
    } else {
        DWORD exit_code = 0;
        if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
            result.error = "cannot read the Yosys exit code";
        } else {
            result.exit_code = static_cast<int>(exit_code);
            result.started = true;
        }
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(log);
    return result;
}

#else

ProcessResult run_process(const std::string& executable, const std::filesystem::path& script,
                          const std::filesystem::path& work_dir,
                          const std::filesystem::path& log_path) {
    (void)work_dir;
    ProcessResult result;
    const int log_fd = ::open(log_path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (log_fd < 0) {
        result.error = "cannot open the Yosys log: " + std::string(std::strerror(errno));
        return result;
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        result.error = "cannot initialize the Yosys spawn";
        ::close(log_fd);
        return result;
    }
    posix_spawn_file_actions_adddup2(&actions, log_fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, log_fd, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, log_fd);

    std::string script_argument = script.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    argv.push_back(const_cast<char*>("-s"));
    argv.push_back(script_argument.data());
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int spawn =
        posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(log_fd);
    if (spawn != 0) {
        result.error = "cannot start '" + executable + "': " + std::string(std::strerror(spawn));
        return result;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            result.error = "waiting for Yosys failed: " + std::string(std::strerror(errno));
            return result;
        }
    }
    result.started = true;
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

#endif

SourceLocation parse_log_source(const std::string_view prefix) {
    std::string text = trim(prefix);
    while (!text.empty() && text.back() == ':') {
        text.pop_back();
    }
    text = trim(text);
    const std::size_t last = text.rfind(':');
    if (last == std::string::npos) {
        return {};
    }
    const std::string rest = text.substr(last + 1);
    std::size_t pos = 0;
    std::uint32_t line = 0;
    bool digits = false;
    while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
        digits = true;
        line = line * 10 + static_cast<std::uint32_t>(rest[pos] - '0');
        ++pos;
    }
    if (!digits) {
        return {};
    }
    SourceLocation location;
    location.file = trim(text.substr(0, last));
    location.line = line;
    if (pos < rest.size() && rest[pos] == '.') {
        ++pos;
        std::uint32_t column = 0;
        while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
            column = column * 10 + static_cast<std::uint32_t>(rest[pos] - '0');
            ++pos;
        }
        location.column = column;
    }
    return location;
}

std::string lower_copy(const std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text) {
        lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return lower;
}

DiagnosticCode classify_log_message(const std::string_view message) {
    const std::string lower = lower_copy(message);
    const auto has = [&lower](const std::string_view needle) {
        return lower.find(needle) != std::string::npos;
    };
    if (has("is not part of the design") || has("module not found") || has("unresolved module")) {
        return DiagnosticCode::UnresolvedModule;
    }
    if (has("multiple conflicting drivers") || has("multiple drivers")) {
        return DiagnosticCode::MultipleDrivers;
    }
    if (has("undriven") || has("has no driver")) {
        return DiagnosticCode::UndrivenInput;
    }
    if (has("combinational loop") || has("logic loop")) {
        return DiagnosticCode::CombinationalLoop;
    }
    if (has("width mismatch")) {
        return DiagnosticCode::WidthMismatch;
    }
    if (has("unsupported cell") || has("not a valid cell type")) {
        return DiagnosticCode::UnmappedCell;
    }
    return DiagnosticCode::YosysError;
}

void add_log_diagnostic(std::vector<Diagnostic>& diagnostics, const DiagnosticCode code,
                        const DiagnosticSeverity severity, std::string message,
                        const SourceLocation& source) {
    for (const Diagnostic& existing : diagnostics) {
        if (existing.code == code && existing.message == message &&
            existing.source.file == source.file && existing.source.line == source.line &&
            existing.source.column == source.column) {
            return;
        }
    }
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    diagnostic.source = source;
    diagnostics.push_back(std::move(diagnostic));
}

void parse_log(const std::string_view log, std::vector<Diagnostic>& diagnostics) {
    std::size_t begin = 0;
    while (begin < log.size()) {
        const std::size_t end = log.find('\n', begin);
        const std::size_t length = end == std::string_view::npos ? log.size() - begin : end - begin;
        std::string line(log.substr(begin, length));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t error_pos = line.find("ERROR:");
        const std::size_t warning_pos = line.find("Warning:");
        if (error_pos != std::string::npos &&
            (warning_pos == std::string::npos || error_pos < warning_pos)) {
            const SourceLocation source = parse_log_source(line.substr(0, error_pos));
            add_log_diagnostic(diagnostics, classify_log_message(line.substr(error_pos + 6)),
                               DiagnosticSeverity::Error, trim(line.substr(error_pos + 6)), source);
        } else if (warning_pos != std::string::npos) {
            const SourceLocation source = parse_log_source(line.substr(0, warning_pos));
            add_log_diagnostic(diagnostics, DiagnosticCode::YosysWarning,
                               DiagnosticSeverity::Warning, trim(line.substr(warning_pos + 8)),
                               source);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
}

std::string executable_from(const YosysOptions& options) {
    if (!options.yosys.empty()) {
        return options.yosys;
    }
    const char* environment = std::getenv("GMAX_YOSYS");
    return environment != nullptr && *environment != '\0' ? environment : "yosys";
}

void remove_work_directory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

void normalize_source_path(std::string& text, const std::string& source) {
    if (source.empty()) {
        return;
    }
    const std::string label = std::filesystem::path(source).filename().generic_string();
    if (label.empty()) {
        return;
    }
    replace_all(text, source, label);
}

// Yosys names anonymous cells after their source file, so the name embeds the
// absolute checkout path. That would make the serialized netlist hash depend on
// where the tree lives. Keep only the basename: diagnostics still name the
// source, and the same netlist hashes the same in every workspace.
void normalize_design_source_paths(db::Design& design, const YosysOptions& options) {
    const auto normalize = [&](std::string& text) {
        normalize_source_path(text, options.netlist);
        for (const std::string& library : options.libraries) {
            normalize_source_path(text, library);
        }
    };

    normalize(design.top);
    for (db::Module& module : design.modules) {
        normalize(module.name);
        for (std::string& name : module.inputs) {
            normalize(name);
        }
        for (std::string& name : module.outputs) {
            normalize(name);
        }
        for (db::Gate& gate : module.gates) {
            normalize(gate.name);
            for (std::string& fanin : gate.fanins) {
                normalize(fanin);
            }
            normalize(gate.out);
            normalize(gate.enable);
            normalize(gate.async_clear);
            normalize(gate.async_preset);
        }
    }
    for (db::Instance& instance : design.instances) {
        normalize(instance.path);
        normalize(instance.module);
        for (auto& [port, net] : instance.connections) {
            (void)port;
            normalize(net);
        }
    }
}

}  // namespace

std::string yosys_recipe_script(const YosysOptions& options, const std::string_view json_path) {
    std::string script;
    if (!options.libraries.empty()) {
        script += "read_verilog -sv -lib";
        for (const std::string& library : options.libraries) {
            script += " " + quote_script(library);
        }
        script += "\n";
    }
    script += "read_verilog -sv " + quote_script(options.netlist) + "\n";
    // hierarchy takes a module name, not a path: quoting it makes the quotes
    // part of the name. Netlist and library paths are quoted above.
    script += "hierarchy -check -top " + options.top + "\n";
    script += "flatten\n";
    if (options.proc) {
        script += "proc\nopt_dff\n";
    }
    script += "opt_clean\n";
    if (options.techmap) {
        script += "techmap\n";
    }
    script += "write_json " + quote_script(std::string(json_path)) + "\n";
    return script;
}

YosysResult elaborate_with_yosys(const YosysOptions& options) {
    YosysResult result;
    if (options.top.empty()) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::InvalidTop;
        diagnostic.message = "the top module name is empty";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    if (options.netlist.empty()) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::IoError;
        diagnostic.message = "the netlist path is empty";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    std::error_code filesystem_error;
    const std::filesystem::path netlist = absolute_path(options.netlist);
    if (!std::filesystem::exists(netlist, filesystem_error)) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::IoError;
        diagnostic.message = "netlist '" + netlist.string() + "' does not exist";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    YosysOptions resolved = options;
    resolved.netlist = netlist.generic_string();
    resolved.libraries.clear();
    for (const std::string& library : options.libraries) {
        const std::filesystem::path path = absolute_path(library);
        if (!std::filesystem::exists(path, filesystem_error)) {
            Diagnostic diagnostic;
            diagnostic.code = DiagnosticCode::IoError;
            diagnostic.message = "library '" + path.string() + "' does not exist";
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }
        resolved.libraries.push_back(path.generic_string());
    }

    std::filesystem::path work = options.work_directory;
    bool ephemeral = false;
    if (work.empty()) {
        std::string error;
        if (!make_temp_directory(work, error)) {
            Diagnostic diagnostic;
            diagnostic.code = DiagnosticCode::IoError;
            diagnostic.message = std::move(error);
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }
        ephemeral = true;
    } else {
        work = absolute_path(work);
        if (!std::filesystem::create_directories(work, filesystem_error) && filesystem_error) {
            Diagnostic diagnostic;
            diagnostic.code = DiagnosticCode::IoError;
            diagnostic.message = "cannot create work directory '" + work.string() +
                                 "': " + filesystem_error.message();
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }
    }
    const bool keep = !ephemeral || options.keep_work_directory;
    const std::filesystem::path script_path = work / "design.ys";
    const std::filesystem::path json_path = work / "design.json";
    const std::filesystem::path log_path = work / "yosys.log";

    std::string error;
    if (!write_file(script_path, yosys_recipe_script(resolved, json_path.generic_string()),
                    error)) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::IoError;
        diagnostic.message = std::move(error);
        result.diagnostics.push_back(std::move(diagnostic));
        if (ephemeral && !keep) {
            remove_work_directory(work);
        }
        return result;
    }

    const std::string executable = executable_from(options);
    const ProcessResult process = run_process(executable, script_path, work, log_path);
    std::string log;
    std::string log_error;
    (void)read_file(log_path, log, log_error);
    result.log = log;
    parse_log(log, result.diagnostics);
    result.exit_code = process.exit_code;

    if (!process.started) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::YosysNotFound;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.message =
            process.error.empty() ? "cannot start '" + executable + "'" : process.error;
        result.diagnostics.push_back(std::move(diagnostic));
        if (ephemeral && !keep) {
            remove_work_directory(work);
        }
        return result;
    }
    if (process.exit_code != 0) {
        if (!has_errors(result.diagnostics)) {
            Diagnostic diagnostic;
            diagnostic.code = DiagnosticCode::YosysFailed;
            diagnostic.severity = DiagnosticSeverity::Error;
            diagnostic.message = "Yosys exited with code " + std::to_string(process.exit_code);
            result.diagnostics.push_back(std::move(diagnostic));
        }
        if (ephemeral && !keep) {
            remove_work_directory(work);
        }
        return result;
    }

    std::string json;
    if (!read_file(json_path, json, error)) {
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::IoError;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.message = std::move(error);
        result.diagnostics.push_back(std::move(diagnostic));
        if (ephemeral && !keep) {
            remove_work_directory(work);
        }
        return result;
    }

    ImportResult imported = import_yosys_json(json, options.top, {}, options.expected_cells);
    normalize_design_source_paths(imported.design, resolved);
    result.design = std::move(imported.design);
    result.counts = std::move(imported.counts);
    for (Diagnostic& diagnostic : imported.diagnostics) {
        result.diagnostics.push_back(std::move(diagnostic));
    }
    if (keep) {
        result.json_path = json_path.string();
    }
    result.ok = !has_errors(result.diagnostics);
    if (ephemeral && !keep) {
        remove_work_directory(work);
    }
    return result;
}

}  // namespace gmax::frontend
