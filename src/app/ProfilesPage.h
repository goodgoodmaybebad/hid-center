// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace hidcenter {

class ProfileController;
class ProfileStore;

/// Named settings snapshots: capture the current device, keep a library, apply
/// one, and bind one to a telemetry source so a game can switch it on its own.
class ProfilesPage : public QWidget
{
    Q_OBJECT

public:
    ProfilesPage(ProfileStore *store, ProfileController *controller,
                 QWidget *parent = nullptr);

    /// Bring the device list up to date after a hotplug event.
    void refreshDevices();

private Q_SLOTS:
    void rebuildTable();
    void updateActions();

    void saveCurrent();
    void applySelected();
    void updateSelected();
    void duplicateSelected();
    void editSelected();
    void deleteSelected();
    void importProfiles();
    void exportSelected();

private:
    void buildHeader(QVBoxLayout *layout);
    void buildToolbar(QVBoxLayout *layout);
    void buildTable(QVBoxLayout *layout);

    QString selectedProfileId() const;
    QString selectedDevicePath() const;
    QString devicePathForProfile(const QString &profileId) const;

    void showMessage(const QString &message, bool error = false);

    ProfileStore *m_store = nullptr;
    ProfileController *m_controller = nullptr;

    QComboBox *m_device = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_empty = nullptr;
    QLabel *m_message = nullptr;

    QPushButton *m_apply = nullptr;
    QPushButton *m_update = nullptr;
    QPushButton *m_duplicate = nullptr;
    QPushButton *m_edit = nullptr;
    QPushButton *m_delete = nullptr;
    QPushButton *m_export = nullptr;
};

} // namespace hidcenter
