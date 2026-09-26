// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Release builds (OPENSHAPE_OWN_OCCT, Windows) link OpenShape's own
// OpenCASCADE build (scripts/windows/build-occt.sh). MSYS2's OCCT package
// has the same version, so it would load just as well when it comes first
// on PATH - and the tests would then check the wrong kernel. ctest puts the
// own build first (TEST_LAUNCHER in tests/CMakeLists.txt); this test fails
// loudly when any other OCCT toolkit is loaded anyway. Other builds have no
// test here.

#include <gtest/gtest.h>

#if defined(_WIN32) && defined(OPENSHAPE_OWN_OCCT_BIN)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

// Lower case, forward slashes: Windows paths compare case-insensitively.
std::wstring normalized(std::wstring path)
{
    std::replace(path.begin(), path.end(), L'\\', L'/');
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return path;
}

std::wstring fromUtf8(const char* text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(length, 1)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), length);
    result.resize(static_cast<size_t>(std::max(length - 1, 0)));
    return result;
}

std::string toUtf8(const std::wstring& text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(length, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<size_t>(std::max(length - 1, 0)));
    return result;
}

// Full paths of the OCCT toolkits (libTK*.dll) loaded into this process.
std::vector<std::wstring> loadedToolkits()
{
    std::vector<std::wstring> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL ok = Module32FirstW(snapshot, &entry); ok; ok = Module32NextW(snapshot, &entry)) {
        const std::wstring name = normalized(entry.szModule);
        if (name.rfind(L"libtk", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, L".dll") == 0)
            result.push_back(entry.szExePath);
    }
    CloseHandle(snapshot);
    return result;
}

} // namespace

TEST(OcctBuild, TestsLoadOpenShapesOwnOcct)
{
    const std::wstring expectedDir = normalized(fromUtf8(OPENSHAPE_OWN_OCCT_BIN)) + L"/";
    const std::vector<std::wstring> toolkits = loadedToolkits();
    // test_geometry links the modeling toolkits: at least TKernel, TKMath,
    // TKG3d, TKBRep and TKTopAlgo are loaded at start.
    ASSERT_GE(toolkits.size(), 5u) << "OCCT toolkits loaded: " << toolkits.size();
    bool kernel = false;
    for (const std::wstring& path : toolkits) {
        const std::wstring p = normalized(path);
        EXPECT_EQ(p.rfind(expectedDir, 0), 0u)
            << "an OCCT toolkit was loaded from outside OpenShape's own build (" << OPENSHAPE_OWN_OCCT_BIN
            << "): " << toUtf8(path);
        kernel = kernel || p == expectedDir + L"libtkernel.dll";
    }
    EXPECT_TRUE(kernel) << "libTKernel.dll was not loaded from " << OPENSHAPE_OWN_OCCT_BIN;
}

#endif
