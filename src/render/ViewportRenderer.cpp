// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "render/ViewportRenderer.h"

#include "core/Lighting.h"
#include "core/Log.h"
#include "interaction/ContactShadow.h"

#include <QtCore/QFile>
#include <QtQuick/QQuickRhiItem>
#include <QtQuick/QQuickWindow>
#include <rhi/qshader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace os::render {

namespace {

// std140 layout shared by all shaders (the Frame block in shaders/*).
struct UniformData {
    float mvp[16];
    float view[16];
    float proj[16];
    float color[4];
    float params[4];  // viewport w/h (device px), line width (device px), depth bias (device px towards the viewer)
    float params2[4]; // mode (meshes: lit / tint / overlay; lines: faded axes); grid: minor, major alpha
    float eye[4];     // eye (perspective) or direction towards the viewer (orthographic); 1 = perspective
    float camera[4];  // world size of a device pixel (at view depth 1 in perspective)
    float light[4];   // direction towards the key light
    float lights[4];  // sky, ground, key, fill (core/Lighting.h)
    float gloss[4];   // specular strength, exponent
    float grid[4];    // grid center xy, minor step
    float fade[4];    // grid radius, axis radius, eye fade start, end; shadows: x = blur (mm)
};
static_assert(sizeof(UniformData) == 3 * 64 + 10 * 16);

using Color = std::array<float, 4>;
// Calm, neutral palette; the model is the loudest thing on screen.
constexpr Color kBackground{0.925f, 0.933f, 0.945f, 1.0f};
constexpr Color kBody{0.78f, 0.80f, 0.83f, 1.0f};
constexpr Color kPreviewBody{0.74f, 0.81f, 0.91f, 1.0f};
constexpr Color kEdge{0.17f, 0.19f, 0.22f, 1.0f};
constexpr Color kAccent{0.16f, 0.47f, 0.93f, 1.0f};
constexpr Color kAccentHover{0.35f, 0.62f, 1.0f, 1.0f};
constexpr Color kAccentActive{0.10f, 0.36f, 0.80f, 1.0f};
constexpr Color kError{0.90f, 0.28f, 0.30f, 1.0f};
constexpr Color kGridLine{0.0f, 0.0f, 0.0f, 1.0f};
constexpr float kGridMinorAlpha = 0.06f;
constexpr float kGridMajorAlpha = 0.12f;
constexpr Color kShadow{0.10f, 0.12f, 0.16f, 1.0f};
constexpr quint32 kShadowLayers = 37; // shadow.vert: the center and three rings of 12
constexpr Color kAxisX{0.86f, 0.27f, 0.27f, 0.6f};
constexpr Color kAxisY{0.27f, 0.66f, 0.33f, 0.6f};
constexpr Color kAxisZ{0.25f, 0.45f, 0.88f, 0.6f};
constexpr Color kSketchSelected{0.96f, 0.52f, 0.13f, 1.0f};
constexpr Color kHistoryHighlight{0.96f, 0.52f, 0.13f, 0.42f}; // model panel hover: what a step touched
constexpr Color kSketchDefined{0.12f, 0.14f, 0.18f, 1.0f};
constexpr Color kSketchConstruction{0.55f, 0.58f, 0.62f, 1.0f};
constexpr Color kSketchDimension{0.36f, 0.39f, 0.44f, 0.9f};
constexpr Color kReference{0.80f, 0.50f, 0.12f, 1.0f}; // construction axes and planes


constexpr quint32 kLineVertexFloats = 8; // p0(3) p1(3) corner(2)
// Edges and sketch curves are pulled this many logical pixels' worth of depth
// towards the viewer, so they win against the faces they lie on.
constexpr float kEdgeBiasPx = 2.5f;
// The grid, axes and shadows are pushed this far away from the viewer, so a
// body's bottom face on the ground hides them rather than fighting them.
constexpr float kGroundBiasPx = 1.5f;

Color withAlpha(Color c, float a)
{
    c[3] = a;
    return c;
}

struct SketchLook {
    Color color;
    float width;      // line width, logical px
    float pointSize;  // point marker size, logical px
};

SketchLook lookOf(interact::SketchStyle style)
{
    using S = interact::SketchStyle;
    switch (style) {
    case S::Normal: return {kAccent, 2.0f, 7.0f};
    case S::Defined: return {kSketchDefined, 2.0f, 6.0f};
    case S::Construction: return {kSketchConstruction, 1.25f, 5.0f};
    case S::Hovered: return {kAccentHover, 3.2f, 10.0f};
    case S::Selected: return {kSketchSelected, 3.5f, 10.0f};
    case S::Preview: return {withAlpha(kAccent, 0.85f), 1.75f, 8.0f};
    case S::Guide: return {withAlpha(kAccent, 0.45f), 1.0f, 5.0f};
    case S::Dimension: return {kSketchDimension, 1.0f, 4.0f};
    case S::Measure: return {withAlpha(kAccent, 0.9f), 1.75f, 7.0f};
    case S::Conflict: return {kError, 2.0f, 7.0f};
    case S::Reference: return {withAlpha(kReference, 0.9f), 1.5f, 6.0f};
    }
    return {kAccent, 2.0f, 7.0f};
}

Mat4 toMat4(const QMatrix4x4& q)
{
    Mat4 m;
    const float* d = q.constData();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            m.at(r, c) = d[c * 4 + r];
    return m;
}

void storeMatrix(float* out, const Mat4& m)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = static_cast<float>(m.at(r, c));
}

