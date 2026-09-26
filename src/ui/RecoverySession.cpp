// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/RecoverySession.h"

#include "core/Log.h"
#include "core/Uuid.h"

#include <QtCore/QDir>
#include <QtCore/QLockFile>

#include <algorithm>

namespace os::ui {

namespace {
std::filesystem::path toPath(const QString& dir)
{
    return std::filesystem::path(dir.toStdWString());
}

QString lockPath(const io::RecoveryStore& store, const std::string& session)
{
    return QString::fromStdWString(store.lockFile(session).wstring());
}
} // namespace

RecoverySession::RecoverySession(const QString& directory)
    : directory_(directory), store_(toPath(directory)), session_(Uuid::generate().toString())
{
    QDir().mkpath(directory_);
    lock_ = std::make_unique<QLockFile>(lockPath(store_, session_));
    if (!lock_->tryLock(0))
        OS_LOG(Warning, File) << "recovery: cannot lock session " << session_ << " in " << directory_.toStdString()
                              << " (error " << int(lock_->error()) << "); recovery copies may be offered to another instance";
}

RecoverySession::~RecoverySession()
{
    removeCopy();
    releaseOrphans();
    lock_.reset(); // QLockFile's destructor unlocks, which removes the lock file
}

bool RecoverySession::isLocked() const
{
    return lock_ && lock_->isLocked();
}

Status RecoverySession::write(const doc::Document& document, const io::RecoveryInfo& info)
{
    return store_.write(session_, document, info);
}

void RecoverySession::removeCopy()
{
    if (const Status s = store_.remove(session_); !s)
        OS_LOG(Warning, File) << "recovery: " << s.developerMessage();
}

bool RecoverySession::isAlive(const std::string& session)
{
    if (session == session_)
        return true;
    if (orphanLocks_.contains(session))
        return false; // we hold its lock: its owner is gone
    auto lock = std::make_unique<QLockFile>(lockPath(store_, session));
    if (lock->tryLock(0)) {
        orphanLocks_.emplace(session, std::move(lock));
        return false;
    }
    // Held by a running instance (or unreadable: don't offer what may be in use).
    return true;
}

std::vector<io::RecoveryEntry> RecoverySession::findOrphans()
{
    const auto alive = [this](const std::string& s) { return isAlive(s); };
    const int removed = store_.removeLeftovers(alive, session_);
    if (removed > 0)
        OS_LOG(Info, File) << "recovery: removed " << removed << " leftover file(s)";
    std::vector<io::RecoveryEntry> orphans = store_.orphaned(alive, session_);
    // Locks of crashed sessions that had nothing unsaved: release (removes them).
    const QStringList locks = QDir(directory_).entryList({QStringLiteral("*.lock")}, QDir::Files);
    for (const QString& file : locks) {
        const std::string session = file.chopped(5).toStdString();
        if (!io::isRecoverySessionName(session) || session == session_ || store_.exists(session))
            continue;
        if (!isAlive(session))
            orphanLocks_.erase(session);
    }
    for (auto it = orphanLocks_.begin(); it != orphanLocks_.end();) {
        const bool offered = std::any_of(orphans.begin(), orphans.end(), [&](const io::RecoveryEntry& e) { return e.session == it->first; });
        it = offered ? std::next(it) : orphanLocks_.erase(it);
    }
    if (!orphans.empty())
        OS_LOG(Info, File) << "recovery: " << orphans.size() << (orphans.size() == 1 ? " copy" : " copies")
                           << " from an earlier run can be restored";
    return orphans;
}

Status RecoverySession::adopt(const std::string& orphan)
{
    if (orphan == session_ || isAlive(orphan))
        return Status::failure(ErrorCode::InvalidArgument, "That work is open in another OpenShape window.",
                               "adopt a live session " + orphan);
    Status status = store_.adopt(orphan, session_);
    if (status)
        orphanLocks_.erase(orphan);
    return status;
}

Status RecoverySession::discard(const std::string& orphan)
{
    if (orphan == session_ || isAlive(orphan))
        return Status::failure(ErrorCode::InvalidArgument, "That work is open in another OpenShape window.",
                               "discard a live session " + orphan);
    Status status = store_.remove(orphan);
    if (status)
        orphanLocks_.erase(orphan);
    return status;
}

void RecoverySession::releaseOrphans()
{
    orphanLocks_.clear();
}

} // namespace os::ui
