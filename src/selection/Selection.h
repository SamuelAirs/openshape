// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Uuid.h"
#include "document/Feature.h"
#include "geometry/TopoSignature.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace os::doc {
class Document;
}

namespace os::sel {

// Conceptual selection kinds. Only Body/Face/Edge are produced today; the
// rest are reserved so the vocabulary is stable (sketch entities arrive in M1).
enum class SelectionKind { Body, Face, Edge, Vertex, SketchEntity, SketchProfile, ConstructionPlane };

// A selected thing. Topology indices are only meaningful for the body shape
// revision they were taken from; the signature lets the selection follow the
// "same" face/edge across a regeneration (e.g. after a push/pull commit).
struct SelectionItem {
    SelectionKind kind = SelectionKind::Body;
    Uuid bodyId;
    int index = -1;
    std::uint64_t shapeRevision = 0;
    std::optional<geom::FaceSignature> faceSignature;
    std::optional<geom::EdgeSignature> edgeSignature;
    // SketchProfile: bodyId holds the sketch id, index the region index,
    // shapeRevision the sketch revision, and `profile` the persistent ref.
    std::optional<doc::ProfileRef> profile;

    bool sameTarget(const SelectionItem& other) const
    {
        return kind == other.kind && bodyId == other.bodyId && index == other.index;
    }
};

// Builds a selection item for the body's current shape, capturing signatures.
std::optional<SelectionItem> makeSelectionItem(const doc::Document& document, SelectionKind kind, const Uuid& bodyId, int index);

class SelectionSet {
public:
    const std::vector<SelectionItem>& items() const { return items_; }
    bool empty() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }

    bool contains(const SelectionItem& item) const;
    void add(const SelectionItem& item);
    void remove(const SelectionItem& item);
    void toggle(const SelectionItem& item);
    void set(const SelectionItem& item);
    void clear() { items_.clear(); }

    // True if every item has the given kind (and the set is non-empty).
    bool allOfKind(SelectionKind kind) const;
    // True if every item belongs to one body.
    std::optional<Uuid> singleBody() const;

    // Re-validates items against the document after a change: items on
    // regenerated shapes are re-resolved by signature, others are dropped.
    // Returns true if anything changed.
    bool refresh(const doc::Document& document);

private:
    std::vector<SelectionItem> items_;
};

} // namespace os::sel
