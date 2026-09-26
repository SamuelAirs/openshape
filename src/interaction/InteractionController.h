// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

// One row of the model tree shown in the history panel.
struct HistoryRow {
    enum class Kind { Sketch, Body, Feature };
    enum class Status { Ok, Warning, Failed, NotComputed, Suppressed };
    struct Parameter {
        std::string key;
        std::string label;
        std::string valueText; // formatted in the display unit
    };

    Kind kind = Kind::Body;
    Uuid id;
    Uuid parentId;          // body of a feature
    std::string name;       // "Body 1", "Extrude", "Sketch 2"
    std::string detail;     // "20.00 mm \xC2\xB7 New body"
    Status status = Status::Ok;
    std::string message;    // user-facing explanation when not Ok
    bool visible = true;    // bodies and sketches
    bool canDelete = true;
    bool canSuppress = false;
    std::vector<Parameter> parameters;
};

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
    // Pen mode (Shapr3D style): once a pen is used, it selects and draws and
    // fingers only move the view. Turned on by the first pen press.
    bool penMode() const { return penMode_; }
    void setPenMode(bool on) { penMode_ = on; }

    // ---- Operation / numeric entry ----
    const Operation* operation() const { return operation_.get(); }
    // Parses unit-aware text ("25", "1in", "20+5") and previews it. Returns an
    // error message, or empty on success. For a measured value (a push/pull
    // showing the thickness) a leading + or - changes it by that much.
    std::string setValueText(const std::string& text);
    std::string operationValueText() const;
    // Screen position of the manipulator tip; the value editor sits beside it.
    std::optional<Vec2> valueLabelPosition() const;

    // Where the world axes point on screen, for the orientation marker:
    // X, Y, Z in that order; `direction` is foreshortened (y down), `depth`
    // is toward the viewer (1 = pointing straight out of the screen).
    struct AxisMark {
        int axis = 0;
        Vec2 direction;
        double depth = 0;
    };
    std::vector<AxisMark> axisTriad() const;
    Status commitOperation();
    void cancelOperation();

    // ---- Actions ----
    Status createBox(double size);
    bool undo();
    bool redo();
    Status deleteSelectedBodies();
    // Removes the selected faces (holes, fillets, chamfers, bosses) and heals the gap.
    Status deleteSelectedFaces();
    std::vector<ContextAction> contextActions() const;
    Status triggerAction(const std::string& id);

    // ---- Sketching ----
    enum class Mode { Model, Sketch };
    Mode mode() const { return session_ ? Mode::Sketch : Mode::Model; }
    SketchSession* sketchSession() { return session_.get(); }
    const SketchSession* sketchSession() const { return session_.get(); }
    // Origin planes a sketch can start on when no face is selected.
    enum class SketchPlane { Top, Front, Right };
    // Starts a sketch on the selected planar face, or on an origin plane.
    Status startSketch(SketchPlane plane = SketchPlane::Top);
    Status editSketch(const Uuid& sketchId);
    // Leaves sketch mode. An empty sketch is deleted.
    void finishSketch();
    void setSketchTool(SketchTool tool);

    // ---- History (model tree) ----
    std::vector<HistoryRow> historyRows() const;
    // Parameter edits from the history keep downstream failures (they are
    // shown, and undo restores the previous value).
    Status setFeatureParameter(const Uuid& featureId, const std::string& key, const std::string& text);
    Status setFeatureSuppressed(const Uuid& featureId, bool suppressed);
    Status deleteFeature(const Uuid& featureId);
    Status setBodyVisible(const Uuid& bodyId, bool visible);
    Status deleteBody(const Uuid& bodyId);
    Status deleteSketch(const Uuid& sketchId);
    Status setSketchVisible(const Uuid& sketchId, bool visible);
    // Combines the first selected body with each of the others (which are
    // hidden): union, subtract them from it, or keep only the common volume.
    Status combineSelectedBodies(doc::CombineMode mode);
    // An independent copy of the body, in place, then selected with the Move
    // arrows so it can be dragged away (Shapr3D's Duplicate).
    Status duplicateBody(const Uuid& bodyId);
    // Model panel hover/expansion: highlights what a row refers to in the view
    // (a body, a sketch, or the faces a step created or modified). nullopt clears.
    void setHistoryHighlight(const std::optional<Uuid>& id);
    const std::optional<Uuid>& historyHighlight() const { return historyHighlight_; }
    // Selects a body from the model panel; `additive` adds it (e.g. to combine).
    Status selectBody(const Uuid& bodyId, bool additive);
    // A tool chosen from the palette: runs it if the selection fits, otherwise
    // explains what to select. Ids: pushpull, fillet, chamfer, shell, move,
    // union, subtract, intersect, measure.
    Status runTool(const std::string& id);

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
    void refreshHistoryHighlight();
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
    int hoveredHandle_ = -1;
    std::unique_ptr<Operation> operation_;
    std::unique_ptr<SketchSession> session_;
    std::optional<Camera> cameraBeforeSketch_; // restored when the sketch is finished
    doc::FeatureKind edgeOperationKind_ = doc::FeatureKind::Fillet;
    doc::FeatureKind faceOperationKind_ = doc::FeatureKind::PushPull;
    doc::FeatureKind profileOperationKind_ = doc::FeatureKind::Extrude;
    bool alignRequested_ = false; // the selected face/edge is the source of an Align
    // What a single selected body offers: arrows, rings, a mirror plane or a pattern.
    enum class BodyTool { Move, Rotate, Mirror, Pattern };
    BodyTool bodyTool_ = BodyTool::Move;
    int hoveredRing_ = -1;
    bool penMode_ = false;
    std::size_t insertPreset_ = 2; // M3
    doc::SketchAxis revolveAxis_ = doc::SketchAxis::Y;
    std::optional<Uuid> historyHighlight_;
    // Faces (of the current body shape) the highlighted step created or changed.
    Uuid highlightBody_;
    std::vector<int> highlightFaces_;

    struct Drag {
        DragMode mode = DragMode::None;
        PointerEvent press;
        Vec2 last;
        Vec3 pivot;
        LinearManipulator handle; // copy of the grabbed handle during a manipulator drag
        int ring = -1;            // grabbed rotation ring, or -1 for an arrow
        RingManipulator ringHandle;
    } drag_;

    // Index of the operation handle under the pointer (tolerance per device), or -1.
    int handleAt(Vec2 screen, PointerDevice device) const;
    // Index of the rotation ring under the pointer, or -1.
    int ringAt(Vec2 screen, PointerDevice device) const;

    struct Animation {
        Camera from;
        Camera to;
        std::chrono::steady_clock::time_point start;
        double durationSeconds = 0.3;
    };
    std::optional<Animation> animation_;
};

} // namespace os::interact
