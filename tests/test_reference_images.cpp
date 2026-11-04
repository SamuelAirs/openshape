// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference photos (doc::ReferenceImage): placement math, commands and undo,
// the project file (bytes and placement round trip; a missing or damaged
// picture opens without it), and what deleting their construction plane does.

#include "TestHelpers.h"

#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace os;

namespace {

// A real 2 x 1 PNG (the document only checks the signature; the renderer decodes it).
std::string pngBytes()
{
    static const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x7B, 0x40, 0xE8, 0xDD, 0x00, 0x00, 0x00, 0x0F, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x08, 0x00, 0x00, 0x1E, 0xF5, 0x05, 0xFB, 0x3C, 0x2B, 0x4A, 0x6E,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    return std::string(reinterpret_cast<const char*>(png), sizeof png);
}

std::string jpegLikeBytes()
{
    std::string b = "\xFF\xD8\xFF\xE0";
    b += std::string(300, '\x5A');
    b += "\xFF\xD9";
    return b;
}

doc::ReferenceImage photo(const std::string& bytes, int w, int h, const std::string& format = {})
{
    doc::ReferenceImage r;
    auto data = doc::makeReferenceImageData(bytes, w, h, format);
    EXPECT_TRUE(data.ok()) << data.developerMessage();
    if (data)
        r.image = data.value();
    r.name = "Photo 1";
    return r;
}

std::filesystem::path tempPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_test_" + name);
}

std::string readZipEntry(const std::filesystem::path& p, const std::string& name)
{
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_RDONLY, &err);
    if (!z)
        return {};
    std::string out;
    zip_stat_t st;
    if (zip_stat(z, name.c_str(), 0, &st) == 0) {
        zip_file_t* f = zip_fopen(z, name.c_str(), 0);
        out.resize(st.size);
        zip_fread(f, out.data(), st.size);
        zip_fclose(f);
    }
    zip_close(z);
    return out;
}

void writeZip(const std::filesystem::path& p, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::filesystem::remove(p);
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_NE(z, nullptr);
    for (const auto& [name, data] : entries) {
        zip_source_t* s = zip_source_buffer(z, data.data(), data.size(), 0);
        ASSERT_GE(zip_file_add(z, name.c_str(), s, ZIP_FL_OVERWRITE), 0);
    }
    ASSERT_EQ(zip_close(z), 0);
}

void expectNear(const Vec3& a, const Vec3& b, const char* what)
{
    EXPECT_NEAR(a.x, b.x, 1e-9) << what;
    EXPECT_NEAR(a.y, b.y, 1e-9) << what;
    EXPECT_NEAR(a.z, b.z, 1e-9) << what;
}

} // namespace

TEST(ReferenceImage, PlacementOnOriginPlanes)
{
    doc::Document d;
    doc::ReferenceImage r = photo(pngBytes(), 2, 1);
    r.width = 100; // 2 x 1 pixels: 100 x 50 mm
    r.center = {10, 20};
    EXPECT_DOUBLE_EQ(r.height(), 50.0);
    auto c = doc::referenceCorners(r, d);
    expectNear(c[0], {-40, -5, 0}, "ground: bottom left");
    expectNear(c[2], {60, 45, 0}, "ground: top right");

    // Front (XZ): x along X, up along Z; turned a quarter turn counter-clockwise.
    r.plane = doc::ReferencePlane::XZ;
    r.rotation = kPi / 2;
    c = doc::referenceCorners(r, d);
    // Its bottom edge now runs up (+Z), its "up" towards -X.
    expectNear(c[0], {10 + 25, 0, 20 - 50}, "front, turned: bottom left");
    expectNear(c[1], {10 + 25, 0, 20 + 50}, "front, turned: bottom right");
    const Vec2 local = doc::referenceLocal(r, d, c[2]);
    EXPECT_NEAR(local.x, 50, 1e-9);
    EXPECT_NEAR(local.y, 25, 1e-9);

    // Right (YZ): x along Y.
    r.plane = doc::ReferencePlane::YZ;
    r.rotation = 0;
    c = doc::referenceCorners(r, d);
    expectNear(c[0], {0, -40, -5}, "right: bottom left");
}

