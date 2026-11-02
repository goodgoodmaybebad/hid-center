// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BasePedalController.h"

#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <cstddef>
#include <utility>

#include "AlphaEvoSettings.h"
#include "LedModeStore.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// As SimPro sends them: each report three times, about 110 ms apart.
constexpr int kRepeats = 3;
constexpr int kPaceMs = 110;

/// Above anything seen in a group, so a damaged saved value is not taken.
constexpr int kGroupValueMax = 1000;

QString settingsPath(const UsbDeviceInfo &info, const char *name)
{
    return QStringLiteral("devices/%1/basePedals/%2")
        .arg(ledSettingsKey(info), QLatin1String(name));
}

QString groupToString(const PedalGroup &group)
{
    QStringList parts;
    for (const quint16 value : group)
        parts << QString::number(value);
    return parts.join(u',');
}

bool groupFromString(const QString &text, PedalGroup *group)
{
    const QStringList parts = text.split(u',');
    if (parts.size() != kPedalGroupSize)
        return false;
    PedalGroup parsed{};
    for (int i = 0; i < kPedalGroupSize; ++i) {
        bool ok = false;
        const int value = parts.at(i).trimmed().toInt(&ok);
        if (!ok || value < 0 || value > kGroupValueMax)
            return false;
        parsed[std::size_t(i)] = quint16(value);
    }
    *group = parsed;
    return true;
}

} // namespace

BasePedalController::BasePedalController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_info(std::move(info))
    , m_pedals(capturedPedalSettings())
{
    const QSettings store;
    m_enabled = store.value(settingsPath(m_info, "enabled")).toBool();

    PedalGroup brake{};
    if (groupFromString(store.value(settingsPath(m_info, "brakeGroup")).toString(), &brake))
        m_pedals.groups[std::size_t(kPedalBrakeGroup)] = brake;
    if (store.value(settingsPath(m_info, "brakeSensor")).toString() == QLatin1String("angle"))
        m_pedals.brakeSensor = PedalBrakeSensor::Angle;

    m_pace = new QTimer(this);
    m_pace->setInterval(kPaceMs);
    m_pace->setTimerType(Qt::PreciseTimer);
    connect(m_pace, &QTimer::timeout, this, &BasePedalController::sendNext);

    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::pedalReportSent, this,
                &BasePedalController::onReportSent);
    }
}

void BasePedalController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    QSettings().setValue(settingsPath(m_info, "enabled"), enabled);
}

void BasePedalController::setBrakeSensor(PedalBrakeSensor sensor)
{
    PedalSettings next = m_pedals;
    next.brakeSensor = sensor;
    change(next);
}

int BasePedalController::brakeCurve() const
{
    return pedalCurveOf(m_pedals.groups[std::size_t(kPedalBrakeGroup)]);
}

void BasePedalController::setBrakeCurve(int curve)
{
    if (curve < 0 || curve >= kPedalCurveCount)
        return;
    PedalSettings next = m_pedals;
    PedalGroup &brake = next.groups[std::size_t(kPedalBrakeGroup)];
    for (int i = 0; i < kPedalCurvePointCount; ++i) {
        brake[std::size_t(kPedalCurveFirstIndex + i)] =
            kPedalCurves[std::size_t(curve)][std::size_t(i)];
    }
    change(next);
}

int BasePedalController::brakeRangeStart() const
{
    return pedalRangePercent(
        m_pedals.groups[std::size_t(kPedalBrakeGroup)][std::size_t(kPedalRangeStartIndex)]);
}

int BasePedalController::brakeRangeEnd() const
{
    return pedalRangePercent(
        m_pedals.groups[std::size_t(kPedalBrakeGroup)][std::size_t(kPedalRangeEndIndex)]);
}

void BasePedalController::setBrakeRange(int startPercent, int endPercent)
{
    const int start = qBound(0, startPercent, 99);
    const int end = qBound(start + 1, endPercent, 100);
    PedalSettings next = m_pedals;
    PedalGroup &brake = next.groups[std::size_t(kPedalBrakeGroup)];
    brake[std::size_t(kPedalRangeStartIndex)] = quint16(pedalRangeValue(start));
    brake[std::size_t(kPedalRangeEndIndex)] = quint16(pedalRangeValue(end));
    change(next);
}

bool BasePedalController::isSending() const
{
    return m_pace->isActive();
}

void BasePedalController::change(const PedalSettings &next)
{
    if (next == m_pedals)
        return;
    m_pedals = next;
    save();

    if (!m_enabled || !m_settings)
        return;
    if (isSending()) {
        m_changedWhileSending = true;
        return;
    }
    startUpdate();
}

void BasePedalController::startUpdate()
{
    m_queue.clear();
    for (const QByteArray &report : composePedalUpdate(m_pedals)) {
        for (int i = 0; i < kRepeats; ++i)
            m_queue << report;
    }
    m_updateFailed = false;
    m_updateError.clear();
    m_updateDryRun = false;

    m_pace->start();
    Q_EMIT sendingChanged(true);
    sendNext();
}

void BasePedalController::sendNext()
{
    if (m_queue.isEmpty()) {
        m_pace->stop();
        Q_EMIT updateSent(!m_updateFailed, m_updateError, m_updateDryRun);
        if (std::exchange(m_changedWhileSending, false) && m_enabled) {
            startUpdate();
            return;
        }
        Q_EMIT sendingChanged(false);
        return;
    }
    m_settings->writePedalReportAsync(m_queue.takeFirst());
}

void BasePedalController::onReportSent(bool ok, const QString &error, bool dryRun)
{
    if (dryRun)
        m_updateDryRun = true;
    if (!ok) {
        m_updateFailed = true;
        m_updateError = error;
    }
}

void BasePedalController::save() const
{
    QSettings store;
    store.setValue(settingsPath(m_info, "brakeGroup"),
                   groupToString(m_pedals.groups[std::size_t(kPedalBrakeGroup)]));
    store.setValue(settingsPath(m_info, "brakeSensor"),
                   m_pedals.brakeSensor == PedalBrakeSensor::Angle ? QStringLiteral("angle")
                                                                   : QStringLiteral("loadCell"));
}

} // namespace hidcenter::simagic_alpha_evo
