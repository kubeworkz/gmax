#include "core/atpg/logic5.h"

#include <span>

namespace gmax::atpg {
namespace {

// A five-value is a pair of ordinary three-state values, one for each circuit.
// Encoding a pair back into Logic5 is deliberately conservative: a mix of a
// known side and an unknown side cannot be represented, so it becomes X.
struct Bit {
    bool known = false;
    bool value = false;
};

Bit good_bit(const Logic5 value) {
    switch (value) {
    case Logic5::Zero:
        return {true, false};
    case Logic5::One:
        return {true, true};
    case Logic5::D:
        return {true, true};
    case Logic5::DBar:
        return {true, false};
    case Logic5::X:
        break;
    }
    return {};
}

Bit faulty_bit(const Logic5 value) {
    switch (value) {
    case Logic5::Zero:
        return {true, false};
    case Logic5::One:
        return {true, true};
    case Logic5::D:
        return {true, false};
    case Logic5::DBar:
        return {true, true};
    case Logic5::X:
        break;
    }
    return {};
}

Bit invert_bit(const Bit input) {
    if (!input.known) {
        return {};
    }
    return {true, !input.value};
}

Bit and_bit(const Bit left, const Bit right) {
    if ((left.known && !left.value) || (right.known && !right.value)) {
        return {true, false};
    }
    if (left.known && right.known) {
        return {true, true};
    }
    return {};
}

Bit or_bit(const Bit left, const Bit right) {
    if ((left.known && left.value) || (right.known && right.value)) {
        return {true, true};
    }
    if (left.known && right.known) {
        return {true, false};
    }
    return {};
}

Bit xor_bit(const Bit left, const Bit right) {
    if (!left.known || !right.known) {
        return {};
    }
    return {true, left.value != right.value};
}

Bit mux_bit(const Bit then_value, const Bit else_value, const Bit select) {
    if (!select.known) {
        return {};
    }
    return select.value ? then_value : else_value;
}

Logic5 encode(const Bit good, const Bit faulty) {
    if (!good.known || !faulty.known) {
        return Logic5::X;
    }
    if (good.value == faulty.value) {
        return good.value ? Logic5::One : Logic5::Zero;
    }
    return good.value ? Logic5::D : Logic5::DBar;
}

}  // namespace

int logic5_good(const Logic5 value) {
    switch (value) {
    case Logic5::Zero:
    case Logic5::DBar:
        return 0;
    case Logic5::One:
    case Logic5::D:
        return 1;
    case Logic5::X:
        break;
    }
    return -1;
}

Logic5 logic5_invert(const Logic5 value) {
    switch (value) {
    case Logic5::Zero:
        return Logic5::One;
    case Logic5::One:
        return Logic5::Zero;
    case Logic5::X:
        return Logic5::X;
    case Logic5::D:
        return Logic5::DBar;
    case Logic5::DBar:
        return Logic5::D;
    }
    return Logic5::X;
}

std::string_view logic5_name(const Logic5 value) {
    switch (value) {
    case Logic5::Zero:
        return "0";
    case Logic5::One:
        return "1";
    case Logic5::X:
        return "X";
    case Logic5::D:
        return "D";
    case Logic5::DBar:
        return "Dbar";
    }
    return "?";
}

Logic5 eval_gate(const db::GateType type, const std::span<const Logic5> fanins) {
    switch (type) {
    case db::GateType::And2:
        if (fanins.size() == 2) {
            return encode(and_bit(good_bit(fanins[0]), good_bit(fanins[1])),
                          and_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1])));
        }
        return Logic5::X;
    case db::GateType::Or2:
        if (fanins.size() == 2) {
            return encode(or_bit(good_bit(fanins[0]), good_bit(fanins[1])),
                          or_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1])));
        }
        return Logic5::X;
    case db::GateType::Nand2:
        if (fanins.size() == 2) {
            return encode(invert_bit(and_bit(good_bit(fanins[0]), good_bit(fanins[1]))),
                          invert_bit(and_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1]))));
        }
        return Logic5::X;
    case db::GateType::Nor2:
        if (fanins.size() == 2) {
            return encode(invert_bit(or_bit(good_bit(fanins[0]), good_bit(fanins[1]))),
                          invert_bit(or_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1]))));
        }
        return Logic5::X;
    case db::GateType::Xor2:
        if (fanins.size() == 2) {
            return encode(xor_bit(good_bit(fanins[0]), good_bit(fanins[1])),
                          xor_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1])));
        }
        return Logic5::X;
    case db::GateType::Xnor2:
        if (fanins.size() == 2) {
            return encode(invert_bit(xor_bit(good_bit(fanins[0]), good_bit(fanins[1]))),
                          invert_bit(xor_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1]))));
        }
        return Logic5::X;
    case db::GateType::Not:
        return fanins.size() == 1 ? logic5_invert(fanins[0]) : Logic5::X;
    case db::GateType::Buf:
    case db::GateType::Po:
        return fanins.size() == 1 ? fanins[0] : Logic5::X;
    case db::GateType::Mux2:
        if (fanins.size() == 3) {
            return encode(
                mux_bit(good_bit(fanins[0]), good_bit(fanins[1]), good_bit(fanins[2])),
                mux_bit(faulty_bit(fanins[0]), faulty_bit(fanins[1]), faulty_bit(fanins[2])));
        }
        return Logic5::X;
    case db::GateType::Tie0:
        return fanins.empty() ? Logic5::Zero : Logic5::X;
    case db::GateType::Tie1:
        return fanins.empty() ? Logic5::One : Logic5::X;
    case db::GateType::Dff:
    case db::GateType::Dlat:
    case db::GateType::Pi:
    case db::GateType::Blackbox:
    case db::GateType::ScanCell:
        return Logic5::X;
    }
    return Logic5::X;
}

}  // namespace gmax::atpg
