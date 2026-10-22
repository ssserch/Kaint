#include "app/tools/Handles.h"

#include <QPainter>
#include <QPainterPath>

namespace Handles {

void draw(QPainter& p, const QPointF& c, Shape shape)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor dark(30, 30, 30, 230);
    const QColor light(255, 255, 255, 240);
    switch (shape) {
    case Shape::Square:
        p.setPen(QPen(dark, 1));
        p.setBrush(light);
        p.drawRect(QRectF(c - QPointF(4, 4), QSizeF(8, 8)));
        break;
    case Shape::Circle:
        p.setPen(QPen(dark, 1));
        p.setBrush(light);
        p.drawEllipse(c, 4.5, 4.5);
        break;
    case Shape::Rotate:
        p.setPen(QPen(dark, 1));
        p.setBrush(light);
        p.drawEllipse(c, 6, 6);
        p.drawLine(c - QPointF(4, 0), c + QPointF(4, 0));
        p.drawLine(c - QPointF(0, 4), c + QPointF(0, 4));
        break;
    case Shape::ResetPivot: {
        // Rombo azul con la flecha de "volver", como el de Paint.NET.
        p.translate(c);
        p.rotate(45);
        p.setPen(QPen(dark, 1));
        p.setBrush(QColor(0, 120, 215));
        p.drawRect(QRectF(-6.5, -6.5, 13, 13));
        p.rotate(-45);
        p.setPen(QPen(light, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(-3.5, -3.5, 7, 7), 30 * 16, 270 * 16);
        p.setBrush(light);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF{QPointF(1.5, -5.5), QPointF(4.5, -3.5), QPointF(1.5, -1.5)});
        break;
    }
    case Shape::Move: {
        // Cuadrado blanco con cuatro flechas, como el de Paint.NET.
        const QRectF box(c - QPointF(9, 9), QSizeF(18, 18));
        p.setPen(QPen(dark, 1));
        p.setBrush(light);
        p.drawRoundedRect(box, 3, 3);
        p.setPen(QPen(dark, 1.4));
        p.drawLine(c - QPointF(6, 0), c + QPointF(6, 0));
        p.drawLine(c - QPointF(0, 6), c + QPointF(0, 6));
        p.setBrush(dark);
        p.setPen(Qt::NoPen);
        for (const QPointF& dir : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
            const QPointF tip = c + dir * 7;
            const QPointF side(-dir.y(), dir.x());
            p.drawPolygon(QPolygonF{tip, tip - dir * 3 + side * 2.5, tip - dir * 3 - side * 2.5});
        }
        break;
    }
    }
    p.restore();
}

void drawPivot(QPainter& p, const QPointF& c, double degrees)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor dark(30, 30, 30, 230);
    const QColor light(255, 255, 255, 240);
    p.translate(c);
    p.rotate(degrees);
    p.setPen(QPen(dark, 1));
    p.setBrush(light);
    p.drawEllipse(QPointF(), 6, 6);
    p.drawLine(QPointF(-4, 0), QPointF(4, 0));
    p.drawLine(QPointF(0, -4), QPointF(0, 4));
    p.restore();
}

QPointF moveIconPos(const QRectF& r)
{
    return r.bottomRight() + QPointF(16, 16);
}

QPointF boxHandle(const QRectF& r, int index)
{
    switch (index) {
    case 0: return r.topLeft();
    case 1: return QPointF(r.center().x(), r.top());
    case 2: return r.topRight();
    case 3: return QPointF(r.right(), r.center().y());
    case 4: return r.bottomRight();
    case 5: return QPointF(r.center().x(), r.bottom());
    case 6: return r.bottomLeft();
    default: return QPointF(r.left(), r.center().y());
    }
}

QPointF oppositeHandle(const QRectF& r, int index)
{
    return boxHandle(r, (index + 4) % 8);
}

bool isCorner(int index)
{
    return index % 2 == 0;
}

QPointF handleAxes(int index)
{
    switch (index) {
    case 0: return {-1, -1};
    case 1: return {0, -1};
    case 2: return {1, -1};
    case 3: return {1, 0};
    case 4: return {1, 1};
    case 5: return {0, 1};
    case 6: return {-1, 1};
    default: return {-1, 0};
    }
}

void drawBox(QPainter& p, const QPolygonF& corners)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 200), 0));
    p.drawPolygon(corners);
    p.setPen(QPen(QColor(0, 0, 0, 200), 0, Qt::DashLine));
    p.drawPolygon(corners);
    p.restore();
}

} // namespace Handles
