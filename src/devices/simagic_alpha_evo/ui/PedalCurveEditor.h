// SPDX-FileCopyrightText: 2026 Raul Segui
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include <array>

namespace hidcenter::simagic_alpha_evo {

/// One pedal's response, drawn and edited as SimPro 3 draws it: the pedal's
/// position now, as a segmented bar; the curve, with three dots that drag up and
/// down and a value pill beside each; and below it a range slider with a handle
/// at each end.
///
/// The curve runs from the range's start at 0 % to its end at 100 %, through the
/// three points, which sit a quarter, half and three quarters of the way between
/// the ends. Only the points' heights are sent to the pedals; where along the
/// travel they sit is how SimPro draws them, not something the pedals were seen
/// to say.
///
/// **Only a display and an editor**, like ClutchPaddlesView: what it shows is the
/// page's to set, and an edit is reported once the drag ends, through edited().
/// The points are held to no lower than the one before and no higher than the
/// one after, and the start below the end.
class PedalCurveEditor : public QWidget
{
    Q_OBJECT

public:
    explicit PedalCurveEditor(QWidget *parent = nullptr);

    /// The range's ends, in percent of the pedal's travel.
    int rangeStart() const { return m_start; }
    int rangeEnd() const { return m_end; }
    void setRange(int startPercent, int endPercent);

    /// The three points, as the output each gives, in percent.
    const std::array<int, 3> &points() const { return m_points; }
    void setPoints(const std::array<int, 3> &percents);

    /// Where the pedal's output is now, 0 to 1, drawn as the position bar and a
    /// line across the graph; below 0 for none.
    void setOutput(qreal output);

    /// Whether the handles and dots can be dragged. Drawn either way.
    void setEditable(bool editable);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// A drag ended with something changed. The new values are rangeStart(),
    /// rangeEnd() and points().
    void edited();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    /// What a drag holds.
    enum class Grab {
        None,
        Start,
        End,
        Point0,
        Point1,
        Point2,
    };

    /// Top to bottom: the live position, the graph, and the range slider.
    QRectF positionRect() const;
    QRectF graphRect() const;
    QRectF sliderRect() const;

    /// From percent of travel and of output to a place on the widget, and back.
    qreal xOf(qreal travelPercent, const QRectF &area) const;
    qreal yOf(qreal outputPercent) const;
    int travelAt(qreal x, const QRectF &area) const;
    int outputAt(qreal y) const;

    /// Where each point is drawn: a quarter, half and three quarters of the way
    /// from the start to the end.
    QPointF pointAt(int index) const;

    Grab grabAt(const QPointF &position) const;

    int m_start = 0;
    int m_end = 100;
    std::array<int, 3> m_points{25, 50, 75};
    qreal m_output = -1.0;
    bool m_editable = true;

    Grab m_grab = Grab::None;
    bool m_changed = false;
};

} // namespace hidcenter::simagic_alpha_evo
