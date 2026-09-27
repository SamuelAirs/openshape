// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "interaction/InteractionController.h"

#include "commands/DocumentCommands.h"
#include "document/SketchProfiles.h"
#include "core/Log.h"
#include "core/Units.h"
#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"
#include "interaction/TouchWording.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace os::interact {

namespace {

constexpr double kWheelZoomPerStep = 0.85;
constexpr double kMinSceneRadius = 50.0;

bool sameHover(const sel::PickResult& a, const sel::PickResult& b)
{
    return a.kind == b.kind && a.bodyId == b.bodyId && a.index == b.index;
}

// Whether `p` (on the sketch plane) lies in a profile region, tested on the
// region's display mesh: hovering makes no kernel call, so it never waits
// for the preview worker (the kernel lock) - and it is cheaper than OCCT's
// face classifier (TD-20). Near a curved edge the mesh is off by its
// deflection (0.1% of the region's size), far below a pointer's reach.
bool meshContains(const geom::Mesh& mesh, const sketch::Plane& plane, const Vec3& p)
{
    const Vec2 q = plane.toLocal(p);
    auto cross = [](Vec2 u, Vec2 v) { return u.x * v.y - u.y * v.x; };
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Vec2 a = plane.toLocal(mesh.vertex(mesh.indices[3 * t]));
        const Vec2 b = plane.toLocal(mesh.vertex(mesh.indices[3 * t + 1]));
        const Vec2 c = plane.toLocal(mesh.vertex(mesh.indices[3 * t + 2]));
        const double d1 = cross(b - a, q - a), d2 = cross(c - b, q - b), d3 = cross(a - c, q - c);
        if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))
            return true;
    }
    return false;
}

std::string surfaceName(geom::SurfaceKind kind)
{
    switch (kind) {
    case geom::SurfaceKind::Plane: return "Flat face";
    case geom::SurfaceKind::Cylinder: return "Cylindrical face";
    case geom::SurfaceKind::Cone: return "Conical face";
    case geom::SurfaceKind::Sphere: return "Spherical face";
    case geom::SurfaceKind::Torus: return "Toroidal face";
    default: return "Curved face";
    }
}

// Where a 3D segment passes nearest the pick ray through `screen`: the point
// on the segment, its distance from `screen` in pixels and its view depth.
struct SegmentHit {
    Vec3 point;
    double pixels = 0;
    double depth = 0;
};
SegmentHit segmentHit(const Camera& camera, Vec2 screen, const Vec3& a, const Vec3& b)
{
    const Ray ray = camera.rayAt(screen);
    const Vec3 u = b - a, v = ray.direction, w = a - ray.origin;
    const double uu = u.dot(u), uv = u.dot(v), vv = v.dot(v), uw = u.dot(w), vw = v.dot(w);
    const double denom = uu * vv - uv * uv;
    const double t = denom > 1e-12 ? std::clamp((uv * vw - vv * uw) / denom, 0.0, 1.0) : 0.0;
    const Vec3 p = a + u * t;
    return {p, (camera.project(p) - screen).length(), camera.depthOf(p)};
}

// Whether a line drawn in the view (a construction axis or plane outline, an
// origin axis) near the pointer is picked rather than what the body pick
// found there. The lines are depth-tested like the bodies: a face in front
// hides them. An edge nearer the pointer stays the target (edges are the
// smallest targets), unless the line passes in front of it there - drawn
// over the edge, it is what the user sees - and is about as near.
bool lineBeatsBodyHit(const Camera& camera, const SegmentHit& line, const sel::PickResult& bodyHit)
{
    const double slack = camera.pixelSize(line.point) * 2;
    if (bodyHit.kind == sel::PickKind::Face)
        return line.depth <= bodyHit.depth + slack;
    if (bodyHit.kind == sel::PickKind::Edge) {
        const bool inFront = line.depth < bodyHit.depth - slack;
        return inFront ? line.pixels <= bodyHit.screenDistance + 2.0 : line.pixels < bodyHit.screenDistance;
    }
    return true;
}

// What a construction axis or plane is made from, for the Model panel.
std::string datumDetail(const doc::Datum& d, LengthUnit unit)
{
    static const char* planes[] = {"YZ", "XZ", "XY"};
    switch (d.method) {
    case doc::DatumMethod::AxisThrough:
        return !d.refs.empty() && d.refs[0].kind == doc::GeometryRef::Kind::Face ? "Through a hole or shaft" : "Through a circle";
    case doc::DatumMethod::AxisAlongEdge: return "Along an edge";
    case doc::DatumMethod::AxisTwoPoints: return "Through 2 points";
    case doc::DatumMethod::AxisParallel:
        return std::string("Parallel to ") + (d.originIndex >= 0 && d.originIndex <= 2 ? "XYZ"[d.originIndex] : '?');
    case doc::DatumMethod::PlaneOffset:
        return formatLength(d.distance, unit)
             + (d.refs.empty() && d.originIndex >= 0 && d.originIndex <= 2 ? std::string(" from ") + planes[d.originIndex]
                                                                          : std::string(" from a face"));
    case doc::DatumMethod::PlaneAngle: return formatAngle(d.angle) + " to a face";
    case doc::DatumMethod::PlaneMidway: return "Midway between 2 faces";
    }
    return {};
}

// Align's origin targets: action id, button label, target.
struct OriginTargetAction {
    const char* id;
    const char* label;
    OriginTarget target;
};
constexpr OriginTargetAction kOriginTargets[] = {
    {"origin:x", "X axis", OriginTarget::XAxis},     {"origin:y", "Y axis", OriginTarget::YAxis},
    {"origin:z", "Z axis", OriginTarget::ZAxis},     {"origin:xy", "XY plane", OriginTarget::XYPlane},
    {"origin:xz", "XZ plane", OriginTarget::XZPlane}, {"origin:yz", "YZ plane", OriginTarget::YZPlane},
    {"origin:point", "Origin", OriginTarget::Point},
};

std::string curveName(geom::CurveKind kind)
{
    switch (kind) {
    case geom::CurveKind::Line: return "Straight edge";
    case geom::CurveKind::Circle: return "Circular edge";
    default: return "Curved edge";
    }
}

} // namespace

InteractionController::InteractionController(doc::Document& document, cmd::UndoStack& undoStack)
    : document_(&document), undoStack_(&undoStack)
{
    afterDocumentEdit();
}

InteractionController::~InteractionController()
{
    // The worker first (it is also the last member): a running preview finishes, its result is dropped.
    previewWorker_.reset();
}

// ---- Previews off the GUI thread ----------------------------------------------------

void InteractionController::enableAsyncPreviews(std::function<void()> notify)
{
    geom::setInteractiveThread();
    previewWorker_ = std::make_unique<PreviewWorker>(std::move(notify));
    if (operation_)
        operation_->setPreviewScheduler(this);
}

void InteractionController::disableAsyncPreviews()
{
    if (!previewWorker_)
        return;
    (void)waitForPreview();
    if (operation_)
        operation_->setPreviewScheduler(nullptr);
    previewWorker_.reset();
}

bool InteractionController::deliverPreviews()
{
    if (!previewWorker_)
        return false;
    const std::uint64_t before = previewsShown_;
    previewWorker_->deliver();
    return previewsShown_ != before;
}

bool InteractionController::previewBusy() const
{
    return previewWorker_ && previewWorker_->busy();
}

bool InteractionController::waitForPreview(std::chrono::milliseconds timeout)
{
    if (!previewWorker_)
        return true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (previewWorker_ && previewWorker_->busy()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0)
            return false;
        (void)previewWorker_->waitUntilIdle(left);
        previewWorker_->deliver();
    }
    return true;
}

std::shared_ptr<const doc::Document> InteractionController::previewSnapshot(const doc::Document& document)
{
    // One copy per document state: a drag previews many values of one state.
    if (!snapshot_ || snapshotOf_ != &document || snapshotRevision_ != document.revision()
        || snapshotUndoRevision_ != undoStack_->revision()) {
        snapshot_ = document.snapshot();
        snapshotOf_ = &document;
        snapshotRevision_ = document.revision();
        snapshotUndoRevision_ = undoStack_->revision();
    }
    return snapshot_;
}

void InteractionController::schedulePreview(std::function<PreviewOutcome()> compute)
{
    if (!previewWorker_) {
        receivePreview(compute());
        return;
    }
    previewWorker_->submit([this, compute = std::move(compute)]() -> PreviewWorker::Delivery {
        auto outcome = std::make_shared<const PreviewOutcome>(compute());
        // Logged as "[worker] preview took" (off the GUI thread; scripts/dev/watch_log.py tells them apart).
        OS_LOG(Debug, Performance) << "preview took " << outcome->milliseconds << " ms";
        return [this, outcome] { receivePreview(*outcome); };
    });
}

void InteractionController::dropScheduledPreview()
{
    if (previewWorker_)
        previewWorker_->dropWaiting();
}

void InteractionController::receivePreview(const PreviewOutcome& outcome)
{
    if (!operation_ || !operation_->acceptPreview(outcome)) {
        ++previewsDropped_;
        return;
    }
    ++previewsShown_;
    notifyState();
    notifyView();
}

void InteractionController::setDocument(doc::Document& document, cmd::UndoStack& undoStack)
{
    document_ = &document;
    undoStack_ = &undoStack;
    dropTyping();
    session_.reset();
    cameraBeforeSketch_.reset();
    selection_.clear();
    operation_.reset();
    datumTool_.reset();
    planeFills_.clear();
    snapshot_.reset();
    snapshotOf_ = nullptr;
    if (previewWorker_)
        previewWorker_->dropWaiting();
    hover_ = {};
    drag_ = {};
    historyHighlight_.reset();
    scene_.clear();
    afterDocumentEdit();
    fitAll(false);
}

// ---- View ------------------------------------------------------------------------

void InteractionController::setViewportSize(Vec2 size)
{
    camera_.viewportSize = {std::max(size.x, 1.0), std::max(size.y, 1.0)};
    notifyView();
}

void InteractionController::setStandardView(StandardView view, bool animate)
{
    Camera to = camera_;
    to.setStandardView(view);
    if (animate)
        startAnimation(to);
    else {
        camera_ = to;
        notifyView();
    }
}

void InteractionController::setViewAngles(double yaw, double pitch, bool animate)
{
    Camera to = camera_;
    to.yaw = std::remainder(yaw, 2 * kPi);
    to.pitch = std::clamp(pitch, -kPi / 2, kPi / 2);
    if (animate) {
        startAnimation(to);
    } else {
        animation_.reset();
        camera_ = to;
        notifyView();
    }
}

void InteractionController::setProjection(Camera::Projection projection)
{
    if (camera_.projection == projection)
        return;
    camera_.setProjection(projection);
    // A view animation running (a standard view, Fit, opening a sketch) ends
    // in the projection chosen: its end view would otherwise put the old one
    // back while the setting remembers the new one.
    if (animation_) {
        animation_->from.setProjection(projection);
        animation_->to.setProjection(projection);
    }
    notifyView();
    notifyState();
}

void InteractionController::fitAll(bool animate)
{
    geom::BoundingBox total;
    for (const auto& body : document_->bodies()) {
        if (!body->isVisible() || body->shape().isNull())
            continue;
        const auto box = geom::approximateBoundingBox(body->shape());
        if (!box.valid)
            continue;
        if (!total.valid) {
            total = box;
        } else {
            total.min = {std::min(total.min.x, box.min.x), std::min(total.min.y, box.min.y), std::min(total.min.z, box.min.z)};
            total.max = {std::max(total.max.x, box.max.x), std::max(total.max.y, box.max.y), std::max(total.max.z, box.max.z)};
        }
    }
    Camera to = camera_;
    if (total.valid)
        to.fit(total.min, total.max);
    else
        to.fit({-40, -40, 0}, {40, 40, 0});
    if (animate)
        startAnimation(to);
    else {
        camera_ = to;
        notifyView();
    }
}

void InteractionController::fitSelection(bool animate)
{
    const auto bodyId = selection_.singleBody();
    const doc::Body* body = bodyId ? document_->body(*bodyId) : nullptr;
    if (!body) {
        fitAll(animate);
        return;
    }
    const auto box = geom::approximateBoundingBox(body->shape());
    if (!box.valid)
        return;
    Camera to = camera_;
    to.fit(box.min, box.max);
    if (animate)
        startAnimation(to);
    else {
        camera_ = to;
        notifyView();
    }
}

void InteractionController::startAnimation(const Camera& to)
{
    animation_ = Animation{camera_, to, std::chrono::steady_clock::now(), 0.32};
    notifyView();
}

bool InteractionController::advanceAnimation()
{
    if (!animation_)
        return false;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - animation_->start).count();
    const double t = std::clamp(elapsed / animation_->durationSeconds, 0.0, 1.0);
    Camera next = Camera::interpolate(animation_->from, animation_->to, t);
    next.viewportSize = camera_.viewportSize;
    next.sceneCenter = camera_.sceneCenter;
    next.sceneRadius = camera_.sceneRadius;
    camera_ = next;
    if (t >= 1.0)
        animation_.reset();
    notifyView();
    return animation_.has_value();
}

void InteractionController::skipAnimation()
{
    if (!animation_)
        return;
    animation_->start -= std::chrono::seconds(10);
    advanceAnimation();
}

// ---- Input -----------------------------------------------------------------------

void InteractionController::pointerPress(const PointerEvent& event)
{
    // A value typed just now is taken before the press acts (a sketch's
    // typed length, then the tap that places the line's end).
    (void)flushTyping();
    animation_.reset();
    drag_ = {};
    drag_.press = event;
    drag_.last = event.position;
    drag_.mode = DragMode::Pending;

    if (event.device == PointerDevice::Pen && !penMode_) {
        penMode_ = true;
        message("Pen detected: use it to select and draw. Fingers now only move the view.");
    }
    // With a pen around, a finger press can only orbit or pan (resting a hand
    // on the screen must not select or draw).
    if (event.device == PointerDevice::Touch && penMode_)
        return;
    // Only a left click, a tap or the pen selects or acts: the value editor
    // keeps clear of that press (keepClearRect).
    if (event.button == PointerButton::Left)
        notePress(event.position);

    if (session_) {
        if (session_->pointerPress(event, camera_)) {
            drag_.mode = DragMode::Sketch;
            notifyState();
            notifyView();
        }
        return;
    }

    if (event.button == PointerButton::Left && operation_) {
        const int index = handleAt(event.position, event.device);
        if (index >= 0) {
            operation_->setActiveHandle(index);
            drag_.mode = DragMode::Manipulator;
            drag_.handle = operation_->handle(index);
            drag_.handle.beginDrag(camera_, event.position, operation_->handleOffset(index));
            hover_ = {};
            notifyState();
            notifyView();
            return;
        }
        // A ring is grabbed once the pointer moves (pointerMove). A click on
        // it activates it - or, where it crosses an edge or a hole or shaft,
        // picks that (Rotate about it: the rings often cross the body).
        if (const int ring = ringAt(event.position, event.device); ring >= 0)
            drag_.ring = ring;
    }
}

void InteractionController::grabPendingRing()
{
    const int ring = drag_.ring;
    operation_->setActiveHandle(ring); // a different ring starts from zero
    drag_.mode = DragMode::Manipulator;
    drag_.ringHandle = operation_->ring(ring);
    drag_.ringHandle.beginDrag(camera_, drag_.press.position, operation_->value() * kPi / 180.0);
    hover_ = {};
    hoveredHandle_ = -1;
    hoveredRing_ = -1;
    notifyState();
    notifyView();
}

int InteractionController::ringAt(Vec2 screen, PointerDevice device) const
{
    if (!operation_)
        return -1;
    const double tolerance = InputProfile::forDevice(device).handleTolerance;
    int best = -1;
    double bestDistance = 1e300;
    for (int i = 0; i < operation_->ringCount(); ++i) {
        const auto d = operation_->ring(i).hitTest(camera_, screen, tolerance);
        if (d && *d < bestDistance) {
            bestDistance = *d;
            best = i;
        }
    }
    return best;
}

int InteractionController::handleAt(Vec2 screen, PointerDevice device) const
{
    if (!operation_)
        return -1;
    const double tolerance = InputProfile::forDevice(device).handleTolerance;
    int best = -1;
    double bestDistance = 1e300;
    for (int i = 0; i < operation_->handleCount(); ++i) {
        const auto d = operation_->handle(i).hitTest(camera_, screen, operation_->handleOffset(i), tolerance);
        if (d && *d < bestDistance) {
            bestDistance = *d;
            best = i;
        }
    }
    return best;
}

void InteractionController::pointerMove(const PointerEvent& event)
{
    if (drag_.mode == DragMode::Sketch) {
        session_->pointerMove(event, camera_);
        notifyState();
        notifyView();
        return;
    }
    if (drag_.mode == DragMode::None) {
        if (session_) {
            session_->hover(event, camera_);
            notifyState();
            notifyView();
            return;
        }
        updateHover(event);
        return;
    }
    const auto profile = InputProfile::forDevice(drag_.press.device);
    if (drag_.mode == DragMode::Pending && drag_.ring >= 0 && operation_) {
        if ((event.position - drag_.press.position).length() < profile.dragThreshold)
            return;
        grabPendingRing(); // then this move turns it (below)
    }
    if (drag_.mode == DragMode::Pending) {
        if ((event.position - drag_.press.position).length() < profile.dragThreshold)
            return;
        const bool pan = drag_.press.button == PointerButton::Middle
            || (drag_.press.button == PointerButton::Left && drag_.press.modifiers.shift);
        drag_.mode = pan ? DragMode::Pan : DragMode::Orbit;
        if (!pan) {
            const auto hit = sel::pickFace(pickTargets(), camera_, drag_.press.position);
            drag_.pivot = hit.hit() ? hit.point : camera_.target;
        }
        if (hover_.hit() || hoveredHandle_ >= 0 || hoveredRing_ >= 0) {
            hover_ = {};
            hoveredHandle_ = -1;
            hoveredRing_ = -1;
        }
    }

    switch (drag_.mode) {
    case DragMode::Orbit:
        camera_.orbit(event.position.x - drag_.last.x, event.position.y - drag_.last.y, drag_.pivot);
        break;
    case DragMode::Pan:
        camera_.pan(drag_.last, event.position);
        break;
    case DragMode::Manipulator:
        if (operation_ && drag_.ring >= 0) {
            // Rings snap to 15 degrees (Alt: whole degrees).
            const double degrees = drag_.ringHandle.dragTo(camera_, event.position) * 180.0 / kPi;
            const double value = snapValue(degrees, event.modifiers.alt ? 1.0 : 15.0);
            if (value != operation_->value()) {
                operation_->setValue(value, *document_, Operation::Change::ValueOnly);
                notifyState();
            }
        } else if (operation_) {
            const double offset = drag_.handle.dragTo(camera_, event.position);
            double value = operation_->valueFromOffset(offset);
            if (!event.modifiers.alt)
                value = snapValue(value, snapIncrement(camera_.pixelSize(operation_->anchor())));
            if (value != operation_->value()) {
                operation_->setValue(value, *document_, Operation::Change::ValueOnly);
                notifyState();
            }
        }
        break;
    default:
        break;
    }
    drag_.last = event.position;
    notifyView();
}

void InteractionController::pointerRelease(const PointerEvent& event)
{
    const DragMode mode = drag_.mode;
    const PointerEvent press = drag_.press;
    const int pendingRing = mode == DragMode::Pending ? drag_.ring : -1;
    drag_.mode = DragMode::None;
    drag_.ring = -1;
    if (press.device == PointerDevice::Touch && penMode_) {
        notifyView(); // a finger only navigated (or merely tapped)
        return;
    }
    if (session_) {
        if (mode == DragMode::Sketch)
            session_->pointerRelease(event, camera_);
        else if (mode == DragMode::Pending && press.button == PointerButton::Right)
            (void)session_->keyPress(Key::Escape); // right-click finishes the line chain / shape, like Esc
        else if (mode == DragMode::Pending && press.button == PointerButton::Left)
            session_->select(sketch::kNoEntity, false); // click on empty space
        notifyState();
        notifyView();
        return;
    }
    // What this press selects is its doing: the press stays kept clear with it.
    const PressHandling handling(pressHandling_, press.button == PointerButton::Left);
    // Only a left click or a tap selects (and applies a pending value); right
    // and middle buttons orbit/pan when dragged and do nothing on a click.
    // In Rotate, what a click on a ring crosses may be what the user wants to
    // turn about: an edge, a construction axis (not a plane: Rotate does not
    // use one), or a hole or shaft (a round face).
    bool axisUnderRing = false;
    if (pendingRing >= 0 && dynamic_cast<const RotateOperation*>(operation_.get())) {
        const sel::PickResult hit = pickAt(press.position, InputProfile::forDevice(press.device));
        const doc::Body* body = hit.hit() ? document_->body(hit.bodyId) : nullptr;
        const doc::Datum* datum = hit.kind == sel::PickKind::Datum ? document_->datum(hit.bodyId) : nullptr;
        axisUnderRing = hit.kind == sel::PickKind::Edge || (datum && datum->kind() == doc::DatumKind::Axis)
                     || (hit.kind == sel::PickKind::Face && body && [&] {
                            const auto face = geom::faceInfo(body->shape(), hit.index);
                            return face && face->hasAxis();
                        }());
    }
    if (pendingRing >= 0 && operation_ && !axisUnderRing) {
        operation_->setActiveHandle(pendingRing); // clicking a ring makes it the active one
        notifyState();
    } else if (mode == DragMode::Pending && press.button == PointerButton::Left)
        click(press);
    else if (mode == DragMode::Manipulator)
        notifyState();
    if (event.device == PointerDevice::Mouse)
        updateHover(event);
    notifyView();
}

void InteractionController::pointerDoubleClick(const PointerEvent& event)
{
    if (session_ || (event.device == PointerDevice::Touch && penMode_))
        return;
    // A double-click or double-tap selects: kept clear like a click.
    const bool left = event.button == PointerButton::Left;
    if (left)
        notePress(event.position);
    const PressHandling handling(pressHandling_, left);
    const auto hit = pickAt(event.position, InputProfile::forDevice(event.device));
    if (hit.kind == sel::PickKind::Profile) {
        (void)editSketch(hit.bodyId); // double-click a profile: edit its sketch
        return;
    }
    if (!hit.hit())
        return;
    if (operation_ && operation_->canCommit())
        return; // never discard a pending value on a double-click
    if (auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, hit.bodyId, -1)) {
        // Shift (or a touch/pen double-tap) adds a second body, e.g. to combine them.
        const bool additive = event.modifiers.shift || event.modifiers.control
                           || InputProfile::forDevice(event.device).additiveSelection;
        if (additive && selection_.allOfKind(sel::SelectionKind::Body))
            selection_.add(*item);
        else
            selection_.set(*item);
        rebuildOperation();
        notifyState();
        notifyView();
    }
}

void InteractionController::pointerLeave()
{
    if (session_) {
        session_->leave();
        notifyView();
        return;
    }
    if (auto* hole = dynamic_cast<HoleOperation*>(operation_.get()); hole && hole->hover()) {
        hole->setHover(std::nullopt);
        notifyView();
    }
    if (auto* text = dynamic_cast<TextOperation*>(operation_.get()); text && text->hover()) {
        text->setHover(std::nullopt);
        notifyView();
    }
    if (hover_.hit() || hoveredHandle_ >= 0 || hoveredRing_ >= 0) {
        hover_ = {};
        hoveredHandle_ = -1;
        hoveredRing_ = -1;
        notifyView();
    }
}

void InteractionController::cancelPointer()
{
    // In a sketch the press may already have placed a shape's first corner
    // or moved a point: without this, the next tap finished a rectangle from
    // where a pinch or a two-finger pan began.
    if (session_ && drag_.mode == DragMode::Sketch) {
        session_->cancelPress();
        notifyState();
    }
    if (drag_.mode == DragMode::Manipulator)
        notifyState();
    drag_ = {};
    notifyView();
}

void InteractionController::wheel(Vec2 position, double steps)
{
    animation_.reset();
    zoomAt(position, std::pow(kWheelZoomPerStep, steps));
    notifyView();
}

