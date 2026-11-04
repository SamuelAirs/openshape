// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"
#include "core/Result.h"
#include "core/Uuid.h"
#include "sketch/Sketch.h"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Reference photos ("Photo 1"): a picture placed in the design at true size
// on a plane, drawn under and behind the model, semi-transparent, to model or
// sketch over (trace a part's outline, check a design against the real
// thing). Not geometry: nothing is built from it and it is never picked in
// the view (its Model panel row selects it). The picture's bytes are kept as
// they came (JPEG or PNG; the app downscales larger pictures to
// kMaxReferenceImageSide when they are added) and saved in the project
// (references/<id>.jpg|png, docs/FILE_FORMAT.md).
namespace os::doc {

class Document;

// The plane a photo lies on: an origin plane, or a construction plane (a
// datum's id). Origin planes use the frames sketches on them use: XY (the
// ground) x along X and y along Y; XZ (Front) x along X, y up (Z); YZ
// (Right) x along Y, y up (Z).
enum class ReferencePlane { XY, XZ, YZ, Datum };
std::string_view toString(ReferencePlane plane);
std::optional<ReferencePlane> referencePlaneFromString(std::string_view text);

// The longest side (pixels) a photo is kept at: larger ones are scaled down when added.
inline constexpr int kMaxReferenceImageSide = 4096;
// What a project file may hold: per photo, and all photos together. The
// loader reads no more; the writer refuses more (a plain message).
inline constexpr std::uint64_t kMaxReferenceImageBytes = 64ull * 1024 * 1024;
inline constexpr std::uint64_t kMaxReferenceImagesBytes = 512ull * 1024 * 1024;
// Values a photo may take (kept, typed, read back).
inline constexpr double kMinReferenceWidth = 0.01; // mm
inline constexpr double kMaxReferenceWidth = 1e6;  // mm
inline constexpr double kMaxReferenceOffset = 1e6; // mm, |u| and |v|
inline constexpr double kMinReferenceOpacity = 0.1;
inline constexpr double kDefaultReferenceOpacity = 0.6;
// Width of a photo whose scale is not known (the Model panel offers Set scale).
inline constexpr double kUnscaledReferenceWidth = 200.0;

// The picture's bytes, shared (copies of a photo, its undo snapshots and the
// renderer's scene share one buffer; it never changes once made).
struct ReferenceImageData {
    std::string bytes;      // the file as it is (JPEG or PNG); empty: missing from the project file
    std::string format;     // "jpg" or "png"
    int pixelWidth = 0;     // as displayed (EXIF orientation applied: the app re-encodes rotated JPEGs)
    int pixelHeight = 0;
    std::uint64_t key = 0;  // tells the renderer it is a new picture (a fresh number per buffer)
    std::string hash;       // FNV-1a 64 of `bytes`, 16 hex digits (the file format's check)
};

class ReferenceImage {
public:
    explicit ReferenceImage(Uuid id = Uuid::generate()) : id_(id) {}
    const Uuid& id() const { return id_; }

    std::string name;
    std::shared_ptr<const ReferenceImageData> image;
    ReferencePlane plane = ReferencePlane::XY;
    Uuid datum;               // plane == Datum
    Vec2 center;              // in the plane's frame, mm
    double rotation = 0;      // radians, counter-clockwise seen from the plane's front (its normal)
    double width = kUnscaledReferenceWidth; // mm; the height follows the picture's aspect
    double opacity = kDefaultReferenceOpacity; // kMinReferenceOpacity..1
    bool visible = true;
    // The picture's scale is not known (added without one): Set scale is offered.
    bool unscaled = false;

    // Height in mm (width x the picture's aspect; square without a picture).
    double height() const;
    // The picture is there (bytes that are a PNG or JPEG matching the size recorded).
    bool hasImage() const;
    // The archive entry a project file keeps the bytes in: "references/<id>.jpg|png".
    std::string entryName() const;

    // Placement only (the bytes go in their own archive entry, entryName()).
    nlohmann::json toJson() const;
    // Fails (a FileFormatError) on wrong types or values out of range. The
    // picture is read separately: `image` is left null (ProjectFile reads it).
    static Result<ReferenceImage> fromJson(const nlohmann::json& json);
    // The entry and hash a file names for the picture (fromJson keeps them).
    const std::string& loadedEntry() const { return loadedEntry_; }
    const std::string& loadedHash() const { return loadedHash_; }
    const std::string& loadedFormat() const { return loadedFormat_; }
    int loadedPixelWidth() const { return loadedWidth_; }
    int loadedPixelHeight() const { return loadedHeight_; }

private:
    Uuid id_;
    std::string loadedEntry_, loadedHash_, loadedFormat_;
    int loadedWidth_ = 0, loadedHeight_ = 0;
};

// FNV-1a (64 bit) of `bytes` as 16 lowercase hex digits.
std::string referenceHash(const std::string& bytes);
// A shared picture from bytes as they are: format "jpg" or "png" (sniffed
// from the bytes when empty), the pixel size as displayed. Fails when the
// bytes are not a PNG or JPEG, the size is not positive or beyond
// kMaxReferenceImageSide, or there are more than kMaxReferenceImageBytes.
Result<std::shared_ptr<const ReferenceImageData>> makeReferenceImageData(std::string bytes, int pixelWidth, int pixelHeight,
                                                                         std::string format = {});

// Whether a photo's values can be kept and read back from a file (width,
// opacity, offsets, rotation finite and in range). Fails with the plain
// message the Model panel shows.
Status checkReferenceValues(const ReferenceImage& reference);

// The frame of the plane the photo lies on: an origin plane's, or a
// construction plane's (doc::sketchPlaneOn, as a sketch on it). A datum that
// is gone or not a plane gives the ground's.
sketch::Plane referencePlaneFrame(const ReferenceImage& reference, const Document& document);
// The photo's frame: origin at its center, x along its bottom edge (the
// plane's x turned by `rotation`), y up the picture.
sketch::Plane referenceFrame(const ReferenceImage& reference, const Document& document);
// Its corners in world space: bottom left, bottom right, top right, top left
// (as the picture is seen from the plane's front).
std::array<Vec3, 4> referenceCorners(const ReferenceImage& reference, const Document& document);
// A point on the photo (world) in the photo's own coordinates: mm from its
// center along its bottom edge and up the picture.
Vec2 referenceLocal(const ReferenceImage& reference, const Document& document, const Vec3& world);

} // namespace os::doc
