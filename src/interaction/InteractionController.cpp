#include "interaction/InteractionController.h"

#include "commands/DocumentCommands.h"
#include "core/Log.h"
#include "core/Units.h"
#include "geometry/Modeling.h"

#include <algorithm>
#include <cstdio>

namespace os::interact {

namespace {

constexpr double kWheelZoomPerStep = 0.85;
constexpr double kMinSceneRadius = 50.0;

bool sameHover(const sel::PickResult& a, const sel::PickResult& b)
{
    return a.kind == b.kind && a.bodyId == b.bodyId && a.index == b.index;
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

void InteractionController::setDocument(doc::Document& document, cmd::UndoStack& undoStack)
{
    document_ = &document;
    undoStack_ = &undoStack;
    selection_.clear();
    operation_.reset();
    hover_ = {};
    drag_ = {};
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

void InteractionController::setProjection(Camera::Projection projection)
{
    if (camera_.projection == projection)
        return;
    // Match the perspective distance to the orthographic zoom so the model
    // keeps roughly the same size on screen.
    if (projection == Camera::Projection::Perspective)
        camera_.distance = camera_.orthoHeight / (2 * std::tan(camera_.fovY / 2));
    else
        camera_.orthoHeight = 2 * camera_.distance * std::tan(camera_.fovY / 2);
    camera_.projection = projection;
    notifyView();
    notifyState();
}

void InteractionController::fitAll(bool animate)
{
    geom::BoundingBox total;
    for (const auto& body : document_->bodies()) {
        if (!body->isVisible() || body->shape().isNull())
            continue;
        const auto box = geom::boundingBox(body->shape());
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
    const auto box = geom::boundingBox(body->shape());
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

// ---- Input -----------------------------------------------------------------------

void InteractionController::pointerPress(const PointerEvent& event)
{
    animation_.reset();
    drag_ = {};
    drag_.press = event;
    drag_.last = event.position;
    drag_.mode = DragMode::Pending;

    if (event.button == PointerButton::Left && operation_) {
        const auto profile = InputProfile::forDevice(event.device);
        const double offset = operation_->displayOffset(operation_->value());
        if (operation_->manipulator().hitTest(camera_, event.position, offset, profile.handleTolerance)) {
            drag_.mode = DragMode::Manipulator;
            operation_->manipulator().beginDrag(camera_, event.position, offset);
            hover_ = {};
            notifyView();
        }
    }
}

void InteractionController::pointerMove(const PointerEvent& event)
{
    if (drag_.mode == DragMode::None) {
        updateHover(event);
        return;
    }
    const auto profile = InputProfile::forDevice(drag_.press.device);
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
        if (hover_.hit() || manipulatorHovered_) {
            hover_ = {};
            manipulatorHovered_ = false;
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
        if (operation_) {
            const double offset = operation_->manipulator().dragTo(camera_, event.position);
            double value = operation_->valueFromOffset(offset);
            if (!event.modifiers.alt)
                value = snapValue(value, snapIncrement(camera_.pixelSize(operation_->anchor())));
            if (value != operation_->value()) {
                operation_->setValue(value, *document_);
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
    drag_.mode = DragMode::None;
    if (mode == DragMode::Pending)
        click(press);
    else if (mode == DragMode::Manipulator)
        notifyState();
    if (event.device == PointerDevice::Mouse)
        updateHover(event);
    notifyView();
}

void InteractionController::pointerDoubleClick(const PointerEvent& event)
{
    const auto hit = pickAt(event.position, InputProfile::forDevice(event.device));
    if (!hit.hit())
        return;
    if (operation_ && operation_->canCommit())
        return; // never discard a pending value on a double-click
    if (auto item = sel::makeSelectionItem(*document_, sel::SelectionKind::Body, hit.bodyId, -1)) {
        selection_.set(*item);
        rebuildOperation();
        notifyState();
        notifyView();
    }
}

void InteractionController::pointerLeave()
{
    if (hover_.hit() || manipulatorHovered_) {
        hover_ = {};
        manipulatorHovered_ = false;
        notifyView();
    }
}

void InteractionController::cancelPointer()
{
    if (drag_.mode == DragMode::Manipulator)
        notifyState();
    drag_ = {};
    notifyView();
}

void InteractionController::wheel(Vec2 position, double steps)
{
    animation_.reset();
    camera_.zoomAt(position, std::pow(kWheelZoomPerStep, steps));
    notifyView();
}

void InteractionController::pinch(Vec2 center, double scale)
{
    if (scale <= 0)
        return;
    animation_.reset();
    camera_.zoomAt(center, 1.0 / scale);
    notifyView();
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
    switch (key) {
    case Key::Escape:
        if (drag_.mode != DragMode::None)
            drag_.mode = DragMode::None;
        if (!operation_ && selection_.empty())
            return false;
        cancelOperation();
        return true;
    case Key::Enter:
        if (operation_ && operation_->canCommit()) {
            (void)commitOperation();
            return true;
        }
        return false;
    case Key::Delete:
    case Key::Backspace:
        if (selection_.allOfKind(sel::SelectionKind::Body)) {
            (void)deleteSelectedBodies();
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
    bool overManipulator = false;
    if (operation_) {
        const double offset = operation_->displayOffset(operation_->value());
        overManipulator = operation_->manipulator().hitTest(camera_, event.position, offset, profile.handleTolerance).has_value();
    }
    const sel::PickResult hit = overManipulator ? sel::PickResult{} : pickAt(event.position, profile);
    if (!sameHover(hit, hover_) || overManipulator != manipulatorHovered_) {
        hover_ = hit;
        manipulatorHovered_ = overManipulator;
        notifyView();
    }
}

void InteractionController::click(const PointerEvent& event)
{
    const auto profile = InputProfile::forDevice(event.device);
    bool additive = profile.additiveSelection || event.modifiers.shift || event.modifiers.control;
    sel::PickResult hit = pickAt(event.position, profile);

    if (operation_ && operation_->canCommit()) {
        // Clicking anywhere else accepts the pending operation (direct-manipulation
        // convention); then the click selects against the updated geometry.
        if (!commitOperation())
            return;
        hit = pickAt(event.position, profile);
        additive = false;
    }

    if (!hit.hit()) {
        selection_.clear();
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
    operation_.reset();
    if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Face) {
        const auto& item = selection_.items().front();
        operation_ = PushPullOperation::create(*document_, item.bodyId, item.index);
    } else if (selection_.allOfKind(sel::SelectionKind::Edge) && selection_.singleBody()) {
        std::vector<int> edges;
        for (const auto& item : selection_.items())
            edges.push_back(item.index);
        operation_ = EdgeOperation::create(*document_, *selection_.singleBody(), edges, edgeOperationKind_);
    }
}

std::string InteractionController::setValueText(const std::string& text)
{
    if (!operation_)
        return "Select a face or an edge first.";
    const auto parsed = parseLength(text, document_->displayUnit());
    if (!parsed.millimeters)
        return parsed.error;
    const double value = *parsed.millimeters;
    if (!operation_->allowsNegative() && value <= 0)
        return operation_->valueLabel() + " must be greater than zero.";
    operation_->setValue(value, *document_);
    notifyState();
    notifyView();
    return operation_->error();
}

std::string InteractionController::operationValueText() const
{
    if (!operation_)
        return {};
    return formatLength(operation_->value(), document_->displayUnit());
}

std::optional<Vec2> InteractionController::valueLabelPosition() const
{
    if (!operation_)
        return std::nullopt;
    const ArrowStyle style;
    const Vec3 anchor = operation_->anchor();
    const double px = camera_.pixelSize(anchor);
    return camera_.project(anchor + operation_->manipulator().direction() * (style.totalPx() * px));
}

Status InteractionController::commitOperation()
{
    if (!operation_)
        return Status::failure(ErrorCode::InvalidArgument, "Nothing to apply.", "commit without operation");
    if (!operation_->canCommit()) {
        const std::string text = operation_->error().empty() ? "Drag the arrow or type a value first." : operation_->error();
        return Status::failure(ErrorCode::InvalidArgument, text, "commit of non-committable operation");
    }
    const bool edgeOperation = operation_->featureKind() != doc::FeatureKind::PushPull;
    Status status = undoStack_->push(operation_->makeCommand(), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    // Edges consumed by a fillet/chamfer no longer exist; a face that was
    // pushed still does and stays selected for the next push.
    if (edgeOperation)
        selection_.clear();
    afterDocumentEdit();
    return status;
}

void InteractionController::cancelOperation()
{
    if (operation_ && operation_->value() != 0.0) {
        operation_->setValue(0.0, *document_);
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
        const auto bb = geom::boundingBox(body->shape());
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

bool InteractionController::undo()
{
    if (!undoStack_->undo(*document_))
        return false;
    operation_.reset();
    afterDocumentEdit();
    return true;
}

bool InteractionController::redo()
{
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
    for (const auto& id : bodies) {
        Status status = undoStack_->push(std::make_unique<cmd::DeleteBodyCommand>(id), *document_);
        if (!status)
            message(status.userMessage());
    }
    afterDocumentEdit();
    return okStatus();
}

std::vector<ContextAction> InteractionController::contextActions() const
{
    std::vector<ContextAction> actions;
    if (selection_.empty())
        return actions;
    if (operation_ && operation_->featureKind() == doc::FeatureKind::PushPull) {
        actions.push_back({"pushpull", "Push/Pull", true});
    } else if (selection_.allOfKind(sel::SelectionKind::Edge) && operation_) {
        actions.push_back({"fillet", "Fillet", edgeOperationKind_ == doc::FeatureKind::Fillet});
        actions.push_back({"chamfer", "Chamfer", edgeOperationKind_ == doc::FeatureKind::Chamfer});
    }
    if (selection_.allOfKind(sel::SelectionKind::Body)) {
        actions.push_back({"fit", "Zoom to", false});
        actions.push_back({"delete", "Delete", false});
    } else if (selection_.singleBody()) {
        actions.push_back({"selectBody", "Select body", false});
    }
    return actions;
}

Status InteractionController::triggerAction(const std::string& id)
{
    if (id == "fillet" || id == "chamfer") {
        const auto kind = id == "fillet" ? doc::FeatureKind::Fillet : doc::FeatureKind::Chamfer;
        const double keep = operation_ ? operation_->value() : 0.0;
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
        return deleteSelectedBodies();
    } else if (id == "fit") {
        fitSelection(true);
    } else if (id == "pushpull") {
        // Already active; the manipulator is the tool.
    } else {
        return Status::failure(ErrorCode::InvalidArgument, "Unknown action.", "unknown action '" + id + "'");
    }
    notifyState();
    notifyView();
    return okStatus();
}

// ---- State -----------------------------------------------------------------------

std::string InteractionController::selectionSummary() const
{
    if (selection_.empty())
        return {};
    const LengthUnit unit = document_->displayUnit();
    const auto& first = selection_.items().front();
    const doc::Body* body = document_->body(first.bodyId);
    if (!body)
        return {};
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
        if (operation_ && operation_->bodyId() == body->id() && operation_->hasPreview()) {
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
        }
        if (rb.mesh)
            scene.bodies.push_back(std::move(rb));
    }

    if (operation_) {
        RenderArrow arrow;
        arrow.anchor = operation_->anchor();
        arrow.direction = operation_->manipulator().direction();
        if (!operation_->error().empty())
            arrow.state = HandleState::Error;
        else if (drag_.mode == DragMode::Manipulator)
            arrow.state = HandleState::Active;
        else if (manipulatorHovered_)
            arrow.state = HandleState::Hovered;
        scene.arrows.push_back(arrow);
    }

    // Grid on the XY plane, spaced for the current zoom.
    const double minor = snapIncrement(camera_.pixelSize(camera_.target), 14.0);
    scene.grid.minorStep = minor;
    scene.grid.majorStep = minor * 10;
    const double major = scene.grid.majorStep;
    scene.grid.center = {std::round(camera_.target.x / major) * major, std::round(camera_.target.y / major) * major, 0};
    const double visible = std::max(camera_.viewportSize.x, camera_.viewportSize.y) * camera_.pixelSize(camera_.target);
    scene.grid.halfLines = std::clamp(static_cast<int>(std::ceil(visible / minor)), 10, 150);
    const double gridReach = scene.grid.halfLines * minor * 1.5 + (scene.grid.center - camera_.sceneCenter).length();
    scene.camera.sceneRadius = std::max(scene.camera.sceneRadius, gridReach);
    return scene;
}

std::vector<sel::PickTarget> InteractionController::pickTargets() const
{
    std::vector<sel::PickTarget> targets;
    for (const auto& body : document_->bodies())
        if (body->isVisible())
            if (auto mesh = scene_.mesh(body->id()))
                targets.push_back({body->id(), std::move(mesh)});
    return targets;
}

sel::PickResult InteractionController::pickAt(Vec2 screen, const InputProfile& profile) const
{
    sel::PickOptions options;
    options.edgeTolerance = profile.pickTolerance;
    return sel::pick(pickTargets(), camera_, screen, options);
}

void InteractionController::documentChanged()
{
    afterDocumentEdit();
}

void InteractionController::afterDocumentEdit()
{
    scene_.update(*document_);
    selection_.refresh(*document_);
    hover_ = {};
    rebuildOperation();
    updateSceneBounds();
    notifyState();
    notifyView();
}

void InteractionController::updateSceneBounds()
{
    geom::BoundingBox total;
    for (const auto& body : document_->bodies()) {
        const auto box = geom::boundingBox(body->shape());
        if (!box.valid)
            continue;
        if (!total.valid) {
            total = box;
        } else {
            total.min = {std::min(total.min.x, box.min.x), std::min(total.min.y, box.min.y), std::min(total.min.z, box.min.z)};
            total.max = {std::max(total.max.x, box.max.x), std::max(total.max.y, box.max.y), std::max(total.max.z, box.max.z)};
        }
    }
    if (total.valid) {
        camera_.sceneCenter = total.center();
        // Generous margin: previews may grow the body before the next update.
        camera_.sceneRadius = std::max(total.size().length(), kMinSceneRadius) * 2;
    } else {
        camera_.sceneCenter = {};
        camera_.sceneRadius = kMinSceneRadius * 2;
    }
}

void InteractionController::notifyView()
{
    if (onViewChanged)
        onViewChanged();
}

void InteractionController::notifyState()
{
    if (onStateChanged)
        onStateChanged();
}

void InteractionController::message(const std::string& text)
{
    OS_LOG(Info, Interaction) << "user message: " << text;
    if (onMessage)
        onMessage(text);
}

} // namespace os::interact