QShader loadShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        OS_LOG(Error, Render) << "missing shader resource " << path.toStdString();
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

void appendSegment(std::vector<float>& out, const Vec3& a, const Vec3& b)
{
    static constexpr float corners[6][2] = {{0, -1}, {0, 1}, {1, -1}, {1, -1}, {0, 1}, {1, 1}};
    for (const auto& c : corners) {
        out.insert(out.end(), {float(a.x), float(a.y), float(a.z), float(b.x), float(b.y), float(b.z), c[0], c[1]});
    }
}

// Two triangles covering a rectangle on the ground (z = 0).
void appendGroundQuad(std::vector<float>& out, Vec2 center, Vec2 half)
{
    const float x0 = float(center.x - half.x), x1 = float(center.x + half.x);
    const float y0 = float(center.y - half.y), y1 = float(center.y + half.y);
    out.insert(out.end(), {x0, y0, 0, x1, y0, 0, x1, y1, 0, x0, y0, 0, x1, y1, 0, x0, y1, 0});
}

void appendTriangle(std::vector<float>& pos, std::vector<float>& nrm, const Vec3& a, const Vec3& b, const Vec3& c,
                    const Vec3& na, const Vec3& nb, const Vec3& nc)
{
    for (const Vec3* p : {&a, &b, &c})
        pos.insert(pos.end(), {float(p->x), float(p->y), float(p->z)});
    for (const Vec3* n : {&na, &nb, &nc})
        nrm.insert(nrm.end(), {float(n->x), float(n->y), float(n->z)});
}

// Arrow = cylinder shaft + cone head, sized in screen pixels via `px`.
void appendArrow(std::vector<float>& pos, std::vector<float>& nrm, const Vec3& anchor, const Vec3& dir, double px,
                 const interact::ArrowStyle& style)
{
    const Vec3 helper = std::abs(dir.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    const Vec3 u = dir.cross(helper).normalized();
    const Vec3 v = dir.cross(u);
    const Vec3 p0 = anchor + dir * (style.gapPx * px);
    const Vec3 p1 = anchor + dir * ((style.gapPx + style.shaftPx) * px);
    const Vec3 tip = anchor + dir * (style.totalPx() * px);
    const double r = style.shaftRadiusPx * px;
    const double R = style.headRadiusPx * px;
    const double h = style.headPx * px;
    constexpr int segments = 20;
    for (int i = 0; i < segments; ++i) {
        const double a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
        const Vec3 d0 = u * std::cos(a0) + v * std::sin(a0);
        const Vec3 d1 = u * std::cos(a1) + v * std::sin(a1);
        // Shaft
        appendTriangle(pos, nrm, p0 + d0 * r, p1 + d0 * r, p1 + d1 * r, d0, d0, d1);
        appendTriangle(pos, nrm, p0 + d0 * r, p1 + d1 * r, p0 + d1 * r, d0, d1, d1);
        // Cone
        const Vec3 n0 = (d0 * h + dir * R).normalized();
        const Vec3 n1 = (d1 * h + dir * R).normalized();
        appendTriangle(pos, nrm, p1 + d0 * R, tip, p1 + d1 * R, n0, (n0 + n1).normalized(), n1);
        // Cone base
        appendTriangle(pos, nrm, p1, p1 + d1 * R, p1 + d0 * R, -dir, -dir, -dir);
    }
}

} // namespace

struct ViewportRenderer::Draw {
    enum class Kind { Mesh, Lines, Arrow, Ground, Shadow } kind = Kind::Mesh;
    QRhiGraphicsPipeline* pipeline = nullptr;
    const GpuBody* body = nullptr;
    QRhiBuffer* lineBuffer = nullptr;
    quint32 first = 0;
    quint32 count = 0;
    UniformData uniforms{};
};

ViewportRenderer::~ViewportRenderer() = default;

