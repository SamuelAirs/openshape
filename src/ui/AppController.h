// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "io/Recovery.h"
#include "ui/AppSettings.h"
#include "ui/Licenses.h"
#include "ui/IncomingFiles.h"
#include "ui/MeasureController.h"
#include "ui/Purchase.h"
#include "ui/Share.h"
#include "ui/Store.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QRectF>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QThread;

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
    // The operation's value is an angle (degrees): the keypad offers the degree sign.
    Q_PROPERTY(bool operationIsAngle READ operationIsAngle NOTIFY stateChanged)
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueLabel READ operationValueLabel NOTIFY stateChanged)
    Q_PROPERTY(QString operationValueText READ operationValueText NOTIFY stateChanged)
    Q_PROPERTY(QString operationError READ operationError NOTIFY stateChanged)
    // Why the value typed was refused when it was taken (not a length, out of
    // range; InteractionController::typedValueError), "" otherwise.
    Q_PROPERTY(QString typedValueError READ typedValueError NOTIFY stateChanged)
    // A value typed key by key waits for its pause (typeValueText).
    Q_PROPERTY(bool typingPending READ typingPending NOTIFY stateChanged)
    Q_PROPERTY(bool operationCanCommit READ operationCanCommit NOTIFY stateChanged)
    Q_PROPERTY(bool operationHasValue READ operationHasValue NOTIFY stateChanged)
    Q_PROPERTY(QString operationPrompt READ operationPrompt NOTIFY stateChanged)
    // Extrude / Revolve: what the profiles make ("new", "join", "cut"; "" for
    // other operations), and whether the sketch lies on a body's face (out
    // of it adds material, into it cuts: the hints say so).
    Q_PROPERTY(QString operationMode READ operationMode NOTIFY stateChanged)
    Q_PROPERTY(bool operationOnBody READ operationOnBody NOTIFY stateChanged)
    // Extrude: New body / Join / Cut was chosen (otherwise the direction and
    // the preview decide it). The hints only promise what the choice does.
    Q_PROPERTY(bool operationModeChosen READ operationModeChosen NOTIFY stateChanged)
    // The Text tool: the chip shows a text field for the words (setOperationText).
    Q_PROPERTY(bool operationTakesText READ operationTakesText NOTIFY stateChanged)
    Q_PROPERTY(QString operationText READ operationText NOTIFY stateChanged)
    // The words were typed or erased in this use of the tool (until then the
    // first key typed in the view replaces the remembered ones).
    Q_PROPERTY(bool operationTextTyped READ operationTextTyped NOTIFY stateChanged)
    Q_PROPERTY(QPointF valueLabelPosition READ valueLabelPosition NOTIFY viewChanged)
    Q_PROPERTY(bool valueLabelVisible READ valueLabelVisible NOTIFY viewChanged)
    // What the value chip must not cover (InteractionController::keepClearRect):
    // a rect in window coordinates, or null when there is nothing.
    Q_PROPERTY(QVariant keepClearRect READ keepClearRect NOTIFY viewChanged)
    // An arrow or ring is being dragged (a docked value chip keeps its side).
    Q_PROPERTY(bool manipulatorDragging READ manipulatorDragging NOTIFY viewChanged)
    // The window's safe-area insets [top, right, bottom, left] in logical px
    // (Main.qml binds Theme.safe*): the sketch's live values moved out from
    // under a finger stay inside them.
    Q_PROPERTY(QVariantList safeInsets READ safeInsets WRITE setSafeInsets NOTIFY viewChanged)
    // How far the sketch's controls reach into the view [top, right, bottom,
    // left] (Main.qml: in a compact window the top bar and the Finish bar,
    // the hint and the tool strip): a sketch started on a face frames it
    // between them (InteractionController::setFrameInsets).
    Q_PROPERTY(QVariantList frameInsets READ frameInsets WRITE setFrameInsets NOTIFY viewChanged)
    // The view cube (AxisTriad.qml): its six faces and twelve edges as
    // interact::ViewCube::layout gives them, in its box coordinates.
    Q_PROPERTY(QVariantMap viewCube READ viewCube NOTIFY viewChanged)
    // The controls over the view while sketching (rects: top bar, sketch tool
    // bar, status, the right column): live values moved out from under a
    // finger and the loupe keep off them.
    Q_PROPERTY(QVariantList sketchObstacles READ sketchObstacles WRITE setSketchObstacles NOTIFY viewChanged)
    // The loupe while a finger places or drags a sketch point: {visible, x, y
    // (its center), sx, sy (the snapped point it magnifies), size, zoom, hint}.
    Q_PROPERTY(QVariantMap sketchLoupe READ sketchLoupe NOTIFY viewChanged)
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
    // 1:1 (Actual size, the View buttons; docs/MEASURE.md): the view at the
    // screen's true scale. The scale is the Measure workspace's
    // (setScreenScale), calibrated with a card or estimated.
    Q_PROPERTY(bool actualSize READ actualSize NOTIFY stateChanged)
    Q_PROPERTY(bool screenScaleCalibrated READ screenScaleCalibrated NOTIFY stateChanged)
    Q_PROPERTY(double screenScale READ screenScale NOTIFY stateChanged)
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
    // removable, deletable}: the recent files, and on iPadOS also the
    // projects in the app's Documents folder. deletable: Delete... is
    // offered (see deleteProject).
    Q_PROPERTY(QVariantList homeProjects READ homeProjects NOTIFY recentFilesChanged)
    // Home's Samples: {name, summary, thumbnail (image source)}, the sample
    // projects that come with the app (samples/Samples.h, setSamplesFolder).
    Q_PROPERTY(QVariantList homeSamples READ homeSamples NOTIFY samplesChanged)
    // A project (or STEP file) opening: read and rebuilt on a worker thread
    // while the window keeps drawing (Main.qml's loading card; Sam on build
    // 58: "it looks like it freezes"). loadingLabel says what is happening
    // ("Opening", "Importing", "Restoring"), loadingName to what, and
    // loadingThumbnail is its preview (an image source, or empty).
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString loadingLabel READ loadingLabel NOTIFY loadingChanged)
    Q_PROPERTY(QString loadingName READ loadingName NOTIFY loadingChanged)
    Q_PROPERTY(QString loadingThumbnail READ loadingThumbnail NOTIFY loadingChanged)
    // Where Home's Delete... puts a project: "the Recycle Bin" (Windows),
    // "the Trash" (macOS), "Recently Deleted in the Files app" (iPhone /
    // iPad: QFile::moveToTrash moves it to Documents/.Trash there), "the trash".
    Q_PROPERTY(QString trashName READ trashName CONSTANT)
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
    // About: the source code of exactly this build (its release tag's or
    // commit's tree; ui/Licenses) and what identifies it ("build 57, commit ...").
    Q_PROPERTY(QString sourceCodeUrl READ sourceCodeUrl CONSTANT)
    Q_PROPERTY(QString buildDescription READ buildDescription CONSTANT)
    // The system share sheet is there (iPhone / iPad; a stub in the
    // acceptance run): exports into the app folder open it, and File ->
    // Share Project... shares the project file.
    Q_PROPERTY(bool canShare READ canShare NOTIFY shareChanged)
    // ---- The App Store purchase "Unlimited projects" (ui/Store.h, ui/Purchase.h)
    // There is a store (iPhone / iPad; a stand-in in the acceptance run):
    // About shows the purchase row. Without one there is no limit and no
    // purchase UI (Windows, macOS).
    Q_PROPERTY(bool storeAvailable READ storeAvailable NOTIFY purchaseChanged)
    Q_PROPERTY(bool projectsUnlocked READ projectsUnlocked NOTIFY purchaseChanged)
    // The free version's limit applies (a store, not unlocked): Home's line.
    Q_PROPERTY(bool projectLimited READ projectLimited NOTIFY purchaseChanged)
    // The purchase sheet (PurchaseOverlay.qml) and its state: purchaseStatus
    // is a PurchaseStatus name ("loading", "ready", "buying", "waiting",
    // "restoring", "failed", "unavailable").
    Q_PROPERTY(bool purchaseVisible READ purchaseVisible NOTIFY purchaseChanged)
    Q_PROPERTY(QString purchaseStatus READ purchaseStatus NOTIFY purchaseChanged)
    Q_PROPERTY(QString purchasePrice READ purchasePrice NOTIFY purchaseChanged)
    Q_PROPERTY(QString purchaseMessage READ purchaseMessage NOTIFY purchaseChanged)
    Q_PROPERTY(bool purchaseCanBuy READ purchaseCanBuy NOTIFY purchaseChanged)
    // ---- Measure (docs/MEASURE.md): the Measure workspace's state.
    Q_PROPERTY(os::ui::MeasureController* measure READ measure CONSTANT)

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    interact::InteractionController& interaction() { return *interaction_; }
    doc::Document& document() { return *document_; }
    cmd::UndoStack& undoStack() { return *undoStack_; }

    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool hasSelection() const;
    QString selectionSummary() const;
    QVariantList contextActions() const { return contextActionsList_; }
    bool operationActive() const;
    bool operationIsAngle() const;
    QString operationTitle() const;
    QString operationValueLabel() const;
    QString operationValueText() const;
    QString operationError() const;
    QString typedValueError() const;
    bool typingPending() const;
    bool operationCanCommit() const;
    bool operationHasValue() const;
    QString operationPrompt() const;
    QString operationMode() const;
    bool operationOnBody() const;
    bool operationModeChosen() const;
    bool operationTakesText() const;
    QString operationText() const;
    bool operationTextTyped() const;
    QPointF valueLabelPosition() const;
    bool valueLabelVisible() const;
    QVariant keepClearRect() const;
    bool manipulatorDragging() const;
    QVariantList safeInsets() const;
    void setSafeInsets(const QVariantList& insets);
    QVariantList frameInsets() const;
    void setFrameInsets(const QVariantList& insets);
    QVariantMap viewCube() const;
    // The view cube, at box coordinates (u, v) in [-1, 1] (u right, v down):
    // what is there (interact::ViewCubeTarget::id, -1 off the cube), a tap
    // there (a face's standard view or a corner's isometric view; false off
    // the cube) and a drag's orbit, whatever tool is armed.
    Q_INVOKABLE int viewCubeTarget(double u, double v) const;
    Q_INVOKABLE bool tapViewCube(double u, double v);
    Q_INVOKABLE void orbitView(double dxPixels, double dyPixels);
    QVariantList sketchObstacles() const;
    void setSketchObstacles(const QVariantList& rects);
    QVariantMap sketchLoupe() const;
    bool touchMode() const;
    void setTouchMode(bool on);
    bool penMode() const;
    void setPenMode(bool on);
    QString documentTitle() const;
    bool dirty() const;
    int bodyCount() const;
    QString displayUnit() const;
    bool perspective() const;
    bool actualSize() const { return interaction_->actualSize(); }
    bool screenScaleCalibrated() const { return screenScaleCalibrated_; }
    double screenScale() const { return interaction_->screenScale(); }
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
    QVariantList homeSamples() const { return homeSamples_; }
    // The samples are built into the app (:/openshape/samples, from
    // resources/samples) and copied into `folder` here, once per version of
    // each, for their thumbnails and opening. The folder is the app's own
    // data, not its Documents folder: samples are not the user's projects,
    // the Files app does not list them and they never count toward the free
    // app's one project.
    void setSamplesFolder(const QString& folder);
    QString samplesFolder() const { return samplesFolder_; }
    // A sample opens as an unsaved copy named after it (nothing is written
    // to the copy in the samples folder): Save asks for a name, and keeping
    // it is one more project, like any other. It opens in the background
    // (loading); false if there are no samples.
    Q_INVOKABLE bool openSample(const QString& name);
    bool loading() const { return loading_; }
    QString loadingLabel() const { return loadingLabel_; }
    QString loadingName() const { return loadingName_; }
    QString loadingThumbnail() const { return loadingThumbnail_; }
    // The acceptance run's slow open: the worker waits this long first.
    void setSimulatedLoadDelay(int ms) { simulatedLoadDelayMs_ = ms; }
    // The name Save offers: the project's own, a sample's, else none.
    Q_INVOKABLE QString saveNameSuggestion() const;
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

    // ---- Home: a project card's Rename..., Duplicate and Delete... (io/ProjectFiles)
    // Why the project at `path` cannot be renamed to `name` ("" when it can,
    // and for its current name): io::checkProjectName's rules, or a project
    // of that name in the same folder. Home shows it as the name is typed.
    Q_INVOKABLE QString projectNameProblem(const QString& path, const QString& name) const;
    // Renames the project file in its folder; the recent list keeps its
    // place, and the open project (if it is this one) its new path and
    // title. False, with a message, if it cannot.
    Q_INVOKABLE bool renameProject(const QString& path, const QString& newName);
    // "Name copy" (then "Name copy 2", ...) next to it, preview included;
    // listed first on Home. False, with a message, if it cannot.
    Q_INVOKABLE bool duplicateProject(const QString& path);
    // Whether `path` is the open project's file.
    Q_INVOKABLE bool isOpenProject(const QString& path) const;
    // Delete... after Home's confirmation. Offered (deletable) for projects
    // in OpenShape's own folder and, on the desktop, for recent projects
    // anywhere. First the project goes to the system's trash (trashName);
    // where that fails, a project in OpenShape's folder is removed only when
    // asked again with `permanently` (Home says it cannot be undone), and
    // one elsewhere stays. Returns "trashed", "deleted", "permanent" (the
    // trash failed: ask before deleting for good) or "" (nothing deleted; a
    // message says why). The list drops it; if it was the open project, a
    // new empty document takes its place and Home stays.
    Q_INVOKABLE QString deleteProject(const QString& path, bool permanently = false);
    QString trashName() const;
    // Moves a file to the trash (true, with where it went in `trashedAt`),
    // or says why not in `error`. A success with an empty `trashedAt` means
    // the system removed the file without reporting a place in the trash:
    // deleteProject then says it was deleted for good, never that it can be
    // restored. QFile::moveToTrash by default (Qt refuses on a drive without
    // a Recycle Bin rather than deleting); the acceptance run puts in one
    // that keeps to its temporary folder, one that fails and one that
    // removes without a trash.
    using TrashHandler = std::function<bool(const QString& file, QString* trashedAt, QString* error)>;
    void setTrashHandler(TrashHandler handler);
    static TrashHandler systemTrashHandler();

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
    // False on the iPhone Duo (one of the screens is the Duo's,
    // isIphoneDuoScreen), where Apple's file picker shows no files: Open...
    // and Import STEP... explain sharing the file from the Files app instead
    // (ShareFromFilesOverlay; TD-158). simulateIphoneDuo makes the desktop
    // answer as the Duo does (the acceptance run).
    Q_INVOKABLE bool filePickerWorks() const;
    Q_INVOKABLE void simulateIphoneDuo(bool on) { simulatedDuo_ = on; }
    // Switches to the Files app (iOS); elsewhere only logged.
    Q_INVOKABLE void openFilesApp();
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
    // ("stl", "3mf" or "step"), replacing an earlier export of that name,
    // then opens the share sheet with the file (when there is one), its
    // popover pointing at `shareAnchor` (window coordinates: the File button).
    Q_INVOKABLE bool exportToAppFolder(const QString& format, const QRectF& shareAnchor = QRectF());
    // Where to save without dialogs: the Documents folder on iOS and Android
    // (set at start), "" for file dialogs; tests and --app-folder set it.
    void setAppFolder(const QString& folder);

    // ---- About -> Licenses (ui/Licenses, resources/licenses; docs/LICENSING.md)
    QString sourceCodeUrl() const;
    QString buildDescription() const;
    // What the Licenses view lists, in order: {id, name, group, version,
    // license, usedFor}; first the LGPL notice and source offer (id
    // "source-offer", group "offer"), then OpenShape ("app"), the libraries
    // ("library") and the third-party code inside Qt ("qt"). Empty if the
    // license texts cannot be read (logged).
    Q_INVOKABLE QVariantList licenseEntries();
    // The page for one of them: the full license texts with copyright,
    // notices and where the source is; "" for an unknown id.
    Q_INVOKABLE QString licenseText(const QString& id);
    // ---- Sharing (iPhone / iPad; see ui/Share.h)
    bool canShare() const { return bool(shareHandler_); }
    // The share sheet to use: platformShareHandler() on iOS (set at start),
    // none elsewhere; the acceptance run puts a stub in (nullptr: none).
    void setShareHandler(ShareHandler handler);
    // Opens the share sheet with `file` (an existing file), pointing at
    // `anchor`. Its end is logged (sent / closed); only a failure is said
    // in a message. False, with a message, when the file is not there;
    // false when there is no share sheet.
    Q_INVOKABLE bool shareFile(const QString& file, const QRectF& anchor = QRectF());
    // File -> Share Project...: shares the project's file. The window saves
    // first (asking for a name for a new project), so this refuses (false)
    // a project without a file or with unsaved changes.
    Q_INVOKABLE bool shareProject(const QRectF& anchor = QRectF());

    // ---- Unlimited projects (the App Store purchase; ui/Store.h, ui/Purchase.h)
    // The free App Store app keeps one project in OpenShape's folder; adding
    // another (New, Import STEP as a project, Duplicate, saving under a new
    // name, a project or STEP file from another app) opens the purchase
    // sheet instead, and goes ahead once unlimited projects are bought or
    // restored. Opening, editing, saving, renaming and deleting are never
    // limited.
    bool storeAvailable() const { return bool(store_); }
    bool projectsUnlocked() const { return purchase_.unlocked(); }
    bool projectLimited() const { return store_ && !purchase_.unlocked(); }
    bool purchaseVisible() const { return purchaseVisible_; }
    QString purchaseStatus() const { return purchaseStatusName(purchase_.status()); }
    QString purchasePrice() const { return purchase_.price(); }
    QString purchaseMessage() const { return purchase_.message(); }
    bool purchaseCanBuy() const { return store_ && purchase_.canBuy(); }
    // The store: platformStore() on iOS (set at start), none elsewhere; the
    // acceptance run and the App Store review screenshot put a stand-in in
    // (nullptr: none). Asks it for the price unless unlocked.
    void setStore(std::shared_ptr<Store> store);
    // Forgets or sets the unlock (stored on the device): for the acceptance run.
    void setProjectsUnlocked(bool unlocked);
    // Whether a project may be added now (`replacing`: the project file that
    // is written over, which does not count against itself).
    Q_INVOKABLE bool mayAddProject(const QString& replacing = QString()) const;
    // Before an action that adds a project ("new", "import"; "save" with the
    // typed name, whose own file does not count): true when it may go ahead.
    // Otherwise the purchase sheet opens, and once unlocked projectAllowed()
    // asks the window to do it after all.
    Q_INVOKABLE bool gateProject(const QString& action, const QVariant& argument = QVariant());
    // Opens the purchase sheet with nothing waiting (Home's line, About).
    Q_INVOKABLE void offerUnlock();
    // The sheet's buttons: Unlock for <price>, Restore purchase (also in
    // About; it opens the sheet to show how it goes), Not now (what waited
    // for it is dropped).
    Q_INVOKABLE void buyUnlock();
    Q_INVOKABLE void restorePurchase();
    Q_INVOKABLE void closePurchase();

    // ---- Measure (docs/MEASURE.md; ui/MeasureController)
    // The workspace's state: the scale (which it passes to setScreenScale,
    // the 1:1 view), the sensors, the photo, the kept measurements. It makes
    // geometry with createOutlineSketch, createCircleSketch and createSizedBox.
    MeasureController* measure() const { return measure_; }

    // ---- Files from other apps ("Open in OpenShape"; ui/IncomingFiles.h)
    // A file another app handed to OpenShape: on iOS, "Open in OpenShape" or
    // sharing to OpenShape from the Files app or Mail, or a project tapped
    // in the Files app (Qt turns UIKit's openURLContexts into a
    // QFileOpenEvent, which eventFilter() passes here); on a Mac, Finder's
    // Open With. A project is brought into the app folder (unless it is
    // there) and opened; a STEP file is imported as a new project, as
    // Home's Open and Import STEP do. With unsaved changes the file waits
    // (incomingFileWaiting: the window asks "Save changes?" and then calls
    // openPendingIncomingFile). False, with a message, when the file cannot
    // be used.
    Q_INVOKABLE bool openIncomingFile(const QUrl& url);
    // Opens or imports the file openIncomingFile() kept (false if none).
    Q_INVOKABLE bool openPendingIncomingFile();
    // "Save changes?" was cancelled: the waiting file is dropped, and the
    // copy made for it in OpenShape's folder (or its scratch copy) removed.
    Q_INVOKABLE void dropPendingIncomingFile();
    // Whether a file from another app waits for "Save changes?".
    bool hasPendingIncomingFile() const { return pendingIncoming_.has_value(); }
    // Qt delivers files from other apps to the application object.
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    // Where the value chip goes (interact::placeValueChip). `layout`: area
    // (rect), avoid (rects), size, tip (point), fieldCenter, keepClear (rect
    // or null), compact, touch, frozen, typing. Returns {x, y, spot, clear}. The spot
    // is remembered for the selection (the chip does not jump).
    Q_INVOKABLE QVariantMap placeValueChip(const QVariantMap& layout) const;
    // Returns an error message ("" on success, or while the value's preview
    // still computes: its verdict comes with a state change). Previews live
    // as the user types.
    Q_INVOKABLE QString setValueText(const QString& text);
    // The same, waiting for the verdict of a preview still computing: Tab to
    // the next field moves on only with a usable value.
    Q_INVOKABLE QString confirmValueText(const QString& text);
    // A key typed into the value chip's field (the whole text so far): it is
    // previewed once typing pauses (0.7 s) or when it is confirmed (Enter,
    // Tab, leaving the field, a tap elsewhere), so the model does not jump
    // through 1, 10, 100 while "100" is typed. Its verdict comes with a
    // state change (typedValueError, operationError).
    Q_INVOKABLE void typeValueText(const QString& text);
    // The same for the sketch's focused live value (a shape being drawn).
    Q_INVOKABLE void typeSketchValue(const QString& text);
    // Takes the value typed now (leaving the field); returns why it was refused, or "".
    Q_INVOKABLE QString flushTyping();
    // The numeric keypad (touch; interact::NumericKeypad): its keys, row by
    // row ({id, label, span, accent}) for a mode ("length", "angle",
    // "count"), and what a key tapped does to the text shown: {text,
    // replacing, action: "none" | "edited" | "next" | "done"}. keypadType is a
    // hardware keyboard's characters while the keypad edits.
    Q_INVOKABLE QVariantList keypadRows(const QString& mode, bool hasNext, bool wide = false) const;
    Q_INVOKABLE QVariantMap keypadPress(const QString& text, bool replacing, const QString& key, const QString& mode) const;
    Q_INVOKABLE QVariantMap keypadType(const QString& text, bool replacing, const QString& characters) const;
    // Where the keypad goes (interact::placeKeypad): `layout` has area,
    // size, target (the value box), avoid (controls), keepClear and compact;
    // returns {x, y, docked, clear}.
    Q_INVOKABLE QVariantMap placeKeypad(const QVariantMap& layout) const;
    // A phone's keypad came up: the view moves so the selection and its
    // arrow lie in `region`, the part of the window left free
    // (InteractionController::revealKeepClear). True when it moved.
    Q_INVOKABLE bool revealKeepClear(const QRectF& region);
    // The largest part of `area` clear of `avoid` (rectangles, each grown by
    // `gap`): interact::largestFreeRect.
    Q_INVOKABLE QRectF largestFreeRect(const QRectF& area, const QVariantList& avoid, double gap) const;
    // While sketching: the sketch on screen (a keypad beside a value keeps
    // clear of it; InteractionController::sketchScreenRect), or undefined.
    Q_INVOKABLE QVariant sketchScreenRect() const;
    // The Text tool's words (previewed at once); returns the error, or "".
    Q_INVOKABLE QString setOperationText(const QString& text);
    Q_INVOKABLE void triggerAction(const QString& id);
    Q_INVOKABLE void setView(const QString& name);
    Q_INVOKABLE void fitAll();
    // The View buttons' projection toggle; the choice is remembered for the
    // next start (QSettings).
    Q_INVOKABLE void togglePerspective();
    Q_INVOKABLE void setPerspective(bool on);
    // 1:1: logical px (points on iOS) per millimeter on the window's screen,
    // and whether a card calibrated it (the Measure workspace calls this
    // whenever the window's screen or its calibration changes). Until then
    // an estimate for the primary screen (measure::estimateScale).
    Q_INVOKABLE void setScreenScale(double pointsPerMm, bool calibrated);
    // The View buttons' 1:1: on (orthographic at true scale) or off (the
    // view from before). Any zoom turns it off.
    Q_INVOKABLE void toggleActualSize();

    // ---- Geometry from measurements (the Measure workspace; one undo step each)
    // A new sketch on the ground plane with the points (mm; QPointF or
    // {x, y}) joined by lines (closed: back to the first), opened facing and
    // framing it. False, with a message, for fewer than two points or a
    // closed outline with no area.
    Q_INVOKABLE bool createOutlineSketch(const QVariantList& pointsMm, bool closed);
    // The same with a circle, its diameter dimension set.
    Q_INVOKABLE bool createCircleSketch(double centerXmm, double centerYmm, double diameterMm);
    // A box body (X, Y, Z sizes in mm), selected and framed.
    Q_INVOKABLE bool createSizedBox(double widthMm, double depthMm, double heightMm);
    Q_INVOKABLE void setDisplayUnit(const QString& symbol);
    Q_INVOKABLE bool handleKey(int key);

    // Sketching
    // plane: "top" (default), "front" or "right"; ignored when a face is selected.
    Q_INVOKABLE void startSketch(const QString& plane = QString());
    Q_INVOKABLE bool faceSelected() const;
    Q_INVOKABLE void finishSketch();
    // "Face on": the view faces the sketch's plane again (after an orbit).
    Q_INVOKABLE void faceSketch();
    Q_INVOKABLE void setSketchTool(const QString& name);
    // Replaces the focused input's text while drawing, at once; returns an
    // error or "". (Keys use typeSketchValue.)
    Q_INVOKABLE QString sketchType(const QString& text);
    Q_INVOKABLE void focusNextSketchInput();
    // A tap on a live value while drawing: it takes the keys (the keypad's) next.
    Q_INVOKABLE bool focusSketchInput(const QString& key);
    Q_INVOKABLE void commitSketchTool();
    // -/+ on the sketch counter (a polygon's sides, a pattern's copies).
    Q_INVOKABLE void stepSketchCounter(int delta);
    // A dimension's new value; for a size label (a line, circle or arc id)
    // the dimension it adds (SketchSession::setDimension).
    Q_INVOKABLE QString setSketchDimension(int constraintId, const QString& text);
    // The dimension editor's check mark on the text it opened with: the
    // value shown, kept exactly (SketchSession::keepDimension).
    Q_INVOKABLE QString keepSketchDimension(int constraintId);
    // A press in the view that takes the focus from the sketch's dimension
    // editor only closes the editor (SketchOverlay.qml calls this as the
    // editor loses the focus; ViewportItem asks right after taking it): the
    // press, its moves and its release do not also draw, trim, select or
    // move what is under the finger, as in Shapr3D and OpenShape3D.
    Q_INVOKABLE void swallowViewPress() { swallowViewPress_ = true; }
    bool takeSwallowedViewPress()
    {
        const bool swallow = swallowViewPress_;
        swallowViewPress_ = false;
        return swallow;
    }

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
    // Selects a construction axis or plane from its row (its actions appear).
    Q_INVOKABLE void selectDatum(const QString& datumId);
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
    void samplesChanged();
    void loadingChanged();
    void homeChanged();
    void preferencesChanged();
    void appFolderChanged();
    void shareChanged();
    // A file from another app waits for "Save changes?" (openIncomingFile).
    void incomingFileWaiting();
    void purchaseChanged();
    // Unlimited projects were unlocked while `action` waited for it (gateProject).
    void projectAllowed(const QString& action, const QVariant& argument);

