#pragma once

#include "core/Result.h"
#include "core/Uuid.h"
#include "geometry/Shape.h"
#include "geometry/TopoSignature.h"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace os::doc {

enum class FeatureKind { Box, PushPull, Fillet, Chamfer };

std::string_view toString(FeatureKind kind);
std::optional<FeatureKind> featureKindFromString(std::string_view text);

// Persistent reference to a face of a feature's *input* shape.
// See docs/TOPOLOGICAL_NAMING.md for why this is a hint plus a signature.
struct FaceRef {
    int indexHint = -1;
    geom::FaceSignature signature;
};

struct EdgeRef {
    int indexHint = -1;
    geom::EdgeSignature signature;
};

enum class ParameterKind { Length, Angle, Count };

// A single editable scalar exposed for the history panel / numeric editing.
struct ParameterInfo {
    std::string key;    // stable identifier used by commands and files
    std::string label;  // user-facing
    ParameterKind kind = ParameterKind::Length;
    double value = 0;   // canonical units (mm / radians)
};

// One step of a body's modeling history. A feature is a pure function of its
// parameters and the body's shape before it (the *input*). Features never
// hold kernel state between evaluations; results are cached by Body.
class Feature {
public:
    explicit Feature(Uuid id = Uuid::generate()) : id_(id) {}
    virtual ~Feature() = default;

    const Uuid& id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }
    bool isSuppressed() const { return suppressed_; }
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }

    virtual FeatureKind kind() const = 0;
    virtual std::unique_ptr<Feature> clone() const = 0;

    // True for features that create a body from nothing (input is ignored).
    virtual bool isBaseFeature() const { return false; }

    virtual Result<geom::Shape> compute(const geom::Shape& input) const = 0;

    virtual std::vector<ParameterInfo> parameters() const = 0;
    virtual Status setParameter(std::string_view key, double value) = 0;
    std::optional<double> parameter(std::string_view key) const;

    virtual void writeParams(nlohmann::json& out) const = 0;
    virtual Status readParams(const nlohmann::json& in) = 0;

    // Ids of other document objects this feature depends on (beyond its own
    // body's preceding history). Empty for all current feature kinds.
    virtual std::vector<Uuid> dependencies() const { return {}; }

protected:
    Feature(const Feature&) = default;

private:
    Uuid id_;
    std::string name_;
    bool suppressed_ = false;
};

std::unique_ptr<Feature> createFeature(FeatureKind kind, Uuid id);

// ---- Concrete features ------------------------------------------------------

class BoxFeature final : public Feature {
public:
    using Feature::Feature;
    Vec3 origin;
    Vec3 size{20, 20, 20};

    FeatureKind kind() const override { return FeatureKind::Box; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new BoxFeature(*this)); }
    bool isBaseFeature() const override { return true; }
    Result<geom::Shape> compute(const geom::Shape& input) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

// Moves a planar face along its normal (extrude / cut by face).
class PushPullFeature final : public Feature {
public:
    using Feature::Feature;
    FaceRef face;
    double distance = 0; // mm, positive = outward

    FeatureKind kind() const override { return FeatureKind::PushPull; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new PushPullFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input) const override;
    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;
};

class EdgeTreatmentFeature : public Feature {
public:
    using Feature::Feature;
    std::vector<EdgeRef> edges;
    double size = 1; // fillet radius or chamfer distance, mm

    std::vector<ParameterInfo> parameters() const override;
    Status setParameter(std::string_view key, double value) override;
    void writeParams(nlohmann::json& out) const override;
    Status readParams(const nlohmann::json& in) override;

protected:
    Result<std::vector<int>> resolveEdges(const geom::Shape& input) const;
    virtual const char* sizeLabel() const = 0;
    EdgeTreatmentFeature(const EdgeTreatmentFeature&) = default;
};

class FilletFeature final : public EdgeTreatmentFeature {
public:
    using EdgeTreatmentFeature::EdgeTreatmentFeature;
    FeatureKind kind() const override { return FeatureKind::Fillet; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new FilletFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input) const override;

private:
    const char* sizeLabel() const override { return "Radius"; }
};

class ChamferFeature final : public EdgeTreatmentFeature {
public:
    using EdgeTreatmentFeature::EdgeTreatmentFeature;
    FeatureKind kind() const override { return FeatureKind::Chamfer; }
    std::unique_ptr<Feature> clone() const override { return std::unique_ptr<Feature>(new ChamferFeature(*this)); }
    Result<geom::Shape> compute(const geom::Shape& input) const override;

private:
    const char* sizeLabel() const override { return "Distance"; }
};

} // namespace os::doc
