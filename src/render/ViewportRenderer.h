#pragma once

#include "interaction/RenderScene.h"

#include <QtQuick/QQuickRhiItemRenderer>
#include <rhi/qrhi.h>

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace os::render {

// Draws a RenderScene with QRhi (Direct3D 11/12, Vulkan, Metal or OpenGL,
// whichever Qt Quick is using). Owns GPU copies of body meshes, keyed by the
// scene's mesh keys so unchanged bodies are never re-uploaded.
class ViewportRenderer final : public QQuickRhiItemRenderer {
public:
    using SceneProvider = std::function<interact::RenderScene()>;
    explicit ViewportRenderer(SceneProvider provider) : provider_(std::move(provider)) {}
    ~ViewportRenderer() override;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    struct GpuBody {
        std::uint64_t meshKey = 0;
        std::unique_ptr<QRhiBuffer> positions;
        std::unique_ptr<QRhiBuffer> normals;
        std::unique_ptr<QRhiBuffer> indices;
        std::unique_ptr<QRhiBuffer> edgeVertices;
        quint32 indexCount = 0;
        quint32 edgeVertexCount = 0;
        std::vector<quint32> faceTriangleOffset;
        // edge topology index -> [firstVertex, vertexCount) in edgeVertices
        std::unordered_map<int, std::pair<quint32, quint32>> edgeRanges;
    };

    struct Draw;

    void createPipelines();
    std::unique_ptr<QRhiGraphicsPipeline> makePipeline(const QShader& vs, const QShader& fs, bool lines, bool depthTest,
                                                      bool depthWrite, bool blend, QRhiGraphicsPipeline::CompareOp op);
    void uploadBody(GpuBody& gpu, const geom::Mesh& mesh, QRhiResourceUpdateBatch* u);
    void ensureDynamicBuffer(std::unique_ptr<QRhiBuffer>& buffer, quint32 size, QRhiBuffer::UsageFlags usage);

    SceneProvider provider_;
    interact::RenderScene scene_;
    float devicePixelRatio_ = 1.0f;

    QRhi* rhi_ = nullptr;
    int sampleCount_ = 0;
    std::unique_ptr<QRhiBuffer> uniforms_;
    quint32 uniformSlotSize_ = 0;
    quint32 uniformSlots_ = 0;
    std::unique_ptr<QRhiShaderResourceBindings> srb_;
    std::unique_ptr<QRhiGraphicsPipeline> meshPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> tintPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> overlayPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> linePipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> overlayLinePipeline_;

    std::unordered_map<Uuid, GpuBody> bodies_;
    // Sketch profile fills, keyed by mesh key.
    std::unordered_map<std::uint64_t, GpuBody> regions_;
    std::unique_ptr<QRhiBuffer> sketchVertices_;
    std::unique_ptr<QRhiBuffer> ringVertices_;
    std::unique_ptr<QRhiBuffer> gridVertices_;
    std::unique_ptr<QRhiBuffer> arrowPositions_;
    std::unique_ptr<QRhiBuffer> arrowNormals_;
};

} // namespace os::render
