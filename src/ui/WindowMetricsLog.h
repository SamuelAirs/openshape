// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "ui/WindowMetrics.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

class QQuickWindow;
class QScreen;

namespace os::ui {

// Logs one line (Info, so it lands in Logs/openshape.log by default) each
// time the window's size, device pixel ratio, safe-area margins, layout or
// screen change: a phone turned, a Split View divider dragged, a folding
// phone folded or unfolded. Changes that arrive together (a fold resizes
// the window and moves its safe area) are gathered into one line once they
// have settled. The safe area is the window's own (QWindow::safeAreaMargins),
// or --safe-area's simulated one (the root item's `simulatedSafeArea`); the
// layout is Main.qml's `compactLayout`. A child of the window.
class WindowMetricsLog : public QObject {
    Q_OBJECT

public:
    explicit WindowMetricsLog(QQuickWindow* window);

    // The window as it is now (what the next line would say).
    WindowMetrics current() const;
    // The last line logged (the acceptance run reads it).
    QString lastLine() const { return lastLine_; }
    // Logs now if anything changed since the last line (the timer does it otherwise).
    void flush();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void schedule();

private:
    void watchScreen(QScreen* screen);

    QQuickWindow* window_;
    QPointer<QScreen> screen_;
    QTimer settle_;
    WindowMetrics last_;
    bool logged_ = false;
    QString lastLine_;
};

} // namespace os::ui