void InteractionController::pinch(Vec2 center, double scale)
{
    if (scale <= 0)
        return;
    animation_.reset();
    zoomAt(center, 1.0 / scale);
    notifyView();
}

void InteractionController::zoomAt(Vec2 screen, double factor)
{
    // In perspective the zoom heads for the surface under the pointer: the
    // target first moves along the view axis to that surface's depth (the
    // image does not change), so the point under the pointer stays there and
    // zooming in approaches it without ever passing through it.
    if (camera_.projection == Camera::Projection::Perspective) {
        const auto hit = sel::pickFace(pickTargets(), camera_, screen);
        if (const double depth = hit.hit() ? camera_.depthOf(hit.point) : 0.0; depth > 1e-6) {
            camera_.target = camera_.eye() + camera_.forward() * depth;
            camera_.distance = depth;
        }
    }
    camera_.zoomAt(screen, factor);
}

void InteractionController::twoFingerPan(Vec2 from, Vec2 to)
{
    animation_.reset();
    camera_.pan(from, to);
    notifyView();
}

void InteractionController::twoFingerRotate(double dxPixels, double dyPixels)
{
    animation_.reset();
    camera_.orbit(dxPixels, dyPixels, camera_.target);
    notifyView();
}

bool InteractionController::keyPress(Key key)
{
    if (session_) {
        // Enter takes the value typed just now; Esc drops it with the shape.
        if (key == Key::Enter)
            (void)flushTyping();
        else if (key == Key::Escape)
            dropTyping();
        const bool handled = session_->keyPress(key);
        notifyState();
        notifyView();
        return handled;
    }
    switch (key) {
    case Key::Escape:
        if (drag_.mode != DragMode::None)
            drag_.mode = DragMode::None;
        if (!operation_ && selection_.empty())
            return false;
        cancelOperation();
        return true;
    case Key::Enter:
        // A value typed just now is what Enter applies (its error, if refused, shows).
        if (typing_.pending() && typingTarget_ == TypingTarget::OperationValue && !flushTyping().empty())
            return true;
        if (operation_ && operation_->canCommit()) {
            (void)commitOperation();
            return true;
        }
        return false;
    case Key::Delete:
    case Key::Backspace:
        // The Text tool's face is selected, but Delete / Backspace there are
        // for the words (the view sends them to its text field): never
        // remove the face under the text being written.
        if (dynamic_cast<const TextOperation*>(operation_.get()))
            return true;
        if (selection_.allOfKind(sel::SelectionKind::Datum)) {
            (void)deleteSelectedDatums();
            return true;
        }
        if (selection_.allOfKind(sel::SelectionKind::Body)) {
            (void)deleteSelectedBodies();
            return true;
        }
        if (selection_.allOfKind(sel::SelectionKind::Face) && selection_.singleBody()) {
            (void)deleteSelectedFaces();
            return true;
        }
        return false;
    case Key::Other:
        break;
    }
    return false;
}

void InteractionController::updateHover(const PointerEvent& event)
{
    const auto profile = InputProfile::forDevice(event.device);
    const int handle = handleAt(event.position, event.device);
    const int ring = handle >= 0 ? -1 : ringAt(event.position, event.device);
    const sel::PickResult hit = handle >= 0 || ring >= 0 ? sel::PickResult{} : operationPickAt(event.position, profile);
    // The Hole tool shows where a click would put the next hole (snapped).
    if (auto* hole = dynamic_cast<HoleOperation*>(operation_.get())) {
        std::optional<Vec2> at;
        if (hit.kind == sel::PickKind::Face && hit.bodyId == hole->bodyId() && hit.index == hole->faceIndex())
            at = hole->snap(hit.point, holeSnapDistance(hit.point, profile)).first;
        const auto& before = hole->hover();
        if (at.has_value() != before.has_value() || (at && (*at - *before).length() > 1e-9)) {
            hole->setHover(at);
            notifyView();
        }
    }
    // The Text tool likewise shows where a click would move the text.
    if (auto* text = dynamic_cast<TextOperation*>(operation_.get())) {
        std::optional<Vec2> at;
        if (hit.kind == sel::PickKind::Face && hit.bodyId == text->bodyId() && hit.index == text->faceIndex())
            at = text->snap(hit.point, holeSnapDistance(hit.point, profile)).first;
        const auto& before = text->hover();
        if (at.has_value() != before.has_value() || (at && (*at - *before).length() > 1e-9)) {
            text->setHover(at);
            notifyView();
        }
    }
    if (!sameHover(hit, hover_) || handle != hoveredHandle_ || ring != hoveredRing_) {
        hover_ = hit;
        hoveredHandle_ = handle;
        hoveredRing_ = ring;
        notifyView();
    }
}

void InteractionController::click(const PointerEvent& event)
{
    const auto profile = InputProfile::forDevice(event.device);
    bool additive = profile.additiveSelection || event.modifiers.shift || event.modifiers.control;
    sel::PickResult hit = operationPickAt(event.position, profile);

    // A finger or pen has no Esc: tapping empty space while an operation
    // still waits for its target gives up, as a tap there does elsewhere
    // (clears the selection). A mouse keeps waiting (a near miss is common).
    const bool tapGivesUp = event.device != PointerDevice::Mouse && !hit.hit();
    // Axis / Plane tool: faces and edges are its picks; clicking empty space
    // applies it (or, with a finger, gives up while it is incomplete).
    if (auto* construct = dynamic_cast<DatumOperation*>(operation_.get())) {
        if (hit.kind == sel::PickKind::Face || hit.kind == sel::PickKind::Edge) {
            const Status status = construct->pick(*document_, hit.bodyId,
                                                  hit.kind == sel::PickKind::Face ? geom::SubShapeKind::Face
                                                                                  : geom::SubShapeKind::Edge,
                                                  hit.index, hit.point);
            if (!status)
                message(forInput(status.userMessage()));
        } else if (!hit.hit() && construct->canCommit()) {
            (void)commitOperation();
        } else if (tapGivesUp) {
            datumTool_.reset();
            selection_.clear();
            rebuildOperation();
        }
        notifyState();
        notifyView();
        return;
    }
    // Align: clicks pick (or re-pick) the target; clicking empty space applies.
    if (auto* align = dynamic_cast<AlignOperation*>(operation_.get())) {
        if (hit.kind == sel::PickKind::Datum) {
            if (const doc::Datum* datum = document_->datum(hit.bodyId))
                (void)align->setDatumTarget(*datum, *document_);
        } else if (hit.kind == sel::PickKind::OriginAxis) {
            (void)align->setOriginTarget(hit.index == 0   ? OriginTarget::XAxis
                                         : hit.index == 1 ? OriginTarget::YAxis
                                                          : OriginTarget::ZAxis,
                                         *document_);
        } else if (hit.kind == sel::PickKind::Face || hit.kind == sel::PickKind::Edge) {
            const Status status = align->setTarget(*document_, hit.bodyId,
                                                   hit.kind == sel::PickKind::Face ? geom::SubShapeKind::Face
                                                                                   : geom::SubShapeKind::Edge,
                                                   hit.index);
            if (!status)
                message(status.userMessage());
        } else if (!hit.hit() && align->canCommit()) {
            (void)commitOperation();
        } else if (tapGivesUp) {
            alignRequested_ = false;
            selection_.clear();
            rebuildOperation();
        }
        notifyState();
        notifyView();
        return;
    }
    // Hole tool: a click on its face adds a hole there (or picks a placed
    // one); elsewhere it applies, as clicking elsewhere does.
    if (auto* hole = dynamic_cast<HoleOperation*>(operation_.get());
        hole && hit.kind == sel::PickKind::Face && hit.bodyId == hole->bodyId() && hit.index == hole->faceIndex()) {
        (void)hole->placeAt(hit.point, holeSnapDistance(hit.point, profile), *document_);
        notifyState();
        notifyView();
        return;
    }
    // Text tool: a click on its face moves the text there; elsewhere it
    // applies what was typed or changed in this use of the tool. Remembered
    // text nobody touched is not applied by a stray click (Enter or Apply
    // still apply it): the tool closes and the click selects as usual.
    if (auto* text = dynamic_cast<TextOperation*>(operation_.get())) {
        if (hit.kind == sel::PickKind::Face && hit.bodyId == text->bodyId() && hit.index == text->faceIndex()) {
            (void)text->placeAt(hit.point, holeSnapDistance(hit.point, profile), *document_);
            textSettings_ = text->settings();
            notifyState();
            notifyView();
            return;
        }
        if (!text->edited()) {
            selection_.clear();
            rebuildOperation(); // back to the usual tools
            hit = pickAt(event.position, profile); // edges too, as without the tool
            additive = false;
        }
    }
    // Extrude "Up to face": the next face click sets the distance.
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && extrude->pickingTarget()) {
        if (hit.kind == sel::PickKind::Face) {
            if (const Status status = extrude->extendToFace(*document_, hit.bodyId, hit.index); !status)
                message(status.userMessage());
        } else {
            extrude->setPickingTarget(false); // clicking elsewhere gives up picking
        }
        notifyState();
        notifyView();
        return;
    }
    // Mirror: a flat face sets the plane; clicking empty space applies.
    if (auto* mirror = dynamic_cast<MirrorOperation*>(operation_.get())) {
        if (const doc::Datum* datum = hit.kind == sel::PickKind::Datum ? document_->datum(hit.bodyId) : nullptr) {
            mirror->setPlane(datum->geometry().origin, datum->geometry().direction, *document_);
        } else if (hit.kind == sel::PickKind::Face) {
            if (const Status status = mirror->setPlaneFromFace(*document_, hit.bodyId, hit.index); !status)
                message(status.userMessage());
        } else if (!hit.hit() && mirror->canCommit()) {
            (void)commitOperation();
        } else if (tapGivesUp) {
            selection_.clear();
            rebuildOperation();
        }
        notifyState();
        notifyView();
        return;
    }
    // Pattern: an edge or round face sets the direction/axis; a click that
    // cannot be used for that applies the pattern (like clicking elsewhere).
    if (auto* pattern = dynamic_cast<PatternOperation*>(operation_.get())) {
        bool used = false;
        if (const doc::Datum* datum = hit.kind == sel::PickKind::Datum ? document_->datum(hit.bodyId) : nullptr;
            datum && datum->kind() == doc::DatumKind::Axis) {
            pattern->setAxisLine(datum->geometry().origin, datum->geometry().direction, *document_);
            used = true;
        } else if (hit.kind == sel::PickKind::Face || hit.kind == sel::PickKind::Edge)
            used = pattern
                       ->setAxisFrom(*document_, hit.bodyId,
                                     hit.kind == sel::PickKind::Face ? geom::SubShapeKind::Face : geom::SubShapeKind::Edge,
                                     hit.index)
                       .ok();
        if (!used && pattern->canCommit())
            (void)commitOperation();
        notifyState();
        notifyView();
        return;
    }
    // Rotate: a straight edge becomes the axis (one ring around it); a corner
    // (an edge clicked near its end) or a circle moves the pivot there. The
    // angle is kept. Other clicks behave as usual.
    if (auto* rotate = dynamic_cast<RotateOperation*>(operation_.get()); rotate && hit.kind == sel::PickKind::Edge) {
        const doc::Body* body = document_->body(hit.bodyId);
        if (const auto edge = body ? geom::edgeInfo(body->shape(), hit.index) : std::nullopt) {
            std::optional<Vec3> corner;
            // The corner zones never cover a whole short edge (a finger's
            // zone is 36 px): its middle still picks the axis.
            const double onScreen = (camera_.project(edge->start) - camera_.project(edge->end)).length();
            double nearest = std::min(profile.pickTolerance * 2, onScreen / 4);
            if ((edge->start - edge->end).length() > 1e-9) // closed curves have no corners
                for (const Vec3& end : {edge->start, edge->end})
                    if (const double d = (camera_.project(end) - event.position).length(); d <= nearest) {
                        nearest = d;
                        corner = end;
                    }
            if (corner)
                rotate->setPivot(*corner, *document_);
            else if (edge->kind == geom::CurveKind::Line)
                rotate->setAxis(edge->midpoint, edge->tangent, *document_);
            else if (edge->kind == geom::CurveKind::Circle)
                rotate->setPivot(edge->center, *document_);
            else
                message(forInput("Turn about a straight edge, or click a corner or a circle to move the pivot there."));
            notifyState();
            notifyView();
            return;
        }
    }
    // ...and so does a construction axis.
    if (auto* rotate = dynamic_cast<RotateOperation*>(operation_.get()); rotate && hit.kind == sel::PickKind::Datum) {
        if (const doc::Datum* datum = document_->datum(hit.bodyId); datum && datum->kind() == doc::DatumKind::Axis) {
            rotate->setAxis(datum->geometry().origin, datum->geometry().direction, *document_);
            notifyState();
            notifyView();
            return;
        }
    }
    // ...and a hole or shaft (a round face) gives its axis.
    if (auto* rotate = dynamic_cast<RotateOperation*>(operation_.get()); rotate && hit.kind == sel::PickKind::Face) {
        const doc::Body* body = document_->body(hit.bodyId);
        if (const auto face = body ? geom::faceInfo(body->shape(), hit.index) : std::nullopt; face && face->hasAxis()) {
            rotate->setAxis(face->axisOrigin, face->axisDirection, *document_);
            notifyState();
            notifyView();
            return;
        }
    }

    if (operation_ && operation_->canCommit()) {
        // Clicking anywhere else accepts the pending operation (direct-manipulation
        // convention); then the click selects against the updated geometry.
        const ApplyResult applied = applyBeforeSelecting();
        if (applied == ApplyResult::Refused)
            return;
        if (applied == ApplyResult::Applied) {
            hit = pickAt(event.position, profile);
            additive = false;
        }
    }

    if (!hit.hit()) {
        selection_.clear();
    } else if (hit.kind == sel::PickKind::Datum) {
        sel::SelectionItem item;
        item.kind = sel::SelectionKind::Datum;
        item.bodyId = hit.bodyId;
        if (additive && selection_.allOfKind(sel::SelectionKind::Datum))
            selection_.toggle(item);
        else
            selection_.set(item);
    } else if (hit.kind == sel::PickKind::OriginAxis) {
        // Only a target for tools (Align); nothing to select.
    } else if (hit.kind == sel::PickKind::Profile) {
        const auto* entry = scene_.sketch(hit.bodyId);
        const sketch::Sketch* sk = document_->sketch(hit.bodyId);
        if (entry && sk && hit.index >= 0 && hit.index < static_cast<int>(entry->regions.size())) {
            sel::SelectionItem item;
            item.kind = sel::SelectionKind::SketchProfile;
            item.bodyId = hit.bodyId;
            item.index = hit.index;
            item.shapeRevision = document_->sketchRevision(hit.bodyId);
            item.profile = doc::makeProfileRef(entry->regions[std::size_t(hit.index)], *sk);
            const bool sameSketch = selection_.allOfKind(sel::SelectionKind::SketchProfile) && selection_.singleBody() == hit.bodyId;
            if (additive && sameSketch)
                selection_.toggle(item);
            else
                selection_.set(item);
        }
    } else {
        const auto kind = hit.kind == sel::PickKind::Edge ? sel::SelectionKind::Edge : sel::SelectionKind::Face;
        if (auto item = sel::makeSelectionItem(*document_, kind, hit.bodyId, hit.index)) {
            const bool sameKind = selection_.allOfKind(kind);
            if (additive && sameKind)
                selection_.toggle(*item);
            else
                selection_.set(*item);
        }
    }
    OS_LOG(Debug, Selection) << "selection now has " << selection_.size() << " item(s)";
    rebuildOperation();
    notifyState();
    notifyView();
}

// ---- Operations ------------------------------------------------------------------

void InteractionController::rebuildOperation()
{
    // Keys typed for the operation that goes (the callers that keep what was
    // typed take it first: a commit, a press, an action).
    if (typingTarget_ == TypingTarget::OperationValue)
        dropTyping();
    // The Axis / Plane tool runs with nothing selected (its picks are its
    // own): a selection made elsewhere (a body's Model-panel row, a
    // double-click, Duplicate) ends it.
    if (datumTool_ && !selection_.empty())
        datumTool_.reset();
    // The Axis / Plane tool keeps its picks across document changes (its
    // preview is worked out again from the document as it is now).
    if (datumTool_) {
        if (auto* construct = dynamic_cast<DatumOperation*>(operation_.get()); construct && construct->kind() == *datumTool_) {
            construct->setValue(construct->value(), *document_);
            return;
        }
        operation_ = DatumOperation::create(*datumTool_);
        return;
    }
    operation_.reset();
    // A preview of the previous operation that has not started is of no use.
    if (previewWorker_)
        previewWorker_->dropWaiting();
    // The new operation computes its previews (also one while it is created)
    // on the worker when previews are asynchronous.
    const PreviewSchedulerScope scheduler(previewWorker_ ? static_cast<PreviewScheduler*>(this) : nullptr);
    if (selection_.empty()) {
        faceOperationKind_ = doc::FeatureKind::PushPull;
        if (edgeOperationKind_ == doc::FeatureKind::Hole)
            edgeOperationKind_ = doc::FeatureKind::Fillet;
        profileOperationKind_ = doc::FeatureKind::Extrude;
        alignRequested_ = false;
        bodyTool_ = BodyTool::Move;
    }
    if (alignRequested_) {
        const auto& items = selection_.items();
        if (items.size() == 1 && (items[0].kind == sel::SelectionKind::Face || items[0].kind == sel::SelectionKind::Edge))
            operation_ = AlignOperation::create(*document_, items[0].bodyId,
                                                items[0].kind == sel::SelectionKind::Face ? geom::SubShapeKind::Face
                                                                                          : geom::SubShapeKind::Edge,
                                                items[0].index);
        if (operation_)
            return;
        alignRequested_ = false;
    }
    if (selection_.allOfKind(sel::SelectionKind::Face) && selection_.singleBody()) {
        const auto& first = selection_.items().front();
        if (selection_.size() == 1 && faceOperationKind_ == doc::FeatureKind::Holes)
            operation_ = HoleOperation::create(*document_, first.bodyId, first.index, holeSettings_);
        if (!operation_ && faceOperationKind_ == doc::FeatureKind::Holes)
            faceOperationKind_ = doc::FeatureKind::PushPull; // not a flat face
        if (selection_.size() == 1 && faceOperationKind_ == doc::FeatureKind::Text && geom::hasFont(doc::kTextFontRegular))
            operation_ = TextOperation::create(*document_, first.bodyId, first.index, textSettings_);
        if (!operation_ && faceOperationKind_ == doc::FeatureKind::Text)
            faceOperationKind_ = doc::FeatureKind::PushPull; // not a flat face (or no font)
        if (selection_.size() == 1 && faceOperationKind_ == doc::FeatureKind::OffsetFace)
            operation_ = OffsetFaceOperation::create(*document_, first.bodyId, first.index);
        if (!operation_ && selection_.size() == 1 && faceOperationKind_ == doc::FeatureKind::PushPull)
            operation_ = PushPullOperation::create(*document_, first.bodyId, first.index);
        // A single round face (a hole, a shaft) is resized by default.
        if (!operation_ && selection_.size() == 1 && faceOperationKind_ == doc::FeatureKind::PushPull)
            if (const doc::Body* body = document_->body(first.bodyId))
                if (const auto info = geom::faceInfo(body->shape(), first.index); info && info->kind == geom::SurfaceKind::Cylinder)
                    operation_ = OffsetFaceOperation::create(*document_, first.bodyId, first.index);
        if (!operation_) {
            // Several faces, a curved face, or Shell chosen explicitly.
            std::vector<int> faces;
            for (const auto& item : selection_.items())
                faces.push_back(item.index);
            operation_ = ShellOperation::create(*document_, first.bodyId, faces);
        }
    } else if (selection_.allOfKind(sel::SelectionKind::Edge) && selection_.singleBody()) {
        std::vector<int> edges;
        for (const auto& item : selection_.items())
            edges.push_back(item.index);
        if (edgeOperationKind_ == doc::FeatureKind::Hole && edges.size() == 1) {
            if (rimHoleKind_ == doc::HoleKind::Plain)
                operation_ = InsertOperation::create(*document_, *selection_.singleBody(), edges.front(), insertPreset_);
            else
                operation_ = HeadOperation::create(*document_, *selection_.singleBody(), edges.front(), rimHoleKind_,
                                                   screwPreset_, holeSettings_.allowance);
        }
        if (!operation_) {
            if (edgeOperationKind_ == doc::FeatureKind::Hole)
                edgeOperationKind_ = doc::FeatureKind::Fillet;
            operation_ = EdgeOperation::create(*document_, *selection_.singleBody(), edges, edgeOperationKind_);
        }
    } else if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Body) {
        const Uuid body = selection_.items().front().bodyId;
        switch (bodyTool_) {
        case BodyTool::Move: operation_ = MoveOperation::create(*document_, body); break;
        case BodyTool::Rotate: operation_ = RotateOperation::create(*document_, body); break;
        case BodyTool::Mirror: operation_ = MirrorOperation::create(*document_, body); break;
        case BodyTool::Pattern: operation_ = PatternOperation::create(*document_, body); break;
        }
    } else if (selection_.allOfKind(sel::SelectionKind::SketchProfile) && selection_.singleBody()) {
        const Uuid sketchId = *selection_.singleBody();
        const auto* entry = scene_.sketch(sketchId);
        std::vector<doc::ProfileRef> refs;
        for (const auto& item : selection_.items())
            if (item.profile)
                refs.push_back(*item.profile);
        const int first = selection_.items().front().index;
        if (entry && first >= 0 && first < static_cast<int>(entry->regions.size())) {
            const Vec3 anchor = entry->regions[std::size_t(first)].interiorPoint;
            if (profileOperationKind_ == doc::FeatureKind::Revolve)
                operation_ = RevolveOperation::create(*document_, sketchId, std::move(refs), anchor, revolveAxis_);
            else
                operation_ = ExtrudeOperation::create(*document_, sketchId, std::move(refs), anchor);
        }
    }
}

InteractionController::TypedValue InteractionController::parseTypedValue(const std::string& text) const
{
    if (!operation_)
        return {std::nullopt, "Select a face or an edge first."};
    const auto parsed = operation_->isAngle() ? parseAngle(text) : parseLength(text, document_->displayUnit());
    if (!parsed.millimeters)
        return {std::nullopt, parsed.error};
    // Angle operations keep their value in degrees.
    double value = operation_->isAngle() ? *parsed.millimeters * 180.0 / kPi : *parsed.millimeters;
    // "+5" / "-5" change a measured value (e.g. a push/pull's thickness) by that much.
    const auto first = text.find_first_not_of(" \t");
    if (const auto base = operation_->relativeBase(); base && first != std::string::npos && (text[first] == '+' || text[first] == '-'))
        value += *base;
    // (The Text tool's angle may be negative: it checks its own range.)
    if (operation_->isAngle() && value > 360.0 + 1e-9 && !dynamic_cast<const TextOperation*>(operation_.get()))
        return {std::nullopt, "The angle must be between 0° and 360°."};
    if (!operation_->allowsNegative() && value <= 0)
        return {std::nullopt, operation_->valueLabel() + " must be greater than zero."};
    return {value, {}};
}

std::string InteractionController::setValueText(const std::string& text)
{
    // The whole text, confirmed: keys typed before it (still waiting for
    // their pause) are part of it.
    if (typingTarget_ == TypingTarget::OperationValue)
        typing_.clear();
    const TypedValue typed = parseTypedValue(text);
    typedValueError_ = typed.error;
    if (!typed.value)
        return typed.error;
    return previewTypedValue(*typed.value);
}

// ---- Values typed key by key ------------------------------------------------------

void InteractionController::typeValue(TypingTarget target, const std::string& text)
{
    // Keys typed into another value first confirm what was typed before (a
    // sketch value, then the operation's: not possible at once, but safe).
    if (typing_.pending() && typingTarget_ != target)
        (void)flushTyping();
    typingTarget_ = target;
    typedValueError_.clear(); // not judged until it is taken
    typing_.type(text, clock_());
}

bool InteractionController::advanceTyping()
{
    const auto text = typing_.takeIfDue(clock_());
    if (!text)
        return false;
    (void)applyTyped(*text);
    return true;
}

