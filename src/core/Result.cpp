// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Result.h"

namespace os {

const char* toString(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None: return "None";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::InvalidReference: return "InvalidReference";
    case ErrorCode::KernelFailure: return "KernelFailure";
    case ErrorCode::InvalidResultShape: return "InvalidResultShape";
    case ErrorCode::FilletRadiusTooLarge: return "FilletRadiusTooLarge";
    case ErrorCode::ChamferTooLarge: return "ChamferTooLarge";
    case ErrorCode::ShellTooThick: return "ShellTooThick";
    case ErrorCode::EmptyResult: return "EmptyResult";
    case ErrorCode::NotPlanar: return "NotPlanar";
    case ErrorCode::FileNotFound: return "FileNotFound";
    case ErrorCode::FileReadError: return "FileReadError";
    case ErrorCode::FileWriteError: return "FileWriteError";
    case ErrorCode::FileFormatError: return "FileFormatError";
    case ErrorCode::FileVersionUnsupported: return "FileVersionUnsupported";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::NoEffect: return "NoEffect";
    }
    return "Unknown";
}

} // namespace os
