#include "core/db/dfdb.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "core/db/crc32.h"

namespace gmax::db::dfdb {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'D', 'F', 'D', 'B'};
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kSectionEntryBytes = 28;
constexpr std::uint32_t kMaxSections = 16;

enum class Section : std::uint32_t {
    Strings = 1,
    Hierarchy = 2,
    Gates = 3,
    Nets = 4,
    Sources = 5,
    Meta = 6,
    Async = 7,
    Enable = 8,
    ClockEdge = 9,
    // 1.4 (ADR-012): the canonical constraint model. Optional in the sense that
    // a database without attached constraints is still written as 1.3 and has
    // no such section; a 1.4 file always carries one.
    Constraints = 10,
};

Status fail(Error error, std::string message) { return Status::failure(error, std::move(message)); }

class Writer {
  public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }

    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void u64(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
        }
    }

    void raw(const void* data, std::size_t size) {
        if (size == 0) {
            return;
        }
        const auto* first = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), first, first + size);
    }

    void text(std::string_view text) {
        u32(static_cast<std::uint32_t>(text.size()));
        raw(text.data(), text.size());
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

  private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
  public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t u8() {
        if (!ok_ || pos_ >= size_) {
            ok_ = false;
            return 0;
        }
        return data_[pos_++];
    }

    std::uint16_t u16() {
        const auto low = u8();
        const auto high = u8();
        return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(high) << 8));
    }

    std::uint32_t u32() {
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(u8()) << shift;
        }
        return value;
    }

    std::uint64_t u64() {
        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(u8()) << shift;
        }
        return value;
    }

    bool raw(void* out, std::size_t size) {
        if (!ok_ || size > size_ - pos_) {
            ok_ = false;
            return false;
        }
        std::memcpy(out, data_ + pos_, size);
        pos_ += size;
        return true;
    }

    std::string text() {
        const std::uint32_t size = u32();
        if (!ok_ || size > size_ - pos_) {
            ok_ = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(data_ + pos_), size);
        pos_ += size;
        return value;
    }

    bool ok() const { return ok_; }
    bool at_end() const { return pos_ == size_; }
    std::size_t remaining() const { return size_ - pos_; }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace

namespace {

std::vector<std::uint8_t> build_strings(const StringTable& strings) {
    Writer writer;
    writer.u32(strings.size());
    for (const std::string& entry : strings.entries()) {
        writer.text(entry);
    }
    return writer.take();
}

// Every collection here is already in canonical order (flatten sorted it), and
// instance connections are a std::map, so the section is sorted-key serialized
// rather than ordered by however the design happened to be built.
std::vector<std::uint8_t> build_hierarchy(const Design& design, StringTable& strings) {
    Writer writer;
    writer.u32(strings.intern(design.top));

    writer.u32(static_cast<std::uint32_t>(design.modules.size()));
    for (const Module& module : design.modules) {
        writer.u32(strings.intern(module.name));
        writer.u32(static_cast<std::uint32_t>(module.inputs.size()));
        for (const std::string& name : module.inputs) {
            writer.u32(strings.intern(name));
        }
        writer.u32(static_cast<std::uint32_t>(module.outputs.size()));
        for (const std::string& name : module.outputs) {
            writer.u32(strings.intern(name));
        }
        writer.u32(static_cast<std::uint32_t>(module.gates.size()));
        for (const Gate& gate : module.gates) {
            writer.u8(static_cast<std::uint8_t>(gate.type));
            writer.u32(strings.intern(gate.name));
            writer.u32(static_cast<std::uint32_t>(gate.fanins.size()));
            for (const std::string& name : gate.fanins) {
                writer.u32(strings.intern(name));
            }
            writer.u32(strings.intern(gate.out));
            writer.u32(gate.lib_cell);
        }
    }

    writer.u32(static_cast<std::uint32_t>(design.instances.size()));
    for (const Instance& instance : design.instances) {
        writer.u32(strings.intern(instance.path));
        writer.u32(strings.intern(instance.module));
        writer.u32(static_cast<std::uint32_t>(instance.connections.size()));
        for (const auto& [port, net] : instance.connections) {
            writer.u32(strings.intern(port));
            writer.u32(strings.intern(net));
        }
    }
    return writer.take();
}

std::vector<std::uint8_t> build_gates(const FlatNetlist& flat) {
    Writer writer;
    writer.u32(flat.gate_count());
    writer.u32(static_cast<std::uint32_t>(flat.fanins.size()));
    writer.u32(static_cast<std::uint32_t>(flat.fanouts.size()));
    for (const GateType type : flat.type) {
        writer.u8(static_cast<std::uint8_t>(type));
    }
    for (const std::uint32_t offset : flat.fanin_begin) {
        writer.u32(offset);
    }
    for (const NetId net : flat.fanins) {
        writer.u32(net);
    }
    for (const std::uint32_t offset : flat.fanout_begin) {
        writer.u32(offset);
    }
    for (const NetId net : flat.fanouts) {
        writer.u32(net);
    }
    for (const NetId net : flat.out_net) {
        writer.u32(net);
    }
    for (const std::uint8_t flag : flat.flags) {
        writer.u8(flag);
    }
    for (const std::uint32_t level : flat.level) {
        writer.u32(level);
    }
    for (const std::uint32_t cell : flat.lib_cell) {
        writer.u32(cell);
    }
    return writer.take();
}

std::vector<std::uint8_t> build_nets(const FlatNetlist& flat) {
    Writer writer;
    writer.u32(flat.net_count());
    for (const std::uint32_t driver : flat.driver) {
        writer.u32(driver);
    }
    for (const std::uint32_t name : flat.name_id) {
        writer.u32(name);
    }
    for (const SourceId source : flat.source_loc) {
        writer.u32(source);
    }
    return writer.take();
}

std::vector<std::uint8_t> build_sources(const FlatNetlist& flat) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(flat.source_table.size()));
    for (const FlatNetlist::SourceLocation& source : flat.source_table) {
        writer.u32(source.file);
        writer.u32(source.line);
    }
    return writer.take();
}

