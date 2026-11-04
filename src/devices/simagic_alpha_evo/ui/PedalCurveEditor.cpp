// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PedalCurveEditor.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace hidcenter::simagic_alpha_evo {
namespace {

/// Pixels.
constexpr int kMargin = 10;
constexpr int kPositionHeight = 14;
constexpr int kSliderHeight = 24;
constexpr int kGap = 8;
constexpr qreal kHandleRadius = 7.0;
constexpr qreal kDotRadius = 6.0;

/// The position bar's segments, as SimPro draws it.
constexpr int kPositionSegments = 40;

/// Room at each end of the slider for its "0 %" and "100 %".
constexpr int kSliderLabelWidth = 40;

/// Clearance between a slider handle at either end and the number beside it.
/// Half of this is the handle's own radius; the rest is breathing room so a
/// handle sitting at 0 % or 100 % never touches the label.
constexpr int kSliderLabelGap = 10;

/// How far from a handle or a dot a press still takes it, in pixels.
constexpr qreal kGrabDistance = 12.0;

} // namespace

PedalCurveEditor::PedalCurveEditor(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PedalCurveEditor::setRange(int startPercent, int endPercent)
{
    const int start = qBound(0, startPercent, 99);
    const int end = qBound(start + 1, endPercent, 100);
    if (start == m_start && end == m_end)
        return;
    m_start = start;
    m_end = end;
    update();
}

void PedalCurveEditor::setPoints(const std::array<int, 3> &percents)
{
    if (percents == m_points)
        return;
    m_points = percents;
    update();
}

void PedalCurveEditor::setOutput(qreal output)
{
    if (qFuzzyCompare(output + 2.0, m_output + 2.0))
        return;
    m_output = output;
    update();
}

void PedalCurveEditor::setEditable(bool editable)
{
    if (editable == m_editable)
        return;
    m_editable = editable;
    m_grab = Grab::None;
    unsetCursor();
    update();
}

QSize PedalCurveEditor::sizeHint() const
{
    return {260, 300};
}

QSize PedalCurveEditor::minimumSizeHint() const
{
    return {180, 200};
}

// --- Geometry ---------------------------------------------------------------------

QRectF PedalCurveEditor::positionRect() const
{
    return QRectF(kMargin, kMargin, width() - 2 * kMargin, kPositionHeight);
}

QRectF PedalCurveEditor::sliderRect() const
{
    // Between its two labels, with a handle's width and a gap at each end so a
    // handle at the extreme of the range clears the number rather than sitting
    // on top of it.
    const qreal inset = kSliderLabelWidth + kHandleRadius + kSliderLabelGap;
    return QRectF(kMargin + inset, height() - kMargin - kSliderHeight,
                  width() - 2 * (kMargin + inset), kSliderHeight);
}

QRectF PedalCurveEditor::graphRect() const
{
    const qreal top = kMargin + kPositionHeight + kGap;
    const qreal bottom = height() - kMargin - kSliderHeight - kGap;
    return QRectF(kMargin, top, width() - 2 * kMargin, bottom - top);
}

qreal PedalCurveEditor::xOf(qreal travelPercent, const QRectF &area) const
{
    return area.left() + area.width() * travelPercent / 100.0;
}

qreal PedalCurveEditor::yOf(qreal outputPercent) const
{
    const QRectF graph = graphRect();
    return graph.bottom() - graph.height() * outputPercent / 100.0;
}

int PedalCurveEditor::travelAt(qreal x, const QRectF &area) const
{
    if (area.width() <= 0)
        return 0;
    return int(std::lround(qBound(0.0, (x - area.left()) * 100.0 / area.width(), 100.0)));
}

int PedalCurveEditor::outputAt(qreal y) const
{
    const QRectF graph = graphRect();
    if (graph.height() <= 0)
        return 0;
    return int(std::lround(qBound(0.0, (graph.bottom() - y) * 100.0 / graph.height(), 100.0)));
}

QPointF PedalCurveEditor::pointAt(int index) const
{
    const qreal travel = m_start + (m_end - m_start) * (index + 1) / 4.0;
    return {xOf(travel, graphRect()), yOf(m_points[std::size_t(index)])};
}

PedalCurveEditor::Grab PedalCurveEditor::grabAt(const QPointF &position) const
{
    // The dots first: they are what is most often wanted, and the slider's
    // handles are well clear of them.
    Grab best = Grab::None;
    qreal bestDistance = kGrabDistance;
    for (int i = 0; i < 3; ++i) {
        const qreal distance = QLineF(position, pointAt(i)).length();
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = Grab(int(Grab::Point0) + i);
        }
    }
    if (best != Grab::None)
        return best;

    const QRectF slider = sliderRect();
    const qreal y = slider.center().y();
    const qreal toStart = QLineF(position, QPointF(xOf(m_start, slider), y)).length();
    const qreal toEnd = QLineF(position, QPointF(xOf(m_end, slider), y)).length();
    if (qMin(toStart, toEnd) > kGrabDistance)
        return Grab::None;
    return toStart <= toEnd ? Grab::Start : Grab::End;
}

