#pragma once

#include "core/Result.h"
#include "core/Units.h"
#include "core/Uuid.h"
#include "document/Body.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace os::doc {

// The persistent model. Owns bodies and their feature histories.
// Mutations go through commands (see commands/) so they can be undone;
// the mutation API here keeps derived state (recompute, revision) consistent.
class Document {
public:
    Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const Uuid& id() const { return id_; }
    void setId(const Uuid& id) { id_ = id; }

    LengthUnit displayUnit() const { return displayUnit_; }
    void setDisplayUnit(LengthUnit unit);

    const std::vector<std::unique_ptr<Body>>& bodies() const { return bodies_; }
    Body* body(const Uuid& id) const;
    int bodyIndex(const Uuid& id) const;
    // Finds the body owning a feature.
    Body* bodyOfFeature(const Uuid& featureId) const;

    Body& addBody(std::unique_ptr<Body> body, int index = -1);
    std::unique_ptr<Body> removeBody(const Uuid& id, int* removedIndex = nullptr);
    void setBodyVisible(const Uuid& id, bool visible);

    // Inserts a feature and recomputes from it. Returns the new feature's state.
    const FeatureState& insertFeature(const Uuid& bodyId, std::unique_ptr<Feature> feature, int index = -1);
    std::unique_ptr<Feature> removeFeature(const Uuid& featureId, int* removedIndex = nullptr);
    // Call after changing a feature's parameters in place.
    void featureChanged(const Uuid& featureId);

    // Evaluates `feature` as if appended to the end of the body's history,
    // without modifying the document. Used for interactive previews.
    Result<geom::Shape> preview(const Uuid& bodyId, const Feature& feature) const;

    void recomputeAll();

    // Monotonic change counter plus listeners, so views can refresh.
    std::uint64_t revision() const { return revision_; }
    using Listener = std::function<void()>;
    int addListener(Listener listener);
    void removeListener(int handle);

    std::string nextBodyName();

private:
    void changed();

    Uuid id_;
    LengthUnit displayUnit_ = LengthUnit::Millimeter;
    std::vector<std::unique_ptr<Body>> bodies_;
    std::uint64_t revision_ = 0;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
};

} // namespace os::doc