// A control column is one per-gate field carried by a control section: the
// design keeps the net name, the flat graph keeps the net id.
struct ControlColumn {
    std::string Gate::*design;
    std::vector<NetId> FlatNetlist::*flat;
};

const ControlColumn kAsyncColumns[] = {
    {&Gate::async_clear, &FlatNetlist::async_clear},
    {&Gate::async_preset, &FlatNetlist::async_preset},
};
const ControlColumn kEnableColumns[] = {
    {&Gate::enable, &FlatNetlist::enable},
};

// Each group of controls lives in its own optional section so every minor
// addition stays additive: a file that predates the section loads with those
// controls absent. A string or net id of kInvalidId means "no control", so no
// empty string is interned. Design gates and flat gates stay in canonical order.
std::vector<std::uint8_t> build_control_section(const Design& design, const FlatNetlist& flat,
                                                StringTable& strings,
                                                std::span<const ControlColumn> columns) {
    Writer writer;
    std::uint32_t design_gates = 0;
    for (const Module& module : design.modules) {
        design_gates += static_cast<std::uint32_t>(module.gates.size());
    }
    writer.u32(design_gates);
    for (const Module& module : design.modules) {
        for (const Gate& gate : module.gates) {
            for (const ControlColumn& column : columns) {
                const std::string& name = gate.*column.design;
                writer.u32(name.empty() ? kInvalidId : strings.intern(name));
            }
        }
    }
    writer.u32(flat.gate_count());
    for (std::size_t gate = 0; gate < flat.gate_count(); ++gate) {
        for (const ControlColumn& column : columns) {
            const std::vector<NetId>& nets = flat.*column.flat;
            writer.u32(gate < nets.size() ? nets[gate] : kInvalidId);
        }
    }
    return writer.take();
}

// The clock edge is one byte per gate: a DFF's capture edge, Rising elsewhere.
// 1.3's optional section keeps 1.0-1.2 files loadable with the default edge.
std::vector<std::uint8_t> build_clock_edge(const Design& design, const FlatNetlist& flat) {
    Writer writer;
    std::uint32_t design_gates = 0;
    for (const Module& module : design.modules) {
        design_gates += static_cast<std::uint32_t>(module.gates.size());
    }
    writer.u32(design_gates);
    for (const Module& module : design.modules) {
        for (const Gate& gate : module.gates) {
            writer.u8(static_cast<std::uint8_t>(gate.clock_edge));
        }
    }
    writer.u32(flat.gate_count());
    for (std::size_t gate = 0; gate < flat.gate_count(); ++gate) {
        writer.u8(gate < flat.clock_edge.size() ? static_cast<std::uint8_t>(flat.clock_edge[gate])
                                                : static_cast<std::uint8_t>(ClockEdge::Rising));
    }
    return writer.take();
}

// Counts are repeated here on purpose: they are cheap, and a section that parses
// but disagrees with the rest of the file is a bug worth catching at load.
std::vector<std::uint8_t> build_meta(const FlatNetlist& flat, const StringTable& strings) {
    Writer writer;
    writer.u32(flat.gate_count());
    writer.u32(flat.net_count());
    writer.u32(strings.size());
    writer.u32(static_cast<std::uint32_t>(flat.source_table.size()));
    return writer.take();
}

// Doubles are stored as their bit pattern so a constraint survives the round
// trip exactly; the canonical decimal form is a hash-input rule, not a storage
// one. Names are written as plain text rather than interned, which keeps the
// strings section byte-identical for a database that has no constraints.
std::uint64_t double_bits(const double value) { return std::bit_cast<std::uint64_t>(value); }

void write_names(Writer& writer, const std::vector<std::string>& names) {
    writer.u32(static_cast<std::uint32_t>(names.size()));
    for (const std::string& name : names) {
        writer.text(name);
    }
}

std::vector<std::uint8_t> build_constraints(const Constraints& constraints) {
    Writer writer;
    writer.u8(constraints.complete ? 1u : 0u);
    write_names(writer, constraints.unsupported);

    writer.u32(static_cast<std::uint32_t>(constraints.clocks.size()));
    for (const ConstraintClock& clock : constraints.clocks) {
        writer.text(clock.name);
        writer.u64(double_bits(clock.period));
        writer.u8(clock.has_waveform ? 1u : 0u);
        writer.u64(double_bits(clock.rise));
        writer.u64(double_bits(clock.fall));
        writer.text(clock.source);
        writer.u8(clock.generated ? 1u : 0u);
        writer.text(clock.master_clock);
        writer.text(clock.source_pin);
        writer.u32(static_cast<std::uint32_t>(clock.divide_by));
        writer.u32(static_cast<std::uint32_t>(clock.multiply_by));
    }

    writer.u32(static_cast<std::uint32_t>(constraints.clock_groups.size()));
    for (const ConstraintClockGroup& group : constraints.clock_groups) {
        writer.u8(static_cast<std::uint8_t>(group.kind));
        writer.text(group.name);
        writer.u32(static_cast<std::uint32_t>(group.groups.size()));
        for (const std::vector<std::string>& members : group.groups) {
            write_names(writer, members);
        }
    }

    writer.u32(static_cast<std::uint32_t>(constraints.false_paths.size()));
    for (const ConstraintFalsePath& path : constraints.false_paths) {
        write_names(writer, path.from);
        write_names(writer, path.through);
        write_names(writer, path.to);
        // 1.5: which of those names came from a [get_clocks] query.
        write_names(writer, path.from_clocks);
        write_names(writer, path.through_clocks);
        write_names(writer, path.to_clocks);
        writer.u32(path.line);
    }

    writer.u32(static_cast<std::uint32_t>(constraints.case_analysis.size()));
    for (const ConstraintCaseAnalysis& item : constraints.case_analysis) {
        writer.text(item.object);
        writer.u8(item.value ? 1u : 0u);
    }

    writer.u32(static_cast<std::uint32_t>(constraints.delays.size()));
    for (const ConstraintDelay& delay : constraints.delays) {
        writer.u8(static_cast<std::uint8_t>(delay.kind));
        writer.text(delay.object);
        writer.text(delay.clock);
        writer.u64(double_bits(delay.value));
        writer.u8(delay.max ? 1u : 0u);
        writer.u8(delay.min ? 1u : 0u);
    }

    writer.u32(static_cast<std::uint32_t>(constraints.driving_cells.size()));
    for (const ConstraintDrivingCell& cell : constraints.driving_cells) {
        writer.text(cell.object);
        writer.text(cell.lib_cell);
        writer.text(cell.pin);
    }

    writer.u32(static_cast<std::uint32_t>(constraints.loads.size()));
    for (const ConstraintLoad& load : constraints.loads) {
        writer.text(load.object);
        writer.u64(double_bits(load.value));
    }
    return writer.take();
}

}  // namespace

