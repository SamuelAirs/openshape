// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/IncomingFiles.h"

#include "ui/AppSettings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <algorithm>

namespace os::ui {

namespace {

// A path as it can be compared: symbolic links resolved when the file or
// folder exists (iOS hands out /private/var/... for what QStandardPaths
// calls /var/...), else cleaned and absolute.
QString comparablePath(const QString& path)
{
    const QFileInfo info(path);
    QString result = info.canonicalFilePath();
    if (result.isEmpty())
        result = QDir::cleanPath(info.absoluteFilePath());
    // /var is a link to /private/var on iOS (and macOS): the same folder,
    // whichever way the path came, even for a file that is gone.
    if (result.startsWith(QLatin1String("/private/var/")))
        result.remove(0, int(sizeof("/private") - 1));
    return result;
}

// Moves a file within the sandbox (out of the Inbox); copies and removes
// when a rename is not possible (another volume).
bool moveFile(const QString& from, const QString& to)
{
    if (QFile::rename(from, to))
        return true;
    if (!QFile::copy(from, to))
        return false;
    QFile::remove(from);
    return true;
}

// The copy is the user's file now: writable, whatever the source was (the
// Inbox's copies are read-only for the app).
void makeWritable(const QString& path)
{
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ReadUser | QFileDevice::WriteUser);
}

// Where a project from elsewhere goes in the app folder: the first name
// that is free, or that holds this very project already (`alreadyThere`).
QString projectTarget(const QString& absolute, const QString& appFolder, bool* alreadyThere)
{
    QString base = projectFileBaseName(QFileInfo(absolute).completeBaseName());
    if (base.isEmpty())
        base = QStringLiteral("Untitled");
    *alreadyThere = false;
    for (int n = 1; n < 1000; ++n) {
        const QString candidate = appFolder + QLatin1Char('/') + (n == 1 ? base : base + QLatin1Char(' ') + QString::number(n))
                                + QStringLiteral(".openshape");
        if (!QFileInfo::exists(candidate))
            return candidate;
        if (sameFileContents(absolute, candidate)) {
            *alreadyThere = true;
            return candidate;
        }
    }
    return {};
}

bool fromAnInbox(const QString& absolute, const IncomingPlaces& places)
{
    return std::any_of(places.inboxes.begin(), places.inboxes.end(),
                       [&](const QString& inbox) { return isInsideFolder(absolute, inbox); });
}

} // namespace

IncomingKind incomingKind(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("openshape"))
        return IncomingKind::Project;
    if (suffix == QLatin1String("step") || suffix == QLatin1String("stp"))
        return IncomingKind::Step;
    return IncomingKind::Unsupported;
}

bool isInsideFolder(const QString& path, const QString& folder)
{
    if (path.isEmpty() || folder.isEmpty())
        return false;
    const QString p = comparablePath(path);
    const QString f = comparablePath(folder);
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (p.compare(f, cs) == 0)
        return true;
    const QString prefix = f.endsWith(QLatin1Char('/')) ? f : f + QLatin1Char('/');
    return p.startsWith(prefix, cs);
}

bool sameFileContents(const QString& a, const QString& b)
{
    QFile first(a);
    QFile second(b);
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly))
        return false;
    if (first.size() != second.size())
        return false;
    constexpr qint64 kChunk = 1 << 20;
    while (!first.atEnd()) {
        const QByteArray x = first.read(kChunk);
        const QByteArray y = second.read(kChunk);
        if (x != y || x.isEmpty())
            return false;
    }
    return second.atEnd();
}

StagedFile stageIncomingFile(const QString& source, const IncomingPlaces& places, IncomingKind kind)
{
    StagedFile staged;
    const QFileInfo info(source);
    const QString name = info.fileName();
    staged.kind = kind == IncomingKind::Unsupported ? incomingKind(name) : kind;
    if (staged.kind == IncomingKind::Unsupported) {
        staged.error = QStringLiteral("FoldForm opens projects (.openshape) and STEP files (.step, .stp); "
                                      "“%1” is neither.")
                           .arg(name);
        return staged;
    }
    if (!info.isFile()) {
        staged.error = QStringLiteral("“%1” could not be read.").arg(name);
        return staged;
    }
    const QString absolute = info.absoluteFilePath();
    if (places.appFolder.isEmpty()) {
        staged.path = absolute;
        return staged;
    }
    const bool fromInbox = fromAnInbox(absolute, places);
    if (!fromInbox && isInsideFolder(absolute, places.appFolder)) {
        staged.path = absolute;
        return staged;
    }
    if (staged.kind == IncomingKind::Step && isInsideFolder(absolute, places.stagingFolder)) {
        staged.path = absolute; // already a scratch copy
        staged.temporary = true;
        return staged;
    }

    if (staged.kind == IncomingKind::Project) {
        QDir().mkpath(places.appFolder);
        bool alreadyThere = false;
        const QString target = projectTarget(absolute, places.appFolder, &alreadyThere);
        bool ok = !target.isEmpty();
        if (ok && alreadyThere) {
            if (fromInbox)
                QFile::remove(absolute);
        } else if (ok) {
            ok = fromInbox ? moveFile(absolute, target) : QFile::copy(absolute, target);
            if (ok)
                makeWritable(target);
        }
        if (!ok) {
            staged.error = QStringLiteral("“%1” could not be copied into FoldForm’s folder.").arg(name);
            return staged;
        }
        staged.path = target;
        staged.copied = true;
        staged.created = !alreadyThere;
    } else {
        if (places.stagingFolder.isEmpty() || !QDir().mkpath(places.stagingFolder)) {
            staged.error = QStringLiteral("“%1” could not be read: there is no room for a working copy.").arg(name);
            return staged;
        }
        const QString target = places.stagingFolder + QLatin1Char('/') + name;
        QFile::remove(target); // a copy left from before
        const bool ok = fromInbox ? moveFile(absolute, target) : QFile::copy(absolute, target);
        if (!ok) {
            staged.error = QStringLiteral("“%1” could not be read.").arg(name);
            return staged;
        }
        staged.path = target;
        staged.temporary = true;
    }
    if (fromInbox) {
        // iOS's copy has been moved out; an empty Inbox would show up in
        // OpenShape's folder in the Files app.
        const QDir inbox = info.absoluteDir();
        if (inbox.isEmpty())
            QDir().rmdir(inbox.absolutePath());
    }
    return staged;
}

bool stagingAddsProject(const QString& source, const IncomingPlaces& places)
{
    const QFileInfo info(source);
    if (places.appFolder.isEmpty() || incomingKind(info.fileName()) != IncomingKind::Project || !info.isFile())
        return false;
    const QString absolute = info.absoluteFilePath();
    if (!fromAnInbox(absolute, places) && isInsideFolder(absolute, places.appFolder))
        return false; // opened where it is
    bool alreadyThere = false;
    const QString target = projectTarget(absolute, places.appFolder, &alreadyThere);
    return !target.isEmpty() && !alreadyThere;
}

} // namespace os::ui
