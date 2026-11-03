// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Home's Samples (samples/Samples.h, resources/samples): five cards with
// their previews; each opens by a tap as an unsaved copy named after it,
// every step rebuilt, the Model panel listing its steps. Changes to one are
// asked about before the next opens, and never reach the sample itself;
// Save offers the sample's name and keeps a project of the user's (in the
// app folder, as on an iPhone). The free app's limit: purchase scenario.

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtQuick/QQuickItem>

#include <memory>

namespace os::app {
namespace {

struct Expected {
    const char* name;
    int bodies;
};
constexpr Expected kSamples[] = {
    {"Lamp shade", 2}, {"Twisted vase", 1}, {"Electronics box", 2}, {"Phone stand", 1}, {"Honeycomb organizer", 1}};

struct SamplesState {
    std::unique_ptr<QTemporaryDir> dir;
    QStringList messages;
    QMetaObject::Connection listening;
};

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

// The sample's copy in the samples folder is still the one built into the app.
bool copyUntouched(AcceptanceRunner& r, const QString& name)
{
    const QString file = name + QStringLiteral(".openshape");
    QFile builtIn(QStringLiteral(":/openshape/samples/") + file);
    QFile copy(QDir(r.app().samplesFolder()).filePath(file));
    return builtIn.open(QIODevice::ReadOnly) && copy.open(QIODevice::ReadOnly) && builtIn.readAll() == copy.readAll();
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<SamplesState>();
    std::vector<AcceptanceRunner::Step> out;
    out.push_back([&r, s] {
        s->listening = QObject::connect(&r.app(), &ui::AppController::message, &r.app(), [s](const QString& t) { s->messages << t; });
        r.app().newDocument();
        r.app().setHomeVisible(true);
    });
    out.push_back([] {}); // the previews load
    out.push_back([&r] {
        r.check(r.app().homeSamples().size() == 5, "samples: five on Home", QString::number(r.app().homeSamples().size()));
        for (int i = 0; i < 5; ++i) {
            QQuickItem* name = r.findItem(QStringLiteral("homeSampleName_%1").arg(i));
            r.check(shown(r, QStringLiteral("homeSample_%1").arg(i)) && name
                        && name->property("text").toString() == QString::fromLatin1(kSamples[i].name),
                    QStringLiteral("samples: card %1 is %2").arg(i).arg(QString::fromLatin1(kSamples[i].name)),
                    name ? name->property("text").toString() : QString());
            QQuickItem* preview = r.findItem(QStringLiteral("homeSampleThumbnail_%1").arg(i));
            r.check(preview && preview->property("status").toInt() == 1, // Image.Ready
                    QStringLiteral("samples: %1 has its preview").arg(QString::fromLatin1(kSamples[i].name)));
        }
        r.screenshot(QStringLiteral("samples_home"));
    });
    for (int i = 0; i < 5; ++i) {
        const QString name = QString::fromLatin1(kSamples[i].name);
        const int bodies = kSamples[i].bodies;
        out.push_back([&r, s, i, name] {
            r.app().setHomeVisible(true);
            s->messages.clear();
            r.check(r.clickItem(QStringLiteral("homeSample_%1").arg(i)), QStringLiteral("samples: tap %1").arg(name));
        });
        out.push_back([] {});
        out.push_back([&r, i] {
            if (i == 1) {
                // The lamp shade was changed: asked about first.
                r.check(shown(r, QStringLiteral("unsavedDialog")), "samples: a changed sample is asked about");
                r.check(r.clickItem(QStringLiteral("unsavedDiscard")), "samples: Don't Save");
            }
        });
        out.push_back([] {});
        out.push_back([&r, s, name, bodies] {
            r.check(!r.app().homeVisible() && r.app().documentTitle() == name, QStringLiteral("samples: %1 is open").arg(name),
                    r.app().documentTitle());
            r.check(r.app().bodyCount() == bodies, QStringLiteral("samples: %1 has %2 bodies").arg(name).arg(bodies),
                    QString::number(r.app().bodyCount()));
            r.check(!r.app().dirty() && !r.app().hasProjectPath(), QStringLiteral("samples: %1 is an unsaved copy, unchanged").arg(name));
            r.check(r.app().history().size() >= 3, QStringLiteral("samples: %1's steps in the Model panel").arg(name),
                    QString::number(r.app().history().size()));
            r.check(!s->messages.join(QLatin1Char('\n')).contains(QStringLiteral("could not be rebuilt")),
                    QStringLiteral("samples: %1 rebuilds every step").arg(name), s->messages.join(QStringLiteral(" | ")));
            r.screenshot(QStringLiteral("samples_") + name.toLower().replace(QLatin1Char(' '), QLatin1Char('_')));
        });
        if (i == 0) {
            // A change to the lamp shade (a box): the copy has changes, the sample none.
            out.push_back([&r] { r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b")); });
            out.push_back([] {});
            out.push_back([&r] {
                r.check(r.app().dirty() && r.app().bodyCount() == 3 && r.app().documentTitle() == QStringLiteral("Lamp shade"),
                        "samples: the copy changed (a box added), still named Lamp shade");
            });
        }
    }
    // Save keeps the open sample as a project of the user's, under its name.
    out.push_back([&r, s] {
        s->dir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-samples-XXXXXX"));
        r.check(s->dir->isValid(), "samples: a temporary app folder");
        r.app().setAppFolder(s->dir->path());
        r.check(r.clickItem(QStringLiteral("fileMenuButton")), "samples: File menu");
    });
    out.push_back([&r] { r.check(r.clickItem(QStringLiteral("saveMenuItem")), "samples: File → Save"); });
    out.push_back([&r] {
        QQuickItem* field = r.findItem(QStringLiteral("saveNameField"));
        r.check(shown(r, QStringLiteral("saveNamePrompt")) && field
                    && field->property("text").toString() == QStringLiteral("Honeycomb organizer"),
                "samples: Save offers the sample's name", field ? field->property("text").toString() : QString());
        r.check(r.clickItem(QStringLiteral("saveNameSave")), "samples: Save");
    });
    out.push_back([] {});
    out.push_back([&r, s] {
        r.check(QFileInfo::exists(s->dir->filePath(QStringLiteral("Honeycomb organizer.openshape"))) && r.app().hasProjectPath()
                    && r.app().documentTitle() == QStringLiteral("Honeycomb organizer"),
                "samples: kept as a project in the app folder");
        r.app().setAppFolder({});
        for (const Expected& e : kSamples)
            r.check(copyUntouched(r, QString::fromLatin1(e.name)),
                    QStringLiteral("samples: the sample %1 itself is unchanged").arg(QString::fromLatin1(e.name)));
        QObject::disconnect(s->listening);
        r.app().newDocument();
    });
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("samples"), 109, steps});

} // namespace
} // namespace os::app
