#include "selection/Selection.h"

#include "core/Log.h"
#include "document/Document.h"

#include <algorithm>

namespace os::sel {

std::optional<SelectionItem> makeSelectionItem(const doc::Document& document, SelectionKind kind, const Uuid& bodyId, int index)
{
    const doc::Body* body = document.body(bodyId);
    if (!body)
        return std::nullopt;
    SelectionItem item;
    item.kind = kind;
    item.bodyId = bodyId;
    item.index = index;
    item.shapeRevision = body->shapeRevision();
    if (kind == SelectionKind::Face) {
        item.faceSignature = geom::captureFaceSignature(body->shape(), index);
        if (!item.faceSignature)
            return std::nullopt;
    } else if (kind == SelectionKind::Edge) {
        item.edgeSignature = geom::captureEdgeSignature(body->shape(), index);
        if (!item.edgeSignature)
            return std::nullopt;
    } else {
        item.index = -1;
    }
    return item;
}

bool SelectionSet::contains(const SelectionItem& item) const
{
    return std::any_of(items_.begin(), items_.end(), [&](const SelectionItem& i) { return i.sameTarget(item); });
}

void SelectionSet::add(const SelectionItem& item)
{
    if (!contains(item))
        items_.push_back(item);
}

void SelectionSet::remove(const SelectionItem& item)
{
    std::erase_if(items_, [&](const SelectionItem& i) { return i.sameTarget(item); });
}

void SelectionSet::toggle(const SelectionItem& item)
{
    if (contains(item))
        remove(item);
    else
        add(item);
}

void SelectionSet::set(const SelectionItem& item)
{
    items_.clear();
    items_.push_back(item);
}

bool SelectionSet::allOfKind(SelectionKind kind) const
{
    return !items_.empty()
        && std::all_of(items_.begin(), items_.end(), [kind](const SelectionItem& i) { return i.kind == kind; });
}

std::optional<Uuid> SelectionSet::singleBody() const
{
    if (items_.empty())
        return std::nullopt;
    const Uuid first = items_.front().bodyId;
    for (const auto& i : items_)
        if (i.bodyId != first)
            return std::nullopt;
    return first;
}

bool SelectionSet::refresh(const doc::Document& document)
{
    bool changed = false;
    std::vector<SelectionItem> kept;
    for (SelectionItem item : items_) {
        const doc::Body* body = document.body(item.bodyId);
        if (!body || !body->isVisible() || body->shape().isNull()) {
            changed = true;
            continue;
        }
        if (item.shapeRevision == body->shapeRevision()) {
            kept.push_back(item);
            continue;
        }
        std::optional<int> resolved;
        if (item.kind == SelectionKind::Body)
            resolved = -1;
        else if (item.kind == SelectionKind::Face && item.faceSignature)
            resolved = geom::resolveFace(body->shape(), *item.faceSignature, item.index);
        else if (item.kind == SelectionKind::Edge && item.edgeSignature)
            resolved = geom::resolveEdge(body->shape(), *item.edgeSignature, item.index);
        changed = true;
        if (!resolved) {
            OS_LOG(Debug, Selection) << "dropping stale selection item on body " << item.bodyId.toString();
            continue;
        }
        // Re-capture so the item describes the face/edge as it is now.
        if (auto fresh = makeSelectionItem(document, item.kind, item.bodyId, *resolved))
            kept.push_back(*fresh);
    }
    // Resolution may map two items onto one target; keep the first.
    std::vector<SelectionItem> unique;
    for (const auto& item : kept)
        if (std::none_of(unique.begin(), unique.end(), [&](const SelectionItem& u) { return u.sameTarget(item); }))
            unique.push_back(item);
    if (unique.size() != items_.size())
        changed = true;
    items_ = std::move(unique);
    return changed;
}

} // namespace os::sel