std::vector<std::uint8_t> serialize(const Design& design, const FlatNetlist& flat,
                                    const StringTable& strings, const Constraints* constraints) {
    StringTable table = strings;

    // Hierarchy first: interning its names grows the table the strings section
    // then writes.
    std::vector<std::uint8_t> hierarchy = build_hierarchy(design, table);
    std::vector<std::uint8_t> async = build_control_section(design, flat, table, kAsyncColumns);
    std::vector<std::uint8_t> enable = build_control_section(design, flat, table, kEnableColumns);
    std::vector<std::uint8_t> clock_edge = build_clock_edge(design, flat);
    std::vector<std::pair<Section, std::vector<std::uint8_t>>> sections;
    sections.emplace_back(Section::Strings, build_strings(table));
    sections.emplace_back(Section::Hierarchy, std::move(hierarchy));
    sections.emplace_back(Section::Gates, build_gates(flat));
    sections.emplace_back(Section::Nets, build_nets(flat));
    sections.emplace_back(Section::Sources, build_sources(flat));
    sections.emplace_back(Section::Meta, build_meta(flat, table));
    sections.emplace_back(Section::Async, std::move(async));
    sections.emplace_back(Section::Enable, std::move(enable));
    sections.emplace_back(Section::ClockEdge, std::move(clock_edge));
    if (constraints != nullptr) {
        sections.emplace_back(Section::Constraints, build_constraints(*constraints));
    }

    Writer writer;
    for (const std::uint8_t byte : kMagic) {
        writer.u8(byte);
    }
    writer.u16(kSchemaMajor);
    writer.u16(constraints != nullptr ? kSchemaMinor : kSchemaMinorWithoutConstraints);
    writer.u32(kEndianMarker);
    writer.u32(static_cast<std::uint32_t>(sections.size()));

    std::uint64_t offset = kHeaderBytes + sections.size() * kSectionEntryBytes;
    for (const auto& [id, payload] : sections) {
        writer.u32(static_cast<std::uint32_t>(id));
        writer.u32(0);
        writer.u64(offset);
        writer.u64(payload.size());
        writer.u32(crc32(payload.data(), payload.size()));
        offset += payload.size();
    }
    for (const auto& [id, payload] : sections) {
        writer.raw(payload.data(), payload.size());
    }
    return writer.take();
}

namespace {

Status parse_strings(Reader& reader, StringTable& strings) {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "strings section ends inside its count");
    }
    if (count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "strings section declares more entries than it can hold");
    }
    strings.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::string value = reader.text();
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "strings section ends inside an entry");
        }
        // Ids are positions in this table, so a duplicate would shift every id
        // after it and silently renumber the file.
        if (strings.intern(value) != i) {
            return fail(Error::BadPayload, "strings section holds a duplicate entry");
        }
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "strings section has trailing bytes");
    }
    return Status{};
}

