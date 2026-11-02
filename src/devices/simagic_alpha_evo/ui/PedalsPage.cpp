// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalsPage.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoPedals.h"
#include "BasePedalController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSpinBox>
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

PedalsPage::PedalsPage(AlphaEvoButtonInput *input, BasePedalController *pedals,
                       QWidget *parent)
    : QWidget(parent)
    , m_input(input)
    , m_pedals(pedals)
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
           "their own."),
        this);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("pedalsStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    layout->addWidget(makeBrakeBox());
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

QGroupBox *PedalsPage::makeBrakeBox()
{
    auto *box = new QGroupBox(tr("Brake settings"), this);
    box->setObjectName(QStringLiteral("brakeSettings"));
    auto *layout = new QVBoxLayout(box);

    m_brakeEnabled = new QCheckBox(tr("&Change the brake's settings from HID Center"), box);
    m_brakeEnabled->setObjectName(QStringLiteral("brakeSettingsEnabled"));
    layout->addWidget(m_brakeEnabled);

    auto *warning = new QLabel(
        tr("Every change also sends the pedals' calibration, which cannot be read back from "
           "them: the one SimPro saved when this was worked out. If you have calibrated the "
           "pedals in SimPro since, leave this off, or the next change here undoes it.\n"
           "Nothing is sent until you change something below."),
        box);
    warning->setWordWrap(true);
    warning->setEnabled(false);
    layout->addWidget(warning);

    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_brakeSensor = new QComboBox(box);
    m_brakeSensor->setObjectName(QStringLiteral("brakeSensor"));
    m_brakeSensor->addItem(tr("Load cell"), int(PedalBrakeSensor::LoadCell));
    m_brakeSensor->addItem(tr("Angle sensor"), int(PedalBrakeSensor::Angle));
    form->addRow(tr("&Sensor"), m_brakeSensor);

    m_brakeCurve = new QComboBox(box);
    m_brakeCurve->setObjectName(QStringLiteral("brakeCurve"));
    m_brakeCurve->addItem(tr("Linear"));
    m_brakeCurve->addItem(tr("Soft start (10 / 30 / 60 %)"));
    m_brakeCurve->addItem(tr("Strong start (40 / 70 / 90 %)"));
    m_brakeCurve->addItem(tr("S-shaped (10 / 50 / 90 %)"));
    m_brakeCurve->setToolTip(tr("SimPro's four curves, in its order. The figures are how much "
                                "brake a quarter, half and three quarters of the way in give."));
    form->addRow(tr("C&urve"), m_brakeCurve);

    auto *range = new QHBoxLayout;
    m_brakeRangeStart = new QSpinBox(box);
    m_brakeRangeStart->setObjectName(QStringLiteral("brakeRangeStart"));
    m_brakeRangeStart->setRange(0, 99);
    m_brakeRangeStart->setSuffix(tr(" %"));
    m_brakeRangeStart->setKeyboardTracking(false);
    range->addWidget(m_brakeRangeStart);
    range->addWidget(new QLabel(tr("to"), box));
    m_brakeRangeEnd = new QSpinBox(box);
    m_brakeRangeEnd->setObjectName(QStringLiteral("brakeRangeEnd"));
    m_brakeRangeEnd->setRange(1, 100);
    m_brakeRangeEnd->setSuffix(tr(" %"));
    m_brakeRangeEnd->setKeyboardTracking(false);
    range->addWidget(m_brakeRangeEnd);
    range->addStretch(1);
    form->addRow(tr("&Range"), range);

    m_brakeStatus = new QLabel(box);
    m_brakeStatus->setObjectName(QStringLiteral("brakeSettingsStatus"));
    m_brakeStatus->setWordWrap(true);
    layout->addWidget(m_brakeStatus);

    if (m_pedals) {
        connect(m_brakeEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
            m_pedals->setEnabled(enabled);
            showBrakeSettings();
        });
        connect(m_brakeSensor, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
                m_pedals->setBrakeSensor(PedalBrakeSensor(m_brakeSensor->itemData(index).toInt()));
        });
        connect(m_brakeCurve, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
                m_pedals->setBrakeCurve(index);
        });
        const auto setRange = [this] {
            m_pedals->setBrakeRange(m_brakeRangeStart->value(), m_brakeRangeEnd->value());
            showBrakeSettings();
        };
        connect(m_brakeRangeStart, &QSpinBox::valueChanged, this, setRange);
        connect(m_brakeRangeEnd, &QSpinBox::valueChanged, this, setRange);

        connect(m_pedals, &BasePedalController::sendingChanged, this, [this](bool sending) {
            if (sending)
                m_brakeStatus->setText(tr("Sending to the pedals…"));
        });
        connect(m_pedals, &BasePedalController::updateSent, this,
                [this](bool ok, const QString &error, bool dryRun) {
                    if (!ok)
                        m_brakeStatus->setText(tr("Could not send to the pedals: %1").arg(error));
                    else if (dryRun)
                        m_brakeStatus->setText(tr("Dry run: logged, not sent."));
                    else
                        m_brakeStatus->setText(tr("Sent to the pedals."));
                });
    }
    showBrakeSettings();
    return box;
}

void PedalsPage::showBrakeSettings()
{
    const bool known = !m_pedals.isNull();
    const bool enabled = known && m_pedals->isEnabled();

    {
        const QSignalBlocker enabledBlocker(m_brakeEnabled);
        const QSignalBlocker sensorBlocker(m_brakeSensor);
        const QSignalBlocker curveBlocker(m_brakeCurve);
        const QSignalBlocker startBlocker(m_brakeRangeStart);
        const QSignalBlocker endBlocker(m_brakeRangeEnd);
        m_brakeEnabled->setChecked(enabled);
        if (known) {
            m_brakeSensor->setCurrentIndex(
                m_brakeSensor->findData(int(m_pedals->brakeSensor())));
            m_brakeCurve->setCurrentIndex(m_pedals->brakeCurve());
            m_brakeRangeStart->setValue(m_pedals->brakeRangeStart());
            m_brakeRangeEnd->setValue(m_pedals->brakeRangeEnd());
        }
    }

    m_brakeEnabled->setEnabled(known);
    for (QWidget *control : {static_cast<QWidget *>(m_brakeSensor),
                             static_cast<QWidget *>(m_brakeCurve),
                             static_cast<QWidget *>(m_brakeRangeStart),
                             static_cast<QWidget *>(m_brakeRangeEnd)}) {
        control->setEnabled(enabled);
    }
    if (!enabled)
        m_brakeStatus->clear();
}

} // namespace hidcenter::simagic_alpha_evo
