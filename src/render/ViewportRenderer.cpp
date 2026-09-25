#include "render/ViewportRenderer.h"

#include "core/Log.h"

#include <QtCore/QFile>
#include <QtQuick/QQuickRhiItem>
#include <QtQuick/QQuickWindow>
#include <rhi/qshader.h>

#include <array>
#include <cstring>

namespace os::render {

namespace {

// std140 layout shared by all shaders (see shaders/*.vert).
struct UniformData {
    float mvp[16];
    float view[16];
    float color[4];
    float params[4];  // viewport w/h (px), line width (px), depth bias
    float params2[4]; // shading mode
};
static_assert(sizeof(UniformData) == 176);

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
constexpr Color kGridMinor{0.0f, 0.0f, 0.0f, 0.05f};
constexpr Color kGridMajor{0.0f, 0.0f, 0.0f, 0.11f};
constexpr Color kAxisX{0.86f, 0.27f, 0.27f, 0.6f};
constexpr Color kAxisY{0.27f, 0.66f, 0.33f, 0.6f};

constexpr quint32 kLineVertexFloats = 8; // p0(3) p1(3) corner(2)

Color withAlpha(Color c, float a)
{
    c[3] = a;
    return c;
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
    enum class Kind { Mesh, Lines, Arrow } kind = Kind::Mesh;
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
        meshPipeline_.reset();
        tintPipeline_.reset();
        overlayPipeline_.reset();
        linePipeline_.reset();
        overlayLinePipeline_.reset();
        srb_.reset();
        uniforms_.reset();
        gridVertices_.reset();
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

    using Op = QRhiGraphicsPipeline::CompareOp;
    meshPipeline_ = makePipeline(meshVs, meshFs, false, true, true, false, Op::Less);
    tintPipeline_ = makePipeline(meshVs, meshFs, false, true, false, true, Op::LessOrEqual);
    overlayPipeline_ = makePipeline(meshVs, meshFs, false, false, false, true, Op::Always);
    linePipeline_ = makePipeline(lineVs, lineFs, true, true, false, true, Op::LessOrEqual);
    overlayLinePipeline_ = makePipeline(lineVs, lineFs, true, false, false, true, Op::Always);
}

std::unique_ptr<QRhiGraphicsPipeline> ViewportRenderer::makePipeline(const QShader& vs, const QShader& fs, bool lines,
                                                                    bool depthTest, bool depthWrite, bool blend,
                                                                    QRhiGraphicsPipeline::CompareOp op)
{
    std::unique_ptr<QRhiGraphicsPipeline> ps(rhi_->newGraphicsPipeline());
    ps->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
    QRhiVertexInputLayout layout;
    if (lines) {
        layout.setBindings({{kLineVertexFloats * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});
    } else {
        layout.setBindings({{3 * sizeof(float)}, {3 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}, {1, 1, QRhiVertexInputAttribute::Float3, 0}});
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

void ViewportRenderer::uploadBody(GpuBody& gpu, const geom::Mesh& mesh, QRhiResourceUpdateBatch* u)
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
            uploadBody(gpu, *rb.mesh, u);
            gpu.meshKey = rb.meshKey;
        }
        kept.emplace(rb.id, std::move(gpu));
    }
    bodies_ = std::move(kept);

    // ---- Matrices --------------------------------------------------------------------
    const Mat4 view = camera.viewMatrix();
    const Mat4 mvp = toMat4(rhi_->clipSpaceCorrMatrix()) * camera.projectionMatrix(false) * view;
    auto uniformsFor = [&](const Color& color, float lineWidthPx, float depthBias, float mode) {
        UniformData d{};
        storeMatrix(d.mvp, mvp);
        storeMatrix(d.view, view);
        std::memcpy(d.color, color.data(), sizeof d.color);
        d.params[0] = float(pixelSize.width());
        d.params[1] = float(pixelSize.height());
        d.params[2] = lineWidthPx * dpr;
        d.params[3] = depthBias;
        d.params2[0] = mode;
        return d;
    };

    std::vector<Draw> draws;
    auto meshDraw = [&](QRhiGraphicsPipeline* p, const GpuBody* b, quint32 first, quint32 count, const Color& c, float mode) {
        Draw d;
        d.kind = Draw::Kind::Mesh;
        d.pipeline = p;
        d.body = b;
        d.first = first;
        d.count = count;
        d.uniforms = uniformsFor(c, 0, 0, mode);
        draws.push_back(d);
    };
    auto lineDraw = [&](QRhiGraphicsPipeline* p, QRhiBuffer* buffer, quint32 first, quint32 count, const Color& c, float width,
                        float bias) {
        if (count == 0 || !buffer)
            return;
        Draw d;
        d.kind = Draw::Kind::Lines;
        d.pipeline = p;
        d.lineBuffer = buffer;
        d.first = first;
        d.count = count;
        d.uniforms = uniformsFor(c, width, bias, 0);
        draws.push_back(d);
    };

    // ---- 1. Shaded bodies ------------------------------------------------------------
    for (const auto& rb : scene_.bodies) {
        const GpuBody& gpu = bodies_.at(rb.id);
        if (gpu.indexCount)
            meshDraw(meshPipeline_.get(), &gpu, 0, gpu.indexCount, rb.isPreview ? kPreviewBody : kBody, 0);
    }

    // ---- 2. Grid (depth-tested so bodies hide it) ------------------------------------
    {
        const auto& g = scene_.grid;
        std::vector<float> minor, major, axisX, axisY;
        const double extent = g.halfLines * g.minorStep;
        for (int i = -g.halfLines; i <= g.halfLines; ++i) {
            const double x = g.center.x + i * g.minorStep;
            const double y = g.center.y + i * g.minorStep;
            const bool isMajor = (i % 10) == 0;
            auto& target = isMajor ? major : minor;
            if (std::abs(x) > g.minorStep * 1e-6)
                appendSegment(target, {x, g.center.y - extent, 0}, {x, g.center.y + extent, 0});
            if (std::abs(y) > g.minorStep * 1e-6)
                appendSegment(target, {g.center.x - extent, y, 0}, {g.center.x + extent, y, 0});
        }
        if (std::abs(g.center.y) <= extent)
            appendSegment(axisX, {g.center.x - extent, 0, 0}, {g.center.x + extent, 0, 0});
        if (std::abs(g.center.x) <= extent)
            appendSegment(axisY, {0, g.center.y - extent, 0}, {0, g.center.y + extent, 0});

        std::vector<float> all;
        all.reserve(minor.size() + major.size() + axisX.size() + axisY.size());
        const quint32 minorFirst = 0, minorCount = quint32(minor.size() / kLineVertexFloats);
        all.insert(all.end(), minor.begin(), minor.end());
        const quint32 majorFirst = quint32(all.size() / kLineVertexFloats), majorCount = quint32(major.size() / kLineVertexFloats);
        all.insert(all.end(), major.begin(), major.end());
        const quint32 axisXFirst = quint32(all.size() / kLineVertexFloats), axisXCount = quint32(axisX.size() / kLineVertexFloats);
        all.insert(all.end(), axisX.begin(), axisX.end());
        const quint32 axisYFirst = quint32(all.size() / kLineVertexFloats), axisYCount = quint32(axisY.size() / kLineVertexFloats);
        all.insert(all.end(), axisY.begin(), axisY.end());

        if (g.visible && !all.empty()) {
            const quint32 bytes = quint32(all.size() * sizeof(float));
            ensureDynamicBuffer(gridVertices_, bytes, QRhiBuffer::VertexBuffer);
            u->updateDynamicBuffer(gridVertices_.get(), 0, bytes, all.data());
            lineDraw(linePipeline_.get(), gridVertices_.get(), minorFirst, minorCount, kGridMinor, 1.0f, 0);
            lineDraw(linePipeline_.get(), gridVertices_.get(), majorFirst, majorCount, kGridMajor, 1.0f, 0);
            lineDraw(linePipeline_.get(), gridVertices_.get(), axisXFirst, axisXCount, kAxisX, 1.5f, 0);
            lineDraw(linePipeline_.get(), gridVertices_.get(), axisYFirst, axisYCount, kAxisY, 1.5f, 0);
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
    }

    // ---- 4. Edges ------------------------------------------------------------------------
    constexpr float kEdgeBias = 0.0004f;
    for (const auto& rb : scene_.bodies) {
        const GpuBody& gpu = bodies_.at(rb.id);
        lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), 0, gpu.edgeVertexCount, kEdge, 1.25f, kEdgeBias);
        for (int e : rb.selectedEdges)
            if (auto it = gpu.edgeRanges.find(e); it != gpu.edgeRanges.end())
                lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), it->second.first, it->second.second, kAccent, 3.5f,
                         kEdgeBias * 2);
        if (auto it = gpu.edgeRanges.find(rb.hoverEdge); rb.hoverEdge >= 0 && it != gpu.edgeRanges.end())
            lineDraw(linePipeline_.get(), gpu.edgeVertices.get(), it->second.first, it->second.second, kAccentHover, 3.0f,
                     kEdgeBias * 2);
    }

    // ---- 5. Manipulator arrows (always on top) --------------------------------------------
    std::vector<float> arrowPos, arrowNrm;
    std::vector<std::pair<quint32, Color>> arrowDraws;
    for (const auto& arrow : scene_.arrows) {
        const quint32 first = quint32(arrowPos.size() / 3);
        appendArrow(arrowPos, arrowNrm, arrow.anchor, arrow.direction, camera.pixelSize(arrow.anchor), scene_.arrowStyle);
        Color c = kAccent;
        if (arrow.state == interact::HandleState::Hovered)
            c = kAccentHover;
        else if (arrow.state == interact::HandleState::Active)
            c = kAccentActive;
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
        }
    }
    cb->endPass();
}

} // namespace os::render
