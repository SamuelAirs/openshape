// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "commands/DocumentCommands.h"

#include "document/Document.h"
#include "geometry/Modeling.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace os::cmd {

namespace {

Status failureFrom(const doc::FeatureState& state)
{
    return Status::failure(state.error == ErrorCode::None ? ErrorCode::KernelFailure : state.error,
                           state.userMessage.empty() ? "The operation failed." : state.userMessage, state.developerMessage);
}

Status missingBody()
{
    return Status::failure(ErrorCode::InvalidReference, "The body no longer exists.", "command: unknown body");
}

Status missingFeature()
{
    return Status::failure(ErrorCode::InvalidReference, "That step no longer exists.", "command: unknown feature");
}

} // namespace

// ---- CreateBody -----------------------------------------------------------------

CreateBodyCommand::CreateBodyCommand(std::string name, std::unique_ptr<doc::Feature> baseFeature)
    : name_(std::move(name)), prototype_(std::move(baseFeature))
{
}

Status CreateBodyCommand::execute(doc::Document& document)
{
    if (!prototype_ || !prototype_->isBaseFeature())
        return Status::failure(ErrorCode::InvalidArgument, "Unable to create the body.", "CreateBody without base feature");
    auto body = std::make_unique<doc::Body>(bodyId_);
    body->setName(name_);
    body->insertFeature(prototype_->clone(), 0);
    doc::Body& added = document.addBody(std::move(body));
    if (added.state(0).status != doc::FeatureStatus::Ok) {
        Status failure = failureFrom(added.state(0));
        document.removeBody(bodyId_);
        return failure;
    }
    return okStatus();
}

void CreateBodyCommand::undo(doc::Document& document)
{
    document.removeBody(bodyId_);
}

// ---- AddFeature -----------------------------------------------------------------

AddFeatureCommand::AddFeatureCommand(Uuid bodyId, std::unique_ptr<doc::Feature> feature, int index)
    : bodyId_(bodyId), prototype_(std::move(feature)), index_(index)
{
}

std::string AddFeatureCommand::label() const
{
    if (!prototype_->name().empty())
        return prototype_->name(); // "Align", "Rotate": more telling than the kind
    switch (prototype_->kind()) {
    case doc::FeatureKind::PushPull: return "Push/Pull";
    case doc::FeatureKind::Fillet: return "Fillet";
    case doc::FeatureKind::Chamfer: return "Chamfer";
    case doc::FeatureKind::Box: return "Add box";
    case doc::FeatureKind::Extrude: return "Extrude";
    case doc::FeatureKind::Shell: return "Shell";
    case doc::FeatureKind::Move: return "Move";
    case doc::FeatureKind::Combine: return "Combine";
    case doc::FeatureKind::Revolve: return "Revolve";
    case doc::FeatureKind::Hole: return "Hole";
    case doc::FeatureKind::Mirror: return "Mirror";
    case doc::FeatureKind::Pattern: return "Pattern";
    case doc::FeatureKind::DeleteFaces: return "Delete faces";
    case doc::FeatureKind::OffsetFace: return "Offset face";
    case doc::FeatureKind::Split: return "Split into bodies";
    case doc::FeatureKind::SplitPiece: return "Split piece";
    case doc::FeatureKind::Copy: return "Copy";
    case doc::FeatureKind::Holes: return "Hole";
    }
    return "Add step";
}

Status AddFeatureCommand::execute(doc::Document& document)
{
    if (!document.body(bodyId_))
        return missingBody();
    const doc::FeatureState& state = document.insertFeature(bodyId_, prototype_->clone(), index_);
    // A new step that would change nothing is refused (the recompute lets it
    // through as a warning so that existing histories keep building).
    if (state.status != doc::FeatureStatus::Ok || state.error == ErrorCode::NoEffect) {
        Status failure = failureFrom(state);
        document.removeFeature(prototype_->id());
        return failure;
    }
    return okStatus();
}

void AddFeatureCommand::undo(doc::Document& document)
{
    document.removeFeature(prototype_->id());
}

// ---- DuplicateBody --------------------------------------------------------------

