// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The App Store purchase "Unlimited projects" (ui/Store.h): the free
// version keeps one project in OpenShape's folder, and every action that
// would add another opens the purchase sheet. A stand-in store (no App
// Store here) answers as StoreKit would: a price, a purchase that goes
// through, fails, waits for approval or is cancelled, Restore purchase
// with and without an earlier purchase, and no product at all. The app
// folder is a temporary one, as on an iPhone (--app-folder).
//
// Clicked: New (allowed with no project; gated, Not now; gated, Unlock →
// the new project opens), Home's line, Restore (nothing found, then
// found), a failed purchase, Ask to Buy (approved later), the store
// unreachable, Duplicate, Import STEP, a project and a STEP file from
// another app, Save As under a new name (and the same name, allowed),
// Delete → New allowed, About's Restore; the sheet at phone, iPad, Split
// View and iPhone Duo (outer display) sizes by touch; and the desktop
// without a store: no limit, no purchase UI.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "document/Feature.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "io/ProjectFile.h"
#include "ui/AppController.h"
#include "ui/Purchase.h"
#include "ui/Store.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <memory>
#include <vector>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;
using Kind = ui::StoreEvent::Kind;

// Answers like StoreKit, a moment later (on the next turn of the event loop).
class StandInStore final : public ui::Store, public std::enable_shared_from_this<StandInStore> {
public:
    enum class Outcome { Purchase, Fail, Defer, Cancel };
    bool productAvailable = true;
    Outcome outcome = Outcome::Purchase;
    bool restoreFinds = true;
    int requests = 0;
    int buys = 0;
    int restores = 0;

    void setListener(ui::StoreListener listener) override { listener_ = std::move(listener); }
    void requestProduct() override
    {
        ++requests;
        if (productAvailable)
            post(Kind::ProductLoaded, QStringLiteral("$9.99"));
        else
            post(Kind::ProductUnavailable);
    }
    void buy() override
    {
        ++buys;
        switch (outcome) {
        case Outcome::Purchase:
            post(Kind::Purchasing);
            post(Kind::Purchased);
            return;
        case Outcome::Fail:
            post(Kind::Purchasing);
            post(Kind::Failed, {}, QStringLiteral("Payment declined (stand-in store)"));
            return;
        case Outcome::Defer:
            post(Kind::Purchasing);
            post(Kind::Deferred);
            return;
        case Outcome::Cancel:
            post(Kind::Cancelled);
            return;
        }
    }
    void restore() override
    {
        ++restores;
        if (restoreFinds)
            post(Kind::Restored);
        post(Kind::RestoreFinished);
    }
    // Ask to Buy approved later: the purchase arrives without being asked for.
    void approve() { post(Kind::Purchased); }

private:
    void post(Kind kind, const QString& price = {}, const QString& message = {})
    {
        ui::StoreEvent event;
        event.kind = kind;
        event.productId = ui::unlockProductId();
        event.price = price;
        event.message = message;
        QTimer::singleShot(0, QCoreApplication::instance(), [weak = weak_from_this(), event] {
            if (auto self = weak.lock())
                if (self->listener_)
                    self->listener_(event);
        });
    }
    ui::StoreListener listener_;
};

struct State {
    std::unique_ptr<QTemporaryDir> dir;
    QString folder;     // OpenShape's folder
    QString trash;      // the stand-in trash
    QString elsewhere;  // a project in another app's folder
    QString step;       // a STEP file elsewhere
    std::shared_ptr<StandInStore> store;
    QStringList messages;
    QMetaObject::Connection listening;
    int card = -1; // the Home card whose menu is open
};

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

QString itemText(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item ? item->property("text").toString() : QString();
}

bool enabled(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isEnabled();
}

int projectCount(const std::shared_ptr<State>& s)
{
    return int(ui::appFolderProjects(s->folder).size());
}

QString lastMessage(const std::shared_ptr<State>& s)
{
    return s->messages.isEmpty() ? QString() : s->messages.last();
}

int cardOf(AcceptanceRunner& r, const QString& name)
{
    const QVariantList projects = r.app().homeProjects();
    for (int i = 0; i < projects.size(); ++i)
        if (projects[i].toMap().value(QStringLiteral("name")).toString() == name)
            return i;
    return -1;
}

QString say(const QString& text)
{
    return QStringLiteral("purchase: ") + text;
}

void append(Steps& steps, Steps more)
{
    for (auto& step : more)
        steps.push_back(std::move(step));
}

