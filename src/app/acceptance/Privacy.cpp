// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The About card links the privacy policy (docs/PRIVACY.md on GitHub), as
// the App Store asks of every app (guideline 5.1.1: a link in App Store
// Connect and inside the app). The link is clicked for real, in the desktop
// and the phone layout; a URL handler catches what it opens, so the run
// never starts a browser.

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtCore/QObject>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>
#include <QtQuick/QQuickItem>

#include <memory>

namespace os::app {
namespace {

// Receives the URLs QDesktopServices::openUrl (Qt.openUrlExternally) would open.
class PrivacyUrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void open(const QUrl& url) { opened.append(url); }
};

const QString kPrivacyUrl = QStringLiteral("https://github.com/SamuelAirs/openshape/blob/main/docs/PRIVACY.md");

// File -> About, the link checked and clicked, About closed again; `where`
// names the layout in the check descriptions. With `touch` everything is
// tapped with one finger, and the link is tapped near its top edge, above
// its letters: a finger's target is the whole 44 px row.
void addClickThrough(std::vector<AcceptanceRunner::Step>& steps, AcceptanceRunner& r, const QString& where, bool touch)
{
    auto catcher = std::make_shared<PrivacyUrlCatcher>();
    auto say = [where](const char* text) { return QStringLiteral("privacy (") + where + QStringLiteral("): ") + QLatin1String(text); };
    auto press = [&r, touch](const QString& name) { return touch ? r.tapItem(name) : r.clickItem(name); };
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("fileMenuButton")), say("File menu")); });
    steps.push_back([] {});
    steps.push_back([&r, say, press] { r.check(press(QStringLiteral("aboutMenuItem")), say("About OpenShape")); });
    steps.push_back([] {});
    steps.push_back([&r, say, catcher, touch] {
        QQuickItem* link = r.findItem(QStringLiteral("aboutPrivacyLink"));
        r.check(link && link->isVisible(), say("the About card has a privacy policy line"));
        r.check(link && link->property("url").toString() == kPrivacyUrl, say("it links docs/PRIVACY.md on GitHub"),
                link ? link->property("url").toString() : QString());
        r.check(link && link->property("text").toString().contains(QStringLiteral("collects no data")),
                say("it says in a few words what the policy says"), link ? link->property("text").toString() : QString());
        QDesktopServices::setUrlHandler(QStringLiteral("https"), catcher.get(), "open");
        if (!touch) {
            r.check(r.clickItem(QStringLiteral("aboutPrivacyLink")), say("click the privacy policy link"));
            return;
        }
        if (!link)
            return;
        const double glyphs = link->property("contentHeight").toDouble();
        r.check(link->height() >= 44.0, say("the link is a 44 px touch target"), r.num(link->height()));
        r.check(link->width() >= link->parentItem()->width() - 1.0, say("the link row is as wide as the card"),
                r.num(link->width()) + QStringLiteral(" of ") + r.num(link->parentItem()->width()));
        // 3 px below the row's top: above the letters (centered in the row).
        const QPointF p = link->mapToScene(QPointF(link->width() / 2, 3.0));
        r.check(3.0 < (link->height() - glyphs) / 2, say("the tap lands above the letters"),
                QStringLiteral("letters ") + r.num(glyphs) + QStringLiteral(" of ") + r.num(link->height()));
        QQuickItem* hit = r.itemAt(p);
        r.check(hit && hit->objectName() == QStringLiteral("aboutPrivacyLinkArea"), say("a finger there reaches the link"),
                hit ? hit->objectName() + QLatin1Char(' ') + QString::fromLatin1(hit->metaObject()->className()) : QString());
        r.touchTap({p});
    });
    steps.push_back([] {});
    steps.push_back([&r, say, catcher, where] {
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        r.check(catcher->opened.size() == 1 && catcher->opened.front() == QUrl(kPrivacyUrl), say("clicking it opens the privacy policy"),
                catcher->opened.isEmpty() ? QStringLiteral("nothing opened") : catcher->opened.front().toString());
        QQuickItem* about = r.findItem(QStringLiteral("aboutOverlay"));
        r.check(about && about->isVisible(), say("the About card stays open after following the link"));
        r.screenshot(QStringLiteral("privacy_about_") + where);
        r.key(Qt::Key_Escape);
    });
    steps.push_back([&r, say] {
        QQuickItem* about = r.findItem(QStringLiteral("aboutOverlay"));
        r.check(about && !about->isVisible(), say("Esc closes the About card"));
    });
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    std::vector<AcceptanceRunner::Step> all;
    addClickThrough(all, r, QStringLiteral("desktop"), false);
    // The phone layout (an iPhone held upright, used by touch): the card is
    // narrower; the link must still be on screen and open the policy.
    all.push_back([&r] {
        r.resizeWindow(402, 874);
        r.app().setTouchMode(true);
    });
    all.push_back([] {});
    all.push_back([] {});
    addClickThrough(all, r, QStringLiteral("phone"), true);
    all.push_back([&r] { r.app().setTouchMode(false); });
    return all;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("privacy"), 106, steps});

} // namespace
} // namespace os::app

#include "Privacy.moc"
