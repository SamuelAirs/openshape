// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "core/Camera.h"
#include "document/Document.h"
#include "geometry/Exchange.h"
#include "interaction/ContextAction.h"
#include "interaction/InputEvents.h"
#include "interaction/Operation.h"
#include "interaction/OverlayPlacement.h"
#include "interaction/PreviewWorker.h"
#include "interaction/RenderScene.h"
#include "interaction/SceneCache.h"
#include "interaction/SketchSession.h"
#include "interaction/Thumbnail.h"
#include "selection/Picking.h"
#include "selection/Selection.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace os::interact {

// One row of the model tree shown in the history panel.
struct HistoryRow {
    enum class Kind { Sketch, Body, Feature, Datum };
    enum class Status { Ok, Warning, Failed, NotComputed, Suppressed };
    struct Parameter {
        std::string key;
        std::string label;
        std::string valueText; // formatted in the display unit (a string parameter: as it is)
        bool isText = false;   // a string (a Text step's text), not a number
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
    // Looks from any direction (radians; Camera::yaw / Camera::pitch), keeping
    // the target and zoom: screenshots and checks from arbitrary angles.
    void setViewAngles(double yaw, double pitch, bool animate = true);
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
    // Preferences: the FDM print allowance (mm, 0-1) added to screw clearance
    // hole and head seat presets (doc::kDefaultHoleAllowance); an open Hole,
    // counterbore or countersink tool showing a preset follows at once.
    double holeAllowance() const { return holeSettings_.allowance; }
    void setHoleAllowance(double mm);
    // The window's safe-area insets (the Dynamic Island, the home indicator):
    // the sketch's live values moved out from under a finger stay inside them.
    const SafeInsets& safeInsets() const { return safeInsets_; }
    void setSafeInsets(const SafeInsets& insets);
    // How far the controls reach into the view from each edge while a sketch
    // is drawn (a phone: the top bar and the Finish bar above, the hint and
    // the tool strip below): a sketch started on a face or construction
    // plane frames it in the view between them (with the safe insets).
    const SafeInsets& frameInsets() const { return frameInsets_; }
    void setFrameInsets(const SafeInsets& insets) { frameInsets_ = insets; }

