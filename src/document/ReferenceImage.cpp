// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/ReferenceImage.h"

#include "document/Datum.h"
#include "document/Document.h"
#include "document/JsonHelpers.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstring>

namespace os::doc {

using nlohmann::json;

namespace {

constexpr std::pair<ReferencePlane, std::string_view> kPlaneNames[] = {
    {ReferencePlane::XY, "XY"}, {ReferencePlane::XZ, "XZ"}, {ReferencePlane::YZ, "YZ"}, {ReferencePlane::Datum, "Datum"}};

bool isPng(const std::string& b)
{
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    return b.size() >= 8 && std::memcmp(b.data(), sig, 8) == 0;
}

bool isJpeg(const std::string& b)
{
    return b.size() >= 3 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xD8
        && static_cast<unsigned char>(b[2]) == 0xFF;
}

std::uint64_t nextImageKey()
{
    static std::atomic<std::uint64_t> key{1};
    return key++;
}

} // namespace

std::string_view toString(ReferencePlane plane)
{
    for (const auto& [p, name] : kPlaneNames)
        if (p == plane)
            return name;
    return "XY";
}

std::optional<ReferencePlane> referencePlaneFromString(std::string_view text)
{
    for (const auto& [p, name] : kPlaneNames)
        if (name == text)
            return p;
    return std::nullopt;
}

std::string referenceHash(const std::string& bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i, hash >>= 4)
        out[std::size_t(i)] = digits[hash & 0xF];
    return out;
}

Result<std::shared_ptr<const ReferenceImageData>> makeReferenceImageData(std::string bytes, int pixelWidth, int pixelHeight,
                                                                         std::string format)
{
    using R = Result<std::shared_ptr<const ReferenceImageData>>;
    const bool png = isPng(bytes), jpeg = isJpeg(bytes);
    if (format.empty())
        format = png ? "png" : jpeg ? "jpg" : "";
    if ((format == "png" && !png) || (format == "jpg" && !jpeg) || format.empty())
        return R::failure(ErrorCode::InvalidArgument, "This picture could not be read. Use a JPEG or PNG photo.",
                          "reference image: not a " + (format.empty() ? std::string("PNG or JPEG") : format));
    if (bytes.size() > kMaxReferenceImageBytes)
        return R::failure(ErrorCode::InvalidArgument, "This picture is too large to keep in a project.",
                          "reference image: " + std::to_string(bytes.size()) + " bytes");
    if (pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > kMaxReferenceImageSide || pixelHeight > kMaxReferenceImageSide)
        return R::failure(ErrorCode::InvalidArgument, "This picture could not be read. Use a JPEG or PNG photo.",
                          "reference image: size " + std::to_string(pixelWidth) + "x" + std::to_string(pixelHeight));
    auto data = std::make_shared<ReferenceImageData>();
    data->hash = referenceHash(bytes);
    data->bytes = std::move(bytes);
    data->format = std::move(format);
    data->pixelWidth = pixelWidth;
    data->pixelHeight = pixelHeight;
    data->key = nextImageKey();
    return R::success(std::move(data));
}

double ReferenceImage::height() const
{
    int w = image ? image->pixelWidth : loadedWidth_;
    int h = image ? image->pixelHeight : loadedHeight_;
    if (w <= 0 || h <= 0)
        w = h = 1;
    return width * double(h) / double(w);
}

bool ReferenceImage::hasImage() const
{
    return image && !image->bytes.empty() && image->pixelWidth > 0 && image->pixelHeight > 0;
}

std::string ReferenceImage::entryName() const
{
    const std::string format = image && !image->format.empty() ? image->format : loadedFormat_.empty() ? "png" : loadedFormat_;
    return "references/" + id_.toString() + "." + format;
}

json ReferenceImage::toJson() const
{
    const int w = image ? image->pixelWidth : loadedWidth_;
    const int h = image ? image->pixelHeight : loadedHeight_;
    json out{{"id", id_.toString()},
             {"name", name},
             {"visible", visible},
             {"plane", std::string(toString(plane))},
             {"center", json::array({center.x, center.y})},
             {"rotation", rotation},
             {"width", width},
             {"opacity", opacity},
             {"pixelSize", json::array({w, h})}};
    if (plane == ReferencePlane::Datum)
        out["datum"] = datum.toString();
    if (unscaled)
        out["unscaled"] = true;
    // A picture missing from the file it came from is written without an entry.
    if (hasImage()) {
        out["image"] = entryName();
        out["format"] = image->format;
        out["hash"] = image->hash;
    }
    return out;
}

