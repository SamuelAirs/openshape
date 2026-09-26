// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/AppController.h"

#include "core/Log.h"
#include "geometry/Exchange.h"
#include "geometry/Text.h"
#include "interaction/TouchWording.h"
#include "io/Export3mf.h"
#include "io/ProjectFile.h"
#include "io/RecentFiles.h"
#include "ui/RecoverySession.h"
#include "ui/ThumbnailSource.h"

#include <QtCore/QBuffer>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QVariantMap>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>

#include <algorithm>
#include <cmath>

namespace os::ui {

namespace {

std::filesystem::path toPath(const QUrl& url)
{
    const QString local = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return std::filesystem::path(local.toStdWString());
}

QString q(const std::string& s)
{
    return QString::fromStdString(s);
}

std::filesystem::path withExtension(std::filesystem::path path, const char* extension)
{
    if (path.extension().empty())
        path += extension;
    return path;
}

// The Text tool's fonts: Noto Sans, built into the executable
// (src/app/CMakeLists.txt), registered with the geometry layer once. A
// development build without it may name a font file in OPENSHAPE_TEXT_FONT
// to try the tool (resources/fonts/README.md).
void registerTextFonts()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    const std::pair<const char*, const char*> fonts[] = {
        {doc::kTextFontRegular, ":/openshape/fonts/NotoSans-Regular.ttf"},
        {doc::kTextFontBold, ":/openshape/fonts/NotoSans-Bold.ttf"},
    };
    for (const auto& [id, resource] : fonts) {
        QFile file(QString::fromLatin1(resource));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (!geom::registerFont(id, file.readAll().toStdString()))
            OS_LOG(Warning, App) << "the built-in font " << id << " could not be read";
    }
    if (geom::hasFont(doc::kTextFontRegular))
        return;
    const QString substitute = qEnvironmentVariable("OPENSHAPE_TEXT_FONT");
    QFile file(substitute);
    if (!substitute.isEmpty() && file.open(QIODevice::ReadOnly)
        && geom::registerFont(doc::kTextFontRegular, file.readAll().toStdString())) {
        OS_LOG(Warning, App) << "text uses " << substitute.toStdString()
                             << " in place of the built-in Noto Sans (OPENSHAPE_TEXT_FONT, for development only)";
        return;
    }
    OS_LOG(Warning, App) << "no font for text: resources/fonts/NotoSans-Regular.ttf was not built in; the Text tool is not available";
}

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent), document_(std::make_unique<doc::Document>()), undoStack_(std::make_unique<cmd::UndoStack>()),
      interaction_(std::make_unique<interact::InteractionController>(*document_, *undoStack_))
{
    registerTextFonts();
    QSettings settings;
    preferences_ = loadPreferences(settings);
    document_->setDisplayUnit(preferences_.defaultUnit);
    interaction_->setSketchGridSnap(preferences_.sketchGridSnap);
    interaction_->setHoleAllowance(preferences_.holeAllowance);

    recoveryDebounce_.setSingleShot(true);
    recoveryDebounce_.setInterval(kRecoveryDebounceMs);
    recoveryDeadline_.setSingleShot(true);
    connect(&recoveryDebounce_, &QTimer::timeout, this, &AppController::writeRecoveryCopy);
    connect(&recoveryDeadline_, &QTimer::timeout, this, &AppController::writeRecoveryCopy);
    // Every path that changes the document ends in stateChanged.
    connect(this, &AppController::stateChanged, this, &AppController::noteEdits);
    // Leaving the app (another window, the iPad home screen, which may end the
    // app without warning): copy unsaved work now rather than in a few seconds.
    if (auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(gui, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive)
                writeRecoveryCopy(); // no-op when the copy is current or copies are off
        });
    // Emitted before the process ends, even when Windows ends it right after
    // (logging off).
    if (auto* core = QCoreApplication::instance())
        connect(core, &QCoreApplication::aboutToQuit, this, &AppController::endRecovery);

    updateRecentFiles();
    attach();
    // Tablets and phones start in the touch layout. The flag lives in the
    // interaction core only (touchMode() reads it), so on-canvas targets and
    // the QML controls always agree.
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    interaction_->setTouchLayout(true);
    // No save dialog there: projects and exports go into the app's Documents
    // folder, which the Files app shows ("On My iPhone / iPad > OpenShape").
    setAppFolder(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
#endif
    interaction_->fitAll(false);
}

AppController::~AppController()
{
    endRecovery(); // exits that skip aboutToQuit (iPadOS unwinds out of exec())
}

void AppController::attach()
{
    interaction_->onViewChanged = [this] { emit viewChanged(); };
    interaction_->onStateChanged = [this] {
        emit stateChanged();
        emit viewChanged();
    };
    interaction_->onMessage = [this](const std::string& text) { notifyMessage(q(text)); };
}

void AppController::notifyMessage(const QString& text)
{
    // Passed on as it is: the interaction layer words its instructions for
    // touch itself, and a message may hold a file, project or body name that
    // no rewording may touch ("Exported Click fixture.stl").
    emit message(text);
}

QString AppController::touchWording(const QString& text) const
{
    return q(interact::touchWording(text.toStdString()));
}

// ---- Properties ----------------------------------------------------------------------

bool AppController::canUndo() const { return undoStack_->canUndo(); }
bool AppController::canRedo() const { return undoStack_->canRedo(); }
QString AppController::undoText() const { return q(undoStack_->undoLabel()); }
QString AppController::redoText() const { return q(undoStack_->redoLabel()); }
bool AppController::hasSelection() const { return !interaction_->selection().empty(); }
QString AppController::selectionSummary() const { return q(interaction_->selectionSummary()); }

QVariantList AppController::contextActions() const
{
    QVariantList list;
    for (const auto& action : interaction_->contextActions()) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), q(action.id));
        map.insert(QStringLiteral("label"), q(action.label));
        map.insert(QStringLiteral("active"), action.active);
        list.append(map);
    }
    return list;
}