    // ---- Operation / numeric entry ----
    const Operation* operation() const { return operation_.get(); }
    // Parses unit-aware text ("25", "1in", "20+5") and previews it. Returns an
    // error message, or empty on success (or while the preview computes on the
    // worker: its error then comes with a state change, operation()->error()).
    // For a measured value (a push/pull showing the thickness) a leading + or
    // - changes it by that much.
    std::string setValueText(const std::string& text);
    // setValueText, then waits for the value's verdict when its preview is
    // still computing: for a key that moves on only with a usable value (Tab
    // to the next field). Keystrokes use setValueText and never wait.
    std::string confirmValueText(const std::string& text);
    std::string operationValueText() const;
    // The Text tool takes words as well as values: the text (UTF-8, as
    // typed), previewed at once. Returns the error, or "".
    bool operationTakesText() const;
    std::string operationText() const;
    std::string setOperationText(const std::string& text);
    // Whether the words were typed or erased in this use of the Text tool
    // (until then a key typed replaces the remembered ones).
    bool operationTextTyped() const;
    // Screen position of the manipulator tip; the value editor sits beside it.
    std::optional<Vec2> valueLabelPosition() const;
    // What the value editor must not cover, on screen: the selection (its
    // faces, edges, profiles or bodies, also where the operation has taken
    // them: a pushed face, a body moved on several axes, a pattern's last
    // copy, and where the shown preview has them while a newer one computes;
    // Operation::carriedSelection), a moved, turned, aligned or new body and
    // a mirror's or pattern's copies as the preview shows them, the arrows
    // and rings, a hole's position (Hole tool) and the last press that could
    // select (a left click, a tap, the pen), while the view and the selection
    // it left are unchanged. Clipped to the viewport; nullopt when there is
    // nothing (or in sketch mode, which has no value editor). Reads display
    // meshes, the preview's box and the operation's arrows only: no kernel
    // call (the UI asks on every drag step, while the preview worker may hold
    // the kernel).
    std::optional<ScreenRect> keepClearRect() const;
    // An arrow or ring is being dragged.
    bool manipulatorDragging() const { return drag_.mode == DragMode::Manipulator; }
    // Where the value editor goes (interact::placeValueChip). The spot is
    // remembered for the current selection, so the chip keeps its place while
    // it stays clear (and a docked one its side during a drag); a new
    // selection chooses afresh.
    ChipPlacement placeValueChip(const ChipPlacementInput& input) const;

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
    // One body per imported shape, each starting with an Imported step, as
    // one undo step. Names stay unique ("Bracket 2"); unnamed shapes become
    // "Imported 1", "Imported 2", ... The view fits everything afterwards.
    // `source` is the file name shown on the steps. Refused, with a plain
    // message and nothing changed, when the project could not keep the
    // geometry: more than doc::kMaxImportedBodyBytes for one body, or more
    // than `maxGeometryBytes` of imported geometry in the document with it
    // (lower only in tests).
    Status importBodies(const std::vector<geom::NamedShape>& shapes, const std::string& source,
                        std::uint64_t maxGeometryBytes = doc::kMaxImportedGeometryBytes);
    // The picture a saved project carries: the visible bodies from the
    // isometric direction, framed (not the current view). Empty without bodies.
    ThumbnailImage renderThumbnail(int size);
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
    // Construction axes and planes: select one (its actions appear: Sketch on
    // a plane, Hide, Delete), show or hide, delete (sketches on a deleted
    // plane stay where they are).
    Status selectDatum(const Uuid& datumId);
    Status setDatumVisible(const Uuid& datumId, bool visible);
    Status deleteDatum(const Uuid& datumId);
    Status deleteSelectedDatums();
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
    // How a Model panel row selects its body: Replace (a click), Toggle
    // (Shift-click: adds it, or takes it out again) or Add (a tap in the touch
    // layout: adds it, e.g. to combine; tapping a selected body's row again,
    // say to fold the row, keeps it selected and its tool running).
    enum class BodyPick { Replace, Toggle, Add };
    Status selectBody(const Uuid& bodyId, BodyPick how);
    // `additive`: Toggle, otherwise Replace.
    Status selectBody(const Uuid& bodyId, bool additive)
    {
        return selectBody(bodyId, additive ? BodyPick::Toggle : BodyPick::Replace);
    }
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

