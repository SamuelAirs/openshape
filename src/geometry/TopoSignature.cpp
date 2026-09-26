// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/TopoSignature.h"

#include "geometry/Modeling.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace os::geom {

namespace {

// Scale used to make centroid distances comparable across part sizes.
double shapeScale(const Shape& shape)
{
    const BoundingBox box = approximateBoundingBox(shape);
    return box.valid ? std::max(box.size().length(), 1e-3) : 1.0;
}

// Exact match check used for the fast path: the hinted index is accepted only
// if it is practically identical to the captured signature.
bool faceMatchesExactly(const FaceInfo& info, const FaceSignature& sig, double scale)
{
    return info.kind == sig.kind && info.normal.dot(sig.normal) > 0.9999
        && (info.centroid - sig.centroid).length() < 1e-6 * scale + 1e-9
        && std::abs(info.area - sig.area) <= 1e-6 * std::max(sig.area, 1.0);
}

} // namespace

std::optional<FaceSignature> captureFaceSignature(const Shape& shape, int faceIndex)
{
    const auto info = faceInfo(shape, faceIndex);
    if (!info)
        return std::nullopt;
    return FaceSignature{info->kind, info->normal, info->centroid, info->area};
}

std::optional<EdgeSignature> captureEdgeSignature(const Shape& shape, int edgeIndex)
{
    const auto info = edgeInfo(shape, edgeIndex);
    if (!info)
        return std::nullopt;
    return EdgeSignature{info->kind, info->midpoint, info->tangent, info->length};
}

std::optional<int> resolveFace(const Shape& shape, const FaceSignature& signature, int hint)
{
    if (shape.isNull())
        return std::nullopt;
    const double scale = shapeScale(shape);
    if (hint >= 0 && hint < shape.faceCount()) {
        if (const auto info = faceInfo(shape, hint); info && faceMatchesExactly(*info, signature, scale))
            return hint;
    }

    // Candidates must have the same surface kind and (for planes) the same
    // orientation. Among those, prefer the nearest centroid, then similar area.
    std::optional<int> best;
    double bestScore = std::numeric_limits<double>::max();
    for (int i = 0; i < shape.faceCount(); ++i) {
        const auto info = faceInfo(shape, i);
        if (!info || info->kind != signature.kind)
            continue;
        const double alignment = info->normal.dot(signature.normal);
        if (signature.kind == SurfaceKind::Plane && alignment < 0.999)
            continue;
        const double centroidTerm = (info->centroid - signature.centroid).length() / scale;
        const double areaTerm = std::abs(info->area - signature.area) / std::max(signature.area, 1e-9);
        const double score = centroidTerm + 0.25 * std::min(areaTerm, 4.0) + (1.0 - alignment);
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    // Reject implausible matches: the candidate moved more than the part size.
    if (best && bestScore > 1.5)
        return std::nullopt;
    return best;
}

std::optional<int> resolveEdge(const Shape& shape, const EdgeSignature& signature, int hint)
{
    if (shape.isNull())
        return std::nullopt;
    const double scale = shapeScale(shape);
    if (hint >= 0 && hint < shape.edgeCount()) {
        if (const auto info = edgeInfo(shape, hint); info && info->kind == signature.kind
            && (info->midpoint - signature.midpoint).length() < 1e-6 * scale + 1e-9
            && std::abs(std::abs(info->tangent.dot(signature.tangent)) - 1.0) < 1e-6)
            return hint;
    }
    std::optional<int> best;
    double bestScore = std::numeric_limits<double>::max();
    for (int i = 0; i < shape.edgeCount(); ++i) {
        const auto info = edgeInfo(shape, i);
        if (!info || info->kind != signature.kind)
            continue;
        const double alignment = std::abs(info->tangent.dot(signature.tangent));
        if (alignment < 0.99)
            continue;
        const double midTerm = (info->midpoint - signature.midpoint).length() / scale;
        const double lengthTerm = std::abs(info->length - signature.length) / std::max(signature.length, 1e-9);
        const double score = midTerm + 0.25 * std::min(lengthTerm, 4.0) + (1.0 - alignment);
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best && bestScore > 1.0)
        return std::nullopt;
    return best;
}

double solidDistance(const SolidSignature& a, const SolidSignature& b)
{
    const double size = std::max({(a.max - a.min).length(), (b.max - b.min).length(), 1e-6});
    const double centroidTerm = (a.centroid - b.centroid).length() / size;
    const double boxTerm = ((a.min - b.min).length() + (a.max - b.max).length()) / (2 * size);
    const double volumeTerm = std::abs(a.volume - b.volume) / std::max({a.volume, b.volume, 1e-12});
    return centroidTerm + 0.5 * boxTerm + 0.5 * volumeTerm;
}

std::vector<int> matchSolids(const std::vector<SolidSignature>& current, const std::vector<SolidSignature>& wanted)
{
    // Beyond this a piece has moved or changed more than its own size: it is
    // another piece, not this one.
    constexpr double kMaxDistance = 1.5;
    std::vector<int> result(wanted.size(), -1);
    std::vector<bool> used(current.size(), false);
    if (wanted.empty())
        return result;
    double best = kMaxDistance;
    for (std::size_t j = 0; j < current.size(); ++j)
        if (const double d = solidDistance(wanted[0], current[j]); d <= best) {
            best = d;
            result[0] = static_cast<int>(j);
        }
    if (result[0] >= 0)
        used[std::size_t(result[0])] = true;
    struct Pair {
        double distance;
        std::size_t wanted, current;
    };
    std::vector<Pair> pairs;
    for (std::size_t i = 1; i < wanted.size(); ++i)
        for (std::size_t j = 0; j < current.size(); ++j)
            if (const double d = solidDistance(wanted[i], current[j]); d <= kMaxDistance)
                pairs.push_back({d, i, j});
    std::sort(pairs.begin(), pairs.end(), [](const Pair& p, const Pair& q) {
        return p.distance != q.distance ? p.distance < q.distance
             : p.wanted != q.wanted     ? p.wanted < q.wanted
                                        : p.current < q.current;
    });
    for (const Pair& p : pairs)
        if (result[p.wanted] < 0 && !used[p.current]) {
            result[p.wanted] = static_cast<int>(p.current);
            used[p.current] = true;
        }
    return result;
}

} // namespace os::geom
