#pragma once

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace rm {

// Error categories shared by every module. Keep this list small and generic:
// modules add context through the message, not through new codes.
enum class ErrorCode {
    NotFound,
    AlreadyExists,
    NotADirectory,
    IsADirectory,
    NotEmpty,
    InvalidPath,
    PermissionDenied,
    IoError,
    Unsupported,
    InvalidArgument,
    ParseError,            // malformed document (JSON, cfg...)
    NetworkError,          // unreachable host, timeout, protocol failure
    AuthenticationFailed,  // server rejected the credentials
    Cancelled,             // stopped on user request
    InsufficientSpace,     // not enough free space on the SD card
    IntegrityError,        // downloaded data does not match its checksum
    NotConfigured,         // a required setting is missing (config.json)
};

const char* toString(ErrorCode code);

struct Error {
    ErrorCode code;
    std::string message;

    std::string describe() const;
};

// Lightweight C++17 stand-in for std::expected: a value or an Error.
// Nothing in the core throws; failures travel through Result.
template <typename T>
class [[nodiscard]] Result {
  public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    T& value() & {
        assert(ok());
        return std::get<T>(data_);
    }
    const T& value() const& {
        assert(ok());
        return std::get<T>(data_);
    }
    T&& value() && {
        assert(ok());
        return std::get<T>(std::move(data_));
    }

    const Error& error() const {
        assert(!ok());
        return std::get<Error>(data_);
    }

    T valueOr(T fallback) const { return ok() ? std::get<T>(data_) : std::move(fallback); }

  private:
    std::variant<T, Error> data_;
};

// Result of an operation that produces no value.
class [[nodiscard]] Status {
  public:
    Status() = default;
    Status(Error error) : error_(std::move(error)), ok_(false) {}

    bool ok() const { return ok_; }
    explicit operator bool() const { return ok_; }

    const Error& error() const {
        assert(!ok_);
        return error_;
    }

  private:
    Error error_{ErrorCode::IoError, {}};
    bool ok_ = true;
};

inline Status success() { return {}; }

inline Error makeError(ErrorCode code, std::string message) { return Error{code, std::move(message)}; }

}  // namespace rm