Status DuplicateBodyCommand::plan(const doc::Document& document)
{
    const doc::Body* source = document.body(sourceId_);
    if (!source)
        return missingBody();
    auto contains = [](const auto& list, const auto& value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    };

    // Bodies to copy, dependencies first: the source and, recursively, the
    // tool bodies its Combine steps consumed - decided by the kind of
    // reference, never by visibility (a tool shown again is still consumed; a
    // copy's source that is merely hidden is still shared).
    std::vector<const doc::Body*> bodies;
    std::vector<Uuid> seen;
    std::function<void(const doc::Body&)> visit = [&](const doc::Body& body) {
        seen.push_back(body.id());
        for (const auto& f : body.features())
            if (const auto* combine = dynamic_cast<const doc::CombineFeature*>(f.get()))
                if (const doc::Body* tool = document.body(combine->toolBody); tool && !contains(seen, tool->id()))
                    visit(*tool);
        bodies.push_back(&body);
    };
    visit(*source);

    // The sketches their steps use, in document order.
    std::vector<Uuid> used;
    for (const doc::Body* b : bodies)
        for (const auto& f : b->features())
            for (const Uuid& dep : f->dependencies())
                if (document.sketch(dep) && !contains(used, dep))
                    used.push_back(dep);
    std::vector<const sketch::Sketch*> sketches;
    for (const auto& s : document.sketches())
        if (contains(used, s->id()))
            sketches.push_back(s.get());

    // Fresh ids for everything copied; steps first get theirs, because a step
    // may refer to a step of another copied body.
    std::map<Uuid, Uuid> copies;
    for (const doc::Body* b : bodies)
        copies[b->id()] = b->id() == sourceId_ ? copyId_ : Uuid::generate();
    for (const sketch::Sketch* s : sketches)
        copies[s->id()] = Uuid::generate();
    std::vector<std::vector<std::unique_ptr<doc::Feature>>> features(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i)
        for (const auto& f : bodies[i]->features()) {
            auto copy = f->cloneWithNewId();
            copies[f->id()] = copy->id();
            features[i].push_back(std::move(copy));
        }
    auto mapped = [&](const Uuid& id) {
        const auto it = copies.find(id);
        return it == copies.end() ? id : it->second;
    };

    std::vector<std::string> names;
    auto uniqueName = [&](const std::string& base, bool body) {
        for (int n = 1;; ++n) {
            const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
            const std::string free = body ? document.uniqueBodyName(candidate) : document.uniqueSketchName(candidate);
            if (free == candidate && !contains(names, candidate)) {
                names.push_back(candidate);
                return candidate;
            }
        }
    };

    for (const sketch::Sketch* s : sketches) {
        sketch::Sketch copy = s->copyWithId(copies.at(s->id()));
        copy.setName(uniqueName(s->name() + " copy", false));
        // The copy's sketches exist for its history; drawn, they would sit
        // exactly on the source's.
        copy.setVisible(false);
        if (copy.hostBody())
            copy.setHostBody(mapped(*copy.hostBody()));
        if (auto attachment = copy.attachment()) {
            attachment->body = mapped(attachment->body);
            attachment->feature = mapped(attachment->feature);
            copy.setAttachment(attachment);
        }
        sketches_.push_back(std::move(copy));
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const doc::Body& b = *bodies[i];
        auto copy = std::make_unique<doc::Body>(copies.at(b.id()));
        copy->setName(uniqueName(b.name() + " copy", true));
        copy->setVisible(b.id() == sourceId_); // copied tools are consumed by the copy: hidden
        int index = 0;
        for (auto& f : features[i]) {
            f->remapReferences(copies);
            copy->insertFeature(std::move(f), index++);
        }
        bodies_.push_back(std::move(copy));
    }
    return okStatus();
}

