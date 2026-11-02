// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QProgressBar;
class QSpinBox;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;
class BasePedalController;

/// The *Pedals* tab: where pedals plugged into the base by CAN-Bus are, live,
/// and the brake's settings.
///
/// The base reports such pedals as two more axes of its joystick report, and
/// this shows them as bars, with the raw value beside each. Below them, the
/// brake's sensor, curve and range, which BasePedalController sends only once
/// the user has turned that on.
///
/// `input` is read only while the page is shown, as the Wheel tab's is. It
/// should be one of its own: switching between the two tabs would otherwise let
/// one's hide event switch off the other's reading.
class PedalsPage : public QWidget
{
    Q_OBJECT

public:
    /// Either may be null: no `input` leaves a page that reads nothing, and no
    /// `pedals` one whose brake settings cannot be changed.
    PedalsPage(AlphaEvoButtonInput *input, BasePedalController *pedals,
               QWidget *parent = nullptr);
    ~PedalsPage() override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void showPedals();
    void showStatus();

    /// The brake box: build it, fill it from the controller, and enable what
    /// may be changed.
    QGroupBox *makeBrakeBox();
    void showBrakeSettings();

    QPointer<AlphaEvoButtonInput> m_input;
    QPointer<BasePedalController> m_pedals;
    QCheckBox *m_brakeEnabled = nullptr;
    QComboBox *m_brakeSensor = nullptr;
    QComboBox *m_brakeCurve = nullptr;
    QSpinBox *m_brakeRangeStart = nullptr;
    QSpinBox *m_brakeRangeEnd = nullptr;
    QLabel *m_brakeStatus = nullptr;
    QProgressBar *m_throttle = nullptr;
    QProgressBar *m_brake = nullptr;
    QLabel *m_throttleRaw = nullptr;
    QLabel *m_brakeRaw = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
