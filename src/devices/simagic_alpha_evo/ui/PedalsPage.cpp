// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalsPage.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoPedals.h"
#include "BasePedalController.h"
#include "PedalCalibrationDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// The bars count in tenths of a percent, so a light touch still moves them.
constexpr int kBarSteps = 1000;

int barValue(qreal fraction)
{
    return int(std::lround(qBound(0.0, fraction, 1.0) * kBarSteps));
}

QSpinBox *makePercentBox(const QString &objectName, int minimum, int maximum, QWidget *parent)
{
    auto *box = new QSpinBox(parent);
    box->setObjectName(objectName);
    box->setRange(minimum, maximum);
    box->setSuffix(QStringLiteral(" %"));
    // Typing 35 would otherwise send 3 to the pedals on the way.
    box->setKeyboardTracking(false);
    return box;
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
    addRow(0, tr("Throttle"), &m_throttleBar, &m_throttleRaw);
    addRow(1, tr("Brake"), &m_brakeBar, &m_brakeRaw);
    m_throttleBar->setObjectName(QStringLiteral("throttleBar"));
    m_brakeBar->setObjectName(QStringLiteral("brakeBar"));
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
    m_throttleBar->setValue(known ? barValue(m_input->throttle()) : 0);
    m_brakeBar->setValue(known ? barValue(m_input->brake()) : 0);
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

// --- Settings ---------------------------------------------------------------------

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
           "them: these below, and the calibration. If you have calibrated the pedals or "
           "changed their settings in SimPro since, leave this off, or the next change here "
           "undoes it.\n"
           "Nothing is sent until you change something below."),
        box);
    warning->setWordWrap(true);
    warning->setEnabled(false);
    layout->addWidget(warning);

    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_brake.group = kPedalBrakeGroup;
    m_throttle.group = kPedalThrottleGroup;

    form->addRow(new QLabel(tr("<b>Brake</b>"), box));
    m_brakeSensor = new QComboBox(box);
    m_brakeSensor->setObjectName(QStringLiteral("brakeSensor"));
    m_brakeSensor->addItem(tr("Load cell"), int(PedalBrakeSensor::LoadCell));
    m_brakeSensor->addItem(tr("Angle sensor"), int(PedalBrakeSensor::Angle));
    form->addRow(tr("&Sensor"), m_brakeSensor);
    addPedalRows(m_brake, QStringLiteral("brake"), form, box);

    form->addRow(new QLabel(tr("<b>Throttle</b>"), box));
    addPedalRows(m_throttle, QStringLiteral("throttle"), form, box);

    auto *calibrateRow = new QHBoxLayout;
    m_calibrate = new QPushButton(tr("Ca&librate pedals…"), box);
    m_calibrate->setObjectName(QStringLiteral("calibratePedals"));
    calibrateRow->addWidget(m_calibrate);
    calibrateRow->addStretch(1);
    layout->addLayout(calibrateRow);

    m_settingsStatus = new QLabel(box);
    m_settingsStatus->setObjectName(QStringLiteral("pedalSettingsStatus"));
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
        connectPedal(m_brake);
        connectPedal(m_throttle);
        connect(m_calibrate, &QPushButton::clicked, this, &PedalsPage::openCalibration);

        connect(m_pedals, &BasePedalController::settingsChanged, this, &PedalsPage::showSettings);
        connect(m_pedals, &BasePedalController::calibrationChanged, this,
                &PedalsPage::showSettings);
        connect(m_pedals, &BasePedalController::sendingChanged, this, [this](bool sending) {
            if (sending)
                m_settingsStatus->setText(tr("Sending to the pedals…"));
            showSettings();
        });
        connect(m_pedals, &BasePedalController::updateSent, this,
                [this](bool ok, const QString &error, bool dryRun) {
                    if (!ok) {
                        m_settingsStatus->setText(
                            tr("Could not send to the pedals: %1").arg(error));
                    } else if (dryRun) {
                        m_settingsStatus->setText(tr("Dry run: logged, not sent."));
                    } else {
                        m_settingsStatus->setText(tr("Sent to the pedals."));
                    }
                });
    }
    showSettings();
    return box;
}