Status DuplicateBodyCommand::execute(doc::Document& document)
{
    if (!planned_) {
        if (Status s = plan(document); !s)
            return s;
        planned_ = true;
    }
    for (const auto& s : sketches_)
        document.addSketch(std::make_unique<sketch::Sketch>(s));
    for (const auto& b : bodies_)
        document.addBody(std::make_unique<doc::Body>(*b));
    const doc::Body* source = document.body(sourceId_);
    const doc::Body* copy = document.body(copyId_);
    if (!copy || (source && !source->shape().isNull() && copy->shape().isNull())) {
        undo(document);
        return Status::failure(ErrorCode::KernelFailure, "Unable to duplicate this body.", "duplicate: the copy did not compute");
    }
    return okStatus();
}

void DuplicateBodyCommand::undo(doc::Document& document)
{
    for (auto it = bodies_.rbegin(); it != bodies_.rend(); ++it)
        document.removeBody((*it)->id());
    for (auto it = sketches_.rbegin(); it != sketches_.rend(); ++it)
        document.removeSketch(it->id());
}

// ---- Split into bodies ------------------------------------------------------------

Result<std::unique_ptr<Command>> makeSplitBodyCommand(const doc::Document& document, const Uuid& bodyId)
{
    using R = Result<std::unique_ptr<Command>>;
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return R::failureFrom(missingBody());
    std::vector<geom::Shape> solids = geom::solids(body->shape());
    if (solids.size() < 2)
        return R::failure(ErrorCode::InvalidArgument, "This body is in one piece; there is nothing to split.",
                          "split: " + std::to_string(solids.size()) + " solid(s)");
    // The body keeps its largest piece; the others follow by size, then by
    // position, so the order is the same every time.
    std::vector<geom::SolidSignature> pieces;
    for (const geom::Shape& s : solids)
        pieces.push_back(geom::solidSignature(s));
    std::sort(pieces.begin(), pieces.end(), [](const geom::SolidSignature& a, const geom::SolidSignature& b) {
        if (std::abs(a.volume - b.volume) > 1e-6 * std::max(a.volume, b.volume))
            return a.volume > b.volume;
        if (std::abs(a.centroid.x - b.centroid.x) > 1e-9)
            return a.centroid.x < b.centroid.x;
        if (std::abs(a.centroid.y - b.centroid.y) > 1e-9)
            return a.centroid.y < b.centroid.y;
        return a.centroid.z < b.centroid.z;
    });

    auto split = std::make_unique<doc::SplitFeature>();
    split->pieces = pieces;
    const Uuid splitId = split->id();
    std::vector<std::unique_ptr<Command>> steps;
    steps.push_back(std::make_unique<AddFeatureCommand>(bodyId, std::move(split)));
    const std::vector<std::string> names = document.nextBodyNames(pieces.size() - 1);
    for (std::size_t k = 1; k < pieces.size(); ++k) {
        auto piece = std::make_unique<doc::SplitPieceFeature>();
        piece->sourceBody = bodyId;
        piece->splitFeature = splitId;
        piece->piece = static_cast<int>(k);
        steps.push_back(std::make_unique<CreateBodyCommand>(names[k - 1], std::move(piece)));
    }
    return R::success(std::make_unique<CompositeCommand>("Split into bodies", std::move(steps)));
}

// ---- DeleteBody -----------------------------------------------------------------

Status DeleteBodyCommand::execute(doc::Document& document)
{
    removed_ = document.removeBody(bodyId_, &index_);
    return removed_ ? okStatus() : missingBody();
}

void DeleteBodyCommand::undo(doc::Document& document)
{
    if (removed_)
        document.addBody(std::move(removed_), index_);
}

// ---- DeleteFeature --------------------------------------------------------------

Status DeleteFeatureCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    if (!body)
        return missingFeature();
    if (body->featureIndex(featureId_) == 0)
        return Status::failure(ErrorCode::InvalidArgument, "The first step of a body cannot be deleted. Delete the body instead.",
                               "delete base feature");
    bodyId_ = body->id();
    removed_ = document.removeFeature(featureId_, &index_);
    return removed_ ? okStatus() : missingFeature();
}

void DeleteFeatureCommand::undo(doc::Document& document)
{
    if (removed_)
        document.insertFeature(bodyId_, std::move(removed_), index_);
}

// ---- SetParameter ---------------------------------------------------------------

