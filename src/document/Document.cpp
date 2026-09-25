#include "document/Document.h"

#include "core/Log.h"

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
    OS_LOG(Debug, Document) << "added body " << ref.id().toString() << " '" << ref.name() << "'";
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
    OS_LOG(Debug, Document) << "removed body " << id.toString();
    changed();
    return body;
}

void Document::setBodyVisible(const Uuid& id, bool visible)
{
    Body* b = body(id);
    if (!b || b->isVisible() == visible)
        return;
    b->setVisible(visible);
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
    changed();
    return feature;
}

void Document::featureChanged(const Uuid& featureId)
{
    Body* b = bodyOfFeature(featureId);
    if (!b)
        return;
    b->recompute(b->featureIndex(featureId), context());
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

std::string Document::nextSketchName()
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

std::string Document::nextBodyName()
{
    for (int n = 1;; ++n) {
        const std::string candidate = "Body " + std::to_string(n);
        const bool taken = std::any_of(bodies_.begin(), bodies_.end(), [&](const auto& b) { return b->name() == candidate; });
        if (!taken)
            return candidate;
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