bool AppController::operationActive() const { return interaction_->operation() != nullptr; }
QString AppController::operationTitle() const
{
    return interaction_->operation() ? q(interaction_->operation()->title()) : QString();
}
QString AppController::operationValueLabel() const
{
    return interaction_->operation() ? q(interaction_->operation()->valueLabel()) : QString();
}
QString AppController::operationValueText() const { return q(interaction_->operationValueText()); }
QString AppController::operationError() const
{
    return interaction_->operation() ? q(interaction_->operation()->error()) : QString();
}
bool AppController::operationCanCommit() const
{
    return interaction_->operation() && interaction_->operation()->canCommit();
}
bool AppController::operationHasValue() const
{
    const auto* op = interaction_->operation();
    return op && std::abs(op->value() - op->neutralValue()) > 1e-9;
}

QString AppController::operationPrompt() const
{
    return interaction_->operation() ? q(interaction_->operation()->prompt()) : QString();
}

bool AppController::operationTakesText() const
{
    return interaction_->operationTakesText();
}

QString AppController::operationText() const
{
    return q(interaction_->operationText());
}

QPointF AppController::valueLabelPosition() const
{
    const auto p = interaction_->valueLabelPosition();
    return p ? QPointF(p->x, p->y) : QPointF();
}

bool AppController::valueLabelVisible() const { return interaction_->valueLabelPosition().has_value(); }

bool AppController::touchMode() const { return interaction_->touchLayout(); }

void AppController::setTouchMode(bool on)
{
    if (interaction_->touchLayout() == on)
        return;
    interaction_->setTouchLayout(on);
    emit touchModeChanged();
}

bool AppController::penMode() const { return interaction_->penMode(); }

void AppController::setPenMode(bool on)
{
    if (interaction_->penMode() == on)
        return;
    interaction_->setPenMode(on);
    emit stateChanged();
}

QVariantList AppController::axisTriad() const
{
    QVariantList list;
    for (const auto& mark : interaction_->axisTriad()) {
        QVariantMap map;
        map.insert(QStringLiteral("axis"), mark.axis);
        map.insert(QStringLiteral("label"), QString(QChar("XYZ"[mark.axis])));
        map.insert(QStringLiteral("dx"), mark.direction.x);
        map.insert(QStringLiteral("dy"), mark.direction.y);
        map.insert(QStringLiteral("depth"), mark.depth);
        list.append(map);
    }
    return list;
}

QString AppController::documentTitle() const
{
    return path_.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path_).completeBaseName();
}

bool AppController::dirty() const { return !undoStack_->isClean(); }
int AppController::bodyCount() const { return int(document_->bodies().size()); }
QString AppController::displayUnit() const { return q(std::string(unitSymbol(document_->displayUnit()))); }
bool AppController::perspective() const { return interaction_->camera().projection == Camera::Projection::Perspective; }

QString AppController::projectFolder() const
{
    return path_.isEmpty() ? QString() : QUrl::fromLocalFile(QFileInfo(path_).absolutePath()).toString();
}

// ---- Sketch state ---------------------------------------------------------------------

bool AppController::sketchMode() const
{
    return interaction_->sketchSession() != nullptr;
}

QString AppController::sketchName() const
{
    const auto* s = interaction_->sketchSession();
    return s ? q(s->sketch().name()) : QString();
}

QString AppController::sketchTool() const
{
    const auto* s = interaction_->sketchSession();
    if (!s)
        return {};
    switch (s->tool()) {
    case interact::SketchTool::Select: return QStringLiteral("select");
    case interact::SketchTool::Line: return QStringLiteral("line");
    case interact::SketchTool::Rectangle: return QStringLiteral("rectangle");
    case interact::SketchTool::Circle: return QStringLiteral("circle");
    case interact::SketchTool::Arc: return QStringLiteral("arc");
    case interact::SketchTool::Slot: return QStringLiteral("slot");
    case interact::SketchTool::Trim: return QStringLiteral("trim");
    case interact::SketchTool::CenterRectangle: return QStringLiteral("centerRectangle");
    case interact::SketchTool::Polygon: return QStringLiteral("polygon");
    case interact::SketchTool::TangentArc: return QStringLiteral("tangentArc");
    }
    return {};
}

QString AppController::sketchStatus() const
{
    const auto* s = interaction_->sketchSession();
    return s ? q(s->statusText()) : QString();
}

QString AppController::sketchHint() const
{
    const auto* s = interaction_->sketchSession();
    return s ? q(s->hintText()) : QString();
}

bool AppController::sketchDrawing() const
{
    const auto* s = interaction_->sketchSession();
    return s && (s->isDrawing() || s->isOffsetting() || s->isPatterning()); // typed values go to the shape, offset or pattern
}

bool AppController::sketchCounterVisible() const
{
    const auto* s = interaction_->sketchSession();
    return s && s->counter().has_value();
}

QString AppController::sketchCounterText() const
{
    const auto* s = interaction_->sketchSession();
    const auto counter = s ? s->counter() : std::nullopt;
    return counter ? q(counter->text) : QString();
}

void AppController::stepSketchCounter(int delta)
{
    if (auto* s = interaction_->sketchSession()) {
        s->stepCounter(delta);
        emit stateChanged();
        emit viewChanged();
    }
}

QVariantList AppController::sketchLabels() const
{
    QVariantList list;
    const auto* s = interaction_->sketchSession();
    if (!s)
        return list;
    for (const auto& label : s->labels(interaction_->camera())) {
        QVariantMap map;
        QString kind;
        switch (label.kind) {
        case interact::SketchLabel::Kind::Dimension: kind = QStringLiteral("dimension"); break;
        case interact::SketchLabel::Kind::Input: kind = QStringLiteral("input"); break;
        case interact::SketchLabel::Kind::Hint: kind = QStringLiteral("hint"); break;
        case interact::SketchLabel::Kind::Constraint: kind = QStringLiteral("constraint"); break;
        }
        map.insert(QStringLiteral("kind"), kind);
        map.insert(QStringLiteral("key"), q(label.key));
        map.insert(QStringLiteral("constraint"), int(label.constraint));
        map.insert(QStringLiteral("text"), q(label.text));
        map.insert(QStringLiteral("caption"), q(label.caption));
        map.insert(QStringLiteral("x"), label.screen.x);
        map.insert(QStringLiteral("y"), label.screen.y);
        map.insert(QStringLiteral("focused"), label.focused);
        map.insert(QStringLiteral("locked"), label.locked);
        map.insert(QStringLiteral("selected"), label.selected);
        map.insert(QStringLiteral("hot"), label.hot);
        list.append(map);
    }
    return list;
}

