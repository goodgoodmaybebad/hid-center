// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalCalibrationDialog.h"

#include "AlphaEvoPedals.h"
#include "BasePedalController.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Raw counts are 12 bits.
constexpr int kRawMax = 4095;

/// How long Save stays disabled once the readings start.
constexpr int kSaveDelayMs = 1000;

using Readings = BasePedalController::Readings;

/// The three sensors, in the rows' order.
int brakeFirst(const PedalRawReadings &r)
{
    return r.brakeFirst;
}
int brakeSecond(const PedalRawReadings &r)
{
    return r.brakeSecond;
}
int throttle(const PedalRawReadings &r)
{
    return r.throttle;
}
using Sensor = int (*)(const PedalRawReadings &);
constexpr Sensor kSensors[] = {brakeFirst, brakeSecond, throttle};

} // namespace

PedalCalibrationDialog::PedalCalibrationDialog(BasePedalController *pedals, QWidget *parent)
    : QDialog(parent)
    , m_pedals(pedals)
{
    setWindowTitle(tr("Calibrate pedals"));
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Press each pedal all the way down and let it all the way back, two or three times, "
           "then Save. A sensor that hardly moves keeps the calibration it had.\n"
           "Cancel, or closing this window, puts the old calibration back."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QString names[] = {tr("Brake, first sensor"), tr("Brake, second sensor"),
                             tr("Throttle")};
    for (int i = 0; i < int(m_rows.size()); ++i) {
        Row &row = m_rows[std::size_t(i)];
        grid->addWidget(new QLabel(names[i], this), i, 0);
        row.bar = new QProgressBar(this);
        row.bar->setRange(0, kRawMax);
        row.bar->setTextVisible(false);
        grid->addWidget(row.bar, i, 1);
        row.values = new QLabel(this);
        row.values->setFont(fixed);
        grid->addWidget(row.values, i, 2);
    }
    layout->addLayout(grid);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(this);
    m_save = buttons->addButton(tr("&Save"), QDialogButtonBox::AcceptRole);
    m_cancel = buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    // Pressed, not accepted: the window stays until the save has gone out.
    connect(m_save, &QPushButton::clicked, this, [this] {
        if (!m_pedals)
            return;
        m_saving = true;
        m_pedals->saveCalibration();
        showState();
    });
    connect(m_cancel, &QPushButton::clicked, this, &PedalCalibrationDialog::reject);

    if (m_pedals) {
        connect(m_pedals, &BasePedalController::readingsChanged, this,
                &PedalCalibrationDialog::showReadings);
        connect(m_pedals, &BasePedalController::calibrationChanged, this,
                &PedalCalibrationDialog::showState);
    }
    showReadings();
    showState();
}

void PedalCalibrationDialog::reject()
{
    if (m_pedals && !m_saving)
        m_pedals->cancelCalibration();
    QDialog::reject();
}

void PedalCalibrationDialog::showReadings()
{
    const Readings readings = m_pedals ? m_pedals->readings() : Readings{};
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows[i];
        const Sensor sensor = kSensors[i];
        if (!readings.seen) {
            row.bar->setValue(0);
            row.values->setText(tr("waiting…"));
            continue;
        }
        const int lowest = sensor(readings.lowest);
        const int highest = sensor(readings.highest);
        row.bar->setValue(sensor(readings.current));
        QString text = tr("%1   lowest %2   highest %3")
                           .arg(sensor(readings.current), 4)
                           .arg(lowest, 4)
                           .arg(highest, 4);
        if (highest - lowest < BasePedalController::kMinimumTravel)
            text += tr("   (not moved yet)");
        row.values->setText(text);
    }
}

void PedalCalibrationDialog::showState()
{
    using Calibration = BasePedalController::Calibration;
    const Calibration state = m_pedals ? m_pedals->calibration() : Calibration::Idle;

    switch (state) {
    case Calibration::Starting:
        m_status->setText(tr("Starting the calibration…"));
        break;
    case Calibration::Measuring:
        m_status->setText(tr("Calibrating: work the pedals now."));
        if (!m_saveArmed) {
            QTimer::singleShot(kSaveDelayMs, this, [this] {
                m_saveArmed = true;
                showState();
            });
        }
        break;
    case Calibration::Finishing:
        m_status->setText(m_saving ? tr("Saving…") : tr("Putting the old calibration back…"));
        break;
    case Calibration::Idle:
        if (m_saving) {
            // Done: the save went out, or failed, which the Pedals tab says.
            accept();
            return;
        }
        m_status->setText(tr("The calibration did not start. The Pedals tab says why."));
        break;
    }

    m_save->setEnabled(state == Calibration::Measuring && m_saveArmed && !m_saving);
    m_cancel->setEnabled(!m_saving
                         && (state == Calibration::Starting || state == Calibration::Measuring
                             || state == Calibration::Idle));
}

} // namespace hidcenter::simagic_alpha_evo