    // How a construction axis or plane is drawn (and picked): an axis as a
    // segment reaching beyond the model, a plane as a square.
    struct DatumShape {
        bool plane = false;
        Vec3 a, b;
        Vec3 corners[4];
    };
    DatumShape datumShape(const doc::DatumGeometry& geometry, doc::DatumKind kind) const;
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
    // User-facing message. Instructions (what to click or select) come already
    // worded for touch in the touch layout (TouchWording); body, sketch and
    // file names in messages are never reworded, so they are passed on as they are.
    std::function<void(const std::string&)> onMessage;

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
    // The same before an action that replaces the operation (Import,
    // Duplicate, Split, a Model panel row): fails only when Refused.
    Status applyPendingValue(const char* action);
    sel::PickResult pickProfile(Vec2 screen) const;
    // The sketch's tapped closed shape (Select tool) straight to Extrude:
    // finishes the sketch, selects that profile, the arrow where it was tapped.
    Status extrudeSketchRegion();
    void enterSketch(const Uuid& sketchId, SketchTool tool);
    // Faces the plane head-on; fits the sketch drawn so far, or - `frame`,
    // a face's or construction plane's box - frames that (and the sketch)
    // in the part of the view the controls leave free (frameInsets).
    void alignViewTo(const sketch::Plane& plane, const std::optional<geom::BoundingBox>& frame = std::nullopt);
    // The selected profile (Extrude) or flat face (Push/Pull) under a press,
    // where a finger's drag moves the arrow: the point pressed on it.
    std::optional<Vec3> selectedRegionAt(Vec2 screen) const;
    void updateHover(const PointerEvent& event);
    // How far (mm, at `point`) the Hole and Text tools' clicks snap: two pick tolerances.
    double holeSnapDistance(const Vec3& point, const InputProfile& profile) const
    {
        return profile.pickTolerance * 2 * camera_.pixelSize(point);
    }
    void rebuildOperation();
    void refreshHistoryHighlight();
    // Deletes these bodies (one undo step), hiding those others are built from.
    Status deleteBodies(const std::vector<Uuid>& bodies);
    // After an edit of `bodyId`, which had `piecesBefore` separate pieces: when
    // it left the body in more pieces, says how to make each piece a body.
    void suggestSplit(const Uuid& bodyId, int piecesBefore);
    void afterDocumentEdit();
    void updateSceneBounds();
    // Wheel and pinch zoom (factor < 1 zooms in), keeping the point under
    // `screen` in place.
    void zoomAt(Vec2 screen, double factor);
    // The ground grid for the current view (renderScene).
    RenderGrid groundGrid() const;
    void startAnimation(const Camera& to);
    std::vector<sel::PickTarget> pickTargets() const;
    // The origin axis line (as drawn) within pick reach of `screen`, unless
    // `bodyHit` is nearer on screen (an edge) or in front of it (a face).
    sel::PickResult pickOriginAxis(Vec2 screen, const InputProfile& profile, const sel::PickResult& bodyHit) const;
    // A construction axis, or a plane's outline, within pick reach (unless
    // `bodyHit` is nearer on screen or in front), optionally of one kind only.
    sel::PickResult pickDatumLine(Vec2 screen, const InputProfile& profile, const sel::PickResult& bodyHit,
                                  std::optional<doc::DatumKind> only) const;
    // Inside a construction plane's square (where nothing else is hit).
    sel::PickResult pickDatumPlane(Vec2 screen) const;
    // Starts the Axis or Plane tool; a fitting selection is its first pick.
    Status startDatumTool(doc::DatumKind kind);
    void addDatumDrawing(RenderScene& scene) const;
    void notifyView();
    void notifyState();
    void notePressSelection();
    void message(const std::string& text);
    // An instruction written for mouse and keyboard ("Click a flat face ..."),
    // worded for touch in the touch layout. Only for the app's own texts: the
    // word rules would also change a name that happens to contain "click".
    std::string forInput(std::string_view instruction) const;

    doc::Document* document_;
    cmd::UndoStack* undoStack_;
    Camera camera_;
    // The visible bodies' approximate box (the grid reaches past its
    // footprint), set with the scene bounds.
    geom::BoundingBox visibleBox_;
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
    std::optional<doc::DatumKind> datumTool_; // the Axis or Plane tool is running
    // What the model spans (for drawing construction geometry beyond it).
    Vec3 modelCenter_;
    double modelSize_ = 0;
    // Construction planes' fill meshes, rebuilt only when a plane moves.
    struct PlaneFill {
        Vec3 corners[4];
        std::shared_ptr<const geom::Mesh> mesh;
        std::uint64_t key = 0;
    };
    mutable std::map<Uuid, PlaneFill> planeFills_;
    // What a single selected body offers: arrows, rings, a mirror plane or a pattern.
    enum class BodyTool { Move, Rotate, Mirror, Pattern };
    BodyTool bodyTool_ = BodyTool::Move;
    int hoveredRing_ = -1;
    bool penMode_ = false;
    bool sketchGridSnap_ = true;
    bool touchLayout_ = false;
    SafeInsets safeInsets_;
    SafeInsets frameInsets_;
    // Where the profile selected last was tapped (in the view, or inside the
    // open sketch before extrudeSketchRegion): its Extrude arrow starts there
    // (under the finger), not at the region's inner point.
    struct ProfileTap {
        Uuid sketch;
        int region = -1;
        Vec3 point;
    };
    std::optional<ProfileTap> profileTap_;
    std::size_t insertPreset_ = 2; // M3
    // What a hole rim's Hole step makes: Plain = the heat-set insert's pilot
    // hole, or a counterbore / countersink (with the screw preset).
    doc::HoleKind rimHoleKind_ = doc::HoleKind::Plain;
    std::size_t screwPreset_ = doc::kDefaultScrew;
    HoleSettings holeSettings_; // what the Hole tool used last
    TextSettings textSettings_; // what the Text tool used last
    doc::SketchAxis revolveAxis_ = doc::SketchAxis::Y;
    std::optional<Uuid> historyHighlight_;
    // Faces (of the current body shape) the highlighted step created or changed.
    Uuid highlightBody_;
    std::vector<int> highlightFaces_;
    // The last press in the view that could select or act (a left click, a
    // tap, the pen; not a resting hand in pen mode, not a right or middle
    // button), the view it was made in and the selection it left: the value
    // editor keeps clear of it until the view moves or the selection changes
    // some other way (the Model panel, Esc, undo).
    struct PressMark {
        Vec2 position;
        Camera camera;
        std::vector<sel::SelectionItem> selection;
    };
    std::optional<PressMark> lastPress_;
    void notePress(Vec2 position);
    // While a press is handled (its release, a double-click), what it selects
    // is its doing: notifyState/notifyView record that selection with it
    // before anyone asks for keepClearRect.
    bool pressHandling_ = false;
    struct PressHandling {
        bool& flag;
        PressHandling(bool& f, bool on) : flag(f) { flag = on; }
        ~PressHandling() { flag = false; }
        PressHandling(const PressHandling&) = delete;
        PressHandling& operator=(const PressHandling&) = delete;
    };
    // The value editor's spot, for this selection and operation
    // (placeValueChip): kept once something has moved since it was chosen.
    mutable ChipSpot chipSpot_ = ChipSpot::None;
    mutable std::vector<sel::SelectionItem> chipSelection_;
    mutable bool chipSettled_ = false;
    mutable std::optional<ChipPlacementInput> chipLast_;

