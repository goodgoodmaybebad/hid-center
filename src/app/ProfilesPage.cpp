// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/ProfilesPage.h"

#include "app/ProfileController.h"
#include "app/ProfileStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace hidcenter {
namespace {

class ProfileDialog : public QDialog
{
public:
    ProfileDialog(const QString &title, const QStringList &games, const QString &name,
                  const QString &game, bool autoApply, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(title);
        setModal(true);

        auto *layout = new QVBoxLayout(this);

        auto *form = new QFormLayout;
        m_name = new QLineEdit(name, this);
        m_name->setPlaceholderText(tr("GT3 baseline"));
        form->addRow(tr("Name"), m_name);

        m_game = new QComboBox(this);
        m_game->addItem(tr("Manual only"), QString());
        for (const QString &source : games)
            m_game->addItem(source, source);
        form->addRow(tr("Game"), m_game);

        m_auto = new QCheckBox(tr("Apply automatically when this game starts sending telemetry"),
                               this);
        form->addRow(QString(), m_auto);
        layout->addLayout(form);

        auto *hint = new QLabel(
            tr("Profiles cover force feedback and the pedals for a SIMAGIC base, and the "
               "pedal settings for SIMSONN. Applying one writes those settings to the device, "
               "just as changing them by hand would. Pedal settings only reach the pedals "
               "while pedal writes are enabled on the Pedals tab."),
            this);
        hint->setWordWrap(true);
        hint->setEnabled(false);
        layout->addWidget(hint);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                             this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);

        const int index = m_game->findData(game);
        if (!game.isEmpty() && index < 0)
            m_game->addItem(game, game);
        m_game->setCurrentIndex(game.isEmpty() ? 0 : m_game->findData(game));
        if (m_game->currentIndex() < 0)
            m_game->setCurrentIndex(0);
        m_auto->setChecked(autoApply && !game.isEmpty());
        m_auto->setEnabled(!game.isEmpty());
        connect(m_game, &QComboBox::currentIndexChanged, this, [this] {
            const bool hasGame = !m_game->currentData().toString().isEmpty();
            m_auto->setEnabled(hasGame);
            if (!hasGame)
                m_auto->setChecked(false);
        });

        resize(460, sizeHint().height());
        m_name->setFocus();
    }

    QString profileName() const { return m_name->text().trimmed(); }
    QString game() const { return m_game->currentData().toString(); }
    bool autoApply() const { return m_auto->isChecked(); }

    void accept() override
    {
        if (profileName().isEmpty()) {
            QMessageBox::warning(this, tr("Profiles"), tr("Give the profile a name."));
            return;
        }
        QDialog::accept();
    }

private:
    QLineEdit *m_name = nullptr;
    QComboBox *m_game = nullptr;
    QCheckBox *m_auto = nullptr;
};

QString fileNameForProfile(const QString &name)
{
    QString safe;
    for (const QChar character : name) {
        if (character.isLetterOrNumber() || character == QLatin1Char('-')
            || character == QLatin1Char('_'))
            safe += character;
        else if (!character.isSpace())
            safe += QLatin1Char('-');
    }
    return safe.isEmpty() ? QStringLiteral("profile") : safe;
}

} // namespace

ProfilesPage::ProfilesPage(ProfileStore *store, ProfileController *controller, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
    , m_controller(controller)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(12);

    buildHeader(layout);
    buildToolbar(layout);
    buildTable(layout);

    connect(m_store, &ProfileStore::changed, this, &ProfilesPage::rebuildTable);
    connect(m_controller, &ProfileController::devicesChanged, this,
            &ProfilesPage::refreshDevices);
    connect(m_controller, &ProfileController::statusMessage, this,
            [this](const QString &message) { showMessage(message); });

    refreshDevices();
    rebuildTable();
}

void ProfilesPage::buildHeader(QVBoxLayout *layout)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(4, 2, 4, 2);
    header->setSpacing(10);

    auto *title = new QLabel(tr("Profiles"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.45);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    header->addWidget(title);

    auto *hint = new QLabel(
        tr("Named snapshots of a device's settings, ready to apply or switch when a game starts."),
        this);
    hint->setObjectName(QStringLiteral("profilesHint"));
    hint->setEnabled(false);
    header->addWidget(hint, 0, Qt::AlignVCenter);
    header->addStretch(1);
    layout->addLayout(header);
}