void PedalsPage::addPedalRows(PedalControls &controls, const QString &name, QFormLayout *form,
                              QWidget *parent)
{
    controls.curve = new QComboBox(parent);
    controls.curve->setObjectName(name + QStringLiteral("Curve"));
    controls.curve->addItem(tr("Linear"));
    controls.curve->addItem(tr("Soft start (10 / 30 / 60 %)"));
    controls.curve->addItem(tr("Strong start (40 / 70 / 90 %)"));
    controls.curve->addItem(tr("S-shaped (10 / 50 / 90 %)"));
    controls.curve->setPlaceholderText(tr("Custom"));
    controls.curve->setToolTip(tr("SimPro's four curves, in its order. Changing a point below "
                                  "makes the curve a custom one."));
    form->addRow(tr("Curve"), controls.curve);

    // How much the pedal gives a quarter, half and three quarters of the way in.
    auto *points = new QHBoxLayout;
    static const char *const kAt[] = {"25", "50", "75"};
    for (std::size_t i = 0; i < controls.points.size(); ++i) {
        points->addWidget(new QLabel(tr("at %1 %:").arg(QLatin1String(kAt[i])), parent));
        controls.points[i] =
            makePercentBox(name + QStringLiteral("Point%1").arg(i + 1), 0, 100, parent);
        points->addWidget(controls.points[i]);
    }
    points->addStretch(1);
    form->addRow(tr("Output"), points);

    auto *range = new QHBoxLayout;
    controls.rangeStart = makePercentBox(name + QStringLiteral("RangeStart"), 0, 99, parent);
    controls.rangeEnd = makePercentBox(name + QStringLiteral("RangeEnd"), 1, 100, parent);
    range->addWidget(controls.rangeStart);
    range->addWidget(new QLabel(tr("to"), parent));
    range->addWidget(controls.rangeEnd);
    range->addStretch(1);
    form->addRow(tr("Range"), range);
}

void PedalsPage::connectPedal(const PedalControls &controls)
{
    const int group = controls.group;
    connect(controls.curve, &QComboBox::currentIndexChanged, this, [this, group](int index) {
        if (index >= 0)
            m_pedals->setCurve(group, index);
    });

    const auto setPoints = [this, controls] {
        std::array<int, kPedalCurvePointCount> percents{};
        for (std::size_t i = 0; i < percents.size(); ++i)
            percents[i] = controls.points[i]->value();
        m_pedals->setCurvePoints(controls.group, percents);
        showSettings();
    };
    for (QSpinBox *point : controls.points)
        connect(point, &QSpinBox::valueChanged, this, setPoints);

    const auto setRange = [this, controls] {
        m_pedals->setRange(controls.group, controls.rangeStart->value(),
                           controls.rangeEnd->value());
        showSettings();
    };
    connect(controls.rangeStart, &QSpinBox::valueChanged, this, setRange);
    connect(controls.rangeEnd, &QSpinBox::valueChanged, this, setRange);
}

void PedalsPage::showSettings()
{
    const bool known = !m_pedals.isNull();
    const bool enabled = known && m_pedals->isEnabled();
    const bool calibrating =
        known && m_pedals->calibration() != BasePedalController::Calibration::Idle;

    QList<QWidget *> controls{m_brakeSensor};
    for (const PedalControls *pedal : {&m_brake, &m_throttle}) {
        controls << pedal->curve << pedal->rangeStart << pedal->rangeEnd;
        for (QSpinBox *point : pedal->points)
            controls << point;
    }

    {
        // Blockers cannot be copied, which std::vector does not need to.
        std::vector<QSignalBlocker> blockers;
        blockers.reserve(std::size_t(controls.size()) + 1);
        blockers.emplace_back(m_settingsEnabled);
        for (QWidget *control : controls)
            blockers.emplace_back(control);

        m_settingsEnabled->setChecked(enabled);
        if (known) {
            m_brakeSensor->setCurrentIndex(
                m_brakeSensor->findData(int(m_pedals->brakeSensor())));
            for (const PedalControls *pedal : {&m_brake, &m_throttle}) {
                pedal->curve->setCurrentIndex(m_pedals->curveOf(pedal->group));
                const std::array<int, kPedalCurvePointCount> points =
                    m_pedals->curvePointsOf(pedal->group);
                for (std::size_t i = 0; i < points.size(); ++i)
                    pedal->points[i]->setValue(points[i]);
                pedal->rangeStart->setValue(m_pedals->rangeStartOf(pedal->group));
                pedal->rangeEnd->setValue(m_pedals->rangeEndOf(pedal->group));
            }
        }
    }

    m_settingsEnabled->setEnabled(known && !calibrating);
    for (QWidget *control : controls)
        control->setEnabled(enabled && !calibrating);
    m_calibrate->setEnabled(known && m_pedals->canCalibrate());
    if (!enabled)
        m_settingsStatus->clear();
}

void PedalsPage::openCalibration()
{
    if (!m_pedals || !m_pedals->canCalibrate())
        return;

    auto *dialog = new PedalCalibrationDialog(m_pedals, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // open() rather than exec(): unplugging the base destroys this page, and the
    // window with it, which exec() would then return into.
    dialog->open();
    m_pedals->startCalibration();
}

} // namespace hidcenter::simagic_alpha_evo
