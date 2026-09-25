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
    Q_PROPERTY(QPointF valueLabelPosition READ valueLabelPosition NOTIFY viewChanged)
    Q_PROPERTY(bool valueLabelVisible READ valueLabelVisible NOTIFY viewChanged)
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
    QPointF valueLabelPosition() const;
    bool valueLabelVisible() const;
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

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openProject(const QUrl& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE bool hasProjectPath() const { return !path_.isEmpty(); }
    Q_INVOKABLE bool exportStep(const QUrl& url);
    Q_INVOKABLE bool exportStl(const QUrl& url);

    Q_INVOKABLE void createBox(double size = 20.0);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
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
    Q_INVOKABLE void startSketch();
    Q_INVOKABLE void finishSketch();
    Q_INVOKABLE void setSketchTool(const QString& name);
    // Replaces the focused input's text while drawing; returns an error or "".
    Q_INVOKABLE QString sketchType(const QString& text);
    Q_INVOKABLE void focusNextSketchInput();
    Q_INVOKABLE void commitSketchTool();
    Q_INVOKABLE QString setSketchDimension(int constraintId, const QString& text);

signals:
    void stateChanged();
    void viewChanged();
    void documentChanged();
    void message(const QString& text);

private:
    void attach();
    void notifyMessage(const QString& text);

    std::unique_ptr<doc::Document> document_;
    std::unique_ptr<cmd::UndoStack> undoStack_;
    std::unique_ptr<interact::InteractionController> interaction_;
    QString path_;
};

} // namespace os::ui
