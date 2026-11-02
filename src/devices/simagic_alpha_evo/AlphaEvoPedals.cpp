// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlphaEvoPedals.h"

#include <cstddef>

namespace hidcenter::simagic_alpha_evo {
namespace {

constexpr int kReportSize = 64;
using Report = std::array<quint8, kReportSize>;

/// The pass-through's header: the settings report, its pass-through
/// sub-command, the pedals' address, the way out, then a length, a byte saying
/// no reply is wanted, and the command.
constexpr quint8 kReportId = 0x80;
constexpr quint8 kPassThrough = 0x1d;
constexpr quint8 kAddress = 0x0a;
constexpr quint8 kOut = 0x01;
constexpr quint8 kNoReply = 0xf1;
constexpr int kHeaderSize = 7;

constexpr quint8 kCommandCalibration = 0x20;
constexpr quint8 kCommandSettings = 0x21;
constexpr quint8 kCommandBrakeLoadCell = 0x1f;
constexpr quint8 kCommandBrakeAngle = 0x1e;

/// The range ends' scale: see pedalRangeValue().
constexpr int kRangeZero = 6;
constexpr int kRangeSpan = 296;

/// One report: the header, `size` bytes of data, and zeros to the end, as every
/// captured report is. The length counts the no-reply byte and the command.
constexpr Report composeReport(quint8 command, const quint8 *data, int size)
{
    Report report{};
    report[0] = kReportId;
    report[1] = kPassThrough;
    report[2] = kAddress;
    report[3] = kOut;
    report[4] = quint8(2 + size);
    report[5] = kNoReply;
    report[6] = command;
    for (int i = 0; i < size; ++i)
        report[std::size_t(kHeaderSize + i)] = data[i];
    return report;
}

constexpr Report calibrationReport(const PedalSettings &settings)
{
    std::array<quint8, 2 * kPedalCalibrationCount> data{};
    for (int i = 0; i < kPedalCalibrationCount; ++i) {
        data[std::size_t(2 * i)] = quint8(settings.calibration[std::size_t(i)] & 0xff);
        data[std::size_t(2 * i + 1)] = quint8(settings.calibration[std::size_t(i)] >> 8);
    }
    return composeReport(kCommandCalibration, data.data(), int(data.size()));
}

constexpr Report settingsReport(const PedalSettings &settings)
{
    std::array<quint8, 2 * kPedalGroupCount * kPedalGroupSize> data{};
    std::size_t at = 0;
    for (const PedalGroup &group : settings.groups) {
        for (const quint16 value : group) {
            data[at++] = quint8(value & 0xff);
            data[at++] = quint8(value >> 8);
        }
    }
    return composeReport(kCommandSettings, data.data(), int(data.size()));
}

constexpr Report brakeSensorReport(const PedalSettings &settings)
{
    return composeReport(settings.brakeSensor == PedalBrakeSensor::LoadCell
                             ? kCommandBrakeLoadCell
                             : kCommandBrakeAngle,
                         nullptr, 0);
}

constexpr PedalSettings settingsAsFirstCaptured()
{
    PedalSettings settings;
    settings.calibration = {1544, 2407, 1461, 2011, 922, 2160, 1523, 2490};
    settings.groups = {{
        {6, 340, 270, 200, 302},
        {6, 340, 270, 200, 302},
        {6, 340, 270, 156, 257},
        {6, 205, 270, 335, 302},
    }};
    settings.brakeSensor = PedalBrakeSensor::LoadCell;
    return settings;
}

// SIMAGIC SimPro Manager changing the brake's settings, typed out from
// `pedal-settings.pcapng` and never generated. Each report is 64 bytes, zeros
// after what is typed here, as captured. Every one was sent three times; these
// are the first of each.

/// Frame 15636: the calibration, as every update in the capture had it, and as
/// `pedal-calibration.pcapng` saved it.
constexpr Report kCaptureCalibration{
    0x80, 0x1d, 0x0a, 0x01, 0x12, 0xf1, 0x20, 0x08, 0x06, 0x67, 0x09, 0xb5,
    0x05, 0xdb, 0x07, 0x9a, 0x03, 0x70, 0x08, 0xf3, 0x05, 0xba, 0x09};

/// Frame 16223: every group, the brake's linear over its whole range.
constexpr Report kCaptureSettings{
    0x80, 0x1d, 0x0a, 0x01, 0x2a, 0xf1, 0x21, 0x06, 0x00, 0x54, 0x01, 0x0e,
    0x01, 0xc8, 0x00, 0x2e, 0x01, 0x06, 0x00, 0x54, 0x01, 0x0e, 0x01, 0xc8,
    0x00, 0x2e, 0x01, 0x06, 0x00, 0x54, 0x01, 0x0e, 0x01, 0x9c, 0x00, 0x01,
    0x01, 0x06, 0x00, 0xcd, 0x00, 0x0e, 0x01, 0x4f, 0x01, 0x2e, 0x01};

/// Frame 90926: the same, with the brake on SimPro's second curve.
constexpr Report kCaptureSettingsCurve2{
    0x80, 0x1d, 0x0a, 0x01, 0x2a, 0xf1, 0x21, 0x06, 0x00, 0x54, 0x01, 0x0e,
    0x01, 0xc8, 0x00, 0x2e, 0x01, 0x06, 0x00, 0x7e, 0x01, 0x46, 0x01, 0xf2,
    0x00, 0x2e, 0x01, 0x06, 0x00, 0x54, 0x01, 0x0e, 0x01, 0x9c, 0x00, 0x01,
    0x01, 0x06, 0x00, 0xcd, 0x00, 0x0e, 0x01, 0x4f, 0x01, 0x2e, 0x01};

/// Frames 34955 and 16806: the brake on its load cell, and on its angle sensor.
constexpr Report kCaptureBrakeLoadCell{0x80, 0x1d, 0x0a, 0x01, 0x02, 0xf1, 0x1f};
constexpr Report kCaptureBrakeAngle{0x80, 0x1d, 0x0a, 0x01, 0x02, 0xf1, 0x1e};

constexpr PedalSettings withBrakeCurve(PedalSettings settings, int curve)
{
    for (int i = 0; i < kPedalCurvePointCount; ++i) {
        settings.groups[std::size_t(kPedalBrakeGroup)][std::size_t(kPedalCurveFirstIndex + i)] =
            kPedalCurves[std::size_t(curve)][std::size_t(i)];
    }
    return settings;
}

constexpr PedalSettings withBrakeAngle(PedalSettings settings)
{
    settings.brakeSensor = PedalBrakeSensor::Angle;
    return settings;
}

static_assert(calibrationReport(settingsAsFirstCaptured()) == kCaptureCalibration,
              "the calibration report must match the capture");
static_assert(settingsReport(settingsAsFirstCaptured()) == kCaptureSettings,
              "the settings report must match the capture");
static_assert(settingsReport(withBrakeCurve(settingsAsFirstCaptured(), 1))
                  == kCaptureSettingsCurve2,
              "the brake on the second curve must match the capture: group 1 is the brake's");
static_assert(settingsReport(withBrakeCurve(settingsAsFirstCaptured(), 0)) == kCaptureSettings,
              "the captured brake is on the linear curve");
static_assert(brakeSensorReport(settingsAsFirstCaptured()) == kCaptureBrakeLoadCell,
              "the brake on its load cell must match the capture");
static_assert(brakeSensorReport(withBrakeAngle(settingsAsFirstCaptured())) == kCaptureBrakeAngle,
              "the brake on its angle sensor must match the capture");

constexpr int rangeValue(int percent)
{
    const int clamped = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    return kRangeZero + (clamped * kRangeSpan + 50) / 100;
}

constexpr int rangePercent(int value)
{
    const int clamped = value < kRangeZero                ? kRangeZero
                        : value > kRangeZero + kRangeSpan ? kRangeZero + kRangeSpan
                                                          : value;
    return ((clamped - kRangeZero) * 100 + kRangeSpan / 2) / kRangeSpan;
}

// The ends of the scale as captured, and what SimPro showed for values it sent:
// the brake's two while its sliders were dragged, then the throttle's, from
// `throttle-range.pcapng`, the same scale.
static_assert(rangeValue(0) == 6 && rangeValue(100) == 302, "the range runs from 6 to 302");
static_assert(rangePercent(41) == 12 && rangePercent(266) == 88,
              "41 is what SimPro showed as 12 %, and 266 as 88 %");
static_assert(rangePercent(257) == 85 && rangePercent(299) == 99,
              "the throttle's 257 is what SimPro showed as 85 %, and 299 as 99 %");

/// Which of kPedalCurves a group has, or -1 for none of them.
constexpr int curveIndexOf(const PedalGroup &group)
{
    for (int curve = 0; curve < kPedalCurveCount; ++curve) {
        bool same = true;
        for (int i = 0; i < kPedalCurvePointCount; ++i) {
            if (group[std::size_t(kPedalCurveFirstIndex + i)]
                != kPedalCurves[std::size_t(curve)][std::size_t(i)]) {
                same = false;
            }
        }
        if (same)
            return curve;
    }
    return -1;
}

static_assert(curveIndexOf({6, 340, 270, 156, 257}) == -1,
              "the throttle's custom curve from the first captures is none of SimPro's four");

/// The pedals as `throttle-range.pcapng` left them, the latest capture: the
/// brake starting at 13 %, the throttle linear from 0 % to 85 %. What an
/// update carries until the user changes something here.
constexpr PedalSettings latestSettings()
{
    PedalSettings settings = settingsAsFirstCaptured();
    settings.groups = {{
        {6, 340, 270, 200, 302},
        {44, 340, 270, 200, 302},
        {6, 340, 270, 200, 257},
        {6, 205, 270, 335, 302},
    }};
    return settings;
}

/// Frame 29772 of `throttle-range.pcapng`: SimPro's last update in it.
constexpr Report kCaptureLatestSettings{
    0x80, 0x1d, 0x0a, 0x01, 0x2a, 0xf1, 0x21, 0x06, 0x00, 0x54, 0x01, 0x0e,
    0x01, 0xc8, 0x00, 0x2e, 0x01, 0x2c, 0x00, 0x54, 0x01, 0x0e, 0x01, 0xc8,
    0x00, 0x2e, 0x01, 0x06, 0x00, 0x54, 0x01, 0x0e, 0x01, 0xc8, 0x00, 0x01,
    0x01, 0x06, 0x00, 0xcd, 0x00, 0x0e, 0x01, 0x4f, 0x01, 0x2e, 0x01};

static_assert(settingsReport(latestSettings()) == kCaptureLatestSettings,
              "the latest settings must match the capture");
static_assert(calibrationReport(latestSettings()) == kCaptureCalibration,
              "the calibration has not changed since it was captured");
constexpr PedalGroup latestGroup(int group)
{
    return latestSettings().groups[std::size_t(group)];
}

static_assert(rangePercent(latestGroup(kPedalBrakeGroup)[kPedalRangeStartIndex]) == 13,
              "the brake starts at 13 % in the latest capture");
static_assert(curveIndexOf(latestGroup(kPedalThrottleGroup)) == 0
                  && rangePercent(latestGroup(kPedalThrottleGroup)[kPedalRangeEndIndex]) == 85,
              "the throttle is linear, to 85 %, in the latest capture");

QByteArray toByteArray(const Report &report)
{
    return QByteArray(reinterpret_cast<const char *>(report.data()), int(report.size()));
}

} // namespace

PedalSettings capturedPedalSettings()
{
    return latestSettings();
}

int pedalCurveOf(const PedalGroup &group)
{
    return curveIndexOf(group);
}

int pedalRangeValue(int percent)
{
    return rangeValue(percent);
}

int pedalRangePercent(int value)
{
    return rangePercent(value);
}

QList<QByteArray> composePedalUpdate(const PedalSettings &settings)
{
    return {toByteArray(calibrationReport(settings)), toByteArray(settingsReport(settings)),
            toByteArray(brakeSensorReport(settings))};
}

} // namespace hidcenter::simagic_alpha_evo
