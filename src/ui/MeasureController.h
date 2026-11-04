// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/Fold.h"
#include "measure/Level.h"
#include "measure/Measurement.h"
#include "measure/ScanTypes.h"
#include "measure/ScreenScale.h"
#include "ui/MeasureDevices.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QObject>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtCore/QSizeF>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <QtQuick/QQuickWindow>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QScreen;
class QThread;

namespace os::ui {

class AppController;

// The Measure workspace's state for QML (app.measure; docs/MEASURE.md):
// the screen's true scale and its card calibration, the sensors (the
// iPhone Duo's hinge, gravity; started only while the workspace is open),
// a photo straightened onto a reference rectangle, the camera's AR
// results, the kept measurements, and the geometry made from them (through
// AppController's design hooks). The hardware comes as MeasureDevices
// (empty functions where there is none); the acceptance run puts stand-ins
// in (setDevices) and drives them.
class MeasureController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by AppController (app.measure)")

    // ---- The screen's scale
    Q_PROPERTY(double pointsPerMm READ pointsPerMm NOTIFY scaleChanged)
    Q_PROPERTY(bool scaleCalibrated READ scaleCalibrated NOTIFY scaleChanged)
    // Where the scale comes from, in words ("iPhone, 3x display (estimate)",
    // "checked with a card").
    Q_PROPERTY(QString scaleNote READ scaleNote NOTIFY scaleChanged)
    Q_PROPERTY(QString screenKey READ screenKey NOTIFY scaleChanged)
    // "phone", "tablet" or "desktop".
    Q_PROPERTY(QString deviceKind READ deviceKind NOTIFY scaleChanged)
    // The window is the whole of a folding phone's inner display (the
    // iPhone Duo unfolded, not in Split View): the fold's crease runs across
    // the middle of its long side, and the Ruler marks it there.
    Q_PROPERTY(bool foldCrease READ foldCrease NOTIFY scaleChanged)
    // The ruler's and the labels' unit: "in" when the display unit is
    // inches, else "mm".
    Q_PROPERTY(QString unit READ unit NOTIFY unitChanged)
    // The card of known size the scale check, the caliper and the photo's
    // "Card" use: a poker-size playing card, 63.5 x 88.9 mm (kPlayingCard).
    Q_PROPERTY(QString cardName READ cardName CONSTANT)          // "Playing card"
    Q_PROPERTY(double cardLongMm READ cardLongMm CONSTANT)
    Q_PROPERTY(double cardShortMm READ cardShortMm CONSTANT)
    Q_PROPERTY(double cardCornerMm READ cardCornerMm CONSTANT)
    Q_PROPERTY(QString cardSizeText READ cardSizeText CONSTANT)  // "63.5 × 88.9 mm"