bool AppController::canStartSketch() const
{
    if (interaction_->sketchSession())
        return false;
    const auto& sel = interaction_->selection();
    // Nothing selected: sketch on the ground plane. One flat face: sketch on
    // it. One profile: continue its sketch.
    return sel.empty()
        || (sel.size() == 1
            && (sel.items().front().kind == sel::SelectionKind::Face
                || sel.items().front().kind == sel::SelectionKind::SketchProfile));
}

bool AppController::faceSelected() const
{
    // A face (sketch on it) or a profile (continue its sketch): no plane menu.
    const auto& sel = interaction_->selection();
    return sel.size() == 1
        && (sel.items().front().kind == sel::SelectionKind::Face
            || sel.items().front().kind == sel::SelectionKind::SketchProfile);
}

void AppController::startSketch(const QString& plane)
{
    using P = interact::InteractionController::SketchPlane;
    const P p = plane == QLatin1String("front") ? P::Front : plane == QLatin1String("right") ? P::Right : P::Top;
    const Status status = interaction_->startSketch(p);
    if (!status)
        notifyMessage(q(status.userMessage()));
}

void AppController::finishSketch()
{
    interaction_->finishSketch();
}

void AppController::setSketchTool(const QString& name)
{
    if (name == QLatin1String("select"))
        interaction_->setSketchTool(interact::SketchTool::Select);
    else if (name == QLatin1String("line"))
        interaction_->setSketchTool(interact::SketchTool::Line);
    else if (name == QLatin1String("rectangle"))
        interaction_->setSketchTool(interact::SketchTool::Rectangle);
    else if (name == QLatin1String("circle"))
        interaction_->setSketchTool(interact::SketchTool::Circle);
    else if (name == QLatin1String("arc"))
        interaction_->setSketchTool(interact::SketchTool::Arc);
    else if (name == QLatin1String("slot"))
        interaction_->setSketchTool(interact::SketchTool::Slot);
    else if (name == QLatin1String("trim"))
        interaction_->setSketchTool(interact::SketchTool::Trim);
    else if (name == QLatin1String("centerRectangle"))
        interaction_->setSketchTool(interact::SketchTool::CenterRectangle);
    else if (name == QLatin1String("polygon"))
        interaction_->setSketchTool(interact::SketchTool::Polygon);
    else if (name == QLatin1String("tangentArc"))
        interaction_->setSketchTool(interact::SketchTool::TangentArc);
}

QString AppController::sketchType(const QString& text)
{
    auto* s = interaction_->sketchSession();
    if (!s)
        return {};
    const QString error = q(s->typeIntoInput(text.toStdString()));
    emit stateChanged();
    emit viewChanged();
    return error;
}

void AppController::focusNextSketchInput()
{
    if (auto* s = interaction_->sketchSession()) {
        s->focusNextInput();
        emit viewChanged();
    }
}

void AppController::commitSketchTool()
{
    if (auto* s = interaction_->sketchSession()) {
        const Status status = s->commitTool();
        if (!status && status.error() != ErrorCode::InvalidArgument)
            notifyMessage(q(status.userMessage()));
        emit stateChanged();
        emit viewChanged();
    }
}

QString AppController::setSketchDimension(int constraintId, const QString& text)
{
    auto* s = interaction_->sketchSession();
    if (!s)
        return {};
    const QString error = q(s->setDimension(sketch::EntityId(constraintId), text.toStdString()));
    emit stateChanged();
    emit viewChanged();
    return error;
}

// ---- History ------------------------------------------------------------------------------

QVariantList AppController::history() const
{
    QVariantList list;
    for (const auto& row : interaction_->historyRows()) {
        QVariantMap map;
        map.insert(QStringLiteral("kind"), row.kind == interact::HistoryRow::Kind::Sketch ? QStringLiteral("sketch")
                                           : row.kind == interact::HistoryRow::Kind::Body ? QStringLiteral("body")
                                                                                          : QStringLiteral("feature"));
        map.insert(QStringLiteral("id"), q(row.id.toString()));
        map.insert(QStringLiteral("name"), q(row.name));
        map.insert(QStringLiteral("detail"), q(row.detail));
        map.insert(QStringLiteral("status"), row.status == interact::HistoryRow::Status::Ok            ? QStringLiteral("ok")
                                             : row.status == interact::HistoryRow::Status::Warning     ? QStringLiteral("warning")
                                             : row.status == interact::HistoryRow::Status::Failed      ? QStringLiteral("failed")
                                             : row.status == interact::HistoryRow::Status::Suppressed  ? QStringLiteral("suppressed")
                                                                                                        : QStringLiteral("blocked"));
        map.insert(QStringLiteral("message"), q(row.message));
        map.insert(QStringLiteral("visible"), row.visible);
        map.insert(QStringLiteral("canDelete"), row.canDelete);
        map.insert(QStringLiteral("canSuppress"), row.canSuppress);
        map.insert(QStringLiteral("canSplit"), row.canSplit);
        // The body a row belongs to (itself for a body row).
        map.insert(QStringLiteral("bodyId"), q((row.kind == interact::HistoryRow::Kind::Feature ? row.parentId : row.id).toString()));
        QVariantList params;
        for (const auto& p : row.parameters) {
            QVariantMap pm;
            pm.insert(QStringLiteral("key"), q(p.key));
            pm.insert(QStringLiteral("label"), q(p.label));
            pm.insert(QStringLiteral("value"), q(p.valueText));
            pm.insert(QStringLiteral("isText"), p.isText);
            params.append(pm);
        }
        map.insert(QStringLiteral("parameters"), params);
        list.append(map);
    }
    return list;
}

