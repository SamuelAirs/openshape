// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/WindowMetricsLog.h"

#include "core/Log.h"

#include <QtCore/QEvent>
#include <QtCore/QMetaProperty>
#include <QtCore/QVariantList>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtQuick/QQuickWindow>

#include <string>

namespace os::ui {
namespace {

// Calls `slot` on `receiver` whenever the QML property `name` of `object` changes.
void watchProperty(QObject* object, const char* name, QObject* receiver, const char* slot)
{
    const QMetaObject* meta = object->metaObject();
    const int index = meta->indexOfProperty(name);
    if (index < 0)
        return;
    const QMetaProperty property = meta->property(index);
    if (!property.hasNotifySignal())
        return;
    const int slotIndex = receiver->metaObject()->indexOfSlot(slot);
    if (slotIndex >= 0)
        QObject::connect(object, property.notifySignal(), receiver, receiver->metaObject()->method(slotIndex));
}

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
const char* stateName(Qt::ApplicationState state)
{
    switch (state) {
    case Qt::ApplicationSuspended: return "suspended";
    case Qt::ApplicationHidden: return "hidden";
    case Qt::ApplicationInactive: return "inactive";
    case Qt::ApplicationActive: return "active";
    }
    return "unknown";
}
#endif

std::string describeScreen(const QScreen* screen)
{
    return "\"" + screen->name().toStdString() + "\" " + std::to_string(screen->size().width()) + "x"
        + std::to_string(screen->size().height()) + " pt";
}

} // namespace

WindowMetricsLog::WindowMetricsLog(QQuickWindow* window) : QObject(window), window_(window)
{
    // A fold or a rotation arrives as several changes (width, height, safe
    // area) within a few event loop passes: one line once they settle.
    settle_.setSingleShot(true);
    settle_.setInterval(250);
    connect(&settle_, &QTimer::timeout, this, &WindowMetricsLog::flush);

    connect(window, &QWindow::widthChanged, this, &WindowMetricsLog::schedule);
    connect(window, &QWindow::heightChanged, this, &WindowMetricsLog::schedule);
    connect(window, &QWindow::safeAreaMarginsChanged, this, &WindowMetricsLog::schedule);
    connect(window, &QWindow::screenChanged, this, [this](QScreen* screen) {
        watchScreen(screen);
        schedule();
    });
    watchProperty(window, "simulatedSafeArea", this, "schedule()");
    watchProperty(window, "compactLayout", this, "schedule()");
    window->installEventFilter(this); // the device pixel ratio
    watchScreen(window->screen());
    schedule();

    // What the system does to the app around a fold: the owner's logs show
    // some runs ending ("exits normally") right after a display change and
    // others moving between the displays (TD-163). Whether iOS ended the app
    // or its last window closed is then in the log.
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
            [](Qt::ApplicationState state) { OS_LOG(Info, App) << "app state: " << stateName(state); });
#endif
    connect(qGuiApp, &QGuiApplication::screenAdded, this,
            [](QScreen* screen) { OS_LOG(Info, App) << "screen added: " << describeScreen(screen); });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this,
            [](QScreen* screen) { OS_LOG(Info, App) << "screen removed: " << describeScreen(screen); });
    connect(qGuiApp, &QGuiApplication::lastWindowClosed, this, [] { OS_LOG(Info, App) << "the last window closed"; });
}

void WindowMetricsLog::watchScreen(QScreen* screen)
{
    if (screen_)
        disconnect(screen_, nullptr, this, nullptr);
    screen_ = screen;
    if (!screen)
        return;
    connect(screen, &QScreen::geometryChanged, this, &WindowMetricsLog::schedule);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, &WindowMetricsLog::schedule);
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, &WindowMetricsLog::schedule);
}

bool WindowMetricsLog::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::DevicePixelRatioChange)
        schedule();
    return QObject::eventFilter(watched, event);
}

void WindowMetricsLog::schedule()
{
    settle_.start();
}

WindowMetrics WindowMetricsLog::current() const
{
    WindowMetrics m;
    m.width = window_->width();
    m.height = window_->height();
    m.devicePixelRatio = window_->devicePixelRatio();
    const QVariantList simulated = window_->property("simulatedSafeArea").toList();
    if (simulated.size() == 4) {
        m.safeAreaSimulated = true;
        m.safeTop = simulated[0].toDouble();
        m.safeRight = simulated[1].toDouble();
        m.safeBottom = simulated[2].toDouble();
        m.safeLeft = simulated[3].toDouble();
    } else {
        const QMargins margins = window_->safeAreaMargins();
        m.safeTop = margins.top();
        m.safeRight = margins.right();
        m.safeBottom = margins.bottom();
        m.safeLeft = margins.left();
    }
    const QVariant compact = window_->property("compactLayout");
    if (compact.isValid())
        m.layout = compact.toBool() ? QStringLiteral("compact") : QStringLiteral("regular");
    if (const QScreen* screen = window_->screen()) {
        m.screenName = screen->name();
        m.screenWidth = screen->geometry().width();
        m.screenHeight = screen->geometry().height();
        m.screenDevicePixelRatio = screen->devicePixelRatio();
    }
    return m;
}

void WindowMetricsLog::flush()
{
    settle_.stop();
    const WindowMetrics now = current();
    if (logged_ && now == last_)
        return;
    lastLine_ = describeWindowMetrics(now, logged_ ? windowMetricsChanges(last_, now) : QStringList{});
    OS_LOG(Info, App) << lastLine_.toStdString();
    last_ = now;
    logged_ = true;
}

} // namespace os::ui