// --- Drawing ----------------------------------------------------------------------

void PedalCurveEditor::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPalette &colours = palette();
    const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
    const QColor accent = colours.color(group, QPalette::Highlight);
    const QColor mid = colours.color(group, QPalette::Mid);
    const QColor base = colours.color(group, QPalette::Base);
    const QColor text = colours.color(group, QPalette::Text);

    // The position: segments lit up to where the pedal is, and the percentage
    // at the end, as SimPro shows it.
    const QRectF position = positionRect();
    const QFontMetricsF metrics(font());
    const qreal percentWidth = metrics.horizontalAdvance(QStringLiteral("100 %")) + 6;
    const QRectF segments = position.adjusted(0, 0, -percentWidth, 0);
    const qreal output = m_output >= 0.0 ? qBound(0.0, m_output, 1.0) : 0.0;
    const int lit = int(std::lround(output * kPositionSegments));
    const qreal pitch = segments.width() / kPositionSegments;
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < kPositionSegments; ++i) {
        painter.setBrush(i < lit ? accent : mid);
        painter.drawRect(QRectF(segments.left() + i * pitch, segments.top(),
                                qMax(1.0, pitch - 2.0), segments.height()));
    }
    painter.setPen(text);
    painter.drawText(QRectF(segments.right(), position.top(), percentWidth, position.height()),
                     Qt::AlignRight | Qt::AlignVCenter,
                     m_output >= 0.0 ? tr("%1 %").arg(std::lround(output * 100.0))
                                     : QStringLiteral("–"));

    // The slider: "0 %" and "100 %" at its ends, a track, the range between the
    // handles in the accent, and a handle at each end.
    const QRectF slider = sliderRect();
    painter.setPen(text);
    painter.drawText(QRectF(kMargin, slider.top(), kSliderLabelWidth, slider.height()),
                     Qt::AlignRight | Qt::AlignVCenter, tr("0 %"));
    painter.drawText(QRectF(slider.right() + kHandleRadius + kSliderLabelGap, slider.top(),
                            kSliderLabelWidth, slider.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("100 %"));
    const qreal trackY = slider.center().y();
    painter.setPen(QPen(mid, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(slider.left(), trackY), QPointF(slider.right(), trackY));
    painter.setPen(QPen(accent, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(xOf(m_start, slider), trackY), QPointF(xOf(m_end, slider), trackY));
    painter.setPen(QPen(accent, 1.5));
    painter.setBrush(base);
    for (const int end : {m_start, m_end})
        painter.drawEllipse(QPointF(xOf(end, slider), trackY), kHandleRadius, kHandleRadius);

    // The graph: a frame, and a dashed grid of quarters across and eighths up,
    // as SimPro's.
    const QRectF graph = graphRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(base);
    painter.drawRect(graph);
    painter.setPen(QPen(mid, 1.0, Qt::DashLine));
    for (int quarter = 1; quarter < 4; ++quarter) {
        const qreal x = graph.left() + graph.width() * quarter / 4.0;
        painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    }
    for (int eighth = 1; eighth < 8; ++eighth) {
        const qreal y = graph.top() + graph.height() * eighth / 8.0;
        painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
    }
    painter.setPen(QPen(mid, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(graph);

    // The curve: flat at 0 % up to the start, through the points to 100 % at the
    // end, and flat at 100 % after it.
    QPainterPath curve;
    curve.moveTo(graph.left(), yOf(0));
    curve.lineTo(xOf(m_start, graph), yOf(0));
    for (int i = 0; i < 3; ++i)
        curve.lineTo(pointAt(i));
    curve.lineTo(xOf(m_end, graph), yOf(100));
    curve.lineTo(graph.right(), yOf(100));

    painter.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(curve);

    // Where the pedal's output is now.
    if (m_output >= 0.0) {
        const qreal y = yOf(qBound(0.0, m_output, 1.0) * 100.0);
        painter.setPen(QPen(text, 1.0, Qt::DashLine));
        painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
    }

    painter.setPen(QPen(base, 1.5));
    painter.setBrush(accent);
    for (int i = 0; i < 3; ++i)
        painter.drawEllipse(pointAt(i), kDotRadius, kDotRadius);

    // The value of each point, always visible in a small pill beside its dot.
    // It replaces the plain label that used to appear only during a drag: the
    // numbers matter while reading the curve, not only while changing it.
    QFont pillFont = font();
    if (pillFont.pointSizeF() > 0.0)
        pillFont.setPointSizeF(qMax(6.5, pillFont.pointSizeF() * 0.8));
    else
        pillFont.setPixelSize(qMax(9, int(pillFont.pixelSize() * 0.8)));
    pillFont.setWeight(QFont::DemiBold);
    const QFontMetricsF pillMetrics(pillFont);

    painter.setFont(pillFont);
    for (int i = 0; i < 3; ++i) {
        const QPointF point = pointAt(i);
        const QString label = tr("%1 %").arg(m_points[std::size_t(i)]);
        const qreal pillWidth = pillMetrics.horizontalAdvance(label) + 12.0;
        const qreal pillHeight = pillMetrics.height() + 6.0;
        const qreal gap = kDotRadius + 8.0;

        qreal top = point.y() - gap - pillHeight;
        if (top < graph.top() + 2.0)
            top = point.y() + gap;
        top = qBound(graph.top() + 2.0, top, graph.bottom() - pillHeight - 2.0);

        qreal left = point.x() - pillWidth / 2.0;
        left = qBound(graph.left() + 2.0, left, graph.right() - pillWidth - 2.0);
        const QRectF pill(left, top, pillWidth, pillHeight);

        const bool active =
            m_grab == Grab(int(Grab::Point0) + i);
        QColor fill = accent;
        fill.setAlphaF(active ? 0.32 : 0.16);
        QColor border = accent;
        border.setAlphaF(active ? 0.8 : 0.5);

        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);

        painter.setPen(accent);
        painter.drawText(pill, Qt::AlignCenter, label);
    }

    // The range handles still need their value while being dragged; the pills
    // above are for the curve's points, which are always visible.
    if (m_grab == Grab::Start || m_grab == Grab::End) {
        const int value = m_grab == Grab::Start ? m_start : m_end;
        const QString label = tr("%1 %").arg(value);
        const qreal pillWidth = pillMetrics.horizontalAdvance(label) + 12.0;
        const qreal pillHeight = pillMetrics.height() + 6.0;
        const qreal centre = xOf(value, slider);
        const qreal left = qBound(qreal(kMargin), centre - pillWidth / 2.0,
                                  width() - kMargin - pillWidth);
        const qreal top = qBound(qreal(kMargin), slider.top() - pillHeight - 2.0,
                                 height() - kMargin - pillHeight);
        const QRectF pill(left, top, pillWidth, pillHeight);

        QColor fill = accent;
        fill.setAlphaF(0.32);
        QColor border = accent;
        border.setAlphaF(0.8);

        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(pill, pillHeight / 2.0, pillHeight / 2.0);
        painter.setPen(accent);
        painter.drawText(pill, Qt::AlignCenter, label);
    }
}

// --- Dragging ---------------------------------------------------------------------

void PedalCurveEditor::mousePressEvent(QMouseEvent *event)
{
    if (!m_editable || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_grab = grabAt(event->position());
    m_changed = false;
    if (m_grab != Grab::None) {
        setCursor(m_grab == Grab::Start || m_grab == Grab::End ? Qt::SizeHorCursor
                                                                : Qt::SizeVerCursor);
        update();
    }
}

void PedalCurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF position = event->position();

    if (m_grab == Grab::None) {
        // Only a hint of what a press would take.
        const Grab hover = m_editable ? grabAt(position) : Grab::None;
        if (hover == Grab::None)
            unsetCursor();
        else if (hover == Grab::Start || hover == Grab::End)
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::SizeVerCursor);
        return;
    }

    switch (m_grab) {
    case Grab::Start: {
        const int start = qBound(0, travelAt(position.x(), sliderRect()), m_end - 1);
        if (start != m_start) {
            m_start = start;
            m_changed = true;
        }
        break;
    }
    case Grab::End: {
        const int end = qBound(m_start + 1, travelAt(position.x(), sliderRect()), 100);
        if (end != m_end) {
            m_end = end;
            m_changed = true;
        }
        break;
    }
    case Grab::Point0:
    case Grab::Point1:
    case Grab::Point2: {
        const int index = int(m_grab) - int(Grab::Point0);
        const int below = index > 0 ? m_points[std::size_t(index - 1)] : 0;
        const int above = index < 2 ? m_points[std::size_t(index + 1)] : 100;
        const int output = qBound(below, outputAt(position.y()), above);
        if (output != m_points[std::size_t(index)]) {
            m_points[std::size_t(index)] = output;
            m_changed = true;
        }
        break;
    }
    case Grab::None:
        break;
    }
    update();
}

void PedalCurveEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_grab == Grab::None || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_grab = Grab::None;
    unsetCursor();
    update();
    if (m_changed) {
        m_changed = false;
        Q_EMIT edited();
    }
}

void PedalCurveEditor::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange)
        update();
    QWidget::changeEvent(event);
}

} // namespace hidcenter::simagic_alpha_evo