namespace {
std::optional<Uuid> uuidOf(const QString& text)
{
    return Uuid::parse(text.toStdString());
}
} // namespace

QString AppController::setFeatureParameter(const QString& featureId, const QString& key, const QString& text)
{
    const auto id = uuidOf(featureId);
    if (!id)
        return QStringLiteral("That step no longer exists.");
    const Status status = interaction_->setFeatureParameter(*id, key.toStdString(), text.toStdString());
    return status ? QString() : q(status.userMessage());
}

void AppController::setFeatureSuppressed(const QString& featureId, bool suppressed)
{
    if (const auto id = uuidOf(featureId)) {
        const Status status = interaction_->setFeatureSuppressed(*id, suppressed);
        if (!status)
            notifyMessage(q(status.userMessage()));
    }
}

void AppController::deleteHistoryItem(const QString& kind, const QString& idText)
{
    const auto id = uuidOf(idText);
    if (!id)
        return;
    Status status = okStatus();
    if (kind == QLatin1String("feature"))
        status = interaction_->deleteFeature(*id);
    else if (kind == QLatin1String("body"))
        status = interaction_->deleteBody(*id);
    else if (kind == QLatin1String("sketch"))
        status = interaction_->deleteSketch(*id);
    if (!status)
        notifyMessage(q(status.userMessage()));
}

void AppController::setHistoryItemVisible(const QString& kind, const QString& idText, bool visible)
{
    const auto id = uuidOf(idText);
    if (!id)
        return;
    const Status status = kind == QLatin1String("sketch") ? interaction_->setSketchVisible(*id, visible)
                                                          : interaction_->setBodyVisible(*id, visible);
    if (!status)
        notifyMessage(q(status.userMessage()));
}

void AppController::editSketch(const QString& sketchId)
{
    if (const auto id = uuidOf(sketchId)) {
        const Status status = interaction_->editSketch(*id);
        if (!status)
            notifyMessage(q(status.userMessage()));
    }
}

void AppController::highlightHistoryItem(const QString& id)
{
    interaction_->setHistoryHighlight(id.isEmpty() ? std::nullopt : uuidOf(id));
}

void AppController::selectBody(const QString& bodyId, bool additive)
{
    if (const auto id = uuidOf(bodyId))
        (void)interaction_->selectBody(*id, additive); // failures explain themselves via message()
}

void AppController::addBodyToSelection(const QString& bodyId)
{
    if (const auto id = uuidOf(bodyId))
        (void)interaction_->selectBody(*id, interact::InteractionController::BodyPick::Add);
}

void AppController::duplicateBody(const QString& bodyId)
{
    if (const auto id = uuidOf(bodyId)) {
        const Status status = interaction_->duplicateBody(*id);
        if (!status)
            notifyMessage(q(status.userMessage()));
    }
}

void AppController::splitBody(const QString& bodyId)
{
    if (const auto id = uuidOf(bodyId))
        (void)interaction_->splitBody(*id); // failures explain themselves via message()
}

void AppController::runTool(const QString& id)
{
    // Guidance for a selection that does not fit arrives through message().
    (void)interaction_->runTool(id.toStdString());
}

// ---- Files ------------------------------------------------------------------------------

void AppController::newDocument()
{
    auto document = std::make_unique<doc::Document>();
    document->setDisplayUnit(preferences_.defaultUnit);
    auto stack = std::make_unique<cmd::UndoStack>();
    interaction_->setDocument(*document, *stack);
    document_ = std::move(document);
    undoStack_ = std::move(stack);
    path_.clear();
    documentReplaced();
    setHomeVisible(false);
    emit documentChanged();
    emit stateChanged();
    emit viewChanged();
}

bool AppController::openProject(const QUrl& url)
{
    const auto path = toPath(url);
    auto loaded = io::loadProject(path);
    if (!loaded) {
        OS_LOG(Warning, File) << loaded.developerMessage();
        notifyMessage(q(loaded.userMessage()));
        return false;
    }
    auto stack = std::make_unique<cmd::UndoStack>();
    interaction_->setDocument(*loaded.value(), *stack);
    document_ = std::move(loaded.value());
    undoStack_ = std::move(stack);
    path_ = QString::fromStdWString(path.wstring());
    documentReplaced();
    setHomeVisible(false);
    rememberRecentFile(path_);
    for (const auto& body : document_->bodies())
        if (body->hasFailures()) {
            notifyMessage(QStringLiteral("Some steps could not be rebuilt. The last good shape is shown."));
            break;
        }
    emit documentChanged();
    emit stateChanged();
    emit viewChanged();
    return true;
}

bool AppController::saveProject()
{
    if (path_.isEmpty())
        return false;
    io::SaveOptions options;
    options.thumbnailPng = thumbnailPng();
    const Status status = io::saveProject(*document_, std::filesystem::path(path_.toStdWString()), options);
    if (!status) {
        OS_LOG(Warning, File) << status.developerMessage();
        notifyMessage(q(status.userMessage()));
        return false;
    }
    undoStack_->setClean();
    // The user's file has the work now: no recovery copy needed.
    stopRecoveryTimers();
    if (recovery_)
        recovery_->removeCopy();
    copyRevision_ = ~std::uint64_t(0);
    rememberRecentFile(path_);
    notifyMessage(QStringLiteral("Saved"));
    emit stateChanged();
    return true;
}

std::vector<unsigned char> AppController::thumbnailPng() const
{
    QElapsedTimer timer;
    timer.start();
    const interact::ThumbnailImage image = interaction_->renderThumbnail(kThumbnailSize);
    if (image.empty())
        return {};
    const QImage picture(image.rgba.data(), image.width, image.height, image.width * 4, QImage::Format_RGBA8888_Premultiplied);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!picture.save(&buffer, "PNG")) {
        OS_LOG(Warning, File) << "the project preview could not be encoded; saving without it";
        return {};
    }
    OS_LOG(Debug, Performance) << "thumbnail took " << timer.elapsed() << " ms (" << bytes.size() << " bytes)";
    return std::vector<unsigned char>(bytes.begin(), bytes.end());
}

