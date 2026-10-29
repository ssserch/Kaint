#include "app/Ruler.h"

#include "app/Canvas.h"
#include "app/Theme.h"
#include "core/Document.h"

#include <QPainter>

#include <cmath>

QString unitSuffix(Unit unit)
{
    switch (unit) {
    case Unit::Pixels: return QObject::tr("px");
    case Unit::Inches: return QObject::tr("in");
    case Unit::Centimeters: return QObject::tr("cm");
    }
    return {};
}

double pixelsPerUnit(Unit unit, double dpi)
{
    switch (unit) {
    case Unit::Pixels: return 1.0;
    case Unit::Inches: return dpi;
    case Unit::Centimeters: return dpi / 2.54;
    }
    return 1.0;
}

QString formatLength(double pixels, Unit unit, double dpi)
{
    if (unit == Unit::Pixels)
        return QString::number(std::lround(pixels));
    return QString::number(pixels / pixelsPerUnit(unit, dpi), 'f', 2);
}

Ruler::Ruler(Qt::Orientation orientation, Canvas* canvas, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_canvas(canvas)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QSize Ruler::sizeHint() const
{
    return m_orientation == Qt::Horizontal ? QSize(100, kThickness) : QSize(kThickness, 100);
}

namespace {

Ruler::Style g_style = Ruler::Style::PaintJs;

// Colores de la regla de paint.js: siempre oscura, con las marcas grises y los números claros.
const QColor kPaintJsBackground(0x20, 0x20, 0x20);
const QColor kPaintJsTick(0xa8, 0xa8, 0xa8);
const QColor kPaintJsText(0xe5, 0xe5, 0xe5);
const QColor kPaintJsEdge(0x57, 0x57, 0x57);

} // namespace

void Ruler::setStyle(Style style)
{
    g_style = style;
}

Ruler::Style Ruler::style()
{
    return g_style;
}

QColor Ruler::background()
{
    return g_style == Style::PaintJs ? kPaintJsBackground : Theme::active().toolBar;
}

void Ruler::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = palette();
    const bool paintJs = g_style == Style::PaintJs;
    // paint.js: oscura; si no, como en Paint.NET, del color de la barra de herramientas que tiene
    // encima.
    p.fillRect(rect(), background());
    const Document* doc = m_canvas->document();
    if (!doc)
        return;

    const bool horiz = m_orientation == Qt::Horizontal;
    const double ppu = pixelsPerUnit(m_canvas->unit(), doc->dpi());
    const double scale = m_canvas->scale() * ppu;
    const QPoint offset = m_canvas->viewport()->mapTo(parentWidget(), QPoint(0, 0)) - pos();
    auto toRuler = [&](double imagePx) {
        const QPointF v = m_canvas->imageToView(QPointF(imagePx, imagePx));
        return horiz ? v.x() + offset.x() : v.y() + offset.y();
    };
    const int length = horiz ? width() : height();

    // Selección resaltada.
    if (!doc->selection().isEmpty()) {
        const QRect b = doc->selection().bounds();
        const double a = toRuler(horiz ? b.left() : b.top());
        const double c = toRuler(horiz ? b.right() + 1 : b.bottom() + 1);
        QColor hl = pal.color(QPalette::Highlight);
        hl.setAlpha(paintJs ? 120 : 90);
        if (horiz)
            p.fillRect(QRectF(a, 0, c - a, height()), hl);
        else
            p.fillRect(QRectF(0, a, width(), c - a), hl);
    }

    // Paso entre números. paint.js: en píxeles 1, 2, 5, 10, 20, 40, 80... con al menos 35 px
    // entre marcas; si no (o en pulgadas y centímetros), 1, 2, 5 x 10^n con al menos ~60 px.
    double step = 1;
    if (paintJs && m_canvas->unit() == Unit::Pixels) {
        while (step * scale < 35 && step < 1e7)
            step *= step == 2 ? 2.5 : 2;
    } else {
        const double minGap = paintJs ? 35 : 60;
        const double steps[] = {1, 2, 5};
        bool found = false;
        for (double mag = 1e-3; mag < 1e7 && !found; mag *= 10) {
            for (double s : steps)
                if (s * mag * scale >= minGap) {
                    step = s * mag;
                    found = true;
                    break;
                }
        }
    }
    const double startUnits = std::floor((-toRuler(0) / scale) / step) * step - step;
    QFont f = font();
    if (paintJs)
        f.setPixelSize(10);
    else
        f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.75));
    p.setFont(f);
    const QColor tickColor = paintJs ? kPaintJsTick : pal.color(QPalette::WindowText);
    const QColor textColor = paintJs ? kPaintJsText : pal.color(QPalette::WindowText);
    // paint.js solo dibuja las marcas con número, cortas; Kaint, además, cuatro intermedias.
    const double minorStep = paintJs ? step : step / 5;
    for (double u = startUnits;; u += minorStep) {
        const double pos = toRuler(u * ppu);
        if (pos > length)
            break;
        if (pos < -60)
            continue;
        const bool major = std::abs(std::remainder(u, step)) < step * 1e-6;
        const int tick = paintJs ? 6 : major ? kThickness : kThickness / 3;
        p.setPen(tickColor);
        if (horiz)
            p.drawLine(QPointF(pos + 0.5, kThickness - tick), QPointF(pos + 0.5, kThickness));
        else
            p.drawLine(QPointF(kThickness - tick, pos + 0.5), QPointF(kThickness, pos + 0.5));
        if (!major)
            continue;
        const QString label = QString::number(std::abs(u) < step * 1e-6 ? 0.0 : u, 'g', 6);
        p.setPen(textColor);
        if (paintJs) {
            // Como paint.js: en la vertical, el número de abajo arriba sobre la marca.
            if (horiz) {
                p.drawText(QPointF(pos + 2, 12), label);
            } else {
                p.save();
                p.translate(kThickness - 9, pos + 2);
                p.rotate(-90);
                p.drawText(QPointF(0, 0), label);
                p.restore();
            }
        } else if (horiz) {
            p.drawText(QPointF(pos + 2, f.pointSizeF() + 3), label);
        } else {
            p.save();
            p.translate(f.pointSizeF() + 3, pos + 2);
            p.rotate(90);
            p.drawText(QPointF(0, 0), label);
            p.restore();
        }
    }

    // Posición del cursor.
    const QPointF cursor = m_canvas->cursorImagePos();
    if (m_canvas->cursorInside()) {
        const double c = toRuler(horiz ? cursor.x() : cursor.y());
        p.setPen(QPen(pal.color(QPalette::Highlight), 1));
        if (horiz)
            p.drawLine(QPointF(c, 0), QPointF(c, height()));
        else
            p.drawLine(QPointF(0, c), QPointF(width(), c));
    }
    p.setPen(paintJs ? kPaintJsEdge : pal.color(QPalette::Mid));
    if (horiz)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());
}
