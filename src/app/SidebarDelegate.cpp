// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/SidebarDelegate.h"

#include "app/DeviceListModel.h"

#include <QFontMetrics>
#include <QIcon>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>

namespace hidcenter {
namespace {

constexpr int kRowHeight = 48;
constexpr int kSideInset = 7;
constexpr int kVerticalInset = 3;
constexpr int kIconSize = 18;
constexpr int kIconGap = 11;
constexpr qreal kRadius = 8.0;

} // namespace

SidebarDelegate::SidebarDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void SidebarDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = option.state & QStyle::State_Enabled;

    const QString devicePath = index.data(DeviceListModel::DevicePathRole).toString();
    const bool offline = DeviceListModel::isOfflinePath(devicePath);

    const QPalette &palette = option.palette;
    const QRect item = option.rect.adjusted(kSideInset, kVerticalInset, -kSideInset,
                                            -kVerticalInset);

    // A little breathing room above the first device keeps the two application
    // pages — Home and Telemetry — reading as one group and the hardware as
    // another, without adding a fake row the selection would have to ignore.
    if (index.row() == DeviceListModel::FixedRows) {
        QColor rule = palette.color(QPalette::Mid);
        rule.setAlphaF(0.45);
        painter->setPen(QPen(rule, 1.0));
        painter->drawLine(option.rect.left() + kSideInset + 4,
                          option.rect.top() + kVerticalInset,
                          option.rect.right() - kSideInset - 4,
                          option.rect.top() + kVerticalInset);
    }

    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette.color(QPalette::Highlight));
        painter->drawRoundedRect(item, kRadius, kRadius);
    } else if (hovered) {
        QColor wash = palette.color(QPalette::WindowText);
        wash.setAlphaF(0.06);
        painter->setPen(Qt::NoPen);
        painter->setBrush(wash);
        painter->drawRoundedRect(item, kRadius, kRadius);
    }

    QColor text = palette.color(QPalette::Text);
    if (selected) {
        text = palette.color(QPalette::HighlightedText);
    } else if (!enabled) {
        text = palette.color(QPalette::Disabled, QPalette::Text);
    } else if (offline) {
        // Present, but not something to act on. Kept legible rather than dimmed
        // to the disabled tone, which reads as "broken".
        text.setAlphaF(0.78);
    }

    QFont font = option.font;
    font.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter->setFont(font);

    const QRect iconRect(item.left() + 8, item.center().y() - kIconSize / 2, kIconSize,
                         kIconSize);
    const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        const QIcon::Mode mode = !enabled          ? QIcon::Disabled
                                 : selected        ? QIcon::Selected
                                                   : QIcon::Normal;
        icon.paint(painter, iconRect, Qt::AlignCenter, mode);
    }

    const int textLeft = iconRect.right() + kIconGap;
    QRect textRect(textLeft, item.top(), item.right() - textLeft - 8, item.height());

    // Offline rows get a small badge rather than a suffix on the name. The icon
    // says it too, but the badge is what survives a glance.
    QRect badgeRect;
    if (offline) {
        QFont badgeFont = option.font;
        badgeFont.setPointSizeF(qMax(6.5, badgeFont.pointSizeF() * 0.72));
        badgeFont.setWeight(QFont::DemiBold);
        const QFontMetrics badgeMetrics(badgeFont);
        const int badgeWidth = badgeMetrics.horizontalAdvance(tr("OFFLINE")) + 12;
        const int badgeHeight = qMin(18, item.height() - 8);
        badgeRect = QRect(item.right() - 8 - badgeWidth, item.center().y() - badgeHeight / 2,
                          badgeWidth, badgeHeight);
        textRect.setRight(badgeRect.left() - 8);
    }

    const QString primary = index.data(Qt::DisplayRole).toString();
    const QString secondary = index.data(DeviceListModel::SecondaryTextRole).toString();
    const bool twoLine = !secondary.isEmpty();

    QFont secondaryFont = option.font;
    if (secondaryFont.pointSizeF() > 0.0)
        secondaryFont.setPointSizeF(qMax(6.5, secondaryFont.pointSizeF() * 0.78));
    else
        secondaryFont.setPixelSize(qMax(9, int(secondaryFont.pixelSize() * 0.78)));

    QColor secondaryColour = selected ? palette.color(QPalette::HighlightedText)
                                      : palette.color(QPalette::Disabled, QPalette::Text);
    if (selected)
        secondaryColour.setAlphaF(0.78);

    const QFontMetrics primaryMetrics(font);
    const QFontMetrics secondaryMetrics(secondaryFont);

    painter->setFont(font);
    painter->setPen(text);
    if (twoLine) {
        const int totalHeight =
            primaryMetrics.height() + 2 + secondaryMetrics.height();
        const int top = item.center().y() - totalHeight / 2;
        const QRect primaryRect(textLeft, top, textRect.width(), primaryMetrics.height());
        const QRect secondaryRect(textLeft, primaryRect.bottom() + 2, textRect.width(),
                                  secondaryMetrics.height());

        painter->drawText(primaryRect, Qt::AlignVCenter | Qt::AlignLeft,
                          primaryMetrics.elidedText(primary, Qt::ElideRight, primaryRect.width()));

        painter->setFont(secondaryFont);
        painter->setPen(secondaryColour);
        painter->drawText(secondaryRect, Qt::AlignVCenter | Qt::AlignLeft,
                          secondaryMetrics.elidedText(secondary, Qt::ElideRight,
                                                      secondaryRect.width()));
    } else {
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          primaryMetrics.elidedText(primary, Qt::ElideRight, textRect.width()));
    }

    if (offline) {
        QFont badgeFont = option.font;
        badgeFont.setPointSizeF(qMax(6.5, badgeFont.pointSizeF() * 0.72));
        badgeFont.setWeight(QFont::DemiBold);
        painter->setFont(badgeFont);

        QColor badgeText = selected ? palette.color(QPalette::HighlightedText)
                                    : palette.color(QPalette::Disabled, QPalette::Text);
        QColor badgeBorder = selected ? QColor(255, 255, 255, 90) : palette.color(QPalette::Mid);
        QColor badgeFill = selected ? QColor(255, 255, 255, 35) : palette.color(QPalette::WindowText);
        if (!selected)
            badgeFill.setAlphaF(0.06);

        painter->setPen(QPen(badgeBorder, 1.0));
        painter->setBrush(badgeFill);
        painter->drawRoundedRect(badgeRect, 4.0, 4.0);
        painter->setPen(badgeText);
        painter->drawText(badgeRect, Qt::AlignCenter, tr("OFFLINE"));
    }

    painter->restore();
}

QSize SidebarDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), kRowHeight};
}

} // namespace hidcenter
