// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "core/Camera.h"
#include "document/Document.h"
#include "geometry/Profiles.h"
#include "interaction/ContextAction.h"
#include "interaction/InputEvents.h"
#include "interaction/OverlayPlacement.h"
#include "interaction/RenderScene.h"
#include "sketch/Sketch.h"
#include "sketch/SketchEdit.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace os::interact {

enum class SketchTool { Select, Line, Rectangle, Circle, Arc, Slot, Trim, CenterRectangle, Polygon, TangentArc };

// A text label drawn by the UI over the viewport while sketching.
struct SketchLabel {
    // Constraint: a small glyph near constrained geometry (tap to select it).
    // Size: the length, diameter or radius of the one selected line, circle
    // or arc that no dimension sets yet (tap it to type one: setDimension).
    enum class Kind { Dimension, Input, Hint, Constraint, Size };
    Kind kind = Kind::Hint;
    std::string key;                          // Input: "width", "height", "diameter", "length"
    sketch::EntityId constraint = sketch::kNoEntity; // Dimension, Constraint: constraint id
    sketch::EntityId entity = sketch::kNoEntity;     // Size: the line, circle or arc
    std::string text;
    std::string caption; // what the value is, shown after it in muted text ("across flats", "sides")
    Vec2 screen;
    bool focused = false; // Input that receives typed digits
    bool locked = false;  // Input whose value the user typed
    bool selected = false; // Constraint that is selected
    bool hot = false;      // Constraint whose glyph is under the pointer (Select tool)
};

// A whole number with -/+ buttons for the active tool or mode (a polygon's
// sides, a pattern's copies): touch needs buttons, keyboards use +/-.
struct SketchCounter {
    std::string text; // "6 sides", "3 in total"
    int value = 0;
};

// Editing session for one sketch. Holds a working copy that tools modify;
// every completed action (a shape, a constraint, a moved point, an edited
// dimension) is committed to the document as one EditSketchCommand.
// Framework-independent and unit-testable like InteractionController.
class SketchSession {
public:
    SketchSession(doc::Document& document, cmd::UndoStack& undoStack, const Uuid& sketchId);

    const Uuid& sketchId() const { return sketchId_; }
    const sketch::Sketch& sketch() const { return working_; }
    bool isValid() const;
    // Reloads the working copy if the document's sketch changed (undo/redo).
    void syncFromDocument();

    SketchTool tool() const { return tool_; }
    void setTool(SketchTool tool);
    // Points drawn away from existing geometry round to a zoom-dependent grid
    // (Preferences: "Snap to grid"). Off: they land exactly under the pointer;
    // points, midpoints and horizontal/vertical inference still snap.
    bool gridSnap() const { return gridSnap_; }
    void setGridSnap(bool on) { gridSnap_ = on; }
    bool isDrawing() const { return anchor_.has_value(); }
    // Offset (from the "Offset" action on selected curves): the pointer picks
    // the side and distance, a typed distance fixes it; click or Enter applies.
    bool isOffsetting() const { return !offsetSource_.empty(); }
    // Mirror (from the "Mirror" action on selected curves): the next click on
    // a line mirrors them across it.
    bool isMirroring() const { return !mirrorSource_.empty(); }
    // Pattern (from the "Pattern" action): clicks set where the next copy
    // goes (linear) or the center (circular); typed spacing/angle and count;
    // Enter or Apply adds the copies.
    bool isPatterning() const { return !patternSource_.empty(); }
    const sketch::PatternLayout& patternLayout() const { return pattern_; }

    // ---- Input (screen coordinates in logical pixels) ----
    // Returns true if the press was consumed by the sketch (tool or geometry);
    // false lets the caller orbit/pan instead.
    //
    // With the Select tool a drag edits what the press is on (mouse, pen or
    // finger alike): a point moves (snapping to other points, midpoints and
    // curves, and joined to what it is dropped on), a line moves with both
    // ends, a circle's or arc's rim sets its radius and an arc's center moves
    // the arc, a press inside a closed shape selected by a tap moves the
    // whole shape, and a press on one of several selected items moves them
    // all. Constraints hold (sketch::solveDragging); the release commits one
    // step and keeps the selection. A press inside a closed shape not
    // selected is not taken (a drag there orbits; tapBackground selects it).
    bool pointerPress(const PointerEvent& event, const Camera& camera);
    void pointerMove(const PointerEvent& event, const Camera& camera);
    void pointerRelease(const PointerEvent& event, const Camera& camera);
    // A tap the sketch did not take (pointerPress returned false: empty
    // space, or inside a closed shape not selected yet, where a drag orbits).
    // Select tool: selects the closed shape under it (Extrude; a drag then
    // moves it), otherwise clears the selection.
    void tapBackground(const PointerEvent& event, const Camera& camera);
    // A double-click or double-tap. Select tool: on a curve (or a point of
    // one), selects the curves joined to it end to end (a whole shape).
    // Returns true when it did.
    bool doubleClick(const PointerEvent& event, const Camera& camera);
    // The press became something else (a second finger arrived: a pan or a
    // pinch; the system cancelled the touch): it neither draws, drags nor
    // selects. A shape this press started goes; one already under way (its
    // first corner placed by an earlier tap, a line chain) stays.
    void cancelPress();
    void hover(const PointerEvent& event, const Camera& camera);
    void leave();
    bool keyPress(Key key);

