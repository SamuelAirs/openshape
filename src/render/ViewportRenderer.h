// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
        // The triangles it rests on the ground with (bodies only), for its
        // contact shadow; none when it does not touch the ground.
        std::unique_ptr<QRhiBuffer> groundIndices;
        quint32 groundIndexCount = 0;
        float shadowStrength = 0;
        float shadowBlur = 0;
        // edge topology index -> [firstVertex, vertexCount) in edgeVertices
        std::unordered_map<int, std::pair<quint32, quint32>> edgeRanges;
    };

    // A reference photo's picture on the GPU: uploaded once per picture
    // (its key), with mipmaps; released when the photo goes away.
    struct GpuPhoto {
        std::uint64_t key = 0;
        std::unique_ptr<QRhiTexture> texture;
        // The uniform buffer and this texture (rebuilt when the uniform buffer grows).
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        QRhiBuffer* srbUniforms = nullptr;
    };

    struct Draw;
    // Vertex layouts: body meshes (positions + normals), screen-space line
    // quads, quads on the ground (positions only: grid, contact shadows),
    // textured quads (positions + texture coordinates: reference photos).
    enum class Layout { Mesh, Line, Ground, Photo };

    void createPipelines();
    std::unique_ptr<QRhiGraphicsPipeline> makePipeline(const QShader& vs, const QShader& fs, Layout vertices, bool depthTest,
                                                      bool depthWrite, bool blend, QRhiGraphicsPipeline::CompareOp op,
                                                      QRhiShaderResourceBindings* bindings = nullptr);
    // Brings the photos' textures in line with the scene (decoding and
    // uploading new pictures, dropping those gone).
    void syncPhotos(QRhiResourceUpdateBatch* u);
    // The photo's bindings for the current uniform buffer (made or remade).
    QRhiShaderResourceBindings* photoBindings(GpuPhoto& photo);
    void uploadBody(GpuBody& gpu, const geom::Mesh& mesh, QRhiResourceUpdateBatch* u, bool castsShadow);
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
    std::unique_ptr<QRhiGraphicsPipeline> gridPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> shadowPipeline_;
    std::unique_ptr<QRhiGraphicsPipeline> photoPipeline_;
    std::unique_ptr<QRhiSampler> photoSampler_;
    std::unique_ptr<QRhiTexture> photoLayoutTexture_; // 1 x 1: only for the bindings' layout
    std::unique_ptr<QRhiShaderResourceBindings> photoLayout_;
    std::unordered_map<Uuid, GpuPhoto> photos_;
    std::unique_ptr<QRhiBuffer> photoVertices_;
    std::unique_ptr<QRhiBuffer> photoOutline_;

    std::unordered_map<Uuid, GpuBody> bodies_;
    // Sketch profile fills, keyed by mesh key.
    std::unordered_map<std::uint64_t, GpuBody> regions_;
    std::unique_ptr<QRhiBuffer> sketchVertices_;
    std::unique_ptr<QRhiBuffer> ringVertices_;
    std::unique_ptr<QRhiBuffer> gridVertices_;   // the X/Y/Z axes
    std::unique_ptr<QRhiBuffer> groundVertices_; // grid and shadow quads
    std::unique_ptr<QRhiBuffer> arrowPositions_;
    std::unique_ptr<QRhiBuffer> arrowNormals_;
};

} // namespace os::render
