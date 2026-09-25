// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Uuid.h"
#include "geometry/Mesh.h"
#include "geometry/Profiles.h"
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

    // Closed regions of a sketch with display meshes (for profile picking and fills).
    struct SketchEntry {
        std::uint64_t revision = 0;
        std::vector<geom::Region> regions;
        std::vector<std::shared_ptr<const geom::Mesh>> meshes;
        std::vector<std::uint64_t> meshKeys;
    };

    // Brings the cache in sync with the document. Returns true if any mesh changed.
    bool update(const doc::Document& document);
    std::shared_ptr<const geom::Mesh> mesh(const Uuid& bodyId) const;
    std::uint64_t revision(const Uuid& bodyId) const;
    const SketchEntry* sketch(const Uuid& sketchId) const;
    void clear()
    {
        entries_.clear();
        sketches_.clear();
    }

private:
    std::unordered_map<Uuid, Entry> entries_;
    std::unordered_map<Uuid, SketchEntry> sketches_;
};

} // namespace os::interact