    // Polygon tool: the number of sides (kept for the next polygon).
    int polygonSides() const { return polygonSides_; }
    // Touch layout: constraint glyphs sit further from the geometry and each
    // other, and their tap targets are larger.
    bool largeTargets() const { return largeTargets_; }
    void setLargeTargets(bool on) { largeTargets_ = on; }
    // The window's safe-area insets: live values moved out from under a
    // finger stay inside them (labels()).
    void setSafeInsets(const SafeInsets& insets) { safeInsets_ = insets; }
    // Half the side of a constraint glyph's square tap target, px. Taps are
    // resolved here, not by the UI: with the Select tool a point or curve
    // within pick reach always wins, and only then a glyph under the pointer.
    double glyphTapHalfSize() const { return largeTargets_ ? 20.0 : 12.0; }
    std::optional<SketchCounter> counter() const;
    bool stepCounter(int delta);

    // ---- Typed values ----
    bool hasInputs() const { return !inputs_.empty(); }
    // Sets the focused input's text (live). Returns an error message or "".
    std::string typeIntoInput(const std::string& text);
    std::string setInput(const std::string& key, const std::string& text);
    void focusNextInput();
    // Completes the shape being drawn using typed values (Enter).
    Status commitTool();
    // Changes a dimension's value; for a line, circle or arc (its Size
    // label) adds the dimension that sets its length, diameter or radius to
    // the value. Returns an error message or "".
    std::string setDimension(sketch::EntityId constraintOrCurve, const std::string& text);

    // ---- Selection & actions ----
    const std::vector<sketch::EntityId>& selection() const { return selected_; }
    void select(sketch::EntityId id, bool additive);
    // The closed shape (region index) a tap selected, and where (sketch
    // coordinates, inside it); its action "extrude" is the controller's.
    std::optional<int> selectedRegion() const
    {
        return selectedRegion_ >= 0 ? std::optional<int>(selectedRegion_) : std::nullopt;
    }
    Vec2 selectedRegionPoint() const { return selectedRegionPoint_; }
    const std::vector<geom::Region>& regions() const { return regions_; }
    std::vector<ContextAction> contextActions() const;
    Status triggerAction(const std::string& id);

    // ---- Presentation ----
    // Dimensions, live values, hints and constraint glyphs, in screen
    // coordinates. After a touch or pen input (or in the touch layout) the
    // live values and hints are kept out from under the finger
    // (keepLabelsClearOfFinger).
    std::vector<SketchLabel> labels(const Camera& camera) const;
    // About the size of a live value's label on screen (px), for keeping it clear of a finger.
    static constexpr Vec2 kLiveLabelSize{88, 24};
    // Touch layout: how far a size label sits from its curve (px). Its 44 px
    // tap target then starts 22 px out, beyond a finger's pick reach of the
    // curve (InputProfile), so a drag that starts on the curve moves it.
    static constexpr double kSizeLabelTouchOffset = 44.0;
    RenderSketch renderData(const Camera& camera) const;
    std::string statusText() const;
    std::string hintText() const;

