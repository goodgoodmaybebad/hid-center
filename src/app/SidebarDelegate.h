// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

namespace hidcenter {

/// Paints the sidebar's Home / Telemetry / device rows.
///
/// The stock delegate draws a full-width highlight that reads like a table row;
/// this draws a rounded card with room to breathe, which is what makes the
/// window look like it belongs beside current desktop applications. Selection,
/// icons and tooltips still come from the model, so the model stays untouched.
class SidebarDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SidebarDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace hidcenter
