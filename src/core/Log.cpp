// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace os {

namespace {

std::mutex& sinkMutex()
{
    static std::mutex mutex;
    return mutex;
}

LogSink& sinkStorage()
{
    static LogSink sink;
    return sink;
}

std::atomic<int>& minimumLevel()
{
    static std::atomic<int> level{static_cast<int>(LogLevel::Info)};
    return level;
}

void defaultSink(LogLevel level, LogCategory category, const std::string& message)
{
    std::fprintf(stderr, "[%s] %s: %s\n", std::string(toString(level)).c_str(),
                 std::string(toString(category)).c_str(), message.c_str());
}

} // namespace

std::string_view toString(LogCategory category)
{
    switch (category) {
    case LogCategory::App: return "APP";
    case LogCategory::Document: return "DOCUMENT";
    case LogCategory::Command: return "COMMAND";
    case LogCategory::Geometry: return "GEOMETRY";
    case LogCategory::Kernel: return "KERNEL";
    case LogCategory::Sketch: return "SKETCH";
    case LogCategory::Constraint: return "CONSTRAINT";
    case LogCategory::Selection: return "SELECTION";
    case LogCategory::Interaction: return "INTERACTION";
    case LogCategory::Render: return "RENDER";
    case LogCategory::File: return "FILE";
    case LogCategory::Performance: return "PERFORMANCE";
    }
    return "UNKNOWN";
}

std::string_view toString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    }
    return "unknown";
}

void setLogSink(LogSink sink)
{
    std::lock_guard lock(sinkMutex());
    sinkStorage() = std::move(sink);
}

void setMinimumLogLevel(LogLevel level)
{
    minimumLevel().store(static_cast<int>(level));
}

bool isLogEnabled(LogLevel level)
{
    return static_cast<int>(level) >= minimumLevel().load();
}

void log(LogLevel level, LogCategory category, const std::string& message)
{
    if (!isLogEnabled(level))
        return;
    std::lock_guard lock(sinkMutex());
    if (sinkStorage())
        sinkStorage()(level, category, message);
    else
        defaultSink(level, category, message);
}

} // namespace os
