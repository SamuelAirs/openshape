// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace os::doc {
class Document;
}

namespace os::cmd {

// A reversible document edit. Commands refer to document objects only by
// UUID (never by pointer), so they stay valid across undo/redo cycles in which
// objects are destroyed and recreated.
//
// Contract:
//   execute() performs the edit (first time and on redo). On failure it must
//             leave the document exactly as it found it.
//   undo()    reverts a successful execute().
class Command {
public:
    virtual ~Command() = default;
    virtual std::string label() const = 0;
    virtual Status execute(doc::Document& document) = 0;
    virtual void undo(doc::Document& document) = 0;
};

// Several commands applied as one undo step. If a child fails, the ones
// already applied are undone and the failure is returned.
class CompositeCommand final : public Command {
public:
    CompositeCommand(std::string label, std::vector<std::unique_ptr<Command>> children)
        : label_(std::move(label)), children_(std::move(children)) {}
    std::string label() const override { return label_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    std::string label_;
    std::vector<std::unique_ptr<Command>> children_;
};

class UndoStack {
public:
    explicit UndoStack(std::size_t maxDepth = 500) : maxDepth_(maxDepth) {}

    // Executes the command. On success it becomes the newest undo entry and
    // the redo history is discarded. On failure nothing is recorded.
    Status push(std::unique_ptr<Command> command, doc::Document& document);

    bool canUndo() const { return index_ > 0; }
    bool canRedo() const { return index_ < commands_.size(); }
    std::string undoLabel() const;
    std::string redoLabel() const;

    bool undo(doc::Document& document);
    Status redo(doc::Document& document);

    void clear();
    std::size_t size() const { return commands_.size(); }
    std::size_t index() const { return index_; }

    // Clean-state tracking for "unsaved changes".
    void setClean() { cleanIndex_ = static_cast<long long>(index_); }
    bool isClean() const { return cleanIndex_ == static_cast<long long>(index_); }
    // No state of this stack matches the saved file (e.g. a document restored
    // from a recovery copy): unsaved until the next setClean().
    void setModified() { cleanIndex_ = -1; }

    // Changes whenever the document changes through this stack (push, undo,
    // redo, clear); lets observers (recovery copies) notice edits cheaply.
    std::uint64_t revision() const { return revision_; }

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t index_ = 0; // number of applied commands
    std::size_t maxDepth_;
    long long cleanIndex_ = 0; // -1 when the clean state was discarded
    std::uint64_t revision_ = 0;
};

} // namespace os::cmd
