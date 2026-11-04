// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference photos in the interaction layer: their Model panel rows and
// value edits, drawing them, framing the view on one, and the points under
// the pointer for Set scale (InteractionController.h, "Reference photos").

#include "interaction/InteractionController.h"

#include "commands/DocumentCommands.h"
#include "core/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace os::interact {

namespace {

constexpr const char* kGround = "Ground";
constexpr const char* kFront = "Front";
constexpr const char* kRight = "Right";

std::string planeName(const doc::ReferenceImage& r, const doc::Document& document)
{
    switch (r.plane) {
    case doc::ReferencePlane::XY: return kGround;
    case doc::ReferencePlane::XZ: return kFront;
    case doc::ReferencePlane::YZ: return kRight;
    case doc::ReferencePlane::Datum:
        if (const doc::Datum* d = document.datum(r.datum))
            return d->name();
        return kGround;
    }
    return kGround;
}

std::string percentText(double opacity)
{
    return std::to_string(int(std::lround(opacity * 100))) + " %";
}

// "60", "60%", "60 %": a whole or decimal number of percent.
std::optional<double> parsePercent(const std::string& text)
{
    std::string t = text;
    t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == ' ' || c == '%'; }), t.end());
    if (t.empty())
        return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(v))
        return std::nullopt;
    return v;
}

} // namespace

void InteractionController::addReferenceRows(std::vector<HistoryRow>& rows) const
{
    const LengthUnit unit = document_->displayUnit();
    const std::string dot = " \xC2\xB7 ";
    for (const auto& ref : document_->references()) {
        HistoryRow row;
        row.kind = HistoryRow::Kind::Reference;
        row.id = ref->id();
        row.name = ref->name;
        row.detail = formatLength(ref->width, unit) + " wide" + dot + planeName(*ref, *document_);
        if (ref->unscaled)
            row.detail += dot + "scale not set";
        row.visible = ref->visible;
        if (!ref->hasImage()) {
            row.status = HistoryRow::Status::Failed;
            row.message = "The picture is missing from the project file (damaged). Delete this photo and add it again.";
        } else if (ref->unscaled) {
            row.message = "Its size is a guess: use Set scale with two points a known distance apart.";
        }
        std::vector<std::string> planes{kGround, kFront, kRight};
        for (const auto& d : document_->datums())
            if (d->kind() == doc::DatumKind::Plane)
                planes.push_back(d->name());
        row.parameters = {
            {"width", "Width", formatLength(ref->width, unit), false, false, false, {}},
            {"opacity", "Opacity", percentText(ref->opacity), false, false, true, {}},
            {"rotation", "Rotation", formatAngle(ref->rotation), false, true, false, {}},
            {"x", "X", formatLength(ref->center.x, unit), false, false, false, {}},
            {"y", "Y", formatLength(ref->center.y, unit), false, false, false, {}},
            {"plane", "Plane", planeName(*ref, *document_), true, false, false, planes},
        };
        rows.push_back(std::move(row));
    }
}

