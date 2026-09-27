// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Body.h"
#include "document/Feature.h"
#include "sketch/Sketch.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace os::cmd {

// Creates a new body from a base feature (e.g. a box).
class CreateBodyCommand final : public Command {
public:
    CreateBodyCommand(std::string name, std::unique_ptr<doc::Feature> baseFeature);
    std::string label() const override { return "Create " + name_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    const Uuid& bodyId() const { return bodyId_; }

private:
    Uuid bodyId_ = Uuid::generate(); // fixed at construction: redo recreates the same identity
    std::string name_;
    std::unique_ptr<doc::Feature> prototype_;
};

// Appends (or inserts) a feature into a body's history. Fails, leaving the
// document untouched, if the feature itself cannot be computed or would
// change nothing (ErrorCode::NoEffect: a cut that misses the body).
class AddFeatureCommand final : public Command {
public:
    AddFeatureCommand(Uuid bodyId, std::unique_ptr<doc::Feature> feature, int index = -1);
    std::string label() const override;
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    const Uuid& featureId() const { return prototype_->id(); }

private:
    Uuid bodyId_;
    std::unique_ptr<doc::Feature> prototype_;
    int index_;
};

// Duplicates a body as an independent copy named "<name> copy": its history
// with fresh ids, plus what belongs to that history alone - the sketches its
// steps use (copied hidden) and the tool bodies its Combine steps consumed
// (copied hidden, recursively), all re-pointed at the copies. Editing the
// copy never changes the source, nor the other way round. A base step from
// files before independent copies (a Copy of another body, a SplitPiece of
// one) is replaced by that body's history (resolved the same way) and the
// step that made it (a Mirror keeping the image, a Move, a Split keeping the
// piece), so the copy does not follow that body either; one whose body
// cannot be built up to there stays as it is (shared). Copies whose history
// is the source's own take over its results (Body::adoptResults) instead of
// computing again. Refused, changing nothing, when the imported geometry it
// copies would take the document beyond Document::importedGeometryLimit (a
// project holding more cannot be saved).
class DuplicateBodyCommand final : public Command {
public:
    explicit DuplicateBodyCommand(Uuid sourceId) : sourceId_(sourceId) {}
    // A copy named `name` whose history ends with one more step, `lastStep`:
    // Mirror and Pattern copies (a Mirror step keeping only the image, a Move
    // step) and split-off pieces (a Split step keeping that piece). Fails,
    // leaving the document untouched, when that step fails or changes nothing.
    DuplicateBodyCommand(Uuid sourceId, std::string name, std::unique_ptr<doc::Feature> lastStep, std::string label);
    std::string label() const override { return label_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    // The new body's id, fixed at construction (redo recreates the same identity).
    const Uuid& copyId() const { return copyId_; }

    // The imported geometry (bytes of BRep text) one copy of `sourceId` would
    // add to the document: its Imported steps and those of everything copied
    // with it.
    static std::uint64_t importedBytesOfCopy(const doc::Document& document, const Uuid& sourceId);

private:
    Status plan(const doc::Document& document);
    std::uint64_t plannedImportedBytes() const;

    Uuid sourceId_;
    Uuid copyId_ = Uuid::generate();
    std::string name_;                        // empty: "<source name> copy"
    std::unique_ptr<doc::Feature> lastStep_;  // may be null
    std::string label_ = "Duplicate";
    bool planned_ = false;
    std::vector<sketch::Sketch> sketches_;           // copies, in document order
    std::vector<std::unique_ptr<doc::Body>> bodies_; // copies: consumed tools first, the copy last
    std::vector<Uuid> originals_;                    // the body each of bodies_ copies
    std::vector<bool> verbatim_;                     // its steps cloned one to one (results can be taken over)
};

// Fails with a plain message when `copies` copies of `sourceId` would take the
// document beyond its imported-geometry limit (Mirror and Pattern with
// separate bodies, Split into bodies: checked before anything changes).
Status checkImportedCopiesFit(const doc::Document& document, const Uuid& sourceId, std::size_t copies);

// One new, independent body per step in `lastSteps`, each a copy of the
// source body (its history cloned, DuplicateBodyCommand) ending in that step,
// named `names[i]`; as one undo step labelled `label`. Mirror and Pattern
// with separate bodies.
std::unique_ptr<Command> makeCopyBodiesCommand(const Uuid& sourceId, std::vector<std::unique_ptr<doc::Feature>> lastSteps,
                                               const std::vector<std::string>& names, const std::string& label);

// Splits a body that is in several separate pieces into bodies, as one undo
// step: a Split step keeps its largest piece, and every other piece becomes a
// new, independent body (named like new bodies): a copy of this body's
// history (DuplicateBodyCommand) ending in a Split step that keeps that piece.
// Editing a piece never changes the others. Fails when the body is in one
// piece. `pieceCount` (optional) receives the number of pieces.
Result<std::unique_ptr<Command>> makeSplitBodyCommand(const doc::Document& document, const Uuid& bodyId,
                                                      int* pieceCount = nullptr);

class DeleteBodyCommand final : public Command {
public:
    explicit DeleteBodyCommand(Uuid bodyId) : bodyId_(bodyId) {}
    std::string label() const override { return "Delete body"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid bodyId_;
    std::unique_ptr<doc::Body> removed_;
    int index_ = -1;
};

class DeleteFeatureCommand final : public Command {
public:
    explicit DeleteFeatureCommand(Uuid featureId) : featureId_(featureId) {}
    std::string label() const override { return "Delete step"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    Uuid bodyId_;
    std::unique_ptr<doc::Feature> removed_;
    int index_ = -1;
};

// Changes one scalar parameter of a feature. Downstream features recompute.
// With rejectIfFeatureFails, an edit that makes this feature fail, or change
// nothing (ErrorCode::NoEffect), is refused (used by direct manipulation);
// otherwise it is kept and the failure shown in the history (used by history
// editing).
class SetParameterCommand final : public Command {
public:
    SetParameterCommand(Uuid featureId, std::string key, double value, bool rejectIfFeatureFails = true);
    std::string label() const override { return "Change " + key_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    std::string key_;
    double newValue_;
    double oldValue_ = 0;
    bool rejectIfFeatureFails_;
};

// Changes a step's string parameter (a Text step's text), like SetParameterCommand.
class SetTextParameterCommand final : public Command {
public:
    SetTextParameterCommand(Uuid featureId, std::string key, std::string value, bool rejectIfFeatureFails = true);
    std::string label() const override { return "Change " + key_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    std::string key_;
    std::string newValue_;
    std::string oldValue_;
    bool rejectIfFeatureFails_;
};

class SetBodyVisibilityCommand final : public Command {
public:
    SetBodyVisibilityCommand(Uuid bodyId, bool visible) : bodyId_(bodyId), visible_(visible) {}
    std::string label() const override { return visible_ ? "Show body" : "Hide body"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid bodyId_;
    bool visible_;
    bool previous_ = true;
};

// Suppresses or restores a feature (a suppressed feature passes its input through).
class SetFeatureSuppressedCommand final : public Command {
public:
    SetFeatureSuppressedCommand(Uuid featureId, bool suppressed) : featureId_(featureId), suppressed_(suppressed) {}
    std::string label() const override { return suppressed_ ? "Suppress step" : "Restore step"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid featureId_;
    bool suppressed_;
    bool previous_ = false;
};

// Adds a new sketch.
class CreateSketchCommand final : public Command {
public:
    explicit CreateSketchCommand(sketch::Sketch sketch) : sketch_(std::move(sketch)) {}
    std::string label() const override { return "New sketch"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;
    const Uuid& sketchId() const { return sketch_.id(); }

private:
    sketch::Sketch sketch_;
};

// Replaces a sketch's content with a new state (one drawing step, a new
// constraint, an edited dimension...). Stores full before/after snapshots:
// sketches are small, and snapshots make undo exact.
class EditSketchCommand final : public Command {
public:
    EditSketchCommand(sketch::Sketch after, std::string label) : after_(std::move(after)), label_(std::move(label)) {}
    std::string label() const override { return label_; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    sketch::Sketch after_;
    std::optional<sketch::Sketch> before_;
    std::string label_;
};

// Deletes a sketch. Refused while features still use it.
class DeleteSketchCommand final : public Command {
public:
    explicit DeleteSketchCommand(Uuid sketchId) : sketchId_(sketchId) {}
    std::string label() const override { return "Delete sketch"; }
    Status execute(doc::Document& document) override;
    void undo(doc::Document& document) override;

private:
    Uuid sketchId_;
    std::unique_ptr<sketch::Sketch> removed_;
    int index_ = -1;
};

} // namespace os::cmd