bool AppController::saveProjectAs(const QUrl& url)
{
    const auto path = withExtension(toPath(url), io::kProjectExtension);
    path_ = QString::fromStdWString(path.wstring());
    emit documentChanged();
    return saveProject();
}

Status AppController::writeExport(const QString& format, const std::filesystem::path& path)
{
    std::vector<geom::NamedShape> shapes;
    for (const auto& body : document_->bodies())
        if (body->isVisible() && !body->shape().isNull())
            shapes.push_back({body->name(), body->shape()});
    if (format == QLatin1String("step"))
        return geom::exportStep(shapes, withExtension(path, ".step"));
    if (format == QLatin1String("stl"))
        return geom::exportStl(shapes, withExtension(path, ".stl"));
    if (format == QLatin1String("3mf"))
        return io::export3mf(shapes, withExtension(path, ".3mf"));
    return Status::failure(ErrorCode::InvalidArgument, "That export format is not available.",
                           "writeExport: unknown format '" + format.toStdString() + "'");
}

bool AppController::exportStep(const QUrl& url)
{
    const Status status = writeExport(QStringLiteral("step"), toPath(url));
    notifyMessage(status ? QStringLiteral("Exported STEP") : q(status.userMessage()));
    return status.ok();
}

namespace {

// "Imported “Bracket”" / "Imported 3 bodies", plus what was skipped.
QString importMessage(const std::vector<geom::NamedShape>& shapes, const std::vector<std::string>& warnings,
                      const doc::Document& document)
{
    QString text = shapes.size() == 1 && !document.bodies().empty()
                     ? QStringLiteral("Imported \u201C%1\u201D").arg(QString::fromStdString(document.bodies().back()->name()))
                     : QStringLiteral("Imported %1 bodies").arg(shapes.size());
    for (const std::string& warning : warnings)
        text += QStringLiteral(". ") + QString::fromStdString(warning);
    return text;
}

} // namespace

bool AppController::importStep(const QUrl& url)
{
    const auto path = toPath(url);
    auto imported = geom::importStep(path);
    if (!imported) {
        OS_LOG(Warning, File) << "import failed: " << imported.developerMessage();
        notifyMessage(q(imported.userMessage()));
        return false;
    }
    const std::string source = QFileInfo(QString::fromStdWString(path.wstring())).fileName().toStdString();
    const Status status = interaction_->importBodies(imported.value(), source);
    if (!status)
        return false; // the controller said why
    notifyMessage(importMessage(imported.value(), imported.warnings(), *document_));
    return true;
}

bool AppController::importStepAsProject(const QUrl& url)
{
    // Read the file first: a file that cannot be imported leaves the current
    // document alone.
    const auto path = toPath(url);
    auto imported = geom::importStep(path);
    if (!imported) {
        OS_LOG(Warning, File) << "import failed: " << imported.developerMessage();
        notifyMessage(q(imported.userMessage()));
        return false;
    }
    newDocument();
    const std::string source = QFileInfo(QString::fromStdWString(path.wstring())).fileName().toStdString();
    if (!interaction_->importBodies(imported.value(), source))
        return false;
    notifyMessage(importMessage(imported.value(), imported.warnings(), *document_));
    return true;
}

QUrl AppController::takeNextFileChoice()
{
    QUrl url;
    std::swap(url, nextFileChoice_);
    return url;
}

bool AppController::exportStl(const QUrl& url)
{
    const Status status = writeExport(QStringLiteral("stl"), toPath(url));
    notifyMessage(status ? QStringLiteral("Exported STL") : q(status.userMessage()));
    return status.ok();
}

bool AppController::export3mf(const QUrl& url)
{
    const Status status = writeExport(QStringLiteral("3mf"), toPath(url));
    notifyMessage(status ? QStringLiteral("Exported 3MF") : q(status.userMessage()));
    return status.ok();
}

// ---- The app folder (iPhone / iPad) ---------------------------------------------------

void AppController::setAppFolder(const QString& folder)
{
    const QString cleaned = folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath());
    if (cleaned == appFolder_)
        return;
    appFolder_ = cleaned;
    if (!appFolder_.isEmpty())
        QDir().mkpath(appFolder_);
    updateRecentFiles(); // entries into the folder's old location move along
    emit appFolderChanged();
}

QString AppController::appFolderUrl() const
{
    return appFolder_.isEmpty() ? QString() : QUrl::fromLocalFile(appFolder_).toString();
}

bool AppController::appFolderHasProject(const QString& name) const
{
    const QString base = projectFileBaseName(name);
    return savesToAppFolder() && !base.isEmpty()
        && QFileInfo::exists(appFolder_ + QLatin1Char('/') + base + QStringLiteral(".openshape"));
}

bool AppController::saveInAppFolder(const QString& name)
{
    if (!savesToAppFolder())
        return false;
    const QString base = projectFileBaseName(name);
    if (base.isEmpty()) {
        notifyMessage(QStringLiteral("Type a name for the project."));
        return false;
    }
    QDir().mkpath(appFolder_);
    return saveProjectAs(QUrl::fromLocalFile(appFolder_ + QLatin1Char('/') + base + QStringLiteral(".openshape")));
}

bool AppController::exportToAppFolder(const QString& format)
{
    if (!savesToAppFolder())
        return false;
    const QString folder = appFolder_ + QStringLiteral("/Exports");
    if (!QDir().mkpath(folder)) {
        OS_LOG(Warning, File) << "cannot create " << folder.toStdString();
        notifyMessage(QStringLiteral("Could not create the Exports folder."));
        return false;
    }
    const QString base = projectFileBaseName(documentTitle());
    const QString file = (base.isEmpty() ? QStringLiteral("Untitled") : base) + QLatin1Char('.') + format;
    const Status status = writeExport(format, std::filesystem::path((folder + QLatin1Char('/') + file).toStdWString()));
    if (!status) {
        OS_LOG(Warning, File) << status.developerMessage();
        notifyMessage(q(status.userMessage()));
        return false;
    }
    // Where to find it: the Files app shows the app's folder by its name.
    notifyMessage(QStringLiteral("Exported %1 to OpenShape \u2192 Exports (Files app)").arg(file));
    return true;
}

