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

/// The brake settings of pedals plugged into the base by CAN-Bus, as this app
/// sets them: which sensor it reads, its curve, and the part of its travel it
/// uses.
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

    /// Whether brake changes are sent to the pedals. Kept between runs. Turning
    /// it on sends nothing by itself.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    PedalBrakeSensor brakeSensor() const { return m_pedals.brakeSensor; }
    void setBrakeSensor(PedalBrakeSensor sensor);

    /// Which of kPedalCurves the brake is on, or -1 for none of them.
    int brakeCurve() const;
    void setBrakeCurve(int curve);

    /// The part of the brake's travel it uses, in percent. The start is held
    /// below the end.
    int brakeRangeStart() const;
    int brakeRangeEnd() const;
    void setBrakeRange(int startPercent, int endPercent);

    /// Whether an update is going out now.
    bool isSending() const;

Q_SIGNALS:
    /// An update started going out, or finished.
    void sendingChanged(bool sending);

    /// An update finished: `ok` when every report of it was sent. `error` is the
    /// last write's failure.
    void updateSent(bool ok, const QString &error, bool dryRun);

private:
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
