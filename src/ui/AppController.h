// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "io/Recovery.h"
#include "ui/AppSettings.h"

#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace os::ui {

class RecoverySession;

// Pixels (square) of the preview saved in project files.
inline constexpr int kThumbnailSize = 256;

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
    // The Model panel rows and the action buttons: their own signals, emitted
    // only when their content changes (TD-18: a drag step used to rebuild both).
    Q_PROPERTY(QVariantList contextActions READ contextActions NOTIFY contextActionsChanged)
    Q_PROPERTY(bool operationActive READ operationActive NOTIFY stateChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueLabel READ operationValueLabel NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueText READ operationValueText NOTIFY stateChanged)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged)
    Q_PROPERTY(bool operationCanCommit READ operationCanCommit NOTIFY stateChanged)
    Q_PROPERTY(bool operationHasValue READ operationHasValue NOTIFY stateChanged)
    Q_PROPERTY(QString operationPrompt READ operationPrompt NOTIFY stateChanged)
    // The Text tool: the chip shows a text field for the words (setOperationText).
    Q_PROPERTY(bool operationTakesText READ operationTakesText NOTIFY stateChanged)
    Q_PROPERTY(QString operationText READ operationText NOTIFY stateChanged)
    // The words were typed or erased in this use of the tool (until then the
    // first key typed in the view replaces the remembered ones).
    Q_PROPERTY(bool operationTextTyped READ operationTextTyped NOTIFY stateChanged)
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
    Q_PROPERTY(bool sketchCounterVisible READ sketchCounterVisible NOTIFY stateChanged)
    Q_PROPERTY(QString sketchCounterText READ sketchCounterText NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sketchLabels READ sketchLabels NOTIFY viewChanged)
    Q_PROPERTY(bool canStartSketch READ canStartSketch NOTIFY stateChanged)
    Q_PROPERTY(int sketchCount READ sketchCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // Recovery copies left by a crashed run, offered for restoring
    // ({session, title, detail, time}); empty once each is restored or discarded.
    Q_PROPERTY(QVariantList recoveryItems READ recoveryItems NOTIFY recoveryChanged)
    // File → Open Recent: {path, name, folder}, most recent first, existing
    // files only (as of the last refreshRecentFiles(), open or save).
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    // The start screen (Home): shown at launch without a file and from File ->
    // Home; opening, New and importing as a project close it.
    Q_PROPERTY(bool homeVisible READ homeVisible WRITE setHomeVisible NOTIFY homeChanged)
    // What Home lists: {path, name, folder, modified, thumbnail (image source),
    // removable}: the recent files, and on iPadOS also the projects in the
    // app's Documents folder.
    Q_PROPERTY(QVariantList homeProjects READ homeProjects NOTIFY recentFilesChanged)
    // File → Preferences… (stored in QSettings, applied at once).
    Q_PROPERTY(QString defaultUnit READ defaultUnit WRITE setDefaultUnit NOTIFY preferencesChanged)
    Q_PROPERTY(bool sketchGridSnap READ sketchGridSnap WRITE setSketchGridSnap NOTIFY preferencesChanged)
    // Seconds; 0 = no recovery copies. One of kRecoveryIntervals.
    Q_PROPERTY(int recoveryInterval READ recoveryInterval WRITE setRecoveryInterval NOTIFY preferencesChanged)
    // Millimeters (0-1) added to screw clearance hole and head seat presets.
    Q_PROPERTY(double holeAllowance READ holeAllowance WRITE setHoleAllowance NOTIFY preferencesChanged)
    // iPhone / iPad: projects are saved by name into the app's own folder
    // (Documents, which the Files app shows) and exports go to its Exports
    // folder: iOS has no save dialog (Qt's FileDialog opens only). False on
    // the desktop, which uses file dialogs.
    Q_PROPERTY(bool savesToAppFolder READ savesToAppFolder NOTIFY appFolderChanged)
    // The app's folder as a URL (for the Open picker to start in), or "".
    Q_PROPERTY(QString appFolderUrl READ appFolderUrl NOTIFY appFolderChanged)

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
    QVariantList contextActions() const { return contextActionsList_; }
    bool operationActive() const;
    QString operationTitle() const;
    QString operationValueLabel() const;
    QString operationValueText() const;
    QString operationError() const;
    bool operationCanCommit() const;
    bool operationHasValue() const;
    QString operationPrompt() const;
    bool operationTakesText() const;
    QString operationText() const;
    bool operationTextTyped() const;
    QPointF valueLabelPosition() const;
    bool valueLabelVisible() const;
    QVariantList axisTriad() const;
    bool touchMode() const;
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
    bool sketchCounterVisible() const;
    QString sketchCounterText() const;
    QVariantList sketchLabels() const;
    bool canStartSketch() const;
    int sketchCount() const { return int(document_->sketches().size()); }
    QVariantList history() const { return historyList_; }
    QVariantList recoveryItems() const;
    QVariantList recentFiles() const { return recentFiles_; }
    bool homeVisible() const { return homeVisible_; }
    void setHomeVisible(bool visible);
    QVariantList homeProjects() const { return homeProjects_; }
    QString defaultUnit() const;
    void setDefaultUnit(const QString& symbol);
    bool sketchGridSnap() const { return preferences_.sketchGridSnap; }
    void setSketchGridSnap(bool on);
    int recoveryInterval() const { return preferences_.recoveryIntervalSeconds; }
    void setRecoveryInterval(int seconds);
    double holeAllowance() const { return preferences_.holeAllowance; }
    // Ignored outside 0-1 mm; applied at once (an open Hole tool follows).
    void setHoleAllowance(double mm);
    // Text typed in Preferences ("0.25", "0.3 mm"): "" when taken, else why not.
    Q_INVOKABLE QString setHoleAllowanceText(const QString& text);

    // ---- Recovery copies (see io/Recovery.h) ----
    // Starts this run's recovery session in `directory`; until then no copies
    // are written. While the document has unsaved changes, a copy is written
    // kRecoveryDebounceMs after edits settle and at least every
    // recoveryInterval() seconds; it is removed on Save, New, Open and
    // discardUnsavedWork(). At the end of the run (endRecovery) it stays if
    // the document still has unsaved changes.
    void startRecovery(const QString& directory);
    RecoverySession* recoverySession() const { return recovery_.get(); }
    // Looks for copies left by crashed runs (fills recoveryItems).
    Q_INVOKABLE void checkForRecovery();
    // Opens a crashed run's copy in place of the current document: unsaved,
    // with its original file remembered; its copy becomes this run's.
    Q_INVOKABLE bool restoreRecovery(const QString& session);
    Q_INVOKABLE void discardRecovery(const QString& session);
    Q_INVOKABLE void discardAllRecovery();
    // "Decide later": the copies stay and are offered at the next start.
    Q_INVOKABLE void postponeRecovery();
    // Writes this run's copy now if there are unsaved changes not in it yet.
    void writeRecoveryCopy();
    // "Don't Save" when closing the window: the user lets go of the unsaved
    // work, so its copy is removed and none is kept at exit (unless there are
    // edits after this).
    Q_INVOKABLE void discardUnsavedWork();
    // The run ends (QCoreApplication::aboutToQuit; the destructor for exits
    // that skip it). Unsaved work the user did not discard stays as a
    // recovery copy (brought up to date first) that the next start offers:
    // iPadOS may end the app at any time, Windows when the user logs off.
    // Otherwise the copy is removed. Runs once.
    void endRecovery();
    // This run's copy ("" when there is none).
    QString recoveryCopyFile() const;

    // ---- GUI-thread time per pointer move (the viewport reports each) ----
    // A slow move is logged at debug level ("gui: pointer move took N ms");
    // each drag ends with one line for its longest move. The watch_log
    // script reports both.
    void notePointerMove(double milliseconds, bool dragging);
    void notePointerRelease();
    // The last finished drag: its longest pointer move (ms) and its moves.
    double lastDragLongestMs() const { return lastDragLongestMs_; }
    double lastDragAverageMs() const { return lastDragAverageMs_; }
    int lastDragMoves() const { return lastDragMoves_; }

    Q_INVOKABLE bool openRecent(const QString& path);
    Q_INVOKABLE void clearRecentFiles();
    // Home: "Remove from list" (the file stays where it is).
    Q_INVOKABLE void removeRecentFile(const QString& path);
    // Rereads the list and drops files that are gone (e.g. deleted in
    // Explorer while the app runs); the File menu calls it as it opens.
    Q_INVOKABLE void refreshRecentFiles();

    Q_INVOKABLE void newDocument();
    Q_INVOKABLE bool openProject(const QUrl& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QUrl& url);
    Q_INVOKABLE bool hasProjectPath() const { return !path_.isEmpty(); }
    Q_INVOKABLE bool exportStep(const QUrl& url);
    // File -> Import STEP...: every closed solid in the file becomes a body
    // (one undo step); what was skipped (open surfaces, curves) is said in
    // the message. False (with a message) when nothing could be imported.
    Q_INVOKABLE bool importStep(const QUrl& url);
    // Home -> Import STEP...: the same into a new document (the current one
    // stays when the file cannot be imported).
    Q_INVOKABLE bool importStepAsProject(const QUrl& url);
    // Acceptance runs cannot click native file dialogs: they give the file
    // the next dialog would return here, and the QML takes it instead of
    // opening the dialog (then the usual onAccepted code runs).
    Q_INVOKABLE void setNextFileChoice(const QUrl& url) { nextFileChoice_ = url; }
    Q_INVOKABLE QUrl takeNextFileChoice();
    Q_INVOKABLE bool exportStl(const QUrl& url);
    Q_INVOKABLE bool export3mf(const QUrl& url);

    // ---- The app folder (iPhone / iPad; see savesToAppFolder)
    bool savesToAppFolder() const { return !appFolder_.isEmpty(); }
    QString appFolder() const { return appFolder_; }
    QString appFolderUrl() const;
    // Saving there by name (projectFileBaseName): replaces a project of that
    // name. False, with a message, if the name is unusable or saving fails.
    Q_INVOKABLE bool saveInAppFolder(const QString& name);
    // Whether a project of that name is there already (Save then replaces it).
    Q_INVOKABLE bool appFolderHasProject(const QString& name) const;
    // Exports the visible bodies to <app folder>/Exports/<document title>.<format>
    // ("stl", "3mf" or "step"), replacing an earlier export of that name.
    Q_INVOKABLE bool exportToAppFolder(const QString& format);
    // Where to save without dialogs: the Documents folder on iOS and Android
    // (set at start), "" for file dialogs; tests and --app-folder set it.
    void setAppFolder(const QString& folder);

    Q_INVOKABLE void createBox(double size = 20.0);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Gesture undo/redo (two/three-finger taps), and the Undo / Redo buttons
    // in the touch layout (no tooltip there), say what they did.
    Q_INVOKABLE void undoWithFeedback();
    Q_INVOKABLE void redoWithFeedback();
    // A hint written for mouse and keyboard, worded for touch (taps, the
    // on-screen ✓ / ✕; no Shift-click, Esc, Enter, scrolling or hovering).
    // Only for the app's own hints: a name in the text would be reworded too.
    Q_INVOKABLE QString touchWording(const QString& text) const;
    // False when the operation was not applied (the reason is shown).
    Q_INVOKABLE bool commitOperation();
    Q_INVOKABLE void cancelOperation();
    // Returns an error message ("" on success, or while the value's preview
    // still computes: its verdict comes with a state change). Previews live
    // as the user types.
    Q_INVOKABLE QString setValueText(const QString& text);
    // The same, waiting for the verdict of a preview still computing: Tab to
    // the next field moves on only with a usable value.
    Q_INVOKABLE QString confirmValueText(const QString& text);
    // The Text tool's words (previewed at once); returns the error, or "".
    Q_INVOKABLE QString setOperationText(const QString& text);
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
    // -/+ on the sketch counter (a polygon's sides, a pattern's copies).
    Q_INVOKABLE void stepSketchCounter(int delta);
    Q_INVOKABLE QString setSketchDimension(int constraintId, const QString& text);

    // History panel. Ids are UUID strings. Edits return an error message or "".
    Q_INVOKABLE QString setFeatureParameter(const QString& featureId, const QString& key, const QString& text);
    Q_INVOKABLE void setFeatureSuppressed(const QString& featureId, bool suppressed);
    Q_INVOKABLE void deleteHistoryItem(const QString& kind, const QString& id);
    Q_INVOKABLE void setHistoryItemVisible(const QString& kind, const QString& id, bool visible);
    Q_INVOKABLE void editSketch(const QString& sketchId);
    // Highlights a row's geometry in the view while hovered/expanded ("" clears).
    Q_INVOKABLE void highlightHistoryItem(const QString& id);
    // Selects a body from the panel; additive (Shift) adds it, e.g. to combine,
    // or takes it out again.
    Q_INVOKABLE void selectBody(const QString& bodyId, bool additive);
    // A tap on a body's row in the touch layout: adds the body (never takes it
    // out: tapping the row again folds it and keeps the body selected).
    Q_INVOKABLE void addBodyToSelection(const QString& bodyId);
    // Model panel row actions for a body.
    Q_INVOKABLE void duplicateBody(const QString& bodyId);
    Q_INVOKABLE void splitBody(const QString& bodyId);
    // Tool palette: runs a tool if the selection fits, otherwise explains it.
    Q_INVOKABLE void runTool(const QString& id);

signals:
    void stateChanged();
    void contextActionsChanged();
    void historyChanged();
    void viewChanged();
    void documentChanged();
    void message(const QString& text);
    void touchModeChanged();
    void recoveryChanged();
    void recentFilesChanged();
    void homeChanged();
    void preferencesChanged();
    void appFolderChanged();

private:
    void attach();
    // After every state change: rebuilds the Model panel rows and the action
    // list from the interaction core, and emits their signals if they changed.
    void refreshLists();
    void notifyMessage(const QString& text);
    // The document was replaced (New, Open, restore): no unsaved edits to copy yet.
    void documentReplaced();
    // Called on every state change: (re)arms the recovery timers after an edit.
    void noteEdits();
    void stopRecoveryTimers();
    void rememberRecentFile(const QString& path);
    // Recomputes recentFiles_ from the settings and the disk; emits on change.
    // With an app folder, entries into its old location (the app's data
    // folder moves when an iOS app is updated) are moved into the current one.
    void updateRecentFiles();
    // A remembered path (a recovery copy's project) where the file is now:
    // io::rebasedIntoFolder into the app folder, if there is one.
    QString currentLocation(const std::string& storedPath) const;
    void savePreferences() const;
    // thumbnail.png for a save: kThumbnailSize pixels square (empty without bodies).
    std::vector<unsigned char> thumbnailPng() const;
    // The visible bodies written as STL, 3MF or STEP (by `format`).
    Status writeExport(const QString& format, const std::filesystem::path& path);

    std::unique_ptr<doc::Document> document_;
    std::unique_ptr<cmd::UndoStack> undoStack_;
    std::unique_ptr<interact::InteractionController> interaction_;
    QString path_;
    Preferences preferences_;
    std::unique_ptr<RecoverySession> recovery_;
    std::vector<io::RecoveryEntry> orphans_; // offered for restoring
    QTimer recoveryDebounce_; // edits settled
    QTimer recoveryDeadline_; // at least this often while editing
    std::uint64_t seenRevision_ = 0;  // undo-stack revision at the last noteEdits()
    std::uint64_t copyRevision_ = ~std::uint64_t(0); // revision in this run's copy (~0: none)
    std::uint64_t discardedRevision_ = ~std::uint64_t(0); // revision the user chose Don't Save at
    bool recoveryWarned_ = false;
    bool recoveryEnded_ = false;
    QVariantList recentFiles_;
    std::vector<interact::HistoryRow> historyRows_;
    QVariantList historyList_;
    std::vector<interact::ContextAction> contextActions_;
    QVariantList contextActionsList_;
    int dragMoves_ = 0;
    double dragLongestMs_ = 0;
    double dragTotalMs_ = 0;
    int lastDragMoves_ = 0;
    double lastDragLongestMs_ = 0;
    double lastDragAverageMs_ = 0;
    QVariantList homeProjects_;
    bool homeVisible_ = false;
    QUrl nextFileChoice_;
    QString appFolder_; // see savesToAppFolder
};

} // namespace os::ui
