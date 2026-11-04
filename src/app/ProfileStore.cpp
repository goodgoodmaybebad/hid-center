// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/ProfileStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSettings>
#include <QUuid>

#include <optional>

namespace hidcenter {
namespace {

constexpr auto kSettingsKey = "profiles/v1";

QString freshId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

std::optional<DeviceProfile> profileFromJson(const QJsonObject &object)
{
    if (object.value(QStringLiteral("schema")).toInt() != 1)
        return std::nullopt;

    DeviceProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.name = object.value(QStringLiteral("name")).toString().trimmed();
    profile.family = object.value(QStringLiteral("family")).toString().trimmed();
    profile.game = object.value(QStringLiteral("game")).toString().trimmed();
    profile.autoApply = object.value(QStringLiteral("autoApply")).toBool(false);
    profile.data = object.value(QStringLiteral("data")).toObject();

    // A profile without a name or family cannot be shown or applied, and one
    // without data has nothing to restore.
    if (profile.name.isEmpty() || profile.family.isEmpty() || profile.data.isEmpty())
        return std::nullopt;
    if (profile.id.isEmpty())
        profile.id = freshId();
    return profile;
}

QJsonObject profileToJson(const DeviceProfile &profile, bool includeId)
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), 1);
    if (includeId)
        object.insert(QStringLiteral("id"), profile.id);
    object.insert(QStringLiteral("name"), profile.name);
    object.insert(QStringLiteral("family"), profile.family);
    if (!profile.game.isEmpty())
        object.insert(QStringLiteral("game"), profile.game);
    object.insert(QStringLiteral("autoApply"), profile.autoApply);
    object.insert(QStringLiteral("data"), profile.data);
    return object;
}

} // namespace

ProfileStore::ProfileStore(QObject *parent)
    : QObject(parent)
{
    load();
}

const DeviceProfile *ProfileStore::find(const QString &id) const
{
    for (const DeviceProfile &profile : m_profiles) {
        if (profile.id == id)
            return &profile;
    }
    return nullptr;
}

QString ProfileStore::add(DeviceProfile profile)
{
    if (profile.id.isEmpty())
        profile.id = freshId();
    profile.name = profile.name.trimmed();
    if (profile.name.isEmpty() || profile.family.isEmpty())
        return {};

    m_profiles.append(profile);
    save();
    Q_EMIT changed();
    return profile.id;
}

bool ProfileStore::update(const DeviceProfile &profile)
{
    for (DeviceProfile &existing : m_profiles) {
        if (existing.id != profile.id)
            continue;
        existing = profile;
        existing.name = existing.name.trimmed();
        save();
        Q_EMIT changed();
        return true;
    }
    return false;
}

bool ProfileStore::remove(const QString &id)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id != id)
            continue;
        m_profiles.removeAt(i);
        save();
        Q_EMIT changed();
        return true;
    }
    return false;
}

bool ProfileStore::importFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Could not open %1: %2").arg(path, file.errorString());
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = tr("%1 is not valid JSON: %2").arg(path, parseError.errorString());
        return false;
    }

    QVector<QJsonObject> objects;
    if (document.isArray()) {
        const QJsonArray array = document.array();
        for (const QJsonValue &value : array) {
            if (value.isObject())
                objects.append(value.toObject());
        }
    } else if (document.isObject()) {
        objects.append(document.object());
    }

    int added = 0;
    for (const QJsonObject &object : objects) {
        std::optional<DeviceProfile> profile = profileFromJson(object);
        if (!profile)
            continue;
        // Fresh id: importing the same file twice creates two profiles rather
        // than reaching back and replacing one already in use.
        profile->id = freshId();
        m_profiles.append(*profile);
        ++added;
    }

    if (added == 0) {
        if (error)
            *error = tr("%1 contains no HID Center profiles.").arg(path);
        return false;
    }

    save();
    Q_EMIT changed();
    return true;
}

bool ProfileStore::exportProfile(const QString &id, const QString &path, QString *error) const
{
    const DeviceProfile *profile = find(id);
    if (!profile) {
        if (error)
            *error = tr("The profile no longer exists.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = tr("Could not write %1: %2").arg(path, file.errorString());
        return false;
    }

    file.write(QJsonDocument(profileToJson(*profile, false))
                   .toJson(QJsonDocument::Indented));
    return true;
}

void ProfileStore::load()
{
    m_profiles.clear();

    const QByteArray json = QSettings().value(QLatin1String(kSettingsKey)).toByteArray();
    if (json.isEmpty())
        return;

    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray())
        return;

    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        if (const std::optional<DeviceProfile> profile = profileFromJson(value.toObject()))
            m_profiles.append(*profile);
    }
}

void ProfileStore::save()
{
    QJsonArray array;
    for (const DeviceProfile &profile : m_profiles)
        array.append(profileToJson(profile, true));

    QSettings().setValue(QLatin1String(kSettingsKey),
                         QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace hidcenter
