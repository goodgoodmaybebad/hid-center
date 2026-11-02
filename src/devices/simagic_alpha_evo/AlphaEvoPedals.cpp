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

// --- Curve points -------------------------------------------------------------

constexpr int kCurveZero = 410;

constexpr int curveValue(int percent)
{
    const int clamped = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    return kCurveZero - (clamped * 28 + 5) / 10;
}

constexpr int curvePercent(int value)
{
    const int clamped = value < kCurveZero - 280 ? kCurveZero - 280
                        : value > kCurveZero     ? kCurveZero
                                                 : value;
    return ((kCurveZero - clamped) * 10 + 14) / 28;
}

constexpr bool presetIs(int curve, int at25, int at50, int at75)
{
    const PedalCurve &points = kPedalCurves[std::size_t(curve)];
    return points[0] == curveValue(at25) && points[1] == curveValue(at50)
           && points[2] == curveValue(at75) && curvePercent(points[0]) == at25
           && curvePercent(points[1]) == at50 && curvePercent(points[2]) == at75;
}

// The line is a reading of the four presets, so every one of them has to be on
// it, both ways.
static_assert(presetIs(0, 25, 50, 75), "linear is 25, 50 and 75 %");
static_assert(presetIs(1, 10, 30, 60), "the second curve is 10, 30 and 60 %");
static_assert(presetIs(2, 40, 70, 90), "the third curve is 40, 70 and 90 %");
static_assert(presetIs(3, 10, 50, 90), "the fourth curve is 10, 50 and 90 %");
static_assert(curveValue(0) == 410 && curveValue(100) == 130, "the line runs from 410 to 130");

// --- Raw readings --------------------------------------------------------------

/// Where they are in report 0x01, as received: the report's id is byte 0.
constexpr int kRawFirstByte = 30;
constexpr int kRawByteCount = 5;
using RawBytes = std::array<quint8, kRawByteCount>;

constexpr PedalRawReadings rawReadingsOf(const RawBytes &bytes)
{
    PedalRawReadings readings;
    readings.brakeFirst = bytes[0] | (bytes[1] & 0x0f) << 8;
    readings.throttle = bytes[2] | (bytes[3] & 0x0f) << 8;
    readings.brakeSecond = (bytes[3] >> 4) | bytes[4] << 4;
    return readings;
}

constexpr bool readsAs(const RawBytes &bytes, int brakeFirst, int throttle, int brakeSecond)
{
    const PedalRawReadings readings = rawReadingsOf(bytes);
    return readings.brakeFirst == brakeFirst && readings.throttle == throttle
           && readings.brakeSecond == brakeSecond;
}

// Bytes 30 to 34 of three reports from `pedal-calibration.pcapng`, while it was
// calibrating: frame 21419 at rest, 30803 with the brake pressed, and 42837 with
// the throttle pressed. Each pedal moves its own sensors and no other.
static_assert(readsAs({0xa7, 0x85, 0xe8, 0x05, 0x39}, 1447, 1512, 912),
              "at rest, every sensor near its lowest");
static_assert(readsAs({0xd9, 0x87, 0xe9, 0x95, 0x83}, 2009, 1513, 2105),
              "the brake moves both its sensors, and not the throttle's");
static_assert(readsAs({0xad, 0x85, 0xc7, 0x59, 0x39}, 1453, 2503, 917),
              "the throttle moves its sensor, and not the brake's");

// --- Calibrating -----------------------------------------------------------------

constexpr quint8 kCommandCalibrationStart = 0x1c;
constexpr quint8 kCommandCalibrationEnd = 0x1d;

// What SimPro restated after saving a calibration, after the update, typed out
// from `pedal-calibration.pcapng`: frames 54224, 55007 and 55634. What they set
// is not known. `1b`'s bytes are also in every `f6` reply captured.
constexpr std::array<quint8, 12> kAfterCalibration16{};
constexpr std::array<quint8, 12> kAfterCalibration1b{0x04, 0xff, 0x68, 0x00, 0x64, 0x04,
                                                     0x04, 0xff, 0x00, 0x00, 0x64, 0x04};
