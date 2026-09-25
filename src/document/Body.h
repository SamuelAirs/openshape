#pragma once

#include "core/Uuid.h"
#include "document/Feature.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace os::doc {

enum class FeatureStatus {
    NotComputed, // never evaluated, or blocked by an upstream failure
    Ok,
    Failed,
    Suppressed,
};

struct FeatureState {
    FeatureStatus status = FeatureStatus::NotComputed;
    geom::Shape output;      // body shape after this feature (valid when Ok/Suppressed)
    ErrorCode error = ErrorCode::None;
    std::string userMessage;
    std::string developerMessage;
    // Non-fatal remark on a successful step, e.g. that its result has several
    // separate solids. Shown in the model panel.
    std::string note;
};

// A solid body with a linear feature history. The body's shape is the output
// of the last successfully evaluated feature.
class Body {
public:
    explicit Body(Uuid id = Uuid::generate()) : id_(id) {}
    Body(const Body& other);
    Body& operator=(const Body&) = delete;

    const Uuid& id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    const std::vector<std::unique_ptr<Feature>>& features() const { return features_; }
    Feature* feature(const Uuid& id) const;
    int featureIndex(const Uuid& id) const;

    // History editing. Callers (commands) must call recompute afterwards;
    // Document wraps these to keep that invariant.
    void insertFeature(std::unique_ptr<Feature> feature, int index);
    std::unique_ptr<Feature> removeFeature(const Uuid& id, int* removedIndex = nullptr);

    // Re-evaluates features [fromIndex, end). Earlier cached results are reused.
    // Evaluation stops at the first failure; later features become NotComputed.
    void recompute(int fromIndex, const EvalContext& context);

    const FeatureState& state(int index) const { return states_.at(static_cast<std::size_t>(index)); }
    // Shape before feature `index` (i.e. the output of the last good feature before it).
    geom::Shape shapeBefore(int index) const;

    const geom::Shape& shape() const { return shape_; }
    // Increments whenever shape() changes identity. Views use it to know when
    // to re-tessellate and when topology indices become stale.
    std::uint64_t shapeRevision() const { return shapeRevision_; }

    bool hasFailures() const;

private:
    Uuid id_;
    std::string name_;
    bool visible_ = true;
    std::vector<std::unique_ptr<Feature>> features_;
    std::vector<FeatureState> states_;
    geom::Shape shape_;
    std::uint64_t shapeRevision_ = 0;
};

} // namespace os::doc