void ProfilesPage::buildToolbar(QVBoxLayout *layout)
{
    auto *bar = new QHBoxLayout;

    auto *deviceLabel = new QLabel(tr("&Device"), this);
    bar->addWidget(deviceLabel);

    m_device = new QComboBox(this);
    m_device->setObjectName(QStringLiteral("profileDevice"));
    m_device->setMinimumWidth(240);
    deviceLabel->setBuddy(m_device);
    bar->addWidget(m_device);
    connect(m_device, &QComboBox::currentIndexChanged, this, &ProfilesPage::updateActions);

    auto *save = new QPushButton(tr("Save current as profile…"), this);
    save->setObjectName(QStringLiteral("saveProfile"));
    connect(save, &QPushButton::clicked, this, &ProfilesPage::saveCurrent);
    bar->addWidget(save);

    bar->addStretch(1);

    auto *import = new QPushButton(tr("Import…"), this);
    connect(import, &QPushButton::clicked, this, &ProfilesPage::importProfiles);
    bar->addWidget(import);

    layout->addLayout(bar);
}

void ProfilesPage::buildTable(QVBoxLayout *layout)
{
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName(QStringLiteral("profilesTable"));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Device"), tr("Switch")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setShowGrid(false);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ProfilesPage::updateActions);
    connect(m_table, &QTableWidget::cellDoubleClicked, this,
            [this](int, int) { applySelected(); });
    layout->addWidget(m_table, 1);

    m_empty = new QLabel(
        tr("No profiles yet. Choose a device above and save its current settings as a profile."),
        this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);
    m_empty->setMinimumHeight(80);
    m_empty->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(m_empty, 1);

    auto *actions = new QHBoxLayout;
    m_apply = new QPushButton(tr("Apply"), this);
    m_apply->setToolTip(tr("Write this profile to a matching connected device."));
    connect(m_apply, &QPushButton::clicked, this, &ProfilesPage::applySelected);
    actions->addWidget(m_apply);

    m_update = new QPushButton(tr("Update from device"), this);
    m_update->setToolTip(tr("Replace this profile's saved values with the device's current ones."));
    connect(m_update, &QPushButton::clicked, this, &ProfilesPage::updateSelected);
    actions->addWidget(m_update);

    m_duplicate = new QPushButton(tr("Duplicate"), this);
    connect(m_duplicate, &QPushButton::clicked, this, &ProfilesPage::duplicateSelected);
    actions->addWidget(m_duplicate);

    m_edit = new QPushButton(tr("Edit…"), this);
    connect(m_edit, &QPushButton::clicked, this, &ProfilesPage::editSelected);
    actions->addWidget(m_edit);

    m_delete = new QPushButton(tr("Delete"), this);
    connect(m_delete, &QPushButton::clicked, this, &ProfilesPage::deleteSelected);
    actions->addWidget(m_delete);

    actions->addStretch(1);

    m_export = new QPushButton(tr("Export…"), this);
    connect(m_export, &QPushButton::clicked, this, &ProfilesPage::exportSelected);
    actions->addWidget(m_export);

    layout->addLayout(actions);

    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    m_message->setVisible(false);
    layout->addWidget(m_message);
}

void ProfilesPage::refreshDevices()
{
    const QString selected = selectedDevicePath();
    m_device->blockSignals(true);
    m_device->clear();

    for (const ProfileDeviceInfo &info : m_controller->devices()) {
        if (!info.supportsProfiles)
            continue;
        QString name = info.name;
        if (!info.usbId.isEmpty())
            name += QStringLiteral("  ·  %1").arg(info.usbId);
        if (info.offline)
            name += tr(" (offline)");
        m_device->addItem(name, info.path);
    }

    const int index = selected.isEmpty() ? -1 : m_device->findData(selected);
    if (index >= 0)
        m_device->setCurrentIndex(index);
    m_device->blockSignals(false);
    m_device->setEnabled(m_device->count() > 0);
    updateActions();
}

void ProfilesPage::rebuildTable()
{
    const QString selected = selectedProfileId();
    m_table->blockSignals(true);
    m_table->setRowCount(0);

    for (const DeviceProfile &profile : m_store->profiles()) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto *name = new QTableWidgetItem(profile.name);
        name->setData(Qt::UserRole, profile.id);
        m_table->setItem(row, 0, name);

        m_table->setItem(row, 1, new QTableWidgetItem(profile.family));

        QString switching = tr("Manual");
        if (!profile.game.isEmpty()) {
            switching = profile.game;
            if (profile.autoApply)
                switching = tr("%1 (auto)").arg(profile.game);
        }
        m_table->setItem(row, 2, new QTableWidgetItem(switching));

        if (profile.id == selected)
            m_table->selectRow(row);
    }
    m_table->blockSignals(false);

    if (m_table->rowCount() > 0 && m_table->selectedItems().isEmpty())
        m_table->selectRow(0);

    const bool empty = m_store->profiles().isEmpty();
    m_table->setVisible(!empty);
    m_empty->setVisible(empty);
    updateActions();
}