TEST(ReferenceImage, RejectsBadPicturesAndValues)
{
    EXPECT_FALSE(doc::makeReferenceImageData("not a picture", 10, 10).ok());
    EXPECT_FALSE(doc::makeReferenceImageData(pngBytes(), 0, 10).ok());
    EXPECT_FALSE(doc::makeReferenceImageData(pngBytes(), 5000, 10).ok()) << "beyond kMaxReferenceImageSide";
    EXPECT_FALSE(doc::makeReferenceImageData(pngBytes(), 2, 1, "jpg").ok()) << "a PNG named a JPEG";
    auto jpeg = doc::makeReferenceImageData(jpegLikeBytes(), 4, 3);
    ASSERT_TRUE(jpeg.ok());
    EXPECT_EQ(jpeg.value()->format, "jpg");

    doc::ReferenceImage r = photo(pngBytes(), 2, 1);
    EXPECT_TRUE(doc::checkReferenceValues(r).ok());
    r.opacity = 0.05;
    EXPECT_FALSE(doc::checkReferenceValues(r).ok());
    r.opacity = 0.6;
    r.width = 0;
    EXPECT_FALSE(doc::checkReferenceValues(r).ok());
    r.width = std::nan("");
    EXPECT_FALSE(doc::checkReferenceValues(r).ok());
}

TEST(ReferenceImage, CommandsUndoRedo)
{
    doc::Document d;
    cmd::UndoStack stack;
    doc::ReferenceImage r = photo(pngBytes(), 2, 1);
    const Uuid id = r.id();
    ASSERT_TRUE(stack.push(std::make_unique<cmd::AddReferenceImageCommand>(r), d).ok());
    ASSERT_EQ(d.references().size(), 1u);
    EXPECT_EQ(d.nextReferenceName(), "Photo 2");

    doc::ReferenceImage wider = *d.reference(id);
    wider.width = 321;
    wider.opacity = 0.3;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::EditReferenceImageCommand>(wider, "Change"), d).ok());
    EXPECT_DOUBLE_EQ(d.reference(id)->width, 321);
    // The picture is shared, never copied.
    EXPECT_EQ(d.reference(id)->image.get(), r.image.get());

    doc::ReferenceImage bad = wider;
    bad.opacity = 2;
    EXPECT_FALSE(stack.push(std::make_unique<cmd::EditReferenceImageCommand>(bad, "Bad"), d).ok());
    EXPECT_DOUBLE_EQ(d.reference(id)->opacity, 0.3) << "a refused edit changes nothing";

    ASSERT_TRUE(stack.push(std::make_unique<cmd::DeleteReferenceImageCommand>(id), d).ok());
    EXPECT_TRUE(d.references().empty());
    stack.undo(d);
    ASSERT_EQ(d.references().size(), 1u);
    EXPECT_DOUBLE_EQ(d.reference(id)->width, 321);
    stack.undo(d);
    EXPECT_DOUBLE_EQ(d.reference(id)->width, r.width);
    stack.undo(d);
    EXPECT_TRUE(d.references().empty());
    ASSERT_TRUE(stack.redo(d).ok());
    ASSERT_TRUE(stack.redo(d).ok());
    EXPECT_DOUBLE_EQ(d.reference(id)->width, 321);

    // A snapshot carries the photos (sharing the picture).
    const auto snap = d.snapshot();
    ASSERT_EQ(snap->references().size(), 1u);
    EXPECT_EQ(snap->references()[0]->image.get(), r.image.get());
}

