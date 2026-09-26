// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/Document.h"

#include "core/Log.h"
#include "document/SketchProfiles.h"
#include "geometry/TopoSignature.h"

#include <algorithm>

namespace os::doc {

Document::Document() : id_(Uuid::generate()) {}

void Document::setDisplayUnit(LengthUnit unit)
{
    if (unit == displayUnit_)
        return;
    displayUnit_ = unit;
    changed();
}

Body* Document::body(const Uuid& id) const
{
    for (const auto& b : bodies_)
        if (b->id() == id)
            return b.get();
    return nullptr;
}

int Document::bodyIndex(const Uuid& id) const
{
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i]->id() == id)
            return static_cast<int>(i);
    return -1;
}

Body* Document::bodyOfFeature(const Uuid& featureId) const
{
    for (const auto& b : bodies_)
        if (b->feature(featureId))
            return b.get();
    return nullptr;
}

Body& Document::addBody(std::unique_ptr<Body> body, int index)
{
    if (index < 0 || index > static_cast<int>(bodies_.size()))
        index = static_cast<int>(bodies_.size());
    Body& ref = *body;
    bodies_.insert(bodies_.begin() + index, std::move(body));
    ref.recompute(0, context());
    recomputeDependents(ref.id()); // bodies that combine with this one
    OS_LOG(Debug, Document) << "added body " << ref.id().toString() << " '" << ref.name() << "'";
    syncSketchAttachments();
    changed();
    return ref;
}

std::unique_ptr<Body> Document::removeBody(const Uuid& id, int* removedIndex)
{
    const int index = bodyIndex(id);
    if (removedIndex)
        *removedIndex = index;
    if (index < 0)
        return nullptr;
    auto body = std::move(bodies_[static_cast<std::size_t>(index)]);
    bodies_.erase(bodies_.begin() + index);
    recomputeDependents(id);
    OS_LOG(Debug, Document) << "removed body " << id.toString();
    syncSketchAttachments();
    changed();
    return body;
}

void Document::setBodyVisible(const Uuid& id, bool visible)
{
    Body* b = body(id);
    if (!b || b->isVisible() == visible)
        return;
    b->setVisible(visible);
    syncSketchAttachments();
    changed();
}

const FeatureState& Document::insertFeature(const Uuid& bodyId, std::unique_ptr<Feature> feature, int index)
{
    Body* b = body(bodyId);
    static const FeatureState missing{FeatureStatus::Failed, {}, ErrorCode::InvalidReference, "The body no longer exists.",
                                      "insertFeature: unknown body", {}};
    if (!b)
        return missing;
    if (index < 0 || index > static_cast<int>(b->features().size()))
        index = static_cast<int>(b->features().size());
    b->insertFeature(std::move(feature), index);
    b->recompute(index, context());
    recomputeDependents(bodyId);
    syncSketchAttachments();
    changed();
    return b->state(index);
}

std::unique_ptr<Feature> Document::removeFeature(const Uuid& featureId, int* removedIndex)
{
    Body* b = bodyOfFeature(featureId);
    if (!b) {
        if (removedIndex)
            *removedIndex = -1;
        return nullptr;
    }
    int index = -1;
    auto feature = b->removeFeature(featureId, &index);
    if (removedIndex)
        *removedIndex = index;
    b->recompute(index, context());
    recomputeDependents(b->id());
    syncSketchAttachments();
    changed();
    return feature;
}

void Document::featureChanged(const Uuid& featureId)
{
    Body* b = bodyOfFeature(featureId);
    if (!b)
        return;
    b->recompute(b->featureIndex(featureId), context());
    recomputeDependents(b->id());
    syncSketchAttachments();
    changed();
}

Result<geom::Shape> Document::preview(const Uuid& bodyId, const Feature& feature) const
{
    EvalContext interactive = context();
    interactive.interactive = true; // refusals may name a size that works
    if (bodyId.isNil())
        return feature.compute({}, interactive);
    const Body* b = body(bodyId);
    if (!b)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "preview: unknown body");
    return feature.compute(b->shape(), interactive);
}

