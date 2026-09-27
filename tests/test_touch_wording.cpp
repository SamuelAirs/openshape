// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Touch wording: in the touch layout no hint, prompt or message may talk of
// clicks, Shift, Esc, Enter, Tab, hovering or the scroll wheel. Every hint
// the sketch tools, the operations and the Modify/Combine tools produce is
// collected here through the interaction layer and checked.
#include "TestFonts.h"

#include "commands/Command.h"
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/TouchWording.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;

    Harness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
        controller.fitAll(false);
    }

    Vec2 sketchScreen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }
    void tap(Vec2 p)
    {
        PointerEvent e;
        e.device = PointerDevice::Touch;
        e.button = PointerButton::Left;
        e.position = p;
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
};

// The touch version of `text` passes, and says something (it was not wiped out).
void expectTouchReady(const std::string& text)
{
    const std::string touch = touchWording(text);
    EXPECT_FALSE(mentionsMouseOrKeyboard(touch)) << "mouse/keyboard words left in: \"" << touch << "\" (from \"" << text << "\")";
    EXPECT_GE(touch.size(), 8u) << text;
}

// A message from the interaction layer in the touch layout: already worded
// for touch (AppController shows messages as they are).
void expectTouchMessage(const std::string& message)
{
    EXPECT_FALSE(mentionsMouseOrKeyboard(message)) << "mouse/keyboard words in a touch message: \"" << message << "\"";
}

