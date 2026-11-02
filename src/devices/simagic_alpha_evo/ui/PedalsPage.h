// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace hidcenter::simagic_alpha_evo {

class AlphaEvoButtonInput;

/// The *Pedals* tab: where pedals plugged into the base by CAN-Bus are, live.
///
/// Read only. The base reports such pedals as two more axes of its joystick
/// report, and this shows them as bars, with the raw value beside each. How
/// their settings are written, through the base, is not known, so nothing here
/// changes them.
///
/// `input` is read only while the page is shown, as the Wheel tab's is. It
/// should be one of its own: switching between the two tabs would otherwise let
/// one's hide event switch off the other's reading.
class PedalsPage : public QWidget
{
    Q_OBJECT

public:
    /// `input` may be null, which leaves a page that reads nothing.
    explicit PedalsPage(AlphaEvoButtonInput *input, QWidget *parent = nullptr);
    ~PedalsPage() override;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void showPedals();
    void showStatus();

    QPointer<AlphaEvoButtonInput> m_input;
    QProgressBar *m_throttle = nullptr;
    QProgressBar *m_brake = nullptr;
    QLabel *m_throttleRaw = nullptr;
    QLabel *m_brakeRaw = nullptr;
    QLabel *m_status = nullptr;
};

} // namespace hidcenter::simagic_alpha_evo
