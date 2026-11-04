// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/Theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QString>

namespace hidcenter {
namespace {

bool g_dark = false;

/// The handful of colours both the palette and the stylesheet need. They are
/// deliberately a small set: every value in the sheet is one of these, so the
/// controls, the chrome and the custom-painted widgets cannot drift apart.
struct Colours
{
    QColor window;
    QColor panel;
    QColor panelAlt;
    QColor input;
    QColor alternate;
    QColor button;
    QColor buttonHover;
    QColor buttonPressed;
    QColor buttonDisabled;
    QColor border;
    QColor borderStrong;
    QColor text;
    QColor mutedText;
    QColor disabledText;
    QColor accent;
    QColor accentHover;
    QColor accentText;
    QColor scrollHandle;
    QColor scrollHandleHover;

    QColor accentSoft() const
    {
        QColor colour = accent;
        colour.setAlphaF(g_dark ? 0.16 : 0.10);
        return colour;
    }

    QColor accentBorder() const
    {
        QColor colour = accent;
        colour.setAlphaF(g_dark ? 0.55 : 0.40);
        return colour;
    }

    QColor hover() const
    {
        QColor colour = text;
        colour.setAlphaF(g_dark ? 0.06 : 0.05);
        return colour;
    }
};

Colours darkColours()
{
    Colours c;
    c.window = QColor(0x12, 0x14, 0x17);
    c.panel = QColor(0x19, 0x1c, 0x21);
    c.panelAlt = QColor(0x1e, 0x22, 0x28);
    c.input = QColor(0x0f, 0x11, 0x15);
    c.alternate = QColor(0x15, 0x18, 0x1c);
    c.button = QColor(0x23, 0x28, 0x30);
    c.buttonHover = QColor(0x2c, 0x32, 0x3c);
    c.buttonPressed = QColor(0x1d, 0x22, 0x29);
    c.buttonDisabled = QColor(0x1a, 0x1d, 0x22);
    c.border = QColor(0x2b, 0x31, 0x3a);
    c.borderStrong = QColor(0x3b, 0x44, 0x50);
    c.text = QColor(0xe7, 0xea, 0xef);
    c.mutedText = QColor(0x96, 0xa0, 0xaf);
    c.disabledText = QColor(0x5f, 0x68, 0x75);
    c.accent = QColor(0x5b, 0x8d, 0xef);
    c.accentHover = QColor(0x7d, 0xa8, 0xff);
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.scrollHandle = QColor(0x39, 0x41, 0x4c);
    c.scrollHandleHover = QColor(0x4a, 0x55, 0x64);
    return c;
}

Colours lightColours()
{
    Colours c;
    c.window = QColor(0xf2, 0xf4, 0xf8);
    c.panel = QColor(0xff, 0xff, 0xff);
    c.panelAlt = QColor(0xf6, 0xf8, 0xfb);
    c.input = QColor(0xff, 0xff, 0xff);
    c.alternate = QColor(0xf7, 0xf9, 0xfc);
    c.button = QColor(0xff, 0xff, 0xff);
    c.buttonHover = QColor(0xf1, 0xf4, 0xf9);
    c.buttonPressed = QColor(0xe8, 0xed, 0xf4);
    c.buttonDisabled = QColor(0xf3, 0xf5, 0xf8);
    c.border = QColor(0xd7, 0xdd, 0xe6);
    c.borderStrong = QColor(0xba, 0xc4, 0xd1);
    c.text = QColor(0x1a, 0x1f, 0x27);
    c.mutedText = QColor(0x64, 0x70, 0x82);
    c.disabledText = QColor(0x9a, 0xa4, 0xb2);
    c.accent = QColor(0x25, 0x63, 0xeb);
    c.accentHover = QColor(0x1d, 0x4e, 0xd8);
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.scrollHandle = QColor(0xc9, 0xd1, 0xdd);
    c.scrollHandleHover = QColor(0xac, 0xb8, 0xc8);
    return c;
}

QPalette paletteFor(const Colours &c)
{
    QPalette palette;

    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.input);
    palette.setColor(QPalette::AlternateBase, c.alternate);
    palette.setColor(QPalette::ToolTipBase, c.panelAlt);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.button);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.accentText);
    palette.setColor(QPalette::Light, c.panelAlt);
    palette.setColor(QPalette::Midlight, c.border);
    palette.setColor(QPalette::Mid, c.borderStrong);
    palette.setColor(QPalette::Dark, c.border);
    palette.setColor(QPalette::Shadow, c.window.darker(160));
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.accentText);
    palette.setColor(QPalette::Link, c.accentHover);
    palette.setColor(QPalette::LinkVisited, c.accent);
    palette.setColor(QPalette::PlaceholderText, c.mutedText);

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // Read by the pedal bars and curve editors. Kept in step with Highlight,
    // which is what the standard widgets use for the same job.
    palette.setColor(QPalette::Accent, c.accent);
