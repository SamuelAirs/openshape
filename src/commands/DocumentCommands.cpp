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
    case doc::FeatureKind::Imported: return "Import";
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

DuplicateBodyCommand::DuplicateBodyCommand(Uuid sourceId, std::string name, std::unique_ptr<doc::Feature> lastStep,
                                           std::string label)
    : sourceId_(sourceId), name_(std::move(name)), lastStep_(std::move(lastStep)), label_(std::move(label))
{
}

namespace {

std::string megabytes(std::uint64_t bytes)
{
    return std::to_string((bytes + (1u << 20) - 1) >> 20);
}

// Bytes of BRep text a set of copied bodies stores (their Imported steps).
std::uint64_t importedBytesOf(const std::vector<std::unique_ptr<doc::Body>>& bodies)
{
    std::uint64_t total = 0;
    for (const auto& b : bodies)
        for (const auto& f : b->features())
            if (const auto* imported = dynamic_cast<const doc::ImportedFeature*>(f.get()))
                total += imported->brepText().size();
    return total;
}

} // namespace

Status DuplicateBodyCommand::plan(const doc::Document& document)
{
    const doc::Body* source = document.body(sourceId_);
    if (!source)
        return missingBody();
    auto contains = [](const auto& list, const auto& value) {
        return std::find(list.begin(), list.end(), value) != list.end();
    };

    // The history a body's copy gets: its steps cloned (ids as in the
    // document for now), except a base step from files before independent
    // copies - a Copy of another body, a SplitPiece of one - which becomes
    // that body's history (resolved the same way; up to the split for a
    // piece) plus the step that made it: then the copy no longer follows
    // that body. A body that cannot be built up to there keeps its link (the
    // copy would fail where the link works).
    std::vector<std::pair<Uuid, Uuid>> replaced; // legacy base step -> the step standing for it
    std::vector<Uuid> inlined;                   // bodies whose history went into the current body's
    std::function<std::vector<std::unique_ptr<doc::Feature>>(const doc::Body&, int, std::vector<Uuid>&)> historyOf;
    auto resolveBase = [&](const doc::Feature& base, std::vector<Uuid>& chain) {
        std::vector<std::unique_ptr<doc::Feature>> out;
        const doc::Body* from = nullptr;
        int end = -1;
        std::unique_ptr<doc::Feature> step;
        if (const auto* copy = dynamic_cast<const doc::CopyFeature*>(&base)) {
            from = document.body(copy->sourceBody);
            if (copy->mirror) {
                auto image = std::make_unique<doc::MirrorFeature>();
                image->planeOrigin = copy->planeOrigin;
                image->planeNormal = copy->planeNormal;
                image->keepOriginal = false;
                step = std::move(image);
            } else if (!copy->motion.isIdentity()) {
                auto move = std::make_unique<doc::MoveFeature>();
                move->setName("Pattern copy");
                move->setMotion(copy->motion);
                step = std::move(move);
            }
        } else if (const auto* piece = dynamic_cast<const doc::SplitPieceFeature*>(&base)) {
            from = document.body(piece->sourceBody);
            end = from ? from->featureIndex(piece->splitFeature) : -1;
            const auto* split = end >= 0 ? dynamic_cast<const doc::SplitFeature*>(from->features()[std::size_t(end)].get()) : nullptr;
            if (!split || split->isSuppressed() || piece->piece < 1 || piece->piece >= static_cast<int>(split->pieces.size()))
                return out;
            auto keep = std::make_unique<doc::SplitFeature>();
            keep->pieces.push_back(split->pieces[std::size_t(piece->piece)]);
            for (std::size_t j = 0; j < split->pieces.size(); ++j)
                if (j != std::size_t(piece->piece))
                    keep->pieces.push_back(split->pieces[j]);
            step = std::move(keep);
        } else {
            return out;
        }
        if (!from || contains(chain, from->id()) || chain.size() > 64)
            return out;
        const std::size_t upTo = end < 0 ? from->features().size() : std::size_t(end);
        for (std::size_t i = 0; i < upTo; ++i)
            if (from->state(static_cast<int>(i)).status != doc::FeatureStatus::Ok
                && from->state(static_cast<int>(i)).status != doc::FeatureStatus::Suppressed)
                return out;
        chain.push_back(from->id());
        out = historyOf(*from, end, chain);
        chain.pop_back();
        if (out.empty())
            return out;
        if (!contains(inlined, from->id()))
            inlined.push_back(from->id());
        replaced.emplace_back(base.id(), step ? step->id() : out.back()->id());
        if (step)
            out.push_back(std::move(step));
        return out;
    };
    historyOf = [&](const doc::Body& body, int end, std::vector<Uuid>& chain) {
        std::vector<std::unique_ptr<doc::Feature>> out;
        const auto& features = body.features();
        const std::size_t n = end < 0 ? features.size() : std::min(std::size_t(end), features.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (i == 0 && features[i]->isBaseFeature() && !features[i]->isSuppressed()) {
                auto resolved = resolveBase(*features[i], chain);
                if (!resolved.empty()) {
                    for (auto& f : resolved)
                        out.push_back(std::move(f));
                    continue;
                }
            }
            out.push_back(features[i]->clone());
        }
        return out;
    };

    // Bodies to copy, dependencies first: the source and, recursively, the
    // tool bodies its Combine steps consumed - decided by the kind of
    // reference, never by visibility (a tool shown again is still consumed; a
    // copy's source that is merely hidden is still shared).
    struct Planned {
        const doc::Body* body = nullptr;
        std::vector<std::unique_ptr<doc::Feature>> history;
        std::vector<Uuid> inlined; // bodies whose history it took over
        bool verbatim = true;
    };
    std::vector<Planned> planned;
    std::vector<Uuid> seen;
    std::function<void(const doc::Body&)> visit = [&](const doc::Body& body) {
        seen.push_back(body.id());
        Planned p;
        p.body = &body;
        std::vector<Uuid> chain{body.id()};
        const std::size_t replacedBefore = replaced.size();
        inlined.clear();
        p.history = historyOf(body, -1, chain);
        p.inlined = inlined;
        p.verbatim = replaced.size() == replacedBefore;
        for (const auto& f : p.history)
            if (const auto* combine = dynamic_cast<const doc::CombineFeature*>(f.get()))
                if (const doc::Body* tool = document.body(combine->toolBody); tool && !contains(seen, tool->id()))
                    visit(*tool);
        planned.push_back(std::move(p));
    };
    visit(*source);

    // The sketches their steps use, in document order.
    std::vector<Uuid> used;
    for (const Planned& p : planned)
        for (const auto& f : p.history)
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
    for (const Planned& p : planned)
        copies[p.body->id()] = p.body->id() == sourceId_ ? copyId_ : Uuid::generate();
    for (const sketch::Sketch* s : sketches)
        copies[s->id()] = Uuid::generate();
    std::vector<std::vector<std::unique_ptr<doc::Feature>>> features(planned.size());
    for (std::size_t i = 0; i < planned.size(); ++i)
        for (const auto& f : planned[i].history) {
            auto copy = f->cloneWithNewId();
            copies[f->id()] = copy->id();
            features[i].push_back(std::move(copy));
        }
    // A sketch drawn on a resolved legacy step, or on the body whose history
    // replaced it, goes with the step standing for it and with the copy
    // (steps never refer to those bodies: only sketch attachments do).
    for (const auto& [legacy, standIn] : replaced)
        if (const auto it = copies.find(standIn); it != copies.end())
            copies[legacy] = it->second;
    std::map<Uuid, Uuid> inlinedInto;
    for (const Planned& p : planned)
        for (const Uuid& from : p.inlined)
            if (!copies.count(from)) // unless copied itself
                inlinedInto.emplace(from, copies.at(p.body->id()));
    auto mapped = [&](const Uuid& id) {
        if (const auto it = copies.find(id); it != copies.end())
            return it->second;
        const auto it = inlinedInto.find(id);
        return it == inlinedInto.end() ? id : it->second;
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
    for (std::size_t i = 0; i < planned.size(); ++i) {
        const doc::Body& b = *planned[i].body;
        const bool isCopy = b.id() == sourceId_;
        auto copy = std::make_unique<doc::Body>(copies.at(b.id()));
        copy->setName(isCopy && !name_.empty() ? name_ : uniqueName(b.name() + " copy", true));
        copy->setVisible(isCopy); // copied tools are consumed by the copy: hidden
        int index = 0;
        for (auto& f : features[i]) {
            f->remapReferences(copies);
            copy->insertFeature(std::move(f), index++);
        }
        if (isCopy && lastStep_)
            copy->insertFeature(lastStep_->clone(), index);
        bodies_.push_back(std::move(copy));
        originals_.push_back(b.id());
        verbatim_.push_back(planned[i].verbatim);
    }
    return okStatus();
}

std::uint64_t DuplicateBodyCommand::plannedImportedBytes() const
{
    return importedBytesOf(bodies_);
}

std::uint64_t DuplicateBodyCommand::importedBytesOfCopy(const doc::Document& document, const Uuid& sourceId)
{
    DuplicateBodyCommand probe(sourceId);
    if (!probe.plan(document))
        return 0;
    return probe.plannedImportedBytes();
}

Status checkImportedCopiesFit(const doc::Document& document, const Uuid& sourceId, std::size_t copies)
{
    const std::uint64_t stored = document.importedGeometryBytes();
    if (stored == 0 || copies == 0)
        return okStatus(); // nothing imported: nothing to copy
    const std::uint64_t each = DuplicateBodyCommand::importedBytesOfCopy(document, sourceId);
    const std::uint64_t limit = document.importedGeometryLimit();
    if (stored <= limit && each <= (limit - stored) / copies)
        return okStatus();
    return Status::failure(ErrorCode::Unsupported,
                           (copies == 1 ? std::string("A separate copy") : std::to_string(copies) + " separate copies")
                               + " of this imported geometry would not fit in the project (at most " + megabytes(limit)
                               + " MB of imported geometry).",
                           "copies: " + std::to_string(copies) + " x " + std::to_string(each) + " bytes on top of "
                               + std::to_string(stored));
}

Status DuplicateBodyCommand::execute(doc::Document& document)
{
    if (!planned_) {
        if (Status s = plan(document); !s)
            return s;
        planned_ = true;
    }
    // Imported geometry is stored once per step: the project must stay
    // within what it can save (checked before anything changes).
    if (const std::uint64_t adding = plannedImportedBytes(); adding > 0) {
        const std::uint64_t stored = document.importedGeometryBytes();
        const std::uint64_t limit = document.importedGeometryLimit();
        if (stored > limit || adding > limit - stored)
            return Status::failure(ErrorCode::Unsupported,
                                   "Another copy of this imported geometry would not fit in the project (at most "
                                       + megabytes(limit) + " MB of imported geometry). Nothing was copied.",
                                   "duplicate: " + std::to_string(adding) + " bytes on top of " + std::to_string(stored));
    }
    for (const auto& s : sketches_)
        document.addSketch(std::make_unique<sketch::Sketch>(s));
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        // The cloned steps give what the original's give: take its results
        // over rather than computing the whole history again (a pattern of
        // many copies, or pieces of a long history, would pay it per copy).
        auto copy = std::make_unique<doc::Body>(*bodies_[i]);
        const doc::Body* original = document.body(originals_[i]);
        const int computed = original && verbatim_[i] ? copy->adoptResults(*original) : 0;
        document.addBody(std::move(copy), -1, computed);
    }
    const doc::Body* source = document.body(sourceId_);
    const doc::Body* copy = document.body(copyId_);
    if (!copy || (source && !source->shape().isNull() && copy->shape().isNull())) {
        undo(document);
        return Status::failure(ErrorCode::KernelFailure, "Unable to duplicate this body.", "duplicate: the copy did not compute");
    }
    if (lastStep_) {
        // The step that places or trims the copy must work and change something.
        const doc::FeatureState& state = copy->state(static_cast<int>(copy->features().size()) - 1);
        if (state.status != doc::FeatureStatus::Ok || state.error == ErrorCode::NoEffect) {
            Status failure = state.status == doc::FeatureStatus::NotComputed
                               ? Status::failure(ErrorCode::InvalidArgument,
                                                 "Unable to copy this body: one of its steps failed. Fix or delete that step first.",
                                                 "duplicate: a step of the source failed")
                               : failureFrom(state);
            undo(document);
            return failure;
        }
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

std::unique_ptr<Command> makeCopyBodiesCommand(const Uuid& sourceId, std::vector<std::unique_ptr<doc::Feature>> lastSteps,
                                               const std::vector<std::string>& names, const std::string& label)
{
    std::vector<std::unique_ptr<Command>> steps;
    for (std::size_t i = 0; i < lastSteps.size(); ++i)
        steps.push_back(std::make_unique<DuplicateBodyCommand>(sourceId, i < names.size() ? names[i] : std::string(),
                                                               std::move(lastSteps[i]), label));
    return std::make_unique<CompositeCommand>(label, std::move(steps));
}

// ---- Split into bodies ------------------------------------------------------------

Result<std::unique_ptr<Command>> makeSplitBodyCommand(const doc::Document& document, const Uuid& bodyId, int* pieceCount)
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

    // Each piece stores the body's imported geometry again: refused plainly
    // (before anything changes) when the project could no longer be saved.
    if (const std::uint64_t each = DuplicateBodyCommand::importedBytesOfCopy(document, bodyId); each > 0) {
        const std::uint64_t stored = document.importedGeometryBytes();
        const std::uint64_t limit = document.importedGeometryLimit();
        const std::uint64_t more = pieces.size() - 1;
        if (stored > limit || each > (limit - stored) / more)
            return R::failure(ErrorCode::Unsupported,
                              "Each piece would store this body's imported geometry again, more than the project can hold (at most "
                                  + megabytes(limit) + " MB of imported geometry). Nothing was split.",
                              "split: " + std::to_string(more) + " x " + std::to_string(each) + " bytes on top of "
                                  + std::to_string(stored));
    }

    // Every other piece first: an independent copy of the body's history as
    // it is now (before its own Split step), ending in a Split step that
    // keeps that piece (listed first; the others are recorded so they go).
    std::vector<std::unique_ptr<Command>> steps;
    const std::vector<std::string> names = document.nextBodyNames(pieces.size() - 1);
    for (std::size_t k = 1; k < pieces.size(); ++k) {
        auto keep = std::make_unique<doc::SplitFeature>();
        keep->pieces.push_back(pieces[k]);
        for (std::size_t j = 0; j < pieces.size(); ++j)
            if (j != k)
                keep->pieces.push_back(pieces[j]);
        steps.push_back(std::make_unique<DuplicateBodyCommand>(bodyId, names[k - 1], std::move(keep), "Split into bodies"));
    }
    // Then the body keeps its largest piece.
    auto split = std::make_unique<doc::SplitFeature>();
    split->pieces = pieces;
    steps.push_back(std::make_unique<AddFeatureCommand>(bodyId, std::move(split)));
    if (pieceCount)
        *pieceCount = static_cast<int>(pieces.size());
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

Status AddDatumCommand::execute(doc::Document& document)
{
    if (document.datum(datum_.id()))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to add this axis or plane.", "datum id already exists");
    document.addDatum(std::make_unique<doc::Datum>(datum_));
    return okStatus();
}

void AddDatumCommand::undo(doc::Document& document)
{
    document.removeDatum(datum_.id());
}

Status EditDatumCommand::execute(doc::Document& document)
{
    const doc::Datum* current = document.datum(after_.id());
    if (!current)
        return Status::failure(ErrorCode::InvalidReference, "That axis or plane no longer exists.", "EditDatum: unknown");
    before_ = *current;
    document.replaceDatum(after_);
    return okStatus();
}

void EditDatumCommand::undo(doc::Document& document)
{
    if (before_)
        document.replaceDatum(*before_);
}

Status DeleteDatumCommand::execute(doc::Document& document)
{
    removed_ = document.removeDatum(datumId_, &index_);
    return removed_ ? okStatus()
                    : Status::failure(ErrorCode::InvalidReference, "That axis or plane no longer exists.", "DeleteDatum: unknown");
}

void DeleteDatumCommand::undo(doc::Document& document)
{
    if (removed_)
        document.addDatum(std::move(removed_), index_);
}

} // namespace os::cmd
