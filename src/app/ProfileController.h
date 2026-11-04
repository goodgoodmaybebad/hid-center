// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hidcenter {

class DeviceModule;
class ProfileStore;
class TelemetryRouter;
struct DeviceProfile;

/// One connected device as the profile page needs to see it.
struct ProfileDeviceInfo
{
    QString path;
    QString name;
    QString family;
    QString usbId;
    bool offline = false;
    bool supportsProfiles = false;
};

/// Ties profiles to the devices that can capture and apply them, and to the
/// telemetry source that can switch them automatically.
class ProfileController : public QObject
{
    Q_OBJECT

public:
    ProfileController(ProfileStore *store, TelemetryRouter *router,
                      QObject *parent = nullptr);

    ProfileStore *store() const { return m_store; }

    /// The devices the controller may use. MainWindow keeps this current as
    /// hardware is plugged in and removed.
    void setDevices(const QVector<DeviceModule *> &devices);

    QVector<ProfileDeviceInfo> devices() const;

    /// Telemetry sources that a profile can be associated with, by display name.
    QStringList gameNames() const;

    /// Capture `devicePath` into a new profile. The profile is stored at once.
    bool captureNew(const QString &devicePath, const QString &name, const QString &game,
                    bool autoApply, QString *error);

    /// Replace an existing profile's data with the device's current settings.
    bool captureInto(const QString &profileId, const QString &devicePath, QString *error);

    /// Apply a profile to one device. False with `error` when the profile is
    /// gone, the device is not there, or the families do not match.
    bool apply(const QString &profileId, const QString &devicePath, QString *error);

    /// The device a profile family should most naturally be applied to: the
    /// selected one when it matches, otherwise the first connected one.
    QString matchingDevicePath(const DeviceProfile &profile, const QString &preferred) const;

    Q_SIGNALS:
    void statusMessage(const QString &message);
    void devicesChanged();

private Q_SLOTS:
    void onActiveSourceChanged();

private:
    DeviceModule *moduleFor(const QString &path) const;

    /// Apply every auto profile bound to `source` to the devices it matches.
    /// Already-applied profile/device pairs are skipped, so this can also run
    /// when a device appears while a game is already sending.
    void applyAutomaticProfiles(const QString &source);

    ProfileStore *m_store = nullptr;
    TelemetryRouter *m_router = nullptr;
    QHash<QString, DeviceModule *> m_devices;

    /// The telemetry source the automatic profiles were last applied for, and
    /// which profile went to which device. Cleared when the source changes, so
    /// starting another game applies its profiles again.
    QString m_lastAutoSource;
    QSet<QString> m_appliedAutomatically;
};

} // namespace hidcenter