void ViewportRenderer::initialize(QRhiCommandBuffer*)
{
    if (rhi_ != rhi()) {
        // New QRhi (first init or device change): drop every GPU resource.
        bodies_.clear();
        regions_.clear();
        sketchVertices_.reset();
        ringVertices_.reset();
        meshPipeline_.reset();
        tintPipeline_.reset();
        overlayPipeline_.reset();
        linePipeline_.reset();
        overlayLinePipeline_.reset();
        gridPipeline_.reset();
        shadowPipeline_.reset();
        srb_.reset();
        uniforms_.reset();
        gridVertices_.reset();
        groundVertices_.reset();
        arrowPositions_.reset();
        arrowNormals_.reset();
        rhi_ = rhi();
        OS_LOG(Info, Render) << "QRhi backend: " << rhi_->backendName() << ", device: " << rhi_->driverInfo().deviceName.constData();
    }
    if (!meshPipeline_ || sampleCount_ != renderTarget()->sampleCount()) {
        sampleCount_ = renderTarget()->sampleCount();
        createPipelines();
    }
}

void ViewportRenderer::createPipelines()
{
    uniformSlotSize_ = rhi_->ubufAligned(sizeof(UniformData));
    if (!uniforms_) {
        uniformSlots_ = 64;
        uniforms_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformSlotSize_ * uniformSlots_));
        uniforms_->create();
    }
    srb_.reset(rhi_->newShaderResourceBindings());
    srb_->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms_.get(),
        sizeof(UniformData))});
    srb_->create();

    const QShader meshVs = loadShader(QStringLiteral(":/openshape/shaders/mesh.vert.qsb"));
    const QShader meshFs = loadShader(QStringLiteral(":/openshape/shaders/mesh.frag.qsb"));
    const QShader lineVs = loadShader(QStringLiteral(":/openshape/shaders/line.vert.qsb"));
    const QShader lineFs = loadShader(QStringLiteral(":/openshape/shaders/line.frag.qsb"));
    const QShader groundVs = loadShader(QStringLiteral(":/openshape/shaders/grid.vert.qsb"));
    const QShader gridFs = loadShader(QStringLiteral(":/openshape/shaders/grid.frag.qsb"));
    const QShader shadowVs = loadShader(QStringLiteral(":/openshape/shaders/shadow.vert.qsb"));
    const QShader shadowFs = loadShader(QStringLiteral(":/openshape/shaders/shadow.frag.qsb"));

    using Op = QRhiGraphicsPipeline::CompareOp;
    meshPipeline_ = makePipeline(meshVs, meshFs, Layout::Mesh, true, true, false, Op::Less);
    tintPipeline_ = makePipeline(meshVs, meshFs, Layout::Mesh, true, false, true, Op::LessOrEqual);
    overlayPipeline_ = makePipeline(meshVs, meshFs, Layout::Mesh, false, false, true, Op::Always);
    linePipeline_ = makePipeline(lineVs, lineFs, Layout::Line, true, false, true, Op::LessOrEqual);
    overlayLinePipeline_ = makePipeline(lineVs, lineFs, Layout::Line, false, false, true, Op::Always);
    gridPipeline_ = makePipeline(groundVs, gridFs, Layout::Ground, true, false, true, Op::LessOrEqual);
    shadowPipeline_ = makePipeline(shadowVs, shadowFs, Layout::Ground, true, false, true, Op::LessOrEqual);
}