// File → <item>.
Steps fileMenu(AcceptanceRunner& r, const QString& item, const QString& what)
{
    return {
        [&r] {
            r.app().setHomeVisible(false);
            r.check(r.clickItem(QStringLiteral("fileMenuButton")), say(QStringLiteral("File menu")));
        },
        [&r, item, what] { r.check(r.clickItem(item), say(what)); },
    };
}

// Home, shown (as File → Home does).
Steps home(AcceptanceRunner& r)
{
    return {[&r] { r.app().setHomeVisible(true); }, [] {}};
}

// The sheet is open, waiting for the purchase; nothing else changed.
void expectSheet(AcceptanceRunner& r, const QString& what)
{
    r.check(shown(r, QStringLiteral("purchaseOverlay")) && r.app().purchaseVisible(), say(what + QStringLiteral(": the purchase sheet opens")));
    r.check(itemText(r, QStringLiteral("purchaseTitle")) == QStringLiteral("Unlimited projects"), say(what + QStringLiteral(": its title")),
            itemText(r, QStringLiteral("purchaseTitle")));
}

// "Not now": the sheet closes and nothing happens.
Steps notNow(AcceptanceRunner& r, const std::shared_ptr<State>& s, const QString& what, int projects)
{
    return {
        [&r, what] { r.check(r.clickItem(QStringLiteral("purchaseNotNow")), say(what + QStringLiteral(": Not now"))); },
        [&r, s, what, projects] {
            r.check(!shown(r, QStringLiteral("purchaseOverlay")), say(what + QStringLiteral(": the sheet closes")));
            r.check(projectCount(s) == projects, say(what + QStringLiteral(": still %1 project(s)").arg(projects)),
                    QString::number(projectCount(s)));
            r.check(!r.app().projectsUnlocked(), say(what + QStringLiteral(": still the free version")));
        },
    };
}

bool writeProject(const QString& file, double size)
{
    doc::Document document;
    auto body = std::make_unique<doc::Body>();
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = {0, 0, 0};
    box->size = {size, size, size};
    body->insertFeature(std::move(box), 0);
    document.addBody(std::move(body));
    QDir().mkpath(QFileInfo(file).absolutePath());
    return io::saveProject(document, std::filesystem::path(file.toStdWString())).ok();
}

