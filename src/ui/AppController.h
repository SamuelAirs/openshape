// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"

#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace os::ui {

// Bridge between QML and the application core. Holds the document, undo
// stack and interaction controller; exposes their state as properties and
// user intents as invokables. Contains no modeling logic itself.
class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY stateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY stateChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY stateChanged)
    Q_PROPERTY(QVariantList contextActions READ contextActions NOTIFY stateChanged)
    Q_PROPERTY(bool operationActive READ operationActive NOTIFY stateChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueLabel READ operationValueLabel NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueText READ operationValueText NOTIFY stateChanged)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged)
    Q_PROPERTY(bool operationCanCommit READ operationCanCommit NOTIFY stateChanged)
    Q_PROPERTY(bool operationHasValue READ operationHasValue NOTIFY stateChanged)
    Q_PROPERTY(QString operationPrompt READ operationPrompt NOTIFY stateChanged)
    Q_PROPERTY(QPointF valueLabelPosition READ valueLabelPosition NOTIFY viewChanged)
    Q_PROPERTY(bool valueLabelVisible READ valueLabelVisible NOTIFY viewChanged)
    Q_PROPERTY(QVariantList axisTriad READ axisTriad NOTIFY viewChanged)
    // Used by touch: touch-sized controls and the Pen switch (on from the
    // start on tablets; on desktops after a touch, off after a real mouse click).
    Q_PROPERTY(bool touchMode READ touchMode NOTIFY touchModeChanged)
    // Pen mode: the pen selects and draws, fingers only navigate.
    Q_PROPERTY(bool penMode READ penMode WRITE setPenMode NOTIFY stateChanged)
    Q_PROPERTY(QString documentTitle READ documentTitle NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(int bodyCount READ bodyCount NOTIFY stateChanged)
    Q_PROPERTY(QString displayUnit READ displayUnit NOTIFY stateChanged)
    Q_PROPERTY(bool perspective READ perspective NOTIFY stateChanged)
    Q_PROPERTY(QString projectFolder READ projectFolder NOTIFY documentChanged)
    Q_PROPERTY(bool sketchMode READ sketchMode NOTIFY stateChanged)
    Q_PROPERTY(QString sketchName READ sketchName NOTIFY stateChanged)
    Q_PROPERTY(QString sketchTool READ sketchTool NOTIFY stateChanged)
    Q_PROPERTY(QString sketchStatus READ sketchStatus NOTIFY stateChanged)
    Q_PROPERTY(QString sketchHint READ sketchHint NOTIFY stateChanged)
    Q_PROPERTY(bool sketchDrawing READ sketchDrawing NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sketchLabels READ sketchLabels NOTIFY viewChanged)
    Q_PROPERTY(bool canStartSketch READ canStartSketch NOTIFY stateChanged)
    Q_PROPERTY(int sketchCount READ sketchCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY stateChanged)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    interact::InteractionController& interaction() { return *interaction_; }
    doc::Document& document() { return *document_; }

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool hasSelection() const;
    QString selectionSummary() const;
    QVariantList contextActions() const;
    bool operationActive() const;
    QString operationTitle() const;
    QString operationValueLabel() const;
    QString operationValueText() const;
    QString operationError() const;
    bool operationCanCommit() const;
    bool operationHasValue() const;
    QString operationPrompt() const;
    QPointF valueLabelPosition() const;
    bool valueLabelVisible() const;
    QVariantList axisTriad() const;
    bool touchMode() const { return touchMode_; }
    void setTouchMode(bool on);
    bool penMode() const;
    void setPenMode(bool on);
    QString documentTitle() const;
    bool dirty() const;
    int bodyCount() const;
    QString displayUnit() const;
    bool perspective() const;
    QString projectFolder() const;
    bool sketchMode() const;
    QString sketchName() const;
    QString sketchTool() const;
    QString sketchStatus() const;
    QString sketchHint() const;
    bool sketchDrawing() const;
    QVariantList sketchLabels() const;
    bool canStartSketch() const;
    int sketchCount() const { return int(document_->sketches().size()); }
    QVariantList history() const;

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openProject(const QUrl& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE bool hasProjectPath() const { return !path_.isEmpty(); }
    Q_INVOKABLE bool exportStep(const QUrl& url);
    Q_INVOKABLE bool exportStl(const QUrl& url);
    Q_INVOKABLE bool export3mf(const QUrl& url);

    Q_INVOKABLE void createBox(double size = 20.0);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Gesture undo/redo (two/three-finger taps) say what they did.
    void undoWithFeedback();
    void redoWithFeedback();
    Q_INVOKABLE void commitOperation();
    Q_INVOKABLE void cancelOperation();
    // Returns an error message ("" on success). Previews live as the user types.
    Q_INVOKABLE QString setValueText(const QString& text);
    Q_INVOKABLE void triggerAction(const QString& id);
    Q_INVOKABLE void setView(const QString& name);
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void togglePerspective();
    Q_INVOKABLE void setDisplayUnit(const QString& symbol);
    Q_INVOKABLE bool handleKey(int key);

    // Sketching
    // plane: "top" (default), "front" or "right"; ignored when a face is selected.
    Q_INVOKABLE void startSketch(const QString& plane = QString());
    Q_INVOKABLE bool faceSelected() const;
    Q_INVOKABLE void finishSketch();
    Q_INVOKABLE void setSketchTool(const QString& name);
    // Replaces the focused input's text while drawing; returns an error or "".
    Q_INVOKABLE QString sketchType(const QString& text);
    Q_INVOKABLE void focusNextSketchInput();
    Q_INVOKABLE void commitSketchTool();
    Q_INVOKABLE QString setSketchDimension(int constraintId, const QString& text);

    // History panel. Ids are UUID strings. Edits return an error message or "".
    Q_INVOKABLE QString setFeatureParameter(const QString& featureId, const QString& key, const QString& text);
    Q_INVOKABLE void setFeatureSuppressed(const QString& featureId, bool suppressed);
    Q_INVOKABLE void deleteHistoryItem(const QString& kind, const QString& id);
    Q_INVOKABLE void setHistoryItemVisible(const QString& kind, const QString& id, bool visible);
    Q_INVOKABLE void editSketch(const QString& sketchId);
    // Highlights a row's geometry in the view while hovered/expanded ("" clears).
    Q_INVOKABLE void highlightHistoryItem(const QString& id);
    // Selects a body from the panel; additive (Shift) adds it, e.g. to combine.
    Q_INVOKABLE void selectBody(const QString& bodyId, bool additive);
    // Model panel row actions for a body.
    Q_INVOKABLE void duplicateBody(const QString& bodyId);
    Q_INVOKABLE void splitBody(const QString& bodyId);
    // Tool palette: runs a tool if the selection fits, otherwise explains it.
    Q_INVOKABLE void runTool(const QString& id);

signals:
    void stateChanged();
    void viewChanged();
    void documentChanged();
    void message(const QString& text);
    void touchModeChanged();

private:
    void attach();
    void notifyMessage(const QString& text);

    std::unique_ptr<doc::Document> document_;
    std::unique_ptr<cmd::UndoStack> undoStack_;
    std::unique_ptr<interact::InteractionController> interaction_;
    QString path_;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    bool touchMode_ = true;
#else
    bool touchMode_ = false;
#endif
};

} // namespace os::ui
