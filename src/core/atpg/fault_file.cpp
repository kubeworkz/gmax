#include "core/atpg/fault_file.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/db/crc32.h"
#include "core/db/dfdb.h"

namespace gmax::atpg {
namespace {

std::string hex32(const std::uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

bool parse_u32(const std::string_view text, const int base, std::uint32_t& out) {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool header_value(const std::string& line, const std::string_view key, std::string_view& value) {
    if (line.size() <= key.size() || line.compare(0, key.size(), key) != 0 ||
        line[key.size()] != ' ') {
        return false;
    }
    value = std::string_view(line).substr(key.size() + 1);
    return !value.empty();
}

bool parse_fault_line(const std::string& line, const char kind, std::uint32_t& net,
                      std::uint32_t& stuck, std::uint32_t* collapsed) {
    std::istringstream stream(line);
    char actual = 0;
    stream >> actual >> net >> stuck;
    if (collapsed != nullptr) {
        stream >> *collapsed;
    }
    if (!stream || actual != kind || stuck > 1) {
        return false;
    }
    stream >> std::ws;
    return stream.eof();
}

std::string fault_body(const std::span<const sim::Fault> raw, const CollapsedFaultList& collapsed) {
    std::string body;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        body += "R " + std::to_string(raw[index].net) + " " +
                (raw[index].stuck == sim::StuckValue::One ? "1" : "0") + " " +
                std::to_string(collapsed.raw_to_collapsed[index]) + "\n";
    }
    for (const sim::Fault& fault : collapsed.faults) {
        body += "C " + std::to_string(fault.net) + " " +
                (fault.stuck == sim::StuckValue::One ? "1" : "0") + "\n";
    }
    return body;
}

std::string fault_payload(const std::span<const sim::Fault> raw,
                          const CollapsedFaultList& collapsed, const std::string& body) {
    return "raw-count " + std::to_string(raw.size()) + "\n" + "collapsed-count " +
           std::to_string(collapsed.faults.size()) + "\n" + body;
}

db::Status failure(const db::Error error, std::string message) {
    return db::Status::failure(error, std::move(message));
}

}  // namespace

std::uint32_t netlist_hash(const db::DesignDb& database) {
    const std::vector<std::uint8_t> bytes =
        db::dfdb::serialize(database.design(), database.flat(), database.strings());
    return db::crc32(bytes.data(), bytes.size());
}

db::Status write_faults(const std::string& path, const db::DesignDb& database,
                        const std::span<const sim::Fault> raw,
                        const CollapsedFaultList& collapsed) {
    if (raw.size() != collapsed.raw_to_collapsed.size()) {
        return failure(db::Error::BadPayload,
                       "the raw fault list and its collapsed mapping have different sizes");
    }
    if (raw.size() > std::numeric_limits<std::uint32_t>::max() ||
        collapsed.faults.size() > std::numeric_limits<std::uint32_t>::max()) {
        return failure(db::Error::BadPayload, "the fault list exceeds the .faults count range");
    }
    for (const sim::Fault& fault : raw) {
        if (fault.net >= database.flat().net_count()) {
            return failure(db::Error::BadPayload,
                           "a raw fault names a net outside the flattened netlist");
        }
    }
    for (const sim::Fault& fault : collapsed.faults) {
        if (fault.net >= database.flat().net_count()) {
            return failure(db::Error::BadPayload,
                           "a collapsed fault names a net outside the flattened netlist");
        }
    }
    for (const std::uint32_t id : collapsed.raw_to_collapsed) {
        if (id >= collapsed.faults.size()) {
            return failure(db::Error::BadPayload,
                           "a raw fault names a collapsed class that does not exist");
        }
    }

    const std::string body = fault_body(raw, collapsed);
    const std::string payload = fault_payload(raw, collapsed, body);
    const std::uint32_t fault_hash = db::crc32(payload);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return failure(db::Error::IoError, "cannot open '" + path + "' for writing");
    }
    out << "gmax-faults 1\n";
    out << "netlist " << hex32(netlist_hash(database)) << "\n";
    out << "raw-count " << raw.size() << "\n";
    out << "collapsed-count " << collapsed.faults.size() << "\n";
    out << "fault-list " << hex32(fault_hash) << "\n";
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!out) {
        return failure(db::Error::IoError, "cannot write '" + path + "'");
    }
    return {};
}