Status parse_hierarchy(Reader& reader, const StringTable& strings, Design& design) {
    design = Design{};
    bool strings_ok = true;
    const auto name = [&strings, &strings_ok](const StringId id) {
        if (!strings.contains(id)) {
            strings_ok = false;
            return std::string();
        }
        return std::string(strings.at(id));
    };

    design.top = name(reader.u32());
    const std::uint32_t module_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "hierarchy section ends inside its header");
    }
    if (module_count > reader.remaining()) {
        return fail(Error::BadPayload, "hierarchy section declares more modules than it can hold");
    }

    design.modules.reserve(module_count);
    for (std::uint32_t m = 0; m < module_count; ++m) {
        Module module;
        module.name = name(reader.u32());

        const std::uint32_t input_count = reader.u32();
        if (input_count > reader.remaining()) {
            return fail(Error::BadPayload,
                        "hierarchy section declares more ports than it can hold");
        }
        module.inputs.reserve(input_count);
        for (std::uint32_t i = 0; i < input_count; ++i) {
            module.inputs.push_back(name(reader.u32()));
        }

        const std::uint32_t output_count = reader.u32();
        if (output_count > reader.remaining()) {
            return fail(Error::BadPayload,
                        "hierarchy section declares more ports than it can hold");
        }
        module.outputs.reserve(output_count);
        for (std::uint32_t i = 0; i < output_count; ++i) {
            module.outputs.push_back(name(reader.u32()));
        }

        const std::uint32_t gate_count = reader.u32();
        if (gate_count > reader.remaining()) {
            return fail(Error::BadPayload,
                        "hierarchy section declares more gates than it can hold");
        }
        module.gates.reserve(gate_count);
        for (std::uint32_t g = 0; g < gate_count; ++g) {
            Gate gate;
            const std::uint8_t raw_type = reader.u8();
            if (raw_type > static_cast<std::uint8_t>(GateType::ScanCell)) {
                return fail(Error::BadPayload, "hierarchy section holds an unknown gate type");
            }
            gate.type = static_cast<GateType>(raw_type);
            gate.name = name(reader.u32());

            const std::uint32_t fanin_count = reader.u32();
            if (fanin_count > reader.remaining()) {
                return fail(Error::BadPayload,
                            "hierarchy section declares more pins than it can hold");
            }
            gate.fanins.reserve(fanin_count);
            for (std::uint32_t i = 0; i < fanin_count; ++i) {
                gate.fanins.push_back(name(reader.u32()));
            }
            gate.out = name(reader.u32());
            gate.lib_cell = reader.u32();
            module.gates.push_back(std::move(gate));
        }
        design.modules.push_back(std::move(module));
    }

    const std::uint32_t instance_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "hierarchy section ends inside its instance count");
    }
    if (instance_count > reader.remaining()) {
        return fail(Error::BadPayload,
                    "hierarchy section declares more instances than it can hold");
    }
    design.instances.reserve(instance_count);
    for (std::uint32_t i = 0; i < instance_count; ++i) {
        Instance instance;
        instance.path = name(reader.u32());
        instance.module = name(reader.u32());

        const std::uint32_t connection_count = reader.u32();
        if (connection_count > reader.remaining()) {
            return fail(Error::BadPayload,
                        "hierarchy section declares more connections than it can hold");
        }
        for (std::uint32_t c = 0; c < connection_count; ++c) {
            const std::string port = name(reader.u32());
            const std::string net = name(reader.u32());
            instance.connections.emplace(port, net);
        }
        design.instances.push_back(std::move(instance));
    }

    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "hierarchy section ends inside a record");
    }
    if (!strings_ok) {
        return fail(Error::BadPayload,
                    "hierarchy section names a string id the table does not have");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "hierarchy section has trailing bytes");
    }
    if (find_module(design, design.top) == nullptr) {
        return fail(Error::BadPayload, "top module is not in the module list");
    }
    return Status{};
}

}  // namespace

Status parse_gates(Reader& reader, FlatNetlist& flat) {
    const std::uint32_t gate_count = reader.u32();
    const std::uint32_t fanin_count = reader.u32();
    const std::uint32_t fanout_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "gates section ends inside its counts");
    }
    // A gate costs at least 14 bytes across the arrays below, so a count larger
    // than the section could hold is a corrupt file, not an allocation to try.
    if (gate_count > reader.remaining() / 14) {
        return fail(Error::BadPayload, "gates section declares more gates than it can hold");
    }
    if (fanin_count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "gates section declares more fanins than it can hold");
    }
    if (fanout_count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "gates section declares more fanouts than it can hold");
    }

    flat.type.resize(gate_count);
    for (GateType& type : flat.type) {
        const std::uint8_t raw = reader.u8();
        if (raw > static_cast<std::uint8_t>(GateType::ScanCell)) {
            return fail(Error::BadPayload, "gates section holds an unknown gate type");
        }
        type = static_cast<GateType>(raw);
    }
    flat.fanin_begin.resize(static_cast<std::size_t>(gate_count) + 1);
    for (std::uint32_t& offset : flat.fanin_begin) {
        offset = reader.u32();
    }
    flat.fanins.resize(fanin_count);
    for (NetId& net : flat.fanins) {
        net = reader.u32();
    }
    flat.fanout_begin.resize(static_cast<std::size_t>(gate_count) + 1);
    for (std::uint32_t& offset : flat.fanout_begin) {
        offset = reader.u32();
    }
    flat.fanouts.resize(fanout_count);
    for (NetId& net : flat.fanouts) {
        net = reader.u32();
    }
    flat.out_net.resize(gate_count);
    for (NetId& net : flat.out_net) {
        net = reader.u32();
    }
    flat.flags.resize(gate_count);
    for (std::uint8_t& flag : flat.flags) {
        flag = reader.u8();
    }
    flat.level.resize(gate_count);
    for (std::uint32_t& level : flat.level) {
        level = reader.u32();
    }
    flat.lib_cell.resize(gate_count);
    for (std::uint32_t& cell : flat.lib_cell) {
        cell = reader.u32();
    }

    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "gates section ends inside its arrays");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "gates section has trailing bytes");
    }
    return Status{};
}

Status parse_nets(Reader& reader, FlatNetlist& flat) {
    const std::uint32_t net_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "nets section ends inside its count");
    }
    if (net_count > reader.remaining() / 12) {
        return fail(Error::BadPayload, "nets section declares more nets than it can hold");
    }

    flat.driver.resize(net_count);
    for (std::uint32_t& driver : flat.driver) {
        driver = reader.u32();
    }
    flat.name_id.resize(net_count);
    for (std::uint32_t& name : flat.name_id) {
        name = reader.u32();
    }
    flat.source_loc.resize(net_count);
    for (SourceId& source : flat.source_loc) {
        source = reader.u32();
    }

    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "nets section ends inside its arrays");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "nets section has trailing bytes");
    }
    return Status{};
}

Status parse_sources(Reader& reader, FlatNetlist& flat) {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "sources section ends inside its count");
    }
    if (count > reader.remaining() / 8) {
        return fail(Error::BadPayload, "sources section declares more entries than it can hold");
    }

    flat.source_table.resize(count);
    for (FlatNetlist::SourceLocation& source : flat.source_table) {
        source.file = reader.u32();
        source.line = reader.u32();
    }

    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "sources section ends inside its entries");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "sources section has trailing bytes");
    }
    return Status{};
}

