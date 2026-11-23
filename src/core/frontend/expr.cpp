#include "core/frontend/expr.h"

#include <utility>

namespace gmax::frontend {
namespace {

bool is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '[' || c == ']' || c == '.';
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    db::Status parse(Expr& out) {
        db::Status status = ternary(out);
        if (!status.ok()) {
            return status;
        }
        skip_space();
        if (pos_ != text_.size()) {
            return fail("unexpected input");
        }
        return {};
    }

  private:
    db::Status fail(std::string_view what) const {
        return db::Status::failure(db::Error::BadPayload,
                                   "expression '" + std::string(text_) + "': " + std::string(what) +
                                       " at column " + std::to_string(pos_ + 1));
    }

    void skip_space() {
        while (pos_ < text_.size() && is_space(text_[pos_])) {
            ++pos_;
        }
    }

    bool peek(char c) {
        skip_space();
        return pos_ < text_.size() && text_[pos_] == c;
    }

    bool take(char c) {
        if (!peek(c)) {
            return false;
        }
        ++pos_;
        return true;
    }

    db::Status ternary(Expr& out) {
        Expr cond;
        db::Status status = or_expr(cond);
        if (!status.ok()) {
            return status;
        }
        if (!peek('?')) {
            out = std::move(cond);
            return {};
        }
        ++pos_;
        Expr then_expr;
        status = ternary(then_expr);
        if (!status.ok()) {
            return status;
        }
        if (!take(':')) {
            return fail("expected ':'");
        }
        Expr else_expr;
        status = ternary(else_expr);
        if (!status.ok()) {
            return status;
        }
        out = Expr{};
        out.kind = ExprKind::Mux;
        out.args.push_back(std::move(then_expr));
        out.args.push_back(std::move(else_expr));
        out.args.push_back(std::move(cond));
        return {};
    }

    db::Status or_expr(Expr& out) {
        Expr left;
        db::Status status = xor_expr(left);
        if (!status.ok()) {
            return status;
        }
        while (peek('|') || peek('+')) {
            ++pos_;
            Expr right;
            status = xor_expr(right);
            if (!status.ok()) {
                return status;
            }
            Expr node;
            node.kind = ExprKind::Or;
            node.args.push_back(std::move(left));
            node.args.push_back(std::move(right));
            left = std::move(node);
        }
        out = std::move(left);
        return {};
    }

    db::Status xor_expr(Expr& out) {
        Expr left;
        db::Status status = and_expr(left);
        if (!status.ok()) {
            return status;
        }
        while (peek('^')) {
            ++pos_;
            Expr right;
            status = and_expr(right);
            if (!status.ok()) {
                return status;
            }
            Expr node;
            node.kind = ExprKind::Xor;
            node.args.push_back(std::move(left));
            node.args.push_back(std::move(right));
            left = std::move(node);
        }
        out = std::move(left);
        return {};
    }

    db::Status and_expr(Expr& out) {
        Expr left;
        db::Status status = unary(left);
        if (!status.ok()) {
            return status;
        }
        while (peek('&') || peek('*')) {
            ++pos_;
            Expr right;
            status = unary(right);
            if (!status.ok()) {
                return status;
            }
            Expr node;
            node.kind = ExprKind::And;
            node.args.push_back(std::move(left));
            node.args.push_back(std::move(right));
            left = std::move(node);
        }
        out = std::move(left);
        return {};
    }

    db::Status unary(Expr& out) {
        if (peek('!') || peek('~')) {
            ++pos_;
            Expr inner;
            const db::Status status = unary(inner);
            if (!status.ok()) {
                return status;
            }
            out = Expr{};
            out.kind = ExprKind::Not;
            out.args.push_back(std::move(inner));
            return {};
        }
        return postfix(out);
    }

    db::Status postfix(Expr& out) {
        db::Status status = primary(out);
        if (!status.ok()) {
            return status;
        }
        while (peek('\'')) {
            ++pos_;
            Expr node;
            node.kind = ExprKind::Not;
            node.args.push_back(std::move(out));
            out = std::move(node);
        }
        return {};
    }

    db::Status primary(Expr& out) {
        if (take('(')) {
            db::Status status = ternary(out);
            if (!status.ok()) {
                return status;
            }
            if (!take(')')) {
                return fail("expected ')'");
            }
            return {};
        }
        skip_space();
        if (pos_ >= text_.size()) {
            return fail("unexpected end of expression");
        }
        const char c = text_[pos_];
        if (c == '0' || c == '1') {
            out = Expr{};
            out.kind = ExprKind::Constant;
            out.value = c == '1';
            ++pos_;
            return {};
        }
        if (is_ident_start(c)) {
            const std::size_t begin = pos_;
            while (pos_ < text_.size() && is_ident_char(text_[pos_])) {
                ++pos_;
            }
            out = Expr{};
            out.kind = ExprKind::Variable;
            out.name = std::string(text_.substr(begin, pos_ - begin));
            return {};
        }
        return fail("unexpected character");
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace

db::Status parse_expression(std::string_view text, Expr& out) {
    Parser parser(text);
    return parser.parse(out);
}

}  // namespace gmax::frontend
