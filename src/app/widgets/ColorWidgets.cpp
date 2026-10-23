#include "app/widgets/ColorWidgets.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>

#include <cmath>

void paintCheckerboard(QPainter& p, const QRectF& r, int cell)
{
    p.save();
    p.setClipRect(r);
    p.fillRect(r, Qt::white);
    for (int y = 0; y * cell < r.height(); ++y)
        for (int x = (y % 2); x * cell < r.width(); x += 2)
            p.fillRect(QRectF(r.left() + x * cell, r.top() + y * cell, cell, cell), QColor(204, 204, 204));
    p.restore();
}

namespace {

qreal hueOf(const QColor& c)
{
    const qreal h = c.hsvHueF();
    return h < 0 ? 0 : h;
}

} // namespace

// ---------------------------------------------------------------------------
// Rueda de color
// ---------------------------------------------------------------------------

ColorWheel::ColorWheel(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(90, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

QRectF ColorWheel::wheelRect() const
{
    const qreal side = std::min(width(), height()) - 6;
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

void ColorWheel::resizeEvent(QResizeEvent*)
{
    const qreal dpr = devicePixelRatioF();
    const QRectF r = wheelRect();
    const int side = std::max(1, int(r.width() * dpr));
    m_wheel = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
    m_wheel.setDevicePixelRatio(dpr);
    const qreal c = side / 2.0;
    for (int y = 0; y < side; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(m_wheel.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const qreal dx = x + 0.5 - c, dy = y + 0.5 - c;
            const qreal d = std::sqrt(dx * dx + dy * dy);
            const qreal alpha = std::clamp(c - d, 0.0, 1.0);
            if (alpha <= 0) {
                row[x] = 0;
                continue;
            }
            qreal hue = std::atan2(-dy, dx) / (2 * M_PI);
            if (hue < 0)
                hue += 1;
            const QColor col = QColor::fromHsvF(float(hue), float(std::min(1.0, d / c)), 1.0f, float(alpha));
            row[x] = qPremultiply(col.rgba());
        }
    }
}

void ColorWheel::setColor(const QColor& c)
{
    if (c == m_color)
        return;
    m_color = c;
    update();
}

void ColorWheel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = wheelRect();
    p.drawImage(r.topLeft(), m_wheel);

    const qreal radius = r.width() / 2.0;
    const qreal angle = hueOf(m_color) * 2 * M_PI;
    const qreal dist = m_color.hsvSaturationF() * radius;
    const QPointF marker = r.center() + QPointF(std::cos(angle) * dist, -std::sin(angle) * dist);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 1.5));
    p.drawEllipse(marker, 5, 5);
    p.setPen(QPen(Qt::white, 1.5));
    p.drawEllipse(marker, 3.5, 3.5);
}

void ColorWheel::pickAt(const QPointF& pos, Qt::KeyboardModifiers mods)
{
    const QRectF r = wheelRect();
    const QPointF d = pos - r.center();
    const qreal radius = r.width() / 2.0;
    qreal hue = std::atan2(-d.y(), d.x()) / (2 * M_PI);
    if (hue < 0)
        hue += 1;
    qreal sat = std::min(1.0, std::hypot(d.x(), d.y()) / radius);
    const bool ctrl = mods & Qt::ControlModifier, alt = mods & Qt::AltModifier, shift = mods & Qt::ShiftModifier;
    if (ctrl)
        sat = m_pressColor.hsvSaturationF();
    if (alt && !ctrl)
        hue = hueOf(m_pressColor);
    if (shift) {
        hue = std::round(hue * 24.0) / 24.0;
        if (hue >= 1)
            hue -= 1;
    }
    const qreal value = m_color.valueF() < 0.02 ? 1.0 : m_color.valueF();
    m_color = QColor::fromHsvF(float(hue), float(sat), float(value), m_color.alphaF());
    update();
    emit colorPicked(m_color, m_button);
}