// The sheet at one window size, by touch: everything on screen, clear of
// the safe area, buttons 44 px tall; Not now closes it.
void addLayout(Steps& steps, AcceptanceRunner& r, const QString& name, int width, int height, const QVariantList& safe)
{
    steps.push_back([&r, width, height, safe] {
        r.app().setTouchMode(true);
        r.resizeWindow(width, height);
        r.window()->setProperty("simulatedSafeArea", safe.isEmpty() ? QVariant() : QVariant(safe));
        r.app().setHomeVisible(true);
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(shown(r, QStringLiteral("homeUnlockLine")), say(name + QStringLiteral(": Home shows the free version's line")));
        QQuickItem* line = r.findItem(QStringLiteral("homeUnlockLine"));
        r.check(line && line->height() >= 44, say(name + QStringLiteral(": the line is a 44 px target")),
                line ? AcceptanceRunner::num(line->height()) : QString());
        r.check(r.tapItem(QStringLiteral("homeUnlockArea")), say(name + QStringLiteral(": tap the line")));
    });
    wait(steps, 2);
    steps.push_back([&r, name, width, height, safe] {
        expectSheet(r, name);
        const double top = safe.isEmpty() ? 0 : safe[0].toDouble();
        const double right = safe.isEmpty() ? 0 : safe[1].toDouble();
        const double bottom = safe.isEmpty() ? 0 : safe[2].toDouble();
        const double left = safe.isEmpty() ? 0 : safe[3].toDouble();
        const QRectF free(left, top, width - left - right, height - top - bottom);
        for (const char* id : {"purchasePanel", "purchaseTitle", "purchaseText", "purchaseUnlock", "purchaseRestore", "purchaseNotNow"}) {
            QQuickItem* item = r.findItem(QString::fromLatin1(id));
            const QRectF rect = item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF();
            r.check(item && item->isVisible() && free.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect),
                    say(name + QStringLiteral(": %1 on screen, clear of the safe area").arg(QLatin1String(id))),
                    QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
        }
        for (const char* id : {"purchaseUnlock", "purchaseRestore", "purchaseNotNow"}) {
            QQuickItem* item = r.findItem(QString::fromLatin1(id));
            r.check(item && item->height() >= 44 && item->width() >= 44, say(name + QStringLiteral(": %1 is a 44 px target").arg(QLatin1String(id))),
                    item ? AcceptanceRunner::num(item->height()) : QString());
        }
        r.check(itemText(r, QStringLiteral("purchaseUnlock")) == QStringLiteral("Unlock for $9.99"),
                say(name + QStringLiteral(": the store's price on the button")), itemText(r, QStringLiteral("purchaseUnlock")));
        // The button is where a finger lands on it (nothing over it).
        QQuickItem* unlock = r.findItem(QStringLiteral("purchaseUnlock"));
        if (unlock) {
            const QPointF center = unlock->mapToScene(QPointF(unlock->width() / 2, unlock->height() / 2));
            r.check(r.itemAt(center) == unlock || (r.itemAt(center) && unlock->isAncestorOf(r.itemAt(center))),
                    say(name + QStringLiteral(": Unlock can be tapped")));
        }
        r.screenshot(QStringLiteral("purchase_sheet_") + name);
        r.check(r.tapItem(QStringLiteral("purchaseNotNow")), say(name + QStringLiteral(": tap Not now")));
    });
    wait(steps, 1);
    steps.push_back([&r, name] { r.check(!shown(r, QStringLiteral("purchaseOverlay")), say(name + QStringLiteral(": closed"))); });
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    Steps steps;

    // ---- A free iPhone: a temporary app folder and the stand-in store.
    steps.push_back([&r, s] {
        s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-purchase-XXXXXX"));
        r.check(s->dir->isValid(), say(QStringLiteral("a temporary sandbox")));
        s->folder = s->dir->path() + QStringLiteral("/Documents");
        s->trash = s->dir->path() + QStringLiteral("/Trash");
        s->elsewhere = s->dir->path() + QStringLiteral("/iCloud Drive/Gift.openshape");
        s->step = s->dir->path() + QStringLiteral("/iCloud Drive/Spacer.step");
        QDir().mkpath(s->folder);
        QDir().mkpath(s->trash);
        QDir().mkpath(QFileInfo(s->elsewhere).absolutePath());
        r.check(writeProject(s->elsewhere, 12), say(QStringLiteral("a project in another app's folder")));
        r.check(geom::exportStep({{"Spacer", geom::makeBox({0, 0, 0}, {20, 20, 3}).value()}}, std::filesystem::path(s->step.toStdWString())).ok(),
                say(QStringLiteral("a STEP file elsewhere")));
        r.app().setAppFolder(s->folder);
        r.app().clearRecentFiles();
        const QString trash = s->trash;
        r.app().setTrashHandler([trash](const QString& path, QString* trashedAt, QString* error) {
            const QString target = trash + QLatin1Char('/') + QFileInfo(path).fileName();
            QFile::remove(target);
            if (QFile::rename(path, target)) {
                *trashedAt = target;
                return true;
            }
            *error = QStringLiteral("stand-in trash failed");
            return false;
        });
        r.app().setProjectsUnlocked(false);
        s->store = std::make_shared<StandInStore>();
        r.app().setStore(s->store);
        s->listening = QObject::connect(&r.app(), &ui::AppController::message, &r.app(), [s](const QString& t) { s->messages << t; });
        r.check(r.app().storeAvailable() && r.app().projectLimited(), say(QStringLiteral("the free version, with a store")));
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(s->store->requests == 1 && r.app().purchasePrice() == QStringLiteral("$9.99"),
                say(QStringLiteral("the price was asked for at start")), r.app().purchasePrice());
    });

    // ---- New with no project: allowed. Save it: the one project.
    append(steps, fileMenu(r, QStringLiteral("newMenuItem"), QStringLiteral("File → New with no project")));
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")), say(QStringLiteral("no project yet: New goes ahead")));
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 2);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("File → Save")));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("saveNamePrompt")), say(QStringLiteral("Save asks for a name")));
        r.type(QStringLiteral("First"));
    });
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("Save")));
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && projectCount(s) == 1 && r.app().documentTitle() == QStringLiteral("First"),
                say(QStringLiteral("the first project is saved, no sheet")), QString::number(projectCount(s)));
    });

    // ---- New again: the sheet; Not now changes nothing.
    append(steps, fileMenu(r, QStringLiteral("newMenuItem"), QStringLiteral("File → New with one project")));
    steps.push_back([&r] {
        expectSheet(r, QStringLiteral("File → New"));
        r.check(itemText(r, QStringLiteral("purchaseUnlock")) == QStringLiteral("Unlock for $9.99") && enabled(r, QStringLiteral("purchaseUnlock")),
                say(QStringLiteral("Unlock for the store's price")), itemText(r, QStringLiteral("purchaseUnlock")));
        r.check(itemText(r, QStringLiteral("purchaseText")).contains(QStringLiteral("one-time purchase")), say(QStringLiteral("it says what the purchase is")));
        r.check(r.app().documentTitle() == QStringLiteral("First") && r.app().bodyCount() == 1, say(QStringLiteral("the project stays open")));
        r.screenshot(QStringLiteral("purchase_new"));
    });
    append(steps, notNow(r, s, QStringLiteral("File → New"), 1));
    steps.push_back([&r] {
        r.check(r.app().documentTitle() == QStringLiteral("First") && r.app().bodyCount() == 1 && !r.app().homeVisible(),
                say(QStringLiteral("Not now: no new project")), r.app().documentTitle());
    });

    // ---- A sample with the one project used: it opens (a sample is not a
    // project of the user's); keeping it under a name of its own would be one.
    append(steps, home(r));
    steps.push_back([&r] {
        r.check(itemText(r, QStringLiteral("homeSampleName_0")) == QStringLiteral("Lamp shade"), say(QStringLiteral("Home: the samples")),
                itemText(r, QStringLiteral("homeSampleName_0")));
        r.check(r.clickItem(QStringLiteral("homeSample_0")), say(QStringLiteral("Home: the Lamp shade sample")));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && r.app().documentTitle() == QStringLiteral("Lamp shade") && !r.app().dirty(),
                say(QStringLiteral("a sample opens with the one project used")), r.app().documentTitle());
        r.check(projectCount(s) == 1, say(QStringLiteral("and it is no project of the user's")), QString::number(projectCount(s)));
    });
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("File → Save the sample")));
    steps.push_back([&r] {
        r.check(itemText(r, QStringLiteral("saveNameField")) == QStringLiteral("Lamp shade"), say(QStringLiteral("Save offers the sample's name")),
                itemText(r, QStringLiteral("saveNameField")));
        r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("Save Lamp shade")));
    });
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("Save a sample"));
        r.check(!QFileInfo::exists(s->folder + QStringLiteral("/Lamp shade.openshape")), say(QStringLiteral("the sample is not saved")));
    });
    append(steps, notNow(r, s, QStringLiteral("Save a sample"), 1));
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("saveNameCancel")), say(QStringLiteral("the name prompt: Cancel"))); });

    // ---- Home → New project → Unlock: bought, and the new project opens.
    append(steps, home(r));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("homeUnlockLine")) && itemText(r, QStringLiteral("homeUnlockLine")).contains(QStringLiteral("one project")),
                say(QStringLiteral("Home: the free version's line")), itemText(r, QStringLiteral("homeUnlockLine")));
        r.check(r.clickItem(QStringLiteral("homeNew")), say(QStringLiteral("Home → New project")));
    });
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("Home → New project"));
        s->store->outcome = StandInStore::Outcome::Purchase;
        r.check(r.clickItem(QStringLiteral("purchaseUnlock")), say(QStringLiteral("Unlock for $9.99")));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(s->store->buys == 1, say(QStringLiteral("one purchase asked of the store")), QString::number(s->store->buys));
        r.check(r.app().projectsUnlocked() && !r.app().projectLimited(), say(QStringLiteral("unlocked")));
        r.check(!shown(r, QStringLiteral("purchaseOverlay")), say(QStringLiteral("the sheet closed")));
        r.check(r.app().documentTitle() == QStringLiteral("Untitled") && r.app().bodyCount() == 0 && !r.app().homeVisible(),
                say(QStringLiteral("then New went ahead: an empty Untitled")), r.app().documentTitle());
        r.check(s->messages.contains(QStringLiteral("Unlimited projects unlocked. Thank you!")), say(QStringLiteral("it says so")), lastMessage(s));
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 2);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("File → Save (unlocked)")));
    steps.push_back([&r] { r.type(QStringLiteral("Second")); });
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("Save Second")));
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && projectCount(s) == 2, say(QStringLiteral("unlocked: a second project")), QString::number(projectCount(s)));
    });
    append(steps, home(r));
    steps.push_back([&r] { r.check(!shown(r, QStringLiteral("homeUnlockLine")), say(QStringLiteral("unlocked: no line on Home"))); });

    // ---- A fresh free state with two projects (as TestFlight testers have):
    // both open, edit and save; Restore finds nothing, then the purchase.
    steps.push_back([&r, s] {
        r.app().setProjectsUnlocked(false);
        s->messages.clear();
        r.check(r.app().projectLimited(), say(QStringLiteral("free again")));
        const int card = cardOf(r, QStringLiteral("First"));
        r.check(card >= 0 && r.clickItem(QStringLiteral("homeCard_%1").arg(card)), say(QStringLiteral("Home: open First")));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && r.app().documentTitle() == QStringLiteral("First"),
                say(QStringLiteral("opening a project is never limited")), r.app().documentTitle());
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 2);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("File → Save an existing project")));
    steps.push_back([&r, s] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && !r.app().dirty() && projectCount(s) == 2,
                say(QStringLiteral("saving an existing project is never limited")));
    });
    append(steps, home(r));
    steps.push_back([&r, s] {
        s->store->restoreFinds = false;
        r.check(r.clickItem(QStringLiteral("homeUnlockArea")), say(QStringLiteral("Home: the free version's line")));
    });
    steps.push_back([&r] {
        expectSheet(r, QStringLiteral("Home's line"));
        r.check(r.clickItem(QStringLiteral("purchaseRestore")), say(QStringLiteral("Restore purchase (none made)")));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(s->store->restores == 1, say(QStringLiteral("the store was asked to restore")));
        r.check(shown(r, QStringLiteral("purchaseOverlay")) && !r.app().projectsUnlocked(), say(QStringLiteral("nothing found: still free, the sheet stays")));
        r.check(itemText(r, QStringLiteral("purchaseStatusText")).contains(QStringLiteral("No earlier purchase")),
                say(QStringLiteral("it says none was found")), itemText(r, QStringLiteral("purchaseStatusText")));
        s->store->restoreFinds = true;
        r.check(r.clickItem(QStringLiteral("purchaseRestore")), say(QStringLiteral("Restore purchase (made earlier)")));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(r.app().projectsUnlocked() && !shown(r, QStringLiteral("purchaseOverlay")), say(QStringLiteral("restored: unlocked, the sheet closed")));
        r.check(s->messages.contains(QStringLiteral("Purchase restored: unlimited projects")), say(QStringLiteral("it says so")), lastMessage(s));
        r.check(r.app().homeVisible(), say(QStringLiteral("nothing waited: Home stays")));
    });

    // ---- A purchase that fails, then one waiting for approval (Ask to Buy).
    steps.push_back([&r, s] {
        r.app().setProjectsUnlocked(false);
        s->store->outcome = StandInStore::Outcome::Fail;
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeUnlockArea")), say(QStringLiteral("Home's line (to fail)"))); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("purchaseUnlock")), say(QStringLiteral("Unlock (declined)"))); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("purchaseOverlay")) && !r.app().projectsUnlocked(), say(QStringLiteral("failed: still free, the sheet stays")));
        r.check(itemText(r, QStringLiteral("purchaseStatusText")) == QStringLiteral("The purchase did not go through: Payment declined (stand-in store)"),
                say(QStringLiteral("it says why")), itemText(r, QStringLiteral("purchaseStatusText")));
        r.check(enabled(r, QStringLiteral("purchaseUnlock")), say(QStringLiteral("Unlock can be tapped again")));
        r.screenshot(QStringLiteral("purchase_failed"));
    });
    append(steps, notNow(r, s, QStringLiteral("failed purchase"), 2));
    // Ask to Buy, from File → New: the approval comes later; New, dropped
    // with Not now, does not run then.
    steps.push_back([s] { s->store->outcome = StandInStore::Outcome::Defer; });
    append(steps, fileMenu(r, QStringLiteral("newMenuItem"), QStringLiteral("File → New (Ask to Buy)")));
    steps.push_back([&r] {
        expectSheet(r, QStringLiteral("File → New (Ask to Buy)"));
        r.check(r.clickItem(QStringLiteral("purchaseUnlock")), say(QStringLiteral("Unlock (needs approval)")));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().purchaseStatus() == QStringLiteral("waiting") && !r.app().projectsUnlocked(), say(QStringLiteral("waiting for approval")),
                r.app().purchaseStatus());
        r.check(itemText(r, QStringLiteral("purchaseStatusText")).contains(QStringLiteral("Waiting for approval")),
                say(QStringLiteral("it says it waits")), itemText(r, QStringLiteral("purchaseStatusText")));
        r.screenshot(QStringLiteral("purchase_waiting"));
    });
    append(steps, notNow(r, s, QStringLiteral("Ask to Buy"), 2));
    steps.push_back([s] {
        s->messages.clear();
        s->store->approve();
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(r.app().projectsUnlocked(), say(QStringLiteral("approved later: unlocked")));
        r.check(r.app().documentTitle() == QStringLiteral("First") && !shown(r, QStringLiteral("purchaseOverlay")),
                say(QStringLiteral("New, dropped with Not now, did not run")), r.app().documentTitle());
        r.check(lastMessage(s) == QStringLiteral("Unlimited projects unlocked. Thank you!"), say(QStringLiteral("it says so")), lastMessage(s));
    });

    // ---- The App Store cannot be reached: Unlock off, a line says why.
    steps.push_back([&r] {
        r.app().setProjectsUnlocked(false);
        auto offline = std::make_shared<StandInStore>();
        offline->productAvailable = false;
        r.app().setStore(offline);
        r.app().setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeUnlockArea")), say(QStringLiteral("Home's line (offline)"))); });
    wait(steps, 1);
    steps.push_back([&r] {
        expectSheet(r, QStringLiteral("offline"));
        r.check(r.app().purchaseStatus() == QStringLiteral("unavailable"), say(QStringLiteral("no product")), r.app().purchaseStatus());
        r.check(!enabled(r, QStringLiteral("purchaseUnlock")) && itemText(r, QStringLiteral("purchaseUnlock")) == QStringLiteral("Unlock"),
                say(QStringLiteral("Unlock is off, without a price")), itemText(r, QStringLiteral("purchaseUnlock")));
        r.check(itemText(r, QStringLiteral("purchaseStatusText")).contains(QStringLiteral("App Store can't be reached")),
                say(QStringLiteral("a line says the App Store can't be reached")), itemText(r, QStringLiteral("purchaseStatusText")));
        r.screenshot(QStringLiteral("purchase_unavailable"));
    });
    append(steps, notNow(r, s, QStringLiteral("offline"), 2));
    steps.push_back([&r, s] { r.app().setStore(s->store); });

    // ---- Delete Second: one project left, then the actions that would add one.
    steps.push_back([&r, s] {
        s->card = cardOf(r, QStringLiteral("Second"));
        r.check(s->card >= 0 && r.clickItem(QStringLiteral("homeCardMenu_%1").arg(s->card)), say(QStringLiteral("Second's ⋯ menu")));
    });
    wait(steps, 1);
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("homeDelete_%1").arg(s->card)), say(QStringLiteral("Delete… Second"))); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete"))); });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(projectCount(s) == 1 && !r.app().projectsUnlocked(), say(QStringLiteral("one project left")), QString::number(projectCount(s)));
        s->card = cardOf(r, QStringLiteral("First"));
        r.check(s->card >= 0 && r.clickItem(QStringLiteral("homeCardMenu_%1").arg(s->card)), say(QStringLiteral("First's ⋯ menu")));
    });
    wait(steps, 1);
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("homeDuplicate_%1").arg(s->card)), say(QStringLiteral("Duplicate"))); });
    wait(steps, 1);
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("Duplicate"));
        r.check(!QFileInfo::exists(s->folder + QStringLiteral("/First copy.openshape")), say(QStringLiteral("no copy was made")));
    });
    append(steps, notNow(r, s, QStringLiteral("Duplicate"), 1));
    // Import STEP from Home: asked before the file is chosen. (Should it go
    // ahead, the prepared file is imported, never a native dialog.)
    steps.push_back([&r, s] {
        r.app().setNextFileChoice(QUrl::fromLocalFile(s->step));
        r.check(r.clickItem(QStringLiteral("homeImport")), say(QStringLiteral("Home → Import STEP")));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        expectSheet(r, QStringLiteral("Import STEP"));
        r.check(r.app().takeNextFileChoice().isValid(), say(QStringLiteral("the file was not even chosen")));
    });
    append(steps, notNow(r, s, QStringLiteral("Import STEP"), 1));
    steps.push_back([&r] { r.check(r.app().homeVisible() && r.app().documentTitle() == QStringLiteral("First"), say(QStringLiteral("Home stays, First open"))); });
    // A project and a STEP file from another app.
    steps.push_back([s] { QWindowSystemInterface::handleFileOpenEvent(QUrl::fromLocalFile(s->elsewhere)); });
    wait(steps, 1);
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("a project from another app"));
        r.check(!QFileInfo::exists(s->folder + QStringLiteral("/Gift.openshape")), say(QStringLiteral("nothing was copied in")));
    });
    append(steps, notNow(r, s, QStringLiteral("a project from another app"), 1));
    steps.push_back([s] { QWindowSystemInterface::handleFileOpenEvent(QUrl::fromLocalFile(s->step)); });
    wait(steps, 1);
    steps.push_back([&r] { expectSheet(r, QStringLiteral("a STEP file from another app")); });
    append(steps, notNow(r, s, QStringLiteral("a STEP file from another app"), 1));
    steps.push_back([&r] { r.check(r.app().documentTitle() == QStringLiteral("First"), say(QStringLiteral("First stays open")), r.app().documentTitle()); });
    // The project already in OpenShape's folder, from another app: just opens.
    steps.push_back([&r, s] {
        r.app().setHomeVisible(true);
        QWindowSystemInterface::handleFileOpenEvent(QUrl::fromLocalFile(s->folder + QStringLiteral("/First.openshape")));
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && !r.app().homeVisible() && r.app().documentTitle() == QStringLiteral("First"),
                say(QStringLiteral("the project already in the folder opens")));
    });
    // Save As under a new name: the sheet over the name prompt; the same name replaces.
    append(steps, fileMenu(r, QStringLiteral("saveAsMenuItem"), QStringLiteral("File → Save As")));
    steps.push_back([&r] { r.type(QStringLiteral("Other name")); });
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("Save As Other name"))); });
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("Save As a new name"));
        r.check(shown(r, QStringLiteral("saveNamePrompt")), say(QStringLiteral("the name prompt waits under the sheet")));
        r.check(!QFileInfo::exists(s->folder + QStringLiteral("/Other name.openshape")), say(QStringLiteral("nothing saved")));
        r.screenshot(QStringLiteral("purchase_save_as"));
    });
    append(steps, notNow(r, s, QStringLiteral("Save As"), 1));
    steps.push_back([&r] {
        // Back to the project's own name (the prompt is still open).
        QQuickItem* field = r.findItem(QStringLiteral("saveNameField"));
        r.check(field && field->setProperty("text", QStringLiteral("First")), say(QStringLiteral("the name field: First")));
    });
    steps.push_back([&r, s] {
        r.check(itemText(r, QStringLiteral("saveNameSave")) == QStringLiteral("Replace"), say(QStringLiteral("its own name: Replace")),
                itemText(r, QStringLiteral("saveNameSave")));
        r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("Replace First")));
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && !shown(r, QStringLiteral("saveNamePrompt")) && projectCount(s) == 1,
                say(QStringLiteral("saved under its own name: never limited")));
    });

    // ---- Delete the one project: New is allowed again.
    append(steps, home(r));
    steps.push_back([&r, s] {
        s->card = cardOf(r, QStringLiteral("First"));
        r.check(s->card >= 0 && r.clickItem(QStringLiteral("homeCardMenu_%1").arg(s->card)), say(QStringLiteral("First's ⋯ menu")));
    });
    wait(steps, 1);
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("homeDelete_%1").arg(s->card)), say(QStringLiteral("Delete… First"))); });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("homeDeleteOk")), say(QStringLiteral("Delete First"))); });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(projectCount(s) == 0, say(QStringLiteral("no project left")), QString::number(projectCount(s)));
        r.check(r.clickItem(QStringLiteral("homeNew")), say(QStringLiteral("Home → New project after deleting")));
    });
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && !r.app().homeVisible() && r.app().documentTitle() == QStringLiteral("Untitled"),
                say(QStringLiteral("deleting the project allows a new one")));
    });

    // ---- About: the purchase row; its Restore.
    append(steps, fileMenu(r, QStringLiteral("aboutMenuItem"), QStringLiteral("About OpenShape")));
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("aboutUnlimited")) && itemText(r, QStringLiteral("aboutUnlimitedText")).startsWith(QStringLiteral("Unlimited projects: not unlocked")),
                say(QStringLiteral("About: the purchase row")), itemText(r, QStringLiteral("aboutUnlimitedText")));
        r.check(shown(r, QStringLiteral("aboutUnlock")) && shown(r, QStringLiteral("aboutRestore")), say(QStringLiteral("About: Unlock… and Restore purchase")));
        r.screenshot(QStringLiteral("purchase_about"));
        r.check(r.clickItem(QStringLiteral("aboutRestore")), say(QStringLiteral("About → Restore purchase")));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().projectsUnlocked() && !shown(r, QStringLiteral("purchaseOverlay")), say(QStringLiteral("About's Restore unlocked")));
        r.check(itemText(r, QStringLiteral("aboutUnlimitedText")) == QStringLiteral("Unlimited projects: unlocked") && !shown(r, QStringLiteral("aboutRestore")),
                say(QStringLiteral("About says unlocked")), itemText(r, QStringLiteral("aboutUnlimitedText")));
        r.key(Qt::Key_Escape);
    });

    // ---- The sheet at iOS sizes, by touch; New gated and bought by finger on a phone.
    steps.push_back([&r, s] {
        r.app().setProjectsUnlocked(false);
        r.check(writeProject(s->folder + QStringLiteral("/Phone part.openshape"), 8), say(QStringLiteral("one project for the layouts")));
    });
    addLayout(steps, r, QStringLiteral("iphone"), 402, 874, {62, 0, 34, 0});
    addLayout(steps, r, QStringLiteral("iphone_landscape"), 874, 402, {0, 62, 21, 62});
    addLayout(steps, r, QStringLiteral("small_iphone"), 375, 667, {20, 0, 0, 0});
    addLayout(steps, r, QStringLiteral("ipad"), 1180, 820, {24, 0, 20, 0});
    addLayout(steps, r, QStringLiteral("ipad_split"), 500, 800, {24, 0, 20, 0});
    addLayout(steps, r, QStringLiteral("duo_outer"), 466, 678, {59, 0, 34, 0});
    addLayout(steps, r, QStringLiteral("duo_split"), 440, 669, {0, 0, 20, 0});
    steps.push_back([&r] {
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
        r.app().setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("homeNew")), say(QStringLiteral("phone: tap New project"))); });
    wait(steps, 1);
    steps.push_back([&r, s] {
        expectSheet(r, QStringLiteral("phone: New project"));
        s->store->outcome = StandInStore::Outcome::Purchase; // (Ask to Buy before)
        r.check(r.tapItem(QStringLiteral("purchaseUnlock")), say(QStringLiteral("phone: tap Unlock for $9.99")));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().projectsUnlocked() && !r.app().homeVisible() && r.app().documentTitle() == QStringLiteral("Untitled"),
                say(QStringLiteral("phone: bought, the new project is open")));
    });

    // ---- The desktop: no store, no limit, no purchase UI.
    steps.push_back([&r] {
        r.app().setTouchMode(false);
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setProjectsUnlocked(false);
        r.app().setStore(nullptr);
        r.check(!r.app().storeAvailable() && !r.app().projectLimited() && r.app().mayAddProject(), say(QStringLiteral("desktop: no store, no limit")));
        r.app().setHomeVisible(true);
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(projectCount(s) >= 1, say(QStringLiteral("desktop: a project in the folder")), QString::number(projectCount(s)));
        r.check(!shown(r, QStringLiteral("homeUnlockLine")), say(QStringLiteral("desktop: no line on Home")));
        r.check(r.clickItem(QStringLiteral("homeNew")), say(QStringLiteral("desktop: Home → New project")));
    });
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("purchaseOverlay")) && !r.app().homeVisible(), say(QStringLiteral("desktop: New goes ahead")));
        r.app().offerUnlock(); // nothing to offer
        r.check(!r.app().purchaseVisible(), say(QStringLiteral("desktop: no sheet even when asked")));
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 2);
    append(steps, fileMenu(r, QStringLiteral("saveMenuItem"), QStringLiteral("desktop: File → Save")));
    steps.push_back([&r] { r.type(QStringLiteral("Desktop two")); });
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("saveNameSave")), say(QStringLiteral("desktop: Save a second project")));
        r.check(QFileInfo::exists(s->folder + QStringLiteral("/Desktop two.openshape")) && !shown(r, QStringLiteral("purchaseOverlay")),
                say(QStringLiteral("desktop: saved, no sheet")));
    });
    append(steps, fileMenu(r, QStringLiteral("aboutMenuItem"), QStringLiteral("desktop: About OpenShape")));
    steps.push_back([&r, s] {
        r.check(shown(r, QStringLiteral("aboutOverlay")) && !shown(r, QStringLiteral("aboutUnlimited")), say(QStringLiteral("desktop: no purchase row in About")));
        r.key(Qt::Key_Escape);
        QObject::disconnect(s->listening);
        r.app().setAppFolder({});
        r.app().newDocument();
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("purchase"), 96, steps});

} // namespace
} // namespace os::app