// Restores a control section written by build_control_section. The caller has
// already sized the flat columns to kInvalidId, so a file that predates the
// section needs no special case.
Status parse_control_section(Reader& reader, const StringTable& strings, Design& design,
                             FlatNetlist& flat, std::string_view name,
                             std::span<const ControlColumn> columns) {
    std::size_t design_gates = 0;
    for (const Module& module : design.modules) {
        design_gates += module.gates.size();
    }
    const std::string prefix = std::string(name) + " section ";
    const std::uint32_t stored_gates = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, prefix + "ends inside its hierarchy count");
    }
    if (stored_gates != design_gates) {
        return fail(Error::BadPayload, prefix + "disagrees with the hierarchy gate count");
    }
    for (Module& module : design.modules) {
        for (Gate& gate : module.gates) {
            for (const ControlColumn& column : columns) {
                const StringId id = reader.u32();
                if (!reader.ok()) {
                    return fail(Error::TruncatedFile, prefix + "ends inside a gate record");
                }
                if (id == kInvalidId) {
                    continue;
                }
                if (!strings.contains(id)) {
                    return fail(Error::BadPayload,
                                prefix + "names a string id the table does not have");
                }
                gate.*column.design = std::string(strings.at(id));
            }
        }
    }

    const std::uint32_t flat_gates = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, prefix + "ends inside its flat gate count");
    }
    if (flat_gates != flat.gate_count()) {
        return fail(Error::BadPayload, prefix + "disagrees with the flat gate count");
    }
    for (std::size_t gate = 0; gate < flat.gate_count(); ++gate) {
        for (const ControlColumn& column : columns) {
            (flat.*column.flat)[gate] = reader.u32();
        }
    }
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, prefix + "ends inside its control columns");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, prefix + "has trailing bytes");
    }
    return Status{};
}

// Restores the clock-edge section written by build_clock_edge; absent in 1.0,
// 1.1 and 1.2 files, which default to rising edges.
Status parse_clock_edge(Reader& reader, Design& design, FlatNetlist& flat) {
    std::size_t design_gates = 0;
    for (const Module& module : design.modules) {
        design_gates += module.gates.size();
    }
    const std::uint32_t stored_gates = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "clock-edge section ends inside its hierarchy count");
    }
    if (stored_gates != design_gates) {
        return fail(Error::BadPayload,
                    "clock-edge section disagrees with the hierarchy gate count");
    }
    for (Module& module : design.modules) {
        for (Gate& gate : module.gates) {
            const std::uint8_t raw = reader.u8();
            if (!reader.ok()) {
                return fail(Error::TruncatedFile, "clock-edge section ends inside a gate record");
            }
            if (raw > static_cast<std::uint8_t>(ClockEdge::Falling)) {
                return fail(Error::BadPayload, "clock-edge section holds an unknown edge");
            }
            gate.clock_edge = static_cast<ClockEdge>(raw);
        }
    }

    const std::uint32_t flat_gates = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "clock-edge section ends inside its flat gate count");
    }
    if (flat_gates != flat.gate_count()) {
        return fail(Error::BadPayload, "clock-edge section disagrees with the flat gate count");
    }
    for (std::size_t gate = 0; gate < flat.gate_count(); ++gate) {
        const std::uint8_t raw = reader.u8();
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "clock-edge section ends inside its control array");
        }
        if (raw > static_cast<std::uint8_t>(ClockEdge::Falling)) {
            return fail(Error::BadPayload, "clock-edge section holds an unknown edge");
        }
        flat.clock_edge[gate] = static_cast<ClockEdge>(raw);
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "clock-edge section has trailing bytes");
    }
    return Status{};
}

Status parse_meta(Reader& reader, const FlatNetlist& flat, const StringTable& strings) {
    const std::uint32_t gate_count = reader.u32();
    const std::uint32_t net_count = reader.u32();
    const std::uint32_t string_count = reader.u32();
    const std::uint32_t source_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "meta section ends inside its counts");
    }
    if (!reader.at_end()) {
        return fail(Error::BadPayload, "meta section has trailing bytes");
    }
    if (gate_count != flat.gate_count() || net_count != flat.net_count() ||
        string_count != strings.size() || source_count != flat.source_table.size()) {
        return fail(Error::BadPayload, "meta section disagrees with the sections it describes");
    }
    return Status{};
}

namespace {

Status read_names(Reader& reader, std::vector<std::string>& out) {
    const std::uint32_t count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "constraints section ends inside a list count");
    }
    if (count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "a constraints list declares more names than it can hold");
    }
    out.clear();
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(reader.text());
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a name");
        }
    }
    return Status{};
}

