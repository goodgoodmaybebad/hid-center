// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalsPage.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoPedals.h"
#include "BasePedalController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
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

/// SimPro's four curves, in its order: kPedalCurves. A curve that is none of
/// them, drawn by hand in SimPro, shows as the placeholder.
QComboBox *makeCurveChoice(const QString &objectName, QWidget *parent)
{
    auto *curve = new QComboBox(parent);
    curve->setObjectName(objectName);
    curve->addItem(QCoreApplication::translate("PedalsPage", "Linear"));
    curve->addItem(QCoreApplication::translate("PedalsPage", "Soft start (10 / 30 / 60 %)"));
    curve->addItem(QCoreApplication::translate("PedalsPage", "Strong start (40 / 70 / 90 %)"));
    curve->addItem(QCoreApplication::translate("PedalsPage", "S-shaped (10 / 50 / 90 %)"));
    curve->setPlaceholderText(
        QCoreApplication::translate("PedalsPage", "Custom, as SimPro set it"));
    curve->setToolTip(QCoreApplication::translate(
        "PedalsPage", "SimPro's four curves, in its order. The figures are how much the pedal "
                      "gives a quarter, half and three quarters of the way in."));
    return curve;
}

/// One end of a pedal's range, in percent.
QSpinBox *makeRangeEnd(const QString &objectName, int minimum, int maximum, QWidget *parent)
{
    auto *box = new QSpinBox(parent);
    box->setObjectName(objectName);
    box->setRange(minimum, maximum);
    box->setSuffix(QCoreApplication::translate("PedalsPage", " %"));
    box->setKeyboardTracking(false);
    return box;
}

/// A start and an end, side by side.
QHBoxLayout *rangeRow(QSpinBox *start, QSpinBox *end, QWidget *parent)
{
    auto *row = new QHBoxLayout;
    row->addWidget(start);
    row->addWidget(new QLabel(QCoreApplication::translate("PedalsPage", "to"), parent));
    row->addWidget(end);
    row->addStretch(1);
    return row;
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

    layout->addWidget(makeSettingsBox());
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

QGroupBox *PedalsPage::makeSettingsBox()
{
    auto *box = new QGroupBox(tr("Pedal settings"), this);
    box->setObjectName(QStringLiteral("pedalSettings"));
    auto *layout = new QVBoxLayout(box);

    m_settingsEnabled = new QCheckBox(tr("&Change the pedals' settings from HID Center"), box);
    m_settingsEnabled->setObjectName(QStringLiteral("pedalSettingsEnabled"));
    layout->addWidget(m_settingsEnabled);

    auto *warning = new QLabel(
        tr("Every change sends all of the pedals' settings, since they cannot be read back from "
           "them: these below, and the calibration SimPro saved when this was worked out. If "
           "you have calibrated the pedals or changed their settings in SimPro since, leave "
           "this off, or the next change here undoes it.\n"
           "Nothing is sent until you change something below."),
        box);
    warning->setWordWrap(true);
    warning->setEnabled(false);
    layout->addWidget(warning);

    auto *form = new QFormLayout;
    layout->addLayout(form);

    auto *brakeHeading = new QLabel(tr("<b>Brake</b>"), box);
    form->addRow(brakeHeading);

    m_brakeSensor = new QComboBox(box);
    m_brakeSensor->setObjectName(QStringLiteral("brakeSensor"));
    m_brakeSensor->addItem(tr("Load cell"), int(PedalBrakeSensor::LoadCell));
    m_brakeSensor->addItem(tr("Angle sensor"), int(PedalBrakeSensor::Angle));
    form->addRow(tr("&Sensor"), m_brakeSensor);

    m_brakeCurve = makeCurveChoice(QStringLiteral("brakeCurve"), box);
    form->addRow(tr("C&urve"), m_brakeCurve);

    m_brakeRangeStart = makeRangeEnd(QStringLiteral("brakeRangeStart"), 0, 99, box);
    m_brakeRangeEnd = makeRangeEnd(QStringLiteral("brakeRangeEnd"), 1, 100, box);
    form->addRow(tr("&Range"), rangeRow(m_brakeRangeStart, m_brakeRangeEnd, box));

    auto *throttleHeading = new QLabel(tr("<b>Throttle</b>"), box);
    form->addRow(throttleHeading);

    m_throttleCurve = makeCurveChoice(QStringLiteral("throttleCurve"), box);
    form->addRow(tr("Cur&ve"), m_throttleCurve);

    m_throttleRangeStart = makeRangeEnd(QStringLiteral("throttleRangeStart"), 0, 99, box);
    m_throttleRangeEnd = makeRangeEnd(QStringLiteral("throttleRangeEnd"), 1, 100, box);
    form->addRow(tr("R&ange"), rangeRow(m_throttleRangeStart, m_throttleRangeEnd, box));

    m_settingsStatus = new QLabel(box);
    m_settingsStatus->setObjectName(QStringLiteral("brakeSettingsStatus"));
    m_settingsStatus->setWordWrap(true);
    layout->addWidget(m_settingsStatus);

    if (m_pedals) {
        connect(m_settingsEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
            m_pedals->setEnabled(enabled);
            showSettings();
        });
        connect(m_brakeSensor, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
                m_pedals->setBrakeSensor(PedalBrakeSensor(m_brakeSensor->itemData(index).toInt()));
        });
        connect(m_brakeCurve, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
                m_pedals->setBrakeCurve(index);
        });
        const auto setBrakeRange = [this] {
            m_pedals->setBrakeRange(m_brakeRangeStart->value(), m_brakeRangeEnd->value());
            showSettings();
        };
        connect(m_brakeRangeStart, &QSpinBox::valueChanged, this, setBrakeRange);
        connect(m_brakeRangeEnd, &QSpinBox::valueChanged, this, setBrakeRange);

        connect(m_throttleCurve, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (index >= 0)
                m_pedals->setThrottleCurve(index);
        });
        const auto setThrottleRange = [this] {
            m_pedals->setThrottleRange(m_throttleRangeStart->value(), m_throttleRangeEnd->value());
            showSettings();
        };
        connect(m_throttleRangeStart, &QSpinBox::valueChanged, this, setThrottleRange);
        connect(m_throttleRangeEnd, &QSpinBox::valueChanged, this, setThrottleRange);

        connect(m_pedals, &BasePedalController::sendingChanged, this, [this](bool sending) {
            if (sending)
                m_settingsStatus->setText(tr("Sending to the pedals…"));
        });
        connect(m_pedals, &BasePedalController::updateSent, this,
                [this](bool ok, const QString &error, bool dryRun) {
                    if (!ok) {
                        m_settingsStatus->setText(
                            tr("Could not send to the pedals: %1").arg(error));
                    }
                    else if (dryRun)
                        m_settingsStatus->setText(tr("Dry run: logged, not sent."));
                    else
                        m_settingsStatus->setText(tr("Sent to the pedals."));
                });
    }
    showSettings();
    return box;
}