TEST(ReferenceImage, DeletingItsPlaneMovesItToTheGround)
{
    doc::Document d;
    cmd::UndoStack stack;
    doc::Datum plane;
    plane.method = doc::DatumMethod::PlaneOffset;
    plane.originIndex = 2; // XY
    plane.distance = 30;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::AddDatumCommand>(plane), d).ok());
    doc::ReferenceImage r = photo(pngBytes(), 2, 1);
    r.plane = doc::ReferencePlane::Datum;
    r.datum = plane.id();
    r.width = 40;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::AddReferenceImageCommand>(r), d).ok());
    EXPECT_NEAR(doc::referenceCorners(*d.reference(r.id()), d)[0].z, 30, 1e-9) << "on the plane 30 mm up";
    // Moving the plane moves the photo, and its revision says so.
    const auto before = d.referenceRevision();
    doc::Datum higher = *d.datum(plane.id());
    higher.distance = 50;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::EditDatumCommand>(higher, "Change"), d).ok());
    EXPECT_NEAR(doc::referenceCorners(*d.reference(r.id()), d)[0].z, 50, 1e-9);
    EXPECT_NE(d.referenceRevision(), before);

    auto del = std::make_unique<cmd::DeleteDatumCommand>(plane.id());
    auto* raw = del.get();
    ASSERT_TRUE(stack.push(std::move(del), d).ok());
    EXPECT_EQ(raw->movedReferences(), 1u);
    EXPECT_EQ(d.reference(r.id())->plane, doc::ReferencePlane::XY);
    EXPECT_NEAR(doc::referenceCorners(*d.reference(r.id()), d)[0].z, 0, 1e-9);
    stack.undo(d);
    EXPECT_EQ(d.reference(r.id())->plane, doc::ReferencePlane::Datum);
    EXPECT_NEAR(doc::referenceCorners(*d.reference(r.id()), d)[0].z, 50, 1e-9);
}

TEST(ReferenceImage, ProjectRoundTripKeepsBytesAndPlacement)
{
    doc::Document d;
    doc::Datum plane;
    plane.method = doc::DatumMethod::PlaneOffset;
    plane.originIndex = 1; // XZ
    plane.distance = -5;
    d.addDatum(std::make_unique<doc::Datum>(plane));
    doc::ReferenceImage a = photo(pngBytes(), 2, 1);
    a.width = 123.25;
    a.center = {-7.5, 12};
    a.rotation = 0.3;
    a.opacity = 0.45;
    a.visible = false;
    doc::ReferenceImage b = photo(jpegLikeBytes(), 4, 3);
    b.name = "Bracket";
    b.plane = doc::ReferencePlane::Datum;
    b.datum = plane.id();
    b.unscaled = true;
    d.addReference(std::make_unique<doc::ReferenceImage>(a));
    d.addReference(std::make_unique<doc::ReferenceImage>(b));

    const auto path = tempPath("references.openshape");
    ASSERT_TRUE(io::saveProject(d, path).ok());
    EXPECT_EQ(readZipEntry(path, "references/" + a.id().toString() + ".png"), pngBytes());
    EXPECT_EQ(readZipEntry(path, "references/" + b.id().toString() + ".jpg"), jpegLikeBytes());

    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    const auto& refs = loaded.value()->references();
    ASSERT_EQ(refs.size(), 2u);
    const doc::ReferenceImage& la = *refs[0];
    EXPECT_EQ(la.id(), a.id());
    EXPECT_EQ(la.name, "Photo 1");
    ASSERT_TRUE(la.hasImage());
    EXPECT_EQ(la.image->bytes, pngBytes());
    EXPECT_EQ(la.image->format, "png");
    EXPECT_EQ(la.image->pixelWidth, 2);
    EXPECT_EQ(la.image->pixelHeight, 1);
    EXPECT_DOUBLE_EQ(la.width, 123.25);
    EXPECT_DOUBLE_EQ(la.center.x, -7.5);
    EXPECT_DOUBLE_EQ(la.center.y, 12);
    EXPECT_DOUBLE_EQ(la.rotation, 0.3);
    EXPECT_DOUBLE_EQ(la.opacity, 0.45);
    EXPECT_FALSE(la.visible);
    EXPECT_FALSE(la.unscaled);
    EXPECT_EQ(la.plane, doc::ReferencePlane::XY);
    const doc::ReferenceImage& lb = *refs[1];
    EXPECT_EQ(lb.name, "Bracket");
    EXPECT_EQ(lb.plane, doc::ReferencePlane::Datum);
    EXPECT_EQ(lb.datum, plane.id());
    EXPECT_TRUE(lb.unscaled);
    ASSERT_TRUE(lb.hasImage());
    EXPECT_EQ(lb.image->bytes, jpegLikeBytes());
    // The same corners in the document read back.
    const auto ca = doc::referenceCorners(b, d), cb = doc::referenceCorners(lb, *loaded.value());
    for (int k = 0; k < 4; ++k)
        expectNear(ca[std::size_t(k)], cb[std::size_t(k)], "same place after reopening");
    std::filesystem::remove(path);
}