    struct Drag {
        DragMode mode = DragMode::None;
        PointerEvent press;
        Vec2 last;
        Vec3 pivot;
        LinearManipulator handle; // copy of the grabbed handle during a manipulator drag
        int ring = -1;            // rotation ring pressed on (grabbed once it moves), or -1
        RingManipulator ringHandle;
        // A finger pressed on the selected profile or face: once it moves,
        // it drags the arrow along its axis through this point.
        std::optional<Vec3> region;
        // The value and active handle when the manipulator drag began: a
        // cancelled drag (a pinch or two-finger pan whose first finger had
        // already moved the arrow) puts them back.
        std::optional<double> valueBefore;
        int handleBefore = 0;
        bool handleMoved = false;   // a finger's handle or ring drag went past its drag threshold
        bool doubleClicked = false; // a double-click came during this press: its release does not click
    } drag_;
    // Notes the operation's value and active handle before a manipulator drag.
    void noteValueBeforeDrag();

    // What was selected when each of the last two left presses began, and
    // where they were: a double-click / double-tap works on the selection
    // from before its first press (whatever that press's click did - a tap
    // on a face or a body toggles it - is undone), and falls back to the
    // first press's spot when the second lands just off the body.
    struct PressMemo {
        bool bodies = false;       // the selection was bodies only (and not empty)
        std::vector<Uuid> bodyIds; // those bodies, in selection order
        Vec2 position;
        bool valid = false;
        bool toolPick = false; // its click was a pick for the tool (Mirror's plane, Rotate's axis, ...)
    };
    PressMemo pressMemos_[2];     // [1] the latest press, [0] the one before
    bool clickPickedForTool_ = false; // the last click() was taken by the tool (not the usual select/apply)
    bool latestPressReleased_ = true;
    PressMemo selectionMemo(Vec2 position) const; // the selection now
    // The memo for a double-click arriving now: touch sends both taps'
    // presses and releases first; Qt's mouse either replaces the second
    // press with the double-click or delivers it before the double-click.
    // Without a press of its own on record, the selection as it is now.
    PressMemo doubleClickMemo(const PointerEvent& event) const;

    // Index of the operation handle under the pointer (tolerance per device), or -1.
    int handleAt(Vec2 screen, PointerDevice device) const;
    int handleAt(Vec2 screen, double tolerancePx) const;
    // A finger's or pen's tap on a handle's grab zone that means the body
    // under it instead (see pointerRelease).
    bool tapBesideHandle(const PointerEvent& press) const;
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
