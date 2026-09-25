// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <string_view>

namespace os {

enum class LogCategory {
    App,
    Document,
    Command,
    Geometry,
    Kernel,
    Sketch,
    Constraint,
    Selection,
    Interaction,
    Render,
    File,
    Performance,
};

enum class LogLevel { Debug, Info, Warning, Error };

std::string_view toString(LogCategory category);
std::string_view toString(LogLevel level);

using LogSink = std::function<void(LogLevel, LogCategory, const std::string&)>;

// Replaces the process-wide log sink. The default sink writes to stderr.
// Thread-safe with respect to concurrent log() calls.
void setLogSink(LogSink sink);
void setMinimumLogLevel(LogLevel level);
bool isLogEnabled(LogLevel level);

void log(LogLevel level, LogCategory category, const std::string& message);

// Stream-style helper: OS_LOG(Info, Geometry) << "made box " << dx;
class LogLine {
public:
    LogLine(LogLevel level, LogCategory category) : level_(level), category_(category) {}
    ~LogLine() { log(level_, category_, stream_.str()); }
    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    template <typename T>
    LogLine& operator<<(const T& value)
    {
        stream_ << value;
        return *this;
    }

private:
    LogLevel level_;
    LogCategory category_;
    std::ostringstream stream_;
};

} // namespace os

// Uses a one-iteration for-loop instead of if/else so the macro composes
// safely inside unbraced if-statements (no dangling-else ambiguity).
#define OS_LOG(level, category)                                                              for (bool os_log_once_ = ::os::isLogEnabled(::os::LogLevel::level); os_log_once_;              os_log_once_ = false)                                                                ::os::LogLine(::os::LogLevel::level, ::os::LogCategory::category)