    std::function<void(const std::string&)> onMessage;
    // Called after a command was pushed (so the owner can refresh views).
    std::function<void()> onCommitted;

private:
    // OnLine, OnCircle: only for a dragged point (dragSnapAt).
    enum class SnapKind { None, Grid, Point, Origin, Midpoint, OnLine, OnCircle };
    struct Snap {
        Vec2 position;
        SnapKind kind = SnapKind::None;
        sketch::EntityId point = sketch::kNoEntity;
        sketch::EntityId curve = sketch::kNoEntity; // Midpoint (of a dragged point), OnLine, OnCircle: the curve
        bool horizontal = false; // inferred relative to the anchor
        bool vertical = false;
    };
    // Select tool: what a press took hold of, and how a drag moves it.
    struct Grab {
        enum class Kind {
            None,
            Point,  // one point, to where the pointer snaps (joined to what it is dropped on)
            Rigid,  // `points` all move by the pointer's travel (a line, an arc, a selection)
            Rim,    // a circle's or arc's rim follows the pointer (its radius)
            Region, // a press inside the tapped (selected) closed shape: the curves around it move (points found on the drag)
        };
        Kind kind = Kind::None;
        sketch::EntityId entity = sketch::kNoEntity; // Point: the point; Rim: the circle or arc
        std::vector<sketch::EntityId> points;        // Rigid, Region
        int region = -1;                             // Region
        std::string label;                           // the undo step
        bool refused = false;                        // nothing of it can move (said once)
    };
    Grab grabAt(sketch::EntityId hit) const;
    // The points of the curves around region `index` (a kernel classification per curve).
    std::vector<sketch::EntityId> regionLoopPoints(int index) const;
    // Region under a sketch position (display meshes: no kernel call), or -1.
    int regionAt(Vec2 local) const;
    // Whether anything the grab moves can move at all (the solver's report).
    bool grabCanMove() const;
    // The dimension a size label adds to a line, circle or arc (its length,
    // diameter or radius at the present size), or none where other
    // dimensions already set that size (it would be redundant) or it cannot
    // be added. `solved`: the sketch with it, solved.
    std::optional<sketch::SketchConstraint> sizeDimension(sketch::EntityId curve, sketch::Sketch* solved = nullptr) const;
    // Whether the size label of `curve` can take a typed size (cached per
    // working-copy change: one solve).
    bool sizeCanBeSet(sketch::EntityId curve) const;
    // Where a dragged point lands: other points, midpoints, curves (not its
    // own, nor what moves with it), then the grid.
    Snap dragSnapAt(Vec2 screen, const Camera& camera, PointerDevice device, sketch::EntityId dragged) const;
    void dragTo(const PointerEvent& event, const Camera& camera);
    // How far a drag has moved the first of `points` (a dragged shape's travel).
    Vec2 travel(const std::vector<sketch::EntityId>& points) const;
    // A dropped point joins what it snapped to (merged into a point; on a
    // line, circle or at a midpoint by a constraint). False: nothing added.
    bool connectDropped(sketch::Sketch& next, sketch::EntityId point, const Snap& snap) const;
    bool showsCursor() const;
    struct Input {
        std::string key;
        std::string label;
        std::string text;
        bool locked = false;
        double value = 0; // mm, when locked
    };

    void notePointer(const PointerEvent& event)
    {
        pointerScreen_ = event.position;
        pointerDevice_ = event.device;
    }
    std::optional<Vec2> toLocal(Vec2 screen, const Camera& camera) const;
    Vec2 toScreen(Vec2 local, const Camera& camera) const;
    Snap snapAt(Vec2 screen, const Camera& camera, PointerDevice device) const;
    sketch::EntityId pickEntity(Vec2 screen, const Camera& camera, PointerDevice device, bool curvesOnly = false) const;

    void beginShape(const Snap& at);
    void resetShape();
    // Current end/radius point honoring typed values.
    Vec2 constrainedCursor() const;
    // Arc tool: the arc from the anchor to arcEnd_ through the cursor (or with
    // the typed radius on the cursor's side), counterclockwise start -> end.
    struct ArcShape {
        Vec2 center, start, end;
        double radius = 0;
        bool swapped = false; // start/end exchanged to keep it counterclockwise
    };
    std::optional<ArcShape> arcShape() const;
    // Tangent arc tool: the curve a point ends and the direction of travel
    // leaving it (continuing that curve); the arc from the anchor, tangent to
    // that direction, to the cursor (or with the typed radius).
    struct TangentStart {
        sketch::EntityId curve = sketch::kNoEntity;
        Vec2 direction;
    };
    // `ambiguous`: set when several curves end there (a corner).
    std::optional<TangentStart> tangentStartAt(sketch::EntityId point, bool* ambiguous = nullptr) const;
    std::optional<ArcShape> tangentArcShape() const;
    struct SlotShape {
        Vec2 a, b; // centers
        double radius = 0;
    };
    std::optional<SlotShape> slotShape() const;
    // Trim: the curve under the pointer and where, for the preview.
    sketch::EntityId pickCurve(Vec2 screen, const Camera& camera, PointerDevice device) const;
    // Offset: recompute the preview for the pointer (local) position.
    void updateOffset(std::optional<Vec2> pointer);
    bool commitOffset();
    void cancelOffset();
    void cancelModes(); // mirror and pattern
    bool applyMirror(sketch::EntityId axis);
    void startPattern(bool circular);
    void updatePatternPreview();
    bool commitPattern();
    void patternClick(const Snap& at);
    // The copies a mirror across `axis` or the pattern would add (drawn as a preview).
    void updatePreview(std::function<Result<std::vector<sketch::EntityId>>(sketch::Sketch&)> edit);
    bool finishShape(const Snap& end);
    bool commit(sketch::Sketch next, const std::string& label);
    std::optional<double> input(const std::string& key) const;
    void message(const std::string& text) const;
    void regionsChanged();
    // Glyphs for the non-dimension constraints, placed beside their geometry
    // and nudged apart (and away from `taken`, the dimension labels).
    void addConstraintIcons(std::vector<SketchLabel>& out, const Camera& camera) const;
    // The constraint whose glyph's tap target holds `screen` (the nearest), or none.
    sketch::EntityId glyphAt(Vec2 screen, const Camera& camera) const;
    bool constraintSelected() const;