// ---- Actions ----------------------------------------------------------------------------

void AppController::createBox(double size)
{
    (void)interaction_->createBox(size);
}

void AppController::undo() { interaction_->undo(); }
void AppController::redo() { interaction_->redo(); }

void AppController::undoWithFeedback()
{
    const QString label = undoText();
    if (interaction_->undo())
        notifyMessage(QStringLiteral("Undo ") + label);
}

void AppController::redoWithFeedback()
{
    const QString label = redoText();
    if (interaction_->redo())
        notifyMessage(QStringLiteral("Redo ") + label);
}

void AppController::commitOperation()
{
    const Status status = interaction_->commitOperation();
    if (!status)
        notifyMessage(q(status.userMessage()));
}

void AppController::cancelOperation() { interaction_->cancelOperation(); }

QString AppController::setValueText(const QString& text)
{
    return q(interaction_->setValueText(text.toStdString()));
}

QString AppController::setOperationText(const QString& text)
{
    return q(interaction_->setOperationText(text.toStdString()));
}

void AppController::triggerAction(const QString& id)
{
    const Status status = interaction_->triggerAction(id.toStdString());
    if (!status)
        notifyMessage(q(status.userMessage()));
}

void AppController::setView(const QString& name)
{
    static const std::map<QString, StandardView> views{
        {QStringLiteral("front"), StandardView::Front}, {QStringLiteral("back"), StandardView::Back},
        {QStringLiteral("left"), StandardView::Left},   {QStringLiteral("right"), StandardView::Right},
        {QStringLiteral("top"), StandardView::Top},     {QStringLiteral("bottom"), StandardView::Bottom},
        {QStringLiteral("iso"), StandardView::Isometric}};
    const auto it = views.find(name);
    if (it != views.end())
        interaction_->setStandardView(it->second);
}

void AppController::fitAll() { interaction_->fitAll(true); }

void AppController::togglePerspective()
{
    interaction_->setProjection(perspective() ? Camera::Projection::Orthographic : Camera::Projection::Perspective);
}

void AppController::setDisplayUnit(const QString& symbol)
{
    if (const auto unit = unitFromSymbol(symbol.toStdString())) {
        document_->setDisplayUnit(*unit);
        emit stateChanged();
    }
}

bool AppController::handleKey(int key)
{
    switch (key) {
    case Qt::Key_Escape: return interaction_->keyPress(interact::Key::Escape);
    case Qt::Key_Return:
    case Qt::Key_Enter: return interaction_->keyPress(interact::Key::Enter);
    case Qt::Key_Delete: return interaction_->keyPress(interact::Key::Delete);
    case Qt::Key_Backspace: return interaction_->keyPress(interact::Key::Backspace);
    default: return false;
    }
}

// ---- Recovery copies ------------------------------------------------------------------------

void AppController::startRecovery(const QString& directory)
{
    recovery_ = std::make_unique<RecoverySession>(directory);
    OS_LOG(Info, File) << "recovery copies go to " << QDir::toNativeSeparators(directory).toStdString();
    documentReplaced();
}

void AppController::documentReplaced()
{
    stopRecoveryTimers();
    if (recovery_)
        recovery_->removeCopy();
    copyRevision_ = ~std::uint64_t(0);
    discardedRevision_ = ~std::uint64_t(0);
    seenRevision_ = undoStack_->revision();
}

void AppController::stopRecoveryTimers()
{
    recoveryDebounce_.stop();
    recoveryDeadline_.stop();
}

void AppController::noteEdits()
{
    const std::uint64_t revision = undoStack_->revision();
    if (revision == seenRevision_)
        return;
    seenRevision_ = revision;
    if (!recovery_ || preferences_.recoveryIntervalSeconds <= 0)
        return;
    if (!dirty()) {
        // Undone back to what is saved: nothing to recover.
        stopRecoveryTimers();
        recovery_->removeCopy();
        copyRevision_ = ~std::uint64_t(0);
        return;
    }
    recoveryDebounce_.start();
    if (!recoveryDeadline_.isActive())
        recoveryDeadline_.start(preferences_.recoveryIntervalSeconds * 1000);
}

void AppController::writeRecoveryCopy()
{
    stopRecoveryTimers();
    const std::uint64_t revision = undoStack_->revision();
    if (!recovery_ || preferences_.recoveryIntervalSeconds <= 0 || !dirty() || copyRevision_ == revision
        || discardedRevision_ == revision)
        return;
    io::RecoveryInfo info;
    info.originalPath = path_.toStdString();
    info.title = documentTitle().toStdString();
    info.appVersion = QCoreApplication::applicationVersion().toStdString();
    const Status status = recovery_->write(*document_, info);
    if (!status) {
        OS_LOG(Warning, File) << "recovery copy failed: " << status.developerMessage();
        if (!recoveryWarned_) // once per run: it would repeat after every edit
            notifyMessage(q(status.userMessage()));
        recoveryWarned_ = true;
        return;
    }
    copyRevision_ = undoStack_->revision();
}

void AppController::discardUnsavedWork()
{
    stopRecoveryTimers();
    if (recovery_)
        recovery_->removeCopy();
    copyRevision_ = ~std::uint64_t(0);
    discardedRevision_ = undoStack_->revision();
}

void AppController::endRecovery()
{
    if (!recovery_ || recoveryEnded_)
        return;
    recoveryEnded_ = true;
    stopRecoveryTimers();
    const bool keep = preferences_.recoveryIntervalSeconds > 0 && dirty() && discardedRevision_ != undoStack_->revision();
    if (keep)
        writeRecoveryCopy();
    recovery_->setKeepCopy(keep && recovery_->hasCopy());
    if (recovery_->keepsCopy())
        OS_LOG(Info, File) << "the run ends with unsaved changes; recovery copy kept for the next start: "
                           << recoveryCopyFile().toStdString();
}

