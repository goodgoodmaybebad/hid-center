// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

#include <array>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;
class BasePedalController;
class PedalCurveEditor;

/// The *Pedals* tab: where pedals plugged into the base by CAN-Bus are, live,
/// and their settings.
///
/// The base reports such pedals as two more axes of its joystick report, and
/// this shows them as bars, with the raw value beside each. Below them, the
/// brake's sensor, and the brake's and the throttle's curve and range, which
/// BasePedalController sends only once the user has turned that on, and a
/// button that calibrates them, in PedalCalibrationDialog.
///
/// `input` is read only while the page is shown, as the Wheel tab's is. It
/// should be one of its own: switching between the two tabs would otherwise let
/// one's hide event switch off the other's reading.
class PedalsPage : public QWidget
{
    Q_OBJECT

public:
    /// Either may be null: no `input` leaves a page that reads nothing, and no
    /// `pedals` one whose settings cannot be changed.
    PedalsPage(AlphaEvoButtonInput *input, BasePedalController *pedals,
               QWidget *parent = nullptr);
    ~PedalsPage() override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    /// One pedal's settings controls: the graph to drag, and boxes for exact
    /// values beneath it.
    struct PedalControls
    {
        int group = 0;
        QComboBox *curve = nullptr;
        PedalCurveEditor *editor = nullptr;
        std::array<QSpinBox *, 3> points{};
        QSpinBox *rangeStart = nullptr;
        QSpinBox *rangeEnd = nullptr;
    };

    void showPedals();
    void showStatus();

    /// The settings box: build it, fill it from the controller, and enable
    /// what may be changed.
    QGroupBox *makeSettingsBox();
    /// A pedal's panel: `extra` first, if any, then its curve, the graph, its
    /// points and its range. `name` starts each control's object name.
    QGroupBox *makePedalPanel(PedalControls &controls, const QString &title,
                              const QString &name, QWidget *extra, QWidget *parent);
    void connectPedal(const PedalControls &controls);
    void showSettings();
    void openCalibration();

    QPointer<AlphaEvoButtonInput> m_input;
    QPointer<BasePedalController> m_pedals;
    QCheckBox *m_settingsEnabled = nullptr;
    QComboBox *m_brakeSensor = nullptr;
    PedalControls m_brake;
    PedalControls m_throttle;
    QPushButton *m_calibrate = nullptr;
    QLabel *m_settingsStatus = nullptr;

    QProgressBar *m_throttleBar = nullptr;
    QProgressBar *m_brakeBar = nullptr;
    QLabel *m_throttleRaw = nullptr;
    QLabel *m_brakeRaw = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
