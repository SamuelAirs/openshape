#pragma once

#include "commands/Command.h"
#include "core/Camera.h"
#include "document/Document.h"
#include "interaction/ContextAction.h"
#include "interaction/InputEvents.h"
#include "interaction/Operation.h"
#include "interaction/RenderScene.h"
#include "interaction/SceneCache.h"
#include "interaction/SketchSession.h"
#include "selection/Picking.h"
#include "selection/Selection.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace os::interact {

// Turns application-level input into navigation, selection, previews and
// commands. Owns no UI toolkit objects; the Qt layer feeds it events and
// reads back state. Everything here is unit-testable headlessly.
class InteractionController {
public:
    InteractionController(doc::Document& document, cmd::UndoStack& undoStack);

    // Replaces the document (new/open). Resets selection and operations.
    void setDocument(doc::Document& document, cmd::UndoStack& undoStack);
    doc::Document& document() { return *document_; }
    cmd::UndoStack& undoStack() { return *undoStack_; }

    // ---- View ----
    const Camera& camera() const { return camera_; }
    void setViewportSize(Vec2 size);
    void setStandardView(StandardView view, bool animate = true);
    void setProjection(Camera::Projection projection);
    void fitAll(bool animate = true);
    void fitSelection(bool animate = true);
    // Advances a running camera animation. Returns true while animating.
    bool advanceAnimation();
    bool isAnimating() const { return animation_.has_value(); }
    // Jumps to the end of a running camera animation (tests, reduced motion).
    void skipAnimation();

    // ---- Input ----
    void pointerPress(const PointerEvent& event);
    void pointerMove(const PointerEvent& event);
    void pointerRelease(const PointerEvent& event);
    void pointerDoubleClick(const PointerEvent& event);
    void pointerLeave();
    // Abandons an in-progress press/drag without treating it as a click
    // (e.g. a second finger touched down). A manipulator drag keeps its value.
    void cancelPointer();
    // steps > 0 zooms in (one wheel notch = 1 step).
    void wheel(Vec2 position, double steps);
    // Two-finger gestures. scale > 1 means fingers moved apart (zoom in).
    void pinch(Vec2 center, double scale);
    void twoFingerPan(Vec2 from, Vec2 to);
    void twoFingerRotate(double dxPixels, double dyPixels);
    // Returns true if the key was handled.
    bool keyPress(Key key);

    // ---- Operation / numeric entry ----
    const Operation* operation() const { return operation_.get(); }
    // Parses unit-aware text ("25", "1in", "20+5") and previews it. Returns an
    // error message, or empty on success.
    std::string setValueText(const std::string& text);
    std::string operationValueText() const;
    // Screen position of the manipulator tip; the value editor sits beside it.
    std::optional<Vec2> valueLabelPosition() const;
    Status commitOperation();
    void cancelOperation();

    // ---- Actions ----
    Status createBox(double size);
    bool undo();
    bool redo();
    Status deleteSelectedBodies();
    std::vector<ContextAction> contextActions() const;
    Status triggerAction(const std::string& id);

    // ---- Sketching ----
    enum class Mode { Model, Sketch };
    Mode mode() const { return session_ ? Mode::Sketch : Mode::Model; }
    SketchSession* sketchSession() { return session_.get(); }
    const SketchSession* sketchSession() const { return session_.get(); }
    // Starts a sketch on the selected planar face, or on the ground (XY) plane.
    Status startSketch();
    Status editSketch(const Uuid& sketchId);
    // Leaves sketch mode. An empty sketch is deleted.
    void finishSketch();
    void setSketchTool(SketchTool tool);

    // ---- State ----
    const sel::SelectionSet& selection() const { return selection_; }
    const sel::PickResult& hover() const { return hover_; }
    std::string selectionSummary() const;
    RenderScene renderScene() const;
    // Call after the document was changed outside the controller.
    void documentChanged();

    // Picking with explicit options (exposed for tests and tools).
    sel::PickResult pickAt(Vec2 screen, const InputProfile& profile) const;

    // ---- Notifications ----
    std::function<void()> onViewChanged;                 // needs redraw
    std::function<void()> onStateChanged;                // selection/operation/undo state changed
    std::function<void(const std::string&)> onMessage;   // user-facing message

private:
    enum class DragMode { None, Pending, Orbit, Pan, Manipulator, Sketch };

    void click(const PointerEvent& event);
    sel::PickResult pickProfile(Vec2 screen) const;
    void enterSketch(const Uuid& sketchId, SketchTool tool);
    void alignViewTo(const sketch::Plane& plane);
    void updateHover(const PointerEvent& event);
    void rebuildOperation();
    void afterDocumentEdit();
    void updateSceneBounds();
    void startAnimation(const Camera& to);
    std::vector<sel::PickTarget> pickTargets() const;
    void notifyView();
    void notifyState();
    void message(const std::string& text);

    doc::Document* document_;
    cmd::UndoStack* undoStack_;
    Camera camera_;
    SceneCache scene_;
    sel::SelectionSet selection_;
    sel::PickResult hover_;
    bool manipulatorHovered_ = false;
    std::unique_ptr<Operation> operation_;
    std::unique_ptr<SketchSession> session_;
    std::optional<Camera> cameraBeforeSketch_; // restored when the sketch is finished
    doc::FeatureKind edgeOperationKind_ = doc::FeatureKind::Fillet;

    struct Drag {
        DragMode mode = DragMode::None;
        PointerEvent press;
        Vec2 last;
        Vec3 pivot;
    } drag_;

    struct Animation {
        Camera from;
        Camera to;
        std::chrono::steady_clock::time_point start;
        double durationSeconds = 0.3;
    };
    std::optional<Animation> animation_;
};

} // namespace os::interact
