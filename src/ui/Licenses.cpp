// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "ui/Licenses.h"

#include "core/Version.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QStringList>

namespace os::ui {

namespace {

bool readFile(const QString& path, QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    text = QString::fromUtf8(file.readAll());
    return true;
}

QString str(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString();
}

} // namespace

BuildInfo BuildInfo::current()
{
    BuildInfo build;
    build.version = QString::fromLatin1(kAppVersion);
    build.buildNumber = QString::fromLatin1(kBuildNumber);
    build.commit = QString::fromLatin1(kSourceCommit);
    build.releaseTag = QString::fromLatin1(kReleaseTag);
    return build;
}

LicenseCatalog LicenseCatalog::load(const QString& dir)
{
    LicenseCatalog catalog;
    catalog.dir_ = dir;
    QString json;
    if (!readFile(dir + QStringLiteral("/index.json"), json)) {
        catalog.error_ = QStringLiteral("%1/index.json cannot be read").arg(dir);
        return catalog;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (!doc.isObject()) {
        catalog.error_ = QStringLiteral("%1/index.json: %2").arg(dir, parseError.errorString());
        return catalog;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toInt() != 1) {
        catalog.error_ = QStringLiteral("%1/index.json has an unknown format").arg(dir);
        return catalog;
    }
    for (const QJsonValue& value : root.value(QStringLiteral("components")).toArray()) {
        const QJsonObject c = value.toObject();
        LicenseEntry entry;
        entry.id = str(c, "id");
        entry.name = str(c, "name");
        entry.group = str(c, "group");
        entry.version = str(c, "version");
        entry.license = str(c, "license");
        entry.usedFor = str(c, "usedFor");
        entry.copyright = str(c, "copyright");
        entry.notice = str(c, "notice");
        entry.homepage = str(c, "homepage");
        entry.source = str(c, "source");
        for (const QJsonValue& t : c.value(QStringLiteral("texts")).toArray()) {
            const QJsonObject text = t.toObject();
            entry.texts.push_back({str(text, "title"), str(text, "file")});
        }
        if (entry.id.isEmpty() || entry.name.isEmpty() || entry.texts.empty()) {
            catalog.error_ = QStringLiteral("%1/index.json: an entry lacks its id, name or texts").arg(dir);
            catalog.entries_.clear();
            return catalog;
        }
        catalog.entries_.push_back(std::move(entry));
    }
    if (catalog.entries_.empty())
        catalog.error_ = QStringLiteral("%1/index.json lists nothing").arg(dir);
    return catalog;
}

const LicenseEntry* LicenseCatalog::find(const QString& id) const
{
    for (const LicenseEntry& entry : entries_) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

QString LicenseCatalog::text(const QString& id) const
{
    const LicenseEntry* entry = find(id);
    if (!entry)
        return {};
    QStringList parts;
    QString head = entry->name;
    if (!entry->version.isEmpty())
        head += QLatin1Char(' ') + entry->version;
    parts << head + QStringLiteral("\nLicense: ") + entry->license
                 + (entry->usedFor.isEmpty() ? QString() : QStringLiteral("\nUsed for: ") + entry->usedFor);
    if (!entry->copyright.isEmpty())
        parts << entry->copyright;
    if (!entry->notice.isEmpty())
        parts << entry->notice;
    if (!entry->homepage.isEmpty())
        parts << QStringLiteral("Home page: ") + entry->homepage;
    if (!entry->source.isEmpty())
        parts << QStringLiteral("Source code: ") + entry->source;
    const QString rule(40, QLatin1Char('-'));
    for (const LicenseText& t : entry->texts) {
        QString body;
        if (!readFile(dir_ + QStringLiteral("/texts/") + t.file, body) || body.trimmed().isEmpty())
            return {};
        parts << rule + QLatin1Char('\n') + t.title + QLatin1Char('\n') + rule + QLatin1Char('\n') + body.trimmed();
    }
    return parts.join(QStringLiteral("\n\n")) + QLatin1Char('\n');
}

QString LicenseCatalog::sourceOffer(const BuildInfo& build) const
{
    QString text;
    if (!readFile(dir_ + QStringLiteral("/source-offer.txt"), text))
        return {};
    text.replace(QStringLiteral("@VERSION@"), build.version);
    text.replace(QStringLiteral("@BUILD@"), buildDescription(build));
    text.replace(QStringLiteral("@SOURCE_URL@"), sourceCodeUrl(build));
    text.replace(QStringLiteral("@LIBRARY_SOURCES@"), librarySourcesText(build));
    text.replace(QStringLiteral("@CONTACT@"), QString::fromLatin1(kSourceContactUrl));
    return text;
}

QString sourceCodeUrl(const BuildInfo& build)
{
    const QString repo = QString::fromLatin1(kRepositoryUrl);
    if (!build.releaseTag.isEmpty())
        return repo + QStringLiteral("/tree/") + build.releaseTag;
    if (!build.commit.isEmpty())
        return repo + QStringLiteral("/tree/") + build.commit;
    return repo;
}

QString librarySourcesText(const BuildInfo& build)
{
    if (!build.releaseTag.isEmpty()) {
        return QString::fromLatin1(kRepositoryUrl) + QStringLiteral("/releases/tag/") + build.releaseTag
               + QStringLiteral(" (the file OpenShape-") + build.releaseTag.mid(1) + QStringLiteral("-ios-sources.tar)");
    }
    return QStringLiteral("the download addresses and SHA-256 checksums in scripts/ios/sources.txt of that source code");
}

QString buildDescription(const BuildInfo& build)
{
    QStringList parts;
    if (!build.buildNumber.isEmpty() && build.buildNumber != build.version)
        parts << QStringLiteral("build ") + build.buildNumber;
    if (!build.releaseTag.isEmpty())
        parts << QStringLiteral("release ") + build.releaseTag;
    if (!build.commit.isEmpty())
        parts << QStringLiteral("commit ") + build.commit.left(12);
    return parts.isEmpty() ? QStringLiteral("a local build") : parts.join(QStringLiteral(", "));
}

} // namespace os::ui