#endif

    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Base, c.window);
    palette.setColor(QPalette::Disabled, QPalette::Button, c.buttonDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, c.borderStrong);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, c.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, c.disabledText);

    return palette;
}

QString colourName(const QColor &colour)
{
    return colour.name(QColor::HexRgb);
}

QString colourWithAlpha(const QColor &colour, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(colour.red())
        .arg(colour.green())
        .arg(colour.blue())
        .arg(alpha);
}

QString styleSheetFor(const Colours &c)
{
    // A QSS template rather than a pile of .arg() calls: the sheet is easier to
    // read and reorder this way, and the ${name} delimiters cannot collide with
    // one another the way bare names would.
    QString css = QStringLiteral(R"QSS(
QMainWindow, QDialog {
    background-color: ${window};
}

QMainWindow::separator {
    background-color: ${border};
    width: 1px;
    height: 1px;
}

QSplitter::handle {
    background-color: transparent;
}
QSplitter::handle:horizontal {
    width: 2px;
}
QSplitter::handle:vertical {
    height: 2px;
}
QSplitter::handle:hover {
    background-color: ${accent};
}

QStatusBar {
    background-color: ${panel};
    color: ${mutedText};
    border-top: 1px solid ${border};
}
QStatusBar::item {
    border: none;
}
QStatusBar QLabel {
    color: ${mutedText};
    padding: 0 4px;
}

QToolTip {
    background-color: ${panelAlt};
    color: ${text};
    border: 1px solid ${borderStrong};
    border-radius: 6px;
    padding: 5px 8px;
}

QMenu {
    background-color: ${panel};
    border: 1px solid ${border};
    border-radius: 8px;
    padding: 5px;
}
QMenu::item {
    padding: 6px 26px 6px 12px;
    border-radius: 5px;
}
QMenu::item:selected {
    background-color: ${accentSoft};
    color: ${text};
}
QMenu::separator {
    height: 1px;
    background-color: ${border};
    margin: 4px 8px;
}

/* --- Sidebar ---------------------------------------------------------- */

QWidget#sidebarPanel {
    background-color: ${panel};
    border-right: 1px solid ${border};
}
QLabel#sidebarBrand {
    color: ${text};
    font-size: 15px;
    font-weight: 700;
}
QLabel#sidebarTagline {
    color: ${mutedText};
    font-size: 9px;
}
QListView#sidebar {
    background: transparent;
    border: none;
    border-radius: 0;
    outline: none;
}
QListView#sidebar::item {
    background: transparent;
    border: none;
    padding: 0;
}

/* --- Dashboard -------------------------------------------------------- */

QFrame#dashboardCard {
    background-color: ${panelAlt};
    border: 1px solid ${border};
    border-radius: 10px;
}
QLabel#dashboardSection {
    color: ${text};
    font-size: 13px;
    font-weight: 700;
}
QLabel#dashboardCardTitle {
    color: ${text};
    font-weight: 600;
}
QLabel#dashboardValue {
    color: ${text};
    font-size: 13px;
    font-weight: 600;
}
QLabel#dashboardMuted {
    color: ${mutedText};
}
QLabel#dashboardProblem {
    color: #e5484d;
}
QLabel#dashboardCode {
    color: ${text};
    background-color: ${input};
    border: 1px solid ${border};
    border-radius: 6px;
    padding: 8px;
    font-family: monospace;
}
QLabel#dashboardBadge {
    font-size: 8px;
    font-weight: 700;
    padding: 3px 8px;
    border: 1px solid ${border};
    border-radius: 8px;
    color: ${mutedText};
    background-color: ${hover};
}
QLabel#dashboardBadge[state="ready"] {
    color: ${accent};
    border-color: ${accentBorder};
    background-color: ${accentSoft};
}
QLabel#dashboardBadge[state="offline"] {
    color: ${mutedText};
    border-color: ${border};
    background-color: ${hover};
}
QLabel#dashboardBadge[state="error"] {
    color: #e5484d;
    border-color: rgba(229, 72, 77, 0.55);
    background-color: rgba(229, 72, 77, 0.14);
}
QPushButton#dashboardAction {
    padding: 4px 11px;
    min-height: 16px;
}

/* --- Tabs ------------------------------------------------------------- */

QTabWidget::pane {
    background-color: ${panel};
    border: 1px solid ${border};
    border-radius: 10px;
    top: -1px;
}
QTabBar {
    background: transparent;
}
QTabBar::tab {
    background-color: transparent;
    color: ${mutedText};
    border: 1px solid transparent;
    border-radius: 7px;
    padding: 7px 14px;
    margin: 0 2px 6px 0;
    min-width: 58px;
}
QTabBar::tab:hover {
    background-color: ${hover};
    color: ${text};
}
QTabBar::tab:selected {
    background-color: ${accentSoft};
    border-color: ${accentBorder};
    color: ${accent};
}
QTabBar::tab:disabled {
    color: ${disabledText};
}

