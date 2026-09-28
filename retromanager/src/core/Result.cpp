#include "retromanager/core/Result.hpp"

namespace rm {

const char* toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::AlreadyExists: return "AlreadyExists";
        case ErrorCode::NotADirectory: return "NotADirectory";
        case ErrorCode::IsADirectory: return "IsADirectory";
        case ErrorCode::NotEmpty: return "NotEmpty";
        case ErrorCode::InvalidPath: return "InvalidPath";
        case ErrorCode::PermissionDenied: return "PermissionDenied";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::Unsupported: return "Unsupported";
    }
    return "Unknown";
}

std::string Error::describe() const {
    std::string out = toString(code);
    if (!message.empty()) {
        out += ": ";
        out += message;
    }
    return out;
}

}  // namespace rm