    // ---- Sensors (while startSensors() .. stopSensors())
    Q_PROPERTY(bool sensorsRunning READ sensorsRunning NOTIFY sensorsChanged)
    Q_PROPERTY(bool hingeAvailable READ hingeAvailable NOTIFY sensorsChanged)
    Q_PROPERTY(bool gravityAvailable READ gravityAvailable NOTIFY sensorsChanged)
    Q_PROPERTY(double foldAngle READ foldAngle NOTIFY foldChanged)
    Q_PROPERTY(double foldBend READ foldBend NOTIFY foldChanged)
    Q_PROPERTY(bool foldSteady READ foldSteady NOTIFY foldChanged)
    Q_PROPERTY(double foldHeld READ foldHeld NOTIFY foldChanged) // NaN when none
    Q_PROPERTY(bool foldHasHold READ foldHasHold NOTIFY foldChanged)
    // How far the next hold has come (0 moving .. 1 held), and whether the
    // next steady stretch makes one (none yet, or moved away since).
    Q_PROPERTY(double foldProgress READ foldProgress NOTIFY foldChanged)
    Q_PROPERTY(bool foldArmed READ foldArmed NOTIFY foldChanged)
    // The caliper: the gap between the free edges (mm), 2 L sin(angle / 2).
    Q_PROPERTY(double caliperArmMm READ caliperArmMm NOTIFY foldChanged)
    Q_PROPERTY(bool caliperCalibrated READ caliperCalibrated NOTIFY foldChanged)
    Q_PROPERTY(double caliperGapMm READ caliperGapMm NOTIFY foldChanged)
    Q_PROPERTY(double caliperHeldGapMm READ caliperHeldGapMm NOTIFY foldChanged) // NaN when no hold
    Q_PROPERTY(double caliperMmPerDegree READ caliperMmPerDegree NOTIFY foldChanged)
    Q_PROPERTY(bool levelLying READ levelLying NOTIFY levelChanged)
    Q_PROPERTY(double levelSlope READ levelSlope NOTIFY levelChanged)
    Q_PROPERTY(double levelEdge READ levelEdge NOTIFY levelChanged)
    Q_PROPERTY(double bubbleX READ bubbleX NOTIFY levelChanged)
    Q_PROPERTY(double bubbleY READ bubbleY NOTIFY levelChanged)
    // What the level shows now (the angle to the kept reference, else the
    // slope lying or the edge's angle standing), smoothed, and its hold.
    Q_PROPERTY(double levelValue READ levelValue NOTIFY levelChanged)
    Q_PROPERTY(bool levelSteady READ levelSteady NOTIFY levelChanged)
    Q_PROPERTY(double levelHeld READ levelHeld NOTIFY levelChanged) // NaN when none
    Q_PROPERTY(bool levelHasHold READ levelHasHold NOTIFY levelChanged)
    Q_PROPERTY(double levelProgress READ levelProgress NOTIFY levelChanged)
    Q_PROPERTY(bool levelArmed READ levelArmed NOTIFY levelChanged)
    Q_PROPERTY(bool hasLevelReference READ hasLevelReference NOTIFY levelChanged)
    Q_PROPERTY(double relativeAngle READ relativeAngle NOTIFY levelChanged)
    // Calibrating and checking the level, end for end (docs/MEASURE.md):
    // "" (none), "calibrate" or "check"; the step (1 the first reading, 2
    // turned, 3 the result); the result in words; the offset found (degrees).
    Q_PROPERTY(QString levelCalMode READ levelCalMode NOTIFY levelChanged)
    Q_PROPERTY(int levelCalStep READ levelCalStep NOTIFY levelChanged)
    Q_PROPERTY(QString levelCalResult READ levelCalResult NOTIFY levelChanged)
    Q_PROPERTY(double levelCalFound READ levelCalFound NOTIFY levelChanged)
    // The posture shown (lying or standing) has a kept offset, and its size.
    Q_PROPERTY(bool levelCalibrated READ levelCalibrated NOTIFY levelChanged)
    Q_PROPERTY(double levelOffset READ levelOffset NOTIFY levelChanged)

