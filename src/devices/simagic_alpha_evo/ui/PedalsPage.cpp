// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalsPage.h"

#include "AlphaEvoButtonInput.h"

#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The bars count in tenths of a percent, so a light touch still moves them.
constexpr int kBarSteps = 1000;

int barValue(qreal fraction)
{
    return int(std::lround(qBound(0.0, fraction, 1.0) * kBarSteps));
}

} // namespace

PedalsPage::PedalsPage(AlphaEvoButtonInput *input, QWidget *parent)
    : QWidget(parent)
    , m_input(input)
{
    auto *layout = new QVBoxLayout(this);

    auto *box = new QGroupBox(tr("Pedals on the wheelbase"), this);
    auto *grid = new QGridLayout(box);
    grid->setColumnStretch(1, 1);

    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto addRow = [&](int row, const QString &name, QProgressBar **bar, QLabel **raw) {
        grid->addWidget(new QLabel(name, box), row, 0);
        *bar = new QProgressBar(box);
        (*bar)->setRange(0, kBarSteps);
        (*bar)->setFormat(QStringLiteral("%p %"));
        grid->addWidget(*bar, row, 1);
        *raw = new QLabel(box);
        (*raw)->setFont(fixed);
        (*raw)->setEnabled(false);
        grid->addWidget(*raw, row, 2);
    };
    addRow(0, tr("Throttle"), &m_throttle, &m_throttleRaw);
    addRow(1, tr("Brake"), &m_brake, &m_brakeRaw);
    m_throttle->setObjectName(QStringLiteral("throttleBar"));
    m_brake->setObjectName(QStringLiteral("brakeBar"));
    layout->addWidget(box);

    auto *note = new QLabel(
        tr("For pedals plugged into the wheelbase (CAN-Bus mode, blue light). Both bars stay "
           "at 0 with no pedals on the base, or with pedals in USB mode, which are a device of "
           "their own. Their settings cannot be changed here yet."),
        this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("pedalsStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch(1);

    if (m_input) {
        connect(m_input, &AlphaEvoButtonInput::pedalsChanged, this, &PedalsPage::showPedals);
        connect(m_input, &AlphaEvoButtonInput::sourceStateChanged, this, &PedalsPage::showStatus);
    }
    showPedals();
    showStatus();
}

PedalsPage::~PedalsPage()
{
    // A page destroyed while shown gets no hide event.
    if (m_input)
        m_input->setEnabled(false);
}

void PedalsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_input)
        m_input->setEnabled(true);
    showStatus();
}

void PedalsPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_input)
        m_input->setEnabled(false);
}

void PedalsPage::showPedals()
{
    const bool known = m_input && m_input->hasPedalAxes();
    m_throttle->setValue(known ? barValue(m_input->throttle()) : 0);
    m_brake->setValue(known ? barValue(m_input->brake()) : 0);
    m_throttleRaw->setText(known ? tr("%1 / %2").arg(m_input->throttleRaw(), 5)
                                       .arg(m_input->throttleRawMax())
                                 : QString());
    m_brakeRaw->setText(known ? tr("%1 / %2").arg(m_input->brakeRaw(), 5)
                                    .arg(m_input->brakeRawMax())
                              : QString());
}

void PedalsPage::showStatus()
{
    QString why;
    if (!m_input) {
        why = tr("Preview only: no pedals are read.");
    } else {
        switch (m_input->sourceState()) {
        case ForceTraceSource::Available:
            if (!m_input->hasPedalAxes())
                why = tr("This wheelbase's joystick report has no pedal axes.");
            break;
        case ForceTraceSource::NoSuchReport:
            why = tr("This wheelbase has no joystick report to read pedals from.");
            break;
        case ForceTraceSource::Offline:
            why = tr("Not connected: pedals are unavailable.");
            break;
        case ForceTraceSource::DeviceGone:
            why = tr("Wheelbase unplugged: pedals are unavailable.");
            break;
        case ForceTraceSource::OpenFailed:
            // Says it could not open the base, and why.
            why = m_input->sourceDescription();
            break;
        }
    }
    m_status->setText(why);
    m_status->setVisible(!why.isEmpty());
}

} // namespace hidcenter::simagic_alpha_evo
