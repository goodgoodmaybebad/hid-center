// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BasePedalController.h"

#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoSettings.h"
#include "LedModeStore.h"

namespace hidcenter::simagic_alpha_evo {
namespace {

/// As SimPro sends them: each report three times, about 110 ms apart.
constexpr int kRepeats = 3;
constexpr int kPaceMs = 110;

/// How long after start-calibration has gone out the readings are taken from.
/// SimPro's capture had raw readings within 150 ms; this leaves room.
constexpr int kSettleMs = 300;

/// How often readingsChanged() may fire.
constexpr int kReadingsIntervalMs = 33;

/// Above anything seen in a group, so a damaged saved value is not taken.
constexpr int kGroupValueMax = 1000;

/// Raw counts are 12 bits.
constexpr int kRawMax = 4095;

QString settingsPath(const UsbDeviceInfo &info, const char *name)
{
    return QStringLiteral("devices/%1/basePedals/%2")
        .arg(ledSettingsKey(info), QLatin1String(name));
}

template<std::size_t N>
QString numbersToString(const std::array<quint16, N> &numbers)
{
    QStringList parts;
    for (const quint16 value : numbers)
        parts << QString::number(value);
    return parts.join(u',');
}

template<std::size_t N>
bool numbersFromString(const QString &text, int maximum, std::array<quint16, N> *numbers)
{
    const QStringList parts = text.split(u',');
    if (parts.size() != qsizetype(N))
        return false;
    std::array<quint16, N> parsed{};
    for (std::size_t i = 0; i < N; ++i) {
        bool ok = false;
        const int value = parts.at(qsizetype(i)).trimmed().toInt(&ok);
        if (!ok || value < 0 || value > maximum)
            return false;
        parsed[i] = quint16(value);
    }
    *numbers = parsed;
    return true;
}

bool isPedalGroup(int group)
{
    return group == kPedalBrakeGroup || group == kPedalThrottleGroup;
}

/// A sensor's new pair, from what was seen, or the old one if it barely moved.
void takePair(int lowest, int highest, int pair, PedalSettings *settings)
{
    if (highest - lowest < BasePedalController::kMinimumTravel)
        return;
    const int low = qBound(0, lowest + BasePedalController::kCalibrationMargin, kRawMax);
    const int high = qBound(low + 1, highest - BasePedalController::kCalibrationMargin, kRawMax);
    settings->calibration[std::size_t(2 * pair)] = quint16(low);
    settings->calibration[std::size_t(2 * pair + 1)] = quint16(high);
}

} // namespace

BasePedalController::BasePedalController(AlphaEvoSettings *settings, AlphaEvoButtonInput *input,
                                         UsbDeviceInfo info, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_input(input)
    , m_info(std::move(info))
    , m_pedals(capturedPedalSettings())
{
    const QSettings store;
    m_enabled = store.value(settingsPath(m_info, "enabled")).toBool();

    for (const auto &[group, name] : {std::pair{kPedalBrakeGroup, "brakeGroup"},
                                      std::pair{kPedalThrottleGroup, "throttleGroup"}}) {
        PedalGroup saved{};
        if (numbersFromString(store.value(settingsPath(m_info, name)).toString(), kGroupValueMax,
                              &saved)) {
            m_pedals.groups[std::size_t(group)] = saved;
        }
    }
    std::array<quint16, kPedalCalibrationCount> calibration{};
    if (numbersFromString(store.value(settingsPath(m_info, "calibration")).toString(), kRawMax,
                          &calibration)) {
        m_pedals.calibration = calibration;
    }
    if (store.value(settingsPath(m_info, "brakeSensor")).toString() == QLatin1String("angle"))
        m_pedals.brakeSensor = PedalBrakeSensor::Angle;

    m_pace = new QTimer(this);
    m_pace->setInterval(kPaceMs);
    m_pace->setTimerType(Qt::PreciseTimer);
    connect(m_pace, &QTimer::timeout, this, &BasePedalController::sendNext);

    m_readingsTimer = new QTimer(this);
    m_readingsTimer->setInterval(kReadingsIntervalMs);
    connect(m_readingsTimer, &QTimer::timeout, this, [this] {
        if (std::exchange(m_readingsDirty, false))
            Q_EMIT readingsChanged();
    });

    if (m_settings) {
        connect(m_settings, &AlphaEvoSettings::pedalReportSent, this,
                &BasePedalController::onReportSent);
    }
    if (m_input) {
        connect(m_input, &AlphaEvoButtonInput::inputReportReceived, this,
                &BasePedalController::onInputReport);
    }
}

BasePedalController::~BasePedalController()
{
    // Best effort: a calibration left running would leave the pedals reporting
    // raw readings. Unpaced, since there is no event loop left to pace it.
    if ((m_calibration == Calibration::Starting || m_calibration == Calibration::Measuring)
        && m_settings) {
        for (const QByteArray &report : composePedalCalibrationCancel(m_pedals))
            m_settings->writePedalReportAsync(report);
    }
    stopWatching();
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

int BasePedalController::curveOf(int group) const
{
    return isPedalGroup(group) ? pedalCurveOf(m_pedals.groups[std::size_t(group)]) : -1;
}

void BasePedalController::setCurve(int group, int curve)
{
    if (!isPedalGroup(group) || curve < 0 || curve >= kPedalCurveCount)
        return;
    PedalSettings next = m_pedals;
    PedalGroup &pedal = next.groups[std::size_t(group)];
    for (int i = 0; i < kPedalCurvePointCount; ++i) {
        pedal[std::size_t(kPedalCurveFirstIndex + i)] =
            kPedalCurves[std::size_t(curve)][std::size_t(i)];
    }
    change(next);
}

std::array<int, kPedalCurvePointCount> BasePedalController::curvePointsOf(int group) const
{
    std::array<int, kPedalCurvePointCount> percents{};
    if (!isPedalGroup(group))
        return percents;
    const PedalGroup &pedal = m_pedals.groups[std::size_t(group)];
    for (int i = 0; i < kPedalCurvePointCount; ++i)
        percents[std::size_t(i)] = pedalCurvePercent(pedal[std::size_t(kPedalCurveFirstIndex + i)]);
    return percents;
}

void BasePedalController::setCurvePoints(int group,
                                         const std::array<int, kPedalCurvePointCount> &percents)
{
    if (!isPedalGroup(group))
        return;
    PedalSettings next = m_pedals;
    PedalGroup &pedal = next.groups[std::size_t(group)];
    const std::array<int, kPedalCurvePointCount> current = curvePointsOf(group);
    int below = 0;
    for (int i = 0; i < kPedalCurvePointCount; ++i) {
        const int percent = qBound(below, percents[std::size_t(i)], 100);
        below = percent;
        // A point left as it was keeps its exact value, which a hand-drawn curve
        // may hold between two whole percentages.
        if (percent != current[std::size_t(i)])
            pedal[std::size_t(kPedalCurveFirstIndex + i)] = quint16(pedalCurveValue(percent));
    }
    change(next);
}

int BasePedalController::rangeStartOf(int group) const
{
    if (!isPedalGroup(group))
        return 0;
    return pedalRangePercent(
        m_pedals.groups[std::size_t(group)][std::size_t(kPedalRangeStartIndex)]);
}

int BasePedalController::rangeEndOf(int group) const
{
    if (!isPedalGroup(group))
        return 100;
    return pedalRangePercent(m_pedals.groups[std::size_t(group)][std::size_t(kPedalRangeEndIndex)]);
}

void BasePedalController::setRange(int group, int startPercent, int endPercent)
{
    if (!isPedalGroup(group))
        return;
    const int start = qBound(0, startPercent, 99);
    const int end = qBound(start + 1, endPercent, 100);
    PedalSettings next = m_pedals;
    PedalGroup &pedal = next.groups[std::size_t(group)];
    pedal[std::size_t(kPedalRangeStartIndex)] = quint16(pedalRangeValue(start));
    pedal[std::size_t(kPedalRangeEndIndex)] = quint16(pedalRangeValue(end));
    change(next);
}

BasePedalController::ProfileState BasePedalController::profileState() const
{
    ProfileState state;
    state.brakeSensor = brakeSensor();
    state.brakePoints = curvePointsOf(kPedalBrakeGroup);
    state.throttlePoints = curvePointsOf(kPedalThrottleGroup);
    state.brakeStart = rangeStartOf(kPedalBrakeGroup);
    state.brakeEnd = rangeEndOf(kPedalBrakeGroup);
    state.throttleStart = rangeStartOf(kPedalThrottleGroup);
    state.throttleEnd = rangeEndOf(kPedalThrottleGroup);
    return state;
}

void BasePedalController::setProfileState(const ProfileState &state)
{
    PedalSettings next = m_pedals;
    next.brakeSensor = state.brakeSensor;

    const auto apply = [&next](int group,
                               const std::array<int, kPedalCurvePointCount> &points,
                               int startPercent, int endPercent) {
        PedalGroup &pedal = next.groups[std::size_t(group)];
        int below = 0;
        for (int i = 0; i < kPedalCurvePointCount; ++i) {
            const int percent = qBound(below, points[std::size_t(i)], 100);
            below = percent;
            pedal[std::size_t(kPedalCurveFirstIndex + i)] =
                quint16(pedalCurveValue(percent));
        }
        const int start = qBound(0, startPercent, 99);
        const int end = qBound(start + 1, endPercent, 100);
        pedal[std::size_t(kPedalRangeStartIndex)] = quint16(pedalRangeValue(start));
        pedal[std::size_t(kPedalRangeEndIndex)] = quint16(pedalRangeValue(end));
    };

    apply(kPedalBrakeGroup, state.brakePoints, state.brakeStart, state.brakeEnd);
    apply(kPedalThrottleGroup, state.throttlePoints, state.throttleStart,
          state.throttleEnd);
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
    Q_EMIT settingsChanged();

    if (!m_enabled || !m_settings)
        return;
    if (isSending() || m_calibration != Calibration::Idle) {
        m_changedWhileSending = true;
        return;
    }
    sendReports(composePedalUpdate(m_pedals));
}

// --- Calibrating ------------------------------------------------------------------

bool BasePedalController::canCalibrate() const
{
    return m_enabled && m_settings && m_input && !isSending()
           && m_calibration == Calibration::Idle;
}

void BasePedalController::startCalibration()
{
    if (!canCalibrate())
        return;

    m_readings = {};
    m_input->setForwardsReports(true);
    m_input->setEnabled(true);
    setCalibration(Calibration::Starting);
    sendReports(composePedalCalibrationStart());
}

void BasePedalController::saveCalibration()
{
    if (m_calibration != Calibration::Starting && m_calibration != Calibration::Measuring)
        return;

    PedalSettings next = m_pedals;
    if (m_readings.seen) {
        const Readings &r = m_readings;
        takePair(r.lowest.brakeFirst, r.highest.brakeFirst, kPedalCalibrationBrakeFirstPair,
                 &next);
        takePair(r.lowest.brakeSecond, r.highest.brakeSecond, kPedalCalibrationBrakeSecondPair,
                 &next);
        takePair(r.lowest.throttle, r.highest.throttle, kPedalCalibrationThrottlePair, &next);
    }

    stopWatching();
    if (next != m_pedals) {
        m_pedals = next;
        save();
        Q_EMIT settingsChanged();
    }
    setCalibration(Calibration::Finishing);
    // Ends the calibration whatever was measured: with nothing new, the old
    // calibration goes back, and the update restates it.
    sendReports(composePedalCalibrationSave(m_pedals));
}

void BasePedalController::cancelCalibration()
{
    if (m_calibration != Calibration::Starting && m_calibration != Calibration::Measuring)
        return;

    stopWatching();
    setCalibration(Calibration::Finishing);
    sendReports(composePedalCalibrationCancel(m_pedals));
}

void BasePedalController::onInputReport(const QByteArray &report)
{
    if (m_calibration != Calibration::Measuring || !m_measuring.isValid()
        || m_measuring.elapsed() < kSettleMs) {
        return;
    }
    const std::optional<PedalRawReadings> raw = pedalRawReadings(report);
    if (!raw)
        return;

    Readings &r = m_readings;
    if (!r.seen) {
        r.seen = true;
        r.lowest = *raw;
        r.highest = *raw;
    }
    r.current = *raw;
    r.lowest.brakeFirst = std::min(r.lowest.brakeFirst, raw->brakeFirst);
    r.lowest.throttle = std::min(r.lowest.throttle, raw->throttle);
    r.lowest.brakeSecond = std::min(r.lowest.brakeSecond, raw->brakeSecond);
    r.highest.brakeFirst = std::max(r.highest.brakeFirst, raw->brakeFirst);
    r.highest.throttle = std::max(r.highest.throttle, raw->throttle);
    r.highest.brakeSecond = std::max(r.highest.brakeSecond, raw->brakeSecond);
    m_readingsDirty = true;
}

void BasePedalController::setCalibration(Calibration state)
{
    if (m_calibration == state)
        return;
    m_calibration = state;
    if (state == Calibration::Measuring) {
        m_measuring.start();
        m_readingsTimer->start();
    }
    Q_EMIT calibrationChanged(state);
}

void BasePedalController::stopWatching()
{
    m_readingsTimer->stop();
    m_measuring.invalidate();
    if (m_input) {
        m_input->setForwardsReports(false);
        m_input->setEnabled(false);
    }
}

// --- Sending ----------------------------------------------------------------------

void BasePedalController::sendReports(const QList<QByteArray> &reports)
{
    m_queue.clear();
    for (const QByteArray &report : reports) {
        for (int i = 0; i < kRepeats; ++i)
            m_queue << report;
    }
    m_updateFailed = false;
    m_updateError.clear();
    m_updateDryRun = false;

    const bool wasSending = isSending();
    m_pace->start();
    if (!wasSending)
        Q_EMIT sendingChanged(true);
    sendNext();
}

void BasePedalController::sendNext()
{
    if (!m_queue.isEmpty()) {
        if (m_settings)
            m_settings->writePedalReportAsync(m_queue.takeFirst());
        else
            m_queue.clear();
        return;
    }

    m_pace->stop();
    Q_EMIT updateSent(!m_updateFailed, m_updateError, m_updateDryRun);

    if (m_calibration == Calibration::Starting) {
        // A start that did not go out leaves nothing to measure, and nothing to
        // end.
        if (m_updateFailed) {
            stopWatching();
            setCalibration(Calibration::Idle);
        } else {
            setCalibration(Calibration::Measuring);
        }
    } else if (m_calibration == Calibration::Finishing) {
        setCalibration(Calibration::Idle);
    }

    if (m_calibration == Calibration::Idle && std::exchange(m_changedWhileSending, false)
        && m_enabled) {
        sendReports(composePedalUpdate(m_pedals));
        return;
    }
    if (!isSending())
        Q_EMIT sendingChanged(false);
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
                   numbersToString(m_pedals.groups[std::size_t(kPedalBrakeGroup)]));
    store.setValue(settingsPath(m_info, "throttleGroup"),
                   numbersToString(m_pedals.groups[std::size_t(kPedalThrottleGroup)]));
    store.setValue(settingsPath(m_info, "calibration"), numbersToString(m_pedals.calibration));
    store.setValue(settingsPath(m_info, "brakeSensor"),
                   m_pedals.brakeSensor == PedalBrakeSensor::Angle ? QStringLiteral("angle")
                                                                   : QStringLiteral("loadCell"));
}

} // namespace hidcenter::simagic_alpha_evo
