// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalsPage.h"

#include "AlphaEvoButtonInput.h"
#include "AlphaEvoPedals.h"
#include "BasePedalController.h"
#include "PedalCalibrationDialog.h"
#include "PedalCurveEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
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
    m_brake.editor->setOutput(known ? m_input->brake() : -1.0);
    m_throttle.editor->setOutput(known ? m_input->throttle() : -1.0);
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

    // Side by side, as SimPro has them.
    auto *panels = new QHBoxLayout;
    layout->addLayout(panels);

    m_brake.group = kPedalBrakeGroup;
    m_throttle.group = kPedalThrottleGroup;

    m_brakeSensor = new QComboBox(box);
    m_brakeSensor->setObjectName(QStringLiteral("brakeSensor"));
    m_brakeSensor->addItem(tr("Load cell"), int(PedalBrakeSensor::LoadCell));
    m_brakeSensor->addItem(tr("Angle sensor"), int(PedalBrakeSensor::Angle));
    panels->addWidget(
        makePedalPanel(m_brake, tr("Brake"), QStringLiteral("brake"), m_brakeSensor, box));
    panels->addWidget(
        makePedalPanel(m_throttle, tr("Throttle"), QStringLiteral("throttle"), nullptr, box));

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

QGroupBox *PedalsPage::makePedalPanel(PedalControls &controls, const QString &title,
                                      const QString &name, QWidget *extra, QWidget *parent)
{
    auto *panel = new QGroupBox(title, parent);
    panel->setObjectName(name + QStringLiteral("Panel"));
    auto *layout = new QVBoxLayout(panel);

    // The curve's choice above the graph, as SimPro's icons are, with what the
    // pedal has beside it: the brake's sensor.
    auto *top = new QHBoxLayout;
    controls.curve = new QComboBox(panel);
    controls.curve->setObjectName(name + QStringLiteral("Curve"));
    controls.curve->addItem(tr("Linear"));
    controls.curve->addItem(tr("Soft start"));
    controls.curve->addItem(tr("Strong start"));
    controls.curve->addItem(tr("S-shaped"));
    controls.curve->setPlaceholderText(tr("Custom"));
    controls.curve->setToolTip(tr("SimPro's four curves, in its order. Dragging a dot makes "
                                  "the curve a custom one."));
    top->addWidget(controls.curve, 1);
    if (extra)
        top->addWidget(extra);
    layout->addLayout(top);

    controls.editor = new PedalCurveEditor(panel);
    controls.editor->setObjectName(name + QStringLiteral("CurveEditor"));
    controls.editor->setToolTip(
        tr("Drag a dot up or down for how much the pedal gives a quarter, half and three "
           "quarters of the way in, and the handles below for the part of its travel it uses."));
    layout->addWidget(controls.editor, 1);

    return panel;
}

void PedalsPage::connectPedal(const PedalControls &controls)
{
    const int group = controls.group;
    connect(controls.curve, &QComboBox::currentIndexChanged, this, [this, group](int index) {
        if (index >= 0)
            m_pedals->setCurve(group, index);
    });

    // A drag on the graph: both at once, the range first, so the points are
    // set against the range they were dragged on.
    connect(controls.editor, &PedalCurveEditor::edited, this, [this, controls] {
        const PedalCurveEditor *editor = controls.editor;
        m_pedals->setRange(controls.group, editor->rangeStart(), editor->rangeEnd());
        m_pedals->setCurvePoints(controls.group, editor->points());
        showSettings();
    });
}

void PedalsPage::showSettings()
{
    const bool known = !m_pedals.isNull();
    const bool enabled = known && m_pedals->isEnabled();
    const bool calibrating =
        known && m_pedals->calibration() != BasePedalController::Calibration::Idle;

    QList<QWidget *> controls{m_brakeSensor};
    for (const PedalControls *pedal : {&m_brake, &m_throttle})
        controls << pedal->curve;

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
                pedal->editor->setRange(m_pedals->rangeStartOf(pedal->group),
                                        m_pedals->rangeEndOf(pedal->group));
                pedal->editor->setPoints(points);
            }
        }
    }

    m_settingsEnabled->setEnabled(known && !calibrating);
    for (QWidget *control : controls)
        control->setEnabled(enabled && !calibrating);
    // Not disabled, only not draggable: the live position is worth seeing
    // whether or not the settings may be changed.
    for (const PedalControls *pedal : {&m_brake, &m_throttle})
        pedal->editor->setEditable(enabled && !calibrating);
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