Status InteractionController::addReferenceImage(const doc::ReferenceImage& reference)
{
    Status status = undoStack_->push(std::make_unique<cmd::AddReferenceImageCommand>(reference), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    afterDocumentEdit();
    frameReference(reference.id(), /*faceIt=*/!session_); // a sketch keeps its view
    return status;
}

Status InteractionController::setReferenceVisible(const Uuid& referenceId, bool visible)
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return Status::failure(ErrorCode::InvalidReference, "That photo no longer exists.", "setReferenceVisible");
    doc::ReferenceImage next = *ref;
    next.visible = visible;
    Status status =
        undoStack_->push(std::make_unique<cmd::EditReferenceImageCommand>(next, visible ? "Show photo" : "Hide photo"), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

Status InteractionController::deleteReference(const Uuid& referenceId)
{
    Status status = undoStack_->push(std::make_unique<cmd::DeleteReferenceImageCommand>(referenceId), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

Status InteractionController::setReferenceParameter(const Uuid& referenceId, const std::string& key, const std::string& text)
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return Status::failure(ErrorCode::InvalidReference, "That photo no longer exists.", "setReferenceParameter");
    doc::ReferenceImage next = *ref;
    std::string label;
    auto length = [&](double& target) -> Status {
        const auto parsed = parseLength(text, document_->displayUnit());
        if (!parsed.millimeters)
            return Status::failure(ErrorCode::InvalidArgument, parsed.error, "setReferenceParameter: parse error");
        target = *parsed.millimeters;
        return okStatus();
    };
    if (key == "width") {
        if (Status s = length(next.width); !s)
            return s;
        if (!(next.width > 0))
            return Status::failure(ErrorCode::InvalidArgument, "Enter a width above 0.", "reference width <= 0");
        next.unscaled = false; // typed: the owner knows its size now
        label = "Change photo width";
    } else if (key == "x" || key == "y") {
        if (Status s = length(key == "x" ? next.center.x : next.center.y); !s)
            return s;
        label = "Move photo";
    } else if (key == "rotation") {
        const auto parsed = parseAngle(text);
        if (!parsed.millimeters)
            return Status::failure(ErrorCode::InvalidArgument, parsed.error, "setReferenceParameter: parse error");
        next.rotation = std::remainder(*parsed.millimeters, 2 * kPi);
        label = "Turn photo";
    } else if (key == "opacity") {
        const auto percent = parsePercent(text);
        if (!percent)
            return Status::failure(ErrorCode::InvalidArgument, "Enter a percentage from 10 to 100.", "reference opacity parse");
        if (*percent < 10 - 1e-9 || *percent > 100 + 1e-9)
            return Status::failure(ErrorCode::InvalidArgument, "Enter a percentage from 10 to 100.", "reference opacity range");
        next.opacity = *percent / 100.0;
        label = "Change photo opacity";
    } else if (key == "plane") {
        label = "Move photo to another plane";
        next.datum = {};
        if (text == kGround) {
            next.plane = doc::ReferencePlane::XY;
        } else if (text == kFront) {
            next.plane = doc::ReferencePlane::XZ;
        } else if (text == kRight) {
            next.plane = doc::ReferencePlane::YZ;
        } else {
            const auto it = std::find_if(document_->datums().begin(), document_->datums().end(), [&](const auto& d) {
                return d->kind() == doc::DatumKind::Plane && d->name() == text;
            });
            if (it == document_->datums().end())
                return Status::failure(ErrorCode::InvalidArgument, "There is no plane of that name.", "reference plane " + text);
            next.plane = doc::ReferencePlane::Datum;
            next.datum = (*it)->id();
        }
    } else {
        return Status::failure(ErrorCode::InvalidArgument, "This value cannot be changed.", "reference key " + key);
    }
    Status status = undoStack_->push(std::make_unique<cmd::EditReferenceImageCommand>(next, label), *document_);
    if (!status)
        return status;
    afterDocumentEdit();
    // On another plane it may stand edge-on to the view: turn to face it (a sketch keeps its view).
    if (key == "plane" && !session_)
        frameReference(referenceId, true);
    return status;
}

Status InteractionController::scaleReference(const Uuid& referenceId, double factor)
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return Status::failure(ErrorCode::InvalidReference, "That photo no longer exists.", "scaleReference");
    if (!std::isfinite(factor) || factor <= 0)
        return Status::failure(ErrorCode::InvalidArgument, "Enter a distance above 0.", "scaleReference factor");
    doc::ReferenceImage next = *ref;
    next.width = ref->width * factor;
    next.unscaled = false;
    Status status = undoStack_->push(std::make_unique<cmd::EditReferenceImageCommand>(next, "Set photo scale"), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    afterDocumentEdit();
    return status;
}

void InteractionController::frameReference(const Uuid& referenceId, bool faceIt, bool animate)
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return;
    const auto corners = doc::referenceCorners(*ref, *document_);
    Vec3 lo = corners[0], hi = corners[0];
    for (const Vec3& p : corners) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    Camera to = camera_;
    if (faceIt) {
        switch (ref->plane) {
        case doc::ReferencePlane::XY: to.setStandardView(StandardView::Top); break;
        case doc::ReferencePlane::XZ: to.setStandardView(StandardView::Front); break;
        case doc::ReferencePlane::YZ: to.setStandardView(StandardView::Right); break;
        case doc::ReferencePlane::Datum: to.lookFrom(doc::referencePlaneFrame(*ref, *document_).normal()); break;
        }
    }
    to.fit(lo, hi);
    if (faceIt) {
        // Seen head-on: the picture fills most of the view (a sphere fit
        // leaves it small), with room around it for the controls.
        const Vec3 center = (lo + hi) * 0.5;
        double halfWidth = 0, halfHeight = 0;
        for (const Vec3& p : corners) {
            halfWidth = std::max(halfWidth, std::abs((p - center).dot(to.right())));
            halfHeight = std::max(halfHeight, std::abs((p - center).dot(to.up())));
        }
        const double aspect = to.viewportSize.x / std::max(to.viewportSize.y, 1.0);
        const double visible = std::max({2 * halfHeight, 2 * halfWidth / std::max(aspect, 1e-6), 1.0}) * 1.3;
        to.target = center;
        to.orthoHeight = visible;
        to.distance = std::max(visible / 2 / std::tan(to.fovY / 2), Camera::kMinDistance);
    }
    if (animate) {
        startAnimation(to);
    } else {
        animation_.reset();
        camera_ = to;
        notifyView();
    }
}

std::optional<Vec2> InteractionController::referencePointAt(const Uuid& referenceId, Vec2 screen) const
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return std::nullopt;
    const sketch::Plane frame = doc::referenceFrame(*ref, *document_);
    return frame.intersect(camera_.rayAt(screen));
}

std::optional<Vec2> InteractionController::referenceScreenPoint(const Uuid& referenceId, Vec2 local) const
{
    const doc::ReferenceImage* ref = document_->reference(referenceId);
    if (!ref)
        return std::nullopt;
    return camera_.project(doc::referenceFrame(*ref, *document_).toWorld(local));
}

std::vector<Vec3> InteractionController::visibleReferenceCorners() const
{
    std::vector<Vec3> out;
    for (const auto& ref : document_->references())
        if (ref->visible && ref->hasImage())
            for (const Vec3& p : doc::referenceCorners(*ref, *document_))
                out.push_back(p);
    return out;
}

void InteractionController::addReferenceDrawing(RenderScene& scene) const
{
    for (const auto& ref : document_->references()) {
        // Hovering or opening its Model panel row shows it, even when hidden.
        const bool highlighted = historyHighlight_ && *historyHighlight_ == ref->id();
        if ((!ref->visible && !highlighted) || !ref->hasImage())
            continue;
        RenderReferenceImage r;
        r.id = ref->id();
        r.image = ref->image;
        const auto corners = doc::referenceCorners(*ref, *document_);
        for (int k = 0; k < 4; ++k)
            r.corners[k] = corners[std::size_t(k)];
        r.normal = doc::referencePlaneFrame(*ref, *document_).normal();
        r.opacity = ref->visible ? ref->opacity : ref->opacity * 0.5;
        r.highlighted = highlighted;
        scene.references.push_back(std::move(r));
    }
}

} // namespace os::interact
