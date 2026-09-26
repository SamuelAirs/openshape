// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "io/Recovery.h"

#include <QtCore/QString>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QLockFile;

namespace os::ui {

// This app instance's recovery session (see io/Recovery.h): a QLockFile in the
// recovery folder says the session's owner is running, so another running
// instance never offers its copy. A lock left by a crashed app belongs to a
// process that no longer exists; QLockFile then treats it as stale.
class RecoverySession {
public:
    // Creates the folder and takes this session's lock.
    explicit RecoverySession(const QString& directory);
    // A clean exit: removes this session's copy and lock and lets go of the
    // other sessions' locks it took (their copies stay for the next start).
    ~RecoverySession();
    RecoverySession(const RecoverySession&) = delete;
    RecoverySession& operator=(const RecoverySession&) = delete;

    bool isLocked() const;
    const QString& directory() const { return directory_; }
    const std::string& session() const { return session_; }
    const io::RecoveryStore& store() const { return store_; }

    Status write(const doc::Document& document, const io::RecoveryInfo& info);
    bool hasCopy() const { return store_.exists(session_); }
    void removeCopy();

    // Copies left by sessions whose app is no longer running, newest first.
    // Their locks are taken and kept, so a second instance starting at the
    // same time does not offer them too. Also clears leftovers (half-written
    // files, locks of dead sessions without a copy).
    std::vector<io::RecoveryEntry> findOrphans();
    // Restoring: the orphan's copy becomes this session's (it stays until the
    // document is saved or discarded) and its lock is released.
    Status adopt(const std::string& orphan);
    // Deletes an orphan's copy and releases its lock.
    Status discard(const std::string& orphan);
    // "Decide later": releases the orphans' locks; their copies stay.
    void releaseOrphans();

private:
    bool isAlive(const std::string& session);

    QString directory_;
    io::RecoveryStore store_;
    std::string session_;
    std::unique_ptr<QLockFile> lock_;
    std::map<std::string, std::unique_ptr<QLockFile>> orphanLocks_;
};

} // namespace os::ui