std::string InteractionController::flushTyping()
{
    const auto text = typing_.take();
    return text ? applyTyped(*text) : std::string();
}

void InteractionController::dropTyping()
{
    typing_.clear();
    typedValueError_.clear();
}

std::string InteractionController::applyTyped(const std::string& text)
{
    if (typingTarget_ == TypingTarget::SketchInput) {
        // The shape may be finished (or the sketch left) since: nothing to set.
        if (!session_ || !session_->hasInputs())
            return {};
        typedValueError_ = session_->typeIntoInput(text);
        notifyState();
        notifyView();
        return typedValueError_;
    }
    if (!operation_)
        return {};
    const TypedValue typed = parseTypedValue(text);
    typedValueError_ = typed.error;
    if (!typed.value) {
        notifyState(); // the value editor shows why
        return typed.error;
    }
    (void)previewTypedValue(*typed.value);
    return {};
}

std::string InteractionController::previewTypedValue(double value)
{
    if (auto* words = dynamic_cast<TextOperation*>(operation_.get()))
        words->markEdited(); // a value typed, even the one it had
    // The same value again (Enter after typing it) keeps the preview that is
    // shown or still computing, and its verdict.
    const bool known = value == operation_->value()
                    && (operation_->previewPending() || operation_->hasPreview() || !operation_->error().empty());
    if (!known)
        operation_->setValue(value, *document_, Operation::Change::ValueOnly);
    notifyState();
    notifyView();
    // A preview still computing has no verdict yet (it arrives with a state change).
    return operation_->previewPending() ? std::string() : operation_->error();
}

std::string InteractionController::confirmValueText(const std::string& text)
{
    const std::string error = setValueText(text);
    if (!error.empty() || !operation_ || !operation_->previewPending())
        return error;
    (void)waitForPreview();
    return operation_ ? operation_->error() : std::string();
}

std::string InteractionController::operationValueText() const
{
    if (!operation_)
        return {};
    if (operation_->isAngle())
        return formatAngle(operation_->value() * kPi / 180.0);
    return formatLength(operation_->value(), document_->displayUnit());
}

bool InteractionController::operationTakesText() const
{
    return dynamic_cast<const TextOperation*>(operation_.get()) != nullptr;
}

std::string InteractionController::operationText() const
{
    const auto* text = dynamic_cast<const TextOperation*>(operation_.get());
    return text ? text->text() : std::string();
}

bool InteractionController::operationTextTyped() const
{
    const auto* text = dynamic_cast<const TextOperation*>(operation_.get());
    return text && text->wordsTyped();
}

std::string InteractionController::setOperationText(const std::string& value)
{
    auto* text = dynamic_cast<TextOperation*>(operation_.get());
    if (!text)
        return "Select a flat face, then Text.";
    text->setText(value, *document_);
    textSettings_ = text->settings();
    notifyState();
    notifyView();
    // A preview still computing has no verdict yet (it arrives with a state change).
    return text->previewPending() ? std::string() : text->error();
}

std::optional<Vec2> InteractionController::valueLabelPosition() const
{
    if (operation_ && operation_->ringCount() > 0) {
        // Beside the rings, to the right of their center.
        const Vec2 c = camera_.project(operation_->ring(0).center());
        return Vec2{c.x + RingStyle{}.radiusPx, c.y - RingStyle{}.radiusPx * 0.5};
    }
    if (operation_ && operation_->handleCount() == 0) {
        if (const auto anchor = operation_->labelAnchor()) {
            const Vec2 c = camera_.project(*anchor);
            return Vec2{c.x + 40, c.y - 20};
        }
        return std::nullopt; // e.g. Align still waiting for its target, Mirror
    }
    if (!operation_)
        return std::nullopt;
    const ArrowStyle style;
    const int active = operation_->activeHandle();
    const LinearManipulator handle = operation_->handle(active);
    const Vec3 anchor = handle.anchor(operation_->handleOffset(active));
    const double px = camera_.pixelSize(anchor);
    Vec2 tip = camera_.project(anchor + handle.direction() * (style.totalPx() * px));
    // The Text tool's arrow stands in the middle of the text: the chip goes
    // beside the letters, not over them.
    if (const auto* text = dynamic_cast<const TextOperation*>(operation_.get()))
        for (const Vec3& corner : text->textCorners())
            tip.x = std::max(tip.x, camera_.project(corner).x);
    return tip;
}

namespace {

// Screen bounds of world points (points behind the eye are left out).
class ScreenBounds {
public:
    explicit ScreenBounds(const Camera& camera) : viewProjection_(camera.viewProjection()), viewport_(camera.viewportSize) {}

    std::optional<Vec2> project(const Vec3& p) const
    {
        const Vec4 clip = viewProjection_ * Vec4{p.x, p.y, p.z, 1.0};
        if (clip.w <= 1e-9)
            return std::nullopt;
        return Vec2{(clip.x / clip.w + 1) * 0.5 * viewport_.x, (1 - clip.y / clip.w) * 0.5 * viewport_.y};
    }
    void add(const Vec3& p)
    {
        points.push_back(p);
        if (const auto s = project(p))
            include(ScreenRect::around(*s));
    }
    void include(const ScreenRect& r) { rect = rect ? rect->united(r) : r; }

    void addFace(const geom::Mesh& mesh, int face)
    {
        if (face < 0 || face >= mesh.faceCount())
            return;
        for (std::uint32_t t = mesh.faceTriangleOffset[std::size_t(face)]; t < mesh.faceTriangleOffset[std::size_t(face) + 1]; ++t)
            for (std::size_t k = 0; k < 3; ++k)
                add(mesh.vertex(mesh.indices[3 * std::size_t(t) + k]));
    }
    void addEdge(const geom::Mesh& mesh, int edge)
    {
        for (const auto& polyline : mesh.edges)
            if (polyline.edgeIndex == edge)
                for (std::size_t i = 0; i + 2 < polyline.points.size(); i += 3)
                    add({polyline.points[i], polyline.points[i + 1], polyline.points[i + 2]});
    }
    // All of a mesh (a body, a sketch region): the corners of its box, cheap
    // for any size and never smaller than the mesh on screen.
    void addMesh(const geom::Mesh& mesh)
    {
        if (mesh.vertexCount() == 0)
            return;
        Vec3 lo = mesh.vertex(0), hi = lo;
        for (std::size_t i = 1; i < mesh.vertexCount(); ++i) {
            const Vec3 v = mesh.vertex(i);
            lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
            hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        }
        addBox(lo, hi);
    }
    void addBox(const Vec3& lo, const Vec3& hi)
    {
        for (int c = 0; c < 8; ++c)
            add({c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y, c & 4 ? hi.z : lo.z});
    }

    std::optional<ScreenRect> rect;
    std::vector<Vec3> points; // everything added, in world coordinates

private:
    Mat4 viewProjection_;
    Vec2 viewport_;
};

bool sameView(const Camera& a, const Camera& b)
{
    return a.target.x == b.target.x && a.target.y == b.target.y && a.target.z == b.target.z && a.yaw == b.yaw
        && a.pitch == b.pitch && a.orthoHeight == b.orthoHeight && a.distance == b.distance && a.fovY == b.fovY
        && a.projection == b.projection && a.viewportSize.x == b.viewportSize.x && a.viewportSize.y == b.viewportSize.y;
}

bool sameTargets(const std::vector<sel::SelectionItem>& a, const std::vector<sel::SelectionItem>& b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](const sel::SelectionItem& x, const sel::SelectionItem& y) { return x.sameTarget(y); });
}

} // namespace

void InteractionController::notePress(Vec2 position) { lastPress_ = PressMark{position, camera_, selection_.items()}; }

std::optional<ScreenRect> InteractionController::keepClearRect() const
{
    if (session_)
        return std::nullopt;
    ScreenBounds selected(camera_);
    auto addItem = [&](sel::SelectionKind kind, const Uuid& id, int index) {
        if (kind == sel::SelectionKind::SketchProfile) {
            const auto* entry = scene_.sketch(id);
            if (entry && index >= 0 && std::size_t(index) < entry->meshes.size() && entry->meshes[std::size_t(index)])
                selected.addMesh(*entry->meshes[std::size_t(index)]);
            return;
        }
        if (kind == sel::SelectionKind::Datum) {
            // A construction axis or plane: as drawn.
            if (const doc::Datum* datum = document_->datum(id)) {
                const DatumShape shape = datumShape(datum->geometry(), datum->kind());
                if (shape.plane)
                    for (const Vec3& corner : shape.corners)
                        selected.add(corner);
                else {
                    selected.add(shape.a);
                    selected.add(shape.b);
                }
            }
            return;
        }
        const auto mesh = scene_.mesh(id);
        if (!mesh)
            return;
        switch (kind) {
        case sel::SelectionKind::Face: selected.addFace(*mesh, index); break;
        case sel::SelectionKind::Edge: selected.addEdge(*mesh, index); break;
        case sel::SelectionKind::Body: selected.addMesh(*mesh); break;
        case sel::SelectionKind::Vertex:
        case sel::SelectionKind::SketchEntity:
        case sel::SelectionKind::SketchProfile:
        case sel::SelectionKind::Datum: break;
        }
    };
    for (const auto& item : selection_.items())
        addItem(item.kind, item.bodyId, item.index);
    // Align's target is shown like a selection, and so kept clear like one.
    if (const auto* align = dynamic_cast<const AlignOperation*>(operation_.get()); align && align->hasTarget()) {
        if (align->targetKind() == geom::SubShapeKind::Face)
            addItem(sel::SelectionKind::Face, align->targetBody(), align->targetIndex());
        else if (align->targetKind() == geom::SubShapeKind::Edge)
            addItem(sel::SelectionKind::Edge, align->targetBody(), align->targetIndex());
    }

    ScreenBounds all(camera_);
    if (selected.rect)
        all.include(*selected.rect);
    if (operation_) {
        const ArrowStyle arrowStyle;
        const int handles = operation_->handleCount();
        // What the operation carries (a pushed face, a moved body, a
        // pattern's last copy) is also where it has taken it: the selection
        // moved by each of its shifts (Operation::carriedSelection). Where
        // the operation has it now and where the preview on screen has it:
        // while the newest value computes on the worker, the shown preview
        // is of an earlier one (behind the arrow, or ahead of it after a drag
        // back). A moved, turned or new body is also kept clear as the
        // preview shows it (its mesh's box, measured on the worker).
        // Shifted in the world, then projected: in perspective a far point
        // moves less on screen than a near one.
        auto carried = [&](const Operation::Carry& carry) {
            for (const Operation::Shift& shift : carry.shifts) {
                const Vec3 by = shift.to - shift.from;
                for (const Vec3& p : selected.points)
                    all.add(p + by);
            }
        };
        carried(operation_->carriedSelection());
        if (const Operation::Carry* shown = operation_->previewCarry()) {
            carried(*shown);
            if (const auto box = operation_->previewBounds(); box && shown->wholePreview)
                all.addBox(box->min, box->max);
        }
        for (int i = 0; i < handles; ++i) {
            const LinearManipulator handle = operation_->handle(i);
            const Vec3 anchor = handle.anchor(operation_->handleOffset(i));
            ScreenBounds arrow(camera_);
            arrow.add(anchor);
            arrow.add(anchor + handle.direction() * (arrowStyle.totalPx() * camera_.pixelSize(anchor)));
            if (arrow.rect)
                all.include(arrow.rect->inflated(arrowStyle.headRadiusPx));
        }
        const RingStyle ringStyle;
        for (int i = 0; i < operation_->ringCount(); ++i)
            if (const auto center = all.project(operation_->ring(i).center()))
                all.include(ScreenRect::around(*center).inflated(ringStyle.radiusPx + ringStyle.widthPx));
        // A value without an arrow (the Hole tool's current hole).
        if (handles == 0 && operation_->ringCount() == 0)
            if (const auto anchor = operation_->labelAnchor())
                if (const auto at = all.project(*anchor))
                    all.include(ScreenRect::around(*at).inflated(12));
    }
    // The last press, while the view and the selection it left are unchanged.
    if (lastPress_ && sameView(lastPress_->camera, camera_) && sameTargets(lastPress_->selection, selection_.items()))
        all.include(ScreenRect::around(lastPress_->position));
    if (!all.rect)
        return std::nullopt;
    return all.rect->clippedTo({0, 0, camera_.viewportSize.x, camera_.viewportSize.y});
}

bool InteractionController::revealKeepClear(const ScreenRect& region)
{
    if (region.width() <= 1 || region.height() <= 1)
        return false;
    bool moved = false;
    // A few rounds: the rectangle is clipped to the window, so one that
    // reaches past it is only known in full once the view has moved.
    for (int round = 0; round < 4; ++round) {
        const auto keep = keepClearRect();
        if (!keep || region.contains(*keep, 0.5))
            break;
        animation_.reset();
        const double scale = std::max(keep->width() / region.width(), keep->height() / region.height());
        if (scale > 1)
            camera_.zoomAt(keep->center(), scale * 1.08); // (a factor above 1 zooms out)
        const auto after = keepClearRect();
        if (!after)
            break;
        camera_.pan(after->center(), region.center());
        moved = true;
    }
    if (moved)
        notifyView();
    return moved;
}

ChipPlacement InteractionController::placeValueChip(const ChipPlacementInput& input) const
{
    // A new selection chooses afresh; the same one keeps its spot.
    const auto& items = selection_.items();
    if (!sameTargets(items, chipSelection_)) {
        chipSelection_ = items;
        chipSpot_ = ChipSpot::None;
        chipSettled_ = false;
        chipLast_.reset();
    }
    // The first placements come while the chip is still being laid out (its
    // actions appear, it grows): until something moves (the arrow, the
    // view), each one chooses afresh for the size it has now.
    auto near = [](double a, double b) { return std::abs(a - b) < 0.5; };
    auto sameRect = [&](const std::optional<ScreenRect>& a, const std::optional<ScreenRect>& b) {
        return a.has_value() == b.has_value()
            && (!a || (near(a->left, b->left) && near(a->top, b->top) && near(a->right, b->right) && near(a->bottom, b->bottom)));
    };
    if (input.frozen || (chipLast_ && (!near(input.tip.x, chipLast_->tip.x) || !near(input.tip.y, chipLast_->tip.y)
                                       || !sameRect(input.keepClear, chipLast_->keepClear))))
        chipSettled_ = true;
    const ChipPlacement placement = interact::placeValueChip(input, chipSettled_ ? chipSpot_ : ChipSpot::None);
    chipSpot_ = placement.spot;
    chipLast_ = input;
    return placement;
}

std::vector<InteractionController::AxisMark> InteractionController::axisTriad() const
{
    const Vec3 right = camera_.right(), up = camera_.up(), back = camera_.backward();
    std::vector<AxisMark> marks;
    for (int i = 0; i < 3; ++i) {
        const Vec3 axis{i == 0 ? 1.0 : 0.0, i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0};
        marks.push_back({i, Vec2{axis.dot(right), -axis.dot(up)}, axis.dot(back)});
    }
    return marks;
}

Status InteractionController::commitOperation()
{
    // A value typed just now (its pause not over) is what is applied.
    if (typing_.pending() && typingTarget_ == TypingTarget::OperationValue) {
        if (const std::string refused = flushTyping(); !refused.empty())
            return Status::failure(ErrorCode::InvalidArgument, refused, "commit of a refused typed value");
    }
    if (!operation_)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing to apply.", "commit without operation");
    // The command computes the step again, so a pending preview is not waited
    // for - unless an automatic choice (join or new body) depends on it.
    if (operation_->previewPending() && (operation_->commitNeedsPreview() || !operation_->canCommit()))
        (void)waitForPreview();
    if (!operation_)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing to apply.", "commit without operation");
    if (!operation_->canCommit()) {
        const auto* words = dynamic_cast<const TextOperation*>(operation_.get());
        const std::string text = !operation_->error().empty()   ? operation_->error()
                               : words && words->text().empty() ? "Type the text first."
                                                                : "Drag the arrow or type a value first.";
        return Status::failure(ErrorCode::InvalidArgument, text, "commit of non-committable operation");
    }
    // A new construction axis or plane comes out selected (Sketch on a plane
    // is then one step away).
    if (const auto* construct = dynamic_cast<const DatumOperation*>(operation_.get())) {
        std::unique_ptr<cmd::Command> command = construct->makeCommand(*document_);
        const auto* add = dynamic_cast<const cmd::AddDatumCommand*>(command.get());
        const Uuid made = add ? add->datumId() : Uuid();
        Status status = undoStack_->push(std::move(command), *document_);
        if (!status) {
            message(status.userMessage());
            return status;
        }
        operation_.reset();
        datumTool_.reset();
        selection_.clear();
        if (document_->datum(made)) {
            sel::SelectionItem item;
            item.kind = sel::SelectionKind::Datum;
            item.bodyId = made;
            selection_.set(item);
        }
        afterDocumentEdit();
        return status;
    }
    // Fillets consume their edges and extrusions their profiles: clear those
    // selections. A pushed face still exists and stays selected.
    const doc::FeatureKind kind = operation_->featureKind();
    const bool clearSelection = kind != doc::FeatureKind::PushPull && kind != doc::FeatureKind::Move
                             && kind != doc::FeatureKind::Mirror && kind != doc::FeatureKind::Pattern;
    // One-shot tool resets, applied only once the command is accepted: a
    // refused apply (possible while a preview is still pending) keeps the
    // tool - Align, Pattern, Mirror - for the next rebuild.
    const bool isAlign = dynamic_cast<const AlignOperation*>(operation_.get()) != nullptr;
    const bool isOneShotBodyTool = kind == doc::FeatureKind::Mirror || kind == doc::FeatureKind::Pattern;
    std::optional<HoleSettings> appliedHoleSettings;
    if (const auto* hole = dynamic_cast<const HoleOperation*>(operation_.get()))
        appliedHoleSettings = hole->settings();
    std::optional<TextSettings> appliedTextSettings;
    if (const auto* text = dynamic_cast<const TextOperation*>(operation_.get()))
        appliedTextSettings = text->settings();
    // Copies made as separate bodies: say so (and why, when it was not asked for).
    std::string done;
    if (const auto* mirror = dynamic_cast<const MirrorOperation*>(operation_.get()); mirror && mirror->separate())
        done = mirror->separateIsAutomatic() ? "Mirrored as a separate body: the image does not touch the original."
                                             : "Mirrored as a separate body.";
    if (const auto* pattern = dynamic_cast<const PatternOperation*>(operation_.get()); pattern && pattern->separate()) {
        const int copies = pattern->count() - 1;
        const char* why = copies == 1 ? ": the copy does not touch the original." : ": the copies do not touch the original.";
        done = "Patterned as " + std::to_string(copies) + (copies == 1 ? " separate body" : " separate bodies")
             + (pattern->separateIsAutomatic() ? why : ".");
    }
    const Uuid target = operation_->bodyId();
    const doc::Body* targetBefore = target.isNil() ? nullptr : document_->body(target);
    const int piecesBefore = targetBefore ? targetBefore->shape().solidCount() : 0;
    // A preview still waiting to start would only delay the command's recompute.
    if (previewWorker_)
        previewWorker_->dropWaiting();
    Status status = undoStack_->push(operation_->makeCommand(*document_), *document_);
    if (!status) {
        // Refused before its preview came back (the waiting job was dropped
        // above): the refusal is the value's verdict, as a refused preview's
        // would be, and no preview of another value stays shown.
        if (operation_->previewPending())
            operation_->refusePendingValue(status.userMessage());
        message(status.userMessage());
        notifyState();
        notifyView();
        return status;
    }
    if (isAlign)
        alignRequested_ = false; // done: the source face/edge offers its usual tools again
    if (isOneShotBodyTool)
        bodyTool_ = BodyTool::Move; // one-shot: the body stays selected with plain arrows
    if (appliedHoleSettings)
        holeSettings_ = *appliedHoleSettings;
    if (appliedTextSettings)
        textSettings_ = *appliedTextSettings;
    operation_.reset();
    if (!done.empty())
        message(done);
    suggestSplit(target, piecesBefore);
    // Edges consumed by a fillet/chamfer no longer exist; a face that was
    // pushed still does and stays selected for the next push.
    if (clearSelection)
        selection_.clear();
    afterDocumentEdit();
    return status;
}

Status InteractionController::applyPendingValue(const char* action)
{
    if (typingTarget_ == TypingTarget::OperationValue)
        (void)flushTyping(); // a value typed just now counts
    if (operation_ && operation_->canCommit() && applyBeforeSelecting() == ApplyResult::Refused)
        return Status::failure(ErrorCode::InvalidArgument,
                               operation_ && !operation_->error().empty() ? operation_->error() : "The value could not be applied.",
                               std::string(action) + ": the pending operation was refused");
    return okStatus();
}

InteractionController::ApplyResult InteractionController::applyBeforeSelecting()
{
    // A value typed just now counts, as if its pause were over.
    if (typingTarget_ == TypingTarget::OperationValue)
        (void)flushTyping();
    // A finished preview's verdict first (its delivery may still be queued).
    if (previewWorker_)
        (void)deliverPreviews();
    if (!operation_ || !operation_->canCommit())
        return ApplyResult::Dropped;
    // Words remembered from the last use, untouched in this one, are applied
    // only on purpose (Enter, Apply), not by picking something else.
    if (const auto* text = dynamic_cast<const TextOperation*>(operation_.get()); text && !text->edited())
        return ApplyResult::Dropped;
    // Without a verdict yet (the preview still computes) the command decides.
    // Refused, the click goes on, as it would have with the refusal shown:
    // a click never applies a refused value, it selects.
    const bool verdictKnown = !operation_->previewPending();
    if (commitOperation())
        return ApplyResult::Applied;
    return verdictKnown ? ApplyResult::Refused : ApplyResult::Dropped;
}

void InteractionController::suggestSplit(const Uuid& bodyId, int piecesBefore)
{
    // A cut that split the body in two: say how to make each piece a body
    // (not automatic: the pieces may belong together).
    if (const doc::Body* after = piecesBefore > 0 ? document_->body(bodyId) : nullptr;
        after && after->shape().solidCount() > piecesBefore && !after->hasFailures())
        message(after->name() + " is now in " + std::to_string(after->shape().solidCount())
                + " separate pieces. To make each piece a body, select it and choose Split into bodies.");
}

void InteractionController::cancelOperation()
{
    if (typingTarget_ == TypingTarget::OperationValue)
        dropTyping();
    // The Axis / Plane tool steps back one pick at a time, then ends.
    if (auto* construct = dynamic_cast<DatumOperation*>(operation_.get())) {
        if (!construct->dropLastPick(*document_)) {
            datumTool_.reset();
            selection_.clear();
            rebuildOperation();
        }
        notifyState();
        notifyView();
        return;
    }
    // Align steps back one pick at a time: offset, then target, then Align itself.
    if (auto* align = dynamic_cast<AlignOperation*>(operation_.get())) {
        if (align->hasTarget() && align->value() != 0.0) {
            align->setValue(0.0, *document_);
        } else if (align->hasTarget()) {
            align->clearTarget();
        } else {
            alignRequested_ = false;
            rebuildOperation();
        }
        notifyState();
        notifyView();
        return;
    }
    // Esc first leaves "Up to face" picking, keeping the value.
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && extrude->pickingTarget()) {
        extrude->setPickingTarget(false);
        notifyState();
        notifyView();
        return;
    }
    if (operation_ && operation_->value() != operation_->neutralValue()) {
        operation_->setValue(operation_->neutralValue(), *document_);
    } else {
        selection_.clear();
        rebuildOperation();
    }
    notifyState();
    notifyView();
}

// ---- Actions ---------------------------------------------------------------------