void PedalsPage::showSettings()
{
    const bool known = !m_pedals.isNull();
    const bool enabled = known && m_pedals->isEnabled();

    {
        const QSignalBlocker enabledBlocker(m_settingsEnabled);
        const QSignalBlocker sensorBlocker(m_brakeSensor);
        const QSignalBlocker curveBlocker(m_brakeCurve);
        const QSignalBlocker startBlocker(m_brakeRangeStart);
        const QSignalBlocker endBlocker(m_brakeRangeEnd);
        const QSignalBlocker throttleCurveBlocker(m_throttleCurve);
        const QSignalBlocker throttleStartBlocker(m_throttleRangeStart);
        const QSignalBlocker throttleEndBlocker(m_throttleRangeEnd);
        m_settingsEnabled->setChecked(enabled);
        if (known) {
            m_brakeSensor->setCurrentIndex(
                m_brakeSensor->findData(int(m_pedals->brakeSensor())));
            m_brakeCurve->setCurrentIndex(m_pedals->brakeCurve());
            m_brakeRangeStart->setValue(m_pedals->brakeRangeStart());
            m_brakeRangeEnd->setValue(m_pedals->brakeRangeEnd());
            m_throttleCurve->setCurrentIndex(m_pedals->throttleCurve());
            m_throttleRangeStart->setValue(m_pedals->throttleRangeStart());
            m_throttleRangeEnd->setValue(m_pedals->throttleRangeEnd());
        }
    }

    m_settingsEnabled->setEnabled(known);
    for (QWidget *control : {static_cast<QWidget *>(m_brakeSensor),
                             static_cast<QWidget *>(m_brakeCurve),
                             static_cast<QWidget *>(m_brakeRangeStart),
                             static_cast<QWidget *>(m_brakeRangeEnd),
                             static_cast<QWidget *>(m_throttleCurve),
                             static_cast<QWidget *>(m_throttleRangeStart),
                             static_cast<QWidget *>(m_throttleRangeEnd)}) {
        control->setEnabled(enabled);
    }
    if (!enabled)
        m_settingsStatus->clear();
}

} // namespace hidcenter::simagic_alpha_evo
