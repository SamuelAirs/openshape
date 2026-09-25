#pragma once

#include "core/Result.h"
#include "core/Units.h"
#include "core/Uuid.h"
#include "document/Body.h"
#include "sketch/Sketch.h"

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
    // without modifying the document. Used for interactive previews. A nil
    // body id previews a base feature (new body) with no input shape.
    Result<geom::Shape> preview(const Uuid& bodyId, const Feature& feature) const;

    // ---- Sketches ----
    const std::vector<std::unique_ptr<sketch::Sketch>>& sketches() const { return sketches_; }
    sketch::Sketch* sketch(const Uuid& id) const;
    void addSketch(std::unique_ptr<sketch::Sketch> sketch, int index = -1);
    std::unique_ptr<sketch::Sketch> removeSketch(const Uuid& id, int* removedIndex = nullptr);
    // Replaces a sketch's content (matched by id) and recomputes dependent features.
    void replaceSketch(const sketch::Sketch& sketch);
    // Changes whenever a sketch's content changes (for view caches).
    std::uint64_t sketchRevision(const Uuid& id) const;
    // Features (in any body) that depend on a document object.
    std::vector<Uuid> dependentFeatures(const Uuid& objectId) const;
    // True if `bodyId` (transitively) uses `otherBodyId` (or is it).
    bool dependsOn(const Uuid& bodyId, const Uuid& otherBodyId) const;
    std::string nextSketchName() const;

    EvalContext context() const { return EvalContext{this}; }

    void recomputeAll();

    // Monotonic change counter plus listeners, so views can refresh.
    std::uint64_t revision() const { return revision_; }
    using Listener = std::function<void()>;
    int addListener(Listener listener);
    void removeListener(int handle);

    std::string nextBodyName() const;

private:
    void changed();

    Uuid id_;
    LengthUnit displayUnit_ = LengthUnit::Millimeter;
    void recomputeDependents(const Uuid& objectId);
    // Moves attached sketches onto their faces' current planes (and
    // recomputes what depends on them). Called after every recompute.
    void syncSketchAttachments();
    void bumpSketchRevision(const Uuid& id);

    std::vector<std::unique_ptr<Body>> bodies_;
    std::vector<std::unique_ptr<sketch::Sketch>> sketches_;
    std::vector<std::pair<Uuid, std::uint64_t>> sketchRevisions_;
    std::uint64_t revision_ = 0;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextListener_ = 1;
};

} // namespace os::doc
