#pragma once

#include <string>

namespace gmax::shell {

class Cli;

// Registers the application commands on a fresh CLI. Returns false and fills
// `error` if a command cannot be added, which is a programming error rather
// than a user error.
bool register_commands(Cli& cli, std::string& error);

}  // namespace gmax::shell
