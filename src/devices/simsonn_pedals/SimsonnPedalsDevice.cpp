// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SimsonnPedalsDevice.h"

#include "BrakeOptionsRead.h"
#include "ui/CalibrationPage.h"
#include "CalibrationSession.h"
#include "ConfigRead.h"
#include "FirmwareProbe.h"
#include "ui/MainSettingsPage.h"
#include "ParamsPoll.h"
#include "PedalInput.h"
#include "SettingsSave.h"
#include "ui/StatusPage.h"

#include <QJsonArray>
#include <QJsonObject>

#include <array>
#include <optional>

namespace hidcenter::simsonn_pedals {
namespace {

/// Usage pages 0xFF00-0xFFFF are vendor-defined. Pedals report their axes on the
/// generic desktop interface (usage page 0x01) and carry configuration on a
/// vendor-defined one.
bool isVendorDefined(quint16 usagePage)
{
    return usagePage >= 0xFF00;
}

QString elastomerName(Elastomer elastomer)
{
    switch (elastomer) {
    case Elastomer::Soft:
        return QStringLiteral("soft");
    case Elastomer::Medium:
        return QStringLiteral("medium");
    case Elastomer::Hard:
        return QStringLiteral("hard");
    }
    return {};
}

std::optional<Elastomer> elastomerForName(const QString &name)
{
    if (name == QLatin1String("soft"))
        return Elastomer::Soft;
    if (name == QLatin1String("medium"))
        return Elastomer::Medium;
    if (name == QLatin1String("hard"))
        return Elastomer::Hard;
    return std::nullopt;
}

QJsonArray curveToJson(const std::array<qint8, ConfigRead::kCurvePoints> &curve)
{
    QJsonArray array;
    for (const qint8 value : curve)
        array.append(int(value));
    return array;
}

} // namespace

SimsonnPedalsDevice::SimsonnPedalsDevice(IUsbDevice *context, QString modelName, QObject *parent)
    : IDevice(parent)
    , m_context(context)
    , m_modelName(std::move(modelName))
{
}

bool SimsonnPedalsDevice::initialize(QString *error)
{
    Q_UNUSED(error);

    // Built before the interface search below, because it reads the axes on the
    // generic-desktop interface and so does not care whether a vendor one was
    // found. Nothing is opened until a page asks it to read.
    m_pedals = new PedalInput(m_context, this);

    for (const UsbInterfaceInfo &iface : m_context->info().interfaces) {
        if (isVendorDefined(iface.hid.usagePage)) {
            m_vendorInterface = iface;
            break;
        }
    }

    if (!m_vendorInterface) {
        Q_EMIT statusMessage(
            tr("%1: configuration interface not found.").arg(m_modelName));
        return true;
    }

    // One round trip, for the firmware version: the only part of the protocol
    // this module drives so far. Everything else the pages show is still local,
    // which is why a probe that fails is not a reason to refuse the device —
    // returning false would replace every tab with an error page, and a device
    // that will not answer is exactly when the Protocol Analyzer is worth
    // reaching.
    m_probe = new FirmwareProbe(m_context, this);
    connect(m_probe, &FirmwareProbe::stateChanged, this, [this] {
        Q_EMIT infoFieldsChanged();

        switch (m_probe->state()) {
        case FirmwareProbe::State::Ready:
            Q_EMIT statusMessage(tr("%1: firmware %2, on %3.")
                                     .arg(m_modelName, m_probe->versionString(),
                                          m_vendorInterface->displayName()));
            return;

        case FirmwareProbe::State::Failed:
            Q_EMIT statusMessage(tr("%1: firmware version unavailable, see the Status tab. %2")
                                     .arg(m_modelName, m_probe->error()));
            return;

        case FirmwareProbe::State::Idle:
        case FirmwareProbe::State::Waiting:
            // Nothing to announce: the first is the state this was built in and
            // the second is it being under way.
            return;
        }
    });
    m_probe->start(*m_vendorInterface);

    // The params exchange, built but deliberately not started. Asking for the
    // params puts the pedals into a 500 report/s stream, and one that closing
    // the interface does not cut off: it runs for 30 seconds from the last
    // request whatever the host does. Only the Calibration page reads them, so
    // only the Calibration page asks, and only while it is on screen.
    m_params = new ParamsPoll(m_context, this);

    // The whole configuration blob, read here and now rather than left to a
    // page. Both settings pages want it — the curves and deadzones on one, the
    // calibration endpoints on the other — and the pedals are the only thing
    // that can change it, so once per connection is once too many if anything.
    // Eighteen round trips is a third of a second, paid while the tabs are still
    // being built.
    m_config = new ConfigRead(m_context, this);
    m_config->start(*m_vendorInterface);
    m_calibration = new CalibrationSession(m_context, m_params, m_config, this);
    m_brakeOptions = new BrakeOptionsRead(m_context, this);
    m_brakeOptions->start(*m_vendorInterface);
    m_settingsSave = new SettingsSave(m_context, m_config, m_brakeOptions, this);

    // All three share the one vendor interface, and that turns out to be fine.
    // A config read run against a Plus X while the params were flooding the same
    // node at 500 reports a second still completed all eighteen packets in
    // 0.32 s: the exchanges are told apart by their report id, and the config
    // read is host-paced, so it simply asks again for anything it misses.

    return true;
}

QVector<DeviceInfoField> SimsonnPedalsDevice::infoFields() const
{
    // Nothing until the pedals have actually said a version. An empty field in
    // the header reads as a device that failed to report one, which is a
    // different thing from one that has not been asked yet.
    if (!m_probe || m_probe->state() != FirmwareProbe::State::Ready)
        return {};

    return {{tr("Firmware"), m_probe->versionString(),
             tr("Firmware version / sub-version: %1").arg(m_probe->rawVersion())}};
}

QVector<DevicePage> SimsonnPedalsDevice::pages()
{
    // Status first, and so selected by default: what is true about the pedals
    // before what can be changed on them. It is also the tab that still says
    // something useful when the configuration protocol did not answer at all.
    auto *status = new StatusPage(m_modelName, m_context->info(), m_vendorInterface, m_probe);

    auto *settings =
        new MainSettingsPage(m_modelName, m_vendorInterface, m_pedals, m_config,
                                           m_brakeOptions, m_settingsSave);
    connect(settings, &MainSettingsPage::curveChanged, this, [this](const QString &pedal) {
        Q_EMIT statusMessage(tr("%1: %2 curve changed, not saved yet.")
                                 .arg(m_modelName, pedal));
    });
    if (m_settingsSave) {
        connect(m_settingsSave, &SettingsSave::changed, this, [this] {
            if (!m_settingsSave->message().isEmpty() && !m_settingsSave->isRunning())
                Q_EMIT statusMessage(QStringLiteral("%1: %2").arg(m_modelName,
                                                                  m_settingsSave->message()));
        });
    }
    auto *calibration = new CalibrationPage(m_modelName, m_vendorInterface, m_params,
                                            m_config, m_calibration);
    connect(m_calibration, &CalibrationSession::changed, this, [this] {
        if (!m_calibration->message().isEmpty() && !m_calibration->isRunning())
            Q_EMIT statusMessage(QStringLiteral("%1: %2").arg(m_modelName,
                                                              m_calibration->message()));
    });

    return {{tr("Status"), status},
            {tr("Main Settings"), settings},
            {tr("Calibration"), calibration}};
}

bool SimsonnPedalsDevice::captureProfile(QJsonObject *profile, QString *error) const
{
    const std::optional<SettingsSave::Settings> stored =
        m_settingsSave ? m_settingsSave->stored() : std::nullopt;
    if (!stored) {
        if (error)
            *error = tr("The pedals' settings have not been read yet.");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);

    QJsonArray pedals;
    for (const SettingsSave::PedalSettings &pedal : stored->pedals) {
        QJsonObject entry;
        entry.insert(QStringLiteral("curve"), curveToJson(pedal.curve));
        entry.insert(QStringLiteral("startDeadzone"), pedal.startDeadzone);
        entry.insert(QStringLiteral("endDeadzone"), pedal.endDeadzone);
        entry.insert(QStringLiteral("filter"), pedal.filter);
        pedals.append(entry);
    }
    root.insert(QStringLiteral("pedals"), pedals);

    QJsonObject brake;
    brake.insert(QStringLiteral("forceKg"), stored->brake.forceKg);
    if (stored->brake.elastomer)
        brake.insert(QStringLiteral("elastomer"), elastomerName(*stored->brake.elastomer));
    if (stored->brake.throttleCut) {
        QJsonObject cut;
        cut.insert(QStringLiteral("enabled"), stored->brake.throttleCut->enabled);
        cut.insert(QStringLiteral("tenths"), stored->brake.throttleCut->tenths);
        brake.insert(QStringLiteral("throttleCut"), cut);
    }
    root.insert(QStringLiteral("brake"), brake);

    if (profile)
        *profile = root;
    return true;
}

bool SimsonnPedalsDevice::applyProfile(const QJsonObject &profile, QString *error)
{
    if (!m_settingsSave || !m_vendorInterface) {
        if (error)
            *error = tr("The pedals cannot be written to on this connection.");
        return false;
    }
    if (m_settingsSave->isRunning()) {
        if (error)
            *error = tr("A settings save is already in progress.");
        return false;
    }
    if (profile.value(QStringLiteral("schema")).toInt() != 1) {
        if (error)
            *error = tr("This profile was written by an unsupported version of HID Center.");
        return false;
    }

    const std::optional<SettingsSave::Settings> current = m_settingsSave->stored();
    if (!current) {
        if (error)
            *error = tr("The pedals' settings have not been read yet.");
        return false;
    }

    SettingsSave::Settings settings = *current;

    const QJsonArray pedals = profile.value(QStringLiteral("pedals")).toArray();
    if (pedals.size() == kPedalCount) {
        for (int i = 0; i < kPedalCount; ++i) {
            const QJsonObject entry = pedals.at(i).toObject();
            SettingsSave::PedalSettings &pedal = settings.pedals[std::size_t(i)];

            const QJsonArray curve = entry.value(QStringLiteral("curve")).toArray();
            if (curve.size() == ConfigRead::kCurvePoints) {
                for (int point = 0; point < ConfigRead::kCurvePoints; ++point) {
                    pedal.curve[std::size_t(point)] =
                        qint8(qBound(-100, curve.at(point).toInt(), 100));
                }
            }

            if (entry.contains(QStringLiteral("startDeadzone")))
                pedal.startDeadzone =
                    quint8(qBound(0, entry.value(QStringLiteral("startDeadzone")).toInt(), 100));
            if (entry.contains(QStringLiteral("endDeadzone")))
                pedal.endDeadzone =
                    quint8(qBound(0, entry.value(QStringLiteral("endDeadzone")).toInt(),
                                  100 - int(pedal.startDeadzone)));
            if (entry.contains(QStringLiteral("filter")))
                pedal.filter =
                    quint8(qBound(0, entry.value(QStringLiteral("filter")).toInt(),
                                  ConfigRead::kFilterMaximum));
        }
    }

    const QJsonObject brake = profile.value(QStringLiteral("brake")).toObject();

    std::optional<Elastomer> elastomer = settings.brake.elastomer;
    if (brake.contains(QStringLiteral("elastomer")))
        elastomer = elastomerForName(brake.value(QStringLiteral("elastomer")).toString());
    if (elastomer) {
        settings.brake.elastomer = elastomer;
        if (brake.contains(QStringLiteral("forceKg"))) {
            settings.brake.forceKg =
                qBound(kBrakeForceMinimumKg,
                       brake.value(QStringLiteral("forceKg")).toInt(),
                       brakeForceMaximumKg(*elastomer));
        }
    }

    const QJsonObject cut = brake.value(QStringLiteral("throttleCut")).toObject();
    if (!cut.isEmpty() && settings.brake.throttleCut) {
        settings.brake.throttleCut->enabled =
            cut.value(QStringLiteral("enabled")).toBool(settings.brake.throttleCut->enabled);
        if (cut.contains(QStringLiteral("tenths"))) {
            settings.brake.throttleCut->tenths =
                qBound(kThrottleCutMinimum, cut.value(QStringLiteral("tenths")).toInt(),
                       kThrottleCutMaximum);
        }
    }

    m_settingsSave->save(*m_vendorInterface, settings);
    if (error)
        error->clear();
    return true;
}

} // namespace hidcenter::simsonn_pedals
