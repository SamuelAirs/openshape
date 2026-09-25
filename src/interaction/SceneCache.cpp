#include "interaction/SceneCache.h"

#include "document/Document.h"
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
    return changed;
}

std::shared_ptr<const geom::Mesh> SceneCache::mesh(const Uuid& bodyId) const
{
    const auto it = entries_.find(bodyId);
    return it == entries_.end() ? nullptr : it->second.mesh;
}

std::uint64_t SceneCache::revision(const Uuid& bodyId) const
{
    const auto it = entries_.find(bodyId);
    return it == entries_.end() ? 0 : it->second.shapeRevision;
}

} // namespace os::interact