/* --- Cards / groups --------------------------------------------------- */

QGroupBox {
    background-color: transparent;
    border: 1px solid ${border};
    border-radius: 10px;
    margin-top: 12px;
    padding: 12px 10px 10px 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px;
    padding: 0 5px;
    color: ${text};
    font-weight: 600;
}
QGroupBox:disabled {
    border-color: ${border};
}
QGroupBox::title:disabled {
    color: ${disabledText};
}

/* --- Buttons ---------------------------------------------------------- */

QPushButton {
    background-color: ${button};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 7px;
    padding: 5px 13px;
    min-height: 18px;
    font-weight: 500;
}
QPushButton:hover {
    background-color: ${buttonHover};
    border-color: ${borderStrong};
}
QPushButton:pressed {
    background-color: ${buttonPressed};
}
QPushButton:focus {
    border-color: ${accent};
}
QPushButton:checked {
    background-color: ${accentSoft};
    border-color: ${accentBorder};
    color: ${accent};
}
QPushButton:disabled {
    background-color: ${buttonDisabled};
    color: ${disabledText};
    border-color: ${border};
}
QPushButton:default {
    border-color: ${accent};
}

/* QToolButton is deliberately left alone: the colour swatches and the RPM
   light grid pack themselves into pixel-tight rows and do their own drawing. */

QCheckBox, QRadioButton {
    spacing: 7px;
}

/* --- Inputs ----------------------------------------------------------- */

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit,
QDateTimeEdit {
    background-color: ${input};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 6px;
    padding: 4px 8px;
    selection-background-color: ${accent};
    selection-color: ${accentText};
}
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover,
QPlainTextEdit:hover, QTextEdit:hover, QDateTimeEdit:hover {
    border-color: ${borderStrong};
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus,
QPlainTextEdit:focus, QTextEdit:focus, QDateTimeEdit:focus {
    border-color: ${accent};
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
QPlainTextEdit:disabled, QTextEdit:disabled, QDateTimeEdit:disabled {
    background-color: ${buttonDisabled};
    color: ${disabledText};
    border-color: ${border};
}
QComboBox QAbstractItemView {
    background-color: ${panel};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 6px;
    padding: 4px;
    outline: none;
    selection-background-color: ${accent};
    selection-color: ${accentText};
}
QComboBox QAbstractItemView::item {
    min-height: 24px;
    padding: 2px 6px;
    border-radius: 4px;
}
QComboBox {
    padding-right: 28px;
}
QComboBox::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 26px;
    border: none;
    background: transparent;
}
QComboBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 11px;
    height: 11px;
}
QSpinBox, QDoubleSpinBox {
    padding-right: 24px;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 19px;
    border: none;
    border-left: 1px solid ${border};
    border-top-right-radius: 6px;
    background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 19px;
    border: none;
    border-left: 1px solid ${border};
    border-bottom-right-radius: 6px;
    background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: ${buttonHover};
}
QSpinBox::up-button:pressed, QDoubleSpinBox::up-button:pressed,
QSpinBox::down-button:pressed, QDoubleSpinBox::down-button:pressed {
    background-color: ${buttonPressed};
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url(:/icons/chevron-up.svg);
    width: 9px;
    height: 9px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 9px;
    height: 9px;
}

/* --- Sliders ---------------------------------------------------------- */

QSlider::groove:horizontal {
    height: 6px;
    background-color: ${input};
    border: 1px solid ${border};
    border-radius: 3px;
}
QSlider::sub-page:horizontal {
    background-color: ${accent};
    border: 1px solid ${accent};
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background-color: ${panel};
    border: 2px solid ${accent};
    width: 12px;
    height: 12px;
    margin: -5px 0;
    border-radius: 8px;
}
QSlider::handle:horizontal:hover {
    border-color: ${accentHover};
    background-color: ${accentSoft};
}
QSlider::handle:horizontal:pressed {
    background-color: ${accent};
}
QSlider::sub-page:horizontal:disabled {
    background-color: ${borderStrong};
    border-color: ${borderStrong};
}
QSlider::handle:horizontal:disabled {
    border-color: ${borderStrong};
    background-color: ${buttonDisabled};
}

/* --- Progress --------------------------------------------------------- */

QProgressBar {
    background-color: ${input};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 6px;
    text-align: center;
}
QProgressBar::chunk {
    background-color: ${accent};
    border-radius: 5px;
}

/* --- Item views ------------------------------------------------------- */

