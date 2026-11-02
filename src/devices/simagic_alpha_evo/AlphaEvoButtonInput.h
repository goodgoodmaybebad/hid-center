// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include "AlphaEvoForceTrace.h"

namespace hidcenter {
class IUsbDevice;
class IUsbDeviceHidInterface;
class ReportDescriptor;
struct UsbInterfaceInfo;
} // namespace hidcenter

namespace hidcenter::simagic_alpha_evo {

/// The input report the joystick buttons are in: the same one the force rides
/// on.
inline constexpr quint8 kButtonReportId = kForceReportId;

/// Where one joystick button is in an input report.
struct ButtonBit
{
    /// The button's number, from 1, as the descriptor's Button usage.
    int button = 0;

    /// Its bit, counted from the first bit of the report as received, so past
    /// the report id when the descriptor declares ids.
    int bit = 0;
};

/// Every joystick button input report `reportId` declares, from the device's
/// own descriptor: each element of a data field on the Button usage page, one
/// bit each, in the order the report carries them. Empty when the report has
/// none, or is not declared.
///
/// Read from the descriptor rather than hardcoded, since the report is a
/// standard joystick report and its descriptor says exactly where the buttons
/// are: on the bases captured so far, 116 of them from bit 92 of the payload.
QList<ButtonBit> buttonBitsOf(const ReportDescriptor &descriptor, quint8 reportId);

/// The buttons a report has pressed, by number, lowest first. A bit past the
/// end of the report counts as released.
QList<int> pressedButtons(const QByteArray &report, const QList<ButtonBit> &bits);

/// Where the clutch paddles are in the joystick report, by their Generic
/// Desktop usage. Found on a GTC with its dual-clutch module by pulling each
/// paddle in turn: the left moves Slider and the right Rz, 0 released and the
/// top of the axis's range pulled fully. With the paddles synced, both drive
/// Slider and Rz stays at 0; see kWheelConfigClutchModeOffset.
inline constexpr quint32 kLeftClutchUsage = 0x00010036;   ///< Slider
inline constexpr quint32 kRightClutchUsage = 0x00010035;  ///< Rz

/// Where pedals connected to the base by CAN-Bus are in the joystick report, by
/// their Generic Desktop usage. Found with a two-pedal P1000 by pressing each
/// in turn: 0 released and the top of the axis's range pressed fully. Where a
/// clutch pedal goes has not been seen.
inline constexpr quint32 kThrottlePedalUsage = 0x00010031; ///< Y
inline constexpr quint32 kBrakePedalUsage = 0x00010032;    ///< Z

/// The byte of the joystick report, as received, where the wheel says what its
/// clutch ratio is set to, as a percentage. It followed every value written
/// to kWheelConfigClutchRatioOffset on a GTC, straight away. Not read for now:
/// the block has to be restated whole anyway, so the app's own value is the one
/// that holds.
inline constexpr int kClutchRatioEchoByte = 38;

/// Where one axis is in an input report.
struct AxisField
{
    /// Its first bit, counted from the first bit of the report as received.
    int bit = 0;
    int bitSize = 0;
    qint32 logicalMin = 0;
    qint32 logicalMax = 0;

    bool isValid() const { return bitSize > 0 && bitSize <= 32 && logicalMax > logicalMin; }
};

/// The axis with Generic Desktop usage `usage` (`(page << 16) | usage`) in
/// input report `reportId`, from the device's own descriptor. Invalid when the
/// report has no such axis.
AxisField axisFieldOf(const ReportDescriptor &descriptor, quint8 reportId, quint32 usage);

/// An axis's raw value in a report, held to its logical range. The axis's
/// logical minimum when the report is too short to hold it.
qint32 axisValue(const QByteArray &report, const AxisField &axis);

/// The joystick buttons pressed on the wheel, and where its clutch paddles and
/// any pedals on the base are, as the UI sees them.
///
/// A listener and a decode, like AlphaEvoForceTrace, and reading the same
/// report from the same interface, which it claims and lets go of in the same
/// shared way. The two claims are separate: the force trace switched off does
/// not stop this, nor the other way round.
///
/// **Off until asked for**, and meant to be asked for only while something
/// shows the buttons: the report comes about a thousand times a second.
/// pressedChanged() is emitted only when the set of pressed buttons changes, and
/// clutchChanged() only when a paddle moves, so what listens is not reached at
/// that rate while the wheel is left alone.
class AlphaEvoButtonInput : public QObject
{
    Q_OBJECT

public:
    /// `device` may be offline, in which case this reports Offline for ever and
    /// does nothing at all.
    explicit AlphaEvoButtonInput(IUsbDevice *device, QObject *parent = nullptr);