std::string readSource(const std::string& relative)
{
    std::ifstream in(std::string(OPENSHAPE_SOURCE_DIR) + "/" + relative, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

void appendUtf8(std::string& out, unsigned code)
{
    if (code < 0x80) {
        out += char(code);
    } else if (code < 0x800) {
        out += char(0xC0 | (code >> 6));
        out += char(0x80 | (code & 0x3F));
    } else {
        out += char(0xE0 | (code >> 12));
        out += char(0x80 | ((code >> 6) & 0x3F));
        out += char(0x80 | (code & 0x3F));
    }
}

// The string literal starting at code[pos] (a double quote), decoded; pos
// ends after its closing quote.
std::string readLiteral(const std::string& code, std::size_t& pos)
{
    std::string out;
    ++pos; // opening quote
    while (pos < code.size() && code[pos] != '"') {
        if (code[pos] == '\\' && pos + 1 < code.size()) {
            const char e = code[pos + 1];
            if (e == 'u' && pos + 5 < code.size()) {
                appendUtf8(out, unsigned(std::stoul(code.substr(pos + 2, 4), nullptr, 16)));
                pos += 6;
                continue;
            }
            out += e;
            pos += 2;
            continue;
        }
        out += code[pos++];
    }
    ++pos; // closing quote
    return out;
}

// Every string literal in `code`, each return statement's pieces joined.
std::vector<std::string> returnedTexts(const std::string& code)
{
    std::vector<std::string> texts;
    std::size_t at = code.find("return ");
    while (at != std::string::npos) {
        const std::size_t next = code.find("return ", at + 7);
        const std::size_t end = std::min(next, code.find("\n        if (", at));
        std::string joined;
        for (std::size_t pos = at; pos < std::min(end, code.size());) {
            if (code[pos] == '"')
                joined += readLiteral(code, pos);
            else
                ++pos;
        }
        if (!joined.empty())
            texts.push_back(joined);
        at = next;
    }
    return texts;
}

} // namespace

TEST(TouchWording, QmlHintsAreTouchReady)
{
    // Main.qml words its own hints through touchWording in the touch layout.
    const std::string qml = readSource("src/ui/qml/Main.qml");
    const std::size_t start = qml.find("function mouseHintText()");
    ASSERT_NE(start, std::string::npos);
    const std::size_t end = qml.find("\n    }\n", start);
    ASSERT_NE(end, std::string::npos);
    const auto hints = returnedTexts(qml.substr(start, end - start));
    EXPECT_GE(hints.size(), 15u); // 18 when written
    for (const std::string& hint : hints)
        expectTouchReady(hint);
}

TEST(TouchWording, HelpCardHasTouchTextForEveryRow)
{
    // Rows are ["what", "mouse and keyboard", "touch"?]: in the touch layout the
    // card shows the touch text, or the only text when both are the same.
    const std::string qml = readSource("src/ui/qml/HelpOverlay.qml");
    int rows = 0, touchRows = 0;
    for (std::size_t pos = 0; (pos = qml.find('[', pos)) != std::string::npos;) {
        std::size_t p = pos + 1;
        while (p < qml.size() && std::isspace(static_cast<unsigned char>(qml[p])))
            ++p;
        if (p >= qml.size() || qml[p] != '"') {
            ++pos;
            continue;
        }
        std::vector<std::string> row;
        while (p < qml.size() && qml[p] == '"') {
            row.push_back(readLiteral(qml, p));
            while (p < qml.size() && (std::isspace(static_cast<unsigned char>(qml[p])) || qml[p] == ','))
                ++p;
        }
        pos = p;
        if (row.size() < 2 || p >= qml.size() || qml[p] != ']')
            continue;
        ++rows;
        touchRows += row.size() > 2 ? 1 : 0;
        const std::string& touch = row.size() > 2 ? row[2] : row[1];
        EXPECT_FALSE(mentionsMouseOrKeyboard(touch)) << "help row \"" << row[0] << "\" on touch: \"" << touch << "\"";
    }
    EXPECT_GE(rows, 50);
    EXPECT_GE(touchRows, 30);
}

TEST(TouchWording, KnownSentencesGetTouchVersions)
{
    // Shortcuts and keys named in passing are left out on touch.
    EXPECT_EQ(touchWording("Add a box, start a sketch, or import a STEP file (Ctrl+I)."),
              "Add a box, start a sketch, or import a STEP file.");
    EXPECT_EQ(touchWording("X / Y (Tab) type the current hole's position"), "X / Y type the current hole's position");
    EXPECT_EQ(touchWording("Click an edge (Shift-click adds more), then drag the arrow or type the size."),
              "Tap an edge (tap more to add them), then drag the arrow or type the size.");
    EXPECT_EQ(touchWording("Select two bodies: double-click one, then Shift+double-click the other (or Shift-click them in the "
                           "Model panel)."),
              "Select two bodies: double-tap one, then double-tap the other (or tap both in the Model panel).");
    EXPECT_EQ(touchWording("Click a face to push/pull, an edge to round it \xC2\xB7 double-click selects the body \xC2\xB7 "
                           "drag to orbit \xC2\xB7 Shift/middle-drag to pan \xC2\xB7 scroll to zoom"),
              "Tap a face to push/pull, an edge to round it \xC2\xB7 double-tap selects the body \xC2\xB7 "
              "drag to orbit \xC2\xB7 two fingers pan, pinch zooms");
    EXPECT_EQ(touchWording("Click the next point \xC2\xB7 type a length \xC2\xB7 Esc ends the line"),
              "Tap the next point \xC2\xB7 tap Line again to end the line");
    EXPECT_EQ(touchWording("Enter to apply \xC2\xB7 Esc to cancel \xC2\xB7 click elsewhere to apply and continue"),
              "\xE2\x9C\x93 applies \xC2\xB7 \xE2\x9C\x95 cancels \xC2\xB7 tap elsewhere to apply and continue");
    // Texts written for both say it once.
    EXPECT_EQ(touchWording("Click a flat face, then Hole, then click or tap where each hole goes."),
              "Tap a flat face, then Hole, then tap where each hole goes.");
    EXPECT_EQ(touchWording("Click or tap the face where a hole goes"), "Tap the face where a hole goes");
    // Word rules keep word forms right.
    EXPECT_EQ(touchWording("clicking twice, clicked, clicks"), "tapping twice, tapped, taps");
    // Text without mouse or keyboard words is left alone.
    EXPECT_EQ(touchWording("Add a box, or start a sketch."), "Add a box, or start a sketch.");
    EXPECT_EQ(touchWording("Some steps can no longer be built."), "Some steps can no longer be built.");
}

TEST(TouchWording, DetectsMouseAndKeyboardWords)
{
    EXPECT_TRUE(mentionsMouseOrKeyboard("Shift-click adds"));
    EXPECT_TRUE(mentionsMouseOrKeyboard("press Enter"));
    EXPECT_TRUE(mentionsMouseOrKeyboard("Esc cancels"));
    EXPECT_TRUE(mentionsMouseOrKeyboard("scroll to zoom"));
    EXPECT_TRUE(mentionsMouseOrKeyboard("width, Tab, height"));
    EXPECT_TRUE(mentionsMouseOrKeyboard("Alt for 1 degree"));
    EXPECT_FALSE(mentionsMouseOrKeyboard("Tap the center of the polygon"));  // "center" is not "enter"
    EXPECT_FALSE(mentionsMouseOrKeyboard("the middle of a side, an alternative")); // nor "alternative" "Alt"
}

TEST(TouchWording, EverySketchHintIsTouchReady)
{
    Harness h;
    h.controller.setTouchLayout(true);
    ASSERT_TRUE(h.controller.startSketch(InteractionController::SketchPlane::Top));
    h.controller.skipAnimation();
    auto* session = h.controller.sketchSession();
    ASSERT_NE(session, nullptr);
    std::set<std::string> hints;
    const SketchTool tools[] = {SketchTool::Line,   SketchTool::Rectangle, SketchTool::CenterRectangle,
                                SketchTool::Polygon, SketchTool::Circle,   SketchTool::Arc,
                                SketchTool::TangentArc, SketchTool::Slot,  SketchTool::Trim,
                                SketchTool::Select};
    for (SketchTool tool : tools) {
        h.controller.setSketchTool(tool);
        hints.insert(session->hintText());
        // First and second taps: the hint for the next point.
        h.tap(h.sketchScreen({0, 0}));
        hints.insert(session->hintText());
        h.tap(h.sketchScreen({20, 0}));
        hints.insert(session->hintText());
        h.controller.setSketchTool(tool); // start over (ends a line chain too)
    }
    // A rectangle to select from: constraints, mirror, pattern and offset modes.
    h.controller.setSketchTool(SketchTool::Rectangle);
    h.tap(h.sketchScreen({0, 0}));
    h.tap(h.sketchScreen({30, 20}));
    h.controller.setSketchTool(SketchTool::Select);
    h.tap(h.sketchScreen({15, 0})); // the bottom line
    hints.insert(session->hintText());
    for (const char* mode : {"mirror", "pattern", "offset"}) {
        (void)h.controller.triggerAction(mode);
        hints.insert(session->hintText());
        (void)h.controller.triggerAction(mode); // again: cancel
    }
    (void)h.controller.triggerAction("pattern");
    (void)h.controller.triggerAction("pattern:circular");
    hints.insert(session->hintText());
    EXPECT_GE(hints.size(), 15u);
    for (const std::string& hint : hints)
        expectTouchReady(hint);
    for (const std::string& message : h.messages)
        expectTouchMessage(message);
}

TEST(TouchWording, EveryToolExplanationIsTouchReady)
{
    Harness h;
    h.controller.setTouchLayout(true);
    // Nothing selected: every Modify/Combine tool says what to select.
    for (const char* tool : {"pushpull", "fillet", "chamfer", "shell", "offset", "hole", "text", "move", "rotate", "mirror",
                             "pattern", "align", "union", "subtract", "intersect", "measure"})
        EXPECT_FALSE(h.controller.runTool(tool).ok()) << tool;
    EXPECT_GE(h.messages.size(), 16u);
    for (const std::string& message : h.messages)
        expectTouchMessage(message);
}

TEST(TouchWording, OperationPromptsAreTouchReady)
{
    Harness h;
    h.controller.setTouchLayout(true);
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    // A face: Align waits for its target.
    h.tap(h.controller.camera().project({0, 0, 20}));
    ASSERT_TRUE(h.controller.triggerAction("align").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    const std::string align = h.controller.operation()->prompt();
    EXPECT_FALSE(align.empty());
    expectTouchReady(align);
    h.controller.cancelOperation();
    // The body: Mirror waits for a plane.
    ASSERT_TRUE(h.controller.selectBody(h.document.bodies().front()->id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    const std::string mirror = h.controller.operation()->prompt();
    EXPECT_FALSE(mirror.empty());
    expectTouchReady(mirror);
}

TEST(TouchWording, TextToolPromptIsTouchReady)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    Harness h;
    h.controller.setTouchLayout(true);
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    h.tap(h.controller.camera().project({3, -3, 20}));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    const std::string prompt = h.controller.operation()->prompt();
    EXPECT_FALSE(prompt.empty());
    expectTouchReady(prompt);
    EXPECT_EQ(touchWording(prompt), "Type the text in the box \xC2\xB7 tap the face to move it "
                                    "(it snaps to the center and the middles of the edges)");
}

TEST(TouchWording, TappingEmptySpaceGivesUpAWaitingAlignOrMirror)
{
    // The touch prompts say "tap empty space to cancel": it must be true.
    Harness h;
    h.controller.setTouchLayout(true);
    ASSERT_TRUE(h.controller.createBox(20).ok());
    h.controller.fitAll(false);
    const Vec2 empty{20, 780}; // a corner of the view, far from the box
    h.tap(h.controller.camera().project({0, 0, 20}));
    ASSERT_TRUE(h.controller.triggerAction("align").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    ASSERT_FALSE(h.controller.operation()->prompt().empty()); // waiting for the target
    h.tap(empty);
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.controller.operation(), nullptr);

    ASSERT_TRUE(h.controller.selectBody(h.document.bodies().front()->id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    ASSERT_FALSE(h.controller.operation()->prompt().empty()); // waiting for a plane
    h.tap(empty);
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_EQ(h.document.bodies().size(), 1u); // nothing was mirrored

    // A mouse click there keeps waiting (a near miss is common with a mouse).
    ASSERT_TRUE(h.controller.selectBody(h.document.bodies().front()->id(), false).ok());
    ASSERT_TRUE(h.controller.triggerAction("mirror").ok());
    PointerEvent click;
    click.position = empty;
    h.controller.pointerPress(click);
    h.controller.pointerRelease(click);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_FALSE(h.controller.operation()->prompt().empty());
}

TEST(TouchWording, NamesInMessagesAreLeftAlone)
{
    // Only the app's own instructions are worded for touch: a body (or a
    // project, a file) named "Click bar" keeps its name in every message.
    Harness h;
    h.controller.setTouchLayout(true);
    const auto addBox = [&](const std::string& name, Vec3 origin, Vec3 size) {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = origin;
        box->size = size;
        EXPECT_TRUE(h.stack.push(std::make_unique<cmd::CreateBodyCommand>(name, std::move(box)), h.document).ok());
        h.controller.documentChanged();
        return h.document.bodies().back()->id();
    };
    const Uuid bar = addBox("Click bar", {0, 0, 0}, {100, 10, 10});
    const Uuid cut = addBox("Cut", {40, -1, -1}, {4, 12, 12});
    ASSERT_TRUE(h.controller.selectBody(bar, false).ok());
    ASSERT_TRUE(h.controller.selectBody(cut, true).ok());
    h.messages.clear();
    ASSERT_TRUE(h.controller.triggerAction("subtract").ok());
    ASSERT_EQ(h.document.body(bar)->shape().solidCount(), 2);
    const auto pieces = std::find_if(h.messages.begin(), h.messages.end(),
                                     [](const std::string& m) { return m.find("separate pieces") != std::string::npos; });
    ASSERT_NE(pieces, h.messages.end());
    EXPECT_EQ(*pieces, "Click bar is now in 2 separate pieces. To make each piece a body, select it and choose Split into bodies.");

    // The instructions are worded for the input in use.
    (void)h.controller.keyPress(Key::Escape);
    ASSERT_TRUE(h.controller.selection().empty());
    h.messages.clear();
    EXPECT_FALSE(h.controller.runTool("pushpull").ok());
    ASSERT_EQ(h.messages.size(), 1u);
    EXPECT_EQ(h.messages.back(), "Tap a flat face, then drag its arrow or type a distance.");
    h.controller.setTouchLayout(false);
    const Status explained = h.controller.runTool("pushpull");
    EXPECT_EQ(h.messages.back(), "Click a flat face, then drag its arrow or type a distance.");
    EXPECT_EQ(explained.userMessage(), h.messages.back());
    // A status that goes to the app as a message (Combine without two bodies).
    h.controller.setTouchLayout(true);
    const Status combine = h.controller.triggerAction("union");
    EXPECT_FALSE(combine.ok());
    expectTouchMessage(combine.userMessage());
    EXPECT_NE(combine.userMessage().find("double-tap"), std::string::npos) << combine.userMessage();
}
