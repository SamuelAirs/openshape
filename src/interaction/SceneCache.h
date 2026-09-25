#pragma once

#include "core/Uuid.h"
#include "geometry/Mesh.h"
#include "geometry/Shape.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace os::doc {
class Document;
}

namespace os::interact {

// Display meshes for document bodies, re-tessellated only when a body's
// shape revision changes.
// TODO(OpenShape-M3): tessellate on a worker thread for large models.
class SceneCache {
public:
    struct Entry {
        std::uint64_t shapeRevision = 0;
        std::shared_ptr<const geom::Mesh> mesh;
    };

    // Brings the cache in sync with the document. Returns true if any mesh changed.
    bool update(const doc::Document& document);
    std::shared_ptr<const geom::Mesh> mesh(const Uuid& bodyId) const;
    std::uint64_t revision(const Uuid& bodyId) const;
    void clear() { entries_.clear(); }

private:
    std::unordered_map<Uuid, Entry> entries_;
};

} // namespace os::interact
