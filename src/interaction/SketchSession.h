#pragma once

#include "commands/Command.h"
#include "core/Camera.h"
#include "document/Document.h"
#include "geometry/Profiles.h"
#include "interaction/ContextAction.h"
#include "interaction/InputEvents.h"
#include "interaction/RenderScene.h"
#include "sketch/Sketch.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace os::interact {

enum class SketchTool { Select, Line, Rectangle, Circle, Arc };

// A text label drawn by the UI over the viewport while sketching.
struct SketchLabel {
    enum class Kind { Dimension, Input, Hint };
    Kind kind = Kind::Hint;
    std::string key;                          // Input: "width", "height", "diameter", "length"
    sketch::EntityId constraint = sketch::kNoEntity; // Dimension: constraint id
    std::string text;
    Vec2 screen;
    bool focused = false; // Input that receives typed digits
    bool locked = false;  // Input whose value the user typed
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
    bool isDrawing() const { return anchor_.has_value(); }

    // ---- Input (screen coordinates in logical pixels) ----
    // Returns true if the press was consumed by the sketch (tool or geometry);
    // false lets the caller orbit/pan instead.
    bool pointerPress(const PointerEvent& event, const Camera& camera);
    void pointerMove(const PointerEvent& event, const Camera& camera);
    void pointerRelease(const PointerEvent& event, const Camera& camera);
    void hover(const PointerEvent& event, const Camera& camera);
    void leave();
    bool keyPress(Key key);

    // ---- Typed values ----
    bool hasInputs() const { return !inputs_.empty(); }
    // Sets the focused input's text (live). Returns an error message or "".
    std::string typeIntoInput(const std::string& text);
    std::string setInput(const std::string& key, const std::string& text);
    void focusNextInput();
    // Completes the shape being drawn using typed values (Enter).
    Status commitTool();
    // Changes a dimension's value. Returns an error message or "".
    std::string setDimension(sketch::EntityId constraint, const std::string& text);

    // ---- Selection & actions ----
    const std::vector<sketch::EntityId>& selection() const { return selected_; }
    void select(sketch::EntityId id, bool additive);
    std::vector<ContextAction> contextActions() const;
    Status triggerAction(const std::string& id);

    // ---- Presentation ----
    std::vector<SketchLabel> labels(const Camera& camera) const;
    RenderSketch renderData(const Camera& camera) const;
    std::string statusText() const;
    std::string hintText() const;

    std::function<void(const std::string&)> onMessage;
    // Called after a command was pushed (so the owner can refresh views).
    std::function<void()> onCommitted;

private:
    enum class SnapKind { None, Grid, Point, Origin, Midpoint };
    struct Snap {
        Vec2 position;
        SnapKind kind = SnapKind::None;
        sketch::EntityId point = sketch::kNoEntity;
        bool horizontal = false; // inferred relative to the anchor
        bool vertical = false;
    };
    struct Input {
        std::string key;
        std::string label;
        std::string text;
        bool locked = false;
        double value = 0; // mm, when locked
    };

    std::optional<Vec2> toLocal(Vec2 screen, const Camera& camera) const;
    Vec2 toScreen(Vec2 local, const Camera& camera) const;
    Snap snapAt(Vec2 screen, const Camera& camera, PointerDevice device) const;
    sketch::EntityId pickEntity(Vec2 screen, const Camera& camera, PointerDevice device) const;

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
    bool finishShape(const Snap& end);
    bool commit(sketch::Sketch next, const std::string& label);
    std::optional<double> input(const std::string& key) const;
    void message(const std::string& text) const;
    void regionsChanged();

    doc::Document& document_;
    cmd::UndoStack& undoStack_;
    Uuid sketchId_;
    sketch::Sketch working_;
    std::uint64_t syncedRevision_ = 0;

    SketchTool tool_ = SketchTool::Select;
    Snap cursor_;
    bool cursorValid_ = false;
    std::optional<Snap> anchor_;                        // first point of the shape being drawn
    std::optional<Snap> arcEnd_;                        // Arc tool: second click (the end); the third bends it
    sketch::EntityId chainStart_ = sketch::kNoEntity;   // first point of a line chain
    std::vector<Input> inputs_;
    std::size_t focusedInput_ = 0;

    // Press tracking.
    bool pressed_ = false;
    bool dragging_ = false;
    Vec2 pressScreen_;
    PointerEvent pressEvent_;
    sketch::EntityId dragPoint_ = sketch::kNoEntity;
    sketch::Sketch dragStart_;

    std::vector<sketch::EntityId> selected_;
    sketch::EntityId hovered_ = sketch::kNoEntity;

    // Cached closed regions of the working copy.
    std::vector<geom::Region> regions_;
    std::vector<std::shared_ptr<const geom::Mesh>> regionMeshes_;
    std::vector<std::uint64_t> regionKeys_; // GPU cache keys, fresh whenever regions change
};

} // namespace os::interact