void ColorWheel::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton)
        return;
    m_button = e->button();
    m_pressColor = m_color;
    pickAt(e->position(), e->modifiers());
}

void ColorWheel::mouseMoveEvent(QMouseEvent* e)
{
    if (m_button != Qt::NoButton)
        pickAt(e->position(), e->modifiers());
}

void ColorWheel::mouseReleaseEvent(QMouseEvent*)
{
    m_button = Qt::NoButton;
}

// ---------------------------------------------------------------------------
// Deslizador de canal
// ---------------------------------------------------------------------------

ColorSlider::ColorSlider(Channel channel, Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent)
    , m_channel(channel)
    , m_orientation(orientation)
{
    setSizePolicy(orientation == Qt::Horizontal ? QSizePolicy::Expanding : QSizePolicy::Fixed,
                  orientation == Qt::Horizontal ? QSizePolicy::Fixed : QSizePolicy::Expanding);
}

QSize ColorSlider::sizeHint() const
{
    return m_orientation == Qt::Horizontal ? QSize(120, 16) : QSize(16, 120);
}

int ColorSlider::maximum() const
{
    switch (m_channel) {
    case Hue: return 359;
    case Saturation:
    case Value: return 100;
    default: return 255;
    }
}

int ColorSlider::value() const
{
    switch (m_channel) {
    case Red: return m_color.red();
    case Green: return m_color.green();
    case Blue: return m_color.blue();
    case Alpha: return m_color.alpha();
    case Hue: return int(std::round(hueOf(m_color) * 359));
    case Saturation: return int(std::round(m_color.hsvSaturationF() * 100));
    case Value: return int(std::round(m_color.valueF() * 100));
    }
    return 0;
}

QColor ColorSlider::colorAt(qreal t) const
{
    QColor c = m_color;
    switch (m_channel) {
    case Red: c.setRed(int(t * 255)); break;
    case Green: c.setGreen(int(t * 255)); break;
    case Blue: c.setBlue(int(t * 255)); break;
    case Alpha: c.setAlpha(int(t * 255)); break;
    case Hue: c = QColor::fromHsvF(float(t * 0.999), 1, 1); break;
    case Saturation: c = QColor::fromHsvF(float(hueOf(m_color)), float(t), std::max(0.25f, m_color.valueF())); break;
    case Value: c = QColor::fromHsvF(float(hueOf(m_color)), m_color.hsvSaturationF(), float(t)); break;
    }
    if (m_channel != Alpha)
        c.setAlpha(255);
    return c;
}

void ColorSlider::setColor(const QColor& c)
{
    if (c == m_color)
        return;
    m_color = c;
    update();
}

void ColorSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool horiz = m_orientation == Qt::Horizontal;
    const QRectF bar = horiz ? QRectF(4, 3, width() - 8, height() - 6) : QRectF(3, 4, width() - 6, height() - 8);
    if (m_channel == Alpha)
        paintCheckerboard(p, bar, 4);
    QLinearGradient g(horiz ? bar.topLeft() : bar.bottomLeft(), horiz ? bar.topRight() : bar.topLeft());
    const int stops = m_channel == Hue ? 12 : 2;
    for (int i = 0; i <= stops; ++i)
        g.setColorAt(qreal(i) / stops, colorAt(qreal(i) / stops));
    p.fillRect(bar, g);
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(bar.adjusted(-0.5, -0.5, 0.5, 0.5));

    const qreal t = qreal(value()) / maximum();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette().color(QPalette::WindowText), 1));
    p.setBrush(palette().color(QPalette::Window));
    if (horiz) {
        const qreal x = bar.left() + t * bar.width();
        p.drawPolygon(QPolygonF({QPointF(x - 4, height()), QPointF(x + 4, height()), QPointF(x, bar.bottom() - 2)}));
        p.drawPolygon(QPolygonF({QPointF(x - 4, 0), QPointF(x + 4, 0), QPointF(x, bar.top() + 2)}));
    } else {
        const qreal y = bar.bottom() - t * bar.height();
        p.drawPolygon(QPolygonF({QPointF(0, y - 4), QPointF(0, y + 4), QPointF(bar.left() + 2, y)}));
        p.drawPolygon(QPolygonF({QPointF(width(), y - 4), QPointF(width(), y + 4), QPointF(bar.right() - 2, y)}));
    }
}