constexpr std::array<quint8, 5> kAfterCalibration17{};

/// Frames 19691 and 51350: start-calibration and end-calibration.
constexpr Report kCaptureCalibrationStart{0x80, 0x1d, 0x0a, 0x01, 0x02, 0xf1, 0x1c};
constexpr Report kCaptureCalibrationEnd{0x80, 0x1d, 0x0a, 0x01, 0x02, 0xf1, 0x1d};

/// Frames 54224, 55007 and 55634, whole.
constexpr Report kCaptureAfterCalibration16{0x80, 0x1d, 0x0a, 0x01, 0x0e, 0xf1, 0x16};
constexpr Report kCaptureAfterCalibration1b{0x80, 0x1d, 0x0a, 0x01, 0x0e, 0xf1, 0x1b,
                                            0x04, 0xff, 0x68, 0x00, 0x64, 0x04, 0x04,
                                            0xff, 0x00, 0x00, 0x64, 0x04};
constexpr Report kCaptureAfterCalibration17{0x80, 0x1d, 0x0a, 0x01, 0x07, 0xf1, 0x17};

static_assert(composeReport(kCommandCalibrationStart, nullptr, 0) == kCaptureCalibrationStart,
              "start-calibration must match the capture");
static_assert(composeReport(kCommandCalibrationEnd, nullptr, 0) == kCaptureCalibrationEnd,
              "end-calibration must match the capture");
static_assert(composeReport(0x16, kAfterCalibration16.data(), int(kAfterCalibration16.size()))
                  == kCaptureAfterCalibration16,
              "report 16 must match the capture");
static_assert(composeReport(0x1b, kAfterCalibration1b.data(), int(kAfterCalibration1b.size()))
                  == kCaptureAfterCalibration1b,
              "report 1b must match the capture");
static_assert(composeReport(0x17, kAfterCalibration17.data(), int(kAfterCalibration17.size()))
                  == kCaptureAfterCalibration17,
              "report 17 must match the capture");

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

int pedalCurveValue(int percent)
{
    return curveValue(percent);
}

int pedalCurvePercent(int value)
{
    return curvePercent(value);
}

std::optional<PedalRawReadings> pedalRawReadings(const QByteArray &report)
{
    if (report.size() < kRawFirstByte + kRawByteCount || quint8(report.at(0)) != 0x01)
        return std::nullopt;
    RawBytes bytes{};
    for (int i = 0; i < kRawByteCount; ++i)
        bytes[std::size_t(i)] = quint8(report.at(kRawFirstByte + i));
    return rawReadingsOf(bytes);
}

QList<QByteArray> composePedalCalibrationStart()
{
    return {toByteArray(composeReport(kCommandCalibrationStart, nullptr, 0))};
}

QList<QByteArray> composePedalCalibrationSave(const PedalSettings &settings)
{
    QList<QByteArray> reports{toByteArray(calibrationReport(settings)),
                              toByteArray(composeReport(kCommandCalibrationEnd, nullptr, 0))};
    reports << composePedalUpdate(settings);
    reports << toByteArray(composeReport(0x16, kAfterCalibration16.data(),
                                         int(kAfterCalibration16.size())))
            << toByteArray(composeReport(0x1b, kAfterCalibration1b.data(),
                                         int(kAfterCalibration1b.size())))
            << toByteArray(composeReport(0x17, kAfterCalibration17.data(),
                                         int(kAfterCalibration17.size())));
    return reports;
}

QList<QByteArray> composePedalCalibrationCancel(const PedalSettings &settings)
{
    return {toByteArray(calibrationReport(settings)),
            toByteArray(composeReport(kCommandCalibrationEnd, nullptr, 0))};
}

} // namespace hidcenter::simagic_alpha_evo
