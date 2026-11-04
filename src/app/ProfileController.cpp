// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/ProfileController.h"

#include "app/DeviceModule.h"
#include "app/ProfileStore.h"
#include "core/Logging.h"
#include "core/plugin/IDevice.h"
#include "core/telemetry/TelemetryHub.h"
#include "core/telemetry/TelemetryRouter.h"
#include "core/telemetry/TelemetrySourceModule.h"
#include "core/usb/IUsbDevice.h"

#include <QJsonObject>

#include <algorithm>

namespace hidcenter {
namespace {

QString automaticKey(const QString &profileId, const QString &devicePath)
{
    return profileId + QLatin1Char('\n') + devicePath;
}

} // namespace

ProfileController::ProfileController(ProfileStore *store, TelemetryRouter *router,
                                     QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_router(router)
{
    connect(TelemetryHub::instance(), &TelemetryHub::activeSourceChanged, this,
            &ProfileController::onActiveSourceChanged);

    // A game may already have been sending when the window was built: the
    // router starts before any widget exists. Remember the current source so
    // the first setDevices() can apply its profiles instead of waiting for a
    // change that already happened.
    m_lastAutoSource = TelemetryHub::instance()->activeSource();
}

void ProfileController::setDevices(const QVector<DeviceModule *> &devices)
{
    m_devices.clear();
    for (DeviceModule *module : devices) {
        if (module)
            m_devices.insert(module->info().devicePath, module);
    }
    Q_EMIT devicesChanged();

    // A device that appears while a game is already sending should still pick
    // up that game's auto profile.
    if (!m_lastAutoSource.isEmpty())
        applyAutomaticProfiles(m_lastAutoSource);
}

QVector<ProfileDeviceInfo> ProfileController::devices() const
{
    QVector<ProfileDeviceInfo> result;
    result.reserve(int(m_devices.size()));
    for (DeviceModule *module : m_devices) {
        ProfileDeviceInfo info;
        info.path = module->info().devicePath;
        info.name = module->displayName();
        info.family = module->familyName();
        info.usbId = module->info().id.toString();
        info.offline = module->usbDevice() && module->usbDevice()->isOffline();
        info.supportsProfiles = module->device() && module->device()->supportsProfiles();
        result.append(info);
    }
    std::sort(result.begin(), result.end(),
              [](const ProfileDeviceInfo &a, const ProfileDeviceInfo &b) {
                  if (a.offline != b.offline)
                      return !a.offline;
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    return result;
}

QStringList ProfileController::gameNames() const
{
    QStringList names;
    if (!m_router)
        return names;
    for (const TelemetrySourceModule *module : m_router->modules()) {
        if (module && !module->displayName().isEmpty())
            names << module->displayName();
    }
    names.removeDuplicates();
    return names;
}

DeviceModule *ProfileController::moduleFor(const QString &path) const
{
    return m_devices.value(path, nullptr);
}

bool ProfileController::captureNew(const QString &devicePath, const QString &name,
                                   const QString &game, bool autoApply, QString *error)
{
    DeviceModule *module = moduleFor(devicePath);
    if (!module || !module->device()) {
        if (error)
            *error = tr("That device is not available.");
        return false;
    }

    IDevice *device = module->device();
    if (!device->supportsProfiles()) {
        if (error)
            *error = tr("%1 does not support profiles.").arg(module->displayName());
        return false;
    }

    QJsonObject data;
    if (!device->captureProfile(&data, error))
        return false;

    DeviceProfile profile;
    profile.name = name;
    profile.family = module->familyName();
    profile.game = game;
    profile.autoApply = autoApply;
    profile.data = data;

    if (m_store->add(profile).isEmpty()) {
        if (error)
            *error = tr("The profile could not be stored. Give it a name first.");
        return false;
    }

    Q_EMIT statusMessage(tr("Saved profile “%1” from %2.").arg(profile.name,
                                                               module->displayName()));
    return true;
}

bool ProfileController::captureInto(const QString &profileId, const QString &devicePath,
                                    QString *error)
{
    DeviceModule *module = moduleFor(devicePath);
    if (!module || !module->device()) {
        if (error)
            *error = tr("That device is not available.");
        return false;
    }

    DeviceProfile profile;
    if (const DeviceProfile *found = m_store->find(profileId))
        profile = *found;
    else {
        if (error)
            *error = tr("The profile no longer exists.");
        return false;
    }

    if (profile.family != module->familyName()) {
        if (error)
            *error = tr("This profile belongs to %1, not %2.")
                         .arg(profile.family, module->familyName());
        return false;
    }

    QJsonObject data;
    if (!module->device()->captureProfile(&data, error))
        return false;

    profile.data = data;
    if (!m_store->update(profile)) {
        if (error)
            *error = tr("The profile could not be updated.");
        return false;
    }

    Q_EMIT statusMessage(tr("Updated profile “%1” from %2.")
                             .arg(profile.name, module->displayName()));
    return true;
}

bool ProfileController::apply(const QString &profileId, const QString &devicePath,
                              QString *error)
{
    const DeviceProfile *profile = m_store->find(profileId);
    if (!profile) {
        if (error)
            *error = tr("The profile no longer exists.");
        return false;
    }

    DeviceModule *module = moduleFor(devicePath);
    if (!module || !module->device()) {
        if (error)
            *error = tr("That device is not available.");
        return false;
    }
    if (profile->family != module->familyName()) {
        if (error)
            *error = tr("This profile belongs to %1, not %2.")
                         .arg(profile->family, module->familyName());
        return false;
    }
    if (!module->device()->supportsProfiles()) {
        if (error)
            *error = tr("%1 does not support profiles.").arg(module->displayName());
        return false;
    }

    if (!module->device()->applyProfile(profile->data, error))
        return false;

    Q_EMIT statusMessage(tr("Applying profile “%1” to %2…")
                             .arg(profile->name, module->displayName()));
    return true;
}

QString ProfileController::matchingDevicePath(const DeviceProfile &profile,
                                              const QString &preferred) const
{
    const auto usable = [&profile](const DeviceModule *module) {
        return module->familyName() == profile.family && module->device()
               && module->device()->supportsProfiles();
    };

    if (DeviceModule *module = moduleFor(preferred)) {
        if (usable(module))
            return preferred;
    }

    for (DeviceModule *module : m_devices) {
        if (usable(module) && module->usbDevice() && !module->usbDevice()->isOffline())
            return module->info().devicePath;
    }
    for (DeviceModule *module : m_devices) {
        if (usable(module))
            return module->info().devicePath;
    }
    return {};
}

void ProfileController::onActiveSourceChanged()
{
    const QString source = TelemetryHub::instance()->activeSource();
    if (source.isEmpty()) {
        m_lastAutoSource.clear();
        m_appliedAutomatically.clear();
        return;
    }

    if (source == m_lastAutoSource)
        return;

    m_lastAutoSource = source;
    m_appliedAutomatically.clear();
    applyAutomaticProfiles(source);
}

void ProfileController::applyAutomaticProfiles(const QString &source)
{
    QSet<QString> handledFamilies;
    for (const DeviceProfile &profile : m_store->profiles()) {
        if (!profile.autoApply || profile.game != source)
            continue;
        if (handledFamilies.contains(profile.family))
            continue;
        handledFamilies.insert(profile.family);

        for (DeviceModule *module : m_devices) {
            if (module->familyName() != profile.family || !module->device()
                || !module->device()->supportsProfiles()) {
                continue;
            }
            if (module->usbDevice() && module->usbDevice()->isOffline())
                continue;

            const QString path = module->info().devicePath;
            const QString key = automaticKey(profile.id, path);
            if (m_appliedAutomatically.contains(key))
                continue;

            QString error;
            if (module->device()->applyProfile(profile.data, &error)) {
                m_appliedAutomatically.insert(key);
                qCInfo(hidcenterApp).noquote()
                    << QStringLiteral("Applying game profile \"%1\" to %2 for %3.")
                           .arg(profile.name, module->displayName(), source);
                Q_EMIT statusMessage(tr("Applying game profile “%1” to %2 for %3…")
                                         .arg(profile.name, module->displayName(), source));
            } else {
                qCWarning(hidcenterApp).noquote()
                    << QStringLiteral("Could not apply game profile \"%1\" to %2: %3")
                           .arg(profile.name, module->displayName(), error);
                Q_EMIT statusMessage(tr("Could not apply profile “%1” to %2: %3")
                                         .arg(profile.name, module->displayName(), error));
            }
        }
    }
}

} // namespace hidcenter
