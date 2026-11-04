// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/HomePage.h"

#include "app/InterfaceScale.h"
#include "app/LogPanel.h"
#include "app/LogWindow.h"
#include "core/DeveloperMode.h"
#include "core/log/LogModel.h"
#include "core/telemetry/TelemetryHub.h"
#include "core/usb/IUsbBackend.h"
#include "core/plugin/PluginManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

namespace hidcenter {
namespace {

const char *const kUdevCommands =
    "sudo cp udev/70-hid-center.rules /etc/udev/rules.d/\n"
    "sudo udevadm control --reload-rules\n"
    "sudo udevadm trigger\n";

QFrame *makeCard(const QString &title, QVBoxLayout **layoutOut, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("dashboardCard"));

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);

    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("dashboardCardTitle"));
    layout->addWidget(titleLabel);

    if (layoutOut)
        *layoutOut = layout;
    return card;
}

QLabel *makeMutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("dashboardMuted"));
    label->setWordWrap(true);
    return label;
}

QLabel *makeBadge(const QString &text, const QString &state, QWidget *parent)
{
    auto *badge = new QLabel(text, parent);
    badge->setObjectName(QStringLiteral("dashboardBadge"));
    badge->setProperty("state", state);
    badge->setAlignment(Qt::AlignCenter);
    return badge;
}

} // namespace

HomePage::HomePage(PluginManager *plugins, IUsbBackend *backend, QWidget *parent)
    : QWidget(parent)
    , m_plugins(plugins)
    , m_backend(backend)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(12);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    layout->addWidget(splitter);

    auto *content = new QWidget(splitter);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(14);

    buildDashboard(content);
    contentLayout->addStretch(1);

    auto *scroll = new QScrollArea(splitter);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    splitter->addWidget(scroll);

    // The log lives here rather than behind a menu because the most important
    // thing it carries — what a dry-run write would have sent to a device — is
    // useless if the user has to go looking for it.
    auto *logBox = new QGroupBox(tr("Log"), splitter);
    auto *logLayout = new QVBoxLayout(logBox);
    auto *logPanel = new LogPanel(logBox);
    logPanel->setPopOutVisible(true);
    connect(logPanel, &LogPanel::popOutRequested, this, &HomePage::popOutLog);
    logLayout->addWidget(logPanel);
    splitter->addWidget(logBox);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    TelemetryHub *hub = TelemetryHub::instance();
    connect(hub, &TelemetryHub::activeSourceChanged, this, &HomePage::refreshTelemetry);
    connect(hub, &TelemetryHub::receivingChanged, this, &HomePage::refreshTelemetry);
    connect(hub, &TelemetryHub::listeningChanged, this, &HomePage::refreshTelemetry);
    connect(hub, &TelemetryHub::testModeChanged, this, &HomePage::refreshTelemetry);

    if (LogModel *log = LogModel::instance()) {
        connect(log, &QAbstractItemModel::rowsInserted, this, &HomePage::refreshWarnings);
        connect(log, &QAbstractItemModel::rowsRemoved, this, &HomePage::refreshWarnings);
        connect(log, &QAbstractItemModel::modelReset, this, &HomePage::refreshWarnings);
    }

    refreshSystemCard();
    refreshWarnings();
    refreshTelemetry();
    rebuildDeviceCards();
}

