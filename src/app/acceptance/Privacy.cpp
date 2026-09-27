// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The About card links the privacy policy (docs/PRIVACY.md on GitHub), as
// the App Store asks of every app (guideline 5.1.1: a link in App Store
// Connect and inside the app). The link is clicked for real, in the desktop
// and the phone layout; a URL handler catches what it opens, so the run
// never starts a browser.

#include "app/AcceptanceRunner.h"

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
// names the layout in the check descriptions.
void addClickThrough(std::vector<AcceptanceRunner::Step>& steps, AcceptanceRunner& r, const QString& where)
{
    auto catcher = std::make_shared<PrivacyUrlCatcher>();
    auto say = [where](const char* text) { return QStringLiteral("privacy (") + where + QStringLiteral("): ") + QLatin1String(text); };
    steps.push_back([&r, say] { r.check(r.clickItem(QStringLiteral("fileMenuButton")), say("File menu")); });
    steps.push_back([] {});
    steps.push_back([&r, say] { r.check(r.clickItem(QStringLiteral("aboutMenuItem")), say("About OpenShape")); });
    steps.push_back([] {});
    steps.push_back([&r, say, catcher] {
        QQuickItem* link = r.findItem(QStringLiteral("aboutPrivacyLink"));
        r.check(link && link->isVisible(), say("the About card has a privacy policy line"));
        r.check(link && link->property("url").toString() == kPrivacyUrl, say("it links docs/PRIVACY.md on GitHub"),
                link ? link->property("url").toString() : QString());
        r.check(link && link->property("text").toString().contains(QStringLiteral("collects no data")),
                say("it says in a few words what the policy says"), link ? link->property("text").toString() : QString());
        QDesktopServices::setUrlHandler(QStringLiteral("https"), catcher.get(), "open");
        r.check(r.clickItem(QStringLiteral("aboutPrivacyLink")), say("click the privacy policy link"));
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
    addClickThrough(all, r, QStringLiteral("desktop"));
    // The phone layout (an iPhone held upright): the card is narrower and
    // the link wraps; it must still be on screen and open the policy.
    all.push_back([&r] { r.resizeWindow(402, 874); });
    all.push_back([] {});
    all.push_back([] {});
    addClickThrough(all, r, QStringLiteral("phone"));
    return all;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("privacy"), 106, steps});

} // namespace
} // namespace os::app

#include "Privacy.moc"