void ProfilesPage::updateActions()
{
    const bool hasProfile = !selectedProfileId().isEmpty();
    for (QPushButton *button : {m_apply, m_update, m_duplicate, m_edit, m_delete, m_export})
        button->setEnabled(hasProfile);
    m_apply->setEnabled(hasProfile && m_device->count() > 0);
    m_update->setEnabled(hasProfile && m_device->count() > 0);
}

QString ProfilesPage::selectedProfileId() const
{
    const QList<QTableWidgetItem *> selected = m_table->selectedItems();
    if (selected.isEmpty())
        return {};
    const QTableWidgetItem *first = m_table->item(selected.first()->row(), 0);
    return first ? first->data(Qt::UserRole).toString() : QString();
}

QString ProfilesPage::selectedDevicePath() const
{
    return m_device->currentData().toString();
}

QString ProfilesPage::devicePathForProfile(const QString &profileId) const
{
    const DeviceProfile *profile = m_store->find(profileId);
    if (!profile)
        return {};
    return m_controller->matchingDevicePath(*profile, selectedDevicePath());
}

void ProfilesPage::showMessage(const QString &message, bool error)
{
    m_message->setText(message);
    m_message->setStyleSheet(error ? QStringLiteral("color: #e5484d;") : QString());
    m_message->setVisible(!message.isEmpty());
}

void ProfilesPage::saveCurrent()
{
    const QString path = selectedDevicePath();
    if (path.isEmpty()) {
        showMessage(tr("No device is available to save settings from."), true);
        return;
    }

    const QStringList games = m_controller->gameNames();
    ProfileDialog dialog(tr("Save profile"), games, QString(), QString(), false, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!m_controller->captureNew(path, dialog.profileName(), dialog.game(),
                                  dialog.autoApply(), &error)) {
        showMessage(error, true);
        return;
    }
    showMessage(tr("Profile saved."));
}

void ProfilesPage::applySelected()
{
    const QString id = selectedProfileId();
    const QString path = devicePathForProfile(id);
    if (id.isEmpty() || path.isEmpty()) {
        showMessage(tr("There is no connected device this profile can be applied to."), true);
        return;
    }

    QString error;
    if (!m_controller->apply(id, path, &error)) {
        showMessage(error, true);
        return;
    }
    showMessage(tr("Profile applied."));
}

void ProfilesPage::updateSelected()
{
    const QString id = selectedProfileId();
    const QString path = devicePathForProfile(id);
    if (id.isEmpty() || path.isEmpty()) {
        showMessage(tr("There is no connected device this profile can be updated from."), true);
        return;
    }

    QString error;
    if (!m_controller->captureInto(id, path, &error)) {
        showMessage(error, true);
        return;
    }
    rebuildTable();
    showMessage(tr("Profile updated from the device."));
}

void ProfilesPage::duplicateSelected()
{
    const DeviceProfile *profile = m_store->find(selectedProfileId());
    if (!profile)
        return;

    DeviceProfile copy = *profile;
    copy.id.clear();
    copy.name = tr("%1 copy").arg(profile->name);
    m_store->add(copy);
    showMessage(tr("Profile duplicated."));
}

void ProfilesPage::editSelected()
{
    const DeviceProfile *profile = m_store->find(selectedProfileId());
    if (!profile)
        return;

    ProfileDialog dialog(tr("Edit profile"), m_controller->gameNames(), profile->name,
                         profile->game, profile->autoApply, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    DeviceProfile updated = *profile;
    updated.name = dialog.profileName();
    updated.game = dialog.game();
    updated.autoApply = dialog.autoApply();
    if (!m_store->update(updated)) {
        showMessage(tr("The profile could not be updated."), true);
        return;
    }
    showMessage(tr("Profile updated."));
}

void ProfilesPage::deleteSelected()
{
    const DeviceProfile *profile = m_store->find(selectedProfileId());
    if (!profile)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Delete profile"),
        tr("Delete “%1”? Its settings cannot be recovered.").arg(profile->name));
    if (answer != QMessageBox::Yes)
        return;

    m_store->remove(profile->id);
    showMessage(tr("Profile deleted."));
}

void ProfilesPage::importProfiles()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import profiles"), QString(), tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!m_store->importFile(path, &error)) {
        showMessage(error, true);
        return;
    }
    showMessage(tr("Profiles imported."));
}

void ProfilesPage::exportSelected()
{
    const DeviceProfile *profile = m_store->find(selectedProfileId());
    if (!profile)
        return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export profile"),
        QStringLiteral("%1.json").arg(fileNameForProfile(profile->name)),
        tr("JSON files (*.json);;All files (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!m_store->exportProfile(profile->id, path, &error)) {
        showMessage(error, true);
        return;
    }
    showMessage(tr("Profile exported."));
}

} // namespace hidcenter
