// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace os::ui {

// Files that reach OpenShape from outside on an iPhone or iPad: "Open in
// OpenShape" or sharing to it from the Files app or Mail, a project tapped
// in the Files app, and a file picked in the system document picker (Open,
// Import STEP) outside OpenShape's folder. Such a file may lie outside the
// app's sandbox (iCloud Drive, another app's folder: readable only through
// the security-scoped access iOS granted, which Qt's own file engine starts
// and stops around each QFile use, but OpenCASCADE and the project reader
// open files themselves) or be the system's copy in the app's Inbox. So
// OpenShape first brings it inside, reading it only through QFile. Qt Core
// only (tests/test_uistate).

enum class IncomingKind { Project, Step, Unsupported };

// By the file name's extension, in any case: .openshape; .step or .stp.
IncomingKind incomingKind(const QString& fileName);

struct IncomingPlaces {
    // OpenShape's folder (Documents on iOS, which the Files app shows);
    // empty on the desktop, where files are used where they are.
    QString appFolder;
    // A private scratch folder for STEP files read from elsewhere (removed
    // again once imported).
    QString stagingFolder;
    // Folders in which iOS puts its own copy of a file sent to the app
    // (<Documents>/Inbox): such a copy is moved out, not copied.
    QStringList inboxes;
};

struct StagedFile {
    IncomingKind kind = IncomingKind::Unsupported;
    QString path;           // the file to open or import; empty on failure
    bool temporary = false; // a scratch copy: remove it once it has been read
    bool copied = false;    // a project copied or moved into the app folder
    QString error;          // why not, in plain words (when path is empty)
};

// Where to read `source` from:
// - no app folder (the desktop): the file itself;
// - a file in the app folder (not in an inbox): the file itself;
// - a project from elsewhere: copied into the app folder under its own name
//   (made a safe file name), or "<name> 2", "<name> 3", ... when a different
//   project has that name; a project there with the same contents is used
//   instead of a new copy (opening the same file twice makes one copy);
// - a STEP file from elsewhere: copied into the staging folder (temporary);
// - from an inbox: moved rather than copied (and an empty inbox removed).
// `kind` Unsupported means: by the file's extension.
StagedFile stageIncomingFile(const QString& source, const IncomingPlaces& places,
                             IncomingKind kind = IncomingKind::Unsupported);

// Whether `path` is `folder` or inside it (compared as cleaned absolute
// paths, case-insensitively on Windows).
bool isInsideFolder(const QString& path, const QString& folder);

// Whether the two files hold the same bytes (false if either is unreadable).
bool sameFileContents(const QString& a, const QString& b);

} // namespace os::ui
