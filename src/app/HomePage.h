// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVector>
#include <QWidget>

#include <QPointer>

class QGridLayout;
class QGroupBox;
class QLabel;

namespace hidcenter {

class IUsbBackend;
class LogWindow;
class PluginManager;

/// One connected device as the dashboard shows it.
struct HomeDeviceInfo
{
    QString path;
    QString name;
    QString family;
    QString usbId;
    QString firmware;
    QString status;
    bool offline = false;
    bool failed = false;
};

/// The landing page: what the application found, what it is doing now, and the
/// switches that only make sense while developing.
class HomePage : public QWidget
{
    Q_OBJECT

public:
    HomePage(PluginManager *plugins, IUsbBackend *backend, QWidget *parent = nullptr);

public Q_SLOTS:
    /// Replace the device cards after a hotplug event or a module update.
    void setDevices(const QVector<HomeDeviceInfo> &devices);

Q_SIGNALS:
    /// The debug option of the same name was toggled. The sidebar acts on it;
    /// this page only offers the switch.
    void showDisconnectedChanged(bool show);

    /// A dashboard card asked for one of the application's pages.
    void openDeviceRequested(const QString &devicePath);
    void openTelemetryRequested();

private Q_SLOTS:
    void popOutLog();
    void refreshTelemetry();

private:
    void buildDashboard(QWidget *parent);
    void rebuildDeviceCards();
    void refreshSystemCard();
    void refreshWarnings();
    void copyUdevCommands();

    /// The interface size, and a button to restart into a new one.
    QGroupBox *makeAppearanceBox(QWidget *parent);

    PluginManager *m_plugins = nullptr;
    IUsbBackend *m_backend = nullptr;

    QVector<HomeDeviceInfo> m_devices;
    QWidget *m_devicesContainer = nullptr;
    QGridLayout *m_deviceGrid = nullptr;
    QLabel *m_deviceSummary = nullptr;
    QWidget *m_emptyState = nullptr;

    QLabel *m_telemetrySource = nullptr;
    QLabel *m_telemetryStatus = nullptr;
    QLabel *m_telemetryDetail = nullptr;

    QLabel *m_systemBackend = nullptr;
    QLabel *m_systemModules = nullptr;
    QLabel *m_systemHotplug = nullptr;
    QLabel *m_systemWarnings = nullptr;

    // QPointer so closing the window (which deletes it) leaves nothing dangling
    // for the next press of "Pop out".
    QPointer<LogWindow> m_logWindow;
};

} // namespace hidcenter
