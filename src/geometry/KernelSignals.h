// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

namespace os::geom {

// OpenCASCADE turns an access violation inside a guarded kernel call into a
// failed step: its signal handlers jump back to the call. Outside kernel
// calls the same handlers would end the app with exit(1), hiding a real
// crash from the crash log and from the operating system's crash reports.
// So the application's crash handler installs this first, then puts itself
// in front and hands a fault over only while insideKernelCall() is true.
void installKernelSignalHandling();

// Whether the calling thread is inside a guarded kernel call.
bool insideKernelCall();

// For tests (--simulate-kernel-fault): an access violation inside a guarded
// kernel call. True if it came back as a failure, as it should.
bool simulateKernelFault();

} // namespace os::geom