db::Status read_faults(const std::string& path, const db::DesignDb& database, FaultListFile& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return failure(db::Error::IoError, "cannot open '" + path + "' for reading");
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0) {
        return failure(db::Error::IoError, "cannot size '" + path + "'");
    }

    std::string line;
    const auto next = [&in, &line] { return static_cast<bool>(std::getline(in, line)); };
    if (!next() || line != "gmax-faults 1") {
        return failure(db::Error::BadMagic, "not a gmax-faults version 1 file");
    }

    std::uint32_t net_hash = 0;
    std::uint32_t raw_count = 0;
    std::uint32_t collapsed_count = 0;
    std::uint32_t fault_hash = 0;
    std::string_view value;
    if (!next() || !header_value(line, "netlist", value) || !parse_u32(value, 16, net_hash)) {
        return failure(db::Error::BadPayload, "the .faults netlist hash is missing or invalid");
    }
    if (!next() || !header_value(line, "raw-count", value) || !parse_u32(value, 10, raw_count)) {
        return failure(db::Error::BadPayload, "the .faults raw count is missing or invalid");
    }
    if (!next() || !header_value(line, "collapsed-count", value) ||
        !parse_u32(value, 10, collapsed_count)) {
        return failure(db::Error::BadPayload, "the .faults collapsed count is missing or invalid");
    }
    if (!next() || !header_value(line, "fault-list", value) || !parse_u32(value, 16, fault_hash)) {
        return failure(db::Error::BadPayload, "the .faults fault-list hash is missing or invalid");
    }
    if (static_cast<std::uint64_t>(raw_count) > static_cast<std::uint64_t>(size) ||
        static_cast<std::uint64_t>(collapsed_count) > static_cast<std::uint64_t>(size)) {
        return failure(db::Error::BadPayload, "the .faults counts exceed the file size");
    }

    const std::uint32_t expected_netlist = netlist_hash(database);
    if (net_hash != expected_netlist) {
        return failure(db::Error::BadPayload, "fault list was written for netlist " +
                                                  hex32(net_hash) + ", not " +
                                                  hex32(expected_netlist));
    }

    std::string payload;
    payload += "raw-count " + std::to_string(raw_count) + "\n";
    payload += "collapsed-count " + std::to_string(collapsed_count) + "\n";
    std::vector<sim::Fault> raw;
    raw.reserve(raw_count);
    std::vector<std::uint32_t> raw_to_collapsed;
    raw_to_collapsed.reserve(raw_count);
    for (std::uint32_t index = 0; index < raw_count; ++index) {
        std::uint32_t net = 0;
        std::uint32_t stuck = 0;
        std::uint32_t collapsed_id = 0;
        if (!next()) {
            return failure(db::Error::TruncatedFile, "the .faults file ends inside its raw list");
        }
        if (!parse_fault_line(line, 'R', net, stuck, &collapsed_id)) {
            return failure(db::Error::BadPayload, "the .faults raw entry is malformed");
        }
        if (net >= database.flat().net_count() || collapsed_id >= collapsed_count) {
            return failure(db::Error::BadPayload,
                           "the .faults raw entry names an out-of-range net or class");
        }
        raw.push_back(sim::Fault{net, stuck == 1 ? sim::StuckValue::One : sim::StuckValue::Zero});
        raw_to_collapsed.push_back(collapsed_id);
        payload += "R " + std::to_string(net) + " " + (stuck == 1 ? "1" : "0") + " " +
                   std::to_string(collapsed_id) + "\n";
    }

    std::vector<sim::Fault> collapsed;
    collapsed.reserve(collapsed_count);
    for (std::uint32_t index = 0; index < collapsed_count; ++index) {
        std::uint32_t net = 0;
        std::uint32_t stuck = 0;
        if (!next()) {
            return failure(db::Error::TruncatedFile,
                           "the .faults file ends inside its collapsed list");
        }
        if (!parse_fault_line(line, 'C', net, stuck, nullptr)) {
            return failure(db::Error::BadPayload, "the .faults collapsed entry is malformed");
        }
        if (net >= database.flat().net_count()) {
            return failure(db::Error::BadPayload,
                           "the .faults collapsed entry names an out-of-range net");
        }
        collapsed.push_back(
            sim::Fault{net, stuck == 1 ? sim::StuckValue::One : sim::StuckValue::Zero});
        payload += "C " + std::to_string(net) + " " + (stuck == 1 ? "1" : "0") + "\n";
    }
    if (next()) {
        return failure(db::Error::BadPayload, "the .faults file has trailing data");
    }
    if (db::crc32(payload) != fault_hash) {
        return failure(db::Error::CrcMismatch,
                       "the .faults payload does not match its fault-list hash");
    }

    out.header = FaultListHeader{net_hash, fault_hash, raw_count, collapsed_count};
    out.raw = std::move(raw);
    out.collapsed = std::move(collapsed);
    out.raw_to_collapsed = std::move(raw_to_collapsed);
    return {};
}

}  // namespace gmax::atpg
