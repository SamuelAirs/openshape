// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>

namespace os::app {

// When the app dies unexpectedly, writes one line to stderr and to the log
// file, so a crash report says what happened and where: on Windows an
// unhandled exception (e.g. an access violation, with the module and offset
// of the faulting address), everywhere std::terminate (e.g. an uncaught C++
// exception). Best effort and allocation-free at crash time; the operating
// system's own crash handling (Windows Error Reporting, crash reports) still
// runs afterwards.
void installCrashLogging(const QString& logFile);

// --simulate-crash: dies the way a real crash does (an access violation on
// Windows, std::terminate elsewhere), after the caller wrote a recovery copy.
// Skips the operating system's crash dialog so automated checks never hang.
[[noreturn]] void simulateCrash();

} // namespace os::app
