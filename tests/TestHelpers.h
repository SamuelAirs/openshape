#pragma once

#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Modeling.h"

#include <gtest/gtest.h>

#include <memory>

namespace os::test {

inline std::unique_ptr<doc::BoxFeature> boxFeature(double x, double y, double z)
{
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {x, y, z};
    return box;
}

inline int faceWithNormal(const geom::Shape& s, const Vec3& n)
{
    for (int i = 0; i < s.faceCount(); ++i) {
        const auto info = geom::faceInfo(s, i);
        if (info && info->isPlanar() && info->normal.dot(n) > 0.9999)
            return i;
    }
    return -1;
}

inline std::vector<int> verticalEdges(const geom::Shape& s)
{
    std::vector<int> out;
    for (int i = 0; i < s.edgeCount(); ++i) {
        const auto info = geom::edgeInfo(s, i);
        if (info && info->kind == geom::CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1.0) < 1e-9)
            out.push_back(i);
    }
    return out;
}

inline std::unique_ptr<doc::PushPullFeature> pushPull(const geom::Shape& input, const Vec3& normal, double distance)
{
    auto f = std::make_unique<doc::PushPullFeature>();
    const int face = faceWithNormal(input, normal);
    EXPECT_GE(face, 0);
    f->face.indexHint = face;
    f->face.signature = *geom::captureFaceSignature(input, face);
    f->distance = distance;
    return f;
}

inline std::unique_ptr<doc::FilletFeature> filletVertical(const geom::Shape& input, double radius)
{
    auto f = std::make_unique<doc::FilletFeature>();
    for (int e : verticalEdges(input))
        f->edges.push_back({e, *geom::captureEdgeSignature(input, e)});
    f->size = radius;
    return f;
}

inline double height(const doc::Body& body)
{
    return geom::boundingBox(body.shape()).size().z;
}

} // namespace os::test