sketch::Sketch* Document::sketch(const Uuid& id) const
{
    for (const auto& s : sketches_)
        if (s->id() == id)
            return s.get();
    return nullptr;
}

void Document::addSketch(std::unique_ptr<sketch::Sketch> sketch, int index)
{
    if (index < 0 || index > static_cast<int>(sketches_.size()))
        index = static_cast<int>(sketches_.size());
    const Uuid id = sketch->id();
    sketches_.insert(sketches_.begin() + index, std::move(sketch));
    bumpSketchRevision(id);
    recomputeDependents(id);
    syncSketchAttachments();
    changed();
}

std::unique_ptr<sketch::Sketch> Document::removeSketch(const Uuid& id, int* removedIndex)
{
    for (std::size_t i = 0; i < sketches_.size(); ++i) {
        if (sketches_[i]->id() != id)
            continue;
        auto removed = std::move(sketches_[i]);
        sketches_.erase(sketches_.begin() + static_cast<long>(i));
        if (removedIndex)
            *removedIndex = static_cast<int>(i);
        bumpSketchRevision(id);
        recomputeDependents(id);
        syncSketchAttachments();
    changed();
        return removed;
    }
    if (removedIndex)
        *removedIndex = -1;
    return nullptr;
}

void Document::replaceSketch(const sketch::Sketch& replacement)
{
    for (auto& s : sketches_) {
        if (s->id() != replacement.id())
            continue;
        *s = replacement;
        bumpSketchRevision(replacement.id());
        recomputeDependents(replacement.id());
        syncSketchAttachments();
    changed();
        return;
    }
}

std::uint64_t Document::sketchRevision(const Uuid& id) const
{
    for (const auto& [sid, rev] : sketchRevisions_)
        if (sid == id)
            return rev;
    return 0;
}

void Document::bumpSketchRevision(const Uuid& id)
{
    static std::uint64_t counter = 0;
    for (auto& [sid, rev] : sketchRevisions_)
        if (sid == id) {
            rev = ++counter;
            return;
        }
    sketchRevisions_.emplace_back(id, ++counter);
}

Datum* Document::datum(const Uuid& id) const
{
    for (const auto& d : datums_)
        if (d->id() == id)
            return d.get();
    return nullptr;
}

void Document::addDatum(std::unique_ptr<Datum> datum, int index)
{
    if (index < 0 || index > static_cast<int>(datums_.size()))
        index = static_cast<int>(datums_.size());
    datums_.insert(datums_.begin() + index, std::move(datum));
    ++datumRevision_;
    syncSketchAttachments(); // resolves it, and moves sketches placed on it
    changed();
}

std::unique_ptr<Datum> Document::removeDatum(const Uuid& id, int* removedIndex)
{
    for (std::size_t i = 0; i < datums_.size(); ++i) {
        if (datums_[i]->id() != id)
            continue;
        auto removed = std::move(datums_[i]);
        datums_.erase(datums_.begin() + static_cast<long>(i));
        if (removedIndex)
            *removedIndex = static_cast<int>(i);
        ++datumRevision_;
        // Sketches that were on it stay where they are.
        syncSketchAttachments();
        changed();
        return removed;
    }
    if (removedIndex)
        *removedIndex = -1;
    return nullptr;
}

void Document::replaceDatum(const Datum& replacement)
{
    for (auto& d : datums_) {
        if (d->id() != replacement.id())
            continue;
        *d = replacement;
        ++datumRevision_;
        syncSketchAttachments();
        changed();
        return;
    }
}

std::vector<Uuid> Document::sketchesOn(const Uuid& datumId) const
{
    std::vector<Uuid> out;
    for (const auto& sk : sketches_)
        if (sk->datumPlane() == datumId)
            out.push_back(sk->id());
    return out;
}

std::string Document::nextDatumName(DatumKind kind) const
{
    const std::string base = kind == DatumKind::Axis ? "Axis " : "Plane ";
    for (int n = 1;; ++n) {
        const std::string candidate = base + std::to_string(n);
        const bool taken = std::any_of(datums_.begin(), datums_.end(), [&](const auto& d) { return d->name() == candidate; });
        if (!taken)
            return candidate;
    }
}

