// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include "AlphaEvoPedals.h"
#include "core/usb/UsbDeviceInfo.h"

class QTimer;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoSettings;

/// The settings of pedals plugged into the base by CAN-Bus, as this app sets
/// them: which sensor the brake reads, and for the brake and the throttle,
/// their curve and the part of their travel they use.
///
/// GUI thread. Holds the whole of an update, keeps what the user chose between
/// runs, and hands each update to AlphaEvoSettings a report at a time.
///
/// **Off unless the user turns it on, and silent until they change something.**
/// The pedals' calibration cannot be read back, and every update restates it,
/// so an update can only carry the calibration captured from SimPro
/// (capturedPedalSettings()). That is right only while the pedals still hold
/// it: a calibration made in SimPro since would be undone. So nothing is sent
/// at start-up, nor when this is turned on, and nothing at all while it is off.
///
/// **Every update is SimPro's, whole and paced as SimPro paces it**: the
/// calibration, the settings and the brake's sensor, each three times, about
/// 110 ms apart. A change made while one is going out is sent once it is done,
/// as one more update carrying the latest.
class BasePedalController : public QObject
{
    Q_OBJECT

public:
    /// `info` is only used to work out where the settings are kept.
    BasePedalController(AlphaEvoSettings *settings, UsbDeviceInfo info,
                        QObject *parent = nullptr);

    /// Whether changes are sent to the pedals. Kept between runs. Turning it on
    /// sends nothing by itself.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    PedalBrakeSensor brakeSensor() const { return m_pedals.brakeSensor; }
    void setBrakeSensor(PedalBrakeSensor sensor);

    /// Which of kPedalCurves the brake is on, or -1 for none of them.
    int brakeCurve() const { return curveOf(kPedalBrakeGroup); }
    void setBrakeCurve(int curve) { setCurve(kPedalBrakeGroup, curve); }

    /// The part of the brake's travel it uses, in percent. The start is held
    /// below the end.
    int brakeRangeStart() const { return rangeStartOf(kPedalBrakeGroup); }
    int brakeRangeEnd() const { return rangeEndOf(kPedalBrakeGroup); }
    void setBrakeRange(int startPercent, int endPercent)
    {
        setRange(kPedalBrakeGroup, startPercent, endPercent);
    }

    /// The same for the throttle. -1 for a curve that is none of SimPro's four,
    /// such as one drawn by hand in SimPro, which is kept until a preset is
    /// picked.
    int throttleCurve() const { return curveOf(kPedalThrottleGroup); }
    void setThrottleCurve(int curve) { setCurve(kPedalThrottleGroup, curve); }
    int throttleRangeStart() const { return rangeStartOf(kPedalThrottleGroup); }
    int throttleRangeEnd() const { return rangeEndOf(kPedalThrottleGroup); }
    void setThrottleRange(int startPercent, int endPercent)
    {
        setRange(kPedalThrottleGroup, startPercent, endPercent);
    }

    /// Whether an update is going out now.
    bool isSending() const;

Q_SIGNALS:
    /// An update started going out, or finished.
    void sendingChanged(bool sending);

    /// An update finished: `ok` when every report of it was sent. `error` is the
    /// last write's failure.
    void updateSent(bool ok, const QString &error, bool dryRun);

private:
    int curveOf(int group) const;
    void setCurve(int group, int curve);
    int rangeStartOf(int group) const;
    int rangeEndOf(int group) const;
    void setRange(int group, int startPercent, int endPercent);

    /// Take `next` as the settings, keep it, and send it if that is on.
    void change(const PedalSettings &next);

    void startUpdate();
    void sendNext();
    void onReportSent(bool ok, const QString &error, bool dryRun);
    void save() const;

    AlphaEvoSettings *m_settings = nullptr;
    UsbDeviceInfo m_info;
    PedalSettings m_pedals;
    bool m_enabled = false;

    /// The reports of the update going out and not yet sent, in order.
    QList<QByteArray> m_queue;
    QTimer *m_pace = nullptr;

    /// A change came while an update was going out.
    bool m_changedWhileSending = false;

    /// How the update going out has gone so far.
    bool m_updateFailed = false;
    QString m_updateError;
    bool m_updateDryRun = false;
};

} // namespace hidcenter::simagic_alpha_evo
