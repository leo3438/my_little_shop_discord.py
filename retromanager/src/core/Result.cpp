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
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::ParseError: return "ParseError";
        case ErrorCode::NetworkError: return "NetworkError";
        case ErrorCode::AuthenticationFailed: return "AuthenticationFailed";
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
