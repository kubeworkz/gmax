#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/status.h"

namespace gmax::frontend {

// A Liberty boolean expression (architecture 7.2). v0 understands the operators
// libraries use for combinational functions: ! and ~ (not), ' (postfix not),
// & and * (and), | and + (or), ^ (xor), and ?: (mux). Mux arguments are
// {then, else, select}, matching Liberty's "s ? a : b" reading. Precedence is
// not > and > xor > or > mux.
enum class ExprKind : std::uint8_t { Constant, Variable, Not, And, Or, Xor, Mux };

struct Expr {
    ExprKind kind = ExprKind::Constant;
    bool value = false;
    std::string name;
    std::vector<Expr> args;
};

// Parses one function or next_state expression. Failure is BadPayload carrying
// the offending column, because a library that cannot be read is a diagnostic
// rather than a crash.
db::Status parse_expression(std::string_view text, Expr& out);

}  // namespace gmax::frontend
