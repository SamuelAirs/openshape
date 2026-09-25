#include "interaction/SceneCache.h"

#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Tessellation.h"

namespace os::interact {

bool SceneCache::update(const doc::Document& document)
{
    bool changed = false;
    std::unordered_map<Uuid, Entry> next;
    for (const auto& body : document.bodies()) {
        auto it = entries_.find(body->id());
        if (it != entries_.end() && it->second.shapeRevision == body->shapeRevision()) {
            next.emplace(body->id(), it->second);
            continue;
        }
        Entry entry;
        entry.shapeRevision = body->shapeRevision();
        entry.mesh = std::make_shared<const geom::Mesh>(geom::tessellate(body->shape()));
        next.emplace(body->id(), std::move(entry));
        changed = true;
    }
    if (next.size() != entries_.size())
        changed = true;
    entries_ = std::move(next);

    static std::uint64_t regionKeys = 1ull << 60;
    std::unordered_map<Uuid, SketchEntry> nextSketches;
    for (const auto& sketch : document.sketches()) {
        const std::uint64_t revision = document.sketchRevision(sketch->id());
        auto it = sketches_.find(sketch->id());
        if (it != sketches_.end() && it->second.revision == revision) {
            nextSketches.emplace(sketch->id(), std::move(it->second));
            continue;
        }
        SketchEntry entry;
        entry.revision = revision;
        if (auto regions = doc::sketchRegions(*sketch)) {
            entry.regions = std::move(regions.value());
            for (const auto& r : entry.regions) {
                entry.meshes.push_back(std::make_shared<const geom::Mesh>(geom::tessellate(r.face)));
                entry.meshKeys.push_back(++regionKeys);
            }
        }
        nextSketches.emplace(sketch->id(), std::move(entry));
        changed = true;
    }
    sketches_ = std::move(nextSketches);
    return changed;
}

std::shared_ptr<const geom::Mesh> SceneCache::mesh(const Uuid& bodyId) const
{
    const auto it = entries_.find(bodyId);
    return it == entries_.end() ? nullptr : it->second.mesh;
}

const SceneCache::SketchEntry* SceneCache::sketch(const Uuid& sketchId) const
{
    const auto it = sketches_.find(sketchId);
    return it == sketches_.end() ? nullptr : &it->second;
}

std::uint64_t SceneCache::revision(const Uuid& bodyId) const
{
    const auto it = entries_.find(bodyId);
    return it == entries_.end() ? 0 : it->second.shapeRevision;
}

} // namespace os::interact