void ColorSlider::setFromPos(const QPointF& pos)
{
    const bool horiz = m_orientation == Qt::Horizontal;
    const qreal t = horiz ? (pos.x() - 4) / std::max(1.0, width() - 8.0) : 1.0 - (pos.y() - 4) / std::max(1.0, height() - 8.0);
    emit valueChanged(int(std::round(std::clamp(t, 0.0, 1.0) * maximum())));
}

void ColorSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        setFromPos(e->position());
}

void ColorSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        setFromPos(e->position());
}

void ColorSlider::wheelEvent(QWheelEvent* e)
{
    const int steps = e->angleDelta().y() / 120;
    if (steps)
        emit valueChanged(std::clamp(value() + steps, 0, maximum()));
    e->accept();
}

// ---------------------------------------------------------------------------
// Muestras principal / secundario
// ---------------------------------------------------------------------------

ColorSwatches::ColorSwatches(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(64, 64);
    setToolTip(tr("Color principal (arriba) y secundario (abajo). La muesca marca la ranura activa (C).\n"
                  "Doble clic para editarlos. X intercambia."));
}

void ColorSwatches::setColors(const QColor& primary, const QColor& secondary)
{
    m_primary = primary;
    m_secondary = secondary;
    update();
}

void ColorSwatches::setEditingSecondary(bool secondary)
{
    m_editingSecondary = secondary;
    update();
}

QRectF ColorSwatches::primaryRect() const { return QRectF(2, 2, 38, 38); }
QRectF ColorSwatches::secondaryRect() const { return QRectF(22, 22, 38, 38); }
QRectF ColorSwatches::swapRect() const { return QRectF(44, 2, 18, 16); }
QRectF ColorSwatches::resetRect() const { return QRectF(2, 46, 16, 16); }

void ColorSwatches::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    auto swatch = [&](const QRectF& r, const QColor& c, bool active) {
        paintCheckerboard(p, r, 5);
        p.fillRect(r, c);
        // Como en Paint.NET: marco blanco por dentro y, en la ranura activa, un pico abajo.
        p.setPen(QPen(palette().color(QPalette::WindowText), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        p.setPen(QPen(Qt::white, 1));
        p.drawRect(r.adjusted(1.5, 1.5, -1.5, -1.5));
        if (active) {
            const qreal cx = r.center().x();
            const QPolygonF notch{QPointF(cx - 5, r.bottom() - 1), QPointF(cx, r.bottom() - 7), QPointF(cx + 5, r.bottom() - 1)};
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(Qt::white);
            p.drawPolygon(notch);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    };
    swatch(secondaryRect(), m_secondary, m_editingSecondary);
    swatch(primaryRect(), m_primary, !m_editingSecondary);

    // Flecha doble para intercambiar.
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF s = swapRect();
    QPainterPath arc;
    arc.moveTo(s.left() + 3, s.top() + 4);
    arc.quadTo(s.right() - 3, s.top() + 4, s.right() - 3, s.bottom() - 2);
    p.setPen(QPen(QColor(60, 130, 210), 1.6));
    p.drawPath(arc);
    p.drawLine(QPointF(s.left() + 3, s.top() + 4), QPointF(s.left() + 6, s.top() + 1));
    p.drawLine(QPointF(s.left() + 3, s.top() + 4), QPointF(s.left() + 6, s.top() + 7));
    p.drawLine(QPointF(s.right() - 3, s.bottom() - 2), QPointF(s.right() - 6, s.bottom() - 5));
    p.drawLine(QPointF(s.right() - 3, s.bottom() - 2), QPointF(s.right(), s.bottom() - 5));

    // Mini muestras negro/blanco para restablecer.
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF rr = resetRect();
    p.setPen(palette().color(QPalette::WindowText));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(rr.left() + 6, rr.top() + 6, 9, 9));
    p.setBrush(Qt::black);
    p.drawRect(QRectF(rr.left() + 1, rr.top() + 1, 9, 9));
}

void ColorSwatches::mousePressEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (swapRect().contains(pos))
        emit swapClicked();
    else if (resetRect().contains(pos))
        emit resetClicked();
    else if (primaryRect().contains(pos))
        emit primaryClicked();
    else if (secondaryRect().contains(pos))
        emit secondaryClicked();
}

