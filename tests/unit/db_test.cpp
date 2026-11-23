// WP-002's exit check: a synthetic 100-gate design round-trips through .dfdb
// with the same ids and the same bytes. Framework-free for the same reason as
// version_test — third_party stays empty until TBD-11 closes.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/crc32.h"
#include "core/db/db.h"
#include "core/db/dfdb.h"

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

std::string pad2(int value) {
    std::string text = std::to_string(value);
    if (text.size() < 2) {
        text.insert(text.begin(), '0');
    }
    return text;
}

// Twenty instances of a five-gate leaf: exactly 100 gates. The leaf's ports are
// bound to nets in top, so flattening has to resolve connections across the
// hierarchy rather than numbering each instance in isolation.
gmax::db::Design make_design(bool reverse) {
    using namespace gmax::db;

    Design design;
    design.top = "top";

    Module leaf;
    leaf.name = "leaf";
    leaf.inputs = {"a", "b"};
    leaf.outputs = {"y"};
    leaf.gates = {
        {GateType::And2, "g1", {"a", "b"}, "n1", 0, "", ""},
        {GateType::Or2, "g2", {"n1", "a"}, "n2", 0, "", ""},
        {GateType::Not, "g3", {"n2"}, "n3", 0, "", ""},
        {GateType::Buf, "g4", {"n3"}, "n4", 0, "", ""},
        {GateType::Nand2, "g5", {"n4", "b"}, "y", 1, "", ""},
    };

    Module top;
    top.name = "top";
    top.inputs = {"a", "b"};
    for (int i = 0; i < 20; ++i) {
        top.outputs.push_back("y" + pad2(i));
    }

    std::vector<Instance> instances;
    for (int i = 0; i < 20; ++i) {
        Instance instance;
        instance.path = "u" + pad2(i);
        instance.module = "leaf";
        instance.connections = {{"a", "a"}, {"b", "b"}, {"y", "y" + pad2(i)}};
        instances.push_back(instance);
    }

    // The reversed variant is the same design built in a different order; it must
    // serialize to the same bytes or "sorted key order" is not doing its job.
    if (reverse) {
        design.modules = {leaf, top};
        design.instances.assign(instances.rbegin(), instances.rend());
    } else {
        design.modules = {top, leaf};
        design.instances = instances;
    }
    return design;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void write_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
    using namespace gmax::db;

    // The container is verified against the standard CRC-32 check value, not
    // against itself.
    expect(crc32("123456789") == 0xCBF43926u, "CRC32 of 123456789 matches the check value");

    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-db-test.dfdb").string();
    const std::string copy = (directory / "gmax-db-test-copy.dfdb").string();

    DesignDb db;
    db.design() = make_design(false);
    const Status flattened = db.flatten();
    expect(flattened.ok(), "flatten succeeds");
    if (!flattened.ok()) {
        std::cerr << "  " << flattened.message() << std::endl;
    }

    expect(db.flat().gate_count() == 100, "the synthetic design is 100 gates");
    expect(db.flat().net_count() == 102, "100 gates and 2 primary inputs give 102 nets");
    expect(db.flat().driver[0] == kPrimaryInput, "top input a is driven by a primary input");
    expect(db.flat().driver[1] == kPrimaryInput, "top input b is driven by a primary input");
    expect(db.flat().level[4] == 4, "the leaf's fifth gate sits at level 4");

    // The bound output port is the interesting case: u03's g5 drives top's y03,
    // so the hierarchical gate numbering has to resolve the connection.
    NetId y03 = kInvalidId;
    for (NetId net = 0; net < db.flat().net_count(); ++net) {
        if (db.strings().at(db.flat().name_id[net]) == "y03") {
            y03 = net;
        }
    }
    expect(y03 != kInvalidId, "y03 exists as a net");
    if (y03 != kInvalidId) {
        expect(db.flat().driver[y03] == 19u, "y03 is driven by u03's last gate");
    }
    bool hierarchical = false;
    for (const std::string& entry : db.strings().entries()) {
        if (entry == "u03/n1") {
            hierarchical = true;
        }
    }
    expect(hierarchical, "a leaf net keeps its hierarchical path");

    // Regression: an instance input bound to a net that a later sibling drives.
    // Walk order is by path, so the consumer is visited while its fanin is still
    // undriven. That net is not a primary input, and marking it as one made the
    // real driver look like a second driver.
    const auto chain = [](const std::string& driver_path, const std::string& consumer_path) {
        Design design;
        design.top = "top";

        Module buffer;
        buffer.name = "buf";
        buffer.inputs = {"a"};
        buffer.outputs = {"y"};
        buffer.gates = {{GateType::Buf, "g1", {"a"}, "y", kInvalidId, "", ""}};

        Module top;
        top.name = "top";
        top.inputs = {"pi"};
        top.outputs = {"po"};
        design.modules = {top, buffer};

        Instance source;
        source.path = driver_path;
        source.module = "buf";
        source.connections = {{"a", "pi"}, {"y", "n1"}};

        Instance sink;
        sink.path = consumer_path;
        sink.module = "buf";
        sink.connections = {{"a", "n1"}, {"y", "po"}};

        design.instances = {source, sink};
        return design;
    };

    const auto net_named = [](const DesignDb& database, const std::string& name) {
        for (NetId net = 0; net < database.flat().net_count(); ++net) {
            if (database.strings().at(database.flat().name_id[net]) == name) {
                return net;
            }
        }
        return static_cast<NetId>(kInvalidId);
    };

    DesignDb chained;
    chained.design() = chain("u01", "u00");  // the consumer sorts first
    const Status chained_status = chained.flatten();
    expect(chained_status.ok(), "a net driven by a later-sorted instance is not a primary input");
    if (!chained_status.ok()) {
        std::cerr << "  " << chained_status.message() << std::endl;
    }
    expect(chained.flat().gate_count() == 2, "the two-instance chain flattens to two gates");
    expect(chained.flat().net_count() == 3, "the chain has pi, n1 and po");
    const NetId chain_pi = net_named(chained, "pi");
    const NetId chain_link = net_named(chained, "n1");
    expect(chain_pi != kInvalidId, "the chain has a pi net");
    expect(chain_link != kInvalidId, "the chain has its link net");
    if (chain_pi != kInvalidId && chain_link != kInvalidId) {
        expect(chained.flat().driver[chain_pi] == kPrimaryInput,
               "only the top input is a primary input");
        expect(chained.flat().driver[chain_link] == 1u,
               "the link net is driven by the later instance's gate");
    }
    expect(chained.flat().level[0] == 1 && chained.flat().level[1] == 0,
           "the consumer sits one level above its driver");

    DesignDb forward;
    forward.design() = chain("u00", "u01");  // the driver sorts first
    expect(forward.flatten().ok(), "the same chain flattens when the driver sorts first");

    // A flop whose next-state cone reads its own output is a register, not a
    // combinational loop: levelization cuts edges out of sequential gates.
    Module latch;
    latch.name = "latch";
    latch.inputs = {"d", "en"};
    latch.outputs = {"q"};
    latch.gates = {
        {GateType::Not, "g1", {"q"}, "n1", 0, "", ""},
        {GateType::And2, "g2", {"d", "n1"}, "n2", 0, "", ""},
        {GateType::Dlat, "g3", {"n2", "en"}, "q", 0, "", ""},
    };
    Design sequential;
    sequential.top = "latch";
    sequential.modules = {latch};
    DesignDb feedback;
    feedback.design() = sequential;
    const Status feedback_status = feedback.flatten();
    expect(feedback_status.ok(), "a latch that reads its own output is not a loop");
    if (!feedback_status.ok()) {
        std::cerr << "  " << feedback_status.message() << std::endl;
    }
    expect(feedback.flat().gate_count() == 3, "the feedback latch keeps its three gates");
    if (feedback.flat().gate_count() == 3) {
        expect(feedback.flat().level[2] == 2, "the flop sits above its data cone");
    }

    const Status saved = db.save(path);
    expect(saved.ok(), "save writes a .dfdb");
    if (!saved.ok()) {
        std::cerr << "  " << saved.message() << std::endl;
    }

    DesignDb loaded;
    const Status read = DesignDb::load(path, loaded);
    expect(read.ok(), "load reads a .dfdb");
    if (!read.ok()) {
        std::cerr << "  " << read.message() << std::endl;
    }
    expect(loaded.flat() == db.flat(), "the flat graph survives the round trip unchanged");
    expect(loaded.design() == db.design(), "the hierarchy survives the round trip unchanged");
    expect(loaded.strings() == db.strings(), "the string table survives the round trip unchanged");

    // Stable ids mean the same hierarchy numbers the same way twice, not merely
    // that one written file matches itself.
    DesignDb rebuilt;
    rebuilt.design() = loaded.design();
    const Status reflattened = rebuilt.flatten();
    expect(reflattened.ok(), "the loaded hierarchy flattens again");
    expect(rebuilt.flat() == db.flat(), "re-flattening assigns identical gate and net ids");
    expect(rebuilt.strings() == db.strings(), "re-flattening assigns identical string ids");

    // Asynchronous clear and preset are part of the gate model and the flat
    // graph. 1.1 carries them in an optional section, so a 1.0 file still loads.
    {
        DesignDb async;
        async.design().top = "top";
        Module top;
        top.name = "top";
        top.inputs = {"d", "ck", "rn", "sn"};
        top.outputs = {"q"};
        Gate flop;
        flop.type = GateType::Dff;
        flop.name = "g1";
        flop.fanins = {"d", "ck", "rn", "sn"};
        flop.out = "q";
        flop.async_clear = "rn";
        flop.async_preset = "sn";
        top.gates = {flop};
        async.design().modules = {top};

        expect(async.flatten().ok(), "a flop with async set and reset flattens");
        expect(async.flat().gate_count() == 1, "the async flop is one gate");
        expect(async.flat().async_clear.size() == 1 && async.flat().async_preset.size() == 1,
               "the flat graph carries one control entry per gate");
        if (async.flat().gate_count() == 1) {
            const NetId clear = async.flat().async_clear[0];
            const NetId preset = async.flat().async_preset[0];
            expect(clear != kInvalidId && preset != kInvalidId, "both controls name a net");
            expect(clear != kInvalidId && async.strings().at(async.flat().name_id[clear]) == "rn",
                   "the async clear is the rn net");
            expect(preset != kInvalidId && async.strings().at(async.flat().name_id[preset]) == "sn",
                   "the async preset is the sn net");
        }

        const std::vector<std::uint8_t> bytes =
            dfdb::serialize(async.design(), async.flat(), async.strings());
        Design design_out;
        FlatNetlist flat_out;
        StringTable strings_out;
        expect(dfdb::parse(bytes, design_out, flat_out, strings_out).ok(),
               "the async controls round-trip through .dfdb");
        expect(flat_out == async.flat(), "the async flat graph survives the round trip");
        expect(design_out == async.design(), "the async hierarchy survives the round trip");

        // Downgrade the file to schema 1.0: async is written last in both the
        // section table and the payload, so drop its entry and trailing payload
        // and set the minor version to zero.
        const auto u32_at = [](const std::vector<std::uint8_t>& data, std::size_t at) {
            return static_cast<std::uint32_t>(data[at]) |
                   (static_cast<std::uint32_t>(data[at + 1]) << 8) |
                   (static_cast<std::uint32_t>(data[at + 2]) << 16) |
                   (static_cast<std::uint32_t>(data[at + 3]) << 24);
        };
        const auto u64_at = [](const std::vector<std::uint8_t>& data, std::size_t at) {
            std::uint64_t value = 0;
            for (int shift = 0; shift < 64; shift += 8) {
                value |= static_cast<std::uint64_t>(data[at + shift / 8]) << shift;
            }
            return value;
        };
        std::vector<std::uint8_t> old = bytes;
        const std::uint32_t sections = u32_at(old, 12);
        const std::size_t last_entry = 16 + (sections - 1) * 28;
        const auto async_offset = u64_at(old, last_entry + 8);
        old.erase(old.begin() + static_cast<std::ptrdiff_t>(last_entry),
                  old.begin() + static_cast<std::ptrdiff_t>(last_entry + 28));
        // Every payload moved back by one table entry, so every remaining
        // section offset does too.
        for (std::uint32_t i = 0; i + 1 < sections; ++i) {
            const std::size_t at = 16 + i * 28 + 8;
            const std::uint64_t shifted = u64_at(old, at) - 28;
            for (int byte = 0; byte < 8; ++byte) {
                old[at + static_cast<std::size_t>(byte)] =
                    static_cast<std::uint8_t>((shifted >> (8 * byte)) & 0xFFu);
            }
        }
        old.resize(static_cast<std::size_t>(async_offset) - 28);
        const std::uint32_t old_sections = sections - 1;
        old[12] = static_cast<std::uint8_t>(old_sections & 0xFFu);
        old[13] = static_cast<std::uint8_t>((old_sections >> 8) & 0xFFu);
        old[14] = static_cast<std::uint8_t>((old_sections >> 16) & 0xFFu);
        old[15] = static_cast<std::uint8_t>((old_sections >> 24) & 0xFFu);
        old[6] = 0;
        old[7] = 0;

        Design old_design;
        FlatNetlist old_flat;
        StringTable old_strings;
        const Status old_status = dfdb::parse(old, old_design, old_flat, old_strings);
        expect(old_status.ok(), "a 1.0 file without the async section still loads");
        if (!old_status.ok()) {
            std::cerr << "  " << old_status.message() << std::endl;
        }
        expect(old_flat.async_clear.size() == 1 && old_flat.async_preset.size() == 1 &&
                   old_flat.async_clear[0] == kInvalidId && old_flat.async_preset[0] == kInvalidId,
               "a 1.0 file loads with its async controls absent");
    }

    expect(loaded.save(copy).ok(), "the loaded database saves");
    expect(read_bytes(path) == read_bytes(copy), "save(load(x)) is byte-identical to x");

    DesignDb reversed;
    reversed.design() = make_design(true);
    expect(reversed.flatten().ok(), "a design built in reverse order flattens");
    expect(reversed.save(copy).ok(), "a design built in reverse order saves");
    expect(read_bytes(copy) == read_bytes(path), "insertion order does not reach the bytes");

    const std::vector<std::uint8_t> good = read_bytes(path);
    expect(good.size() > 16, "the file carries a header and sections");
    const auto load_error = [&copy](const std::vector<std::uint8_t>& candidate) {
        write_bytes(copy, candidate);
        DesignDb probe;
        return DesignDb::load(copy, probe).error();
    };

    std::vector<std::uint8_t> newer = good;
    newer[6] = 2;  // minor version, one past what this build writes
    expect(load_error(newer) == Error::NewerSchema, "a newer minor is refused, not guessed");

    std::vector<std::uint8_t> foreign = good;
    foreign[0] = 'X';
    expect(load_error(foreign) == Error::BadMagic, "a foreign file is rejected by its magic");

    std::vector<std::uint8_t> major = good;
    major[4] = 2;
    expect(load_error(major) == Error::UnsupportedMajor, "another major version needs a converter");

    std::vector<std::uint8_t> short_file = good;
    short_file.resize(8);
    expect(load_error(short_file) == Error::TruncatedFile,
           "a file shorter than the header is truncated");

    std::vector<std::uint8_t> rotted = good;
    rotted.back() = static_cast<std::uint8_t>(rotted.back() ^ 0xFFu);
    expect(load_error(rotted) == Error::CrcMismatch, "a flipped byte fails its section CRC32");

    DesignDb cyclic;
    cyclic.design().top = "top";
    Module loop;
    loop.name = "top";
    loop.inputs = {"a"};
    loop.gates = {
        {GateType::And2, "g1", {"n2", "a"}, "n1", kInvalidId, "", ""},
        {GateType::Or2, "g2", {"n1"}, "n2", kInvalidId, "", ""},
    };
    cyclic.design().modules = {loop};
    expect(cyclic.flatten().error() == Error::CycleDetected,
           "a combinational loop is reported, not silently ordered");

    DesignDb doubled;
    doubled.design().top = "top";
    Module shared;
    shared.name = "top";
    shared.gates = {
        {GateType::Buf, "g1", {"n0"}, "n1", kInvalidId, "", ""},
        {GateType::Buf, "g2", {"n0"}, "n1", kInvalidId, "", ""},
    };
    doubled.design().modules = {shared};
    expect(doubled.flatten().error() == Error::MultipleDrivers,
           "two gates driving one net is reported");

    // Source locations have no builder path yet, so the container is exercised
    // directly with a hand-built graph.
    {
        Design minimal;
        minimal.top = "t";
        Module module;
        module.name = "t";
        minimal.modules = {module};

        StringTable strings;
        FlatNetlist flat;
        flat.type = {GateType::Buf};
        flat.fanin_begin = {0, 0};
        flat.fanout_begin = {0, 0};
        flat.out_net = {0};
        flat.flags = {0};
        flat.level = {0};
        flat.lib_cell = {kInvalidId};
        flat.async_clear = {kInvalidId};
        flat.async_preset = {kInvalidId};
        flat.driver = {0};
        flat.name_id = {strings.intern("n")};
        flat.source_loc = {0};
        flat.source_table = {{strings.intern("t.v"), 12}};

        const std::vector<std::uint8_t> bytes = dfdb::serialize(minimal, flat, strings);
        Design design_out;
        FlatNetlist flat_out;
        StringTable strings_out;
        const Status parsed = dfdb::parse(bytes, design_out, flat_out, strings_out);
        expect(parsed.ok(), "a flat graph carrying a source location round-trips");
        expect(flat_out == flat, "source locations survive the round trip");
        expect(design_out == minimal, "the minimal hierarchy survives the round trip");
    }

    std::filesystem::remove(path);
    std::filesystem::remove(copy);

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "db " << db.flat().gate_count() << " gates, " << db.flat().net_count() << " nets"
              << std::endl;
    return EXIT_SUCCESS;
}