    doc::Document& document_;
    cmd::UndoStack& undoStack_;
    Uuid sketchId_;
    sketch::Sketch working_;
    std::uint64_t syncedRevision_ = 0;
    std::uint64_t workingChanges_ = 0; // counts working_'s geometry changes (regionsChanged)
    struct SizeCheck {
        sketch::EntityId curve = sketch::kNoEntity;
        std::uint64_t changes = ~std::uint64_t(0);
        bool settable = false;
    };
    mutable SizeCheck sizeCheck_;

    SketchTool tool_ = SketchTool::Select;
    bool gridSnap_ = true;
    Snap cursor_;
    bool cursorValid_ = false;
    std::optional<Snap> anchor_;                        // first point of the shape being drawn
    std::optional<Snap> arcEnd_;                        // Arc tool: second click (the end); the third bends it
    sketch::EntityId chainStart_ = sketch::kNoEntity;   // first point of a line chain
    TangentStart tangentStart_;                         // Tangent arc: the curve continued from the anchor
    std::vector<Input> inputs_;
    std::size_t focusedInput_ = 0;
    int polygonSides_ = 6;
    bool largeTargets_ = false;
    SafeInsets safeInsets_;

    // The last pointer position and device: a finger or pen there hides the
    // labels beside it (labels()).
    std::optional<Vec2> pointerScreen_;
    PointerDevice pointerDevice_ = PointerDevice::Mouse;

    // Press tracking.
    bool pressed_ = false;
    bool pressBeganShape_ = false; // this press placed the shape's first point
    Snap pressSnap_;               // drawing tools: where the press snapped
    bool dragging_ = false;
    Vec2 pressScreen_;
    PointerEvent pressEvent_;
    Vec2 pressLocal_;              // Select: where the press met the sketch plane
    Grab grab_;                    // Select: what the press took hold of
    std::optional<Snap> dragSnap_; // a dragged point's snap (shown; joined on release)
    bool keepSelectionOnRelease_ = false; // a double-click chose the selection
    sketch::Sketch dragStart_;

    std::vector<sketch::EntityId> selected_;
    int selectedRegion_ = -1;      // a tapped closed shape (Extrude)
    Vec2 selectedRegionPoint_;
    int hoveredRegion_ = -1;
    sketch::EntityId hovered_ = sketch::kNoEntity;
    sketch::EntityId hoveredGlyph_ = sketch::kNoEntity; // Select tool: a constraint glyph under the pointer

    std::optional<Vec2> trimCursor_; // Trim tool: pointer position (local) over hovered_

    std::vector<sketch::EntityId> offsetSource_;     // curves being offset
    std::vector<geom::PlanarCurve> offsetPreview_;   // local coordinates (z = 0)
    std::optional<Vec2> offsetPointer_;              // decides the side (and distance when not typed)
    double offsetDistance_ = 0;                      // signed, as last previewed

    std::vector<sketch::EntityId> mirrorSource_;     // curves being mirrored
    sketch::EntityId mirrorAxis_ = sketch::kNoEntity; // the line under the pointer (previewed)
    std::vector<sketch::EntityId> patternSource_;    // curves being repeated
    sketch::PatternLayout pattern_;
    Vec2 patternOrigin_;                             // the selection's middle: a linear step is measured from it
    sketch::Sketch preview_;                         // working copy with the copies added
    std::vector<sketch::EntityId> previewCurves_;    // the copies in preview_

    // Cached closed regions of the working copy.
    std::vector<geom::Region> regions_;
    std::vector<std::shared_ptr<const geom::Mesh>> regionMeshes_;
    std::vector<std::uint64_t> regionKeys_; // GPU cache keys, fresh whenever regions change
};

} // namespace os::interact