Status InteractionController::createBox(double size)
{
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {size, size, size};
    bool first = true;
    double maxX = 0, minY = 0;
    for (const auto& body : document_->bodies()) {
        const auto bb = geom::approximateBoundingBox(body->shape());
        if (!bb.valid)
            continue;
        maxX = first ? bb.max.x : std::max(maxX, bb.max.x);
        minY = first ? bb.min.y : std::min(minY, bb.min.y);
        first = false;
    }
    // The first box sits centered on the origin; later ones line up to the right.
    box->origin = first ? Vec3{-size / 2, -size / 2, 0} : Vec3{maxX + size * 0.5, minY, 0};

    Status status = undoStack_->push(std::make_unique<cmd::CreateBodyCommand>(document_->nextBodyName(), std::move(box)),
                                     *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    afterDocumentEdit();
    if (first)
        fitAll(true);
    return status;
}

Status InteractionController::importBodies(const std::vector<geom::NamedShape>& shapes, const std::string& source,
                                          std::uint64_t maxGeometryBytes)
{
    if (shapes.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to import.", "importBodies: no shapes");
    // What the project will have to store (the Imported steps' BRep text,
    // made now once and kept for saving): refused here, before anything
    // changes, rather than a project that cannot be saved.
    std::uint64_t stored = 0;
    for (const auto& body : document_->bodies())
        for (const auto& f : body->features())
            if (const auto* imported = dynamic_cast<const doc::ImportedFeature*>(f.get()))
                stored += imported->brepText().size();
    std::uint64_t adding = 0;
    std::vector<std::unique_ptr<doc::ImportedFeature>> features;
    for (const geom::NamedShape& shape : shapes) {
        if (shape.shape.isNull())
            continue;
        auto feature = std::make_unique<doc::ImportedFeature>();
        feature->setShape(shape.shape);
        feature->source = source;
        const std::uint64_t bytes = feature->brepText().size();
        adding += bytes;
        if (bytes > doc::kMaxImportedBodyBytes || stored + adding > maxGeometryBytes) {
            const auto mb = [](std::uint64_t n) { return std::to_string((n + (1u << 20) - 1) >> 20); };
            Status tooLarge = Status::failure(
                ErrorCode::Unsupported,
                "These parts are too large to keep in a project (at most " + mb(maxGeometryBytes) + " MB of imported geometry, "
                    + mb(doc::kMaxImportedBodyBytes) + " MB per body). Nothing was imported.",
                "importBodies: " + std::to_string(bytes) + " bytes for one body, " + std::to_string(stored + adding)
                    + " in all");
            message(tooLarge.userMessage());
            return tooLarge;
        }
        features.push_back(std::move(feature));
    }
    if (session_)
        finishSketch();
    // Like clicking elsewhere: a pending value is applied first.
    if (Status status = applyPendingValue("importBodies"); !status)
        return status;
    std::vector<std::string> taken;
    for (const auto& body : document_->bodies())
        taken.push_back(body->name());
    auto isTaken = [&](const std::string& name) { return std::find(taken.begin(), taken.end(), name) != taken.end(); };
    int unnamed = 1;
    std::vector<std::unique_ptr<cmd::Command>> steps;
    std::size_t next = 0;
    for (const geom::NamedShape& shape : shapes) {
        if (shape.shape.isNull())
            continue;
        std::string name = shape.name;
        if (name.empty()) {
            do
                name = "Imported " + std::to_string(unnamed++);
            while (isTaken(name));
        } else {
            const std::string base = name;
            for (int n = 2; isTaken(name); ++n)
                name = base + " " + std::to_string(n);
        }
        taken.push_back(name);
        steps.push_back(std::make_unique<cmd::CreateBodyCommand>(name, std::move(features[next++])));
    }
    if (steps.empty())
        return Status::failure(ErrorCode::InvalidArgument, "There is nothing to import.", "importBodies: only null shapes");
    const std::string label = steps.size() == 1 ? "Import " + taken.back() : "Import " + std::to_string(steps.size()) + " bodies";
    Status status = undoStack_->push(std::make_unique<cmd::CompositeCommand>(label, std::move(steps)), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    selection_.clear();
    afterDocumentEdit();
    fitAll(true);
    return status;
}

ThumbnailImage InteractionController::renderThumbnail(int size)
{
    scene_.update(*document_);
    std::vector<std::shared_ptr<const geom::Mesh>> meshes;
    for (const auto& body : document_->bodies())
        if (body->isVisible() && !body->shape().isNull())
            if (auto mesh = scene_.mesh(body->id()))
                meshes.push_back(std::move(mesh));
    return interact::renderThumbnail(meshes, size);
}

bool InteractionController::undo()
{
    dropTyping(); // a value typed for what undo takes away
    if (!undoStack_->undo(*document_))
        return false;
    operation_.reset();
    afterDocumentEdit();
    return true;
}

bool InteractionController::redo()
{
    dropTyping();
    Status status = undoStack_->redo(*document_);
    if (!status) {
        if (undoStack_->canRedo() || status.error() != ErrorCode::InvalidArgument)
            message(status.userMessage());
        return false;
    }
    operation_.reset();
    afterDocumentEdit();
    return true;
}

Status InteractionController::deleteSelectedBodies()
{
    std::vector<Uuid> bodies;
    for (const auto& item : selection_.items())
        if (item.kind == sel::SelectionKind::Body)
            bodies.push_back(item.bodyId);
    if (bodies.empty())
        return Status::failure(ErrorCode::InvalidArgument, "Select a body to delete.", "delete without body selection");
    selection_.clear();
    Status status = deleteBodies(bodies);
    if (!status)
        message(status.userMessage());
    return status;
}

namespace {

// "A", "A and B", "A, B and C", "A, B and 3 more".
std::string nameList(const std::vector<std::string>& names)
{
    std::string out;
    const std::size_t shown = names.size() > 3 ? 2 : names.size();
    for (std::size_t i = 0; i < shown; ++i)
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    if (shown < names.size())
        out += " and " + std::to_string(names.size() - shown) + " more";
    return out;
}

} // namespace

Status InteractionController::deleteBodies(const std::vector<Uuid>& bodies)
{
    auto contains = [](const std::vector<Uuid>& list, const Uuid& id) { return std::find(list.begin(), list.end(), id) != list.end(); };
    // Bodies built from a body that stays (a piece split off it, its separate
    // copy, a body that consumed it as a tool) would break: it is hidden
    // instead. Repeated until stable, since keeping one can keep its sources.
    std::vector<Uuid> remove;
    for (const Uuid& id : bodies)
        if (document_->body(id) && !contains(remove, id))
            remove.push_back(id);
    std::vector<Uuid> kept;
    for (bool again = true; again;) {
        again = false;
        for (auto it = remove.begin(); it != remove.end(); ++it) {
            const auto users = document_->bodiesUsing(*it);
            if (std::any_of(users.begin(), users.end(), [&](const Uuid& user) { return !contains(remove, user); })) {
                kept.push_back(*it);
                remove.erase(it);
                again = true;
                break;
            }
        }
    }
    const std::vector<Uuid> deleted = remove;
    // The bodies built from others go first, so every deletion leaves the rest
    // intact (and undo restores sources before what is built from them).
    std::vector<std::unique_ptr<cmd::Command>> steps;
    while (!remove.empty()) {
        auto leaf = std::find_if(remove.begin(), remove.end(), [&](const Uuid& id) {
            const auto users = document_->bodiesUsing(id);
            return std::none_of(users.begin(), users.end(), [&](const Uuid& user) { return contains(remove, user); });
        });
        if (leaf == remove.end())
            leaf = remove.begin(); // a cycle (only in a hand-edited file)
        steps.push_back(std::make_unique<cmd::DeleteBodyCommand>(*leaf));
        remove.erase(leaf);
    }
    std::vector<std::string> hiddenNames, userNames;
    for (const Uuid& id : kept) {
        const doc::Body* body = document_->body(id);
        if (body->isVisible()) {
            steps.push_back(std::make_unique<cmd::SetBodyVisibilityCommand>(id, false));
            hiddenNames.push_back(body->name());
        }
        for (const Uuid& user : document_->bodiesUsing(id))
            if (const doc::Body* u = document_->body(user); u && !contains(kept, user) && !contains(deleted, user)
                && std::find(userNames.begin(), userNames.end(), u->name()) == userNames.end())
                userNames.push_back(u->name());
    }
    if (steps.empty()) {
        if (kept.empty())
            return Status::failure(ErrorCode::InvalidReference, "That body no longer exists.", "delete: unknown bodies");
        const doc::Body& first = *document_->body(kept.front());
        return Status::failure(ErrorCode::InvalidArgument,
                               first.name() + " cannot be deleted: " + nameList(userNames)
                                   + (userNames.size() == 1 ? " is" : " are") + " built from it.",
                               "delete: other bodies depend on it");
    }
    std::unique_ptr<cmd::Command> command;
    if (steps.size() == 1)
        command = std::move(steps.front());
    else
        command = std::make_unique<cmd::CompositeCommand>(kept.empty() ? "Delete bodies" : "Delete", std::move(steps));
    Status status = undoStack_->push(std::move(command), *document_);
    if (!status)
        return status;
    operation_.reset();
    if (!hiddenNames.empty())
        message(nameList(hiddenNames) + (hiddenNames.size() == 1 ? " is" : " are") + " hidden, not deleted: "
                + nameList(userNames) + (userNames.size() == 1 ? " is" : " are") + " built from "
                + (hiddenNames.size() == 1 ? "it." : "them."));
    afterDocumentEdit();
    return status;
}

Status InteractionController::deleteSelectedFaces()
{
    if (!selection_.allOfKind(sel::SelectionKind::Face) || !selection_.singleBody())
        return Status::failure(ErrorCode::InvalidArgument, "Select faces of one body to remove.", "deleteFaces: selection");
    const Uuid bodyId = *selection_.singleBody();
    const doc::Body* body = document_->body(bodyId);
    if (!body)
        return Status::failure(ErrorCode::InvalidReference, "That body no longer exists.", "deleteFaces: body");
    auto feature = std::make_unique<doc::DeleteFacesFeature>();
    for (const auto& item : selection_.items()) {
        const auto signature = geom::captureFaceSignature(body->shape(), item.index);
        if (!signature)
            return Status::failure(ErrorCode::InvalidReference, "A selected face no longer exists.", "deleteFaces: face");
        feature->faces.push_back({item.index, *signature});
    }
    Status status = undoStack_->push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(feature)), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    selection_.clear();
    afterDocumentEdit();
    return status;
}

std::vector<ContextAction> InteractionController::contextActions() const
{
    if (session_)
        return session_->contextActions();
    std::vector<ContextAction> actions;
    if (const auto* construct = dynamic_cast<const DatumOperation*>(operation_.get())) {
        using Mode = DatumOperation::Mode;
        const Mode mode = construct->mode();
        if (construct->kind() == doc::DatumKind::Axis) {
            actions.push_back({"datum:axis", "Through / along", mode == Mode::Axis});
            actions.push_back({"datum:twoPoints", "Two points", mode == Mode::AxisTwoPoints});
            for (int axis = 0; axis < 3; ++axis)
                actions.push_back({"datum:parallel:" + std::to_string(axis), std::string("Parallel to ") + "XYZ"[axis],
                                   mode == Mode::AxisParallel && construct->originIndex() == axis});
        } else {
            const bool fromOrigin = mode == Mode::PlaneOffset && construct->originIndex() >= 0;
            actions.push_back({"datum:offset", "Offset from face", mode == Mode::PlaneOffset && !fromOrigin});
            static const char* planes[] = {"From YZ", "From XZ", "From XY"};
            for (int axis : {2, 1, 0})
                actions.push_back({"datum:origin:" + std::to_string(axis), planes[axis],
                                   fromOrigin && construct->originIndex() == axis});
            actions.push_back({"datum:angle", "At angle", mode == Mode::PlaneAngle});
            actions.push_back({"datum:midway", "Midway", mode == Mode::PlaneMidway});
        }
        if (construct->canCommit())
            actions.push_back({"apply", "Apply", false});
        return actions;
    }
    if (const auto* mirror = dynamic_cast<const MirrorOperation*>(operation_.get())) {
        actions.push_back({"plane:0", "Across YZ", mirror->originPlane() == 0});
        actions.push_back({"plane:1", "Across XZ", mirror->originPlane() == 1});
        actions.push_back({"plane:2", "Across XY", mirror->originPlane() == 2});
        actions.push_back({"separate", "Separate bodies", mirror->separate()});
        if (mirror->canCommit())
            actions.push_back({"apply", "Apply", false});
        actions.push_back({"move", "Move", false});
        actions.push_back({"rotate", "Rotate", false});
        actions.push_back({"pattern", "Pattern", false});
        return actions;
    }
    if (const auto* pattern = dynamic_cast<const PatternOperation*>(operation_.get())) {
        actions.push_back({"layout:linear", "Linear", !pattern->circular()});
        actions.push_back({"layout:circular", "Circular", pattern->circular()});
        for (int axis = 0; axis < 3; ++axis)
            actions.push_back({"axis:" + std::to_string(axis),
                               std::string(pattern->circular() ? "Around " : "Along ") + "XYZ"[axis],
                               pattern->axisIndex() == axis});
        actions.push_back({"fewer", "\xE2\x88\x92 copy", false});
        actions.push_back({"more", "+ copy", false});
        actions.push_back({"separate", "Separate bodies", pattern->separate()});
        return actions;
    }
    if (const auto* hole = dynamic_cast<const HoleOperation*>(operation_.get())) {
        const auto& screws = doc::metricScrews();
        const HoleSettings& s = hole->settings();
        const bool preset = std::abs(hole->diameter() - hole->presetDiameter()) < 1e-9;
        for (std::size_t i = 0; i < screws.size(); ++i)
            actions.push_back({"size:" + std::to_string(i), screws[i].name, preset && s.screw == i});
        actions.push_back({"fit:close", "Close fit", preset && s.fit == doc::HoleFit::Close});
        actions.push_back({"fit:normal", "Normal fit", preset && s.fit == doc::HoleFit::Normal});
        actions.push_back({"fit:tap", "Tap", preset && s.fit == doc::HoleFit::Tap});
        actions.push_back({"throughAll", "Through all", s.throughAll});
        actions.push_back({"head:counterbore", "Counterbore", s.head == doc::HoleKind::Counterbore});
        actions.push_back({"head:countersink", "Countersink", s.head == doc::HoleKind::Countersink});
        using Field = HoleOperation::Field;
        actions.push_back({"field:diameter", "\xC3\x98", hole->field() == Field::Diameter});
        if (!s.throughAll)
            actions.push_back({"field:depth", "Depth", hole->field() == Field::Depth});
        if (hole->current() >= 0) {
            actions.push_back({"field:x", "X", hole->field() == Field::X});
            actions.push_back({"field:y", "Y", hole->field() == Field::Y});
            actions.push_back({"removeHole", "Remove hole", false});
        }
        if (hole->positions().size() >= 2)
            actions.push_back({"fromLast", "From last hole", hole->fromLastHole()});
        return actions;
    }
    if (const auto* text = dynamic_cast<const TextOperation*>(operation_.get())) {
        using Field = TextOperation::Field;
        actions.push_back({"emboss", "Emboss", text->depth() > 0});
        actions.push_back({"deboss", "Deboss", text->depth() < 0});
        actions.push_back({"field:depth", "Depth", text->field() == Field::Depth});
        actions.push_back({"field:size", "Size", text->field() == Field::Size});
        actions.push_back({"field:angle", "Angle", text->field() == Field::Angle});
        for (int degrees : {0, 90, 180, 270})
            actions.push_back({"angle:" + std::to_string(degrees), std::to_string(degrees) + "\xC2\xB0",
                               std::abs(text->angleDegrees() - degrees) < 1e-9});
        if (geom::hasFont(doc::kTextFontBold))
            actions.push_back({"bold", "Bold", text->bold()});
        return actions;
    }
    if (const auto* align = dynamic_cast<const AlignOperation*>(operation_.get())) {
        actions.push_back({"flip", "Flip", align->flipped()});
        if (align->canUseGround())
            actions.push_back({"ground", "Onto ground", align->targetIsGround()});
        const auto origin = align->originTarget();
        for (const auto& [id, label, target] : kOriginTargets)
            actions.push_back({id, label, origin == target});
        return actions;
    }
    if (const auto* revolve = dynamic_cast<const RevolveOperation*>(operation_.get())) {
        actions.push_back({"extrude", "Extrude", false});
        actions.push_back({"revolve", "Revolve", true});
        actions.push_back({"axis:y", "Axis: vertical", revolve->axis() == doc::SketchAxis::Y});
        actions.push_back({"axis:x", "Axis: horizontal", revolve->axis() == doc::SketchAxis::X});
        if (revolve->hasHost()) {
            actions.push_back({"mode:new", "New body", revolve->mode() == doc::ExtrudeMode::NewBody});
            actions.push_back({"mode:join", "Join", revolve->mode() == doc::ExtrudeMode::Join});
            actions.push_back({"mode:cut", "Cut", revolve->mode() == doc::ExtrudeMode::Cut});
        }
        actions.push_back({"editSketch", "Edit sketch", false});
        return actions;
    }
    if (const auto* extrude = dynamic_cast<const ExtrudeOperation*>(operation_.get())) {
        actions.push_back({"extrude", "Extrude", true});
        actions.push_back({"revolve", "Revolve", false});
        if (extrude->hasHost()) {
            const auto mode = extrude->mode();
            actions.push_back({"mode:new", "New body", mode == doc::ExtrudeMode::NewBody});
            actions.push_back({"mode:join", "Join", mode == doc::ExtrudeMode::Join});
            actions.push_back({"mode:cut", "Cut", mode == doc::ExtrudeMode::Cut});
            if (mode == doc::ExtrudeMode::Cut)
                actions.push_back({"throughAll", "Through all", extrude->throughAll()});
        }
        actions.push_back({"symmetric", "Symmetric", extrude->symmetric()});
        // The draft angle is typed in the chip; the button shows it when set.
        const double draft = extrude->draftDegrees();
        actions.push_back({"draft", draft == 0 ? std::string("Draft") : "Draft " + formatAngle(draft * kPi / 180.0),
                           extrude->editingDraft()});
        actions.push_back({"upToFace", "Up to face", extrude->pickingTarget()});
        actions.push_back({"editSketch", "Edit sketch", false});
        return actions;
    }
    if (selection_.empty())
        return actions;
    if (selection_.allOfKind(sel::SelectionKind::Datum)) {
        const doc::Datum* datum = selection_.size() == 1 ? document_->datum(selection_.items().front().bodyId) : nullptr;
        if (datum && datum->kind() == doc::DatumKind::Plane)
            actions.push_back({"sketch", "Sketch", false});
        actions.push_back({"hideDatum", "Hide", false});
        actions.push_back({"delete", "Delete", false});
        return actions;
    }
    if (operation_ && (operation_->featureKind() == doc::FeatureKind::PushPull
                       || operation_->featureKind() == doc::FeatureKind::Shell
                       || operation_->featureKind() == doc::FeatureKind::OffsetFace)) {
        const bool single = selection_.size() == 1;
        SelectionMemo& memo = selectionMemo();
        if (single && !memo.planarFace) {
            memo.planarFace = false;
            if (const doc::Body* body = document_->body(selection_.items().front().bodyId))
                if (const auto info = geom::faceInfo(body->shape(), selection_.items().front().index))
                    memo.planarFace = info->isPlanar();
        }
        const bool planar = single && *memo.planarFace;
        if (single && planar)
            actions.push_back({"pushpull", "Push/Pull", operation_->featureKind() == doc::FeatureKind::PushPull});
        actions.push_back({"shell", "Shell", operation_->featureKind() == doc::FeatureKind::Shell});
        if (single && planar)
            actions.push_back({"sketch", "Sketch", false});
        if (single && planar)
            actions.push_back({"hole", "Hole", false});
        if (single && planar)
            actions.push_back({"text", "Text", false});
        if (single && !planar)
            actions.push_back({"offset", "Offset", operation_->featureKind() == doc::FeatureKind::OffsetFace});
        if (single)
            actions.push_back({"align", "Align", false});
        actions.push_back({"deleteFaces", single ? "Delete face" : "Delete faces", false});
    } else if (selection_.allOfKind(sel::SelectionKind::Edge) && operation_) {
        actions.push_back({"fillet", "Fillet", edgeOperationKind_ == doc::FeatureKind::Fillet});
        actions.push_back({"chamfer", "Chamfer", edgeOperationKind_ == doc::FeatureKind::Chamfer});
        // A hole's rim: offer the heat-set insert helper.
        SelectionMemo& memo = selectionMemo();
        if (!memo.holeRim) {
            memo.holeRim = false;
            if (selection_.size() == 1)
                if (const doc::Body* body = document_->body(selection_.items().front().bodyId))
                    memo.holeRim = doc::holePlacement(body->shape(), selection_.items().front().index).has_value();
        }
        const bool rim = *memo.holeRim;
        if (rim) {
            const bool hole = edgeOperationKind_ == doc::FeatureKind::Hole;
            actions.push_back({"insert", "Heat-set insert", hole && rimHoleKind_ == doc::HoleKind::Plain});
            actions.push_back({"counterbore", "Counterbore", hole && rimHoleKind_ == doc::HoleKind::Counterbore});
            actions.push_back({"countersink", "Countersink", hole && rimHoleKind_ == doc::HoleKind::Countersink});
        }
        if (selection_.size() == 1)
            actions.push_back({"align", "Align", false});
        if (const auto* insert = dynamic_cast<const InsertOperation*>(operation_.get())) {
            const auto& presets = doc::heatSetInsertPresets();
            for (std::size_t i = 0; i < presets.size(); ++i)
                actions.push_back({"preset:" + std::to_string(i), presets[i].name, insert->presetIndex() == i});
        }
        if (const auto* head = dynamic_cast<const HeadOperation*>(operation_.get())) {
            const auto& screws = doc::metricScrews();
            for (std::size_t i = 0; i < screws.size(); ++i)
                actions.push_back({"preset:" + std::to_string(i), screws[i].name, head->presetIndex() == i});
        }
    }
    if (selection_.allOfKind(sel::SelectionKind::Body)) {
        if (selection_.size() == 1) {
            actions.push_back({"move", "Move", dynamic_cast<const MoveOperation*>(operation_.get()) != nullptr});
            const auto* rotate = dynamic_cast<const RotateOperation*>(operation_.get());
            actions.push_back({"rotate", "Rotate", rotate != nullptr});
            if (rotate && rotate->hasCustomPivot())
                actions.push_back({"pivotCenter", "Center pivot", false});
            actions.push_back({"mirror", "Mirror", dynamic_cast<const MirrorOperation*>(operation_.get()) != nullptr});
            actions.push_back({"pattern", "Pattern", dynamic_cast<const PatternOperation*>(operation_.get()) != nullptr});
            actions.push_back({"duplicate", "Duplicate", false});
            if (const doc::Body* body = document_->body(selection_.items().front().bodyId); body && body->shape().solidCount() > 1)
                actions.push_back({"split", "Split into bodies", false});
        }
        if (selection_.size() >= 2) {
            // The first body is kept; the others are the tools.
            const doc::Body* second = document_->body(selection_.items()[1].bodyId);
            const std::string tools = selection_.size() == 2 && second ? second->name()
                                                                       : std::to_string(selection_.size() - 1) + " bodies";
            actions.push_back({"union", "Union", false});
            actions.push_back({"subtract", "Subtract " + tools, false});
            actions.push_back({"intersect", "Intersect", false});
            if (selection_.size() == 2)
                actions.push_back({"swap", "Swap", false});
        }
        actions.push_back({"fit", "Zoom to", false});
        actions.push_back({"delete", "Delete", false});
    } else if (selection_.singleBody()) {
        actions.push_back({"selectBody", "Select body", false});
    }
    return actions;
}

Status InteractionController::triggerAction(const std::string& id)
{
    // A value typed just now belongs to the value it was typed into, not to
    // the field or mode the action switches to.
    (void)flushTyping();
    if (session_) {
        Status status = session_->triggerAction(id);
        notifyState();
        notifyView();
        return status;
    }
    if (auto* construct = dynamic_cast<DatumOperation*>(operation_.get()); construct && id.rfind("datum:", 0) == 0) {
        using Mode = DatumOperation::Mode;
        if (id == "datum:axis")
            construct->setMode(Mode::Axis, *document_);
        else if (id == "datum:twoPoints")
            construct->setMode(Mode::AxisTwoPoints, *document_);
        else if (id.rfind("datum:parallel:", 0) == 0)
            construct->setParallelTo(std::stoi(id.substr(15)), *document_);
        else if (id == "datum:offset" && construct->mode() == Mode::PlaneOffset && construct->originIndex() >= 0)
            (void)construct->dropLastPick(*document_); // back to picking a face
        else if (id == "datum:offset")
            construct->setMode(Mode::PlaneOffset, *document_);
        else if (id.rfind("datum:origin:", 0) == 0)
            construct->setOriginPlane(std::stoi(id.substr(13)), *document_);
        else if (id == "datum:angle")
            construct->setMode(Mode::PlaneAngle, *document_);
        else if (id == "datum:midway")
            construct->setMode(Mode::PlaneMidway, *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "hideDatum") {
        std::vector<Uuid> datums;
        for (const auto& item : selection_.items())
            if (item.kind == sel::SelectionKind::Datum)
                datums.push_back(item.bodyId);
        for (const Uuid& datum : datums)
            if (Status status = setDatumVisible(datum, false); !status)
                return status;
        return okStatus();
    }
    if (auto* hole = dynamic_cast<HoleOperation*>(operation_.get())) {
        using Field = HoleOperation::Field;
        bool handled = true;
        if (id.rfind("size:", 0) == 0)
            hole->setScrew(std::stoul(id.substr(5)), *document_);
        else if (id == "fit:close" || id == "fit:normal" || id == "fit:tap")
            hole->setFit(id == "fit:close" ? doc::HoleFit::Close : id == "fit:tap" ? doc::HoleFit::Tap : doc::HoleFit::Normal,
                         *document_);
        else if (id == "throughAll")
            hole->setThroughAll(!hole->settings().throughAll, *document_);
        else if (id == "head:counterbore" || id == "head:countersink")
            hole->setHead(id == "head:counterbore" ? doc::HoleKind::Counterbore : doc::HoleKind::Countersink, *document_);
        else if (id == "field:diameter" || id == "field:depth" || id == "field:x" || id == "field:y")
            hole->setField(id == "field:depth" ? Field::Depth
                           : id == "field:x" ? Field::X
                           : id == "field:y" ? Field::Y
                                             : Field::Diameter,
                           *document_);
        else if (id == "nextField")
            hole->nextField(*document_);
        else if (id == "fromLast")
            hole->setFromLastHole(!hole->fromLastHole(), *document_);
        else if (id == "removeHole")
            hole->removeCurrent(*document_);
        else if (id == "hole") {
            // Hole again (palette or face): keep the holes placed so far.
        } else
            handled = false;
        if (handled) {
            holeSettings_ = hole->settings();
            notifyState();
            notifyView();
            return okStatus();
        }
    }
    if (auto* text = dynamic_cast<TextOperation*>(operation_.get())) {
        using Field = TextOperation::Field;
        bool handled = true;
        if (id == "emboss" || id == "deboss")
            text->setRaised(id == "emboss", *document_);
        else if (id == "field:depth" || id == "field:size" || id == "field:angle")
            text->setField(id == "field:size" ? Field::Size : id == "field:angle" ? Field::Angle : Field::Depth, *document_);
        else if (id == "nextField")
            text->nextField(*document_);
        else if (id.rfind("angle:", 0) == 0)
            text->setAngleDegrees(std::strtod(id.c_str() + 6, nullptr), *document_);
        else if (id == "bold")
            text->setBold(!text->bold(), *document_);
        else if (id == "text") {
            // Text again (palette or face): keep what is typed.
        } else
            handled = false;
        if (handled) {
            textSettings_ = text->settings();
            notifyState();
            notifyView();
            return okStatus();
        }
    }
    if (id == "text") {
        if (!geom::hasFont(doc::kTextFontRegular)) {
            const std::string text = "Text is not available in this build: its font (Noto Sans) is missing.";
            message(text);
            return Status::failure(ErrorCode::Unsupported, text, "text: no font registered");
        }
        faceOperationKind_ = doc::FeatureKind::Text;
        rebuildOperation();
        if (!dynamic_cast<TextOperation*>(operation_.get())) {
            const std::string text = "Text goes on a flat face: select one, then Text.";
            message(text);
            notifyState();
            notifyView();
            return Status::failure(ErrorCode::InvalidArgument, text, "text: not a flat face");
        }
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "hole") {
        faceOperationKind_ = doc::FeatureKind::Holes;
        rebuildOperation();
        if (!dynamic_cast<HoleOperation*>(operation_.get())) {
            const std::string text = "Holes are drilled into a flat face: select one, then Hole.";
            message(text);
            notifyState();
            notifyView();
            return Status::failure(ErrorCode::InvalidArgument, text, "hole: not a flat face");
        }
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "revolve" || id == "extrude" || id.rfind("axis:", 0) == 0) {
        if (!selection_.allOfKind(sel::SelectionKind::SketchProfile))
            return okStatus();
        if (id == "revolve" || id.rfind("axis:", 0) == 0)
            profileOperationKind_ = doc::FeatureKind::Revolve;
        else
            profileOperationKind_ = doc::FeatureKind::Extrude;
        if (id == "axis:x")
            revolveAxis_ = doc::SketchAxis::X;
        else if (id == "axis:y")
            revolveAxis_ = doc::SketchAxis::Y;
        rebuildOperation();
        // A revolve is most often a full turn: preview it right away.
        if (auto* revolve = dynamic_cast<RevolveOperation*>(operation_.get()))
            revolve->setValue(360.0, *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* revolve = dynamic_cast<RevolveOperation*>(operation_.get()); revolve && id.rfind("mode:", 0) == 0) {
        revolve->setMode(id == "mode:join" ? doc::ExtrudeMode::Join : id == "mode:cut" ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::NewBody);
        revolve->setValue(revolve->value(), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id.rfind("mode:", 0) == 0) {
        const auto mode = id == "mode:join" ? doc::ExtrudeMode::Join : id == "mode:cut" ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::NewBody;
        extrude->setModeOverride(mode);
        extrude->setValue(extrude->value(), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id == "throughAll") {
        extrude->setThroughAll(!extrude->throughAll());
        extrude->setValue(extrude->value(), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id == "symmetric") {
        extrude->setSymmetric(!extrude->symmetric(), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id == "draft") {
        // The chip now edits the draft (again: back to the distance).
        extrude->setActiveHandle(extrude->editingDraft() ? 0 : 1);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id == "upToFace") {
        extrude->setPickingTarget(!extrude->pickingTarget());
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "editSketch") {
        if (const auto sketchId = selection_.singleBody(); sketchId && selection_.allOfKind(sel::SelectionKind::SketchProfile))
            return editSketch(*sketchId);
        return Status::failure(ErrorCode::InvalidArgument, "Select a sketch profile first.", "editSketch without profile");
    }
    if (id == "union" || id == "subtract" || id == "intersect")
        return combineSelectedBodies(id == "union" ? doc::CombineMode::Union
                                     : id == "subtract" ? doc::CombineMode::Subtract
                                                        : doc::CombineMode::Intersect);
    if (id == "duplicate") {
        // The selected body, or the body of the selected faces/edges.
        const auto body = selection_.allOfKind(sel::SelectionKind::SketchProfile) || selection_.allOfKind(sel::SelectionKind::Datum)
                            ? std::nullopt
                            : selection_.singleBody();
        if (!body)
            return Status::failure(ErrorCode::InvalidArgument,
                                   forInput("Select one body to duplicate: double-click it, or click it in the Model panel."),
                                   "duplicate without one body selected");
        return duplicateBody(*body);
    }
    if (id == "split") {
        const auto body = selection_.allOfKind(sel::SelectionKind::SketchProfile) ? std::nullopt : selection_.singleBody();
        if (!body)
            return Status::failure(ErrorCode::InvalidArgument, "Select the body to split into its separate pieces.",
                                   "split without one body selected");
        return splitBody(*body);
    }
    if (id == "move" || id == "rotate" || id == "mirror" || id == "pattern") {
        bodyTool_ = id == "rotate" ? BodyTool::Rotate : id == "mirror" ? BodyTool::Mirror
                  : id == "pattern" ? BodyTool::Pattern : BodyTool::Move;
        if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Body)
            rebuildOperation();
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "align") {
        alignRequested_ = true;
        rebuildOperation();
        if (!dynamic_cast<AlignOperation*>(operation_.get())) {
            const std::string text = "Align works with a flat or round face, a straight edge or a circle.";
            message(text);
            notifyState();
            notifyView();
            return Status::failure(ErrorCode::InvalidArgument, text, "align: unsupported source");
        }
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "apply")
        return commitOperation();
    if (id == "pivotCenter") {
        auto* rotate = dynamic_cast<RotateOperation*>(operation_.get());
        if (!rotate)
            return Status::failure(ErrorCode::InvalidArgument, "Center pivot belongs to Rotate.", "pivotCenter without rotate");
        rotate->resetPivot(*document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "separate") {
        if (auto* mirror = dynamic_cast<MirrorOperation*>(operation_.get()))
            mirror->setSeparate(!mirror->separate(), *document_);
        else if (auto* pattern = dynamic_cast<PatternOperation*>(operation_.get()))
            pattern->setSeparate(!pattern->separate(), *document_);
        else
            return Status::failure(ErrorCode::InvalidArgument, "Separate bodies is an option of Mirror and Pattern.",
                                   "separate without mirror/pattern");
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* mirror = dynamic_cast<MirrorOperation*>(operation_.get()); mirror && id.rfind("plane:", 0) == 0) {
        mirror->setOriginPlane(std::stoi(id.substr(6)), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* pattern = dynamic_cast<PatternOperation*>(operation_.get())) {
        bool handled = true;
        if (id == "layout:linear" || id == "layout:circular")
            pattern->setCircular(id == "layout:circular", *document_);
        else if (id.rfind("axis:", 0) == 0)
            pattern->setAxisIndex(std::stoi(id.substr(5)), *document_);
        else if (id == "fewer" || id == "more")
            pattern->setCount(pattern->count() + (id == "more" ? 1 : -1), *document_);
        else
            handled = false;
        if (handled) {
            notifyState();
            notifyView();
            return okStatus();
        }
    }
    if (auto* align = dynamic_cast<AlignOperation*>(operation_.get()); align && id.rfind("origin:", 0) == 0) {
        for (const auto& [originId, label, target] : kOriginTargets)
            if (id == originId)
                (void)align->setOriginTarget(target, *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* align = dynamic_cast<AlignOperation*>(operation_.get()); align && (id == "flip" || id == "ground")) {
        Status status = okStatus();
        if (id == "flip")
            align->setFlipped(!align->flipped(), *document_);
        else
            status = align->setGroundTarget(*document_);
        if (!status)
            message(status.userMessage());
        notifyState();
        notifyView();
        return status;
    }
    if (id == "swap") {
        // Subtract keeps the first body: swapping changes which one is cut.
        if (selection_.size() != 2 || !selection_.allOfKind(sel::SelectionKind::Body))
            return okStatus();
        const sel::SelectionItem first = selection_.items()[0];
        const sel::SelectionItem second = selection_.items()[1];
        selection_.set(second);
        selection_.add(first);
        rebuildOperation();
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "deleteFaces")
        return deleteSelectedFaces();
    if (id == "pushpull" || id == "shell" || id == "offset") {
        faceOperationKind_ = id == "shell" ? doc::FeatureKind::Shell
                           : id == "offset" ? doc::FeatureKind::OffsetFace
                                            : doc::FeatureKind::PushPull;
        rebuildOperation();
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "sketch")
        return startSketch();
    if (id == "insert" || id == "counterbore" || id == "countersink") {
        edgeOperationKind_ = doc::FeatureKind::Hole;
        rimHoleKind_ = id == "counterbore"   ? doc::HoleKind::Counterbore
                     : id == "countersink" ? doc::HoleKind::Countersink
                                           : doc::HoleKind::Plain;
        rebuildOperation();
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* insert = dynamic_cast<InsertOperation*>(operation_.get()); insert && id.rfind("preset:", 0) == 0) {
        insertPreset_ = std::stoul(id.substr(7));
        insert->setPreset(insertPreset_, *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (auto* head = dynamic_cast<HeadOperation*>(operation_.get()); head && id.rfind("preset:", 0) == 0) {
        screwPreset_ = std::stoul(id.substr(7));
        head->setPreset(screwPreset_, *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "fillet" || id == "chamfer") {
        const auto kind = id == "fillet" ? doc::FeatureKind::Fillet : doc::FeatureKind::Chamfer;
        // A fillet's radius carries over to a chamfer (not a hole's size).
        const double keep = dynamic_cast<const EdgeOperation*>(operation_.get()) ? operation_->value() : 0.0;
        edgeOperationKind_ = kind;
        rebuildOperation();
        if (operation_ && keep > 0)
            operation_->setValue(keep, *document_);
    } else if (id == "selectBody") {
        const auto body = selection_.singleBody();
        if (!body)
            return Status::failure(ErrorCode::InvalidArgument, "Select something on one body first.", "selectBody");
        if (auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, *body, -1))
            selection_.set(*item);
        rebuildOperation();
    } else if (id == "delete") {
        if (selection_.allOfKind(sel::SelectionKind::Datum))
            return deleteSelectedDatums();
        return deleteSelectedBodies();
    } else if (id == "fit") {
        fitSelection(true);
    } else {
        return Status::failure(ErrorCode::InvalidArgument, "Unknown action.", "unknown action '" + id + "'");
    }
    notifyState();
    notifyView();
    return okStatus();
}

// ---- State -----------------------------------------------------------------------

InteractionController::SelectionMemo& InteractionController::selectionMemo() const
{
    // What the cached facts depend on: the items, their bodies' shapes (or
    // sketches), the display unit.
    std::string key = std::to_string(static_cast<int>(document_->displayUnit()));
    for (const auto& item : selection_.items()) {
        key += '|' + std::to_string(static_cast<int>(item.kind)) + ':' + item.bodyId.toString() + ':' + std::to_string(item.index);
        const doc::Body* body = document_->body(item.bodyId);
        key += ':' + std::to_string(body ? body->shapeRevision() : document_->sketchRevision(item.bodyId));
        if (body)
            key += ':' + body->name(); // a summary of several bodies names them
    }
    if (key != selectionMemo_.key)
        selectionMemo_ = SelectionMemo{std::move(key), std::nullopt, std::nullopt, std::nullopt};
    return selectionMemo_;
}

std::string InteractionController::selectionSummary() const
{
    SelectionMemo& memo = selectionMemo();
    if (!memo.summary)
        memo.summary = computeSelectionSummary();
    return *memo.summary;
}

std::string InteractionController::computeSelectionSummary() const
{
    if (selection_.empty())
        return {};
    const LengthUnit unit = document_->displayUnit();
    const auto& first = selection_.items().front();
    if (first.kind == sel::SelectionKind::Datum) {
        if (selection_.size() > 1)
            return std::to_string(selection_.size()) + " axes and planes";
        const doc::Datum* datum = document_->datum(first.bodyId);
        return datum ? datum->name() + " \xC2\xB7 " + datumDetail(*datum, unit) : std::string();
    }
    if (first.kind == sel::SelectionKind::SketchProfile) {
        double area = 0;
        const auto* entry = scene_.sketch(first.bodyId);
        for (const auto& item : selection_.items())
            if (entry && item.index >= 0 && item.index < static_cast<int>(entry->regions.size()))
                area += entry->regions[std::size_t(item.index)].area;
        char text[96];
        std::snprintf(text, sizeof text, "%.2f %s\xC2\xB2", fromMillimeters(fromMillimeters(area, unit), unit),
                      std::string(unitSymbol(unit)).c_str());
        const std::string noun = selection_.size() == 1 ? "Profile" : std::to_string(selection_.size()) + " profiles";
        return noun + " \xC2\xB7 " + text;
    }
    const doc::Body* body = document_->body(first.bodyId);
    if (!body)
        return {};
    // Two faces/edges (on any bodies): measure between them.
    if (selection_.size() == 2) {
        auto refOf = [&](const sel::SelectionItem& item) -> std::optional<geom::SubShapeRef> {
            const doc::Body* b = document_->body(item.bodyId);
            if (!b || (item.kind != sel::SelectionKind::Face && item.kind != sel::SelectionKind::Edge))
                return std::nullopt;
            return geom::SubShapeRef{&b->shape(),
                                     item.kind == sel::SelectionKind::Face ? geom::SubShapeKind::Face : geom::SubShapeKind::Edge,
                                     item.index};
        };
        const auto a = refOf(selection_.items()[0]);
        const auto b = refOf(selection_.items()[1]);
        if (a && b) {
            if (const auto m = geom::measure(*a, *b)) {
                if (m->parallelGap)
                    return "Gap " + formatLength(*m->parallelGap, unit) + " · parallel";
                std::string text = "Distance " + formatLength(m->distance, unit);
                if (m->angle)
                    text += " · Angle " + formatAngle(*m->angle);
                return text;
            }
        }
    }
    if (selection_.size() > 1 && selection_.allOfKind(sel::SelectionKind::Body) && selection_.size() <= 3) {
        std::string names;
        for (const auto& item : selection_.items())
            if (const doc::Body* b = document_->body(item.bodyId))
                names += (names.empty() ? "" : " + ") + b->name();
        return names;
    }
    if (selection_.size() > 1) {
        const char* noun = first.kind == sel::SelectionKind::Edge ? " edges" : first.kind == sel::SelectionKind::Face ? " faces" : " bodies";
        return std::to_string(selection_.size()) + noun;
    }
    switch (first.kind) {
    case sel::SelectionKind::Face:
        if (const auto info = geom::faceInfo(body->shape(), first.index)) {
            char area[64];
            std::snprintf(area, sizeof area, "%.2f %s\xC2\xB2", fromMillimeters(fromMillimeters(info->area, unit), unit),
                          std::string(unitSymbol(unit)).c_str());
            return surfaceName(info->kind) + " \xC2\xB7 " + area;
        }
        break;
    case sel::SelectionKind::Edge:
        if (const auto info = geom::edgeInfo(body->shape(), first.index)) {
            std::string text = curveName(info->kind) + " \xC2\xB7 " + formatLength(info->length, unit);
            if (info->kind == geom::CurveKind::Circle)
                text += " \xC2\xB7 R " + formatLength(info->radius, unit);
            return text;
        }
        break;
    case sel::SelectionKind::Body: {
        const auto bb = geom::boundingBox(body->shape());
        if (!bb.valid)
            break;
        const auto size = bb.size();
        char text[160];
        std::snprintf(text, sizeof text, "%.2f \xC3\x97 %.2f \xC3\x97 %.2f %s", fromMillimeters(size.x, unit),
                      fromMillimeters(size.y, unit), fromMillimeters(size.z, unit), std::string(unitSymbol(unit)).c_str());
        return body->name() + " \xC2\xB7 " + text;
    }
    default:
        break;
    }
    return {};
}

RenderScene InteractionController::renderScene() const
{
    RenderScene scene;
    scene.camera = camera_;
    for (const auto& body : document_->bodies()) {
        if (!body->isVisible())
            continue;
        RenderBody rb;
        rb.id = body->id();
        if (operation_ && operation_->hasPreview() && !operation_->previewMeshBody().isNil()
            && operation_->previewMeshBody() == body->id()) {
            rb.mesh = operation_->previewMesh();
            rb.meshKey = operation_->previewKey();
            rb.isPreview = true;
        } else {
            rb.mesh = scene_.mesh(body->id());
            rb.meshKey = scene_.revision(body->id());
            if (hover_.hit() && hover_.bodyId == body->id()) {
                if (hover_.kind == sel::PickKind::Face)
                    rb.hoverFace = hover_.index;
                else
                    rb.hoverEdge = hover_.index;
            }
            for (const auto& item : selection_.items()) {
                if (item.bodyId != body->id())
                    continue;
                if (item.kind == sel::SelectionKind::Face)
                    rb.selectedFaces.push_back(item.index);
                else if (item.kind == sel::SelectionKind::Edge)
                    rb.selectedEdges.push_back(item.index);
                else if (item.kind == sel::SelectionKind::Body)
                    rb.selected = true;
            }
            if (highlightBody_ == body->id())
                rb.highlightFaces = highlightFaces_;
            // What the Axis / Plane tool picked is shown like a selection.
            if (const auto* construct = dynamic_cast<const DatumOperation*>(operation_.get()))
                for (const auto& picked : construct->picked())
                    if (picked.body == body->id())
                        (picked.kind == geom::SubShapeKind::Face ? rb.selectedFaces : rb.selectedEdges).push_back(picked.index);
            // Align's target is shown like a selection on its body.
            if (const auto* align = dynamic_cast<const AlignOperation*>(operation_.get());
                align && align->hasTarget() && align->targetBody() == body->id()) {
                if (align->targetKind() == geom::SubShapeKind::Face)
                    rb.selectedFaces.push_back(align->targetIndex());
                else if (align->targetKind() == geom::SubShapeKind::Edge)
                    rb.selectedEdges.push_back(align->targetIndex());
            }
        }
        if (rb.mesh)
            scene.bodies.push_back(std::move(rb));
    }

    // Construction axes and planes (and the one the Axis / Plane tool is making).
    addDatumDrawing(scene);

    // A new-body preview has no document body to stand in for.
    if (operation_ && operation_->hasPreview() && operation_->previewMeshBody().isNil()) {
        RenderBody rb;
        rb.mesh = operation_->previewMesh();
        rb.meshKey = operation_->previewKey();
        rb.isPreview = true;
        scene.bodies.push_back(std::move(rb));
    }

    // Sketches: the one being edited in full detail, the others as curves
    // plus selectable profile fills.
    for (const auto& sk : document_->sketches()) {
        if (session_ && sk->id() == session_->sketchId()) {
            scene.sketches.push_back(session_->renderData(camera_));
            continue;
        }
        // Hovering a sketch in the model panel shows it, even when hidden.
        const bool highlighted = historyHighlight_ && *historyHighlight_ == sk->id();
        if (!sk->isVisible() && !highlighted)
            continue;
        RenderSketch rs;
        const sketch::Plane& plane = sk->plane();
        // Sketches already used by a feature recede: thin grey curves, and
        // profile fills only while hovered or selected.
        const bool consumed = !document_->dependentFeatures(sk->id()).empty();
        const SketchStyle curveStyle = highlighted ? SketchStyle::Hovered
                                     : consumed    ? SketchStyle::Construction
                                                   : SketchStyle::Normal;
        for (const auto& [id, l] : sk->lines())
            rs.lines.push_back({plane.toWorld(sk->point(l.start)->position), plane.toWorld(sk->point(l.end)->position),
                                l.construction ? SketchStyle::Construction : curveStyle});
        for (const auto& [id, c] : sk->circles()) {
            constexpr int segments = 72;
            const Vec2 center = sk->point(c.center)->position;
            for (int i = 0; i < segments; ++i) {
                const double a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
                rs.lines.push_back({plane.toWorld(center + Vec2{std::cos(a0), std::sin(a0)} * c.radius),
                                    plane.toWorld(center + Vec2{std::cos(a1), std::sin(a1)} * c.radius),
                                    c.construction ? SketchStyle::Construction : curveStyle});
            }
        }
        for (const auto& [id, a] : sk->arcs()) {
            const Vec2 center = sk->point(a.center)->position;
            const Vec2 s = sk->point(a.start)->position, e = sk->point(a.end)->position;
            const double a0 = std::atan2(s.y - center.y, s.x - center.x);
            double sweep = std::atan2(e.y - center.y, e.x - center.x) - a0;
            while (sweep <= 0)
                sweep += 2 * kPi;
            const double r = sk->arcRadius(id);
            const int n = std::max(2, int(std::ceil(sweep / (2 * kPi) * 72)));
            for (int i = 0; i < n; ++i) {
                const double t0 = a0 + sweep * i / n, t1 = a0 + sweep * (i + 1) / n;
                rs.lines.push_back({plane.toWorld(center + Vec2{std::cos(t0), std::sin(t0)} * r),
                                    plane.toWorld(center + Vec2{std::cos(t1), std::sin(t1)} * r),
                                    a.construction ? SketchStyle::Construction : curveStyle});
            }
        }
        if (const auto* entry = scene_.sketch(sk->id())) {
            for (std::size_t i = 0; i < entry->meshes.size(); ++i) {
                SketchStyle style = highlighted ? SketchStyle::Hovered : SketchStyle::Normal;
                if (hover_.kind == sel::PickKind::Profile && hover_.bodyId == sk->id() && hover_.index == static_cast<int>(i))
                    style = SketchStyle::Hovered;
                for (const auto& item : selection_.items())
                    if (item.kind == sel::SelectionKind::SketchProfile && item.bodyId == sk->id()
                        && item.index == static_cast<int>(i))
                        style = SketchStyle::Selected;
                if (consumed && style == SketchStyle::Normal)
                    continue;
                rs.regions.push_back({entry->meshes[i], entry->meshKeys[i], style, false});
            }
        }
        scene.sketches.push_back(std::move(rs));
    }

    if (operation_) {
        for (int i = 0; i < operation_->handleCount(); ++i) {
            const LinearManipulator handle = operation_->handle(i);
            RenderArrow arrow;
            arrow.anchor = handle.anchor(operation_->handleOffset(i));
            arrow.direction = handle.direction();
            arrow.axis = operation_->handleAxis(i);
            const bool active = i == operation_->activeHandle();
            if (!operation_->error().empty() && active)
                arrow.state = HandleState::Error;
            else if (drag_.mode == DragMode::Manipulator && active)
                arrow.state = HandleState::Active;
            else if (hoveredHandle_ == i)
                arrow.state = HandleState::Hovered;
            scene.arrows.push_back(arrow);
        }
        for (int i = 0; i < operation_->ringCount(); ++i) {
            const RingManipulator ring = operation_->ring(i);
            const bool active = i == operation_->activeHandle();
            RenderRing rr;
            constexpr int segments = 72;
            for (int k = 0; k <= segments; ++k)
                rr.points.push_back(ring.pointAt(camera_, 2 * kPi * k / segments));
            rr.marker = ring.pointAt(camera_, active ? operation_->value() * kPi / 180.0 : 0.0);
            rr.axis = operation_->handleAxis(i);
            if (!operation_->error().empty() && active)
                rr.state = HandleState::Error;
            else if (drag_.mode == DragMode::Manipulator && drag_.ring == i)
                rr.state = HandleState::Active;
            else if (hoveredRing_ == i)
                rr.state = HandleState::Hovered;
            scene.rings.push_back(std::move(rr));
        }
        // A push/pull that measures the thickness shows it as a dimension line
        // through the part, from the opposite face to the face (as dragged).
        if (const auto* push = dynamic_cast<const PushPullOperation*>(operation_.get()); push && push->thickness()) {
            RenderSketch guide;
            guide.editing = true; // on top of the bodies, like a sketch dimension
            guide.lines.push_back({push->thickness()->to, push->anchor(), SketchStyle::Measure});
            guide.points.push_back({push->thickness()->to, SketchStyle::Measure});
            scene.sketches.push_back(std::move(guide));
        }
        // The Hole tool: the current hole (whose X / Y the chip edits, with
        // lines from where they are measured) and where a click would add one.
        if (const auto* hole = dynamic_cast<const HoleOperation*>(operation_.get())) {
            RenderSketch guide;
            guide.editing = true;
            const doc::HoleFrame& frame = hole->frame();
            const double r = hole->diameter() / 2;
            auto circle = [&](Vec2 c, SketchStyle style) {
                constexpr int segments = 48;
                for (int i = 0; i < segments; ++i) {
                    const double a0 = 2 * kPi * i / segments, a1 = 2 * kPi * (i + 1) / segments;
                    guide.lines.push_back({frame.toWorld(c + Vec2{std::cos(a0), std::sin(a0)} * r),
                                           frame.toWorld(c + Vec2{std::cos(a1), std::sin(a1)} * r), style});
                }
                guide.points.push_back({frame.toWorld(c), style});
            };
            if (hole->current() >= 0) {
                const Vec2 c = hole->livePositions(hole->value())[std::size_t(hole->current())];
                const Vec2 from = hole->reference();
                circle(c, SketchStyle::Selected);
                guide.lines.push_back({frame.toWorld(from), frame.toWorld({c.x, from.y}), SketchStyle::Dimension});
                guide.lines.push_back({frame.toWorld({c.x, from.y}), frame.toWorld(c), SketchStyle::Dimension});
            }
            if (hole->hover())
                circle(*hole->hover(), SketchStyle::Hovered);
            scene.sketches.push_back(std::move(guide));
        }
        // The Text tool: the text's center, and where a click would move it.
        if (const auto* text = dynamic_cast<const TextOperation*>(operation_.get())) {
            RenderSketch guide;
            guide.editing = true;
            guide.points.push_back({text->center(), SketchStyle::Selected});
            if (text->hover())
                guide.points.push_back({text->frame().toWorld(*text->hover()), SketchStyle::Hovered});
            scene.sketches.push_back(std::move(guide));
        }
    }

    scene.grid = groundGrid();
    // Align aiming at an origin axis or plane shows it, and an axis line
    // under the pointer shows it can be clicked.
    {
        RenderSketch marks;
        auto axisLine = [&](int axis, SketchStyle style) {
            if (const auto segment = originAxisSegment(scene.grid, axis))
                marks.lines.push_back({segment->first, segment->second, style});
        };
        if (hover_.kind == sel::PickKind::OriginAxis)
            axisLine(hover_.index, SketchStyle::Hovered);
        if (const auto* align = dynamic_cast<const AlignOperation*>(operation_.get()); align && align->originTarget()) {
            switch (*align->originTarget()) {
            case OriginTarget::XAxis: axisLine(0, SketchStyle::Selected); break;
            case OriginTarget::YAxis: axisLine(1, SketchStyle::Selected); break;
            case OriginTarget::ZAxis: axisLine(2, SketchStyle::Selected); break;
            case OriginTarget::XYPlane:
            case OriginTarget::XZPlane:
            case OriginTarget::YZPlane: {
                // An outline square around the origin, in the plane.
                const auto target = *align->originTarget();
                const Vec3 u = target == OriginTarget::YZPlane ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                const Vec3 v = target == OriginTarget::XYPlane ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
                const double h = std::max(camera_.sceneRadius * 0.15, 10.0);
                const Vec3 corners[4] = {(u + v) * -h, (u - v) * h, (u + v) * h, (v - u) * h};
                for (int i = 0; i < 4; ++i)
                    marks.lines.push_back({corners[i], corners[(i + 1) % 4], SketchStyle::Selected});
                break;
            }
            case OriginTarget::Point: marks.points.push_back({{}, SketchStyle::Selected}); break;
            }
            marks.editing = !marks.points.empty(); // points only draw on top
        }
        if (!marks.lines.empty() || !marks.points.empty())
            scene.sketches.push_back(std::move(marks));
    }
    // Everything drawn on the ground (the axes reach furthest) stays inside
    // the clipping range.
    const double groundReach = scene.grid.axisRadius + (scene.grid.center - camera_.sceneCenter).length();
    scene.camera.sceneRadius = std::max(scene.camera.sceneRadius, groundReach);
    return scene;
}

RenderGrid InteractionController::groundGrid() const
{
    RenderGrid grid;
    // Lines spaced for the current zoom, around the target: at the ground
    // below it (in perspective the target can sit on a tall part's top,
    // nearer than the ground), unless that point is not in front of the eye.
    const Vec3 below{camera_.target.x, camera_.target.y, 0};
    const Vec3 scaleAt = camera_.projection == Camera::Projection::Perspective
                                 && camera_.depthOf(below) < 0.25 * camera_.depthOf(camera_.target)
                             ? camera_.target
                             : below;
    const double minor = snapIncrement(camera_.pixelSize(scaleAt), 14.0);
    grid.minorStep = minor;
    grid.majorStep = minor * 10;
    const double major = grid.majorStep;
    grid.center = {std::round(camera_.target.x / major) * major, std::round(camera_.target.y / major) * major, 0};
    // About a view's width around the target at this zoom, and at least twice
    // as far as any corner of the visible bodies' footprint: the grid only
    // starts fading at half its radius, so the lines under a model never do.
    const double visible = std::max(camera_.viewportSize.x, camera_.viewportSize.y) * camera_.pixelSize(scaleAt);
    double radius = std::clamp(visible, 10 * minor, 150 * minor);
    if (visibleBox_.valid) {
        for (const double x : {visibleBox_.min.x, visibleBox_.max.x})
            for (const double y : {visibleBox_.min.y, visibleBox_.max.y})
                radius = std::max(radius, 2 * std::hypot(x - grid.center.x, y - grid.center.y));
    }
    grid.radius = radius;
    grid.axisRadius = radius * 1.5; // the axes stay readable further out
    // In perspective the far side, towards the horizon, fades sooner.
    if (camera_.projection == Camera::Projection::Perspective) {
        const double toCenter = (camera_.eye() - grid.center).length();
        grid.eyeFadeStart = toCenter + 0.3 * radius;
        grid.eyeFadeEnd = toCenter + radius;
    }
    return grid;
}

std::vector<sel::PickTarget> InteractionController::pickTargets() const
{
    std::vector<sel::PickTarget> targets;
    for (const auto& body : document_->bodies())
        if (body->isVisible())
            if (auto mesh = scene_.mesh(body->id()))
                targets.push_back({body->id(), std::move(mesh), scene_.accelerator(body->id())});
    return targets;
}

sel::PickResult InteractionController::pickAt(Vec2 screen, const InputProfile& profile) const
{
    sel::PickOptions options;
    options.edgeTolerance = profile.pickTolerance;
    const sel::PickResult body = sel::pick(pickTargets(), camera_, screen, options);
    // Construction axes and plane outlines are thin, like edges: they win
    // over a face behind them (not over an edge nearer the pointer).
    if (const sel::PickResult line = pickDatumLine(screen, profile, body, std::nullopt); line.hit())
        return line;
    const sel::PickResult region = pickProfile(screen);
    const double slack = camera_.pixelSize(region.point) * 2;
    const bool consumed = region.hit() && !document_->dependentFeatures(region.bodyId).empty();
    // Edges are the smallest targets: keep them reachable, unless a sketch
    // not used yet floats clearly in front of the edge (a circle drawn beside
    // a box, with the box's far edge passing behind it on screen). An edge
    // in the sketch's plane is never behind it: the far edge of the face a
    // sketch lies on recedes with the plane, so a point of the plane a few
    // pixels inside that edge is nearer the eye than the edge, and a body's
    // bottom edge lies in a sketch on the ground in front of it.
    if (body.kind == sel::PickKind::Edge) {
        if (!region.hit() || consumed || region.depth + slack >= body.depth)
            return body;
        const sketch::Sketch* sk = document_->sketch(region.bodyId);
        if (!sk || std::abs((body.point - sk->plane().origin).dot(sk->plane().normal().normalized())) <= slack)
            return body;
        // And the sketch is in front of the surface under the cursor.
        const sel::PickResult face = sel::pickFace(pickTargets(), camera_, screen);
        return !face.hit() || region.depth + slack < face.depth ? region : body;
    }
    // A sketch lying on a face is "on top" of it. A sketch already used by a
    // step only wins where it lies on the surface hit: otherwise, e.g. the
    // circle over a hole it cut, it would hide the body behind it.
    if (region.hit()
        && (!body.hit() || (consumed ? std::abs(region.depth - body.depth) <= slack : region.depth <= body.depth + slack)))
        return region;
    if (body.hit())
        return body;
    // Inside a construction plane: only where nothing else is.
    return pickDatumPlane(screen);
}

// While a tool waits for a face (Mirror's plane, Extrude's "Up to face") or
// for a face or edge (Align's target), sketch profiles are not candidates,
// and where only a face will do, a nearby edge does not steal the click
// (at low zoom edges win within the pick tolerance).
sel::PickResult InteractionController::operationPickAt(Vec2 screen, const InputProfile& profile) const
{
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(operation_.get());
    // Mirror: flat faces and construction planes.
    if (dynamic_cast<const MirrorOperation*>(operation_.get())) {
        const sel::PickResult face = sel::pickFace(pickTargets(), camera_, screen);
        if (const sel::PickResult line = pickDatumLine(screen, profile, face, doc::DatumKind::Plane); line.hit())
            return line;
        if (face.hit())
            return face;
        const sel::PickResult plane = pickDatumPlane(screen);
        const doc::Datum* datum = plane.hit() ? document_->datum(plane.bodyId) : nullptr;
        return datum && datum->kind() == doc::DatumKind::Plane ? plane : sel::PickResult{};
    }
    // The Axis / Plane tool: faces and edges of bodies (faces only where
    // only a flat face will do).
    if (const auto* construct = dynamic_cast<const DatumOperation*>(operation_.get())) {
        if (construct->mode() == DatumOperation::Mode::PlaneOffset || construct->mode() == DatumOperation::Mode::PlaneMidway)
            return sel::pickFace(pickTargets(), camera_, screen);
        sel::PickOptions options;
        options.edgeTolerance = profile.pickTolerance;
        return sel::pick(pickTargets(), camera_, screen, options);
    }
    if ((extrude && extrude->pickingTarget()) || dynamic_cast<const HoleOperation*>(operation_.get())
        || dynamic_cast<const TextOperation*>(operation_.get()))
        return sel::pickFace(pickTargets(), camera_, screen);
    if (dynamic_cast<const AlignOperation*>(operation_.get())) {
        sel::PickOptions options;
        options.edgeTolerance = profile.pickTolerance;
        const sel::PickResult body = sel::pick(pickTargets(), camera_, screen, options);
        // Construction axes and planes, and the X/Y/Z lines drawn through the
        // origin, are targets too.
        if (const sel::PickResult line = pickDatumLine(screen, profile, body, std::nullopt); line.hit())
            return line;
        if (const sel::PickResult axis = pickOriginAxis(screen, profile, body); axis.hit())
            return axis;
        return body.hit() ? body : pickDatumPlane(screen);
    }
    return pickAt(screen, profile);
}

sel::PickResult InteractionController::pickOriginAxis(Vec2 screen, const InputProfile& profile,
                                                      const sel::PickResult& bodyHit) const
{
    // The lines exactly as the renderer draws them (originAxisSegment).
    const RenderGrid g = groundGrid();
    sel::PickResult best;
    double bestPixels = profile.pickTolerance;
    for (int axis = 0; axis < 3; ++axis) {
        const auto segment = originAxisSegment(g, axis);
        if (!segment)
            continue;
        const auto [a, b] = *segment;
        const SegmentHit hit = segmentHit(camera_, screen, a, b);
        if (hit.pixels > bestPixels)
            continue;
        if (!lineBeatsBodyHit(camera_, hit, bodyHit))
            continue;
        bestPixels = hit.pixels;
        best.kind = sel::PickKind::OriginAxis;
        best.bodyId = Uuid();
        best.index = axis;
        best.point = hit.point;
        best.depth = hit.depth;
        best.screenDistance = hit.pixels;
    }
    return best;
}

InteractionController::DatumShape InteractionController::datumShape(const doc::DatumGeometry& g, doc::DatumKind kind) const
{
    DatumShape shape;
    const Vec3 d = g.direction.normalized();
    if (kind == doc::DatumKind::Axis) {
        // Through the model and beyond it: centered where the model's middle
        // is nearest the axis.
        const Vec3 center = modelSize_ > 0 ? g.origin + d * (modelCenter_ - g.origin).dot(d) : g.center;
        const double half = std::max(modelSize_ * 0.75, 20.0);
        shape.a = center - d * half;
        shape.b = center + d * half;
        return shape;
    }
    shape.plane = true;
    const double half = g.size > 0 ? std::max(g.size, 10.0) : std::max(modelSize_ * 0.5, 20.0);
    Vec3 x = g.xAxis - d * g.xAxis.dot(d);
    if (x.length() < 1e-9)
        x = sketch::Plane::fromNormal(g.origin, d).xAxis;
    x = x.normalized();
    const Vec3 y = d.cross(x).normalized();
    shape.corners[0] = g.center - x * half - y * half;
    shape.corners[1] = g.center + x * half - y * half;
    shape.corners[2] = g.center + x * half + y * half;
    shape.corners[3] = g.center - x * half + y * half;
    return shape;
}

sel::PickResult InteractionController::pickDatumLine(Vec2 screen, const InputProfile& profile, const sel::PickResult& bodyHit,
                                                     std::optional<doc::DatumKind> only) const
{
    sel::PickResult best;
    double bestPixels = profile.pickTolerance;
    for (const auto& datum : document_->datums()) {
        if (!datum->isVisible() || (only && datum->kind() != *only))
            continue;
        const DatumShape shape = datumShape(datum->geometry(), datum->kind());
        std::vector<std::pair<Vec3, Vec3>> segments;
        if (shape.plane)
            for (int i = 0; i < 4; ++i)
                segments.emplace_back(shape.corners[i], shape.corners[(i + 1) % 4]);
        else
            segments.emplace_back(shape.a, shape.b);
        for (const auto& [a, b] : segments) {
            const SegmentHit hit = segmentHit(camera_, screen, a, b);
            if (hit.pixels > bestPixels)
                continue;
            if (!lineBeatsBodyHit(camera_, hit, bodyHit))
                continue;
            bestPixels = hit.pixels;
            best.kind = sel::PickKind::Datum;
            best.bodyId = datum->id();
            best.index = -1;
            best.point = hit.point;
            best.depth = hit.depth;
            best.screenDistance = hit.pixels;
        }
    }
    return best;
}

sel::PickResult InteractionController::pickDatumPlane(Vec2 screen) const
{
    sel::PickResult best;
    const Ray ray = camera_.rayAt(screen);
    for (const auto& datum : document_->datums()) {
        if (!datum->isVisible() || datum->kind() != doc::DatumKind::Plane)
            continue;
        const DatumShape shape = datumShape(datum->geometry(), datum->kind());
        const Vec3 n = datum->geometry().direction.normalized();
        const double denom = ray.direction.dot(n);
        if (std::abs(denom) < 1e-9)
            continue;
        const double t = (shape.corners[0] - ray.origin).dot(n) / denom;
        const Vec3 p = ray.at(t);
        // Inside the square: on the inner side of all four edges.
        bool inside = true;
        for (int i = 0; i < 4 && inside; ++i) {
            const Vec3 edge = shape.corners[(i + 1) % 4] - shape.corners[i];
            inside = edge.cross(p - shape.corners[i]).dot(n) >= 0;
        }
        const double depth = camera_.depthOf(p);
        if (!inside || (best.hit() && depth >= best.depth))
            continue;
        best.kind = sel::PickKind::Datum;
        best.bodyId = datum->id();
        best.point = p;
        best.depth = depth;
    }
    return best;
}

void InteractionController::addDatumDrawing(RenderScene& scene) const
{
    RenderSketch drawing;
    auto isSelected = [&](const Uuid& id) {
        return std::any_of(selection_.items().begin(), selection_.items().end(), [&](const sel::SelectionItem& item) {
            return item.kind == sel::SelectionKind::Datum && item.bodyId == id;
        });
    };
    const auto* align = dynamic_cast<const AlignOperation*>(operation_.get());
    const double px = camera_.pixelSize(camera_.target);
    auto draw = [&](const Uuid& key, const doc::DatumGeometry& g, doc::DatumKind kind, SketchStyle style) {
        const DatumShape shape = datumShape(g, kind);
        if (!shape.plane) {
            // A thin dashed line: 8 px dashes, 5 px gaps (at the view's target).
            const Vec3 along = shape.b - shape.a;
            const double length = along.length();
            double period = std::max(13.0 * px, length / 400.0);
            const int count = std::max(1, static_cast<int>(length / period));
            period = length / count;
            const Vec3 dir = along * (1.0 / std::max(length, 1e-12));
            for (int i = 0; i < count; ++i) {
                const Vec3 start = shape.a + dir * (period * i);
                drawing.lines.push_back({start, start + dir * (period * 8.0 / 13.0), style});
            }
            return;
        }
        for (int i = 0; i < 4; ++i)
            drawing.lines.push_back({shape.corners[i], shape.corners[(i + 1) % 4], style});
        // The translucent fill: a two-triangle mesh, rebuilt when the plane moves.
        PlaneFill& fill = planeFills_[key];
        bool same = fill.mesh != nullptr;
        for (int i = 0; i < 4 && same; ++i)
            same = (fill.corners[i] - shape.corners[i]).length() < 1e-12;
        if (!same) {
            static std::uint64_t keys = 1ull << 61; // apart from bodies, profiles and previews
            auto mesh = std::make_shared<geom::Mesh>();
            const Vec3 n = g.direction.normalized();
            for (const Vec3& c : shape.corners) {
                mesh->positions.insert(mesh->positions.end(), {float(c.x), float(c.y), float(c.z)});
                mesh->normals.insert(mesh->normals.end(), {float(n.x), float(n.y), float(n.z)});
            }
            mesh->indices = {0, 1, 2, 0, 2, 3};
            mesh->triangleFace = {0, 0};
            mesh->faceTriangleOffset = {0, 2};
            for (int i = 0; i < 4; ++i)
                fill.corners[i] = shape.corners[i];
            fill.mesh = std::move(mesh);
            fill.key = ++keys;
        }
        const SketchStyle fillStyle = style == SketchStyle::Selected  ? SketchStyle::Selected
                                    : style == SketchStyle::Hovered   ? SketchStyle::Hovered
                                                                      : SketchStyle::Normal;
        drawing.regions.push_back({fill.mesh, fill.key, fillStyle, true});
    };
    for (const auto& datum : document_->datums()) {
        const bool highlighted = historyHighlight_ && *historyHighlight_ == datum->id();
        if (!datum->isVisible() && !highlighted)
            continue;
        SketchStyle style = datum->failed() ? SketchStyle::Conflict : SketchStyle::Reference;
        if (hover_.kind == sel::PickKind::Datum && hover_.bodyId == datum->id())
            style = SketchStyle::Hovered;
        if (highlighted)
            style = SketchStyle::Hovered;
        if (isSelected(datum->id()) || (align && align->datumTarget() == datum->id()))
            style = SketchStyle::Selected;
        draw(datum->id(), datum->geometry(), datum->kind(), style);
    }
    // The axis or plane the Axis / Plane tool would make.
    if (const auto* construct = dynamic_cast<const DatumOperation*>(operation_.get()); construct && construct->preview())
        draw(Uuid(), *construct->preview(), construct->kind(), SketchStyle::Preview);
    // Fills of planes that are gone.
    for (auto it = planeFills_.begin(); it != planeFills_.end();)
        if (!it->first.isNil() && !document_->datum(it->first))
            it = planeFills_.erase(it);
        else
            ++it;
    if (!drawing.lines.empty())
        scene.sketches.push_back(std::move(drawing));
}

// ---- Sketching -------------------------------------------------------------------------

Status InteractionController::startSketch(SketchPlane originPlane)
{
    if (session_)
        finishSketch();
    // Drawing on a sketch continues it: new curves split its regions.
    if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::SketchProfile) {
        const Uuid sketchId = selection_.items().front().bodyId;
        if (document_->sketch(sketchId)) {
            enterSketch(sketchId, SketchTool::Rectangle);
            return okStatus();
        }
    }
    sketch::Plane plane = sketch::Plane::xy();
    // Front (XZ) faces a viewer at -Y; Right (YZ) faces a viewer at +X. Both
    // keep sketch "up" along world Z.
    if (originPlane == SketchPlane::Front)
        plane = sketch::Plane{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    else if (originPlane == SketchPlane::Right)
        plane = sketch::Plane{{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    std::optional<Uuid> host;
    std::optional<sketch::Attachment> attachment;
    std::optional<Uuid> datumPlane;
    if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Datum) {
        const doc::Datum* datum = document_->datum(selection_.items().front().bodyId);
        if (!datum || datum->kind() != doc::DatumKind::Plane) {
            const std::string text = "Sketches go on planes: select a construction plane or a flat face.";
            message(text);
            return Status::failure(ErrorCode::NotPlanar, text, "startSketch on an axis");
        }
        // On the plane, following it.
        plane = doc::sketchPlaneOn(datum->geometry());
        datumPlane = datum->id();
    }
    if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Face) {
        const auto& item = selection_.items().front();
        const doc::Body* body = document_->body(item.bodyId);
        const auto info = body ? geom::faceInfo(body->shape(), item.index) : std::nullopt;
        if (!info || !info->isPlanar()) {
            const std::string text = "Sketches can only be placed on flat faces.";
            message(text);
            return Status::failure(ErrorCode::NotPlanar, text, "startSketch on non-planar face");
        }
        // Origin: the world origin projected onto the face plane, so sketch
        // coordinates line up with the model's coordinates.
        const Vec3 n = info->normal.normalized();
        plane = sketch::Plane::fromNormal(n * n.dot(info->centroid), n);
        host = item.bodyId;
        // The shown shape is the output of the body's last successful step.
        for (int i = static_cast<int>(body->features().size()) - 1; i >= 0 && !attachment; --i) {
            const auto status = body->state(i).status;
            if (status == doc::FeatureStatus::Ok || status == doc::FeatureStatus::Suppressed)
                attachment = doc::makeAttachment(*body, body->features()[std::size_t(i)]->id(), item.index);
        }
    }
    // A visible sketch already lying on this plane is continued rather than
    // starting an independent one, so everything drawn on a plane interacts
    // (as in Shapr3D). The newest such sketch wins.
    const Vec3 n = plane.normal().normalized();
    for (auto it = document_->sketches().rbegin(); it != document_->sketches().rend(); ++it) {
        const sketch::Sketch& existing = **it;
        const Vec3 m = existing.plane().normal().normalized();
        if (existing.isVisible() && std::abs(m.dot(n)) > 0.9999 && std::abs((existing.plane().origin - plane.origin).dot(n)) < 1e-4) {
            message("Continuing " + existing.name() + " on this plane.");
            enterSketch(existing.id(), SketchTool::Rectangle);
            return okStatus();
        }
    }
    operation_.reset();
    selection_.clear();

    sketch::Sketch s(Uuid::generate(), plane);
    s.setName(document_->nextSketchName());
    s.setHostBody(host);
    s.setAttachment(attachment);
    s.setDatumPlane(datumPlane);
    const Uuid id = s.id();
    Status status = undoStack_->push(std::make_unique<cmd::CreateSketchCommand>(std::move(s)), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    enterSketch(id, SketchTool::Rectangle);
    return status;
}

Status InteractionController::editSketch(const Uuid& sketchId)
{
    if (!document_->sketch(sketchId))
        return Status::failure(ErrorCode::InvalidReference, "That sketch no longer exists.", "editSketch: unknown sketch");
    if (session_)
        finishSketch();
    enterSketch(sketchId, SketchTool::Select);
    return okStatus();
}

void InteractionController::enterSketch(const Uuid& sketchId, SketchTool tool)
{
    dropTyping();
    selection_.clear();
    operation_.reset();
    datumTool_.reset();
    hover_ = {};
    drag_ = {};
    if (!cameraBeforeSketch_)
        cameraBeforeSketch_ = animation_ ? animation_->to : camera_;
    session_ = std::make_unique<SketchSession>(*document_, *undoStack_, sketchId);
    session_->setGridSnap(sketchGridSnap_);
    // The sketch's messages are instructions and complaints about the shape
    // being drawn (no names): worded for the input in use.
    session_->onMessage = [this](const std::string& text) { message(forInput(text)); };
    session_->onCommitted = [this] { afterDocumentEdit(); };
    session_->setLargeTargets(touchLayout_);
    session_->setSafeInsets(safeInsets_);
    session_->setTool(tool);
    alignViewTo(session_->sketch().plane());
    afterDocumentEdit();
}

void InteractionController::setSketchGridSnap(bool on)
{
    sketchGridSnap_ = on;
    if (session_)
        session_->setGridSnap(on);
}

void InteractionController::setSafeInsets(const SafeInsets& insets)
{
    if (insets == safeInsets_)
        return;
    safeInsets_ = insets;
    if (session_)
        session_->setSafeInsets(insets);
    notifyView();
}

void InteractionController::setTouchLayout(bool on)
{
    touchLayout_ = on;
    if (session_)
        session_->setLargeTargets(on);
    notifyView();
}

void InteractionController::setHoleAllowance(double mm)
{
    mm = doc::validHoleAllowance(mm);
    if (std::abs(mm - holeSettings_.allowance) < 1e-12)
        return;
    holeSettings_.allowance = mm;
    if (auto* hole = dynamic_cast<HoleOperation*>(operation_.get()))
        hole->setAllowance(mm, *document_);
    else if (auto* head = dynamic_cast<HeadOperation*>(operation_.get()))
        head->setAllowance(mm, *document_);
    else
        return;
    notifyState();
    notifyView();
}

void InteractionController::finishSketch()
{
    if (!session_)
        return;
    const Uuid id = session_->sketchId();
    const bool empty = !session_->sketch().hasGeometry();
    dropTyping();
    session_.reset();
    drag_ = {};
    // An empty sketch is clutter; remove it (undoable like everything else).
    if (empty && document_->sketch(id))
        (void)undoStack_->push(std::make_unique<cmd::DeleteSketchCommand>(id), *document_);
    afterDocumentEdit();
    // Return to the 3D view the user came from, so the next step (e.g. an
    // extrusion arrow) is visible in depth rather than pointing at the viewer.
    if (cameraBeforeSketch_) {
        // Orientation only: keep the current target and zoom on the sketch.
        Camera to = camera_;
        to.yaw = cameraBeforeSketch_->yaw;
        to.pitch = cameraBeforeSketch_->pitch;
        cameraBeforeSketch_.reset();
        if (!empty && std::abs(to.forward().z) > 0.999 && std::abs(camera_.forward().z) > 0.999)
            to.setStandardView(StandardView::Isometric); // came from a top view: tilt for depth
        startAnimation(to);
    }
}

Status InteractionController::commitSketchTool()
{
    if (!session_)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing is being drawn.", "commitSketchTool outside a sketch");
    if (const std::string refused = flushTyping(); !refused.empty()) {
        notifyState();
        notifyView();
        return Status::failure(ErrorCode::InvalidArgument, refused, "commitSketchTool: the typed value is refused");
    }
    Status status = session_->commitTool();
    notifyState();
    notifyView();
    return status;
}

void InteractionController::focusNextSketchInput()
{
    if (!session_)
        return;
    (void)flushTyping(); // the value typed stays with the input it was typed into
    session_->focusNextInput();
    notifyState();
    notifyView();
}

bool InteractionController::focusSketchInput(const std::string& key)
{
    if (!session_)
        return false;
    (void)flushTyping(); // the value typed stays with the input it was typed into
    const bool focused = session_->focusInput(key);
    notifyState();
    notifyView();
    return focused;
}

void InteractionController::setSketchTool(SketchTool tool)
{
    if (!session_)
        return;
    dropTyping(); // the shape being drawn goes
    session_->setTool(tool);
    notifyState();
    notifyView();
}

void InteractionController::alignViewTo(const sketch::Plane& plane)
{
    Camera to = camera_;
    const Vec3 n = plane.normal().normalized();
    to.pitch = std::asin(std::clamp(n.z, -1.0, 1.0));
    // Looking straight down/up: choose the yaw that keeps the sketch x axis
    // pointing right on screen. Otherwise face the plane head-on.
    to.yaw = std::abs(n.z) > 0.999 ? std::atan2(-plane.xAxis.x, plane.xAxis.y) : std::atan2(n.y, n.x);
    to.target = plane.origin;
    if (session_ && session_->sketch().hasGeometry()) {
        Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
        for (const auto& [id, p] : session_->sketch().points()) {
            const Vec3 w = plane.toWorld(p.position);
            lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
            hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
        }
        to.fit(lo, hi);
    }
    startAnimation(to);
}

sel::PickResult InteractionController::pickProfile(Vec2 screen) const
{
    sel::PickResult best;
    const Ray ray = camera_.rayAt(screen);
    for (const auto& sk : document_->sketches()) {
        if (!sk->isVisible() || (session_ && sk->id() == session_->sketchId()))
            continue;
        const auto* entry = scene_.sketch(sk->id());
        if (!entry || entry->regions.empty())
            continue;
        const Vec3 n = sk->plane().normal();
        const double denom = ray.direction.dot(n);
        if (std::abs(denom) < 1e-12)
            continue;
        const double t = (sk->plane().origin - ray.origin).dot(n) / denom;
        if (t < 0)
            continue;
        const Vec3 p = ray.at(t);
        const double depth = camera_.depthOf(p);
        if (best.hit() && depth >= best.depth)
            continue;
        for (std::size_t i = 0; i < entry->regions.size() && i < entry->meshes.size(); ++i) {
            if (entry->meshes[i] && meshContains(*entry->meshes[i], sk->plane(), p)) {
                best.kind = sel::PickKind::Profile;
                best.bodyId = sk->id();
                best.index = static_cast<int>(i);
                best.point = p;
                best.depth = depth;
                break;
            }
        }
    }
    return best;
}


// ---- History ---------------------------------------------------------------------------

namespace {

std::string featureTitle(const doc::Feature& f)
{
    switch (f.kind()) {
    case doc::FeatureKind::Box: return "Box";
    case doc::FeatureKind::PushPull: return "Push/Pull";
    case doc::FeatureKind::Fillet: return "Fillet";
    case doc::FeatureKind::Chamfer: return "Chamfer";
    case doc::FeatureKind::Extrude: return "Extrude";
    case doc::FeatureKind::Shell: return "Shell";
    case doc::FeatureKind::Move: return "Move";
    case doc::FeatureKind::Combine: return "Combine";
    case doc::FeatureKind::Revolve: return "Revolve";
    case doc::FeatureKind::Hole:
        switch (static_cast<const doc::HoleFeature&>(f).holeKind) {
        case doc::HoleKind::Plain: return "Hole";
        case doc::HoleKind::Counterbore: return "Counterbore";
        case doc::HoleKind::Countersink: return "Countersink";
        }
        return "Hole";
    case doc::FeatureKind::Mirror: return static_cast<const doc::MirrorFeature&>(f).keepOriginal ? "Mirror" : "Mirror image";
    case doc::FeatureKind::Pattern: return "Pattern";
    case doc::FeatureKind::DeleteFaces: return "Delete faces";
    case doc::FeatureKind::OffsetFace: return "Offset face";
    case doc::FeatureKind::Split: return "Split";
    case doc::FeatureKind::SplitPiece: return "Piece";
    case doc::FeatureKind::Copy: return static_cast<const doc::CopyFeature&>(f).mirror ? "Mirror copy" : "Copy";
    case doc::FeatureKind::Holes: return static_cast<const doc::HolesFeature&>(f).positions.size() == 1 ? "Hole" : "Holes";
    case doc::FeatureKind::Imported: return "Import";
    case doc::FeatureKind::Text: return "Text";
    }
    return "Step";
}

// "about Z" for rotations about an axis parallel to X, Y or Z, else "turn".
std::string rotationText(double angle, const Vec3& axisVector)
{
    const Vec3 a = axisVector.normalized();
    const double c[3] = {a.x, a.y, a.z};
    int axis = -1;
    for (int k = 0; k < 3; ++k)
        if (std::abs(c[k]) > 0.9999)
            axis = k;
    const double shown = axis >= 0 && c[axis] < 0 ? -angle : angle;
    return formatAngle(shown) + (axis >= 0 ? std::string(" about ") + "XYZ"[axis] : std::string(" turn"));
}

// "Across YZ" for origin planes, else "Across a face".
std::string mirrorPlaneText(const Vec3& origin, const Vec3& normal)
{
    const Vec3 n = normal.normalized();
    static const char* planes[] = {"YZ", "XZ", "XY"};
    const double c[3] = {n.x, n.y, n.z};
    for (int k = 0; k < 3; ++k)
        if (std::abs(c[k]) > 0.9999 && origin.length() < 1e-9)
            return std::string("Across ") + planes[k];
    return "Across a face";
}

std::string featureDetail(const doc::Feature& f, LengthUnit unit, const doc::Document& document)
{
    const std::string dot = " \xC2\xB7 ";
    switch (f.kind()) {
    case doc::FeatureKind::Box: {
        const auto& box = static_cast<const doc::BoxFeature&>(f);
        char text[128];
        std::snprintf(text, sizeof text, "%.2f \xC3\x97 %.2f \xC3\x97 %.2f %s", fromMillimeters(box.size.x, unit),
                      fromMillimeters(box.size.y, unit), fromMillimeters(box.size.z, unit),
                      std::string(unitSymbol(unit)).c_str());
        return text;
    }
    case doc::FeatureKind::PushPull:
        return formatLength(static_cast<const doc::PushPullFeature&>(f).distance, unit);
    case doc::FeatureKind::Fillet:
    case doc::FeatureKind::Chamfer: {
        const auto& e = static_cast<const doc::EdgeTreatmentFeature&>(f);
        const std::string count = std::to_string(e.edges.size()) + (e.edges.size() == 1 ? " edge" : " edges");
        return (f.kind() == doc::FeatureKind::Fillet ? "R " : "") + formatLength(e.size, unit) + dot + count;
    }
    case doc::FeatureKind::Hole: {
        const auto& h = static_cast<const doc::HoleFeature&>(f);
        std::string text = "\xC3\x98" + formatLength(h.diameter, unit)
                         + (h.holeKind == doc::HoleKind::Countersink ? dot + formatAngle(h.angle)
                                                                     : " \xC3\x97 " + formatLength(h.depth, unit));
        if (!h.preset.empty())
            text += dot + h.preset;
        return text;
    }
    case doc::FeatureKind::Holes: {
        const auto& h = static_cast<const doc::HolesFeature&>(f);
        std::string text = h.positions.size() == 1 ? std::string() : std::to_string(h.positions.size()) + " \xC3\x97 ";
        text += "\xC3\x98" + formatLength(h.diameter, unit) + dot
              + (h.throughAll ? std::string("Through all") : formatLength(h.depth, unit) + " deep");
        if (h.head != doc::HoleKind::Plain)
            text += dot + std::string(h.head == doc::HoleKind::Counterbore ? "Counterbore" : "Countersink");
        if (!h.preset.empty())
            text += dot + h.preset;
        return text;
    }
    case doc::FeatureKind::Revolve: {
        const auto& r = static_cast<const doc::RevolveFeature&>(f);
        const char* mode = r.mode == doc::ExtrudeMode::NewBody ? "New body" : r.mode == doc::ExtrudeMode::Join ? "Join" : "Cut";
        return formatAngle(r.angle) + dot + mode;
    }
    case doc::FeatureKind::Combine: {
        const auto& c = static_cast<const doc::CombineFeature&>(f);
        const doc::Body* tool = document.body(c.toolBody);
        return std::string(doc::toString(c.mode)) + dot + (tool ? tool->name() : std::string("missing body"));
    }
    case doc::FeatureKind::Move: {
        const auto& m = static_cast<const doc::MoveFeature&>(f);
        std::string text;
        if (m.rotates) // "90.0° about Z" for axis rotations, "37.5° turn" for Align's free axes
            text = rotationText(m.rotationAngle, m.rotationAxis);
        const Vec3 t = m.translation;
        if (!m.rotates || t.length() > 1e-9) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "%.2f, %.2f, %.2f %s", fromMillimeters(t.x, unit), fromMillimeters(t.y, unit),
                          fromMillimeters(t.z, unit), std::string(unitSymbol(unit)).c_str());
            text += (text.empty() ? std::string() : dot) + buf;
        }
        return text;
    }
    case doc::FeatureKind::DeleteFaces: {
        const auto n = static_cast<const doc::DeleteFacesFeature&>(f).faces.size();
        return std::to_string(n) + (n == 1 ? " face" : " faces");
    }
    case doc::FeatureKind::OffsetFace: {
        const double d = static_cast<const doc::OffsetFaceFeature&>(f).distance;
        return (d >= 0 ? "+" : "") + formatLength(d, unit);
    }
    case doc::FeatureKind::Split: {
        const auto n = static_cast<const doc::SplitFeature&>(f).pieces.size();
        return "Keeps 1 of " + std::to_string(n) + " pieces";
    }
    case doc::FeatureKind::SplitPiece: {
        const auto& p = static_cast<const doc::SplitPieceFeature&>(f);
        const doc::Body* source = document.body(p.sourceBody);
        return "Piece " + std::to_string(p.piece + 1) + " of " + (source ? source->name() : std::string("a deleted body"));
    }
    case doc::FeatureKind::Mirror: {
        const auto& m = static_cast<const doc::MirrorFeature&>(f);
        return mirrorPlaneText(m.planeOrigin, m.planeNormal);
    }
    case doc::FeatureKind::Copy: {
        const auto& c = static_cast<const doc::CopyFeature&>(f);
        const doc::Body* source = document.body(c.sourceBody);
        const std::string of = "Of " + (source ? source->name() : std::string("a deleted body"));
        if (c.mirror)
            return of + dot + mirrorPlaneText(c.planeOrigin, c.planeNormal);
        if (std::abs(c.motion.angle) > 1e-12)
            return of + dot + rotationText(c.motion.angle, c.motion.axis);
        const Vec3 t = c.motion.translation;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%.2f, %.2f, %.2f %s", fromMillimeters(t.x, unit), fromMillimeters(t.y, unit),
                      fromMillimeters(t.z, unit), std::string(unitSymbol(unit)).c_str());
        return of + dot + buf;
    }
    case doc::FeatureKind::Pattern: {
        const auto& pt = static_cast<const doc::PatternFeature&>(f);
        const bool linear = pt.layout == doc::PatternFeature::Layout::Linear;
        const Vec3 d = (linear ? pt.direction : pt.axis).normalized();
        const double c[3] = {d.x, d.y, d.z};
        std::string axis = linear ? " along an edge" : " around an axis";
        for (int k = 0; k < 3; ++k)
            if (std::abs(c[k]) > 0.9999)
                axis = std::string(linear ? " along " : " around ") + "XYZ"[k];
        const std::string count = std::to_string(pt.count) + "\xC3\x97";
        return linear ? count + dot + formatLength(pt.spacing, unit) + axis : count + axis + dot + formatAngle(pt.angle);
    }
    case doc::FeatureKind::Shell: {
        const auto& s = static_cast<const doc::ShellFeature&>(f);
        return "Wall " + formatLength(s.thickness, unit) + dot + std::to_string(s.faces.size())
             + (s.faces.size() == 1 ? " opening" : " openings");
    }
    case doc::FeatureKind::Extrude: {
        const auto& e = static_cast<const doc::ExtrudeFeature&>(f);
        const char* mode = e.mode == doc::ExtrudeMode::NewBody ? "New body" : e.mode == doc::ExtrudeMode::Join ? "Join" : "Cut";
        std::string extent = e.throughAll && e.mode == doc::ExtrudeMode::Cut
                               ? std::string("Through all")
                               : formatLength(e.symmetric ? std::abs(e.distance) : e.distance, unit);
        if (e.symmetric)
            extent += " symmetric";
        if (e.draftAngle != 0)
            extent += dot + "Draft " + formatAngle(e.draftAngle);
        return extent + dot + mode;
    }
    case doc::FeatureKind::Imported: {
        const auto& imported = static_cast<const doc::ImportedFeature&>(f);
        return imported.source.empty() ? std::string("STEP") : imported.source;
    }
    case doc::FeatureKind::Text: {
        // "“Hello” · 10.00 mm · raised 1.00 mm" (the text as typed: a name from the file)
        const auto& t = static_cast<const doc::TextFeature&>(f);
        std::string text = "\xE2\x80\x9C" + t.text + "\xE2\x80\x9D" + dot + formatLength(t.size, unit) + dot
                         + (t.depth >= 0 ? "raised " : "cut ") + formatLength(std::abs(t.depth), unit);
        if (std::abs(t.angle) > 1e-9)
            text += dot + formatAngle(t.angle);
        if (t.font == doc::kTextFontBold)
            text += dot + std::string("Bold");
        return text;
    }
    }
    return {};
}

} // namespace

std::vector<HistoryRow> InteractionController::historyRows() const
{
    std::vector<HistoryRow> rows;
    const LengthUnit unit = document_->displayUnit();
    for (const auto& sk : document_->sketches()) {
        HistoryRow row;
        row.kind = HistoryRow::Kind::Sketch;
        row.id = sk->id();
        row.name = sk->name();
        const auto& report = sk->solveReport();
        if (report.ok && report.degreesOfFreedom == 0)
            row.detail = "Fully defined";
        else if (report.ok && report.degreesOfFreedom > 0)
            row.detail = std::to_string(report.degreesOfFreedom) + " DOF";
        row.visible = sk->isVisible();
        row.canDelete = document_->dependentFeatures(sk->id()).empty();
        if (!row.canDelete)
            row.message = "Used by a 3D step";
        rows.push_back(std::move(row));
    }
    for (const auto& datum : document_->datums()) {
        HistoryRow row;
        row.kind = HistoryRow::Kind::Datum;
        row.id = datum->id();
        row.name = datum->name();
        row.detail = datumDetail(*datum, unit);
        row.visible = datum->isVisible();
        if (datum->failed()) {
            row.status = HistoryRow::Status::Failed;
            row.message = datum->error() + " It stays where it was.";
        }
        for (const auto& p : datum->parameters())
            row.parameters.push_back({p.key, p.label,
                                      p.kind == doc::ParameterKind::Angle ? formatAngle(p.value) : formatLength(p.value, unit), false,
                                      p.kind == doc::ParameterKind::Angle, false});
        rows.push_back(std::move(row));
    }
    for (const auto& body : document_->bodies()) {
        HistoryRow bodyRow;
        bodyRow.kind = HistoryRow::Kind::Body;
        bodyRow.id = body->id();
        bodyRow.name = body->name();
        bodyRow.visible = body->isVisible();
        if (body->hasFailures()) {
            bodyRow.status = HistoryRow::Status::Failed;
            bodyRow.message = "Some steps failed; the last good shape is shown.";
        } else if (body->shape().solidCount() > 1) {
            bodyRow.status = HistoryRow::Status::Warning;
            bodyRow.message = "Made of " + std::to_string(body->shape().solidCount())
                            + " separate pieces; they move and combine together.";
            bodyRow.canSplit = true;
        }
        // Bodies built from this one (split-off pieces, separate copies, a body
        // that consumed it as a tool) would break if it were deleted: Delete
        // hides it instead, and a hidden one stays.
        if (const auto users = document_->bodiesUsing(body->id()); !users.empty()) {
            std::vector<std::string> names;
            for (const Uuid& user : users)
                names.push_back(document_->body(user)->name());
            const std::string built = nameList(names) + (names.size() == 1 ? " is" : " are") + " built from it";
            bodyRow.canDelete = body->isVisible();
            if (bodyRow.message.empty())
                bodyRow.message = body->isVisible() ? built + ": Delete hides it instead." : "Kept hidden: " + built + ".";
        }
        rows.push_back(std::move(bodyRow));

        const auto& features = body->features();
        for (std::size_t i = 0; i < features.size(); ++i) {
            const doc::Feature& f = *features[i];
            const doc::FeatureState& state = body->state(static_cast<int>(i));
            HistoryRow row;
            row.kind = HistoryRow::Kind::Feature;
            row.id = f.id();
            row.parentId = body->id();
            row.name = f.name().empty() ? featureTitle(f) : f.name();
            row.detail = featureDetail(f, unit, *document_);
            row.canDelete = i > 0;
            row.canSuppress = i > 0;
            // A later split into bodies dealt with the pieces a step left.
            bool splitLater = false;
            for (std::size_t k = i + 1; k < features.size(); ++k)
                splitLater = splitLater
                          || (features[k]->kind() == doc::FeatureKind::Split
                              && body->state(static_cast<int>(k)).status == doc::FeatureStatus::Ok);
            switch (state.status) {
            case doc::FeatureStatus::Ok:
                row.status = state.note.empty() || splitLater ? HistoryRow::Status::Ok : HistoryRow::Status::Warning;
                break;
            case doc::FeatureStatus::Failed: row.status = HistoryRow::Status::Failed; break;
            case doc::FeatureStatus::NotComputed: row.status = HistoryRow::Status::NotComputed; break;
            case doc::FeatureStatus::Suppressed: row.status = HistoryRow::Status::Suppressed; break;
            }
            row.message = state.status == doc::FeatureStatus::Suppressed ? "Suppressed"
                        : state.status == doc::FeatureStatus::Ok         ? state.note
                                                                         : state.userMessage;
            // A step that left the body in pieces (and it still is): offer the split there too.
            row.canSplit = state.status == doc::FeatureStatus::Ok && state.output.solidCount() > 1
                        && body->shape().solidCount() > 1 && !body->hasFailures();
            for (const auto& p : f.textParameters())
                row.parameters.push_back({p.key, p.label, p.value, true, false, false});
            for (const auto& p : f.parameters()) {
                if (p.kind == doc::ParameterKind::Length)
                    row.parameters.push_back({p.key, p.label, formatLength(p.value, unit), false, false, false});
                else if (p.kind == doc::ParameterKind::Angle)
                    row.parameters.push_back({p.key, p.label, formatAngle(p.value), false, true, false});
                else if (p.kind == doc::ParameterKind::Count)
                    row.parameters.push_back({p.key, p.label, std::to_string(std::lround(p.value)), false, false, true});
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

Status InteractionController::setFeatureParameter(const Uuid& featureId, const std::string& key, const std::string& text)
{
    // A construction plane's distance or angle (Model panel).
    if (const doc::Datum* datum = document_->datum(featureId)) {
        bool angle = false;
        for (const auto& p : datum->parameters())
            angle = angle || (p.key == key && p.kind == doc::ParameterKind::Angle);
        const auto parsed = angle ? parseAngle(text) : parseLength(text, document_->displayUnit());
        if (!parsed.millimeters)
            return Status::failure(ErrorCode::InvalidArgument, parsed.error, "setFeatureParameter: parse error");
        doc::Datum edited = *datum;
        if (Status status = edited.setParameter(key, *parsed.millimeters); !status)
            return status;
        Status status = undoStack_->push(std::make_unique<cmd::EditDatumCommand>(edited, "Change " + key), *document_);
        if (status)
            afterDocumentEdit();
        return status;
    }
    bool isAngle = false, isCount = false, isText = false;
    if (const doc::Body* body = document_->bodyOfFeature(featureId)) {
        for (const auto& p : body->feature(featureId)->parameters()) {
            isAngle = isAngle || (p.key == key && p.kind == doc::ParameterKind::Angle);
            isCount = isCount || (p.key == key && p.kind == doc::ParameterKind::Count);
        }
        isText = body->feature(featureId)->textParameter(key).has_value();
    }
    if (isText) {
        // A string (a Text step's text): taken as typed.
        Status status = undoStack_->push(
            std::make_unique<cmd::SetTextParameterCommand>(featureId, key, text, /*rejectIfFeatureFails=*/false), *document_);
        if (!status)
            return status;
        operation_.reset();
        afterDocumentEdit();
        if (const doc::Body* body = document_->bodyOfFeature(featureId); body && body->hasFailures())
            message("Some steps can no longer be built. They are marked in the history; undo restores the previous value.");
        return status;
    }
    double value = 0;
    if (isCount) {
        // Whole numbers only: "5", not "5mm" or "2.5".
        char* end = nullptr;
        const long n = std::strtol(text.c_str(), &end, 10);
        while (end && *end == ' ')
            ++end;
        if (text.empty() || !end || *end != '\0')
            return Status::failure(ErrorCode::InvalidArgument, "Enter a whole number.", "setFeatureParameter: bad count");
        value = double(n);
    } else {
        const auto parsed = isAngle ? parseAngle(text) : parseLength(text, document_->displayUnit());
        if (!parsed.millimeters)
            return Status::failure(ErrorCode::InvalidArgument, parsed.error, "setFeatureParameter: parse error");
        value = *parsed.millimeters;
    }
    Status status = undoStack_->push(
        std::make_unique<cmd::SetParameterCommand>(featureId, key, value, /*rejectIfFeatureFails=*/false), *document_);
    if (!status)
        return status;
    operation_.reset();
    afterDocumentEdit();
    if (const doc::Body* body = document_->bodyOfFeature(featureId); body && body->hasFailures())
        message("Some steps can no longer be built. They are marked in the history; undo restores the previous value.");
    return status;
}

Status InteractionController::setFeatureSuppressed(const Uuid& featureId, bool suppressed)
{
    Status status = undoStack_->push(std::make_unique<cmd::SetFeatureSuppressedCommand>(featureId, suppressed), *document_);
    if (status) {
        operation_.reset();
        afterDocumentEdit();
    }
    return status;
}

Status InteractionController::deleteFeature(const Uuid& featureId)
{
    Status status = undoStack_->push(std::make_unique<cmd::DeleteFeatureCommand>(featureId), *document_);
    if (status) {
        operation_.reset();
        afterDocumentEdit();
    }
    return status;
}

Status InteractionController::setBodyVisible(const Uuid& bodyId, bool visible)
{
    Status status = undoStack_->push(std::make_unique<cmd::SetBodyVisibilityCommand>(bodyId, visible), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

Status InteractionController::deleteBody(const Uuid& bodyId)
{
    return deleteBodies({bodyId}); // the selection drops deleted or hidden bodies itself
}

Status InteractionController::deleteSketch(const Uuid& sketchId)
{
    if (session_ && session_->sketchId() == sketchId)
        finishSketch();
    Status status = undoStack_->push(std::make_unique<cmd::DeleteSketchCommand>(sketchId), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

namespace {
constexpr const char* kSelectTwoBodies =
    "Select two bodies: double-click one, then Shift+double-click the other (or Shift-click them in the Model panel).";
} // namespace

Status InteractionController::combineSelectedBodies(doc::CombineMode mode)
{
    if (selection_.size() < 2 || !selection_.allOfKind(sel::SelectionKind::Body))
        return Status::failure(ErrorCode::InvalidArgument, forInput(kSelectTwoBodies), "combine without two bodies");
    // The first selected body is the target; every other one is a tool.
    Uuid target = selection_.items()[0].bodyId;
    // A copy or split-off piece is built from its source, so it cannot be a
    // tool of it. Union and intersect give the same shape either way round:
    // then the copy keeps the result and the source becomes its tool.
    if (selection_.size() == 2 && mode != doc::CombineMode::Subtract) {
        const Uuid other = selection_.items()[1].bodyId;
        if (document_->dependsOn(other, target) && !document_->dependsOn(target, other))
            target = other;
    }
    std::vector<std::unique_ptr<cmd::Command>> steps;
    for (std::size_t i = 0; i < selection_.size(); ++i) {
        const Uuid tool = selection_.items()[i].bodyId;
        if (tool == target)
            continue;
        if (document_->dependsOn(tool, target)) {
            const std::string text = "These bodies already depend on each other.";
            message(text);
            return Status::failure(ErrorCode::InvalidArgument, text, "combine would create a cycle");
        }
        auto feature = std::make_unique<doc::CombineFeature>();
        feature->toolBody = tool;
        feature->mode = mode;
        steps.push_back(std::make_unique<cmd::AddFeatureCommand>(target, std::move(feature)));
        steps.push_back(std::make_unique<cmd::SetBodyVisibilityCommand>(tool, false));
    }
    const char* label = mode == doc::CombineMode::Union ? "Union" : mode == doc::CombineMode::Subtract ? "Subtract" : "Intersect";
    const int piecesBefore = document_->body(target) ? document_->body(target)->shape().solidCount() : 0;
    Status status = undoStack_->push(std::make_unique<cmd::CompositeCommand>(label, std::move(steps)), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    // A subtract (or intersect) that cut the body in pieces. A union of
    // bodies that do not touch was meant to hold them together: no hint.
    if (mode != doc::CombineMode::Union)
        suggestSplit(target, piecesBefore);
    if (auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, target, -1))
        selection_.set(*item);
    afterDocumentEdit();
    return status;
}

Status InteractionController::duplicateBody(const Uuid& bodyId)
{
    if (session_)
        return Status::failure(ErrorCode::InvalidArgument, "Finish the sketch first.", "duplicate in sketch mode");
    if (!document_->body(bodyId))
        return Status::failure(ErrorCode::InvalidReference, "That body no longer exists.", "duplicate: unknown body");
    // Like clicking elsewhere: a pending value is applied first.
    if (Status status = applyPendingValue("duplicateBody"); !status)
        return status;
    auto command = std::make_unique<cmd::DuplicateBodyCommand>(bodyId);
    const Uuid copy = command->copyId();
    Status status = undoStack_->push(std::move(command), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    // The copy sits on the source: select it with the Move arrows to drag it away.
    operation_.reset();
    bodyTool_ = BodyTool::Move;
    alignRequested_ = false;
    if (auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, copy, -1))
        selection_.set(*item);
    afterDocumentEdit();
    return status;
}

Status InteractionController::splitBody(const Uuid& bodyId)
{
    // Every failure is reported here (the Model panel's button ignores the Status).
    if (session_) {
        message("Finish the sketch first.");
        return Status::failure(ErrorCode::InvalidArgument, "Finish the sketch first.", "split in sketch mode");
    }
    if (Status status = applyPendingValue("splitBody"); !status)
        return status;
    int pieces = 0;
    auto command = cmd::makeSplitBodyCommand(*document_, bodyId, &pieces);
    if (!command) {
        message(command.userMessage());
        return Status::failureFrom(command);
    }
    Status status = undoStack_->push(std::move(command.value()), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    // (Counted from the pieces: each new body may bring hidden copies of the
    // tools its history consumed.)
    message("Split into " + std::to_string(pieces) + " bodies.");
    afterDocumentEdit();
    return status;
}

void InteractionController::setHistoryHighlight(const std::optional<Uuid>& id)
{
    if (historyHighlight_ == id)
        return;
    historyHighlight_ = id;
    refreshHistoryHighlight();
    notifyView();
}

void InteractionController::refreshHistoryHighlight()
{
    highlightBody_ = Uuid();
    highlightFaces_.clear();
    if (!historyHighlight_)
        return;
    auto allFaces = [this](const doc::Body& body) {
        highlightBody_ = body.id();
        for (int i = 0; i < body.shape().faceCount(); ++i)
            highlightFaces_.push_back(i);
    };
    if (const doc::Body* body = document_->body(*historyHighlight_)) {
        allFaces(*body);
        return;
    }
    const doc::Body* body = document_->bodyOfFeature(*historyHighlight_);
    if (!body)
        return;
    const int index = body->featureIndex(*historyHighlight_);
    const doc::FeatureState& state = body->state(index);
    if (state.status != doc::FeatureStatus::Ok)
        return;
    if (body->features()[std::size_t(index)]->isBaseFeature()) {
        allFaces(*body); // the step that made the body: all of it
        return;
    }
    highlightBody_ = body->id();
    // New geometry (the fillet, the extruded walls) says best what a step did;
    // a pure move creates none, so fall back to everything it changed.
    const geom::Shape before = body->shapeBefore(index);
    highlightFaces_ = geom::facesCreatedBy(before, state.output, body->shape());
    if (highlightFaces_.empty())
        highlightFaces_ = geom::facesChangedBy(before, state.output, body->shape());
}

Status InteractionController::selectBody(const Uuid& bodyId, BodyPick how)
{
    if (session_)
        return Status::failure(ErrorCode::InvalidArgument, "Finish the sketch first.", "selectBody in sketch mode");
    const doc::Body* body = document_->body(bodyId);
    if (!body)
        return Status::failure(ErrorCode::InvalidReference, "That body no longer exists.", "selectBody: unknown body");
    if (!body->isVisible()) {
        const std::string text = "Show the body first to select it.";
        message(text);
        return Status::failure(ErrorCode::InvalidArgument, text, "selectBody: hidden body");
    }
    // Adding a body that is already selected changes nothing (not even a
    // pending value: the tap was on the panel, not elsewhere in the view).
    if (how == BodyPick::Add && selection_.allOfKind(sel::SelectionKind::Body)
        && std::any_of(selection_.items().begin(), selection_.items().end(),
                       [&](const sel::SelectionItem& selected) { return selected.bodyId == bodyId; }))
        return okStatus();
    // Like clicking elsewhere in the view: a pending value is applied first.
    if (Status status = applyPendingValue("selectBody"); !status)
        return status;
    auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, bodyId, -1);
    if (!item)
        return Status::failure(ErrorCode::InvalidReference, "That body has no shape to select.", "selectBody: no item");
    datumTool_.reset(); // selecting a body ends the Axis / Plane tool (as selecting a datum does)
    if (how != BodyPick::Replace && selection_.allOfKind(sel::SelectionKind::Body)) {
        if (how == BodyPick::Toggle)
            selection_.toggle(*item);
        else
            selection_.add(*item);
    } else {
        selection_.set(*item);
    }
    rebuildOperation();
    notifyState();
    notifyView();
    return okStatus();
}

Status InteractionController::runTool(const std::string& id)
{
    auto explain = [this](const std::string& instruction) {
        const std::string text = forInput(instruction);
        message(text);
        return Status::failure(ErrorCode::InvalidArgument, text, "runTool: selection does not fit");
    };
    if (session_)
        return explain("Finish the sketch first.");
    if (id == "axis" || id == "plane")
        return startDatumTool(id == "axis" ? doc::DatumKind::Axis : doc::DatumKind::Plane);
    // Another tool ends the Axis / Plane tool.
    if (datumTool_) {
        datumTool_.reset();
        rebuildOperation();
    }
    const bool faces = selection_.allOfKind(sel::SelectionKind::Face) && selection_.singleBody();
    const bool edges = selection_.allOfKind(sel::SelectionKind::Edge) && selection_.singleBody();
    const bool bodies = selection_.allOfKind(sel::SelectionKind::Body);
    if (id == "pushpull") {
        if (faces && selection_.size() == 1)
            return triggerAction("pushpull");
        return explain("Click a flat face, then drag its arrow or type a distance.");
    }
    if (id == "fillet" || id == "chamfer") {
        if (edges)
            return triggerAction(id);
        return explain("Click an edge (Shift-click adds more), then drag the arrow or type the size.");
    }
    if (id == "shell") {
        if (faces)
            return triggerAction("shell");
        return explain("Click the face to leave open (Shift-click adds more), then type the wall thickness.");
    }
    if (id == "rotate" || id == "mirror" || id == "pattern") {
        if (!bodies && !selection_.empty() && selection_.singleBody())
            (void)triggerAction("selectBody");
        if (selection_.allOfKind(sel::SelectionKind::Body) && selection_.size() == 1)
            return triggerAction(id);
        return explain(id == "rotate"   ? "Double-click a body (or click it in the Model panel), then drag a ring or type an angle."
                       : id == "mirror" ? "Double-click a body (or click it in the Model panel), then click the flat face to mirror across."
                                        : "Double-click a body (or click it in the Model panel) to repeat it in a row or around an axis.");
    }
    if (id == "move") {
        if (bodies && selection_.size() == 1)
            return triggerAction("move");
        if (const auto body = selection_.singleBody(); body && !selection_.empty() && !bodies)
            return triggerAction("selectBody");
        return explain("Double-click a body (or click it in the Model panel), then drag an arrow or type a distance.");
    }
    if (id == "union" || id == "subtract" || id == "intersect") {
        if (bodies && selection_.size() >= 2)
            return combineSelectedBodies(id == "union" ? doc::CombineMode::Union
                                         : id == "subtract" ? doc::CombineMode::Subtract
                                                            : doc::CombineMode::Intersect);
        return explain(id == "subtract" ? std::string("Select the body to keep first, then the body to cut away with "
                                                      "Shift+double-click (or Shift-click in the Model panel).")
                                        : std::string(kSelectTwoBodies));
    }
    if (id == "offset") {
        if (faces && selection_.size() == 1)
            return triggerAction("offset");
        return explain("Click a face (a hole or shaft takes its new diameter), then drag the arrow or type the value.");
    }
    if (id == "align") {
        if ((faces || edges) && selection_.size() == 1)
            return triggerAction("align");
        return explain("Click the face or edge of the body to move (a flat or round face, a straight edge or a circle), "
                       "then Align, then the face or edge to align it to.");
    }
    if (id == "hole") {
        if (faces && selection_.size() == 1)
            return triggerAction("hole");
        return explain("Click a flat face, then Hole, then click or tap where each hole goes.");
    }
    if (id == "text") {
        if (faces && selection_.size() == 1)
            return triggerAction("text");
        return explain("Click a flat face, then Text: type the words, click where they go, and drag the arrow "
                       "out to raise them or in to cut them.");
    }
    if (id == "measure")
        return explain("Select two faces or edges (Shift-click the second); the distance and angle appear at the bottom left.");
    return Status::failure(ErrorCode::InvalidArgument, "Unknown tool.", "runTool: unknown id '" + id + "'");
}

Status InteractionController::selectDatum(const Uuid& datumId)
{
    if (session_)
        return Status::failure(ErrorCode::InvalidArgument, "Finish the sketch first.", "selectDatum in sketch mode");
    const doc::Datum* datum = document_->datum(datumId);
    if (!datum)
        return Status::failure(ErrorCode::InvalidReference, "That axis or plane no longer exists.", "selectDatum: unknown");
    if (!datum->isVisible()) {
        const std::string text = "Show it first to select it.";
        message(text);
        return Status::failure(ErrorCode::InvalidArgument, text, "selectDatum: hidden");
    }
    // Like clicking it in the view: a pending value is applied first.
    if (operation_ && operation_->canCommit())
        if (Status status = commitOperation(); !status)
            return status;
    datumTool_.reset();
    alignRequested_ = false;
    sel::SelectionItem item;
    item.kind = sel::SelectionKind::Datum;
    item.bodyId = datumId;
    selection_.set(item);
    rebuildOperation();
    notifyState();
    notifyView();
    return okStatus();
}

Status InteractionController::setDatumVisible(const Uuid& datumId, bool visible)
{
    const doc::Datum* datum = document_->datum(datumId);
    if (!datum)
        return Status::failure(ErrorCode::InvalidReference, "That axis or plane no longer exists.", "setDatumVisible");
    doc::Datum next = *datum;
    next.setVisible(visible);
    Status status = undoStack_->push(std::make_unique<cmd::EditDatumCommand>(next, visible ? "Show" : "Hide"), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

Status InteractionController::deleteDatum(const Uuid& datumId)
{
    Status status = undoStack_->push(std::make_unique<cmd::DeleteDatumCommand>(datumId), *document_);
    if (status)
        afterDocumentEdit();
    return status;
}

Status InteractionController::deleteSelectedDatums()
{
    std::vector<std::unique_ptr<cmd::Command>> steps;
    for (const auto& item : selection_.items())
        if (item.kind == sel::SelectionKind::Datum)
            steps.push_back(std::make_unique<cmd::DeleteDatumCommand>(item.bodyId));
    if (steps.empty())
        return Status::failure(ErrorCode::InvalidArgument, "Select an axis or plane to delete.", "deleteSelectedDatums");
    selection_.clear();
    std::unique_ptr<cmd::Command> command =
        steps.size() == 1 ? std::move(steps.front()) : std::make_unique<cmd::CompositeCommand>("Delete", std::move(steps));
    Status status = undoStack_->push(std::move(command), *document_);
    if (!status)
        message(status.userMessage());
    afterDocumentEdit();
    return status;
}

Status InteractionController::startDatumTool(doc::DatumKind kind)
{
    if (session_) {
        const std::string text = "Finish the sketch first.";
        message(text);
        return Status::failure(ErrorCode::InvalidArgument, text, "datum tool in sketch mode");
    }
    // Like clicking elsewhere: a pending value is applied first.
    if (operation_ && operation_->canCommit() && !dynamic_cast<const DatumOperation*>(operation_.get()))
        if (Status status = commitOperation(); !status)
            return status;
    // A fitting selection is the first pick: a hole or an edge for an axis;
    // a flat face (offset), two parallel faces (midway) or an edge (at an
    // angle) for a plane.
    std::vector<sel::SelectionItem> picks;
    for (const auto& item : selection_.items())
        if (item.kind == sel::SelectionKind::Face || item.kind == sel::SelectionKind::Edge)
            picks.push_back(item);
    alignRequested_ = false;
    bodyTool_ = BodyTool::Move;
    selection_.clear();
    datumTool_ = kind;
    operation_ = DatumOperation::create(kind);
    auto* construct = static_cast<DatumOperation*>(operation_.get());
    if (kind == doc::DatumKind::Plane && !picks.empty()) {
        const bool edgeFirst = picks.front().kind == sel::SelectionKind::Edge;
        if (edgeFirst)
            construct->setMode(DatumOperation::Mode::PlaneAngle, *document_);
        else if (picks.size() == 2)
            construct->setMode(DatumOperation::Mode::PlaneMidway, *document_);
    }
    for (const auto& item : picks) {
        const doc::Body* body = document_->body(item.bodyId);
        const bool face = item.kind == sel::SelectionKind::Face;
        const auto point = !body ? std::nullopt
                         : face  ? std::optional<Vec3>(geom::faceInfo(body->shape(), item.index).value_or(geom::FaceInfo{}).centroid)
                                 : std::optional<Vec3>(geom::edgeInfo(body->shape(), item.index).value_or(geom::EdgeInfo{}).midpoint);
        if (point)
            (void)construct->pick(*document_, item.bodyId, face ? geom::SubShapeKind::Face : geom::SubShapeKind::Edge,
                                  item.index, *point);
    }
    notifyState();
    notifyView();
    return okStatus();
}

Status InteractionController::setSketchVisible(const Uuid& sketchId, bool visible)
{
    const sketch::Sketch* current = document_->sketch(sketchId);
    if (!current)
        return Status::failure(ErrorCode::InvalidReference, "That sketch no longer exists.", "setSketchVisible");
    sketch::Sketch next = *current;
    next.setVisible(visible);
    Status status = undoStack_->push(std::make_unique<cmd::EditSketchCommand>(next, visible ? "Show sketch" : "Hide sketch"),
                                     *document_);
    if (status)
        afterDocumentEdit();
    return status;
}


void InteractionController::documentChanged()
{
    afterDocumentEdit();
}

void InteractionController::afterDocumentEdit()
{
    if (session_) {
        session_->syncFromDocument();
        if (!session_->isValid())
            session_.reset(); // undo removed the sketch being edited
    }
    scene_.update(*document_);
    selection_.refresh(*document_);
    hover_ = {};
    refreshHistoryHighlight();
    rebuildOperation();
    updateSceneBounds();
    notifyState();
    notifyView();
}

void InteractionController::updateSceneBounds()
{
    auto add = [](geom::BoundingBox& total, const geom::BoundingBox& box) {
        if (!total.valid) {
            total = box;
        } else {
            total.min = {std::min(total.min.x, box.min.x), std::min(total.min.y, box.min.y), std::min(total.min.z, box.min.z)};
            total.max = {std::max(total.max.x, box.max.x), std::max(total.max.y, box.max.y), std::max(total.max.z, box.max.z)};
        }
    };
    geom::BoundingBox total;
    visibleBox_ = {};
    for (const auto& body : document_->bodies()) {
        const auto box = geom::approximateBoundingBox(body->shape());
        if (!box.valid)
            continue;
        add(total, box);
        if (body->isVisible())
            add(visibleBox_, box);
    }
    modelCenter_ = total.valid ? total.center() : Vec3{};
    modelSize_ = total.valid ? total.size().length() : 0.0;
    if (total.valid) {
        camera_.sceneCenter = total.center();
        // Generous margin: previews may grow the body before the next update.
        camera_.sceneRadius = std::max(total.size().length(), kMinSceneRadius) * 2;
    } else {
        camera_.sceneCenter = {};
        camera_.sceneRadius = kMinSceneRadius * 2;
    }
}

void InteractionController::notePressSelection()
{
    if (pressHandling_ && lastPress_)
        lastPress_->selection = selection_.items();
}

void InteractionController::notifyView()
{
    notePressSelection();
    if (onViewChanged)
        onViewChanged();
}

void InteractionController::notifyState()
{
    notePressSelection();
    // No operation, no value editor: the next one chooses its spot afresh
    // (even on the same selection, e.g. a hole rim again after an undo).
    if (!operation_) {
        chipSpot_ = ChipSpot::None;
        chipSelection_.clear();
        chipSettled_ = false;
        chipLast_.reset();
    }
    if (onStateChanged)
        onStateChanged();
}

std::string InteractionController::forInput(std::string_view instruction) const
{
    return touchLayout_ ? touchWording(instruction) : std::string(instruction);
}

void InteractionController::message(const std::string& text)
{
    OS_LOG(Info, Interaction) << "user message: " << text;
    if (onMessage)
        onMessage(text);
}

} // namespace os::interact