SetParameterCommand::SetParameterCommand(Uuid featureId, std::string key, double value, bool rejectIfFeatureFails)
    : featureId_(featureId), key_(std::move(key)), newValue_(value), rejectIfFeatureFails_(rejectIfFeatureFails)
{
}

Status SetParameterCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return missingFeature();
    const auto old = feature->parameter(key_);
    if (!old)
        return Status::failure(ErrorCode::InvalidArgument, "This value cannot be edited.", "unknown parameter " + key_);
    oldValue_ = *old;
    if (Status s = feature->setParameter(key_, newValue_); !s)
        return s;
    document.featureChanged(featureId_);

    const doc::FeatureState& state = body->state(body->featureIndex(featureId_));
    if (rejectIfFeatureFails_ && (state.status == doc::FeatureStatus::Failed || state.error == ErrorCode::NoEffect)) {
        Status failure = failureFrom(state);
        (void)feature->setParameter(key_, oldValue_);
        document.featureChanged(featureId_);
        return failure;
    }
    return okStatus();
}

void SetParameterCommand::undo(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return;
    (void)feature->setParameter(key_, oldValue_);
    document.featureChanged(featureId_);
}

// ---- SetBodyVisibility ----------------------------------------------------------

Status SetBodyVisibilityCommand::execute(doc::Document& document)
{
    const doc::Body* body = document.body(bodyId_);
    if (!body)
        return missingBody();
    previous_ = body->isVisible();
    document.setBodyVisible(bodyId_, visible_);
    return okStatus();
}

void SetBodyVisibilityCommand::undo(doc::Document& document)
{
    document.setBodyVisible(bodyId_, previous_);
}

// ---- SetFeatureSuppressed --------------------------------------------------------

Status SetFeatureSuppressedCommand::execute(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    doc::Feature* feature = body ? body->feature(featureId_) : nullptr;
    if (!feature)
        return missingFeature();
    if (feature->isBaseFeature() && suppressed_)
        return Status::failure(ErrorCode::InvalidArgument, "The first step of a body cannot be suppressed.",
                               "suppress base feature");
    previous_ = feature->isSuppressed();
    feature->setSuppressed(suppressed_);
    document.featureChanged(featureId_);
    return okStatus();
}

void SetFeatureSuppressedCommand::undo(doc::Document& document)
{
    doc::Body* body = document.bodyOfFeature(featureId_);
    if (doc::Feature* feature = body ? body->feature(featureId_) : nullptr) {
        feature->setSuppressed(previous_);
        document.featureChanged(featureId_);
    }
}

// ---- Sketches ---------------------------------------------------------------------

Status CreateSketchCommand::execute(doc::Document& document)
{
    if (document.sketch(sketch_.id()))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to create the sketch.", "sketch id already exists");
    document.addSketch(std::make_unique<sketch::Sketch>(sketch_));
    return okStatus();
}

void CreateSketchCommand::undo(doc::Document& document)
{
    document.removeSketch(sketch_.id());
}

Status EditSketchCommand::execute(doc::Document& document)
{
    const sketch::Sketch* current = document.sketch(after_.id());
    if (!current)
        return Status::failure(ErrorCode::InvalidReference, "The sketch no longer exists.", "EditSketch: unknown sketch");
    before_ = *current;
    document.replaceSketch(after_);
    return okStatus();
}

void EditSketchCommand::undo(doc::Document& document)
{
    if (before_)
        document.replaceSketch(*before_);
}

Status DeleteSketchCommand::execute(doc::Document& document)
{
    if (!document.dependentFeatures(sketchId_).empty())
        return Status::failure(ErrorCode::InvalidArgument,
                               "This sketch is used by a 3D feature. Delete that feature first.",
                               "DeleteSketch: sketch has dependents");
    removed_ = document.removeSketch(sketchId_, &index_);
    return removed_ ? okStatus()
                    : Status::failure(ErrorCode::InvalidReference, "The sketch no longer exists.", "DeleteSketch: unknown");
}

void DeleteSketchCommand::undo(doc::Document& document)
{
    if (removed_)
        document.addSketch(std::move(removed_), index_);
}

} // namespace os::cmd
