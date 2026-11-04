// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SimagicAlphaEvoDevice.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoForceTrace.h"
#include "AlphaEvoSettings.h"
#include "AlphaEvoTelemetry.h"
#include "BasePedalController.h"
#include "WheelConfigController.h"
#include "WheelLedController.h"
#include "core/Logging.h"
#include "ui/MainSettingsPage.h"
#include "ui/PedalsPage.h"
#include "ui/StatusPage.h"
#include "ui/TelemetryPage.h"
#include "ui/WheelPage.h"
#include "ui/WheelbaseLedPage.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

#include <optional>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Stable names for the settings in a profile file. The human labels are
/// translated, so they cannot be the keys; the enum values are an
/// implementation detail that may be reordered. These strings are the file
/// format.
const QHash<Setting, QString> &settingKeys()
{
    static const QHash<Setting, QString> keys{
        {Setting::FfStrength, QStringLiteral("forceFeedbackStrength")},
        {Setting::MechanicalCentering, QStringLiteral("mechanicalCentering")},
        {Setting::MechanicalDamper, QStringLiteral("mechanicalDamper")},
        {Setting::MechanicalFriction, QStringLiteral("mechanicalFriction")},
        {Setting::MechanicalInertia, QStringLiteral("mechanicalInertia")},
        {Setting::CenterDamper, QStringLiteral("centerDamper")},
        {Setting::GameCentering, QStringLiteral("gameCentering")},
        {Setting::GameInertia, QStringLiteral("gameInertia")},
        {Setting::GameDamper, QStringLiteral("gameDamper")},
        {Setting::GameFriction, QStringLiteral("gameFriction")},
        {Setting::MaxAngle, QStringLiteral("maxAngle")},
        {Setting::AngleLock, QStringLiteral("angleLock")},
        {Setting::AngleLockStrength, QStringLiteral("angleLockStrength")},
        {Setting::WheelRotationSpeed, QStringLiteral("wheelRotationSpeed")},
        {Setting::SteeringTorqueAssist, QStringLiteral("steeringTorqueAssist")},
        {Setting::DynamicPredictionLevel, QStringLiteral("dynamicPredictionLevel")},
        {Setting::FeedbackDetail, QStringLiteral("feedbackDetail")},
        {Setting::FilterLevel, QStringLiteral("filterLevel")},
        {Setting::SlewRateControl, QStringLiteral("slewRateControl")},
        {Setting::RingLight, QStringLiteral("ringLight")},
        {Setting::RingLightBrightness, QStringLiteral("ringLightBrightness")},
    };
    return keys;
}

std::optional<Setting> settingForProfileKey(const QString &key)
{
    for (auto it = settingKeys().cbegin(); it != settingKeys().cend(); ++it) {
        if (it.value() == key)
            return it.key();
    }
    return std::nullopt;
}

QJsonArray pointsArray(const std::array<int, kPedalCurvePointCount> &points)
{
    QJsonArray array;
    for (const int value : points)
        array.append(value);
    return array;
}

void readPoints(const QJsonValue &value, std::array<int, kPedalCurvePointCount> *points)
{
    const QJsonArray array = value.toArray();
    if (array.size() != int(points->size()))
        return;
    for (int i = 0; i < array.size(); ++i)
        (*points)[std::size_t(i)] = qBound(0, array.at(i).toInt(), 100);
}

} // namespace

SimagicAlphaEvoDevice::SimagicAlphaEvoDevice(IUsbDevice *context, QString modelName, QObject *parent)
    : IDevice(parent)
    , m_context(context)
    , m_modelName(std::move(modelName))
{
}

