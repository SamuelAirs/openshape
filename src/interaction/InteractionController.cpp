#include "interaction/InteractionController.h"

#include "commands/DocumentCommands.h"
#include "document/SketchProfiles.h"
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
    session_.reset();
    cameraBeforeSketch_.reset();
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
    animation_.reset();
    drag_ = {};
    drag_.press = event;
    drag_.last = event.position;
    drag_.mode = DragMode::Pending;

    if (session_) {
        if (session_->pointerPress(event, camera_)) {
            drag_.mode = DragMode::Sketch;
            notifyState();
            notifyView();
        }
        return;
    }

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
    if (session_) {
        if (mode == DragMode::Sketch)
            session_->pointerRelease(event, camera_);
        else if (mode == DragMode::Pending)
            session_->select(sketch::kNoEntity, false); // click on empty space
        notifyState();
        notifyView();
        return;
    }
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
    if (session_)
        return;
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
    if (session_) {
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
    operation_.reset();
    if (selection_.size() == 1 && selection_.items().front().kind == sel::SelectionKind::Face) {
        const auto& item = selection_.items().front();
        operation_ = PushPullOperation::create(*document_, item.bodyId, item.index);
    } else if (selection_.allOfKind(sel::SelectionKind::Edge) && selection_.singleBody()) {
        std::vector<int> edges;
        for (const auto& item : selection_.items())
            edges.push_back(item.index);
        operation_ = EdgeOperation::create(*document_, *selection_.singleBody(), edges, edgeOperationKind_);
    } else if (selection_.allOfKind(sel::SelectionKind::SketchProfile) && selection_.singleBody()) {
        const Uuid sketchId = *selection_.singleBody();
        const auto* entry = scene_.sketch(sketchId);
        std::vector<doc::ProfileRef> refs;
        for (const auto& item : selection_.items())
            if (item.profile)
                refs.push_back(*item.profile);
        const int first = selection_.items().front().index;
        if (entry && first >= 0 && first < static_cast<int>(entry->regions.size()))
            operation_ = ExtrudeOperation::create(*document_, sketchId, std::move(refs),
                                                  entry->regions[std::size_t(first)].interiorPoint);
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
    // Fillets consume their edges and extrusions their profiles: clear those
    // selections. A pushed face still exists and stays selected.
    const bool clearSelection = operation_->featureKind() != doc::FeatureKind::PushPull;
    Status status = undoStack_->push(operation_->makeCommand(*document_), *document_);
    if (!status) {
        message(status.userMessage());
        return status;
    }
    operation_.reset();
    // Edges consumed by a fillet/chamfer no longer exist; a face that was
    // pushed still does and stays selected for the next push.
    if (clearSelection)
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
    if (session_)
        return session_->contextActions();
    std::vector<ContextAction> actions;
    if (const auto* extrude = dynamic_cast<const ExtrudeOperation*>(operation_.get())) {
        actions.push_back({"extrude", "Extrude", true});
        if (extrude->hasHost()) {
            const auto mode = extrude->mode();
            actions.push_back({"mode:new", "New body", mode == doc::ExtrudeMode::NewBody});
            actions.push_back({"mode:join", "Join", mode == doc::ExtrudeMode::Join});
            actions.push_back({"mode:cut", "Cut", mode == doc::ExtrudeMode::Cut});
        }
        actions.push_back({"editSketch", "Edit sketch", false});
        return actions;
    }
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
    if (session_) {
        Status status = session_->triggerAction(id);
        notifyState();
        notifyView();
        return status;
    }
    if (auto* extrude = dynamic_cast<ExtrudeOperation*>(operation_.get()); extrude && id.rfind("mode:", 0) == 0) {
        const auto mode = id == "mode:join" ? doc::ExtrudeMode::Join : id == "mode:cut" ? doc::ExtrudeMode::Cut : doc::ExtrudeMode::NewBody;
        extrude->setModeOverride(mode);
        extrude->setValue(extrude->value(), *document_);
        notifyState();
        notifyView();
        return okStatus();
    }
    if (id == "editSketch") {
        if (const auto sketchId = selection_.singleBody(); sketchId && selection_.allOfKind(sel::SelectionKind::SketchProfile))
            return editSketch(*sketchId);
        return Status::failure(ErrorCode::InvalidArgument, "Select a sketch profile first.", "editSketch without profile");
    }
    if (id == "extrude")
        return okStatus(); // already active: the arrow is the tool
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
        if (operation_ && !operation_->previewBody().isNil() && operation_->previewBody() == body->id() && operation_->hasPreview()) {
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

    // A new-body preview has no document body to stand in for.
    if (operation_ && operation_->previewBody().isNil() && operation_->hasPreview()) {
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
        if (!sk->isVisible())
            continue;
        RenderSketch rs;
        const sketch::Plane& plane = sk->plane();
        // Sketches already used by a feature recede: thin grey curves, and
        // profile fills only while hovered or selected.
        const bool consumed = !document_->dependentFeatures(sk->id()).empty();
        const SketchStyle curveStyle = consumed ? SketchStyle::Construction : SketchStyle::Normal;
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
        if (const auto* entry = scene_.sketch(sk->id())) {
            for (std::size_t i = 0; i < entry->meshes.size(); ++i) {
                SketchStyle style = SketchStyle::Normal;
                if (hover_.kind == sel::PickKind::Profile && hover_.bodyId == sk->id() && hover_.index == static_cast<int>(i))
                    style = SketchStyle::Hovered;
                for (const auto& item : selection_.items())
                    if (item.kind == sel::SelectionKind::SketchProfile && item.bodyId == sk->id()
                        && item.index == static_cast<int>(i))
                        style = SketchStyle::Selected;
                if (consumed && style == SketchStyle::Normal)
                    continue;
                rs.regions.push_back({entry->meshes[i], entry->meshKeys[i], style});
            }
        }
        scene.sketches.push_back(std::move(rs));
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
    const sel::PickResult body = sel::pick(pickTargets(), camera_, screen, options);
    if (body.kind == sel::PickKind::Edge)
        return body; // edges are the smallest targets; keep them reachable
    const sel::PickResult region = pickProfile(screen);
    // A sketch lying on a face is "on top" of it.
    if (region.hit() && (!body.hit() || region.depth <= body.depth + camera_.pixelSize(region.point) * 2))
        return region;
    return body;
}

// ---- Sketching -------------------------------------------------------------------------

Status InteractionController::startSketch()
{
    if (session_)
        finishSketch();
    sketch::Plane plane = sketch::Plane::xy();
    std::optional<Uuid> host;
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
    }
    operation_.reset();
    selection_.clear();

    sketch::Sketch s(Uuid::generate(), plane);
    s.setName(document_->nextSketchName());
    s.setHostBody(host);
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
    selection_.clear();
    operation_.reset();
    hover_ = {};
    drag_ = {};
    if (!cameraBeforeSketch_)
        cameraBeforeSketch_ = animation_ ? animation_->to : camera_;
    session_ = std::make_unique<SketchSession>(*document_, *undoStack_, sketchId);
    session_->onMessage = [this](const std::string& text) { message(text); };
    session_->onCommitted = [this] { afterDocumentEdit(); };
    session_->setTool(tool);
    alignViewTo(session_->sketch().plane());
    afterDocumentEdit();
}

void InteractionController::finishSketch()
{
    if (!session_)
        return;
    const Uuid id = session_->sketchId();
    const bool empty = !session_->sketch().hasGeometry();
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

void InteractionController::setSketchTool(SketchTool tool)
{
    if (!session_)
        return;
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
        for (std::size_t i = 0; i < entry->regions.size(); ++i) {
            if (geom::regionContains(entry->regions[i].face, p)) {
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