QTableView, QTreeView, QListView {
    background-color: ${input};
    alternate-background-color: ${alternate};
    color: ${text};
    border: 1px solid ${border};
    border-radius: 8px;
    gridline-color: ${border};
    selection-background-color: ${accent};
    selection-color: ${accentText};
    outline: none;
}
QHeaderView {
    background-color: transparent;
}
QHeaderView::section {
    background-color: ${panelAlt};
    color: ${mutedText};
    border: none;
    border-right: 1px solid ${border};
    border-bottom: 1px solid ${border};
    padding: 5px 8px;
    font-weight: 600;
}
QTableCornerButton::section {
    background-color: ${panelAlt};
    border: none;
    border-right: 1px solid ${border};
    border-bottom: 1px solid ${border};
}

/* --- Scroll bars ------------------------------------------------------ */

QScrollBar:vertical {
    background: transparent;
    width: 12px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background-color: ${scrollHandle};
    min-height: 30px;
    border-radius: 4px;
}
QScrollBar::handle:vertical:hover {
    background-color: ${scrollHandleHover};
}
QScrollBar:horizontal {
    background: transparent;
    height: 12px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background-color: ${scrollHandle};
    min-width: 30px;
    border-radius: 4px;
}
QScrollBar::handle:horizontal:hover {
    background-color: ${scrollHandleHover};
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
    border: none;
    background: transparent;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
}
QAbstractScrollArea::corner {
    background: transparent;
    border: none;
}
)QSS");

    const QHash<QString, QString> values{
        {QStringLiteral("${window}"), colourName(c.window)},
        {QStringLiteral("${panel}"), colourName(c.panel)},
        {QStringLiteral("${panelAlt}"), colourName(c.panelAlt)},
        {QStringLiteral("${input}"), colourName(c.input)},
        {QStringLiteral("${alternate}"), colourName(c.alternate)},
        {QStringLiteral("${button}"), colourName(c.button)},
        {QStringLiteral("${buttonHover}"), colourName(c.buttonHover)},
        {QStringLiteral("${buttonPressed}"), colourName(c.buttonPressed)},
        {QStringLiteral("${buttonDisabled}"), colourName(c.buttonDisabled)},
        {QStringLiteral("${border}"), colourName(c.border)},
        {QStringLiteral("${borderStrong}"), colourName(c.borderStrong)},
        {QStringLiteral("${text}"), colourName(c.text)},
        {QStringLiteral("${mutedText}"), colourName(c.mutedText)},
        {QStringLiteral("${disabledText}"), colourName(c.disabledText)},
        {QStringLiteral("${accent}"), colourName(c.accent)},
        {QStringLiteral("${accentHover}"), colourName(c.accentHover)},
        {QStringLiteral("${accentText}"), colourName(c.accentText)},
        {QStringLiteral("${accentSoft}"), colourWithAlpha(c.accent, g_dark ? 41 : 26)},
        {QStringLiteral("${accentBorder}"), colourWithAlpha(c.accent, g_dark ? 140 : 102)},
        {QStringLiteral("${hover}"), colourWithAlpha(c.text, g_dark ? 15 : 13)},
        {QStringLiteral("${scrollHandle}"), colourName(c.scrollHandle)},
        {QStringLiteral("${scrollHandleHover}"), colourName(c.scrollHandleHover)},
    };

    for (auto it = values.cbegin(); it != values.cend(); ++it)
        css.replace(it.key(), it.value());

    return css;
}

} // namespace

void applyTheme(QApplication &app)
{
    const QPalette systemPalette = app.palette();

    bool dark = systemPalette.color(QPalette::Window).lightness() < 128;
    // An explicit override is useful for screenshots and for desktops where the
    // toolkit cannot tell the application which scheme it is running under.
    QString requested = qEnvironmentVariable("HIDCENTER_THEME").trimmed().toLower();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (requested.isEmpty()) {
        switch (QGuiApplication::styleHints()->colorScheme()) {
        case Qt::ColorScheme::Dark:
            dark = true;
            break;
        case Qt::ColorScheme::Light:
            dark = false;
            break;
        case Qt::ColorScheme::Unknown:
            break;
        }
    }
#endif
    if (requested == QLatin1String("dark"))
        dark = true;
    else if (requested == QLatin1String("light"))
        dark = false;

    g_dark = dark;

    // Fusion is Qt's own style and behaves the same on every desktop. It also
    // gives the stylesheet a flat base to paint over, rather than leaving each
    // platform theme's own rounded gradients underneath it.
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        app.setStyle(fusion);

    const Colours colours = dark ? darkColours() : lightColours();
    app.setPalette(paletteFor(colours));
    app.setStyleSheet(styleSheetFor(colours));
}

bool themeIsDark()
{
    return g_dark;
}

} // namespace hidcenter