bool SimagicAlphaEvoDevice::initialize(QString *error)
{
    Q_UNUSED(error);

    m_settings = new AlphaEvoSettings(m_context, this);

    // Independent of the configuration protocol: telemetry flows the other way
    // and does not need the base to have answered anything. It is given the
    // settings object because that owns the clock its reports go out on — and
    // that clock exists whether or not there is a base behind it.
    m_telemetry = new AlphaEvoTelemetry(m_settings, this);

    // Built here rather than with the page, and for the same reason: the strip
    // should follow the car from the moment the base is detected, whether or not
    // anyone ever opens the tab. It also loads the saved modes, which is what
    // puts the user's own colours on the wheel at start-up rather than the
    // defaults the animation thread began with.
    m_leds = new WheelLedController(m_settings, m_context ? m_context->info() : UsbDeviceInfo{},
                                    this);

    // The steering wheel's own settings, for the same reason and at the same
    // point. It restores the saved brightness and hands it over straight away,
    // before the probe below: the settings object keeps it until the probe has
    // started the I/O thread, and sends it then. The colours wait for the probe
    // to say which wheel is on the base; see below.
    m_wheelConfig = new WheelConfigController(
        m_settings, m_context ? m_context->info() : UsbDeviceInfo{}, this);

    // Also independent of the configuration protocol, and of the probe below: it
    // listens to input reports the kernel is already delivering rather than
    // asking the base for anything. Built here for the ordinary reason — the
    // Status page may be opened, closed and rebuilt, and the source it taps
    // should not be rediscovered every time.
    m_forceTrace = new AlphaEvoForceTrace(m_context, this);

    // The buttons pressed on the wheel, from the same input report, for the
    // same reasons. The Wheel tab switches it on while it is shown.
    m_buttons = new AlphaEvoButtonInput(m_context, this);

    // The same report again, for the Pedals tab. A reader of its own, so the
    // Wheel tab hiding as the Pedals tab shows cannot switch it off.
    m_pedalInput = new AlphaEvoButtonInput(m_context, this);

    // The settings of pedals on the base. Sends nothing until the user turns it
    // on and changes something. A reader of its own for calibrating, read only
    // while one runs.
    m_calibrationInput = new AlphaEvoButtonInput(m_context, this);
    m_basePedals = new BasePedalController(
        m_settings, m_calibrationInput, m_context ? m_context->info() : UsbDeviceInfo{}, this);

    if (!m_settings->probe(&m_probeError)) {
        // Deliberately not a failure. Returning false would replace every tab
        // with an error page, and a base that will not answer is exactly when
        // the Protocol Analyzer is worth reaching.
        qCWarning(hidcenterDevice).noquote()
            << m_modelName << "did not answer the configuration protocol:" << m_probeError;
        Q_EMIT statusMessage(tr("%1: settings unavailable — see the FFB Settings tab.")
                                 .arg(m_modelName));
        return true;
    }

    // The probe read the base status, wheel id and all, without announcing it,
    // and only now is it known whether this is offline. So the wheel is looked
    // at once here, which is what sends a GTC its colours at start-up; later
    // changes arrive with each status refresh.
    m_wheelConfig->refreshAttachedWheel();

    // The firmware version comes from the same status report as the settings, so
    // it changes whenever that is re-read — after a firmware update, say.
    connect(m_settings, &AlphaEvoSettings::statusRefreshed, this,
            &SimagicAlphaEvoDevice::infoFieldsChanged);

    Q_EMIT statusMessage(tr("%1: firmware %2, on %3.")
                             .arg(m_modelName)
                             .arg(m_settings->firmwareVersion())
                             .arg(m_settings->activeInterface()
                                      ? m_settings->activeInterface()->displayName()
                                      : tr("no interface")));
    return true;
}

QVector<DeviceInfoField> SimagicAlphaEvoDevice::infoFields() const
{
    // Nothing offline: the firmware version would be read out of a status report
    // no base ever sent, and "Firmware 0" reads as a fact rather than a blank.
    if (!m_settings || !m_settings->isReady() || m_settings->isOffline())
        return {};

    return {{tr("Firmware"), QString::number(m_settings->firmwareVersion()),
             tr("Raw firmware field: %1 (0x%2)")
                 .arg(m_settings->firmwareRaw())
                 .arg(m_settings->firmwareRaw(), 4, 16, QLatin1Char('0'))}};
}

QVector<DevicePage> SimagicAlphaEvoDevice::pages()
{
    // Status first, and so selected by default: what is true about the base
    // before what can be changed on it. It is also the tab that still says
    // something useful when the base did not answer at all.
    return {{tr("Status"), new StatusPage(m_settings, m_forceTrace, m_probeError)},
            {tr("FFB Settings"), new MainSettingsPage(m_modelName, m_settings, m_probeError)},
            {tr("Telemetry"), new TelemetryPage(m_telemetry, m_settings)},
            {tr("Wheelbase LED"), new WheelbaseLedPage(m_leds, m_settings, m_probeError)},
            {tr("Wheel"), new WheelPage(m_wheelConfig, m_buttons, m_settings, m_probeError)},
            {tr("Pedals"), new PedalsPage(m_pedalInput, m_basePedals)}};
}

