#include "ui/AppController.h"

#include "core/Log.h"
#include "geometry/Exchange.h"
#include "io/ProjectFile.h"

#include <QtCore/QFileInfo>
#include <QtCore/QVariantMap>

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

} // namespace

AppController::AppController(QObject* parent)
    : QObject(parent), document_(std::make_unique<doc::Document>()), undoStack_(std::make_unique<cmd::UndoStack>()),
      interaction_(std::make_unique<interact::InteractionController>(*document_, *undoStack_))
{
    attach();
    interaction_->fitAll(false);
}

AppController::~AppController() = default;

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
    emit message(text);
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
    return interaction_->operation() && interaction_->operation()->value() != 0.0;
}

QPointF AppController::valueLabelPosition() const
{
    const auto p = interaction_->valueLabelPosition();
    return p ? QPointF(p->x, p->y) : QPointF();
}

bool AppController::valueLabelVisible() const { return interaction_->valueLabelPosition().has_value(); }

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
    return s && s->isDrawing();
}

QVariantList AppController::sketchLabels() const
{
    QVariantList list;
    const auto* s = interaction_->sketchSession();
    if (!s)
        return list;
    for (const auto& label : s->labels(interaction_->camera())) {
        QVariantMap map;
        map.insert(QStringLiteral("kind"), label.kind == interact::SketchLabel::Kind::Dimension ? QStringLiteral("dimension")
                                           : label.kind == interact::SketchLabel::Kind::Input   ? QStringLiteral("input")
                                                                                                : QStringLiteral("hint"));
        map.insert(QStringLiteral("key"), q(label.key));
        map.insert(QStringLiteral("constraint"), int(label.constraint));
        map.insert(QStringLiteral("text"), q(label.text));
        map.insert(QStringLiteral("x"), label.screen.x);
        map.insert(QStringLiteral("y"), label.screen.y);
        map.insert(QStringLiteral("focused"), label.focused);
        map.insert(QStringLiteral("locked"), label.locked);
        list.append(map);
    }
    return list;
}

bool AppController::canStartSketch() const
{
    if (interaction_->sketchSession())
        return false;
    const auto& sel = interaction_->selection();
    // Nothing selected: sketch on the ground plane. One flat face: sketch on it.
    return sel.empty() || (sel.size() == 1 && sel.items().front().kind == sel::SelectionKind::Face);
}

void AppController::startSketch()
{
    const Status status = interaction_->startSketch();
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

// ---- Files ------------------------------------------------------------------------------

void AppController::newDocument()
{
    auto document = std::make_unique<doc::Document>();
    auto stack = std::make_unique<cmd::UndoStack>();
    interaction_->setDocument(*document, *stack);
    document_ = std::move(document);
    undoStack_ = std::move(stack);
    path_.clear();
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
    const Status status = io::saveProject(*document_, std::filesystem::path(path_.toStdWString()));
    if (!status) {
        OS_LOG(Warning, File) << status.developerMessage();
        notifyMessage(q(status.userMessage()));
        return false;
    }
    undoStack_->setClean();
    notifyMessage(QStringLiteral("Saved"));
    emit stateChanged();
    return true;
}

bool AppController::saveProjectAs(const QUrl& url)
{
    const auto path = withExtension(toPath(url), io::kProjectExtension);
    path_ = QString::fromStdWString(path.wstring());
    emit documentChanged();
    return saveProject();
}

bool AppController::exportStep(const QUrl& url)
{
    std::vector<geom::NamedShape> shapes;
    for (const auto& body : document_->bodies())
        if (body->isVisible() && !body->shape().isNull())
            shapes.push_back({body->name(), body->shape()});
    const Status status = geom::exportStep(shapes, withExtension(toPath(url), ".step"));
    notifyMessage(status ? QStringLiteral("Exported STEP") : q(status.userMessage()));
    return status.ok();
}

bool AppController::exportStl(const QUrl& url)
{
    std::vector<geom::NamedShape> shapes;
    for (const auto& body : document_->bodies())
        if (body->isVisible() && !body->shape().isNull())
            shapes.push_back({body->name(), body->shape()});
    const Status status = geom::exportStl(shapes, withExtension(toPath(url), ".stl"));
    notifyMessage(status ? QStringLiteral("Exported STL") : q(status.userMessage()));
    return status.ok();
}

// ---- Actions ----------------------------------------------------------------------------

void AppController::createBox(double size)
{
    (void)interaction_->createBox(size);
}

void AppController::undo() { interaction_->undo(); }
void AppController::redo() { interaction_->redo(); }

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

} // namespace os::ui
