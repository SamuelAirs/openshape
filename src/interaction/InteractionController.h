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
#include "interaction/PreviewWorker.h"
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
        bool operator==(const Parameter&) const = default;
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
    // The body (or the step that left it) is in several pieces: offer
    // "Split into bodies" (for the body `id`, or `parentId` for a step).
    bool canSplit = false;
    std::vector<Parameter> parameters;
    // The UI rebuilds the Model panel only when a row changed (TD-18).
    bool operator==(const HistoryRow&) const = default;
};

// Turns application-level input into navigation, selection, previews and
// commands. Owns no UI toolkit objects; the Qt layer feeds it events and
// reads back state. Everything here is unit-testable headlessly.
class InteractionController : private PreviewScheduler {
public:
    InteractionController(doc::Document& document, cmd::UndoStack& undoStack);
    ~InteractionController() override;
    InteractionController(const InteractionController&) = delete;
    InteractionController& operator=(const InteractionController&) = delete;

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
    // Preferences: whether sketch points snap to the grid (this and later sketches).
    bool sketchGridSnap() const { return sketchGridSnap_; }
    void setSketchGridSnap(bool on);
    // The touch layout (AppController::touchMode reads and sets this, so the
    // two never disagree): on-canvas targets (constraint glyphs) get larger
    // tap areas and sit further apart.
    bool touchLayout() const { return touchLayout_; }
    void setTouchLayout(bool on);

    // ---- Operation / numeric entry ----
    const Operation* operation() const { return operation_.get(); }
    // Parses unit-aware text ("25", "1in", "20+5") and previews it. Returns an
    // error message, or empty on success (or while the preview computes on the
    // worker: its error then comes with a state change, operation()->error()).
    // For a measured value (a push/pull showing the thickness) a leading + or
    // - changes it by that much.
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
    // Deletes the selected bodies, as one undo step. A body that other bodies
    // are built from (pieces split off it, its separate copies, bodies that
    // consumed it as a tool) is hidden instead, with a message: deleting it
    // would break them.
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
    // Like deleteSelectedBodies for one body; fails (without a message) when
    // other bodies are built from it and it is already hidden.
    Status deleteBody(const Uuid& bodyId);
    Status deleteSketch(const Uuid& sketchId);
    Status setSketchVisible(const Uuid& sketchId, bool visible);
    // Combines the first selected body with each of the others (which are
    // hidden): union, subtract them from it, or keep only the common volume.
    Status combineSelectedBodies(doc::CombineMode mode);
    // An independent copy of the body, in place, then selected with the Move
    // arrows so it can be dragged away (Shapr3D's Duplicate).
    Status duplicateBody(const Uuid& bodyId);
    // A body in several separate pieces becomes one body per piece (it keeps
    // the largest); they stay linked to its history.
    Status splitBody(const Uuid& bodyId);
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
    // pickAt, narrowed to what the active tool is waiting for (e.g. faces only).
    sel::PickResult operationPickAt(Vec2 screen, const InputProfile& profile) const;

    // ---- Previews off the GUI thread (TD-1) ----
    // From now on operations compute their previews on a worker thread (the
    // caller's thread becomes the interactive one: geom::setInteractiveThread).
    // `notify` is called on the worker whenever a result is ready; the UI then
    // calls deliverPreviews() on this thread (a queued call). Off by default:
    // headless tests compute previews synchronously.
    void enableAsyncPreviews(std::function<void()> notify);
    // Back to synchronous previews (waits for a running one).
    void disableAsyncPreviews();
    bool asyncPreviews() const { return previewWorker_ != nullptr; }
    // Shows finished previews (or drops stale ones). True if one was shown.
    bool deliverPreviews();
    // A preview is being computed, or waits to be delivered.
    bool previewBusy() const;
    // Blocks until every requested preview is computed and delivered (tests,
    // commit when an automatic choice depends on it). False on timeout.
    bool waitForPreview(std::chrono::milliseconds timeout = std::chrono::seconds(60));
    PreviewWorker* previewWorker() { return previewWorker_.get(); }
    // Previews shown and dropped as stale so far.
    std::uint64_t previewsShown() const { return previewsShown_; }
    std::uint64_t previewsDropped() const { return previewsDropped_; }

