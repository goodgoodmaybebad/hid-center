// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>

namespace hidcenter {

/// How large the whole interface is drawn, in percent, as the user chose it on
/// the Home page. 0 is automatic: whatever the desktop asks for.
///
/// Qt fixes its scale when the application object is made, so a change takes
/// effect at the next start. It is applied as Qt's own scale factor, which
/// draws text at the larger size rather than stretching it, so it stays sharp.

/// The sizes offered, 0 first.
QList<int> interfaceScales();

int savedInterfaceScale();
void saveInterfaceScale(int percent);

/// Apply the saved size. Must run before the application object is made. Does
/// nothing when it is automatic, or when QT_SCALE_FACTOR is already set, which
/// is the user asking for something themselves.
void applyInterfaceScale();

/// Whether QT_SCALE_FACTOR was set before start-up, so the saved size was not
/// applied: what the Home page says about a change.
bool interfaceScaleOverridden();

} // namespace hidcenter
