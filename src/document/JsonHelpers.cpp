// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/JsonHelpers.h"

#include <cmath>

namespace os::doc {

using nlohmann::json;

namespace {

std::optional<geom::SurfaceKind> surfaceKindFromInt(int v)
{
    if (v < 0 || v > static_cast<int>(geom::SurfaceKind::Other))
        return std::nullopt;
    return static_cast<geom::SurfaceKind>(v);
}

std::optional<geom::CurveKind> curveKindFromInt(int v)
{
    if (v < 0 || v > static_cast<int>(geom::CurveKind::Other))
        return std::nullopt;
    return static_cast<geom::CurveKind>(v);
}

} // namespace

json vecToJson(const Vec3& v)
{
    return json::array({v.x, v.y, v.z});
}

std::optional<Vec3> vecFromJson(const json& value)
{
    if (!value.is_array() || value.size() != 3)
        return std::nullopt;
    for (const auto& c : value)
        if (!c.is_number() || !std::isfinite(c.get<double>()))
            return std::nullopt;
    return Vec3{value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}

std::optional<Vec3> vecFromJson(const json& parent, const char* key)
{
    if (!parent.is_object() || !parent.contains(key))
        return std::nullopt;
    return vecFromJson(parent[key]);
}

std::optional<double> numberFrom(const json& parent, const char* key)
{
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_number())
        return std::nullopt;
    const double v = parent[key].get<double>();
    if (!std::isfinite(v))
        return std::nullopt;
    return v;
}

std::optional<int> intFrom(const json& parent, const char* key)
{
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_number_integer())
        return std::nullopt;
    return parent[key].get<int>();
}

json faceRefToJson(const FaceRef& ref)
{
    return {{"indexHint", ref.indexHint},
            {"surface", static_cast<int>(ref.signature.kind)},
            {"normal", vecToJson(ref.signature.normal)},
            {"centroid", vecToJson(ref.signature.centroid)},
            {"area", ref.signature.area}};
}

std::optional<FaceRef> faceRefFromJson(const json& parent, const char* key)
{
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_object())
        return std::nullopt;
    const json& j = parent[key];
    const auto hint = intFrom(j, "indexHint");
    const auto kindValue = intFrom(j, "surface");
    const auto normal = vecFromJson(j, "normal");
    const auto centroid = vecFromJson(j, "centroid");
    const auto area = numberFrom(j, "area");
    if (!hint || !kindValue || !normal || !centroid || !area)
        return std::nullopt;
    const auto kind = surfaceKindFromInt(*kindValue);
    if (!kind)
        return std::nullopt;
    return FaceRef{*hint, geom::FaceSignature{*kind, *normal, *centroid, *area}};
}

json edgeRefToJson(const EdgeRef& ref)
{
    return {{"indexHint", ref.indexHint},
            {"curve", static_cast<int>(ref.signature.kind)},
            {"midpoint", vecToJson(ref.signature.midpoint)},
            {"tangent", vecToJson(ref.signature.tangent)},
            {"length", ref.signature.length}};
}

std::optional<EdgeRef> edgeRefFromJson(const json& j)
{
    if (!j.is_object())
        return std::nullopt;
    const auto hint = intFrom(j, "indexHint");
    const auto kindValue = intFrom(j, "curve");
    const auto midpoint = vecFromJson(j, "midpoint");
    const auto tangent = vecFromJson(j, "tangent");
    const auto length = numberFrom(j, "length");
    if (!hint || !kindValue || !midpoint || !tangent || !length)
        return std::nullopt;
    const auto kind = curveKindFromInt(*kindValue);
    if (!kind)
        return std::nullopt;
    return EdgeRef{*hint, geom::EdgeSignature{*kind, *midpoint, *tangent, *length}};
}

} // namespace os::doc