QString AppController::recoveryCopyFile() const
{
    return recovery_ && recovery_->hasCopy() ? QString::fromStdWString(recovery_->store().projectFile(recovery_->session()).wstring())
                                             : QString();
}

void AppController::checkForRecovery()
{
    if (!recovery_)
        return;
    orphans_ = recovery_->findOrphans();
    emit recoveryChanged();
}

QVariantList AppController::recoveryItems() const
{
    QVariantList list;
    for (const auto& entry : orphans_) {
        const QString original = currentLocation(entry.info.originalPath);
        QString title = QString::fromStdString(entry.info.title);
        if (title.isEmpty())
            title = original.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(original).completeBaseName();
        QVariantMap map;
        map.insert(QStringLiteral("session"), QString::fromStdString(entry.session));
        map.insert(QStringLiteral("title"), title);
        map.insert(QStringLiteral("detail"), original.isEmpty() ? QStringLiteral("Never saved") : QDir::toNativeSeparators(original));
        map.insert(QStringLiteral("time"),
                   entry.info.savedAtMs > 0
                       ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(entry.info.savedAtMs), QLocale::ShortFormat)
                       : QStringLiteral("Time unknown"));
        list.append(map);
    }
    return list;
}

bool AppController::restoreRecovery(const QString& sessionText)
{
    const std::string session = sessionText.toStdString();
    const auto it = std::find_if(orphans_.begin(), orphans_.end(), [&](const io::RecoveryEntry& e) { return e.session == session; });
    if (!recovery_ || it == orphans_.end())
        return false;
    auto loaded = io::loadProject(it->projectFile);
    if (!loaded) {
        OS_LOG(Warning, File) << "restoring " << session << ": " << loaded.developerMessage();
        notifyMessage(QStringLiteral("This recovery copy could not be opened. ") + q(loaded.userMessage()));
        return false;
    }
    const io::RecoveryEntry entry = *it;
    orphans_.erase(it);
    auto stack = std::make_unique<cmd::UndoStack>();
    stack->setModified(); // not saved anywhere yet
    interaction_->setDocument(*loaded.value(), *stack);
    document_ = std::move(loaded.value());
    undoStack_ = std::move(stack);
    // (Where the project is now: on iOS the app's folder moves with updates.)
    path_ = currentLocation(entry.info.originalPath);
    documentReplaced();
    setHomeVisible(false);
    // The copy stays (now as this run's) until the document is saved or discarded.
    if (const Status adopted = recovery_->adopt(session, entry.info); adopted) {
        copyRevision_ = undoStack_->revision();
    } else {
        OS_LOG(Warning, File) << "restoring " << session << ": " << adopted.developerMessage();
        writeRecoveryCopy();
    }
    OS_LOG(Info, File) << "restored recovery copy " << session << " (" << documentTitle().toStdString() << ")";
    notifyMessage(QStringLiteral("Restored “%1”. Save to keep it.").arg(documentTitle()));
    emit recoveryChanged();
    emit documentChanged();
    emit stateChanged();
    emit viewChanged();
    return true;
}

void AppController::discardRecovery(const QString& sessionText)
{
    const std::string session = sessionText.toStdString();
    const auto it = std::find_if(orphans_.begin(), orphans_.end(), [&](const io::RecoveryEntry& e) { return e.session == session; });
    if (!recovery_ || it == orphans_.end())
        return;
    if (const Status status = recovery_->discard(session); !status) {
        OS_LOG(Warning, File) << "discarding " << session << ": " << status.developerMessage();
        notifyMessage(q(status.userMessage()));
        return;
    }
    orphans_.erase(it);
    emit recoveryChanged();
}

void AppController::discardAllRecovery()
{
    std::vector<std::string> sessions;
    for (const auto& e : orphans_)
        sessions.push_back(e.session);
    for (const auto& s : sessions)
        discardRecovery(QString::fromStdString(s));
}

void AppController::postponeRecovery()
{
    if (recovery_)
        recovery_->releaseOrphans();
    orphans_.clear();
    emit recoveryChanged();
}

// ---- Recent files and preferences -----------------------------------------------------------

namespace {
std::vector<std::string> toStd(const QStringList& list)
{
    std::vector<std::string> out;
    for (const QString& s : list)
        out.push_back(s.toStdString());
    return out;
}
} // namespace

QString AppController::currentLocation(const std::string& storedPath) const
{
    return QString::fromStdString(appFolder_.isEmpty() ? storedPath : io::rebasedIntoFolder(storedPath, appFolder_.toStdString()));
}

