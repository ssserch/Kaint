#include "app/Ruler.h"

#include "app/Canvas.h"
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

void Ruler::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Window));
    const Document* doc = m_canvas->document();
    if (!doc)
        return;

    const bool horiz = m_orientation == Qt::Horizontal;
    const double ppu = pixelsPerUnit(m_canvas->unit(), doc->dpi());
    const double scale = m_canvas->scale() * ppu; // píxeles de pantalla por unidad
    // Coordenada de pantalla (dentro de la regla) de una posición de imagen.
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
        hl.setAlpha(90);
        if (horiz)
            p.fillRect(QRectF(a, 0, c - a, height()), hl);
        else
            p.fillRect(QRectF(0, a, width(), c - a), hl);
    }

    // Paso "bonito" (1, 2, 5 x 10^n) con al menos ~60 px entre números.
    double step = 1;
    const double steps[] = {1, 2, 5};
    for (double mag = 1e-3; mag < 1e7; mag *= 10) {
        bool found = false;
        for (double s : steps)
            if (s * mag * scale >= 60) {
                step = s * mag;
                found = true;
                break;
            }
        if (found)
            break;
    }
    const double startUnits = std::floor((-toRuler(0) / scale) / step) * step - step;
    QFont f = font();
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.75));
    p.setFont(f);
    p.setPen(pal.color(QPalette::WindowText));
    for (double u = startUnits;; u += step / 5) {
        const double pos = toRuler(u * ppu);
        if (pos > length)
            break;
        if (pos < -60)
            continue;
        const bool major = std::abs(std::remainder(u, step)) < step * 1e-6;
        const int tick = major ? kThickness : kThickness / 3;
        if (horiz)
            p.drawLine(QPointF(pos, kThickness - tick), QPointF(pos, kThickness));
        else
            p.drawLine(QPointF(kThickness - tick, pos), QPointF(kThickness, pos));
        if (major) {
            const QString label = QString::number(u, 'g', 6);
            if (horiz) {
                p.drawText(QPointF(pos + 2, f.pointSizeF() + 3), label);
            } else {
                p.save();
                p.translate(f.pointSizeF() + 3, pos + 2);
                p.rotate(90);
                p.drawText(QPointF(0, 0), label);
                p.restore();
            }
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
    p.setPen(pal.color(QPalette::Mid));
    if (horiz)
        p.drawLine(0, height() - 1, width(), height() - 1);
    else
        p.drawLine(width() - 1, 0, width() - 1, height());
}