TEST(ReferenceImage, FilesWithoutPhotosAreAsBefore)
{
    doc::Document d;
    const auto json = io::documentToJson(d);
    EXPECT_FALSE(json.contains("references"));
}

TEST(ReferenceImage, MissingOrDamagedPictureOpensWithoutIt)
{
    doc::Document d;
    doc::ReferenceImage a = photo(pngBytes(), 2, 1);
    doc::ReferenceImage b = photo(pngBytes(), 2, 1);
    b.name = "Photo 2";
    d.addReference(std::make_unique<doc::ReferenceImage>(a));
    d.addReference(std::make_unique<doc::ReferenceImage>(b));
    const std::string json = io::documentToJson(d).dump();
    std::string damaged = pngBytes();
    damaged[20] ^= 0x55;
    const auto path = tempPath("references_damaged.openshape");
    // a's entry is missing, b's does not match its hash.
    writeZip(path, {{"document.json", json}, {"references/" + b.id().toString() + ".png", damaged}});
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    ASSERT_EQ(loaded.value()->references().size(), 2u);
    EXPECT_FALSE(loaded.value()->references()[0]->hasImage());
    EXPECT_FALSE(loaded.value()->references()[1]->hasImage());
    EXPECT_DOUBLE_EQ(loaded.value()->references()[0]->height(), a.height()) << "keeps its shape from the recorded size";

    // Saved again (without the pictures it lacks) and reopened: still there.
    const auto again = tempPath("references_again.openshape");
    ASSERT_TRUE(io::saveProject(*loaded.value(), again).ok());
    auto reloaded = io::loadProject(again);
    ASSERT_TRUE(reloaded.ok()) << reloaded.developerMessage();
    EXPECT_EQ(reloaded.value()->references().size(), 2u);
    std::filesystem::remove(path);
    std::filesystem::remove(again);
}

TEST(ReferenceImage, InvalidJsonIsRefused)
{
    doc::ReferenceImage r = photo(pngBytes(), 2, 1);
    nlohmann::json good = r.toJson();
    EXPECT_TRUE(doc::ReferenceImage::fromJson(good).ok());
    auto broken = [&](const char* key, nlohmann::json value) {
        nlohmann::json j = good;
        j[key] = value;
        return doc::ReferenceImage::fromJson(j).ok();
    };
    EXPECT_FALSE(broken("width", "wide"));
    EXPECT_FALSE(broken("opacity", 5));
    EXPECT_FALSE(broken("center", nlohmann::json::array({1})));
    EXPECT_FALSE(broken("pixelSize", nlohmann::json::array({0, 4})));
    EXPECT_FALSE(broken("image", "../evil.png"));
    EXPECT_FALSE(broken("visible", "yes"));
    auto unknownPlane = doc::ReferenceImage::fromJson([&] {
        nlohmann::json j = good;
        j["plane"] = "Sphere";
        return j;
    }());
    EXPECT_FALSE(unknownPlane.ok());
    EXPECT_EQ(unknownPlane.error(), ErrorCode::FileVersionUnsupported);
    // A photo on a plane the file does not have lands on the ground.
    doc::Document d;
    nlohmann::json onMissing = good;
    onMissing["plane"] = "Datum";
    onMissing["datum"] = Uuid::generate().toString();
    nlohmann::json root = io::documentToJson(d);
    root["references"] = nlohmann::json::array({onMissing});
    auto loaded = io::documentFromJson(root);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    EXPECT_EQ(loaded.value()->references()[0]->plane, doc::ReferencePlane::XY);
}