void HomePage::buildDashboard(QWidget *parent)
{
    auto *layout = static_cast<QVBoxLayout *>(parent->layout());

    // --- Devices ---------------------------------------------------------
    auto *devicesHeading = new QHBoxLayout;
    auto *devicesTitle = new QLabel(tr("Devices"), parent);
    devicesTitle->setObjectName(QStringLiteral("dashboardSection"));
    devicesHeading->addWidget(devicesTitle);
    devicesHeading->addStretch(1);
    m_deviceSummary = makeMutedLabel(QString(), parent);
    m_deviceSummary->setObjectName(QStringLiteral("dashboardMuted"));
    m_deviceSummary->setWordWrap(false);
    devicesHeading->addWidget(m_deviceSummary);
    layout->addLayout(devicesHeading);

    m_devicesContainer = new QWidget(parent);
    m_deviceGrid = new QGridLayout(m_devicesContainer);
    m_deviceGrid->setContentsMargins(0, 0, 0, 0);
    m_deviceGrid->setHorizontalSpacing(12);
    m_deviceGrid->setVerticalSpacing(12);
    m_deviceGrid->setColumnStretch(0, 1);
    m_deviceGrid->setColumnStretch(1, 1);
    layout->addWidget(m_devicesContainer);

    m_emptyState = new QFrame(parent);
    m_emptyState->setObjectName(QStringLiteral("dashboardCard"));
    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(18, 16, 18, 16);
    emptyLayout->setSpacing(8);
    auto *emptyTitle = new QLabel(tr("No supported device is connected"), m_emptyState);
    emptyTitle->setObjectName(QStringLiteral("dashboardCardTitle"));
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(makeMutedLabel(
        tr("Plug a device in and it will appear here. If one is connected but not listed, "
           "it may not be readable by your user. Install the udev rules and replug it:"),
        m_emptyState));
    auto *commands = new QLabel(QString::fromLatin1(kUdevCommands), m_emptyState);
    commands->setObjectName(QStringLiteral("dashboardCode"));
    commands->setTextInteractionFlags(Qt::TextSelectableByMouse);
    emptyLayout->addWidget(commands);
    auto *copy = new QPushButton(tr("Copy commands"), m_emptyState);
    copy->setObjectName(QStringLiteral("dashboardAction"));
    connect(copy, &QPushButton::clicked, this, &HomePage::copyUdevCommands);
    emptyLayout->addWidget(copy, 0, Qt::AlignLeft);
    layout->addWidget(m_emptyState);

    // --- Telemetry and system -------------------------------------------
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(12);

    QVBoxLayout *telemetryLayout = nullptr;
    QFrame *telemetryCard = makeCard(tr("Telemetry"), &telemetryLayout, parent);
    m_telemetrySource = new QLabel(telemetryCard);
    m_telemetrySource->setObjectName(QStringLiteral("dashboardValue"));
    telemetryLayout->addWidget(m_telemetrySource);
    m_telemetryStatus = makeMutedLabel(QString(), telemetryCard);
    telemetryLayout->addWidget(m_telemetryStatus);
    m_telemetryDetail = makeMutedLabel(QString(), telemetryCard);
    m_telemetryDetail->setVisible(false);
    telemetryLayout->addWidget(m_telemetryDetail);
    auto *openTelemetry = new QPushButton(tr("Open telemetry"), telemetryCard);
    openTelemetry->setObjectName(QStringLiteral("dashboardAction"));
    connect(openTelemetry, &QPushButton::clicked, this, &HomePage::openTelemetryRequested);
    telemetryLayout->addWidget(openTelemetry, 0, Qt::AlignLeft);
    statusRow->addWidget(telemetryCard, 1);

    QVBoxLayout *systemLayout = nullptr;
    QFrame *systemCard = makeCard(tr("System"), &systemLayout, parent);
    m_systemBackend = makeMutedLabel(QString(), systemCard);
    m_systemModules = makeMutedLabel(QString(), systemCard);
    m_systemHotplug = makeMutedLabel(QString(), systemCard);
    m_systemWarnings = makeMutedLabel(QString(), systemCard);
    systemLayout->addWidget(m_systemBackend);
    systemLayout->addWidget(m_systemModules);
    systemLayout->addWidget(m_systemHotplug);
    systemLayout->addWidget(m_systemWarnings);
    systemLayout->addStretch(1);
    statusRow->addWidget(systemCard, 1);

    layout->addLayout(statusRow);

    // --- Preferences -----------------------------------------------------
    layout->addWidget(makeAppearanceBox(parent));
}

