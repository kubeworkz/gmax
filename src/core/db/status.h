#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace gmax::db {

// Failure classes the database reports. They are values, not exceptions: loading
// a checkpoint is part of a run, and a run should be able to say what is wrong
// with a file without unwinding.
enum class Error {
    None = 0,
    IoError,
    BadMagic,
    UnsupportedMajor,
    NewerSchema,
    BadEndianness,
    TruncatedFile,
    SectionTable,
    MissingSection,
    UnknownSection,
    CrcMismatch,
    BadPayload,
    MultipleDrivers,
    CycleDetected,
};

inline std::string_view error_name(Error error) {
    switch (error) {
        case Error::None:
            return "ok";
        case Error::IoError:
            return "io-error";
        case Error::BadMagic:
            return "bad-magic";
        case Error::UnsupportedMajor:
            return "unsupported-major";
        case Error::NewerSchema:
            return "newer-schema";
        case Error::BadEndianness:
            return "bad-endianness";
        case Error::TruncatedFile:
            return "truncated-file";
        case Error::SectionTable:
            return "section-table";
        case Error::MissingSection:
            return "missing-section";
        case Error::UnknownSection:
            return "unknown-section";
        case Error::CrcMismatch:
            return "crc-mismatch";
        case Error::BadPayload:
            return "bad-payload";
        case Error::MultipleDrivers:
            return "multiple-drivers";
        case Error::CycleDetected:
            return "cycle-detected";
    }
    return "unknown";
}

class Status {
public:
    Status() = default;

    static Status failure(Error error, std::string message) {
        return Status(error, std::move(message));
    }

    bool ok() const { return error_ == Error::None; }
    explicit operator bool() const { return ok(); }
    Error error() const { return error_; }
    const std::string& message() const { return message_; }

private:
    Status(Error error, std::string message) : error_(error), message_(std::move(message)) {}

    Error error_ = Error::None;
    std::string message_;
};

}  // namespace gmax::db