    /// Whether the interface is held open and read. On claims it, off lets go
    /// and reports every button released.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    ForceTraceSource sourceState() const { return m_state; }

    /// How many buttons the report has room for: the highest button number
    /// it declares. 0 when there is no report to read it from.
    int buttonCount() const { return m_buttonCount; }

    /// The buttons pressed, by number, lowest first, as the last report had
    /// them. Empty while not enabled.
    const QList<int> &pressed() const { return m_pressed; }

    /// One sentence saying why no buttons can be shown. Empty when they can.
    QString sourceDescription() const;

    /// Whether the report has both clutch axes. False for a base whose report
    /// declares neither, whatever wheel is on it.
    bool hasClutchAxes() const { return m_leftClutch.isValid() && m_rightClutch.isValid(); }

    /// How far each clutch axis is pulled, 0 to 1, as the last report had it.
    /// Both 0 while not enabled. Synced paddles are both on the left axis.
    qreal leftClutch() const;
    qreal rightClutch() const;

    /// Whether the report has both pedal axes. True whether or not pedals are
    /// plugged into the base: the axes are there either way, and read 0.
    bool hasPedalAxes() const { return m_throttle.isValid() && m_brake.isValid(); }

    /// How far each pedal is pressed, 0 to 1, and as the raw value with the top
    /// of its range, as the last report had it. 0 while not enabled.
    qreal throttle() const;
    qreal brake() const;
    qint32 throttleRaw() const { return m_throttleValue; }
    qint32 brakeRaw() const { return m_brakeValue; }
    qint32 throttleRawMax() const { return m_throttle.logicalMax; }
    qint32 brakeRawMax() const { return m_brake.logicalMax; }

    /// Whether every joystick report read is also handed on whole, through
    /// inputReportReceived(). Off by default: the report comes about a thousand
    /// times a second, and only a pedal calibration wants it raw.
    void setForwardsReports(bool forwards) { m_forwardsReports = forwards; }

Q_SIGNALS:
    void pressedChanged(const QList<int> &pressed);

    /// Either clutch axis moved.
    void clutchChanged();

    /// Either pedal axis moved.
    void pedalsChanged();

    /// A joystick report was read, as received. Only while setForwardsReports()
    /// is on.
    void inputReportReceived(const QByteArray &report);
    void sourceStateChanged(hidcenter::simagic_alpha_evo::ForceTraceSource state);

private Q_SLOTS:
    void onInputReport(const QByteArray &report);
    void onDeviceLost();

private:
    bool attach();
    void detach();
    void setState(ForceTraceSource state);
    void setPressed(const QList<int> &pressed);
    void setClutch(qint32 left, qint32 right);
    void setPedals(qint32 throttle, qint32 brake);

    /// Find the clutch and pedal axes in a descriptor, keeping what was found
    /// before for whichever pair it has none of.
    void findAxes(const ReportDescriptor &descriptor);

    /// The HID interface whose input report 0x01 has buttons, or nullptr.
    const UsbInterfaceInfo *buttonInterface() const;

    QPointer<IUsbDevice> m_usbDevice;
    QPointer<IUsbDeviceHidInterface> m_device;
    QMetaObject::Connection m_reportConnection;
    QMetaObject::Connection m_deviceGoneConnection;

    ForceTraceSource m_state = ForceTraceSource::NoSuchReport;
    bool m_enabled = false;
    bool m_forwardsReports = false;
    int m_buttonCount = 0;

    /// Taken from enumeration data, and again from the handle once open, which
    /// is the descriptor the reports actually follow.
    QList<ButtonBit> m_bits;
    QList<int> m_pressed;

    AxisField m_leftClutch;
    AxisField m_rightClutch;
    qint32 m_leftClutchValue = 0;
    qint32 m_rightClutchValue = 0;

    AxisField m_throttle;
    AxisField m_brake;
    qint32 m_throttleValue = 0;
    qint32 m_brakeValue = 0;
};

} // namespace hidcenter::simagic_alpha_evo
