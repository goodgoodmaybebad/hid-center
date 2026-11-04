// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/InterfaceScale.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QSettings>
#include <QString>

namespace hidcenter {
namespace {

const char *const kSettingsKey = "interface/scalePercent";

/// Set by applyInterfaceScale(), when QT_SCALE_FACTOR was there before it.
bool g_overridden = false;

} // namespace

QList<int> interfaceScales()
{
    return {0, 100, 125, 150, 175, 200};
}

int savedInterfaceScale()
{
    const int percent = QSettings().value(QLatin1String(kSettingsKey), 0).toInt();
    return interfaceScales().contains(percent) ? percent : 0;
}

void saveInterfaceScale(int percent)
{
    QSettings().setValue(QLatin1String(kSettingsKey),
                         interfaceScales().contains(percent) ? percent : 0);
}

void applyInterfaceScale()
{
    // Fractional factors, 1.25 and 1.5 among them, as they are rather than
    // rounded to whole ones. Qt 6's default, said here so it stays so.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    if (!qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) {
        g_overridden = true;
        return;
    }
    const int percent = savedInterfaceScale();
    if (percent > 0)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(percent / 100.0, 'g', 4));
}

bool interfaceScaleOverridden()
{
    return g_overridden;
}

} // namespace hidcenter