private:
    bool swallowViewPress_ = false; // swallowViewPress
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
    // Home's Delete... is offered for the project at `path` (see deleteProject).
    bool projectDeletable(const QString& path, bool recent) const;
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
    // Where files from outside go (ui/IncomingFiles.h): the app folder, a
    // scratch folder for STEP copies and the system's Inbox.
    IncomingPlaces incomingPlaces() const;
    // Import STEP (File menu, Home): with an app folder the file is read
    // through a scratch copy when it is outside the folder (the picker's
    // files lie outside the sandbox on iOS).
    bool importStepFrom(const QUrl& url, bool asProject);
    // Reads `path` (named `sourceName` in the step) into this document or a
    // new one, in the background; `then` gets whether it was imported.
    void importStepFile(const QString& path, const QString& sourceName, bool asProject, std::function<void(bool)> then = {});
    // Opens a project file as it is (no staging), in the background; `then`
    // gets whether it opened.
    void openProjectFile(const QString& path, std::function<void(bool)> then = {});
    // openProject, with `then` for a project that cannot be opened (false)
    // or has opened (true); not called while the purchase sheet asks first.
    bool openProjectThen(const QUrl& url, std::function<void(bool)> then);
    // A project read and rebuilt on the worker thread, with its display meshes.
    struct LoadedProject;
    // Swaps in a project read in the background (the second half of an open).
    bool adoptProject(LoadedProject& loaded, const QString& path);
    // The second half of restoreRecovery.
    void adoptRecovery(LoadedProject& loaded, const io::RecoveryEntry& entry);
    // Runs `work` on a worker thread, then `done` on this one (where an open
    // swaps the document in). One at a time: another waits for it. Meanwhile
    // `loading` is true with the label, name and preview the card shows.
    void runInBackground(const QString& label, const QString& name, const QString& thumbnail, std::function<void()> work,
                         std::function<void()> done);
    void finishBackground();
    struct BackgroundJob {
        QString label, name, thumbnail;
        std::function<void()> work, done;
    };
    std::deque<BackgroundJob> waitingJobs_;
    std::unique_ptr<QThread> backgroundThread_;
    std::function<void()> backgroundDone_;
    QElapsedTimer loadingTime_;
    bool loading_ = false;
    QString loadingLabel_, loadingName_, loadingThumbnail_;
    int simulatedLoadDelayMs_ = 0;
    // Removes what stageIncomingFile made for a file that is not used after
    // all: a new copy in the app folder, or a scratch copy (never the open
    // project's file).
    void discardStagedFile(const StagedFile& staged) const;
    // The store's events (on the GUI thread).
    void storeEvent(const StoreEvent& event);
    // Opens the purchase sheet (asking for the price when there is none).
    void showPurchase();
    // gateProject for the actions the controller runs itself: `then` runs
    // once unlocked. Returns whether the action may go ahead now.
    bool gateProjectThen(const QString& action, const QString& replacing, std::function<void()> then);
    // The file Save in the app folder writes for `name` ("" for no name).
    QString appFolderProjectPath(const QString& name) const;

    std::unique_ptr<doc::Document> document_;
    std::unique_ptr<cmd::UndoStack> undoStack_;
    std::unique_ptr<interact::InteractionController> interaction_;
    QString path_;
    Preferences preferences_;
    std::unique_ptr<RecoverySession> recovery_;
    std::vector<io::RecoveryEntry> orphans_; // offered for restoring
    std::optional<LicenseCatalog> licenses_; // read at the first use (About -> Licenses)
    QTimer recoveryDebounce_; // edits settled
    QTimer recoveryDeadline_; // at least this often while editing
    QTimer typingPause_;      // a value typed key by key: its pause is over
    // (Re)starts typingPause_ for the value typed (InteractionController::typingDeadline).
    void armTypingPause();
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
    QVariantList homeSamples_;
    QString samplesFolder_;
    QString untitledName_; // an unsaved document's name (an open sample's), else "Untitled"
    bool homeVisible_ = false;
    QUrl nextFileChoice_;
    bool simulatedDuo_ = false;
    bool screenScaleCalibrated_ = false; // setScreenScale
    // The primary screen's scale estimate (before the Measure workspace sets one).
    void estimateScreenScale();
    QString appFolder_; // see savesToAppFolder
    ShareHandler shareHandler_;
    std::shared_ptr<Store> store_;
    PurchaseFlow purchase_;
    bool purchaseVisible_ = false;
    // What waits for the purchase: an action of the window's (projectAllowed)
    // or the controller's own (`run`).
    struct PendingProject {
        QString action;
        QVariant argument;
        std::function<void()> run;
    };
    std::optional<PendingProject> pendingProject_;
    TrashHandler trashHandler_;
    MeasureController* measure_ = nullptr; // a child (the Measure workspace)
    // A file from another app, brought in and waiting for "Save changes?".
    std::optional<StagedFile> pendingIncoming_;
    QString pendingIncomingName_; // its name as it came (the STEP step's source)
};

} // namespace os::ui
