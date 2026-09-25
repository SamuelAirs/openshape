#include "io/Export3mf.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/Tessellation.h"

#include <zip.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace os::io {

namespace {

std::string xmlEscape(const std::string& text)
{
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

Status writeFailure(const std::string& dev)
{
    return Status::failure(ErrorCode::FileWriteError, "Unable to write the 3MF file. Check the location is writable.", dev);
}

} // namespace

WeldedMesh weldedMesh(const geom::Shape& shape, const ThreeMfOptions& options)
{
    geom::TessellationParams params;
    params.adaptive = false;
    params.linearDeflection = options.linearDeflection;
    params.angularDeflection = options.angularDeflection;
    const geom::Mesh mesh = geom::tessellate(shape, params);

    // Weld coincident vertices (faces are tessellated separately but share
    // edge nodes exactly, since edges are discretized once).
    WeldedMesh out;
    std::map<std::tuple<long long, long long, long long>, std::uint32_t> index;
    std::vector<std::uint32_t> remap(mesh.vertexCount());
    const double quantum = 1e-6; // mm
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const Vec3 v = mesh.vertex(i);
        const auto key = std::make_tuple(std::llround(v.x / quantum), std::llround(v.y / quantum), std::llround(v.z / quantum));
        auto [it, inserted] = index.emplace(key, static_cast<std::uint32_t>(out.vertices.size()));
        if (inserted)
            out.vertices.push_back(v);
        remap[i] = it->second;
    }
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const std::array<std::uint32_t, 3> tri{remap[mesh.indices[3 * t]], remap[mesh.indices[3 * t + 1]],
                                               remap[mesh.indices[3 * t + 2]]};
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
            continue; // degenerate after welding
        out.triangles.push_back(tri);
    }
    return out;
}

Status export3mf(const std::vector<geom::NamedShape>& shapes, const std::filesystem::path& path, const ThreeMfOptions& options)
{
    if (shapes.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "export3mf: no shapes");
    ScopedTimer timer("export3mf");

    std::ostringstream model;
    model.precision(9);
    model << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          << "<model unit=\"millimeter\" xml:lang=\"en-US\" "
             "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
          << " <metadata name=\"Application\">OpenShape</metadata>\n"
          << " <resources>\n";
    int id = 1;
    std::vector<int> ids;
    for (const auto& named : shapes) {
        const WeldedMesh mesh = weldedMesh(named.shape, options);
        if (mesh.triangles.empty())
            continue;
        model << "  <object id=\"" << id << "\" type=\"model\" name=\"" << xmlEscape(named.name) << "\">\n"
              << "   <mesh>\n    <vertices>\n";
        for (const Vec3& v : mesh.vertices)
            model << "     <vertex x=\"" << v.x << "\" y=\"" << v.y << "\" z=\"" << v.z << "\"/>\n";
        model << "    </vertices>\n    <triangles>\n";
        for (const auto& t : mesh.triangles)
            model << "     <triangle v1=\"" << t[0] << "\" v2=\"" << t[1] << "\" v3=\"" << t[2] << "\"/>\n";
        model << "    </triangles>\n   </mesh>\n  </object>\n";
        ids.push_back(id++);
    }
    if (ids.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to export.", "export3mf: all meshes empty");
    model << " </resources>\n <build>\n";
    for (int objectId : ids)
        model << "  <item objectid=\"" << objectId << "\"/>\n";
    model << " </build>\n</model>\n";

    const std::string contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";

    // Build the archive in memory, then write it with std::ofstream (wide paths).
    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_source_t* memory = zip_source_buffer_create(nullptr, 0, 0, &zerr);
    if (!memory) {
        zip_error_fini(&zerr);
        return writeFailure("zip_source_buffer_create failed");
    }
    zip_t* archive = zip_open_from_source(memory, ZIP_TRUNCATE, &zerr);
    zip_error_fini(&zerr);
    if (!archive) {
        zip_source_free(memory);
        return writeFailure("zip_open_from_source failed");
    }
    zip_source_keep(memory);
    const std::string modelText = model.str();
    const std::pair<const char*, const std::string*> entries[] = {
        {"[Content_Types].xml", &contentTypes}, {"_rels/.rels", &rels}, {"3D/3dmodel.model", &modelText}};
    for (const auto& [name, data] : entries) {
        void* copy = std::malloc(data->size());
        if (!copy) {
            zip_discard(archive);
            zip_source_free(memory);
            return writeFailure("out of memory");
        }
        std::memcpy(copy, data->data(), data->size());
        zip_source_t* source = zip_source_buffer(archive, copy, data->size(), 1);
        if (!source || zip_file_add(archive, name, source, ZIP_FL_OVERWRITE) < 0) {
            if (source)
                zip_source_free(source);
            else
                std::free(copy);
            zip_discard(archive);
            zip_source_free(memory);
            return writeFailure(std::string("zip_file_add failed for ") + name);
        }
    }
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        zip_source_free(memory);
        return writeFailure("zip_close failed");
    }
    std::string bytes;
    if (zip_source_open(memory) == 0) {
        zip_source_seek(memory, 0, SEEK_END);
        const zip_int64_t size = zip_source_tell(memory);
        zip_source_seek(memory, 0, SEEK_SET);
        if (size > 0) {
            bytes.resize(static_cast<std::size_t>(size));
            zip_source_read(memory, bytes.data(), static_cast<zip_uint64_t>(size));
        }
        zip_source_close(memory);
    }
    zip_source_free(memory);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        return writeFailure("cannot write 3MF file");
    OS_LOG(Info, File) << "exported 3MF (" << ids.size() << " object(s), " << bytes.size() << " bytes)";
    return okStatus();
}

} // namespace os::io