bool SimagicAlphaEvoDevice::captureProfile(QJsonObject *profile, QString *error) const
{
    if (!m_settings || !m_settings->isReady()) {
        if (error)
            *error = tr("The wheelbase's settings are not available yet.");
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);

    QJsonObject ffb;
    for (const SettingSpec &spec : settingSpecs())
        ffb.insert(settingKeys().value(spec.id), m_settings->value(spec.id));
    root.insert(QStringLiteral("ffb"), ffb);

    if (m_basePedals) {
        const BasePedalController::ProfileState state = m_basePedals->profileState();
        QJsonObject pedals;
        pedals.insert(QStringLiteral("brakeSensor"), int(state.brakeSensor));
        pedals.insert(QStringLiteral("brakePoints"), pointsArray(state.brakePoints));
        pedals.insert(QStringLiteral("throttlePoints"), pointsArray(state.throttlePoints));
        pedals.insert(QStringLiteral("brakeStart"), state.brakeStart);
        pedals.insert(QStringLiteral("brakeEnd"), state.brakeEnd);
        pedals.insert(QStringLiteral("throttleStart"), state.throttleStart);
        pedals.insert(QStringLiteral("throttleEnd"), state.throttleEnd);
        root.insert(QStringLiteral("pedals"), pedals);
    }

    if (profile)
        *profile = root;
    return true;
}

bool SimagicAlphaEvoDevice::applyProfile(const QJsonObject &profile, QString *error)
{
    if (!m_settings || !m_settings->isReady()) {
        if (error)
            *error = tr("The wheelbase's settings are not available yet.");
        return false;
    }

    if (profile.value(QStringLiteral("schema")).toInt() != 1) {
        if (error)
            *error = tr("This profile was written by an unsupported version of HID Center.");
        return false;
    }

    const QJsonObject ffb = profile.value(QStringLiteral("ffb")).toObject();
    SettingValues changes;
    for (auto it = ffb.constBegin(); it != ffb.constEnd(); ++it) {
        const std::optional<Setting> setting = settingForProfileKey(it.key());
        if (!setting || !it.value().isDouble())
            continue;
        changes.insert(*setting, clampToSpec(*setting, it.value().toInt()));
    }

    if (!changes.isEmpty()) {
        if (m_settings->isBusy()) {
            if (error)
                *error = tr("A settings write is already in progress. Try again when it finishes.");
            return false;
        }
        m_settings->applyAsync(changes);
    }

    if (m_basePedals) {
        const QJsonObject pedals = profile.value(QStringLiteral("pedals")).toObject();
        if (!pedals.isEmpty()) {
            BasePedalController::ProfileState state = m_basePedals->profileState();
            if (pedals.contains(QStringLiteral("brakeSensor")))
                state.brakeSensor = PedalBrakeSensor(
                    qBound(0, pedals.value(QStringLiteral("brakeSensor")).toInt(), 1));
            readPoints(pedals.value(QStringLiteral("brakePoints")), &state.brakePoints);
            readPoints(pedals.value(QStringLiteral("throttlePoints")),
                       &state.throttlePoints);
            if (pedals.contains(QStringLiteral("brakeStart")))
                state.brakeStart = qBound(0, pedals.value(QStringLiteral("brakeStart")).toInt(), 99);
            if (pedals.contains(QStringLiteral("brakeEnd")))
                state.brakeEnd = qBound(0, pedals.value(QStringLiteral("brakeEnd")).toInt(), 100);
            if (pedals.contains(QStringLiteral("throttleStart")))
                state.throttleStart =
                    qBound(0, pedals.value(QStringLiteral("throttleStart")).toInt(), 99);
            if (pedals.contains(QStringLiteral("throttleEnd")))
                state.throttleEnd =
                    qBound(0, pedals.value(QStringLiteral("throttleEnd")).toInt(), 100);
            m_basePedals->setProfileState(state);
        }
    }

    if (error)
        error->clear();
    return true;
}

} // namespace hidcenter::simagic_alpha_evo