void AppController::updateRecentFiles()
{
    QSettings settings;
    std::vector<std::string> stored = toStd(loadRecentFiles(settings));
    if (!appFolder_.isEmpty()) {
        // iPhone / iPad after an app update: the same files, in the app
        // folder's new location (stored so, and without duplicates).
        std::vector<std::string> moved;
        bool changed = false;
        for (const std::string& file : stored) {
            const std::string now = currentLocation(file).toStdString();
            changed = changed || now != file;
            if (std::none_of(moved.begin(), moved.end(), [&](const std::string& m) { return io::sameRecentPath(m, now); }))
                moved.push_back(now);
        }
        if (changed) {
            QStringList files;
            for (const std::string& file : moved)
                files.append(QString::fromStdString(file));
            saveRecentFiles(settings, files);
            stored = std::move(moved);
        }
    }
    QVariantList list;
    const std::vector<std::string> recent = io::existingRecentFiles(stored);
    for (const std::string& file : recent) {
        const QFileInfo info(QString::fromStdString(file));
        QVariantMap map;
        map.insert(QStringLiteral("path"), info.absoluteFilePath());
        map.insert(QStringLiteral("name"), info.completeBaseName());
        map.insert(QStringLiteral("folder"), info.absoluteDir().dirName());
        list.append(map);
    }
    // Home: the same files with previews and dates, plus (iPadOS) the
    // projects in the app's Documents folder, which the Files app shows.
    std::vector<std::string> folder;
#if defined(Q_OS_IOS)
    const QDir documents(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    for (const QFileInfo& info : documents.entryInfoList({QStringLiteral("*.openshape")}, QDir::Files, QDir::Time))
        folder.push_back(info.absoluteFilePath().toStdString());
#endif
    QVariantList home;
    for (const std::string& file : io::homeProjects(recent, folder)) {
        const QFileInfo info(QString::fromStdString(file));
        const QDateTime modified = info.lastModified();
        QVariantMap map;
        map.insert(QStringLiteral("path"), info.absoluteFilePath());
        map.insert(QStringLiteral("name"), info.completeBaseName());
        map.insert(QStringLiteral("folder"), QDir::toNativeSeparators(info.absolutePath()));
        map.insert(QStringLiteral("modified"), QLocale().toString(modified, QLocale::ShortFormat));
        map.insert(QStringLiteral("thumbnail"), thumbnailSource(info.absoluteFilePath(), modified.toMSecsSinceEpoch()));
        map.insert(QStringLiteral("removable"),
                   std::any_of(recent.begin(), recent.end(), [&](const std::string& r) { return io::sameRecentPath(r, file); }));
        home.append(map);
    }
    if (list == recentFiles_ && home == homeProjects_)
        return; // the menu keeps its items
    recentFiles_ = list;
    homeProjects_ = home;
    emit recentFilesChanged();
}

void AppController::setHomeVisible(bool visible)
{
    if (visible)
        updateRecentFiles(); // files saved, moved or deleted meanwhile
    if (visible == homeVisible_)
        return;
    homeVisible_ = visible;
    emit homeChanged();
}

void AppController::removeRecentFile(const QString& path)
{
    QSettings settings;
    QStringList files;
    for (const std::string& f : io::withoutRecentFile(toStd(loadRecentFiles(settings)), path.toStdString()))
        files.append(QString::fromStdString(f));
    saveRecentFiles(settings, files);
    updateRecentFiles();
}

void AppController::refreshRecentFiles()
{
    updateRecentFiles();
}

void AppController::rememberRecentFile(const QString& path)
{
    QSettings settings;
    QStringList files;
    for (const std::string& f : io::withRecentFile(toStd(loadRecentFiles(settings)), QFileInfo(path).absoluteFilePath().toStdString()))
        files.append(QString::fromStdString(f));
    saveRecentFiles(settings, files);
    updateRecentFiles();
}

bool AppController::openRecent(const QString& path)
{
    const bool opened = openProject(QUrl::fromLocalFile(path));
    if (!opened)
        updateRecentFiles(); // a file that is gone drops out of the list
    return opened;
}

void AppController::clearRecentFiles()
{
    QSettings settings;
    saveRecentFiles(settings, {});
    updateRecentFiles();
}

void AppController::savePreferences() const
{
    QSettings settings;
    ui::savePreferences(settings, preferences_);
}

QString AppController::defaultUnit() const
{
    return q(std::string(unitSymbol(preferences_.defaultUnit)));
}

void AppController::setDefaultUnit(const QString& symbol)
{
    const LengthUnit unit = symbol == QLatin1String("in") ? LengthUnit::Inch : LengthUnit::Millimeter;
    if (unit == preferences_.defaultUnit)
        return;
    preferences_.defaultUnit = unit;
    savePreferences();
    // An untouched new document follows the choice right away.
    if (path_.isEmpty() && document_->bodies().empty() && document_->sketches().empty() && !undoStack_->canUndo()
        && !undoStack_->canRedo())
        document_->setDisplayUnit(unit);
    emit preferencesChanged();
    emit stateChanged();
}

void AppController::setSketchGridSnap(bool on)
{
    if (on == preferences_.sketchGridSnap)
        return;
    preferences_.sketchGridSnap = on;
    interaction_->setSketchGridSnap(on);
    savePreferences();
    emit preferencesChanged();
}

void AppController::setRecoveryInterval(int seconds)
{
    if (std::find(kRecoveryIntervals.begin(), kRecoveryIntervals.end(), seconds) == kRecoveryIntervals.end()
        || seconds == preferences_.recoveryIntervalSeconds)
        return;
    preferences_.recoveryIntervalSeconds = seconds;
    savePreferences();
    stopRecoveryTimers();
    if (seconds == 0) {
        if (recovery_)
            recovery_->removeCopy(); // turned off: keep nothing around
        copyRevision_ = ~std::uint64_t(0);
    } else if (dirty() && copyRevision_ != undoStack_->revision()) {
        recoveryDebounce_.start();
        recoveryDeadline_.start(seconds * 1000);
    }
    emit preferencesChanged();
}

void AppController::setHoleAllowance(double mm)
{
    if (!std::isfinite(mm) || mm < 0.0 || mm > doc::kMaxHoleAllowance)
        return;
    mm = std::round(mm * 1000.0) / 1000.0; // a micrometer is plenty for a printer
    if (std::abs(mm - preferences_.holeAllowance) < 1e-12)
        return;
    preferences_.holeAllowance = mm;
    interaction_->setHoleAllowance(mm);
    savePreferences();
    emit preferencesChanged();
    emit stateChanged(); // an open Hole tool's preset diameter changed
}

QString AppController::setHoleAllowanceText(const QString& text)
{
    // Always millimeters here, whatever the document's unit ("0.2" = 0.2 mm);
    // a unit can still be typed ("0.01in").
    const auto parsed = parseLength(text.toStdString(), LengthUnit::Millimeter);
    if (!parsed.millimeters)
        return q(parsed.error);
    if (!(*parsed.millimeters >= 0.0) || *parsed.millimeters > doc::kMaxHoleAllowance + 1e-9)
        return QStringLiteral("The allowance must be between 0 and 1 mm.");
    setHoleAllowance(std::clamp(*parsed.millimeters, 0.0, doc::kMaxHoleAllowance));
    return {};
}

} // namespace os::ui
