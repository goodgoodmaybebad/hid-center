// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QPointer>

#include <array>

class QLabel;
class QProgressBar;
class QPushButton;

namespace hidcenter::simagic_alpha_evo {

class BasePedalController;

/// Calibrating pedals on the base: each sensor's raw reading live, with the
/// lowest and highest seen, and Save and Cancel.
///
/// **Only a view.** The calibration itself is BasePedalController's, which the
/// opener starts. Closing the window any way but Save, Escape and the window's
/// own close button included, cancels it, which puts the old calibration back.
///
/// Save stays disabled for a second after the readings start, so a double
/// click on the button that opened this cannot land on it.
class PedalCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    PedalCalibrationDialog(BasePedalController *pedals, QWidget *parent = nullptr);

    void reject() override;

private:
    /// One sensor's row.
    struct Row
    {
        QProgressBar *bar = nullptr;
        QLabel *values = nullptr;
    };

    void showReadings();
    void showState();

    QPointer<BasePedalController> m_pedals;
    std::array<Row, 3> m_rows{};
    QLabel *m_status = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_cancel = nullptr;

    /// Save was pressed, so the window closes once the save has gone out.
    bool m_saving = false;
    bool m_saveArmed = false;
};

} // namespace hidcenter::simagic_alpha_evo
