#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QString>

#include <functional>
#include <vector>

class QQuickWindow;

namespace os::ui {
class AppController;
}

namespace os::app {

// End-to-end acceptance test of the real application: injects mouse and
// keyboard input through Qt's platform input path (the same path the OS
// uses), so Qt Quick event delivery, QML key handling, shortcuts, the
// viewport item and the interaction core are all exercised together.
// Geometry is verified with exact measurements at each step and screenshots
// are written for visual review. Exit code = number of failed checks.
class AcceptanceRunner : public QObject {
    Q_OBJECT

public:
    AcceptanceRunner(QQuickWindow* window, ui::AppController* app, QString outputDir, QObject* parent = nullptr);
    void start();

private:
    using Step = std::function<void()>;
    void runNext();

    // Input helpers (window-local logical coordinates).
    void mouseMove(QPointF p, Qt::MouseButtons held = Qt::NoButton);
    void mousePress(QPointF p, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void mouseRelease(QPointF p, Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier);
    void drag(QPointF from, QPointF to, int steps = 10);
    void key(int key, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& text = {});
    void type(const QString& text);
    // Clicks the center of a QML item found by objectName; false if not found/visible.
    bool clickItem(const QString& objectName);

    QPointF screenPoint(double x, double y, double z) const;
    double bodyHeight() const;
    double bodyVolume() const;
    void check(bool condition, const QString& description, const QString& actual = {});
    void screenshot(const QString& name);

    QQuickWindow* window_;
    ui::AppController* app_;
    QString outputDir_;
    std::vector<Step> steps_;
    std::size_t next_ = 0;
    int failures_ = 0;
    int checks_ = 0;
};

} // namespace os::app