void ColorSwatches::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (primaryRect().contains(pos))
        emit primaryDoubleClicked();
    else if (secondaryRect().contains(pos))
        emit secondaryDoubleClicked();
}

// ---------------------------------------------------------------------------
// Paleta
// ---------------------------------------------------------------------------

PaletteWidget::PaletteWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip(tr("Clic izquierdo: ranura activa · clic derecho: ranura inactiva"));
}

void PaletteWidget::setColors(const QList<QColor>& colors)
{
    m_colors = colors;
    updateGeometry();
    update();
}

void PaletteWidget::setVisibleCount(int count)
{
    m_visible = count;
    updateGeometry();
    update();
}

void PaletteWidget::setAddMode(bool on)
{
    m_addMode = on;
    if (on && !m_blinkTimer)
        m_blinkTimer = startTimer(450);
    if (!on && m_blinkTimer) {
        killTimer(m_blinkTimer);
        m_blinkTimer = 0;
    }
    m_blinkOn = on;
    setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void PaletteWidget::timerEvent(QTimerEvent* e)
{
    if (e->timerId() == m_blinkTimer) {
        m_blinkOn = !m_blinkOn;
        update();
    }
}

int PaletteWidget::columns() const
{
    return 16;
}

int PaletteWidget::cellSize() const
{
    return std::clamp((width() - 4) / columns(), 6, 18);
}

QSize PaletteWidget::sizeHint() const
{
    return {16 * 13 + 4, heightForWidth(16 * 13 + 4)};
}

int PaletteWidget::heightForWidth(int w) const
{
    const int cell = std::clamp((w - 4) / 16, 6, 18);
    const int count = int(std::min<qsizetype>(m_visible, m_colors.size()));
    return (count + 15) / 16 * cell + 4;
}

QRect PaletteWidget::cellRect(int index) const
{
    const int cell = cellSize();
    return QRect(2 + (index % 16) * cell, 2 + (index / 16) * cell, cell - 1, cell - 1);
}

int PaletteWidget::indexAt(const QPoint& p) const
{
    const int count = int(std::min<qsizetype>(m_visible, m_colors.size()));
    for (int i = 0; i < count; ++i)
        if (cellRect(i).adjusted(0, 0, 1, 1).contains(p))
            return i;
    return -1;
}

void PaletteWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const int count = int(std::min<qsizetype>(m_visible, m_colors.size()));
    for (int i = 0; i < count; ++i) {
        const QRect r = cellRect(i);
        if (m_colors[i].alpha() < 255)
            paintCheckerboard(p, r, 3);
        p.fillRect(r, m_colors[i]);
        p.setPen(QColor(0, 0, 0, 60));
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }
    if (m_addMode && m_blinkOn) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
}

bool PaletteWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const int i = indexAt(he->pos());
        if (i >= 0) {
            const QColor c = m_colors[i];
            QToolTip::showText(he->globalPos(),
                               tr("%1\nClic izquierdo: ranura activa · clic derecho: ranura inactiva")
                                   .arg(c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb).toUpper()),
                               this);
            return true;
        }
    }
    return QWidget::event(e);
}

void PaletteWidget::mousePressEvent(QMouseEvent* e)
{
    const int i = indexAt(e->position().toPoint());
    if (i >= 0)
        emit colorClicked(i, m_colors[i], e->button());
}