std::unique_ptr<QRhiGraphicsPipeline> ViewportRenderer::makePipeline(const QShader& vs, const QShader& fs, Layout vertices,
                                                                    bool depthTest, bool depthWrite, bool blend,
                                                                    QRhiGraphicsPipeline::CompareOp op)
{
    std::unique_ptr<QRhiGraphicsPipeline> ps(rhi_->newGraphicsPipeline());
    ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    QRhiVertexInputLayout layout;
    switch (vertices) {
    case Layout::Line:
        layout.setBindings({{kLineVertexFloats * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
        break;
    case Layout::Mesh:
        layout.setBindings({{3 * sizeof(float)}, {3 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}, {1, 1, QRhiVertexInputAttribute::Float3, 0}});
        break;
    case Layout::Ground:
        layout.setBindings({{3 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
        break;
    }
    ps->setVertexInputLayout(layout);
    ps->setShaderResourceBindings(srb_.get());
    ps->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    ps->setSampleCount(renderTarget()->sampleCount());
    ps->setDepthTest(depthTest);
    ps->setDepthWrite(depthWrite);
    ps->setDepthOp(op);
    ps->setCullMode(QRhiGraphicsPipeline::None);
    if (blend) {
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        ps->setTargetBlends({b});
    }
    if (!ps->create())
        OS_LOG(Error, Render) << "failed to create graphics pipeline";
    return ps;
}

void ViewportRenderer::synchronize(QQuickRhiItem* item)
{
    // Runs on the render thread while the GUI thread is blocked: safe to read
    // controller state through the provider.
    scene_ = provider_();
    if (item->window())
        devicePixelRatio_ = static_cast<float>(item->window()->effectiveDevicePixelRatio());
}

void ViewportRenderer::uploadBody(GpuBody& gpu, const geom::Mesh& mesh, QRhiResourceUpdateBatch* u, bool castsShadow)
{
    auto makeStatic = [&](std::unique_ptr<QRhiBuffer>& buffer, QRhiBuffer::UsageFlags usage, const void* data, quint32 bytes) {
        buffer.reset();
        if (bytes == 0)
            return;
        buffer.reset(rhi_->newBuffer(QRhiBuffer::Immutable, usage, bytes));
        buffer->create();
        u->uploadStaticBuffer(buffer.get(), data);
    };
    makeStatic(gpu.positions, QRhiBuffer::VertexBuffer, mesh.positions.data(), quint32(mesh.positions.size() * sizeof(float)));
    makeStatic(gpu.normals, QRhiBuffer::VertexBuffer, mesh.normals.data(), quint32(mesh.normals.size() * sizeof(float)));
    makeStatic(gpu.indices, QRhiBuffer::IndexBuffer, mesh.indices.data(), quint32(mesh.indices.size() * sizeof(quint32)));
    gpu.indexCount = quint32(mesh.indices.size());
    gpu.faceTriangleOffset.assign(mesh.faceTriangleOffset.begin(), mesh.faceTriangleOffset.end());
    const interact::ContactFootprint footprint = castsShadow ? interact::contactFootprint(mesh) : interact::ContactFootprint{};
    makeStatic(gpu.groundIndices, QRhiBuffer::IndexBuffer, footprint.indices.data(),
               quint32(footprint.indices.size() * sizeof(quint32)));
    gpu.groundIndexCount = quint32(footprint.indices.size());
    gpu.shadowStrength = float(footprint.strength());
    gpu.shadowBlur = float(footprint.blur());

    std::vector<float> lines;
    gpu.edgeRanges.clear();
    for (const auto& edge : mesh.edges) {
        const quint32 first = quint32(lines.size() / kLineVertexFloats);
        const auto& p = edge.points;
        for (std::size_t i = 0; i + 5 < p.size(); i += 3)
            appendSegment(lines, {p[i], p[i + 1], p[i + 2]}, {p[i + 3], p[i + 4], p[i + 5]});
        gpu.edgeRanges[edge.edgeIndex] = {first, quint32(lines.size() / kLineVertexFloats) - first};
    }
    makeStatic(gpu.edgeVertices, QRhiBuffer::VertexBuffer, lines.data(), quint32(lines.size() * sizeof(float)));
    gpu.edgeVertexCount = quint32(lines.size() / kLineVertexFloats);
}

void ViewportRenderer::ensureDynamicBuffer(std::unique_ptr<QRhiBuffer>& buffer, quint32 size, QRhiBuffer::UsageFlags usage)
{
    if (buffer && buffer->size() >= size)
        return;
    buffer.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, usage, std::max<quint32>(size, 4096)));
    buffer->create();
}

void ViewportRenderer::render(QRhiCommandBuffer* cb)
{
    QRhiResourceUpdateBatch* u = rhi_->nextResourceUpdateBatch();
    const QSize pixelSize = renderTarget()->pixelSize();
    const float dpr = devicePixelRatio_;
    const Camera& camera = scene_.camera;

    // ---- Sync GPU meshes with the scene ------------------------------------------
    std::unordered_map<Uuid, GpuBody> kept;
    for (const auto& rb : scene_.bodies) {
        auto it = bodies_.find(rb.id);
        GpuBody gpu = it != bodies_.end() ? std::move(it->second) : GpuBody{};
        if (gpu.meshKey != rb.meshKey || !gpu.positions) {
            uploadBody(gpu, *rb.mesh, u, true);
            gpu.meshKey = rb.meshKey;
        }
        kept.emplace(rb.id, std::move(gpu));
    }
    bodies_ = std::move(kept);

    // Sketch profile fills are cached the same way, keyed by mesh key.
    std::unordered_map<std::uint64_t, GpuBody> keptRegions;
    for (const auto& sketch : scene_.sketches) {
        for (const auto& region : sketch.regions) {
            if (!region.mesh || keptRegions.contains(region.meshKey))
                continue;
            auto it = regions_.find(region.meshKey);
            GpuBody gpu = it != regions_.end() ? std::move(it->second) : GpuBody{};
            if (!gpu.positions) {
                uploadBody(gpu, *region.mesh, u, false);
                gpu.meshKey = region.meshKey;
            }
            keptRegions.emplace(region.meshKey, std::move(gpu));
        }
    }
    regions_ = std::move(keptRegions);

    // ---- Matrices and the frame's uniforms ---------------------------------------------
    const Mat4 view = camera.viewMatrix();
    const Mat4 proj = toMat4(rhi_->clipSpaceCorrMatrix()) * camera.projectionMatrix(false);
    const Mat4 mvp = proj * view;
    const bool perspective = camera.projection == Camera::Projection::Perspective;
    const float heightPx = float(std::max(pixelSize.height(), 1));
    const auto& grid = scene_.grid;
    UniformData frame{};
    storeMatrix(frame.mvp, mvp);
    storeMatrix(frame.view, view);
    storeMatrix(frame.proj, proj);
    frame.params[0] = float(pixelSize.width());
    frame.params[1] = float(pixelSize.height());
    {
        const Vec3 e = perspective ? camera.eye() : camera.backward();
        const float eye[4] = {float(e.x), float(e.y), float(e.z), perspective ? 1.0f : 0.0f};
        std::memcpy(frame.eye, eye, sizeof eye);
        // World size of one device pixel (at view depth 1 in perspective).
        frame.camera[0] = perspective ? float(2 * std::tan(camera.fovY / 2)) / heightPx : float(camera.orthoHeight) / heightPx;
        const StudioLighting lighting;
        const Vec3 key = lighting.keyDirection(camera);
        const float light[4] = {float(key.x), float(key.y), float(key.z), 0.0f};
        std::memcpy(frame.light, light, sizeof light);
        const float lights[4] = {float(lighting.sky), float(lighting.ground), float(lighting.key), float(lighting.fill)};
        std::memcpy(frame.lights, lights, sizeof lights);
        frame.gloss[0] = float(lighting.specular);
        frame.gloss[1] = float(lighting.shininess);
        const float g[4] = {float(grid.center.x), float(grid.center.y), float(grid.minorStep), 0.0f};
        std::memcpy(frame.grid, g, sizeof g);
        const float f[4] = {float(grid.radius), float(grid.axisRadius), float(grid.eyeFadeStart), float(grid.eyeFadeEnd)};
        std::memcpy(frame.fade, f, sizeof f);
    }
    auto uniformsFor = [&](const Color& color, float lineWidthPx, float depthBiasPx, float mode) {
        UniformData d = frame;
        std::memcpy(d.color, color.data(), sizeof d.color);
        d.params[2] = lineWidthPx * dpr;
        d.params[3] = depthBiasPx * dpr;
        d.params2[0] = mode;
        return d;
    };

    std::vector<Draw> draws;
    auto meshDraw = [&](QRhiGraphicsPipeline* p, const GpuBody* b, quint32 first, quint32 count, const Color& c, float mode,
                        float bias = 0) {
        Draw d;
        d.kind = Draw::Kind::Mesh;
        d.pipeline = p;
        d.body = b;
        d.first = first;
        d.count = count;
        d.uniforms = uniformsFor(c, 0, bias, mode);
        draws.push_back(d);
    };
    auto lineDraw = [&](QRhiGraphicsPipeline* p, QRhiBuffer* buffer, quint32 first, quint32 count, const Color& c, float width,
                        float bias, float mode = 0) {
        if (count == 0 || !buffer)
            return;
        Draw d;
        d.kind = Draw::Kind::Lines;
        d.pipeline = p;
        d.lineBuffer = buffer;
        d.first = first;
        d.count = count;
        d.uniforms = uniformsFor(c, width, bias, mode);
        draws.push_back(d);
    };

    // ---- 1. Shaded bodies ------------------------------------------------------------
    for (const auto& rb : scene_.bodies) {
        const GpuBody& gpu = bodies_.at(rb.id);
        if (gpu.indexCount)
            meshDraw(meshPipeline_.get(), &gpu, 0, gpu.indexCount, rb.isPreview ? kPreviewBody : kBody, 0);
    }

    // ---- 2. Ground: contact shadows, the grid and the X/Y/Z axes (depth-tested so
    //         bodies hide them, no depth writes) ---------------------------------------
    if (grid.visible) {
        // Soft shadows where bodies rest on the ground, seen from above it.
        if ((perspective ? camera.eye().z : camera.backward().z) > 0) {
            for (const auto& rb : scene_.bodies) {
                const GpuBody& gpu = bodies_.at(rb.id);
                if (!gpu.groundIndexCount || gpu.shadowStrength <= 0)
                    continue;
                Draw d;
                d.kind = Draw::Kind::Shadow;
                d.pipeline = shadowPipeline_.get();
                d.body = &gpu;
                d.count = gpu.groundIndexCount;
                // Each of the layers is this faint, so together they reach the strength.
                const float layer = 1.0f - std::pow(1.0f - gpu.shadowStrength, 1.0f / float(kShadowLayers));
                d.uniforms = uniformsFor(withAlpha(kShadow, layer), 0, -kGroundBiasPx, 0);
                d.uniforms.fade[0] = gpu.shadowBlur;
                draws.push_back(d);
            }
        }
        std::vector<float> ground;
        appendGroundQuad(ground, {grid.center.x, grid.center.y}, {grid.radius, grid.radius});
        const quint32 bytes = quint32(ground.size() * sizeof(float));
        ensureDynamicBuffer(groundVertices_, bytes, QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(groundVertices_.get(), 0, bytes, ground.data());
        Draw d;
        d.kind = Draw::Kind::Ground;
        d.pipeline = gridPipeline_.get();
        d.first = 0;
        d.count = 6;
        d.uniforms = uniformsFor(kGridLine, 1.0f, -kGroundBiasPx, 0);
        d.uniforms.params2[1] = kGridMinorAlpha;
        d.uniforms.params2[2] = kGridMajorAlpha;
        draws.push_back(d);

        // The axes through the origin where they cross the axes' disc (the Z
        // axis as high as they reach); they fade out further than the grid.
        std::vector<float> axes;
        quint32 counts[3] = {0, 0, 0};
        for (int axis = 0; axis < 3; ++axis)
            if (const auto segment = originAxisSegment(grid, axis)) {
                appendSegment(axes, segment->first, segment->second);
                counts[axis] = 6;
            }
        const quint32 xCount = counts[0], yCount = counts[1], zCount = counts[2];
        if (!axes.empty()) {
            const quint32 axisBytes = quint32(axes.size() * sizeof(float));
            ensureDynamicBuffer(gridVertices_, axisBytes, QRhiBuffer::VertexBuffer);
            u->updateDynamicBuffer(gridVertices_.get(), 0, axisBytes, axes.data());
            lineDraw(linePipeline_.get(), gridVertices_.get(), 0, xCount, kAxisX, 1.5f, -kGroundBiasPx, 1);
            lineDraw(linePipeline_.get(), gridVertices_.get(), xCount, yCount, kAxisY, 1.5f, -kGroundBiasPx, 1);
            lineDraw(linePipeline_.get(), gridVertices_.get(), xCount + yCount, zCount, kAxisZ, 1.5f, -kGroundBiasPx, 1);
        }
    }

    // ---- 3. Face highlights -----------------------------------------------------------
    for (const auto& rb : scene_.bodies) {
        const GpuBody& gpu = bodies_.at(rb.id);
        auto faceRange = [&](int face, quint32& first, quint32& count) {
            if (face < 0 || face + 1 >= int(gpu.faceTriangleOffset.size()))
                return false;
            first = gpu.faceTriangleOffset[std::size_t(face)] * 3;
            count = (gpu.faceTriangleOffset[std::size_t(face) + 1] - gpu.faceTriangleOffset[std::size_t(face)]) * 3;
            return count > 0;
        };
        if (rb.selected)
            meshDraw(tintPipeline_.get(), &gpu, 0, gpu.indexCount, withAlpha(kAccent, 0.28f), 1);
        for (int face : rb.selectedFaces) {
            quint32 first = 0, count = 0;
            if (faceRange(face, first, count))
                meshDraw(tintPipeline_.get(), &gpu, first, count, withAlpha(kAccent, 0.42f), 1);
        }
        quint32 first = 0, count = 0;
        if (rb.hoverFace >= 0 && faceRange(rb.hoverFace, first, count))
            meshDraw(tintPipeline_.get(), &gpu, first, count, withAlpha(kAccentHover, 0.25f), 1);
        // Faces are stored contiguously by index: merge runs into one draw each.
        std::vector<int> faces = rb.highlightFaces;
        std::sort(faces.begin(), faces.end());
        for (std::size_t i = 0; i < faces.size();) {
            std::size_t j = i;
            while (j + 1 < faces.size() && faces[j + 1] == faces[j] + 1)
                ++j;
            quint32 runFirst = 0, runCount = 0, lastFirst = 0, lastCount = 0;
            const bool okFirst = faceRange(faces[i], runFirst, runCount);
            const bool okLast = faceRange(faces[j], lastFirst, lastCount);
            if (okFirst || okLast) {
                const quint32 begin = okFirst ? runFirst : lastFirst;
                const quint32 end = okLast ? lastFirst + lastCount : runFirst + runCount;
                if (end > begin)
                    meshDraw(tintPipeline_.get(), &gpu, begin, end - begin, kHistoryHighlight, 1);
            }
            i = j + 1;
        }
    }

    // ---- 4. Edges ------------------------------------------------------------------------
    for (const auto& rb : scene_.bodies) {
        const GpuBody& gpu = bodies_.at(rb.id);
        lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), 0, gpu.edgeVertexCount, kEdge, 1.25f, kEdgeBiasPx);
        for (int e : rb.selectedEdges)
            if (auto it = gpu.edgeRanges.find(e); it != gpu.edgeRanges.end())
                lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), it->second.first, it->second.second, kAccent, 3.5f,
                         kEdgeBiasPx * 2);
        if (auto it = gpu.edgeRanges.find(rb.hoverEdge); rb.hoverEdge >= 0 && it != gpu.edgeRanges.end())
            lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), it->second.first, it->second.second, kAccentHover, 3.0f,
                     kEdgeBiasPx * 2);
    }

    // ---- 5. Sketches: profile fills, curves, points --------------------------------------------
    {
        std::vector<float> lines;
        struct Batch {
            quint32 first, count;
            Color color;
            float width;
            bool onTop;
        };
        std::vector<Batch> batches;
        for (const auto& sketch : scene_.sketches) {
            for (const auto& region : sketch.regions) {
                const auto it = regions_.find(region.meshKey);
                if (it == regions_.end() || !it->second.indexCount)
                    continue;
                const float alpha = region.style == interact::SketchStyle::Selected ? 0.38f
                                  : region.style == interact::SketchStyle::Hovered  ? 0.22f
                                                                                    : 0.08f;
                meshDraw(sketch.editing ? overlayPipeline_.get() : tintPipeline_.get(), &it->second, 0, it->second.indexCount,
                         withAlpha(region.reference ? kReference : kAccent, alpha), 1, kEdgeBiasPx);
            }
            // Group segments by style so each style is one draw call.
            for (int s = 0; s <= int(interact::SketchStyle::Reference); ++s) {
                const auto style = interact::SketchStyle(s);
                const quint32 first = quint32(lines.size() / kLineVertexFloats);
                for (const auto& l : sketch.lines)
                    if (l.style == style)
                        appendSegment(lines, l.a, l.b);
                const quint32 count = quint32(lines.size() / kLineVertexFloats) - first;
                const SketchLook look = lookOf(style);
                if (count)
                    batches.push_back({first, count, look.color, look.width, sketch.editing});
                // Points are zero-length segments: the shader turns them into squares.
                const quint32 pointFirst = quint32(lines.size() / kLineVertexFloats);
                if (sketch.editing)
                    for (const auto& p : sketch.points)
                        if (p.style == style)
                            appendSegment(lines, p.position, p.position);
                const quint32 pointCount = quint32(lines.size() / kLineVertexFloats) - pointFirst;
                if (pointCount)
                    batches.push_back({pointFirst, pointCount, look.color, look.pointSize, true});
            }
        }
        if (!lines.empty()) {
            const quint32 bytes = quint32(lines.size() * sizeof(float));
            ensureDynamicBuffer(sketchVertices_, bytes, QRhiBuffer::VertexBuffer);
            u->updateDynamicBuffer(sketchVertices_.get(), 0, bytes, lines.data());
            for (const auto& b : batches)
                lineDraw(b.onTop ? overlayLinePipeline_.get() : linePipeline_.get(), sketchVertices_.get(), b.first, b.count,
                         b.color, b.width, kEdgeBiasPx * 2);
        }
    }

    // ---- 6a. Rotation rings (on top), with a dot at the current angle -----------------
    if (!scene_.rings.empty()) {
        std::vector<float> lines;
        struct RingBatch {
            quint32 first, count;
            Color color;
            float width;
        };
        std::vector<RingBatch> batches;
        for (const auto& ring : scene_.rings) {
            Color c = ring.axis == 0 ? Color{0.86f, 0.26f, 0.26f, 1.0f}
                    : ring.axis == 1 ? Color{0.24f, 0.64f, 0.30f, 1.0f}
                                     : kAccent;
            float width = 2.5f;
            if (ring.state == interact::HandleState::Hovered) {
                for (int k = 0; k < 3; ++k)
                    c[k] += (1.0f - c[k]) * 0.3f;
                width = 4.0f;
            } else if (ring.state == interact::HandleState::Active) {
                width = 4.5f;
            } else if (ring.state == interact::HandleState::Error) {
                c = kError;
            }
            const quint32 first = quint32(lines.size() / kLineVertexFloats);
            for (std::size_t k = 0; k + 1 < ring.points.size(); ++k)
                appendSegment(lines, ring.points[k], ring.points[k + 1]);
            batches.push_back({first, quint32(lines.size() / kLineVertexFloats) - first, c, width});
            const quint32 dot = quint32(lines.size() / kLineVertexFloats);
            appendSegment(lines, ring.marker, ring.marker); // zero length: drawn as a square
            batches.push_back({dot, quint32(lines.size() / kLineVertexFloats) - dot, c, 11.0f});
        }
        const quint32 bytes = quint32(lines.size() * sizeof(float));
        ensureDynamicBuffer(ringVertices_, bytes, QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(ringVertices_.get(), 0, bytes, lines.data());
        for (const auto& b : batches)
            lineDraw(overlayLinePipeline_.get(), ringVertices_.get(), b.first, b.count, b.color, b.width, 0);
    }

    // ---- 6. Manipulator arrows (always on top) --------------------------------------------
    std::vector<float> arrowPos, arrowNrm;
    std::vector<std::pair<quint32, Color>> arrowDraws;
    for (const auto& arrow : scene_.arrows) {
        const quint32 first = quint32(arrowPos.size() / 3);
        appendArrow(arrowPos, arrowNrm, arrow.anchor, arrow.direction, camera.pixelSize(arrow.anchor), scene_.arrowStyle);
        Color c = kAccent;
        if (arrow.axis == 0)
            c = {0.86f, 0.26f, 0.26f, 1.0f};
        else if (arrow.axis == 1)
            c = {0.24f, 0.64f, 0.30f, 1.0f};
        auto mix = [](Color a, float t, float target) {
            for (int k = 0; k < 3; ++k)
                a[k] = a[k] + (target - a[k]) * t;
            return a;
        };
        if (arrow.state == interact::HandleState::Hovered)
            c = arrow.axis < 0 ? kAccentHover : mix(c, 0.3f, 1.0f);
        else if (arrow.state == interact::HandleState::Active)
            c = arrow.axis < 0 ? kAccentActive : mix(c, 0.25f, 0.0f);
        else if (arrow.state == interact::HandleState::Error)
            c = kError;
        arrowDraws.push_back({first, c});
    }
    if (!arrowPos.empty()) {
        const quint32 bytes = quint32(arrowPos.size() * sizeof(float));
        ensureDynamicBuffer(arrowPositions_, bytes, QRhiBuffer::VertexBuffer);
        ensureDynamicBuffer(arrowNormals_, bytes, QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(arrowPositions_.get(), 0, bytes, arrowPos.data());
        u->updateDynamicBuffer(arrowNormals_.get(), 0, bytes, arrowNrm.data());
        const quint32 total = quint32(arrowPos.size() / 3);
        for (std::size_t i = 0; i < arrowDraws.size(); ++i) {
            const quint32 first = arrowDraws[i].first;
            const quint32 end = i + 1 < arrowDraws.size() ? arrowDraws[i + 1].first : total;
            Draw d;
            d.kind = Draw::Kind::Arrow;
            d.pipeline = overlayPipeline_.get();
            d.first = first;
            d.count = end - first;
            d.uniforms = uniformsFor(arrowDraws[i].second, 0, 0, 2);
            draws.push_back(d);
        }
    }

    // ---- Uniform slots --------------------------------------------------------------------
    if (draws.size() > uniformSlots_) {
        while (uniformSlots_ < draws.size())
            uniformSlots_ *= 2;
        uniforms_.reset(rhi_->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, uniformSlotSize_ * uniformSlots_));
        uniforms_->create();
        srb_->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms_.get(),
            sizeof(UniformData))});
        srb_->create();
    }
    for (std::size_t i = 0; i < draws.size(); ++i)
        u->updateDynamicBuffer(uniforms_.get(), quint32(i) * uniformSlotSize_, sizeof(UniformData), &draws[i].uniforms);

    // ---- Record the pass ----------------------------------------------------------------------
    const QColor clear = QColor::fromRgbF(kBackground[0], kBackground[1], kBackground[2], kBackground[3]);
    cb->beginPass(renderTarget(), clear, {1.0f, 0}, u);
    cb->setViewport(QRhiViewport(0, 0, float(pixelSize.width()), float(pixelSize.height())));
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const Draw& d = draws[i];
        cb->setGraphicsPipeline(d.pipeline);
        const QRhiCommandBuffer::DynamicOffset offset(0, quint32(i) * uniformSlotSize_);
        cb->setShaderResources(srb_.get(), 1, &offset);
        switch (d.kind) {
        case Draw::Kind::Mesh: {
            const QRhiCommandBuffer::VertexInput inputs[] = {{d.body->positions.get(), 0}, {d.body->normals.get(), 0}};
            cb->setVertexInput(0, 2, inputs, d.body->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.count, 1, d.first);
            break;
        }
        case Draw::Kind::Lines: {
            const QRhiCommandBuffer::VertexInput input(d.lineBuffer, 0);
            cb->setVertexInput(0, 1, &input);
            cb->draw(d.count, 1, d.first);
            break;
        }
        case Draw::Kind::Arrow: {
            const QRhiCommandBuffer::VertexInput inputs[] = {{arrowPositions_.get(), 0}, {arrowNormals_.get(), 0}};
            cb->setVertexInput(0, 2, inputs);
            cb->draw(d.count, 1, d.first);
            break;
        }
        case Draw::Kind::Ground: {
            const QRhiCommandBuffer::VertexInput input(groundVertices_.get(), 0);
            cb->setVertexInput(0, 1, &input);
            cb->draw(d.count, 1, d.first);
            break;
        }
        case Draw::Kind::Shadow: {
            const QRhiCommandBuffer::VertexInput input(d.body->positions.get(), 0);
            cb->setVertexInput(0, 1, &input, d.body->groundIndices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.count, kShadowLayers);
            break;
        }
        }
    }
    cb->endPass();
}

} // namespace os::render