    // ---- Photo
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY sensorsChanged)
    // The system's photo picker is there (iOS); elsewhere a file dialog (loadPhoto).
    Q_PROPERTY(bool canPickPhoto READ canPickPhoto NOTIFY sensorsChanged)
    Q_PROPERTY(bool photoBusy READ photoBusy NOTIFY photoChanged)
    Q_PROPERTY(QUrl photoUrl READ photoUrl NOTIFY photoChanged)
    Q_PROPERTY(QSizeF photoSize READ photoSize NOTIFY photoChanged)
    Q_PROPERTY(QVariantList corners READ corners NOTIFY photoChanged) // 4 QPointF, image px
    Q_PROPERTY(bool cornersDetected READ cornersDetected NOTIFY photoChanged)
    Q_PROPERTY(QString referenceKind READ referenceKind NOTIFY photoChanged) // card, a4, letter, rectangle
    Q_PROPERTY(double referenceSideA READ referenceSideA NOTIFY photoChanged)
    Q_PROPERTY(double referenceSideB READ referenceSideB NOTIFY photoChanged)
    Q_PROPERTY(QString referenceName READ referenceName NOTIFY photoChanged)
    Q_PROPERTY(QUrl straightUrl READ straightUrl NOTIFY straightChanged)
    Q_PROPERTY(QSizeF straightSize READ straightSize NOTIFY straightChanged)
    Q_PROPERTY(double straightMmPerPixel READ straightMmPerPixel NOTIFY straightChanged)
    Q_PROPERTY(QPointF straightOriginMm READ straightOriginMm NOTIFY straightChanged)
    // The reference's corners in the straightened view (px), in order.
    Q_PROPERTY(QVariantList straightReference READ straightReference NOTIFY straightChanged)
    Q_PROPERTY(QString photoError READ photoError NOTIFY photoChanged)
    // Find outline (docs/MEASURE.md): the last result, {valid, message,
    // points [px], through [bool: an arc's middle], holes [{points,
    // through}], maxError (mm), tolerance (mm)}; empty after Back or a new photo.
    Q_PROPERTY(QVariantMap foundOutline READ foundOutline NOTIFY foundOutlineChanged)
    // A set of views (docs/MEASURE.md, "Views": a top, a front and a side
    // photo of one part): each a whole photo session (the photo, its
    // corners and reference, the straightened view) under a name, on a
    // plane ("XY", "XZ", "YZ"), with what Photo drew on it (`state`, kept
    // as QML gives it: its dimensions and its origin point). For the app
    // session only (TD-177). [{name, plane, straightUrl, width, height,
    // mmPerPixel, state}]
    Q_PROPERTY(QVariantList photoViews READ photoViews NOTIFY photoViewsChanged)
    // Which of them the photo being measured is (-1: not in the set yet).
    Q_PROPERTY(int photoViewIndex READ photoViewIndex NOTIFY photoViewsChanged)

    // ---- Camera (AR)
    Q_PROPERTY(bool arAvailable READ arAvailable NOTIFY sensorsChanged)
    Q_PROPERTY(bool lidar READ lidar NOTIFY sensorsChanged)
    Q_PROPERTY(bool arBusy READ arBusy NOTIFY arChanged)
    // The last result: {outcome ("done", "cancelled", "failed"), mode
    // ("distance", "outline", "box"), error, distances [mm], total, outline
    // [{x, y} mm], sides [mm], perimeter, area, width, depth, height}.
    Q_PROPERTY(QVariantMap arResult READ arResult NOTIFY arChanged)

    // ---- Scan (docs/SCAN.md): photos around an object standing on a sheet
    // of paper become a mesh and a body.
    // {url, width, height, corners [4 QPointF, image px], found (the paper's
    // corners were found: by Vision or detectPaper; else placed for the
    // owner to fix), source ("vision", "detected", "hand", ""), mask (a
    // subject mask is there), used / problem (after a build)}.
    Q_PROPERTY(QVariantList scanPhotos READ scanPhotos NOTIFY scanChanged)
    Q_PROPERTY(QString scanPaper READ scanPaper NOTIFY scanChanged) // "a4", "letter" or "legal"
    Q_PROPERTY(bool scanBusy READ scanBusy NOTIFY scanChanged)      // photos being read
    Q_PROPERTY(bool scanBuilding READ scanBuilding NOTIFY scanChanged)
    Q_PROPERTY(double scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(QString scanStage READ scanStage NOTIFY scanProgressChanged)
    // After a build: {valid, width, depth, height, volume (mm³), triangles,
    // warnings [text], used, photos, focal35mm, focalEstimated, ms}.
    Q_PROPERTY(QVariantMap scanResult READ scanResult NOTIFY scanChanged)
    Q_PROPERTY(QString scanError READ scanError NOTIFY scanChanged)
    // Detail from the photos: multi-view stereo (measure::refineWithStereo)
    // refines the carved shape, a few seconds more. On by default.
    Q_PROPERTY(bool scanStereoAvailable READ scanStereoAvailable CONSTANT)
    Q_PROPERTY(bool scanDetailFromStereo READ scanDetailFromStereo WRITE setScanDetailFromStereo NOTIFY scanChanged)

    // ---- Kept measurements: {id, name, quantity ("length", "angle",
    // "area"), value, text (to read), valueText (to type), source}, newest first.
    Q_PROPERTY(QVariantList measurements READ measurements NOTIFY measurementsChanged)

public:
    explicit MeasureController(AppController* app);
    ~MeasureController() override;

    // The window the hardware is asked about (the hinge of the device it is
    // on, the picker shown over it); its screen gives the scale. MeasureScreen
    // attaches its window; until then the first Qt Quick window is used.
    Q_INVOKABLE void attachWindow(QQuickWindow* window);

    // ---- The screen's scale
    double pointsPerMm() const { return pointsPerMm_; }
    bool scaleCalibrated() const { return scaleCalibrated_; }
    QString scaleNote() const { return scaleNote_; }
    QString screenKey() const { return screenKey_; }
    QString deviceKind() const { return deviceKind_; }
    bool foldCrease() const { return foldCrease_; }
    QString unit() const;
    QString cardName() const;
    double cardLongMm() const { return card().longMm; }
    double cardShortMm() const { return card().shortMm; }
    double cardCornerMm() const { return card().cornerRadiusMm; }
    QString cardSizeText() const;
    const measure::CardSize& card() const;
    // A span of `spanPt` points on the screen matched `mm` of a real thing
    // (Check with a card: the card's long side). Kept for this screen;
    // false (and nothing changes) when the scale would be implausible.
    Q_INVOKABLE bool calibrate(double spanPt, double mm);
    Q_INVOKABLE void resetCalibration();
    // Re-reads the screen (the window moved, resized, the Duo folded).
    Q_INVOKABLE void updateScale();

    // ---- Sensors
    Q_INVOKABLE void startSensors();
    Q_INVOKABLE void stopSensors();
    bool sensorsRunning() const { return sensorsRunning_; }
    bool hingeAvailable() const { return hingeAvailable_; }
    bool gravityAvailable() const { return gravityAvailable_; }
    double foldAngle() const { return foldAngle_; }
    double foldBend() const { return 180.0 - foldAngle_; }
    bool foldSteady() const { return foldSteady_; }
    double foldHeld() const;
    bool foldHasHold() const { return fold_.held().has_value(); }
    double foldProgress() const { return foldProgress_; }
    bool foldArmed() const { return foldArmed_; }
    double caliperArmMm() const { return caliperArmMm_; }
    bool caliperCalibrated() const { return caliperCalibrated_; }
    double caliperGapMm() const;
    double caliperHeldGapMm() const;
    double caliperMmPerDegree() const;
    // The arm length from a gap of known size pinched at the current angle
    // (a card's side); kept. False when the angle is too small to tell.
    Q_INVOKABLE bool calibrateCaliper(double knownGapMm);
    Q_INVOKABLE void resetCaliper();
    bool levelLying() const { return tilt_.lying; }
    double levelSlope() const { return tilt_.slopeDeg; }
    double levelEdge() const { return tilt_.edgeDeg; }
    double bubbleX() const { return tilt_.bubble.x; }
    double bubbleY() const { return tilt_.bubble.y; }
    double levelValue() const { return levelValue_; }
    bool levelSteady() const { return levelSteady_; }
    double levelHeld() const;
    bool levelHasHold() const { return level_.held().has_value(); }
    double levelProgress() const { return levelProgress_; }
    bool levelArmed() const { return levelArmed_; }
    bool hasLevelReference() const { return levelReference_.has_value(); }
    double relativeAngle() const;
    // Keeps the current resting position: the level then reads the angle
    // between it and the next one.
    Q_INVOKABLE void keepLevelReference();
    Q_INVOKABLE void clearLevelReference();
    // Drops the fold's and the level's holds (a new measurement starts).
    Q_INVOKABLE void clearHold();
    // The level's end-for-end calibration and check (MeasureLevelCalibration.cpp).
    QString levelCalMode() const { return levelCal_.mode; }
    int levelCalStep() const { return levelCal_.step; }
    QString levelCalResult() const { return levelCal_.result; }
    double levelCalFound() const { return levelCal_.found; }
    bool levelCalibrated() const;
    double levelOffset() const;
    Q_INVOKABLE void startLevelCalibration();
    Q_INVOKABLE void startLevelCheck();
    // Turned end for end: the second reading waits a whole steady second.
    Q_INVOKABLE void levelCalTurned();
    // Cancel, or Done once the result is shown.
    Q_INVOKABLE void endLevelCalibration();
    // Forgets the kept offset of the posture shown (lying or standing).
    Q_INVOKABLE void resetLevelCalibration();
    // Time passing while the sensors run (a timer, ten times a second): the
    // Duo's hinge reports only changes, so its last angle is fed again while
    // it keeps still, or a still fold would never be held. The acceptance
    // run calls it with its own clock.
    void tickSensors();

    // ---- Photo
    bool cameraAvailable() const;
    bool canPickPhoto() const { return bool(devices_.pickPhoto); }
    bool photoBusy() const { return photoBusy_; }
    QUrl photoUrl() const { return photoUrl_; }
    QSizeF photoSize() const { return photoSize_; }
    QVariantList corners() const;
    bool cornersDetected() const { return cornersDetected_; }
    QString referenceKind() const { return referenceKind_; }
    double referenceSideA() const { return referenceSideA_; }
    double referenceSideB() const { return referenceSideB_; }
    QString referenceName() const;
    QUrl straightUrl() const { return straightUrl_; }
    QSizeF straightSize() const { return straightSize_; }
    double straightMmPerPixel() const { return straightMmPerPixel_; }
    QPointF straightOriginMm() const { return straightOriginMm_; }
    QVariantList straightReference() const { return straightReference_; }
    QString photoError() const { return photoError_; }
    QVariantMap foundOutline() const { return foundOutline_; }
    // The camera / the photo library over the window (devices.pickPhoto);
    // `anchor`: the button that asked (an iPad's popover).
    Q_INVOKABLE void takePhoto(const QRectF& anchor = QRectF());
    Q_INVOKABLE void choosePhoto(const QRectF& anchor = QRectF());
    // An image file (the desktop's file dialog; tests).
    Q_INVOKABLE void loadPhoto(const QUrl& url);
    Q_INVOKABLE void setCorner(int index, double x, double y);
    // "card", "a4", "letter", or "rectangle" with its two sides (mm). False
    // for a rectangle whose sides are not positive.
    Q_INVOKABLE bool setReference(const QString& kind, double sideA = 0, double sideB = 0);
    // Straightens the photo on the reference (in the background): false
    // when the corners make no convex quadrilateral (photoError says so).
    Q_INVOKABLE bool straighten();
    // Back to the corners (the straightened view is dropped).
    Q_INVOKABLE void unstraighten();
    Q_INVOKABLE void clearPhoto();
    // Measuring on the straightened view (its pixel coordinates).
    Q_INVOKABLE double distanceMm(const QPointF& a, const QPointF& b) const;
    // {sides [mm], perimeter, area (closed), corners [degrees], width, height}.
    Q_INVOKABLE QVariantMap outlineReport(const QVariantList& points, bool closed) const;
    // {valid, center (px), radius (px), diameter (mm)}.
    Q_INVOKABLE QVariantMap circleFrom3(const QPointF& a, const QPointF& b, const QPointF& c) const;
    // The points in millimeters on the surface (x right, y down the view).
    Q_INVOKABLE QVariantList straightToMm(const QVariantList& points) const;
    // A copy of the straightened photo that outlives the session (the
    // session's own files are removed when it ends): for placing it in the
    // design (AppController::addReferenceImage). "" when there is none.
    Q_INVOKABLE QString keepStraightImage();
    // Find outline: the part lying on contrasting paper beside the card,
    // found in the straightened photo (in the background, photoBusy);
    // outlineFound() when done (foundOutline says what). A tap (px) picks
    // the part where there are several; (-1, -1) takes the largest.
    Q_INVOKABLE bool findOutline(double tapX = -1, double tapY = -1);
    // A found outline's points as the path to draw (arcs as short chords, px).
    Q_INVOKABLE QVariantList profilePath(const QVariantList& points, const QVariantList& through) const;
    // The found outline (its points perhaps dragged; holes from `found`):
    // {sides [mm], labelAt [px], perimeter, area (holes out), width, height
    // (along the part's longest side), holes [{circle, diameter, width,
    // height, center}], items [{label, quantity, value}] for the readout}.
    Q_INVOKABLE QVariantMap profileReport(const QVariantList& points, const QVariantMap& found) const;
    // ---- Photo views (the set)
    QVariantList photoViews() const;
    int photoViewIndex() const { return photoViewIndex_; }
    // Keeps the straightened photo being measured in the set (or updates
    // its entry): the index, -1 when nothing is straightened.
    Q_INVOKABLE int savePhotoView(const QString& name, const QString& plane, const QVariantMap& state);
    // Measures one of the set again (its photo, corners and straightened
    // view come back). A photo not in the set is dropped: save it first.
    Q_INVOKABLE bool openPhotoView(int index);
    // A new photo for the next view (the set stays).
    Q_INVOKABLE void newPhotoView();
    Q_INVOKABLE bool removePhotoView(int index);
    Q_INVOKABLE void clearPhotoViews();
    // Every view's straightened photo as a reference photo at true size on
    // its plane, its origin point (state.origin, straightened-image px; else
    // the middle of its closed outlines, else of the photo) at the plane's
    // origin; with `sketches`, each closed outline (state.dims) as a sketch
    // on that plane too. One undo step per photo and per sketch. Returns
    // how many photos were placed.
    Q_INVOKABLE int placePhotoViews(bool sketches);
    // Where a point of a view's straightened photo lands on its plane (mm,
    // y up), the view's origin rule applied: the acceptance run checks it.
    Q_INVOKABLE QPointF photoViewPlanePoint(int index, const QPointF& pixel) const;
    // A length typed for a reference side ("120", "4.5in", "12cm"), in mm
    // (a number alone in the display unit); 0 when it is not a length.
    Q_INVOKABLE double parseLengthMm(const QString& text) const;

    // ---- Camera (AR)
    bool arAvailable() const { return ar_.available && bool(devices_.startAr); }
    bool lidar() const { return ar_.lidar; }
    bool arBusy() const { return arBusy_; }
    QVariantMap arResult() const { return arResult_; }
    // "distance", "outline" or "box": ARKit's view until Done or Cancel.
    Q_INVOKABLE bool startAr(const QString& mode);
    Q_INVOKABLE void clearArResult();

    // ---- Kept measurements
    QVariantList measurements() const;
    // quantity: "length", "angle", "area" (any case). Returns the new id ("" if refused).
    Q_INVOKABLE QString addMeasurement(const QString& quantity, double value, const QString& source,
                                       const QString& name = QString());
    Q_INVOKABLE bool renameMeasurement(const QString& id, const QString& name);
    Q_INVOKABLE bool removeMeasurement(const QString& id);
    // One of them as in `measurements`, or an empty map.
    Q_INVOKABLE QVariantMap measurement(const QString& id) const;
    // In the display unit: "42.3 mm", "37.5°" (to read) and "42.3mm" (to type).
    Q_INVOKABLE QString formatValue(const QString& quantity, double value) const;
    Q_INVOKABLE QString valueText(const QString& quantity, double value) const;
    // formatValue in two parts for a big readout: the number ("42.3"; an
    // angle with its degree sign, "37.5°") and the unit as written for that
    // value ("mm", "cm²"; "" for an angle).
    Q_INVOKABLE QString formatNumber(const QString& quantity, double value) const;
    Q_INVOKABLE QString unitText(const QString& quantity, double value) const;
    // The unit's symbol for a quantity in the display unit ("mm", "in", "°").
    Q_INVOKABLE QString unitSymbol(const QString& quantity) const;

    // ---- Making geometry (AppController's design hooks): true when made.
    // pointsMm: QPointF or {x, y}, on a surface; centered on the origin.
    Q_INVOKABLE bool makeOutlineSketch(const QVariantList& pointsMm, bool flipY);
    // The found outline (points in the straightened photo's px, holes from
    // `found`) as a sketch of lines, arcs and circles at true size, its
    // longest side along X, centered on the origin (AppController::createProfileSketch).
    Q_INVOKABLE bool makeProfileSketch(const QVariantList& points, const QVariantMap& found);
    Q_INVOKABLE bool makeCircleSketch(double diameterMm);
    Q_INVOKABLE bool makeBox(double widthMm, double depthMm, double heightMm);

    // ---- Scan
    QVariantList scanPhotos() const;
    QString scanPaper() const { return scanPaper_; }
    bool scanBusy() const { return scanBusy_; }
    bool scanBuilding() const { return scanBuilding_; }
    double scanProgress() const { return scanProgress_; }
    QString scanStage() const { return scanStage_; }
    QVariantMap scanResult() const { return scanResult_; }
    QString scanError() const { return scanError_; }
    bool scanStereoAvailable() const { return true; }
    bool scanDetailFromStereo() const { return scanDetailFromStereo_; }
    void setScanDetailFromStereo(bool on);
    static constexpr int kMaxScanPhotos = 16;
    // The camera, again and again (a "Take another" loop), and the photo
    // library (several at once): each photo is added as it comes.
    Q_INVOKABLE void scanTakePhoto(const QRectF& anchor = QRectF());
    Q_INVOKABLE void scanChoosePhotos(const QRectF& anchor = QRectF());
    // Image files (the desktop's file dialog; QUrl or path strings).
    Q_INVOKABLE void scanAddFiles(const QVariantList& files);
    Q_INVOKABLE void scanRemovePhoto(int index);
    Q_INVOKABLE void scanSetCorner(int photo, int corner, double x, double y);
    Q_INVOKABLE void setScanPaper(const QString& kind);
    // Builds the mesh on a worker thread (progress, stage); false when there
    // are not enough photos or a build runs.
    Q_INVOKABLE bool scanBuild();
    Q_INVOKABLE void scanCancel();
    // The mesh as a body in the design (AppController::addMeshBody, "Scan N").
    Q_INVOKABLE bool scanAddToDesign();
    // Back to the photos (the result dropped), or a new scan (everything).
    Q_INVOKABLE void scanDiscardResult();
    Q_INVOKABLE void scanClear();
    // The desktop's file dialog with several files: the next files it gives
    // (the acceptance run sets them; empty: none waiting).
    void setNextScanFiles(const QStringList& files) { nextScanFiles_ = files; }
    Q_INVOKABLE QVariantList takeNextScanFiles();
    // The last build's mesh (millimeters, the paper's center at the origin),
    // or null: the result's preview draws it.
    const measure::ScanMesh* scanMesh() const { return scanMesh_.get(); }
    // Waits (processing events) while photos are read or a build runs, up to `ms`.
    bool waitForScan(int ms);

    // ---- For tests (the acceptance run)
    // The hardware (platformMeasureDevices() until set). Sensors running
    // restart on the new ones.
    void setDevices(MeasureDevices devices);
    const MeasureDevices& devices() const { return devices_; }
    // The steady clock (seconds) the holds are timed with (a test's own).
    void setClock(std::function<double()> clock);
    // Takes the window to be a folding phone's whole inner display
    // (foldCrease) on any platform.
    void setAssumeFoldingDisplay(bool on);
    // What was asked of the design hooks: "outline n", "circle d", "box w d h".
    const QStringList& madeLog() const { return madeLog_; }
    // The points of the last outline handed over (mm, as passed on).
    const QVariantList& lastOutlinePoints() const { return lastOutlinePoints_; }
    // The loops of the last profile sketch handed over ({points, through}, mm).
    const QVariantList& lastProfileLoops() const { return lastProfileLoops_; }
    // Waits (processing events) until no photo work runs, up to `ms`.
    bool waitForPhoto(int ms);

signals:
    void scaleChanged();
    void unitChanged();
    void sensorsChanged();
    void foldChanged();
    void levelChanged();
    void photoChanged();
    void straightChanged();
    void foundOutlineChanged();
    // Find outline finished (foundOutline: found or why not).
    void outlineFound();
    void photoViewsChanged();
    void arChanged();
    void arFinished();
    void measurementsChanged();
    // A new hold (the fold's or the level's): the workspace's short animation.
    void held(const QString& what);
    void scanChanged();
    void scanProgressChanged();
    // A build ended: "done", "failed" or "cancelled".
    void scanFinished(const QString& outcome);

private:
    QQuickWindow* window() const;
    void watchWindow(QQuickWindow* window);
    void setScale(double pointsPerMm, bool calibrated, const QString& note);
    void onHinge(const HingeReading& reading);
    // Feeds the fold's steady reading (true when what QML shows changed).
    bool feedFold(double degrees);
    void onGravity(const GravityReading& reading);
    void feedLevel();
    // The calibration's side of a level reading and of a hold.
    void correctGravity(const GravityReading& reading);
    void applyLevelCorrection(); // gravity_ and tilt_ from rawGravity_
    void levelCalHold();
    void loadLevelCalibration();
    QString levelCalKey(bool lying) const;
    double now() const;
    void loadCaliper();
    double defaultCaliperArm() const;
    void saveMeasurements() const;
    void loadMeasurements();
    void checkUnit();
    QString photoFolder() const;
    // Runs `work` on a worker thread, then `done` here (unless the photo
    // changed meanwhile: generation).
    void runPhotoWork(std::function<void()> work, std::function<void()> done);
    void adoptPickedFile(const QString& file);
    void setPhotoError(const QString& error);
    void forgetFile(const QString& file);
    // The camera's latest photo is kept for the next one's overlay ("": none).
    void keepCameraPhoto(const QString& file);

    AppController* app_;
    QPointer<QQuickWindow> window_;
    QPointer<QScreen> screen_;
    QList<QMetaObject::Connection> windowConnections_;
    QList<QMetaObject::Connection> screenConnections_;
    MeasureDevices devices_;
    std::function<double()> clock_;
    QElapsedTimer elapsed_;

    double pointsPerMm_ = 96.0 / 25.4;
    bool scaleCalibrated_ = false;
    QString scaleNote_;
    QString screenKey_;
    QString deviceKind_ = QStringLiteral("desktop");
    double screenWidthPt_ = 0;
    double screenHeightPt_ = 0;
    bool foldCrease_ = false;
    bool assumeFoldingDisplay_ = false;
    QString lastUnit_;

    bool sensorsRunning_ = false;
    StopWatching stopHinge_;
    StopWatching stopGravity_;
    bool hingeAvailable_ = false;
    bool gravityAvailable_ = false;
    QTimer* sensorTick_ = nullptr;
    measure::SteadyReading fold_;
    double foldAngle_ = 0;
    double foldRadians_ = 0;
    bool foldSteady_ = false;
    double foldProgress_ = 0;
    bool foldArmed_ = true;
    double lastHingeDegrees_ = 0;
    double lastHingeFeed_ = -1; // now() of the last hinge feed; < 0 before any
    double caliperArmMm_ = 0;
    bool caliperCalibrated_ = false;
    measure::SteadyReading level_;
    measure::Gravity gravity_;
    measure::Tilt tilt_;
    std::optional<measure::Gravity> levelReference_;
    int levelTrack_ = -1; // 0 slope, 1 edge, 2 relative: a change resets the hold
    double levelValue_ = 0;
    bool levelSteady_ = false;
    double levelProgress_ = 0;
    bool levelArmed_ = true;
    // The level's calibration: the offsets kept (lying: both components in
    // the device's upright frame; standing: per edge, the window's turn),
    // the raw readings of the last second, and a calibration or check under way.
    struct LevelCalibration {
        Vec2 lyingOffset;
        bool lyingKept = false;
        std::array<double, 4> edgeOffset{};
        std::array<bool, 4> edgeKept{};
        QString loadedFor; // the screen the offsets were read for
        bool loaded = false;
        QString mode;
        int step = 0;
        bool firstLying = true;
        int firstTurn = 0;
        measure::Gravity first; // upright frame (lying) or the window's (standing)
        QString result;
        double found = std::numeric_limits<double>::quiet_NaN();
    };
    LevelCalibration levelCal_;
    measure::Gravity rawGravity_; // the window's frame, as the sensors gave it
    int gravityTurn_ = 0;
    std::deque<std::pair<double, measure::Gravity>> recentGravity_; // (now(), raw)

    bool photoBusy_ = false;
    int photoGeneration_ = 0;
    std::vector<QThread*> workers_;
    QString photoFile_; // the upright photo measured (in photoFolder)
    QString lastCameraPhoto_; // the camera's latest photo: the next one's overlay (CameraOptions)
    QUrl photoUrl_;
    QSizeF photoSize_;
    std::array<QPointF, 4> corners_{};
    bool cornersDetected_ = false;
    QString referenceKind_ = QStringLiteral("card");
    double referenceSideA_ = 0;
    double referenceSideB_ = 0;
    QString straightFile_;
    QUrl straightUrl_;
    QSizeF straightSize_;
    double straightMmPerPixel_ = 0;
    QPointF straightOriginMm_;
    QVariantList straightReference_;
    QString photoError_;
    QVariantMap foundOutline_;
    QStringList madeFiles_; // files this run wrote into photoFolder (removed at the end)
    struct PhotoView {
        QString name;
        QString plane;
        QString photoFile;
        QSizeF photoSize;
        std::array<QPointF, 4> corners{};
        bool cornersDetected = false;
        QString referenceKind;
        double referenceSideA = 0;
        double referenceSideB = 0;
        QString straightFile;
        QSizeF straightSize;
        double mmPerPixel = 0;
        QPointF originMm;
        QVariantList straightReference;
        QVariantMap state;
    };
    std::vector<PhotoView> photoViews_;
    int photoViewIndex_ = -1;
    bool fileInViews(const QString& file) const;
    // The point of a view's photo put at its plane's origin (px).
    QPointF photoViewAnchor(const PhotoView& view) const;
    int fileCounter_ = 0;

    ArCapabilities ar_;
    bool arBusy_ = false;
    QVariantMap arResult_;

    measure::MeasurementLog log_;
    QStringList madeLog_;
    QVariantList lastOutlinePoints_;
    QVariantList lastProfileLoops_;

    // ---- Scan
    struct ScanPhotoEntry {
        QString file;     // the copy measured (in photoFolder, at most kScanPhotoSide)
        QString maskFile; // the subject mask (8-bit PNG), or ""
        int width = 0;
        int height = 0;
        std::array<QPointF, 4> corners{};
        bool found = false;
        QString source;
        bool used = false;
        QString problem;
        // The lens as a 35 mm equivalent from the photo's EXIF (the picker
        // reads it before re-encoding; still right after resizing, being
        // relative to the frame); 0: the engine estimates it from the paper.
        double focal35mm = 0;
    };
    void addScanFilesFrom(const QStringList& files, bool ownFiles, const QList<double>& focals = {});
    void scanPickedPhoto(const PhotoResult& result);
    void runScanWork(std::function<void()> work, std::function<void()> done);
    void updateScanBusy();
    std::vector<ScanPhotoEntry> scanPhotos_;
    QString scanPaper_ = QStringLiteral("a4");
    bool scanBusy_ = false;
    int scanReading_ = 0; // batches of photos being read
    bool scanBuilding_ = false;
    double scanProgress_ = 0;
    QString scanStage_;
    QVariantMap scanResult_;
    QString scanError_;
    bool scanDetailFromStereo_ = true;
    std::shared_ptr<measure::ScanMesh> scanMesh_;
    std::shared_ptr<std::atomic<bool>> scanCancel_;
    int scanGeneration_ = 0;
    QStringList nextScanFiles_;
};

} // namespace os::ui
