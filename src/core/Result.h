#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace os {

// Error codes for operations that can fail for reasons the user can act on.
// Keep this list short and meaningful; the developer message carries detail.
enum class ErrorCode {
    None,
    InvalidArgument,
    InvalidReference,     // a stored face/edge/object reference no longer resolves
    KernelFailure,        // the kernel threw or reported not-done
    InvalidResultShape,   // the kernel produced a shape that fails validity checks
    FilletRadiusTooLarge,
    ChamferTooLarge,
    EmptyResult,          // e.g. a cut removed the whole body
    NotPlanar,
    FileNotFound,
    FileReadError,
    FileWriteError,
    FileFormatError,
    FileVersionUnsupported,
    Unsupported,
};

const char* toString(ErrorCode code);

// Structured operation outcome. Kernel failures never escape as exceptions:
// they are converted into a Result carrying a user-facing message (plain
// language, actionable) and a developer message (technical, for the log).
template <typename T>
class [[nodiscard]] Result {
public:
    static Result success(T value, std::vector<std::string> warnings = {})
    {
        Result r;
        r.value_ = std::move(value);
        r.warnings_ = std::move(warnings);
        return r;
    }

    static Result failure(ErrorCode code, std::string userMessage, std::string developerMessage = {})
    {
        Result r;
        r.code_ = code;
        r.userMessage_ = std::move(userMessage);
        r.developerMessage_ = std::move(developerMessage);
        return r;
    }

    template <typename U>
    static Result failureFrom(const Result<U>& other)
    {
        return failure(other.error(), other.userMessage(), other.developerMessage());
    }

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }

    T& value() { return *value_; }
    const T& value() const { return *value_; }
    T* operator->() { return &*value_; }
    const T* operator->() const { return &*value_; }

    ErrorCode error() const { return code_; }
    const std::string& userMessage() const { return userMessage_; }
    const std::string& developerMessage() const { return developerMessage_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::optional<T> value_;
    ErrorCode code_ = ErrorCode::None;
    std::string userMessage_;
    std::string developerMessage_;
    std::vector<std::string> warnings_;
};

struct Unit {};
using Status = Result<Unit>;

inline Status okStatus() { return Status::success(Unit{}); }

} // namespace os