Result<ReferenceImage> ReferenceImage::fromJson(const json& j)
{
    auto bad = [](const std::string& why) {
        return Result<ReferenceImage>::failure(ErrorCode::FileFormatError, "The file contains an invalid reference photo.",
                                               "reference: " + why);
    };
    if (!j.is_object() || !j.contains("id") || !j["id"].is_string())
        return bad("missing id");
    const auto id = Uuid::parse(j["id"].get<std::string>());
    if (!id || id->isNil())
        return bad("invalid id");
    ReferenceImage r(*id);
    if (j.contains("name")) {
        if (!j["name"].is_string())
            return bad("name is not a string");
        r.name = j["name"].get<std::string>();
        if (r.name.size() > 1024)
            return bad("name too long");
    }
    if (j.contains("visible")) {
        if (!j["visible"].is_boolean())
            return bad("visible is not a boolean");
        r.visible = j["visible"].get<bool>();
    }
    if (j.contains("unscaled")) {
        if (!j["unscaled"].is_boolean())
            return bad("unscaled is not a boolean");
        r.unscaled = j["unscaled"].get<bool>();
    }
    if (!j.contains("plane") || !j["plane"].is_string())
        return bad("missing plane");
    const auto plane = referencePlaneFromString(j["plane"].get<std::string>());
    if (!plane)
        return Result<ReferenceImage>::failure(ErrorCode::FileVersionUnsupported,
                                               "This project places a reference photo in a way this version of FoldForm does not support.",
                                               "unknown reference plane '" + j["plane"].get<std::string>() + "'");
    r.plane = *plane;
    if (r.plane == ReferencePlane::Datum) {
        if (!j.contains("datum") || !j["datum"].is_string())
            return bad("missing datum");
        const auto datum = Uuid::parse(j["datum"].get<std::string>());
        if (!datum || datum->isNil())
            return bad("invalid datum");
        r.datum = *datum;
    } else if (j.contains("datum")) {
        return bad("datum on an origin plane");
    }
    if (!j.contains("center") || !j["center"].is_array() || j["center"].size() != 2 || !j["center"][0].is_number()
        || !j["center"][1].is_number())
        return bad("invalid center");
    r.center = {j["center"][0].get<double>(), j["center"][1].get<double>()};
    const auto rotation = numberFrom(j, "rotation");
    const auto width = numberFrom(j, "width");
    const auto opacity = numberFrom(j, "opacity");
    if (!rotation || !width || !opacity)
        return bad("missing rotation, width or opacity");
    r.rotation = *rotation;
    r.width = *width;
    r.opacity = *opacity;
    if (!j.contains("pixelSize") || !j["pixelSize"].is_array() || j["pixelSize"].size() != 2
        || !j["pixelSize"][0].is_number_integer() || !j["pixelSize"][1].is_number_integer())
        return bad("invalid pixelSize");
    const auto w = j["pixelSize"][0].get<std::int64_t>(), h = j["pixelSize"][1].get<std::int64_t>();
    if (w <= 0 || h <= 0 || w > kMaxReferenceImageSide || h > kMaxReferenceImageSide)
        return bad("pixelSize out of range");
    r.loadedWidth_ = int(w);
    r.loadedHeight_ = int(h);
    if (j.contains("image")) {
        if (!j["image"].is_string() || !j.contains("format") || !j["format"].is_string() || !j.contains("hash")
            || !j["hash"].is_string())
            return bad("image without format or hash");
        r.loadedEntry_ = j["image"].get<std::string>();
        r.loadedFormat_ = j["format"].get<std::string>();
        r.loadedHash_ = j["hash"].get<std::string>();
        if (r.loadedFormat_ != "png" && r.loadedFormat_ != "jpg")
            return bad("unknown format");
        if (r.loadedEntry_.rfind("references/", 0) != 0)
            return bad("image entry outside references/");
    }
    if (Status s = checkReferenceValues(r); !s)
        return bad(s.developerMessage());
    return Result<ReferenceImage>::success(std::move(r));
}

Status checkReferenceValues(const ReferenceImage& r)
{
    auto fail = [](const std::string& user, const std::string& dev) {
        return Status::failure(ErrorCode::InvalidArgument, user, dev);
    };
    if (!std::isfinite(r.width) || r.width < kMinReferenceWidth || r.width > kMaxReferenceWidth)
        return fail("Enter a width above 0 and up to 1000 m.", "reference width " + std::to_string(r.width));
    if (!std::isfinite(r.opacity) || r.opacity < kMinReferenceOpacity - 1e-9 || r.opacity > 1 + 1e-9)
        return fail("Enter an opacity from 10 to 100 %.", "reference opacity " + std::to_string(r.opacity));
    if (!std::isfinite(r.center.x) || !std::isfinite(r.center.y) || std::abs(r.center.x) > kMaxReferenceOffset
        || std::abs(r.center.y) > kMaxReferenceOffset)
        return fail("Enter a position within 1000 m of the origin.", "reference center out of range");
    if (!std::isfinite(r.rotation) || std::abs(r.rotation) > 4 * kPi)
        return fail("Enter an angle from -360 to 360\xC2\xB0.", "reference rotation " + std::to_string(r.rotation));
    return okStatus();
}

sketch::Plane referencePlaneFrame(const ReferenceImage& r, const Document& document)
{
    switch (r.plane) {
    case ReferencePlane::XY: return sketch::Plane::xy();
    case ReferencePlane::XZ: return sketch::Plane{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    case ReferencePlane::YZ: return sketch::Plane{{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    case ReferencePlane::Datum:
        if (const Datum* d = document.datum(r.datum); d && d->kind() == DatumKind::Plane)
            return sketchPlaneOn(d->geometry());
        return sketch::Plane::xy();
    }
    return sketch::Plane::xy();
}

sketch::Plane referenceFrame(const ReferenceImage& r, const Document& document)
{
    const sketch::Plane plane = referencePlaneFrame(r, document);
    const double c = std::cos(r.rotation), s = std::sin(r.rotation);
    sketch::Plane frame;
    frame.origin = plane.toWorld(r.center);
    frame.xAxis = plane.xAxis * c + plane.yAxis * s;
    frame.yAxis = plane.yAxis * c - plane.xAxis * s;
    return frame;
}

std::array<Vec3, 4> referenceCorners(const ReferenceImage& r, const Document& document)
{
    const sketch::Plane f = referenceFrame(r, document);
    const double hw = r.width / 2, hh = r.height() / 2;
    return {f.toWorld({-hw, -hh}), f.toWorld({hw, -hh}), f.toWorld({hw, hh}), f.toWorld({-hw, hh})};
}

Vec2 referenceLocal(const ReferenceImage& r, const Document& document, const Vec3& world)
{
    return referenceFrame(r, document).toLocal(world);
}

} // namespace os::doc
