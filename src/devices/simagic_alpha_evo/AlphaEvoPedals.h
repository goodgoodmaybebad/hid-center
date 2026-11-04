// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QtGlobal>

#include <array>
#include <optional>

namespace hidcenter::simagic_alpha_evo {

// Pedals plugged into the base by CAN-Bus, reached through a pass-through in the
// settings report. Everything here is from two captures of SIMAGIC SimPro
// Manager and a two-pedal P1000; see docs/08-pedals-on-the-base.md.

/// How many raw readings the calibration holds: a lowest and a highest for each
/// of four sensors.
inline constexpr int kPedalCalibrationCount = 8;

/// The settings report's groups, and the numbers in each.
inline constexpr int kPedalGroupCount = 4;
inline constexpr int kPedalGroupSize = 5;

/// The group SimPro changed for every brake setting, and the one for every
/// throttle setting.
inline constexpr int kPedalBrakeGroup = 1;
inline constexpr int kPedalThrottleGroup = 2;

/// Where in a group the range's ends and the curve are.
inline constexpr int kPedalRangeStartIndex = 0;
inline constexpr int kPedalCurveFirstIndex = 1;
inline constexpr int kPedalCurvePointCount = 3;
inline constexpr int kPedalRangeEndIndex = 4;

/// Which of the brake's two sensors it reads.
enum class PedalBrakeSensor {
    LoadCell,
    Angle,
};

using PedalGroup = std::array<quint16, kPedalGroupSize>;

/// Everything one update to the pedals carries.
struct PedalSettings
{
    std::array<quint16, kPedalCalibrationCount> calibration{};
    std::array<PedalGroup, kPedalGroupCount> groups{};
    PedalBrakeSensor brakeSensor = PedalBrakeSensor::LoadCell;

    constexpr bool operator==(const PedalSettings &) const = default;
};

/// What SimPro sent last, in the latest capture: the calibration it made, every
/// group, the brake on its load cell from 13 % with a linear curve, and the
/// throttle linear to 85 %.
///
/// **One user's pedals.** The calibration cannot be read back, so whatever is
/// sent with an update is restated from here, or from what the user set since.
PedalSettings capturedPedalSettings();

/// SimPro's four curves, as a group's three curve points: linear, then the
/// other three in the order SimPro lists them.
inline constexpr int kPedalCurveCount = 4;
using PedalCurve = std::array<quint16, kPedalCurvePointCount>;
inline constexpr std::array<PedalCurve, kPedalCurveCount> kPedalCurves{{
    {340, 270, 200},
    {382, 326, 242},
    {298, 214, 158},
    {382, 270, 158},
}};

/// Which of kPedalCurves a group has, or -1 for none of them.
int pedalCurveOf(const PedalGroup &group);

/// A curve point as a group holds it, from the output it gives, 0 to 100 %, and
/// back: 410 at 0 %, 130 at 100 %, in a straight line. All four of SimPro's
/// curves are on that line exactly.
int pedalCurveValue(int percent);
int pedalCurvePercent(int value);

/// The pedals' sensors as the base reports them while calibrating, raw counts,
/// 12 bits each.
struct PedalRawReadings
{
    int brakeFirst = 0;
    int throttle = 0;
    int brakeSecond = 0;
};

/// Each sensor's lowest and highest raw count in PedalSettings::calibration:
/// the pair at that index, lowest first.
inline constexpr int kPedalCalibrationBrakeFirstPair = 1;
inline constexpr int kPedalCalibrationBrakeSecondPair = 2;
inline constexpr int kPedalCalibrationThrottlePair = 3;

/// The raw readings in joystick report `report`, as received, report id first.
/// Nothing when it is not report 0x01 or too short.
///
/// Only meaningful between the start and the end of a calibration: the same
/// bytes are the calibrated pedal axes otherwise. At fixed offsets, from the
/// capture, not from the descriptor.
std::optional<PedalRawReadings> pedalRawReadings(const QByteArray &report);

/// The reports that put the pedals into calibrating: start-calibration.
QList<QByteArray> composePedalCalibrationStart();

/// The reports that save a calibration and end it, as SimPro sent them:
/// `settings`'s calibration, end-calibration, then the whole update with that
/// calibration, then three more reports SimPro restated, whose meaning is not
/// known, as captured.
QList<QByteArray> composePedalCalibrationSave(const PedalSettings &settings);

/// The reports that end a calibration without changing it: `settings`'s
/// calibration, the one held before it started, and end-calibration.
QList<QByteArray> composePedalCalibrationCancel(const PedalSettings &settings);

/// A range end as a group holds it, from a percentage, 0 to 100, and back:
/// 6 at 0 %, 302 at 100 %, in a straight line. SimPro showed 41 as 12 % and
/// 266 as 88 %, which is that line.
int pedalRangeValue(int percent);
int pedalRangePercent(int value);

/// The three reports of an update, in the order SimPro sends them: the
/// calibration, the settings, then the brake's sensor. 64 bytes each, for
/// SET_FEATURE.
///
/// **Always all three.** The last may be what makes the pedals take the other
/// two, as the wheel's last report is, and on the wheel sending that one alone
/// wiped what it applied.
QList<QByteArray> composePedalUpdate(const PedalSettings &settings);

} // namespace hidcenter::simagic_alpha_evo
