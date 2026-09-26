// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The help card links the user guide (docs/USER_GUIDE.md on GitHub). The
// link is clicked for real; a URL handler catches what it opens, so the run
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
class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void open(const QUrl& url) { opened.append(url); }
};

const QString kGuideUrl = QStringLiteral("https://github.com/SamuelAirs/openshape/blob/main/docs/USER_GUIDE.md");

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto catcher = std::make_shared<UrlCatcher>();
    return {
        [&r] { r.check(r.clickItem(QStringLiteral("helpButton")), "guide: the ? button opens the help card"); },
        [] {},
        [&r, catcher] {
            QQuickItem* link = r.findItem(QStringLiteral("helpGuideLink"));
            r.check(link && link->isVisible(), "the help card has a line linking the user guide");
            r.check(link && link->property("url").toString() == kGuideUrl, "it links docs/USER_GUIDE.md on GitHub",
                    link ? link->property("url").toString() : QString());
            QDesktopServices::setUrlHandler(QStringLiteral("https"), catcher.get(), "open");
            r.check(r.clickItem(QStringLiteral("helpGuideLink")), "click the user guide link");
        },
        [] {},
        [&r, catcher] {
            QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
            r.check(catcher->opened.size() == 1 && catcher->opened.front() == QUrl(kGuideUrl),
                    "clicking it opens the user guide",
                    catcher->opened.isEmpty() ? QStringLiteral("nothing opened") : catcher->opened.front().toString());
            QQuickItem* help = r.findItem(QStringLiteral("helpOverlay"));
            r.check(help && help->isVisible(), "the help card stays open after following the link");
            r.screenshot(QStringLiteral("userguide_help_card"));
            r.key(Qt::Key_Escape);
        },
        [&r] {
            QQuickItem* help = r.findItem(QStringLiteral("helpOverlay"));
            r.check(help && !help->isVisible(), "Esc closes the help card");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("userguide"), 105, steps});

} // namespace
} // namespace os::app

#include "UserGuide.moc"
