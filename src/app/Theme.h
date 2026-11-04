// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QApplication;

namespace hidcenter {

/// Install the application's visual language: a flat, rounded, palette-driven
/// theme and the stylesheet that goes with it.
///
/// The palette is what the custom-painted widgets — pedal curves, trace graphs,
/// LED rings — already read from. Styling standard controls through the
/// stylesheet and leaving those widgets on the palette means the two agree in
/// both a light and a dark desktop without either having to know about the
/// other.
///
/// Call once, after QApplication exists and before any window is built.
void applyTheme(QApplication &app);

/// Whether the applied theme is the dark variant. Useful to callers that draw
/// something themselves before a palette is available.
bool themeIsDark();

} // namespace hidcenter