Status parse_constraints(Reader& reader, Constraints& constraints, const std::uint16_t minor) {
    constraints = Constraints{};
    constraints.present = true;
    constraints.complete = reader.u8() != 0;
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "constraints section ends inside its status flag");
    }
    Status status = read_names(reader, constraints.unsupported);
    if (!status) {
        return status;
    }

    const std::uint32_t clock_count = reader.u32();
    if (!reader.ok() || clock_count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "constraints section declares more clocks than it can hold");
    }
    constraints.clocks.reserve(clock_count);
    for (std::uint32_t i = 0; i < clock_count; ++i) {
        ConstraintClock clock;
        clock.name = reader.text();
        clock.period = std::bit_cast<double>(reader.u64());
        clock.has_waveform = reader.u8() != 0;
        clock.rise = std::bit_cast<double>(reader.u64());
        clock.fall = std::bit_cast<double>(reader.u64());
        clock.source = reader.text();
        clock.generated = reader.u8() != 0;
        clock.master_clock = reader.text();
        clock.source_pin = reader.text();
        clock.divide_by = static_cast<int>(reader.u32());
        clock.multiply_by = static_cast<int>(reader.u32());
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a clock");
        }
        constraints.clocks.push_back(std::move(clock));
    }

    const std::uint32_t group_count = reader.u32();
    if (!reader.ok() || group_count > reader.remaining() / 4) {
        return fail(Error::BadPayload,
                    "constraints section declares more clock groups than it can hold");
    }
    constraints.clock_groups.reserve(group_count);
    for (std::uint32_t i = 0; i < group_count; ++i) {
        ConstraintClockGroup group;
        const std::uint8_t kind = reader.u8();
        if (kind > static_cast<std::uint8_t>(ClockGroupKind::PhysicallyExclusive)) {
            return fail(Error::BadPayload, "constraints section has an unknown clock-group kind");
        }
        group.kind = static_cast<ClockGroupKind>(kind);
        group.name = reader.text();
        const std::uint32_t members = reader.u32();
        if (!reader.ok() || members > reader.remaining() / 4) {
            return fail(Error::BadPayload, "a clock group declares more members than it can hold");
        }
        group.groups.resize(members);
        for (std::vector<std::string>& names : group.groups) {
            status = read_names(reader, names);
            if (!status) {
                return status;
            }
        }
        constraints.clock_groups.push_back(std::move(group));
    }

    const std::uint32_t path_count = reader.u32();
    if (!reader.ok() || path_count > reader.remaining() / 12) {
        return fail(Error::BadPayload,
                    "constraints section declares more false paths than it can hold");
    }
    constraints.false_paths.reserve(path_count);
    for (std::uint32_t i = 0; i < path_count; ++i) {
        ConstraintFalsePath path;
        status = read_names(reader, path.from);
        if (!status) {
            return status;
        }
        status = read_names(reader, path.through);
        if (!status) {
            return status;
        }
        status = read_names(reader, path.to);
        if (!status) {
            return status;
        }
        if (minor >= 5) {
            status = read_names(reader, path.from_clocks);
            if (!status) {
                return status;
            }
            status = read_names(reader, path.through_clocks);
            if (!status) {
                return status;
            }
            status = read_names(reader, path.to_clocks);
            if (!status) {
                return status;
            }
            path.line = reader.u32();
            if (!reader.ok()) {
                return fail(Error::TruncatedFile, "constraints section ends inside a false path");
            }
        }
        constraints.false_paths.push_back(std::move(path));
    }

    const std::uint32_t case_count = reader.u32();
    if (!reader.ok() || case_count > reader.remaining() / 4) {
        return fail(Error::BadPayload,
                    "constraints section declares more case values than it can hold");
    }
    constraints.case_analysis.reserve(case_count);
    for (std::uint32_t i = 0; i < case_count; ++i) {
        ConstraintCaseAnalysis item;
        item.object = reader.text();
        item.value = reader.u8() != 0;
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a case value");
        }
        constraints.case_analysis.push_back(std::move(item));
    }

    const std::uint32_t delay_count = reader.u32();
    if (!reader.ok() || delay_count > reader.remaining() / 4) {
        return fail(Error::BadPayload, "constraints section declares more delays than it can hold");
    }
    constraints.delays.reserve(delay_count);
    for (std::uint32_t i = 0; i < delay_count; ++i) {
        ConstraintDelay delay;
        const std::uint8_t kind = reader.u8();
        if (kind > static_cast<std::uint8_t>(DelayKind::Output)) {
            return fail(Error::BadPayload, "constraints section has an unknown delay kind");
        }
        delay.kind = static_cast<DelayKind>(kind);
        delay.object = reader.text();
        delay.clock = reader.text();
        delay.value = std::bit_cast<double>(reader.u64());
        delay.max = reader.u8() != 0;
        delay.min = reader.u8() != 0;
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a delay");
        }
        constraints.delays.push_back(std::move(delay));
    }

    const std::uint32_t cell_count = reader.u32();
    if (!reader.ok() || cell_count > reader.remaining() / 12) {
        return fail(Error::BadPayload,
                    "constraints section declares more driving cells than it can hold");
    }
    constraints.driving_cells.reserve(cell_count);
    for (std::uint32_t i = 0; i < cell_count; ++i) {
        ConstraintDrivingCell cell;
        cell.object = reader.text();
        cell.lib_cell = reader.text();
        cell.pin = reader.text();
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a driving cell");
        }
        constraints.driving_cells.push_back(std::move(cell));
    }

    const std::uint32_t load_count = reader.u32();
    if (!reader.ok() || load_count > reader.remaining() / 12) {
        return fail(Error::BadPayload, "constraints section declares more loads than it can hold");
    }
    constraints.loads.reserve(load_count);
    for (std::uint32_t i = 0; i < load_count; ++i) {
        ConstraintLoad load;
        load.object = reader.text();
        load.value = std::bit_cast<double>(reader.u64());
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "constraints section ends inside a load");
        }
        constraints.loads.push_back(std::move(load));
    }

    if (!reader.at_end()) {
        return fail(Error::BadPayload, "constraints section has trailing bytes");
    }
    return Status{};
}

const char* section_name(const Section section) {
    switch (section) {
    case Section::Strings:
        return "strings";
    case Section::Hierarchy:
        return "hierarchy";
    case Section::Gates:
        return "gates";
    case Section::Nets:
        return "nets";
    case Section::Sources:
        return "sources";
    case Section::Meta:
        return "meta";
    case Section::Async:
        return "async";
    case Section::Enable:
        return "enable";
    case Section::ClockEdge:
        return "clock-edge";
    case Section::Constraints:
        return "constraints";
    }
    return "unknown";
}

bool offsets_are_slices(const std::vector<std::uint32_t>& begin, const std::uint32_t terminal) {
    if (begin.empty() || begin.back() != terminal) {
        return false;
    }
    for (std::size_t i = 1; i < begin.size(); ++i) {
        if (begin[i] < begin[i - 1]) {
            return false;
        }
    }
    return true;
}