void Document::syncDatums()
{
    for (auto& d : datums_) {
        auto resolved = resolveDatum(*d, context());
        if (!resolved) {
            if (d->error() != resolved.userMessage()) {
                d->setError(resolved.userMessage());
                ++datumRevision_;
                OS_LOG(Debug, Document) << "datum " << d->id().toString() << " failed: " << resolved.developerMessage();
            }
            continue; // keeps its last position
        }
        const DatumGeometry& now = resolved.value();
        const DatumGeometry& was = d->geometry();
        const bool same = (now.origin - was.origin).length() < 1e-9 && (now.direction - was.direction).length() < 1e-12
                       && (now.xAxis - was.xAxis).length() < 1e-12 && (now.center - was.center).length() < 1e-9
                       && std::abs(now.size - was.size) < 1e-9;
        if (!same || d->failed()) {
            d->setGeometry(now);
            d->setError({});
            ++datumRevision_;
        }
    }
}

bool Document::dependsOn(const Uuid& bodyId, const Uuid& otherBodyId) const
{
    // Depth-first over "body uses body" edges.
    std::vector<Uuid> stack{bodyId};
    std::vector<Uuid> seen;
    while (!stack.empty()) {
        const Uuid current = stack.back();
        stack.pop_back();
        if (current == otherBodyId)
            return true;
        if (std::find(seen.begin(), seen.end(), current) != seen.end())
            continue;
        seen.push_back(current);
        if (const Body* b = body(current))
            for (const auto& f : b->features())
                for (const auto& dep : f->dependencies())
                    if (body(dep))
                        stack.push_back(dep);
    }
    return false;
}

std::vector<Uuid> Document::dependentFeatures(const Uuid& objectId) const
{
    std::vector<Uuid> out;
    for (const auto& b : bodies_)
        for (const auto& f : b->features())
            for (const auto& dep : f->dependencies())
                if (dep == objectId)
                    out.push_back(f->id());
    return out;
}

std::vector<Uuid> Document::bodiesUsing(const Uuid& objectId) const
{
    std::vector<Uuid> out;
    for (const auto& b : bodies_) {
        if (b->id() == objectId)
            continue;
        const bool uses = std::any_of(b->features().begin(), b->features().end(), [&](const auto& f) {
            const auto deps = f->dependencies();
            return std::find(deps.begin(), deps.end(), objectId) != deps.end();
        });
        if (uses)
            out.push_back(b->id());
    }
    return out;
}

void Document::recomputeDependents(const Uuid& objectId)
{
    // Transitive: a body that uses a body that changed changes too (A combines
    // with B, B with C: editing C updates B, then A). Each recomputed body is
    // queued in turn. A file can describe a cycle; the per-body cap ends it.
    std::vector<Uuid> queue{objectId};
    std::vector<std::pair<Uuid, int>> recomputed;
    const int cap = static_cast<int>(bodies_.size()) + 1;
    for (std::size_t next = 0; next < queue.size(); ++next) {
        const Uuid changed = queue[next];
        for (auto& b : bodies_) {
            const auto& features = b->features();
            for (std::size_t i = 0; i < features.size(); ++i) {
                const auto deps = features[i]->dependencies();
                if (std::find(deps.begin(), deps.end(), changed) == deps.end())
                    continue;
                auto it = std::find_if(recomputed.begin(), recomputed.end(), [&](const auto& p) { return p.first == b->id(); });
                if (it == recomputed.end())
                    it = recomputed.insert(recomputed.end(), {b->id(), 0});
                if (++it->second > cap) {
                    OS_LOG(Warning, Document) << "dependency cycle through body " << b->id().toString();
                    break;
                }
                const std::uint64_t before = b->shapeRevision();
                b->recompute(static_cast<int>(i), context());
                if (b->shapeRevision() != before)
                    queue.push_back(b->id());
                break;
            }
        }
    }
}

