#include "commands/Command.h"

#include "core/Log.h"

namespace os::cmd {

Status CompositeCommand::execute(doc::Document& document)
{
    for (std::size_t i = 0; i < children_.size(); ++i) {
        Status status = children_[i]->execute(document);
        if (!status) {
            for (std::size_t j = i; j-- > 0;)
                children_[j]->undo(document);
            return status;
        }
    }
    return okStatus();
}

void CompositeCommand::undo(doc::Document& document)
{
    for (std::size_t j = children_.size(); j-- > 0;)
        children_[j]->undo(document);
}

Status UndoStack::push(std::unique_ptr<Command> command, doc::Document& document)
{
    if (!command)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing to do.", "UndoStack::push(nullptr)");
    const std::string label = command->label();
    Status status = command->execute(document);
    if (!status) {
        OS_LOG(Info, Command) << "'" << label << "' failed: " << status.developerMessage();
        return status;
    }
    // Discard redo history; the clean state is unreachable if it lived there.
    if (cleanIndex_ > static_cast<long long>(index_))
        cleanIndex_ = -1;
    commands_.resize(index_);
    commands_.push_back(std::move(command));
    ++index_;
    if (commands_.size() > maxDepth_) {
        commands_.erase(commands_.begin());
        --index_;
        if (cleanIndex_ >= 0)
            --cleanIndex_;
    }
    OS_LOG(Debug, Command) << "executed '" << label << "'";
    return status;
}

std::string UndoStack::undoLabel() const
{
    return canUndo() ? commands_[index_ - 1]->label() : std::string();
}

std::string UndoStack::redoLabel() const
{
    return canRedo() ? commands_[index_]->label() : std::string();
}

bool UndoStack::undo(doc::Document& document)
{
    if (!canUndo())
        return false;
    --index_;
    commands_[index_]->undo(document);
    OS_LOG(Debug, Command) << "undid '" << commands_[index_]->label() << "'";
    return true;
}

Status UndoStack::redo(doc::Document& document)
{
    if (!canRedo())
        return Status::failure(ErrorCode::InvalidArgument, "Nothing to redo.", "redo with empty redo stack");
    Status status = commands_[index_]->execute(document);
    if (!status) {
        // Should not happen for deterministic commands; drop the broken redo tail.
        OS_LOG(Error, Command) << "redo of '" << commands_[index_]->label() << "' failed: " << status.developerMessage();
        commands_.resize(index_);
        return status;
    }
    OS_LOG(Debug, Command) << "redid '" << commands_[index_]->label() << "'";
    ++index_;
    return status;
}

void UndoStack::clear()
{
    commands_.clear();
    index_ = 0;
    cleanIndex_ = 0;
}

} // namespace os::cmd