// A section can carry a valid CRC and still describe a graph that cannot exist.
// These checks are the ones a parser can make without knowing the design.
Status validate_flat(const FlatNetlist& flat, const StringTable& strings) {
    const std::size_t gate_count = flat.gate_count();
    const std::size_t net_count = flat.net_count();
    if (flat.fanin_begin.size() != gate_count + 1 || flat.fanout_begin.size() != gate_count + 1) {
        return fail(Error::BadPayload, "gates section has the wrong number of slice offsets");
    }
    if (!offsets_are_slices(flat.fanin_begin, static_cast<std::uint32_t>(flat.fanins.size())) ||
        !offsets_are_slices(flat.fanout_begin, static_cast<std::uint32_t>(flat.fanouts.size()))) {
        return fail(Error::BadPayload, "gates section slice offsets do not match its arrays");
    }
    if (flat.out_net.size() != gate_count || flat.flags.size() != gate_count ||
        flat.level.size() != gate_count || flat.lib_cell.size() != gate_count ||
        flat.clock_edge.size() != gate_count || flat.enable.size() != gate_count ||
        flat.async_clear.size() != gate_count || flat.async_preset.size() != gate_count) {
        return fail(Error::BadPayload, "gates section arrays disagree on the gate count");
    }
    if (flat.name_id.size() != net_count || flat.source_loc.size() != net_count) {
        return fail(Error::BadPayload, "nets section arrays disagree on the net count");
    }
    for (const NetId net : flat.fanins) {
        if (net >= net_count) {
            return fail(Error::BadPayload, "a fanin names a net that does not exist");
        }
    }
    for (const NetId net : flat.fanouts) {
        if (net >= net_count) {
            return fail(Error::BadPayload, "a fanout names a net that does not exist");
        }
    }
    for (const NetId net : flat.out_net) {
        if (net != kInvalidId && net >= net_count) {
            return fail(Error::BadPayload, "a gate drives a net that does not exist");
        }
    }
    for (std::size_t gate = 0; gate < gate_count; ++gate) {
        if (flat.clock_edge[gate] == ClockEdge::Falling && flat.type[gate] != GateType::Dff) {
            return fail(Error::BadPayload, "a gate that is not a DFF has a falling clock edge");
        }
        const auto enable = flat.enable[gate];
        if (enable != kInvalidId) {
            if (enable >= net_count) {
                return fail(Error::BadPayload, "an enable names a net that does not exist");
            }
            if (flat.type[gate] != GateType::Dff) {
                return fail(Error::BadPayload, "a gate that is not a DFF carries an enable");
            }
            const auto enable_begin = flat.fanins.begin() + flat.fanin_begin[gate];
            const auto enable_end = flat.fanins.begin() + flat.fanin_begin[gate + 1];
            if (std::find(enable_begin, enable_end, enable) == enable_end) {
                return fail(Error::BadPayload, "an enable is not one of its gate's fanins");
            }
        }
        for (const NetId net : {flat.async_clear[gate], flat.async_preset[gate]}) {
            if (net == kInvalidId) {
                continue;
            }
            if (net >= net_count) {
                return fail(Error::BadPayload,
                            "an asynchronous control names a net that does not exist");
            }
            if (flat.type[gate] != GateType::Dff && flat.type[gate] != GateType::Dlat) {
                return fail(Error::BadPayload,
                            "a gate that is not a DFF or DLAT carries an asynchronous control");
            }
            const auto begin = flat.fanins.begin() + flat.fanin_begin[gate];
            const auto end = flat.fanins.begin() + flat.fanin_begin[gate + 1];
            if (std::find(begin, end, net) == end) {
                return fail(Error::BadPayload,
                            "an asynchronous control is not one of its gate's fanins");
            }
        }
    }
    for (const std::uint32_t driver : flat.driver) {
        if (driver != kPrimaryInput && driver != kInvalidId && driver >= gate_count) {
            return fail(Error::BadPayload, "a net names a driver that does not exist");
        }
    }
    for (const std::uint32_t name : flat.name_id) {
        if (name >= strings.size()) {
            return fail(Error::BadPayload, "a net names a string id the table does not have");
        }
    }
    for (const SourceId source : flat.source_loc) {
        if (source != kInvalidId && source >= flat.source_table.size()) {
            return fail(Error::BadPayload, "a net names a source location that does not exist");
        }
    }
    for (const FlatNetlist::SourceLocation& source : flat.source_table) {
        if (source.file >= strings.size()) {
            return fail(Error::BadPayload,
                        "a source location names a file the table does not have");
        }
    }
    return Status{};
}

}  // namespace