    // ---- Notifications ----
    std::function<void()> onViewChanged;                 // needs redraw
    std::function<void()> onStateChanged;                // selection/operation/undo state changed
    std::function<void(const std::string&)> onMessage;   // user-facing message

private:
    enum class DragMode { None, Pending, Orbit, Pan, Manipulator, Sketch };

    // PreviewScheduler (operations call these).
    std::shared_ptr<const doc::Document> previewSnapshot(const doc::Document& document) override;
    void schedulePreview(std::function<PreviewOutcome()> compute) override;
    void dropScheduledPreview() override;
    void receivePreview(const PreviewOutcome& outcome);

    // Kernel queries about the selection, cached until the selection or its
    // bodies change: the UI reads the summary and the actions after every
    // state change (each drag step), when the GUI thread must not wait for
    // the kernel (the worker holds it while it computes a preview).
    struct SelectionMemo {
        std::string key;
        std::optional<std::string> summary;
        std::optional<bool> planarFace;
        std::optional<bool> holeRim;
    };
    SelectionMemo& selectionMemo() const;
    std::string computeSelectionSummary() const;

    void click(const PointerEvent& event);
    // A click elsewhere (or on a Model panel row) applies the operation first.
    // Applied; Refused: the operation stays (its message says why), the click
    // stops; Dropped: nothing to apply, or the command refused a value whose
    // preview had not come back yet - the click goes on to select.
    enum class ApplyResult { Applied, Refused, Dropped };
    ApplyResult applyBeforeSelecting();
    sel::PickResult pickProfile(Vec2 screen) const;
    void enterSketch(const Uuid& sketchId, SketchTool tool);
    void alignViewTo(const sketch::Plane& plane);
    void updateHover(const PointerEvent& event);
    void rebuildOperation();
    void refreshHistoryHighlight();
    // Deletes these bodies (one undo step), hiding those others are built from.
    Status deleteBodies(const std::vector<Uuid>& bodies);
    // After an edit of `bodyId`, which had `piecesBefore` separate pieces: when
    // it left the body in more pieces, says how to make each piece a body.
    void suggestSplit(const Uuid& bodyId, int piecesBefore);
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
    bool sketchGridSnap_ = true;
    bool touchLayout_ = false;
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
        int ring = -1;            // rotation ring pressed on (grabbed once it moves), or -1
        RingManipulator ringHandle;
    } drag_;

    // Index of the operation handle under the pointer (tolerance per device), or -1.
    int handleAt(Vec2 screen, PointerDevice device) const;
    // Index of the rotation ring under the pointer, or -1.
    int ringAt(Vec2 screen, PointerDevice device) const;
    // Starts turning the ring pressed on (drag_.ring) once the pointer moves.
    void grabPendingRing();

    struct Animation {
        Camera from;
        Camera to;
        std::chrono::steady_clock::time_point start;
        double durationSeconds = 0.3;
    };
    std::optional<Animation> animation_;

    mutable SelectionMemo selectionMemo_;
    std::shared_ptr<const doc::Document> snapshot_; // the last preview snapshot, reused while nothing changed
    const doc::Document* snapshotOf_ = nullptr;
    std::uint64_t snapshotRevision_ = 0;
    std::uint64_t snapshotUndoRevision_ = 0;
    std::uint64_t previewsShown_ = 0;
    std::uint64_t previewsDropped_ = 0;
    // Last member: destroyed first, so no job outlives what it reports to.
    std::unique_ptr<PreviewWorker> previewWorker_;
};

} // namespace os::interact