std::string Document::nextSketchName() const
{
    for (int n = 1;; ++n) {
        const std::string candidate = "Sketch " + std::to_string(n);
        const bool taken = std::any_of(sketches_.begin(), sketches_.end(), [&](const auto& s) { return s->name() == candidate; });
        if (!taken)
            return candidate;
    }
}

void Document::recomputeAll()
{
    for (auto& b : bodies_)
        b->recompute(0, context());
    // Bodies that combine with bodies listed after them need a second pass.
    for (auto& b : bodies_)
        recomputeDependents(b->id());
    syncSketchAttachments();
    changed();
}

int Document::addListener(Listener listener)
{
    const int handle = nextListener_++;
    listeners_.emplace_back(handle, std::move(listener));
    return handle;
}

void Document::removeListener(int handle)
{
    std::erase_if(listeners_, [handle](const auto& p) { return p.first == handle; });
}

std::string Document::nextBodyName() const
{
    for (int n = 1;; ++n) {
        const std::string candidate = "Body " + std::to_string(n);
        const bool taken = std::any_of(bodies_.begin(), bodies_.end(), [&](const auto& b) { return b->name() == candidate; });
        if (!taken)
            return candidate;
    }
}

std::vector<std::string> Document::nextBodyNames(std::size_t count) const
{
    std::vector<std::string> names;
    for (int n = 1; names.size() < count; ++n) {
        const std::string candidate = "Body " + std::to_string(n);
        const bool taken = std::any_of(bodies_.begin(), bodies_.end(), [&](const auto& b) { return b->name() == candidate; });
        if (!taken)
            names.push_back(candidate);
    }
    return names;
}

std::string Document::uniqueBodyName(const std::string& base) const
{
    for (int n = 1;; ++n) {
        const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
        const bool taken = std::any_of(bodies_.begin(), bodies_.end(), [&](const auto& b) { return b->name() == candidate; });
        if (!taken)
            return candidate;
    }
}

std::string Document::uniqueSketchName(const std::string& base) const
{
    for (int n = 1;; ++n) {
        const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
        const bool taken = std::any_of(sketches_.begin(), sketches_.end(), [&](const auto& s) { return s->name() == candidate; });
        if (!taken)
            return candidate;
    }
}

void Document::syncSketchAttachments()
{
    // A few passes at most: moving a sketch recomputes its dependents, which
    // may move another sketch attached downstream (and the datums on them).
    for (int pass = 0; pass < 4; ++pass) {
        syncDatums();
        bool moved = false;
        for (auto& sk : sketches_) {
            if (!sk->attachment() && !sk->datumPlane())
                continue;
            const sketch::Plane plane = effectivePlane(*sk, context());
            const sketch::Plane& stored = sk->plane();
            const bool same = (plane.origin - stored.origin).length() < 1e-9 && (plane.xAxis - stored.xAxis).length() < 1e-12
                           && (plane.yAxis - stored.yAxis).length() < 1e-12;
            if (same)
                continue;
            sk->setPlane(plane);
            // Keep the attachment's signature current so the next change is
            // compared against where the face is now.
            if (const Body* body = sk->attachment() ? this->body(sk->attachment()->body) : nullptr) {
                const int index = body->featureIndex(sk->attachment()->feature);
                if (index >= 0) {
                    const auto& output = body->state(index).output;
                    const geom::FaceSignature old{geom::SurfaceKind::Plane, sk->attachment()->faceNormal,
                                                  sk->attachment()->faceCentroid, sk->attachment()->faceArea};
                    if (const auto face = geom::resolveFace(output, old, sk->attachment()->faceHint))
                        if (auto fresh = makeAttachment(*body, sk->attachment()->feature, *face))
                            sk->setAttachment(fresh);
                }
            }
            bumpSketchRevision(sk->id());
            recomputeDependents(sk->id());
            moved = true;
        }
        if (!moved)
            return;
    }
}

void Document::changed()
{
    ++revision_;
    // Copy: a listener may remove itself.
    const auto listeners = listeners_;
    for (const auto& [handle, listener] : listeners)
        listener();
}

} // namespace os::doc