Status parse(std::span<const std::uint8_t> bytes, Design& design, FlatNetlist& flat,
             StringTable& strings, Constraints* constraints) {
    if (bytes.size() < kHeaderBytes) {
        return fail(Error::TruncatedFile, "file is shorter than a .dfdb header");
    }

    Reader reader(bytes.data(), bytes.size());
    for (const std::uint8_t expected : kMagic) {
        if (reader.u8() != expected) {
            return fail(Error::BadMagic, "not a .dfdb file");
        }
    }
    const std::uint16_t major = reader.u16();
    const std::uint16_t minor = reader.u16();
    const std::uint32_t endianness = reader.u32();
    const std::uint32_t section_count = reader.u32();
    if (!reader.ok()) {
        return fail(Error::TruncatedFile, "file ends inside the header");
    }
    if (endianness != kEndianMarker) {
        return fail(Error::BadEndianness, "file was written with a different byte order");
    }
    if (major != kSchemaMajor) {
        return fail(Error::UnsupportedMajor,
                    "file is .dfdb " + std::to_string(major) + ".x; this build reads " +
                        std::to_string(kSchemaMajor) + ".x and needs a converter");
    }
    if (minor > kSchemaMinor) {
        return fail(Error::NewerSchema,
                    "file is .dfdb " + std::to_string(major) + "." + std::to_string(minor) +
                        "; this build writes " + std::to_string(kSchemaMajor) + "." +
                        std::to_string(kSchemaMinor) + " and will not read a newer minor");
    }
    if (section_count == 0 || section_count > kMaxSections) {
        return fail(Error::SectionTable,
                    "file declares " + std::to_string(section_count) + " sections");
    }
    const auto table_end = static_cast<std::uint64_t>(kHeaderBytes) +
                           static_cast<std::uint64_t>(section_count) * kSectionEntryBytes;
    if (table_end > static_cast<std::uint64_t>(bytes.size())) {
        return fail(Error::TruncatedFile, "file ends inside the section table");
    }

    struct Entry {
        Section id;
        std::uint64_t offset;
        std::uint64_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(section_count);
    for (std::uint32_t i = 0; i < section_count; ++i) {
        const std::uint32_t raw_id = reader.u32();
        const std::uint32_t reserved = reader.u32();
        const std::uint64_t offset = reader.u64();
        const std::uint64_t size = reader.u64();
        const std::uint32_t stored_crc = reader.u32();
        if (!reader.ok()) {
            return fail(Error::TruncatedFile, "file ends inside the section table");
        }
        if (reserved != 0) {
            return fail(Error::SectionTable, "section table entry has a non-zero reserved field");
        }
        if (offset < table_end || offset > bytes.size() ||
            size > static_cast<std::uint64_t>(bytes.size()) - offset) {
            return fail(Error::SectionTable, "a section lies outside the file");
        }
        if (crc32(bytes.data() + static_cast<std::size_t>(offset),
                  static_cast<std::size_t>(size)) != stored_crc) {
            return fail(Error::CrcMismatch,
                        "section " + std::to_string(raw_id) + " fails its CRC32");
        }
        const auto id = static_cast<Section>(raw_id);
        for (const Entry& existing : entries) {
            if (existing.id == id) {
                return fail(Error::SectionTable,
                            "section " + std::to_string(raw_id) + " appears twice");
            }
        }
        entries.push_back({id, offset, size});
    }

    const auto find = [&entries](const Section id) -> const Entry* {
        for (const Entry& entry : entries) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    };
    std::vector<Section> required = {Section::Strings, Section::Hierarchy, Section::Gates,
                                     Section::Nets,    Section::Sources,   Section::Meta};
    // Each minor added one optional-in-older-files section; a file that predates
    // it is still valid and loads with that control absent.
    if (minor >= 1) {
        required.push_back(Section::Async);
    }
    if (minor >= 2) {
        required.push_back(Section::Enable);
    }
    if (minor >= 3) {
        required.push_back(Section::ClockEdge);
    }
    for (const Section section : required) {
        if (find(section) == nullptr) {
            return fail(Error::MissingSection,
                        std::string("file has no ") + section_name(section) + " section");
        }
    }
    for (const Entry& entry : entries) {
        switch (entry.id) {
        case Section::Strings:
        case Section::Hierarchy:
        case Section::Gates:
        case Section::Nets:
        case Section::Sources:
        case Section::Meta:
        case Section::Async:
        case Section::Enable:
        case Section::ClockEdge:
        case Section::Constraints:
            break;
        default:
            return fail(Error::UnknownSection,
                        "file has a section this build does not know: " +
                            std::to_string(static_cast<std::uint32_t>(entry.id)));
        }
    }

    const auto open = [&bytes](const Entry& entry) {
        return Reader(bytes.data() + static_cast<std::size_t>(entry.offset),
                      static_cast<std::size_t>(entry.size));
    };

    {
        Reader section = open(*find(Section::Strings));
        const Status status = parse_strings(section, strings);
        if (!status) {
            return status;
        }
    }
    {
        Reader section = open(*find(Section::Hierarchy));
        const Status status = parse_hierarchy(section, strings, design);
        if (!status) {
            return status;
        }
    }
    {
        Reader section = open(*find(Section::Gates));
        const Status status = parse_gates(section, flat);
        if (!status) {
            return status;
        }
    }
    {
        Reader section = open(*find(Section::Nets));
        const Status status = parse_nets(section, flat);
        if (!status) {
            return status;
        }
    }
    {
        Reader section = open(*find(Section::Sources));
        const Status status = parse_sources(section, flat);
        if (!status) {
            return status;
        }
    }
    // A 1.0 file has no async section, so default every control to absent
    // before the optional section fills them in.
    flat.async_clear.assign(flat.gate_count(), kInvalidId);
    flat.async_preset.assign(flat.gate_count(), kInvalidId);
    if (const Entry* async = find(Section::Async); async != nullptr) {
        Reader section = open(*async);
        const Status status =
            parse_control_section(section, strings, design, flat, "async", kAsyncColumns);
        if (!status) {
            return status;
        }
    }
    flat.enable.assign(flat.gate_count(), kInvalidId);
    if (const Entry* enable = find(Section::Enable); enable != nullptr) {
        Reader section = open(*enable);
        const Status status =
            parse_control_section(section, strings, design, flat, "enable", kEnableColumns);
        if (!status) {
            return status;
        }
    }
    // Files older than 1.3 have no clock-edge section; rising is the default.
    flat.clock_edge.assign(flat.gate_count(), ClockEdge::Rising);
    if (const Entry* clock_edge = find(Section::ClockEdge); clock_edge != nullptr) {
        Reader section = open(*clock_edge);
        const Status status = parse_clock_edge(section, design, flat);
        if (!status) {
            return status;
        }
    }
    if (constraints != nullptr) {
        constraints->present = false;
        if (const Entry* entry = find(Section::Constraints); entry != nullptr) {
            Reader section = open(*entry);
            const Status status = parse_constraints(section, *constraints, minor);
            if (!status) {
                return status;
            }
        }
    }
    {
        Reader section = open(*find(Section::Meta));
        const Status status = parse_meta(section, flat, strings);
        if (!status) {
            return status;
        }
    }

    return validate_flat(flat, strings);
}

}  // namespace gmax::db::dfdb
