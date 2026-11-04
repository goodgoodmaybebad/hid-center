// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <array>

#include "AlphaEvoPedals.h"
#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;
class AlphaEvoSettings;

/// The settings of pedals plugged into the base by CAN-Bus, as this app sets
/// them: which sensor the brake reads, the brake's and the throttle's curve and
/// the part of their travel they use, and the calibration.
///
/// GUI thread. Holds the whole of an update, keeps what the user chose between
/// runs, and hands each update to AlphaEvoSettings a report at a time.
///
/// **Off unless the user turns it on, and silent until they change something.**
/// Nothing the pedals hold can be read back, and every update restates all of
/// it, so an update can only carry what was captured from SimPro
/// (capturedPedalSettings()) and what has been set here since. That is right
/// only while nothing else changes the pedals. So nothing is sent at start-up,
/// nor when this is turned on, and nothing at all while it is off.
///
/// **Every update is SimPro's, whole and paced as SimPro paces it**: each report
/// three times, about 110 ms apart. A change made while one is going out is sent
/// once it is done, as one more update carrying the latest.
///
/// **A calibration is SimPro's too**: start-calibration, then the raw readings
/// watched while the user works the pedals, then either the new calibration and
/// the update SimPro follows it with, or the old calibration back. A sensor that
/// barely moved keeps its old pair.
class BasePedalController : public QObject
{
    Q_OBJECT

public:
    enum class Calibration {
        Idle,
        Starting,  ///< start-calibration is going out
        Measuring, ///< the raw readings are being watched
        Finishing, ///< the save or the cancel is going out
    };
    Q_ENUM(Calibration)

    /// `info` is only used to work out where the settings are kept. `input` is
    /// read only while calibrating, and should be a reader of its own. Either
    /// pointer may be null, and with no `input` nothing can be calibrated.
    BasePedalController(AlphaEvoSettings *settings, AlphaEvoButtonInput *input,
                        UsbDeviceInfo info, QObject *parent = nullptr);
    ~BasePedalController() override;

    /// Whether changes are sent to the pedals. Kept between runs. Turning it on
    /// sends nothing by itself.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    PedalBrakeSensor brakeSensor() const { return m_pedals.brakeSensor; }
    void setBrakeSensor(PedalBrakeSensor sensor);

    /// Which of kPedalCurves a pedal is on, or -1 for none of them, such as a
    /// curve drawn by hand. `group` is kPedalBrakeGroup or kPedalThrottleGroup.
    int curveOf(int group) const;
    void setCurve(int group, int curve);

    /// A pedal's three curve points, as the output each gives, in percent, a
    /// quarter, half and three quarters of the way in. Each point is held to no
    /// less than the one before it.
    std::array<int, kPedalCurvePointCount> curvePointsOf(int group) const;
    void setCurvePoints(int group, const std::array<int, kPedalCurvePointCount> &percents);

    /// The part of a pedal's travel it uses, in percent. The start is held below
    /// the end.
    int rangeStartOf(int group) const;
    int rangeEndOf(int group) const;
    void setRange(int group, int startPercent, int endPercent);

    /// The whole editable state of the two pedals, for a profile. Presets are
    /// not included: the points themselves are the truth, and curveOf() derives
    /// the preset from them.
    struct ProfileState
    {
        PedalBrakeSensor brakeSensor = PedalBrakeSensor::LoadCell;
        std::array<int, kPedalCurvePointCount> brakePoints{25, 50, 75};
        std::array<int, kPedalCurvePointCount> throttlePoints{25, 50, 75};
        int brakeStart = 0;
        int brakeEnd = 100;
        int throttleStart = 0;
        int throttleEnd = 100;
    };

    ProfileState profileState() const;

    /// Put the whole state in one change, so an applied profile is a single
    /// update rather than one report per field. Does not change whether writes
    /// are enabled.
    void setProfileState(const ProfileState &state);

    /// Whether an update, or a calibration's reports, are going out now.
    bool isSending() const;

    Calibration calibration() const { return m_calibration; }

    /// Whether a calibration can be started now: turned on, nothing going out,
    /// and a reader to watch the sensors with.
    bool canCalibrate() const;

    /// Start one. Ignored unless canCalibrate().
    void startCalibration();

    /// Save what the readings showed, or put the old calibration back. Only while
    /// starting or measuring.
    void saveCalibration();
    void cancelCalibration();

    /// The readings as last read, and the lowest and highest seen, while
    /// measuring. `seen` is false until the first reading after the pedals have
    /// switched to raw.
    struct Readings
    {
        bool seen = false;
        PedalRawReadings current;
        PedalRawReadings lowest;
        PedalRawReadings highest;
    };
    const Readings &readings() const { return m_readings; }

    /// How far a sensor must have moved, in raw counts, for a save to take its
    /// new pair. Less than this keeps the old one: a pedal nobody pressed.
    static constexpr int kMinimumTravel = 200;

    /// How far inside what was seen a saved pair is, in raw counts, at each end,
    /// as SimPro's was.
    static constexpr int kCalibrationMargin = 16;

Q_SIGNALS:
    /// An update started going out, or finished.
    void sendingChanged(bool sending);

    /// An update finished: `ok` when every report of it was sent. `error` is the
    /// last write's failure.
    void updateSent(bool ok, const QString &error, bool dryRun);

    void calibrationChanged(hidcenter::simagic_alpha_evo::BasePedalController::Calibration state);

    /// readings() changed. At most about thirty times a second.
    void readingsChanged();

    /// The settings changed, here or by a calibration.
    void settingsChanged();

private:
    /// Take `next` as the settings, keep it, and send it if that is on.
    void change(const PedalSettings &next);

    /// Send `reports`, each three times in turn.
    void sendReports(const QList<QByteArray> &reports);
    void sendNext();
    void onReportSent(bool ok, const QString &error, bool dryRun);
    void onInputReport(const QByteArray &report);
    void setCalibration(Calibration state);
    void stopWatching();
    void save() const;

    QPointer<AlphaEvoSettings> m_settings;
    QPointer<AlphaEvoButtonInput> m_input;
    UsbDeviceInfo m_info;
    PedalSettings m_pedals;
    bool m_enabled = false;

    /// The reports going out and not yet sent, in order.
    QList<QByteArray> m_queue;
    QTimer *m_pace = nullptr;

    /// A change came while reports were going out.
    bool m_changedWhileSending = false;

    /// How what is going out has gone so far.
    bool m_updateFailed = false;
    QString m_updateError;
    bool m_updateDryRun = false;

    Calibration m_calibration = Calibration::Idle;
    Readings m_readings;

    /// Since the measuring started. Readings are not taken for the first part
    /// of it, while the pedals may still be reporting calibrated values.
    QElapsedTimer m_measuring;

    /// Holds readingsChanged() to the rate a person can watch.
    QTimer *m_readingsTimer = nullptr;
    bool m_readingsDirty = false;
};

} // namespace hidcenter::simagic_alpha_evo