void HomePage::rebuildDeviceCards()
{
    if (!m_deviceGrid)
        return;

    while (QLayoutItem *item = m_deviceGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    int row = 0;
    int column = 0;
    for (const HomeDeviceInfo &device : m_devices) {
        auto *card = new QFrame(m_devicesContainer);
        card->setObjectName(QStringLiteral("dashboardCard"));

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(6);

        auto *nameRow = new QHBoxLayout;
        auto *name = new QLabel(device.name, card);
        name->setObjectName(QStringLiteral("dashboardCardTitle"));
        nameRow->addWidget(name, 1);

        const QString badgeState = device.failed ? QStringLiteral("error")
                                  : device.offline ? QStringLiteral("offline")
                                                   : QStringLiteral("ready");
        const QString badgeText = device.failed ? tr("ERROR")
                                 : device.offline ? tr("OFFLINE")
                                                  : tr("READY");
        nameRow->addWidget(makeBadge(badgeText, badgeState, card), 0, Qt::AlignTop);
        layout->addLayout(nameRow);

        QString meta = device.family;
        if (!device.usbId.isEmpty())
            meta += QStringLiteral("  ·  %1").arg(device.usbId);
        layout->addWidget(makeMutedLabel(meta, card));

        if (!device.firmware.isEmpty()) {
            auto *firmware = new QLabel(tr("Firmware %1").arg(device.firmware), card);
            firmware->setObjectName(QStringLiteral("dashboardMuted"));
            layout->addWidget(firmware);
        }

        if (!device.status.isEmpty()) {
            auto *status = new QLabel(device.status, card);
            status->setWordWrap(true);
            status->setObjectName(QStringLiteral("dashboardProblem"));
            layout->addWidget(status);
        }

        layout->addStretch(1);

        auto *open = new QPushButton(device.offline ? tr("Preview") : tr("Open"), card);
        open->setObjectName(QStringLiteral("dashboardAction"));
        const QString path = device.path;
        connect(open, &QPushButton::clicked, this,
                [this, path] { Q_EMIT openDeviceRequested(path); });
        layout->addWidget(open, 0, Qt::AlignLeft);

        m_deviceGrid->addWidget(card, row, column);
        if (++column == 2) {
            column = 0;
            ++row;
        }
    }

    // A leftover half-row should not make the next card stretch vertically.
    if (m_deviceGrid->rowCount() > 0)
        m_deviceGrid->setRowStretch(m_deviceGrid->rowCount(), 0);

    const bool empty = m_devices.isEmpty();
    m_devicesContainer->setVisible(!empty);
    m_emptyState->setVisible(empty);

    const int connected = int(m_devices.size());
    m_deviceSummary->setText(
        connected == 0 ? QString()
                       : connected == 1 ? tr("1 device connected")
                                        : tr("%1 devices connected").arg(connected));
}

void HomePage::setDevices(const QVector<HomeDeviceInfo> &devices)
{
    m_devices = devices;
    rebuildDeviceCards();
}

void HomePage::refreshTelemetry()
{
    const TelemetryHub *hub = TelemetryHub::instance();
    if (!m_telemetrySource)
        return;

    QString source;
    QString status;
    QString detail;

    if (hub->isTestMode()) {
        source = tr("Test mode");
        status = tr("Telemetry values are being set by hand.");
    } else if (!hub->activeSource().isEmpty()) {
        source = hub->activeSource();
        status = hub->isReceiving() ? tr("Telemetry is flowing.")
                                    : tr("The source is running but has gone quiet.");
    } else if (hub->isListening()) {
        source = tr("Waiting for a game");
        status = tr("Listening for telemetry.");
    } else {
        source = tr("Not listening");
        detail = hub->listeningProblem();
        status = detail.isEmpty() ? tr("No telemetry source is available.") : QString();
    }

    m_telemetrySource->setText(source);
    m_telemetryStatus->setText(status);
    m_telemetryDetail->setText(detail);
    m_telemetryDetail->setVisible(!detail.isEmpty());
}

void HomePage::refreshSystemCard()
{
    if (!m_systemBackend)
        return;

    m_systemBackend->setText(m_backend ? tr("HID backend: %1").arg(m_backend->name())
                                       : tr("No HID backend is available."));
    m_systemHotplug->setText(
        m_backend ? tr("Hotplug detection: %1")
                        .arg(m_backend->isMonitoring() ? tr("active") : tr("unavailable"))
                  : QString());

    const int modules = m_plugins ? int(m_plugins->plugins().size()) : 0;
    QString moduleText = modules == 1 ? tr("1 device module loaded")
                                      : tr("%1 device modules loaded").arg(modules);
    if (m_plugins && !m_plugins->rejected().isEmpty()) {
        const int rejected = int(m_plugins->rejected().size());
        moduleText += rejected == 1 ? tr(", 1 rejected") : tr(", %1 rejected").arg(rejected);
    }
    m_systemModules->setText(moduleText);

    QStringList moduleDetails;
    if (m_plugins) {
        for (const PluginManager::LoadedPlugin &plugin : m_plugins->plugins())
            moduleDetails << plugin.familyName;
        for (const PluginManager::RejectedPlugin &rejected : m_plugins->rejected()) {
            moduleDetails << tr("%1 — %2").arg(rejected.fileName, rejected.reason);
        }
    }
    m_systemModules->setToolTip(moduleDetails.join(QLatin1Char('\n')));
}

void HomePage::refreshWarnings()
{
    if (!m_systemWarnings)
        return;

    const LogModel *log = LogModel::instance();
    if (!log) {
        m_systemWarnings->setText(tr("Warnings: unavailable"));
        return;
    }

    int warnings = 0;
    QString latest;
    for (int row = log->rowCount() - 1; row >= 0; --row) {
        const LogEntry *entry = log->entryAt(row);
        if (!entry || messageSeverity(entry->type) < 2)
            continue;
        if (warnings == 0) {
            latest = entry->message.section(QLatin1Char('\n'), 0, 0);
            if (latest.size() > 160)
                latest = latest.left(157) + QStringLiteral("…");
        }
        ++warnings;
    }

    const QString objectName = warnings > 0 ? QStringLiteral("dashboardProblem")
                                            : QStringLiteral("dashboardMuted");
    if (m_systemWarnings->objectName() != objectName) {
        m_systemWarnings->setObjectName(objectName);
        m_systemWarnings->style()->unpolish(m_systemWarnings);
        m_systemWarnings->style()->polish(m_systemWarnings);
    }
    if (warnings == 0) {
        m_systemWarnings->setText(tr("Warnings: none"));
    } else {
        m_systemWarnings->setText(
            warnings == 1 ? tr("1 warning: %1").arg(latest)
                          : tr("%1 warnings, latest: %2").arg(warnings).arg(latest));
    }
    m_systemWarnings->setToolTip(warnings > 1 ? latest : QString());
}

void HomePage::copyUdevCommands()
{
    QApplication::clipboard()->setText(QString::fromLatin1(kUdevCommands));
}

QGroupBox *HomePage::makeAppearanceBox(QWidget *parent)
{
    auto *box = new QGroupBox(tr("Appearance"), parent);
    auto *layout = new QVBoxLayout(box);

    auto *row = new QHBoxLayout;
    auto *label = new QLabel(tr("&Interface size"), box);
    row->addWidget(label);
    auto *scale = new QComboBox(box);
    scale->setObjectName(QStringLiteral("interfaceScale"));
    for (const int percent : interfaceScales()) {
        scale->addItem(percent == 0 ? tr("Automatic, as the desktop asks")
                                    : tr("%1 %").arg(percent),
                       percent);
    }
    scale->setCurrentIndex(qMax(0, scale->findData(savedInterfaceScale())));
    label->setBuddy(scale);
    row->addWidget(scale);

    auto *restart = new QPushButton(tr("&Restart now"), box);
    restart->setObjectName(QStringLiteral("restartForScale"));
    restart->setVisible(false);
    row->addWidget(restart);
    row->addStretch(1);
    layout->addLayout(row);

    auto *note = new QLabel(box);
    note->setWordWrap(true);
    note->setEnabled(false);
    note->setText(interfaceScaleOverridden()
                      ? tr("QT_SCALE_FACTOR is set, so it decides the size, not this.")
                      : tr("Takes effect when HID Center starts again."));
    layout->addWidget(note);

    const int applied = savedInterfaceScale();
    connect(scale, &QComboBox::currentIndexChanged, this, [scale, restart, applied](int index) {
        const int percent = scale->itemData(index).toInt();
        saveInterfaceScale(percent);
        restart->setVisible(percent != applied && !interfaceScaleOverridden());
    });
    connect(restart, &QPushButton::clicked, this, [] {
        // The same program and arguments, then this one goes, closing its
        // devices as it would on any quit.
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                    QCoreApplication::arguments().mid(1))) {
            QCoreApplication::quit();
        }
    });
    return box;
}

void HomePage::popOutLog()
{
    if (!m_logWindow) {
        m_logWindow = new LogWindow(this);
        // Free it on close; the QPointer above then reads null and the next
        // press builds a fresh one.
        m_logWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_logWindow->showAndRaise();
}

} // namespace hidcenter
