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
                                      "insertFeature: unknown body"};
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
    if (bodyId.isNil())
        return feature.compute({}, context());
    const Body* b = body(bodyId);
    if (!b)
        return Result<geom::Shape>::failure(ErrorCode::InvalidReference, "The body no longer exists.", "preview: unknown body");
    return feature.compute(b->shape(), context());
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

void Document::recomputeDependents(const Uuid& objectId)
{
    for (auto& b : bodies_) {
        const auto& features = b->features();
        for (std::size_t i = 0; i < features.size(); ++i) {
            const auto deps = features[i]->dependencies();
            if (std::find(deps.begin(), deps.end(), objectId) != deps.end()) {
                b->recompute(static_cast<int>(i), context());
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

void Document::syncSketchAttachments()
{
    // A few passes at most: moving a sketch recomputes its dependents, which
    // may move another sketch attached downstream.
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (auto& sk : sketches_) {
            if (!sk->attachment())
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
            if (const Body* body = this->body(sk->attachment()->body)) {
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
