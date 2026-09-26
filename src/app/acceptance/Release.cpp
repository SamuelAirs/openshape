// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a release shows about itself: the version (CMake's PROJECT_VERSION)
// in the About card, and on Windows the executable's icon and version
// resource (src/app/openshape.rc.in), checked on the running program, so a
// packaged or installed copy is checked too.

#include "app/AcceptanceRunner.h"
#include "core/Version.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <vector>
#endif

namespace os::app {
namespace {

#if defined(Q_OS_WIN)
// The running executable's ProductVersion string (VERSIONINFO), or empty.
QString exeProductVersion()
{
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
    if (size == 0)
        return {};
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, data.data()))
        return {};
    wchar_t* value = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\StringFileInfo\\040904B0\\ProductVersion", reinterpret_cast<void**>(&value), &length) || !value)
        return {};
    return QString::fromWCharArray(value);
}

// How many icons the running executable carries (Explorer shows the first).
int exeIconCount()
{
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    return static_cast<int>(ExtractIconExW(exe.c_str(), -1, nullptr, nullptr, 0));
}
#endif

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    const QString version = QString::fromLatin1(kAppVersion);
    return {
        [&r, version] {
            r.check(QCoreApplication::applicationVersion() == version, "release: the application version is CMake's",
                    QCoreApplication::applicationVersion());
#if defined(Q_OS_WIN)
            r.check(exeProductVersion() == version, "release: OpenShape.exe's version resource matches", exeProductVersion());
            r.check(exeIconCount() >= 1, "release: OpenShape.exe carries the app icon", QString::number(exeIconCount()));
#endif
            r.check(r.clickItem(QStringLiteral("fileMenuButton")), "release: File menu");
        },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("aboutMenuItem")), "release: About OpenShape"); },
        [] {}, [] {},
        [&r, version] {
            QQuickItem* title = r.findItem(QStringLiteral("aboutVersion"));
            const QString text = title ? title->property("text").toString() : QString();
            r.check(title && title->isVisible() && text == QStringLiteral("OpenShape ") + version,
                    "release: the About card names the version", text);
            // Only the Windows package carries the license files
            // (scripts/package-windows.sh); elsewhere the card links the list
            // in the repository and must not name files that are not there.
            QQuickItem* files = r.findItem(QStringLiteral("aboutLicenseFiles"));
            const QString filesText = files ? files->property("text").toString() : QString();
#if defined(Q_OS_WIN)
            r.check(filesText.contains(QStringLiteral("THIRD_PARTY_LICENSES.txt"))
                        && filesText.contains(QStringLiteral("OpenShape.exe")),
                    "release: the About card points to the bundled license texts", filesText);
#else
            r.check(filesText.contains(QStringLiteral("THIRD_PARTY.md"))
                        && filesText.contains(QStringLiteral("github.com/SamuelAirs/openshape"))
                        && !filesText.contains(QStringLiteral("THIRD_PARTY_LICENSES.txt"))
                        && !filesText.contains(QStringLiteral("OpenShape.exe")),
                    "release: the About card links the license list in the repository", filesText);
#endif
            r.screenshot(QStringLiteral("release_about"));
            r.key(Qt::Key_Escape);
        },
        [] {},
        [&r] {
            QQuickItem* title = r.findItem(QStringLiteral("aboutVersion"));
            r.check(title && !title->isVisible(), "release: Esc closes the About card");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("release"), 95, steps});

} // namespace
} // namespace os::app
