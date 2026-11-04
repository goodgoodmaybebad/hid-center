// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace hidcenter {

/// One named settings snapshot, plus what it belongs to and when to use it.
///
/// `data` is entirely the device module's: the store never reads inside it.
/// `family` is the module's family name, which is what decides whether a profile
/// can be applied to a device. `game` is the telemetry source's display name; an
/// empty one means the profile is manual-only.
struct DeviceProfile
{
    QString id;
    QString name;
    QString family;
    QString game;
    bool autoApply = false;
    QJsonObject data;
};

/// Profiles on disk, as one JSON array in QSettings.
///
/// Deliberately separate from the devices: a profile can be created from one
/// connection and applied to another of the same family, exported and imported,
/// and switched by a game without any hardware being open.
class ProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit ProfileStore(QObject *parent = nullptr);

    const QVector<DeviceProfile> &profiles() const { return m_profiles; }
    const DeviceProfile *find(const QString &id) const;

    /// Add `profile`, generating an id when it has none. Returns the stored id.
    QString add(DeviceProfile profile);

    /// Replace the profile with the same id. False when it is gone.
    bool update(const DeviceProfile &profile);

    bool remove(const QString &id);

    /// Add every profile in a file. Profiles get fresh ids so an import can
    /// never silently overwrite the user's own. False with `error` on a file
    /// that is not a profile export.
    bool importFile(const QString &path, QString *error);

    /// Write one profile as a portable JSON object.
    bool exportProfile(const QString &id, const QString &path, QString *error) const;

Q_SIGNALS:
    void changed();

private:
    void load();
    void save();

    QVector<DeviceProfile> m_profiles;
};

} // namespace hidcenter
