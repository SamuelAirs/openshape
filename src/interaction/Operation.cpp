#include "interaction/Operation.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"

namespace os::interact {

namespace {
std::uint64_t nextPreviewKey()
{
    // Preview mesh keys live in the upper half of the key space so they never
    // collide with body shape revisions.
    static std::uint64_t counter = 1ull << 62;
    return ++counter;
}
} // namespace

void Operation::setValue(double value, const doc::Document& document)
{
    if (!allowsNegative() && value < 0)
        value = 0;
    value_ = value;
    error_.clear();
    if (value == 0.0) {
        previewMesh_.reset();
        return;
    }
    auto feature = makeFeature(value);
    auto result = document.preview(previewBody(), *feature);
    if (!result) {
        previewMesh_.reset();
        error_ = result.userMessage();
        OS_LOG(Debug, Interaction) << title() << " preview failed at " << value << ": " << result.developerMessage();
        return;
    }
    previewMesh_ = std::make_shared<const geom::Mesh>(geom::tessellate(result.value()));
    previewKey_ = nextPreviewKey();
}

std::unique_ptr<cmd::Command> Operation::makeCommand(const doc::Document&) const
{
    return std::make_unique<cmd::AddFeatureCommand>(bodyId_, makeFeature(value_));
}

// ---- Push/pull -----------------------------------------------------------------

std::unique_ptr<PushPullOperation> PushPullOperation::create(const doc::Document& document, const Uuid& bodyId, int faceIndex)
{
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return nullptr;
    const auto info = geom::faceInfo(body->shape(), faceIndex);
    const auto signature = geom::captureFaceSignature(body->shape(), faceIndex);
    if (!info || !signature || !info->isPlanar())
        return nullptr;
    return std::unique_ptr<PushPullOperation>(
        new PushPullOperation(bodyId, LinearManipulator(info->centroid, info->normal), doc::FaceRef{faceIndex, *signature}));
}

std::unique_ptr<doc::Feature> PushPullOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::PushPullFeature>();
    feature->face = face_;
    feature->distance = value;
    return feature;
}

// ---- Shell -----------------------------------------------------------------------

std::unique_ptr<ShellOperation> ShellOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                       const std::vector<int>& faceIndices)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || faceIndices.empty())
        return nullptr;
    std::vector<doc::FaceRef> refs;
    for (int f : faceIndices) {
        const auto signature = geom::captureFaceSignature(body->shape(), f);
        if (!signature)
            return nullptr;
        refs.push_back({f, *signature});
    }
    const auto first = geom::faceInfo(body->shape(), faceIndices.front());
    if (!first)
        return nullptr;
    return std::unique_ptr<ShellOperation>(
        new ShellOperation(bodyId, LinearManipulator(first->centroid, first->normal * -1.0), std::move(refs)));
}

std::unique_ptr<doc::Feature> ShellOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::ShellFeature>();
    feature->faces = faces_;
    feature->thickness = value;
    return feature;
}

// ---- Fillet / chamfer ------------------------------------------------------------

std::unique_ptr<EdgeOperation> EdgeOperation::create(const doc::Document& document, const Uuid& bodyId,
                                                     const std::vector<int>& edgeIndices, doc::FeatureKind kind)
{
    const doc::Body* body = document.body(bodyId);
    if (!body || edgeIndices.empty())
        return nullptr;
    std::vector<doc::EdgeRef> refs;
    for (int e : edgeIndices) {
        const auto signature = geom::captureEdgeSignature(body->shape(), e);
        if (!signature)
            return nullptr;
        refs.push_back({e, *signature});
    }
    const auto first = geom::edgeInfo(body->shape(), edgeIndices.front());
    if (!first)
        return nullptr;
    // Direction into the material: opposite the sum of the adjacent face normals.
    Vec3 outward;
    for (int f : geom::facesOfEdge(body->shape(), edgeIndices.front()))
        if (const auto info = geom::faceInfo(body->shape(), f))
            outward += info->normal;
    Vec3 direction = (outward * -1.0).normalized();
    if (direction.length() < 0.5) {
        // Degenerate (e.g. seam edge): fall back to any direction perpendicular to the edge.
        direction = first->tangent.cross(Vec3{0, 0, 1}).normalized();
        if (direction.length() < 0.5)
            direction = first->tangent.cross(Vec3{1, 0, 0}).normalized();
    }
    return std::unique_ptr<EdgeOperation>(
        new EdgeOperation(bodyId, LinearManipulator(first->midpoint, direction), std::move(refs), kind));
}

std::unique_ptr<doc::Feature> EdgeOperation::makeFeature(double value) const
{
    std::unique_ptr<doc::EdgeTreatmentFeature> feature;
    if (kind_ == doc::FeatureKind::Fillet)
        feature = std::make_unique<doc::FilletFeature>();
    else
        feature = std::make_unique<doc::ChamferFeature>();
    feature->edges = edges_;
    feature->size = value;
    return feature;
}

// ---- Extrude --------------------------------------------------------------------

std::unique_ptr<ExtrudeOperation> ExtrudeOperation::create(const doc::Document& document, const Uuid& sketchId,
                                                           std::vector<doc::ProfileRef> profiles, const Vec3& anchor)
{
    const sketch::Sketch* sk = document.sketch(sketchId);
    if (!sk || profiles.empty())
        return nullptr;
    std::optional<Uuid> host;
    if (sk->hostBody() && document.body(*sk->hostBody()) && document.body(*sk->hostBody())->isVisible())
        host = sk->hostBody();
    return std::unique_ptr<ExtrudeOperation>(
        new ExtrudeOperation(sketchId, host, LinearManipulator(anchor, sk->plane().normal()), std::move(profiles)));
}

doc::ExtrudeMode ExtrudeOperation::mode() const
{
    if (modeOverride_)
        return host_ || *modeOverride_ == doc::ExtrudeMode::NewBody ? *modeOverride_ : doc::ExtrudeMode::NewBody;
    if (!host_)
        return doc::ExtrudeMode::NewBody;
    return value() < 0 ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::Join;
}

Uuid ExtrudeOperation::previewBody() const
{
    return mode() == doc::ExtrudeMode::NewBody ? Uuid() : host_.value_or(Uuid());
}

std::unique_ptr<doc::Feature> ExtrudeOperation::makeFeature(double value) const
{
    auto feature = std::make_unique<doc::ExtrudeFeature>();
    feature->sketchId = sketchId_;
    feature->profiles = profiles_;
    feature->distance = value;
    feature->mode = mode();
    feature->throughAll = throughAll_ && feature->mode == doc::ExtrudeMode::Cut;
    return feature;
}

std::unique_ptr<cmd::Command> ExtrudeOperation::makeCommand(const doc::Document& document) const
{
    if (mode() == doc::ExtrudeMode::NewBody) {
        return std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), makeFeature(value()));
    }
    return std::make_unique<cmd::AddFeatureCommand>(*host_, makeFeature(value()));
}

} // namespace os::interact
