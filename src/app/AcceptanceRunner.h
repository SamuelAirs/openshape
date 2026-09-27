// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Math.h"

#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstddef>
#include <functional>
#include <vector>

class QQuickItem;
class QQuickWindow;

namespace os::ui {
class AppController;
}
namespace os::doc {
class Body;
}

namespace os::app {

// End-to-end acceptance test of the real application: injects mouse and
// keyboard input through Qt's platform input path (the same path the OS
// uses), so Qt Quick event delivery, QML key handling, shortcuts, the
// viewport item and the interaction core are all exercised together.
// Geometry is verified with exact measurements at each step and screenshots
// are written for visual review. Exit code = number of failed checks.
//
// The run is a sequence of named scenarios (see AcceptanceScenario below):
// "core" is the original story (Milestones 0 and 1, then most tools on one
// growing model); every other scenario lives in its own file in
// src/app/acceptance/ and starts from a new, empty document.
class AcceptanceRunner : public QObject {
    Q_OBJECT

public:
    using Step = std::function<void()>;

    AcceptanceRunner(QQuickWindow* window, ui::AppController* app, QString outputDir, QObject* parent = nullptr);
    // Only run these scenarios (by name; empty = all).
    void setScenarioFilter(const QStringList& names) { filter_ = names; }
    void start();

    // ---- For scenarios -----------------------------------------------------------

    QQuickWindow* window() const { return window_; }
    ui::AppController& app() const { return *app_; }
    // Where screenshots go (files for review can go there too).
    const QString& outputDir() const { return outputDir_; }

    // Input helpers (window-local logical coordinates).
    void mouseMove(QPointF p, Qt::MouseButtons held = Qt::NoButton);
    void mousePress(QPointF p, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void mouseRelease(QPointF p, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void drag(QPointF from, QPointF to, int steps = 10);
    void key(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = {});
    void type(const QString& text);
    // A quick multi-finger tap through Qt's touch path (fingers at `points`).
    void touchTap(const QList<QPointF>& points);
    // Clicks the center of a QML item found by objectName, first scrolling any
    // Flickable around it (the tool palette in a short window) so it is on
    // screen, as a user would; false if not found/visible.
    bool clickItem(const QString& objectName, Qt::KeyboardModifiers mods = Qt::NoModifier);
    // A QML item by objectName (declared items and generated delegates; a
    // visible one when two share the name), or null.
    QQuickItem* findItem(const QString& objectName) const;
    // Resizes the window (logical px; below the desktop minimum too, e.g. a
    // phone). The next scenario starts at the size the run started with.
    void resizeWindow(int width, int height);
    QSize initialWindowSize() const { return initialSize_; }

    // The document's body `index`. If an earlier failure left fewer bodies,
    // records a failure and abandons the rest of the current scenario
    // (instead of undefined behaviour); the next scenario still runs.
    const doc::Body& body(std::size_t index);
    // Where a model point appears in the window.
    QPointF screenPoint(double x, double y, double z) const;
    // The first of these model points (e.g. along one edge) that a click
    // reaches in the 3D view, not under a panel or the value chip; the first
    // if none does. Small windows (the CI Mac's 1024x653, TD-35) put the chip
    // over the point a large window leaves free.
    QPointF uncoveredScreenPoint(const std::vector<Vec3>& candidates) const;
    // The item a click at `p` reaches (topmost visible, taking mouse buttons).
    QQuickItem* itemAt(QPointF p) const;
    double bodyHeight() const;
    double bodyVolume() const;
    void check(bool condition, const QString& description, const QString& actual = {});
    void screenshot(const QString& name);
    static QString num(double v) { return QString::number(v, 'f', 6); }

private:
    struct AbortScenario {};
    // Logical window position -> the native (device) pixels Qt's platform input takes.
    QPointF nativeLocal(QPointF p) const;
    QPointF nativeGlobal(QPointF p) const;
    void runNext();
    void beginScenario(const QString& name, bool reset);
    void endScenario();
    std::vector<Step> coreScenario();
    QString describeClick() const;

    QQuickWindow* window_;
    ui::AppController* app_;
    QString outputDir_;
    QStringList filter_;
    std::vector<Step> steps_;
    std::vector<std::size_t> scenarioStarts_; // index of each scenario's first step
    std::size_t next_ = 0;
    int failures_ = 0;
    int checks_ = 0;
    QString scenario_;
    int scenarioChecks_ = 0;
    int scenarioFailures_ = 0;
    QStringList summary_;
    int animationWaitMs_ = 0; // for a camera animation to end before the next step
    int previewWaitMs_ = 0;   // for previews (computed on the worker) to be shown
    QSize initialSize_;
    QSize initialMinimum_;
    QString initialAppFolder_;
    QPointF lastClick_;
    bool hasLastClick_ = false;
    double holeBlockVolume_ = 0; // the right block before its hole (face-edit checks)
};

// A named part of the acceptance run. Scenarios run by `order` (then name);
// each one after the first starts from a new, empty document (touch and pen
// mode off, millimeters, isometric view, no overlays open, the window at the
// size the run started with).
//
// To add one, create src/app/acceptance/<Name>.cpp (CMake picks it up) with
//
//   namespace os::app {
//   namespace {
//   std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r) { return { [&r] { ... }, ... }; }
//   const bool registered = registerAcceptanceScenario({"name", 100, steps});
//   } }
//
// Steps run 160 ms apart, and each waits until camera animations end and
// previews (computed on a worker thread) are shown: a step sees the preview
// or the error of a value the step before it typed or dragged. Add empty
// steps ([] {}) for other things to settle. Keep per-scenario state in a
// std::shared_ptr captured by the steps.
struct AcceptanceScenario {
    QString name;
    int order = 100; // "core" is 0
    std::function<std::vector<AcceptanceRunner::Step>(AcceptanceRunner&)> steps;
};
bool registerAcceptanceScenario(AcceptanceScenario scenario);
// All registered scenarios, in run order ("core" is added by the runner).
std::vector<AcceptanceScenario> acceptanceScenarios();

} // namespace os::app
