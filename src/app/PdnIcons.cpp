#include "app/Icons.h"

#include "app/Theme.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QStyle>
#include <QStyleOption>
#include <QtMath>

#include <cmath>

// Iconos dibujados por código imitando los de Paint.NET. Cada uno se dibuja en una rejilla de
// 20 px (herramientas) o 16 px (barras y ventanas) y se escala a cualquier tamaño.
namespace Icons {

namespace {

constexpr int TS = 20;

// Colores activos para las partes que se tiñen (inválidos: los colores propios del icono).
QColor g_primary;
QColor g_secondary;

QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QColor tint(const QColor& active, const QColor& fallback)
{
    if (!active.isValid())
        return fallback;
    QColor c = active.toRgb();
    c.setAlpha(255);
    return c;
}

// Borde que se distingue tanto en colores claros como en oscuros.
QColor outlineFor(const QColor& c)
{
    return c.lightnessF() < 0.25 ? mix(c, Qt::white, 0.45) : mix(c, Qt::black, 0.55);
}

void aa(QPainter& p)
{
    p.setRenderHint(QPainter::Antialiasing, true);
}

void marchingAnts(QPainter& p)
{
    QPen pen(QColor(65, 75, 88), 1.2, Qt::DashLine);
    QVector<qreal> d; d << 2.5 << 1.8;
    pen.setDashPattern(d);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
}

void drawRectSelect(QPainter& p)
{
    // Rectángulo azul claro con el borde de hormigas, como en Paint.NET.
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(178, 215, 245));
    p.drawRect(QRectF(3, 3, 14, 14));
    marchingAnts(p);
    p.drawRect(QRectF(3, 3, 14, 14));
}

void drawEllipseSelect(QPainter& p)
{
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(137, 190, 233));
    p.drawEllipse(3, 3, 14, 14);
    marchingAnts(p);
    p.drawEllipse(3, 3, 14, 14);
}

void drawMagicWand(QPainter& p)
{
    aa(p);
    QPen shaft(QColor(74, 85, 101), 3.0);
    shaft.setCapStyle(Qt::RoundCap);
    p.setPen(shaft);
    p.drawLine(3, 17, 11, 9);
    QPen tip(QColor(196, 208, 222), 3.0);
    tip.setCapStyle(Qt::RoundCap);
    p.setPen(tip);
    p.drawLine(11, 9, 13, 7);
    auto sparkle = [&](qreal cx, qreal cy, qreal r, const QColor &c, qreal w) {
        QPen sp(c, w); sp.setCapStyle(Qt::RoundCap);
        p.setPen(sp);
        p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
        p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy));
    };
    sparkle(15.5, 4.0, 3.2, QColor(255, 196, 36), 1.6);
    sparkle(9.5, 3.0, 1.9, QColor(255, 226, 120), 1.2);
    sparkle(18.0, 9.5, 1.7, QColor(255, 226, 120), 1.2);
}

void drawMove(QPainter& p)
{
    aa(p);
    QPainterPath a;
    a.moveTo(2, 1); a.lineTo(2, 12.5); a.lineTo(5.0, 9.8);
    a.lineTo(7.0, 14.2); a.lineTo(8.9, 13.3); a.lineTo(7.0, 9.2);
    a.lineTo(11.0, 8.8); a.closeSubpath();
    p.setPen(QPen(QColor(32, 56, 92), 1.0));
    p.setBrush(QColor(96, 146, 205));
    p.drawPath(a);
    const qreal cx = 14.5, cy = 14.5, r = 5.0;
    QPen cp(QColor(64, 84, 110), 1.3); cp.setCapStyle(Qt::RoundCap);
    p.setPen(cp);
    p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
    p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(64, 84, 110));
    const qreal h = 2.0;
    QPointF up[] = {QPointF(cx, cy - r - 0.8), QPointF(cx - h, cy - r + 1.6), QPointF(cx + h, cy - r + 1.6)};
    QPointF dn[] = {QPointF(cx, cy + r + 0.8), QPointF(cx - h, cy + r - 1.6), QPointF(cx + h, cy + r - 1.6)};
    QPointF lf[] = {QPointF(cx - r - 0.8, cy), QPointF(cx - r + 1.6, cy - h), QPointF(cx - r + 1.6, cy + h)};
    QPointF rt[] = {QPointF(cx + r + 0.8, cy), QPointF(cx + r - 1.6, cy - h), QPointF(cx + r - 1.6, cy + h)};
    p.drawPolygon(up, 3); p.drawPolygon(dn, 3); p.drawPolygon(lf, 3); p.drawPolygon(rt, 3);
}

// Arco con punta de flecha al final (span positivo: sentido antihorario), con relleno y borde.
void arrowArc(QPainter& p, QPointF c, qreal r, qreal startDeg, qreal spanDeg, qreal width, const QColor& fill,
              const QColor& line)
{
    const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
    QPainterPath arc;
    arc.arcMoveTo(box, startDeg);
    arc.arcTo(box, startDeg, spanDeg);
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::FlatCap);
    QPainterPath shape = stroker.createStroke(arc);
    const qreal end = qDegreesToRadians(startDeg + spanDeg);
    const qreal dir = spanDeg >= 0 ? 1 : -1;
    const QPointF tip = c + QPointF(std::cos(end), -std::sin(end)) * r;
    const QPointF tangent(-std::sin(end) * dir, -std::cos(end) * dir);
    const QPointF radial(std::cos(end), -std::sin(end));
    QPainterPath head;
    head.addPolygon(QPolygonF{tip + tangent * width * 1.35, tip + radial * width * 1.2, tip - radial * width * 1.2});
    head.closeSubpath();
    p.setPen(QPen(line, 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(fill);
    p.drawPath(shape.united(head).simplified());
}

void drawBrush(QPainter& p)
{
    // Pincel en diagonal como el de Paint.NET: punta con el color principal, virola metálica y
    // mango oscuro.
    aa(p);
    const QColor paint = tint(g_primary, QColor(46, 110, 210));
    p.save();
    p.translate(2.0, 18.0);
    p.rotate(-45);
    QPainterPath bristles;
    bristles.moveTo(0, 0.9);
    bristles.cubicTo(1.4, -1.6, 4.8, -2.9, 8.2, -2.5);
    bristles.lineTo(8.2, 2.5);
    bristles.cubicTo(5.0, 2.9, 2.0, 2.4, 0, 0.9);
    QLinearGradient tipFill(0, -2.6, 0, 2.6);
    tipFill.setColorAt(0, mix(paint, Qt::white, 0.35));
    tipFill.setColorAt(1, paint);
    p.setPen(QPen(outlineFor(paint), 0.8));
    p.setBrush(tipFill);
    p.drawPath(bristles);
    QLinearGradient metal(0, -2.6, 0, 2.6);
    metal.setColorAt(0, QColor(236, 240, 245));
    metal.setColorAt(1, QColor(140, 150, 162));
    p.setPen(QPen(QColor(80, 88, 98), 0.8));
    p.setBrush(metal);
    p.drawRect(QRectF(8.2, -2.6, 3.2, 5.2));
    QPainterPath handle;
    handle.moveTo(11.4, -2.0);
    handle.lineTo(20.6, -1.2);
    handle.quadTo(22.2, 0, 20.6, 1.2);
    handle.lineTo(11.4, 2.0);
    handle.closeSubpath();
    QLinearGradient wood(0, -2.0, 0, 2.0);
    wood.setColorAt(0, QColor(112, 118, 128));
    wood.setColorAt(1, QColor(52, 56, 64));
    p.setPen(QPen(QColor(30, 32, 38), 0.8));
    p.setBrush(wood);
    p.drawPath(handle);
    p.restore();
}

void drawPencil(QPainter& p)
{
    // Lápiz de madera con goma rosa y punta oscura, como en Paint.NET.
    aa(p);
    p.save();
    p.translate(2.0, 18.0);
    p.rotate(-45);
    QPainterPath tip;
    tip.moveTo(0, 0); tip.lineTo(4.6, -1.8); tip.lineTo(4.6, 1.8); tip.closeSubpath();
    p.setPen(QPen(QColor(90, 70, 40), 0.7));
    p.setBrush(QColor(236, 210, 160));
    p.drawPath(tip);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 50, 56));
    QPainterPath lead;
    lead.moveTo(0, 0); lead.lineTo(1.8, -0.7); lead.lineTo(1.8, 0.7); lead.closeSubpath();
    p.drawPath(lead);
    QLinearGradient wood(0, -1.8, 0, 1.8);
    wood.setColorAt(0, QColor(240, 206, 140));
    wood.setColorAt(1, QColor(200, 150, 80));
    p.setPen(QPen(QColor(120, 84, 40), 0.7));
    p.setBrush(wood);
    p.drawRect(QRectF(4.6, -1.8, 13.4, 3.6));
    p.setBrush(QColor(170, 176, 186));
    p.drawRect(QRectF(18.0, -1.8, 1.6, 3.6));
    p.setBrush(QColor(236, 120, 130));
    p.drawRoundedRect(QRectF(19.6, -1.8, 2.6, 3.6), 1.2, 1.2);
    p.restore();
}

void drawEraser(QPainter& p)
{
    // Goma en diagonal, mitad morada y mitad blanca, como en Paint.NET.
    aa(p);
    p.save();
    p.translate(10, 10);
    p.rotate(-35);
    const QRectF body(-8.4, -3.6, 16.8, 7.2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(176, 80, 196));
    p.drawRoundedRect(body, 1.4, 1.4);
    p.setBrush(QColor(246, 236, 248));
    p.drawRoundedRect(QRectF(-1.2, -3.6, 9.6, 7.2), 1.4, 1.4);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(90, 40, 104), 0.9));
    p.drawRoundedRect(body, 1.4, 1.4);
    p.drawLine(QPointF(-1.2, -3.6), QPointF(-1.2, 3.6));
    p.restore();
}

void drawFill(QPainter& p)
{
    // Como el de Paint.NET: cubo gris en rombo con su asa y pintura del color principal que se
    // derrama por la izquierda.
    aa(p);
    const QColor color = tint(g_primary, QColor(40, 110, 215));
    QPainterPath paint;
    paint.moveTo(7.6, 8.4);
    paint.cubicTo(3.0, 8.0, 0.8, 11.0, 1.2, 15.0);
    paint.cubicTo(1.4, 17.4, 2.6, 18.6, 4.0, 18.6);
    paint.cubicTo(4.6, 16.0, 3.6, 13.4, 5.4, 12.0);
    paint.cubicTo(6.4, 11.2, 7.6, 11.2, 8.6, 11.4);
    paint.closeSubpath();
    QLinearGradient paintFill(0, 8, 0, 19);
    paintFill.setColorAt(0, mix(color, Qt::white, 0.3));
    paintFill.setColorAt(1, color);
    p.setPen(QPen(outlineFor(color), 0.8));
    p.setBrush(paintFill);
    p.drawPath(paint);

    // Asa en arco por encima de la boca del cubo.
    QPainterPath handle;
    handle.moveTo(8.2, 9.8);
    handle.cubicTo(6.6, 4.4, 12.2, 1.4, 15.8, 8.6);
    p.setPen(QPen(QColor(60, 64, 72), 1.1, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawPath(handle);
    QPolygonF body{QPointF(12.6, 5.4), QPointF(18.6, 11.4), QPointF(12.6, 17.4), QPointF(6.6, 11.4)};
    QLinearGradient metal(7, 6, 18, 17);
    metal.setColorAt(0, QColor(222, 226, 232));
    metal.setColorAt(1, QColor(120, 128, 140));
    p.setPen(QPen(QColor(64, 70, 80), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(metal);
    p.drawPolygon(body);
    QPolygonF mouth{QPointF(12.6, 5.4), QPointF(15.0, 7.8), QPointF(9.0, 13.8), QPointF(6.6, 11.4)};
    p.setBrush(QColor(84, 90, 100));
    p.drawPolygon(mouth);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF{QPointF(11.4, 7.0), QPointF(13.0, 8.6), QPointF(8.6, 13.0), QPointF(7.0, 11.4)});
}

void drawColorPicker(QPainter& p)
{
    // Cuentagotas en diagonal: punta de cristal con color, tubo, anillo y bulbo azul.
    aa(p);
    p.save();
    p.translate(2.2, 17.8);
    p.rotate(-45);
    QPainterPath tip;
    tip.moveTo(0, 0);
    tip.lineTo(4.4, -1.1);
    tip.lineTo(4.4, 1.1);
    tip.closeSubpath();
    p.setPen(QPen(QColor(40, 70, 120), 0.7));
    p.setBrush(QColor(70, 140, 230));
    p.drawPath(tip);
    p.setPen(QPen(QColor(80, 96, 118), 0.8));
    p.setBrush(QColor(236, 243, 251));
    p.drawRect(QRectF(4.4, -1.6, 7.4, 3.2));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(70, 140, 230));
    p.drawRect(QRectF(4.8, -1.2, 3.0, 2.4));
    p.setPen(QPen(QColor(60, 68, 80), 0.8));
    p.setBrush(QColor(150, 160, 172));
    p.drawRect(QRectF(11.8, -2.4, 2.2, 4.8));
    QLinearGradient bulb(0, -3.2, 0, 3.2);
    bulb.setColorAt(0, QColor(96, 150, 225));
    bulb.setColorAt(1, QColor(24, 64, 140));
    p.setPen(QPen(QColor(18, 44, 96), 0.8));
    p.setBrush(bulb);
    p.drawRoundedRect(QRectF(14.0, -3.2, 7.6, 6.4), 3.2, 3.2);
    p.restore();
}

void drawRecolor(QPainter& p)
{
    // Como el de Paint.NET: el color secundario (arriba) pasa a ser el principal (abajo).
    aa(p);
    const QColor primary = tint(g_primary, QColor(50, 120, 220));
    const QColor secondary = tint(g_secondary, QColor(225, 80, 50));
    p.setPen(QPen(outlineFor(secondary), 0.9));
    p.setBrush(secondary);
    p.drawEllipse(QPointF(14.0, 6.0), 4.4, 4.4);
    p.setPen(QPen(outlineFor(primary), 0.9));
    p.setBrush(primary);
    p.drawEllipse(QPointF(6.0, 14.0), 4.4, 4.4);
    QPainterPath shaft;
    shaft.moveTo(13.6, 6.4);
    shaft.lineTo(8.6, 11.4);
    QPainterPathStroker stroker;
    stroker.setWidth(2.6);
    stroker.setCapStyle(Qt::FlatCap);
    QPainterPath arrow = stroker.createStroke(shaft);
    QPainterPath head;
    head.addPolygon(QPolygonF{QPointF(6.0, 14.0), QPointF(6.4, 8.6), QPointF(11.4, 13.6)});
    head.closeSubpath();
    p.setPen(QPen(QColor(40, 110, 92), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(236, 248, 244));
    p.drawPath(arrow.united(head).simplified());
}

void drawCloneStamp(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(38, 42, 50), 0.9));
    p.setBrush(QColor(82, 90, 104));
    p.drawEllipse(QRectF(7.0, 1.0, 6.0, 5.2));
    p.setBrush(QColor(112, 120, 134));
    p.drawRect(QRectF(8.6, 4.8, 2.8, 5.4));
    p.setPen(QPen(QColor(140, 84, 20), 0.9));
    QLinearGradient bg(4, 10, 16, 15);
    bg.setColorAt(0.0, QColor(248, 182, 88));
    bg.setColorAt(1.0, QColor(210, 130, 36));
    p.setBrush(bg);
    QPainterPath base;
    base.moveTo(5.0, 10.2); base.lineTo(15.0, 10.2);
    base.lineTo(16.6, 14.2); base.lineTo(3.4, 14.2);
    base.closeSubpath();
    p.drawPath(base);
    p.setBrush(QColor(184, 110, 28));
    p.drawRect(QRectF(3.2, 14.6, 13.6, 2.6));
}

void drawText(QPainter& p)
{
    // "T" con remates, gris, como en Paint.NET.
    aa(p);
    QPainterPath t;
    t.moveTo(2.5, 2); t.lineTo(17.5, 2); t.lineTo(17.5, 6.2); t.lineTo(16.2, 6.2); t.quadTo(15.8, 3.8, 13.2, 3.6);
    t.lineTo(11.4, 3.6); t.lineTo(11.4, 16.2); t.quadTo(11.6, 17.2, 13.6, 17.2); t.lineTo(13.6, 18.2); t.lineTo(6.4, 18.2);
    t.lineTo(6.4, 17.2); t.quadTo(8.4, 17.2, 8.6, 16.2); t.lineTo(8.6, 3.6); t.lineTo(6.8, 3.6); t.quadTo(4.2, 3.8, 3.8, 6.2);
    t.lineTo(2.5, 6.2); t.closeSubpath();
    p.setPen(QPen(QColor(70, 76, 86), 0.6));
    p.setBrush(QColor(128, 136, 150));
    p.drawPath(t);
}

void drawLine(QPainter& p)
{
    aa(p);
    QPen cp(QColor(158, 172, 192), 1.9);
    cp.setCapStyle(Qt::RoundCap);
    p.setPen(cp);
    p.setBrush(Qt::NoBrush);
    QPainterPath c;
    c.moveTo(3.4, 16.4);
    c.cubicTo(7.0, 6.0, 13.0, 18.0, 16.8, 4.0);
    p.drawPath(c);
    p.setPen(QPen(QColor(28, 60, 100), 0.9));
    p.setBrush(QColor(100, 154, 216));
    p.drawEllipse(QPointF(3.4, 16.4), 2.3, 2.3);
    p.drawEllipse(QPointF(16.8, 4.0), 2.3, 2.3);
}

void drawShape(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(28, 60, 104), 0.9));
    p.setBrush(QColor(80, 138, 210));
    p.drawRect(QRectF(1.6, 2.8, 9.4, 9.4));
    p.setPen(QPen(QColor(34, 92, 52), 0.9));
    p.setBrush(QColor(106, 184, 110));
    QPointF tri[] = { QPointF(14.6, 6.2), QPointF(19.0, 16.8), QPointF(10.2, 16.8) };
    p.drawPolygon(tri, 3);
    p.setPen(QPen(QColor(84, 40, 110), 0.9));
    p.setBrush(QColor(170, 98, 210));
    p.drawEllipse(QRectF(5.8, 8.2, 8.8, 8.8));
}

void drawGradient(QPainter& p)
{
    // Cuadro con marco claro y degradado del color principal al secundario, como el de Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(96, 102, 112), 0.8));
    p.setBrush(QColor(232, 235, 239));
    p.drawRect(QRectF(2.4, 2.4, 15.2, 15.2));
    QLinearGradient grad(4.5, 4.5, 15.5, 15.5);
    grad.setColorAt(0.0, tint(g_primary, QColor(56, 120, 230)));
    grad.setColorAt(1.0, tint(g_secondary, QColor(160, 72, 205)));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRect(QRectF(4.4, 4.4, 11.2, 11.2));
}

void drawLasso(QPainter& p)
{
    aa(p);
    QPen rope(QColor(50, 106, 176), 1.7);
    rope.setCapStyle(Qt::RoundCap);
    p.setPen(rope);
    p.setBrush(QColor(178, 215, 245));
    p.drawEllipse(QRectF(3.2, 2.0, 12.4, 9.8));
    p.setBrush(Qt::NoBrush);
    QPainterPath tail;
    tail.moveTo(9.0, 11.8);
    tail.cubicTo(9.4, 15.0, 13.8, 15.4, 12.2, 18.6);
    p.drawPath(tail);
}

void drawMoveSelection(QPainter& p)
{
    aa(p);
    QPainterPath a;
    a.moveTo(2, 1); a.lineTo(2, 12.5); a.lineTo(5.0, 9.8);
    a.lineTo(7.0, 14.2); a.lineTo(8.9, 13.3); a.lineTo(7.0, 9.2);
    a.lineTo(11.0, 8.8); a.closeSubpath();
    QPen ap(QColor(104, 154, 212), 1.3);
    ap.setJoinStyle(Qt::RoundJoin);
    p.setPen(ap);
    p.setBrush(Qt::NoBrush);
    p.drawPath(a);
    const qreal cx = 14.5, cy = 14.5, r = 5.0;
    QPen cp(QColor(64, 84, 110), 1.3); cp.setCapStyle(Qt::RoundCap);
    p.setPen(cp);
    p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
    p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(64, 84, 110));
    const qreal h = 2.0;
    QPointF up[] = {QPointF(cx, cy - r - 0.8), QPointF(cx - h, cy - r + 1.6), QPointF(cx + h, cy - r + 1.6)};
    QPointF dn[] = {QPointF(cx, cy + r + 0.8), QPointF(cx - h, cy + r - 1.6), QPointF(cx + h, cy + r - 1.6)};
    QPointF lf[] = {QPointF(cx - r - 0.8, cy), QPointF(cx - r + 1.6, cy - h), QPointF(cx - r + 1.6, cy + h)};
    QPointF rt[] = {QPointF(cx + r + 0.8, cy), QPointF(cx + r - 1.6, cy - h), QPointF(cx + r - 1.6, cy + h)};
    p.drawPolygon(up, 3); p.drawPolygon(dn, 3); p.drawPolygon(lf, 3); p.drawPolygon(rt, 3);
}

void drawZoom(QPainter& p)
{
    // Lupa con mango azul oscuro y la cuadrícula dentro (el cristal, transparente), como en
    // Paint.NET.
    aa(p);
    QPen hp(QColor(40, 80, 140), 3.0);
    hp.setCapStyle(Qt::RoundCap);
    p.setPen(hp);
    p.drawLine(QPointF(12.6, 12.6), QPointF(17.6, 17.6));
    p.setPen(QPen(QColor(40, 80, 140), 1.8));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(2.2, 2.2, 11.6, 11.6));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 60, 76));
    for (int i = 0; i < 4; ++i)
        p.drawRect(QRectF(5.0 + (i % 2) * 3.3, 5.0 + (i / 2) * 3.3, 2.7, 2.7));
}

void drawPan(QPainter& p)
{
    // Mano abierta como la de Paint.NET: cuatro dedos rectos, el pulgar en diagonal a la
    // izquierda, color tostado y contorno marrón. Coordenadas en medios píxeles para que el
    // contorno quede nítido a 20 px.
    aa(p);
    QPainterPath hand;
    hand.setFillRule(Qt::WindingFill);
    hand.addRoundedRect(QRectF(5.5, 4.5, 3, 9), 1.5, 1.5);
    hand.addRoundedRect(QRectF(8.5, 2.5, 3, 11), 1.5, 1.5);
    hand.addRoundedRect(QRectF(11.5, 3.5, 3, 10), 1.5, 1.5);
    hand.addRoundedRect(QRectF(14.5, 5.5, 3, 8), 1.5, 1.5);
    hand.addRoundedRect(QRectF(5.5, 9.5, 12, 8.5), 3, 3);
    QTransform thumb;
    thumb.translate(6.4, 14.2);
    thumb.rotate(50);
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(QRectF(-6, -1.6, 7.5, 3.2), 1.6, 1.6);
    hand.addPath(thumb.map(thumbPath));
    hand = hand.simplified();

    const QColor outline(110, 66, 28);
    QLinearGradient skin(0, 2, 0, 18);
    skin.setColorAt(0, QColor(246, 206, 148));
    skin.setColorAt(1, QColor(222, 160, 88));
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(skin);
    p.drawPath(hand);
    // Separaciones entre los dedos, desde la punta del más corto hasta la palma.
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(8.5, 6), QPointF(8.5, 11));
    p.drawLine(QPointF(11.5, 5), QPointF(11.5, 11));
    p.drawLine(QPointF(14.5, 7), QPointF(14.5, 11));
}

void drawNewDoc(QPainter& p)
{
    // Hoja con la esquina doblada y un "+" verde, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(Qt::white);
    QPainterPath page;
    page.moveTo(2.5, 0.5); page.lineTo(8.5, 0.5); page.lineTo(11.5, 3.5); page.lineTo(11.5, 9); page.lineTo(9, 14.5);
    page.lineTo(2.5, 14.5); page.closeSubpath();
    p.drawPath(page);
    p.setBrush(QColor(210, 214, 222));
    QPainterPath fold;
    fold.moveTo(8.5, 0.5); fold.lineTo(8.5, 3.5); fold.lineTo(11.5, 3.5); fold.closeSubpath();
    p.drawPath(fold);
    QPen plus(QColor(46, 160, 90), 2.0);
    plus.setCapStyle(Qt::FlatCap);
    p.setPen(plus);
    p.drawLine(QPointF(12, 8.5), QPointF(12, 15.5));
    p.drawLine(QPointF(8.5, 12), QPointF(15.5, 12));
}

void drawOpenDoc(QPainter& p)
{
    // Carpeta beige con una flecha verde que sale, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(150, 110, 50), 1));
    p.setBrush(QColor(214, 170, 96));
    QPainterPath back;
    back.moveTo(0.5, 5.5); back.lineTo(0.5, 3.5); back.lineTo(5, 3.5); back.lineTo(6.5, 5); back.lineTo(13.5, 5);
    back.lineTo(13.5, 14.5); back.lineTo(0.5, 14.5); back.closeSubpath();
    p.drawPath(back);
    p.setBrush(QColor(240, 196, 120));
    QPainterPath front;
    front.moveTo(0.5, 14.5); front.lineTo(2.5, 7.5); front.lineTo(15.5, 7.5); front.lineTo(13.5, 14.5); front.closeSubpath();
    p.drawPath(front);
    arrowArc(p, QPointF(13.6, 6.6), 4.6, 180, -80, 1.6, QColor(46, 170, 120), QColor(20, 100, 70));
}

void drawSaveDoc(QPainter& p)
{
    // Disquete morado, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(80, 40, 110), 1));
    p.setBrush(QColor(150, 80, 190));
    QPainterPath body;
    body.moveTo(0.5, 0.5); body.lineTo(12.5, 0.5); body.lineTo(15.5, 3.5); body.lineTo(15.5, 15.5); body.lineTo(0.5, 15.5);
    body.closeSubpath();
    p.drawPath(body);
    p.setBrush(QColor(236, 230, 244));
    p.drawRect(QRectF(3.5, 8.5, 9, 7));
    p.setPen(QPen(QColor(150, 130, 170), 0.8));
    p.drawLine(QPointF(5, 11), QPointF(11, 11));
    p.drawLine(QPointF(5, 13), QPointF(11, 13));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(200, 170, 225));
    p.drawRect(QRectF(4, 1, 7, 4));
}

void drawUndo(QPainter& p)
{
    aa(p);
    arrowArc(p, QPointF(8.8, 9.2), 4.6, -40, 215, 2.6, QColor(52, 128, 232), QColor(18, 60, 140));
}

void drawRedo(QPainter& p)
{
    aa(p);
    arrowArc(p, QPointF(7.2, 9.2), 4.6, 220, -215, 2.6, QColor(52, 128, 232), QColor(18, 60, 140));
}

void drawCut(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(60, 60, 60), 1.3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(1, 10, 5, 5);
    p.drawLine(4, 11, 10, 3);
    p.drawEllipse(10, 10, 5, 5);
    p.drawLine(12, 11, 6, 3);
    p.setBrush(QColor(60, 60, 60));
    p.drawEllipse(QPointF(8, 7), 1.2, 1.2);
}

void drawCopy(QPainter& p)
{
    // Dos hojas con la esquina doblada, como en Paint.NET.
    aa(p);
    auto sheet = [&p](qreal x, qreal y, const QColor& fill) {
        QPainterPath page;
        page.moveTo(x, y); page.lineTo(x + 6, y); page.lineTo(x + 9, y + 3); page.lineTo(x + 9, y + 11); page.lineTo(x, y + 11);
        page.closeSubpath();
        p.setPen(QPen(QColor(100, 100, 100), 1));
        p.setBrush(fill);
        p.drawPath(page);
        QPainterPath fold;
        fold.moveTo(x + 6, y); fold.lineTo(x + 6, y + 3); fold.lineTo(x + 9, y + 3); fold.closeSubpath();
        p.setBrush(QColor(200, 204, 212));
        p.drawPath(fold);
    };
    sheet(1.5, 0.5, QColor(226, 229, 235));
    sheet(5.5, 4.5, Qt::white);
}

void drawPaste(QPainter& p)
{
    // Portapapeles beige con una hoja delante, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(150, 110, 50), 1));
    p.setBrush(QColor(224, 178, 104));
    p.drawRoundedRect(QRectF(0.5, 1.5, 11, 13.5), 1, 1);
    p.setBrush(QColor(170, 130, 70));
    p.drawRoundedRect(QRectF(3.5, 0.5, 5, 3), 1, 1);
    QPainterPath page;
    page.moveTo(6.5, 5.5); page.lineTo(12, 5.5); page.lineTo(15.5, 9); page.lineTo(15.5, 15.5); page.lineTo(6.5, 15.5);
    page.closeSubpath();
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(Qt::white);
    p.drawPath(page);
    QPainterPath fold;
    fold.moveTo(12, 5.5); fold.lineTo(12, 9); fold.lineTo(15.5, 9); fold.closeSubpath();
    p.setBrush(QColor(200, 204, 212));
    p.drawPath(fold);
}

void printAction(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(80, 80, 90), 1));
    p.setBrush(QColor(235, 238, 242));
    p.drawRect(4, 2, 8, 4);
    p.setBrush(QColor(120, 130, 145));
    p.drawRoundedRect(2, 6, 12, 5, 1, 1);
    p.setBrush(Qt::white);
    p.drawRect(4, 10, 8, 4);
}

void cropAction(QPainter& p)
{
    // Marcas de recorte con la imagen dentro, como en Paint.NET.
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(150, 156, 166));
    p.drawRect(QRectF(5, 5, 6, 6));
    p.setPen(QPen(QColor(60, 60, 70), 1.4));
    p.drawLine(QPointF(4, 1), QPointF(4, 12));
    p.drawLine(QPointF(1, 12), QPointF(12, 12));
    p.drawLine(QPointF(12, 4), QPointF(12, 15));
    p.drawLine(QPointF(4, 4), QPointF(15, 4));
}

void deselectAction(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(110, 110, 120), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(2, 2, 11, 11);
    p.setPen(QPen(QColor(200, 60, 60), 1.6));
    p.drawLine(5, 5, 11, 11);
    p.drawLine(11, 5, 5, 11);
}

void pixelGridAction(QPainter& p)
{
    p.setPen(QPen(QColor(110, 120, 135), 1));
    for (int i = 1; i <= 13; i += 4) {
        p.drawLine(i, 1, i, 14);
        p.drawLine(1, i, 14, i);
    }
}

void rulersAction(QPainter& p)
{
    // Regla en forma de L, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(120, 90, 40), 1));
    p.setBrush(QColor(236, 196, 120));
    QPainterPath l;
    l.moveTo(0.5, 0.5); l.lineTo(15.5, 0.5); l.lineTo(15.5, 5.5); l.lineTo(5.5, 5.5); l.lineTo(5.5, 15.5); l.lineTo(0.5, 15.5);
    l.closeSubpath();
    p.drawPath(l);
    p.setPen(QPen(QColor(120, 90, 40), 0.9));
    for (int i = 7; i < 15; i += 3) {
        p.drawLine(QPointF(i, 0.5), QPointF(i, 3));
        p.drawLine(QPointF(0.5, i), QPointF(3, i));
    }
}

void toolsWindow(QPainter& p)
{
    // Martillo, como el botón de Herramientas de Paint.NET.
    aa(p);
    p.save();
    p.translate(8, 8);
    p.rotate(45);
    p.setPen(QPen(QColor(90, 70, 50), 0.9));
    p.setBrush(QColor(196, 150, 96));
    p.drawRoundedRect(QRectF(-1.3, -3, 2.6, 11), 1, 1);
    p.setPen(QPen(QColor(60, 64, 72), 0.9));
    p.setBrush(QColor(170, 176, 186));
    p.drawRoundedRect(QRectF(-5.5, -6.5, 11, 4), 1, 1);
    p.restore();
}

void historyWindow(QPainter& p)
{
    // Reloj con flecha de vuelta, como el botón de Historial de Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(70, 120, 185), 1.2));
    p.setBrush(QColor(236, 242, 250));
    p.drawEllipse(QRectF(2.5, 2.5, 12, 12));
    p.setPen(QPen(QColor(40, 70, 120), 1.3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(8.5, 5), QPointF(8.5, 8.5));
    p.drawLine(QPointF(8.5, 8.5), QPointF(11, 10));
    arrowArc(p, QPointF(8.5, 8.5), 7.0, 150, 70, 1.3, QColor(70, 120, 185), QColor(40, 70, 120));
}

void layersWindow(QPainter& p)
{
    // Tres capas apiladas, como el botón de Capas de Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(50, 90, 150), 1));
    for (int i = 0; i < 3; ++i) {
        p.setBrush(QColor(120 + i * 30, 165 + i * 22, 225 + i * 10));
        p.drawRect(QRectF(1.5 + i * 2.5, 1.5 + i * 2.5, 9, 9));
    }
}

void colorsWindow(QPainter& p)
{
    // Rueda de colores en gajos, como el botón de Colores de Paint.NET.
    aa(p);
    const QColor colors[] = {QColor(235, 60, 60), QColor(250, 160, 40), QColor(245, 225, 60), QColor(80, 200, 90),
                             QColor(60, 190, 230), QColor(60, 100, 230), QColor(170, 80, 220), QColor(235, 80, 170)};
    const QRectF r(1.5, 1.5, 13, 13);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 8; ++i) {
        p.setBrush(colors[i]);
        p.drawPie(r, (90 - i * 45) * 16, -45 * 16);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(90, 90, 100), 1));
    p.drawEllipse(r);
}

void settings(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(90, 95, 105), 1.3));
    p.setBrush(QColor(200, 206, 214));
    p.drawEllipse(4, 4, 8, 8);
    p.setPen(QPen(QColor(90, 95, 105), 1.6));
    for (int a = 0; a < 360; a += 45) {
        double r = a * M_PI / 180.0;
        p.drawLine(QPointF(8 + 5.0 * cos(r), 8 + 5.0 * sin(r)),
                   QPointF(8 + 7.0 * cos(r), 8 + 7.0 * sin(r)));
    }
    p.setBrush(Qt::white);
    p.setPen(QPen(QColor(90, 95, 105), 1));
    p.drawEllipse(6, 6, 4, 4);
}

void help(QPainter& p)
{
    // Círculo azul con la interrogación en blanco, como en Paint.NET.
    aa(p);
    p.setPen(QPen(QColor(30, 80, 150), 1));
    p.setBrush(QColor(50, 120, 210));
    p.drawEllipse(QRectF(0.5, 0.5, 15, 15));
    QFont f;
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0.5, 0.5, 15, 15), Qt::AlignCenter, QStringLiteral("?"));
}

// En el tema oscuro, los iconos de línea oscuros se aclaran conservando el tono, como en el
// original (los de herramientas, en color, se quedan como están).
QImage lightened(QImage image)
{
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a == 0)
                continue;
            int h, s, l, al;
            QColor c = QColor::fromRgb(line[x]);
            c.getHsl(&h, &s, &l, &al);
            if (l < 150) {
                c.setHsl(h < 0 ? 0 : h, s, std::clamp(235 - l, 0, 255), a);
                line[x] = c.rgba();
            }
        }
    }
    return image;
}

struct Entry {
    int design;
    void (*paint)(QPainter&);
    bool themed;
};

const QHash<QString, Entry>& table()
{
    static const QHash<QString, Entry> icons{
        {QStringLiteral("rectangleselect"), {TS, drawRectSelect, false}},
        {QStringLiteral("ellipseselect"), {TS, drawEllipseSelect, false}},
        {QStringLiteral("lassoselect"), {TS, drawLasso, false}},
        {QStringLiteral("magicwand"), {TS, drawMagicWand, false}},
        {QStringLiteral("moveselectedpixels"), {TS, drawMove, false}},
        {QStringLiteral("moveselection"), {TS, drawMoveSelection, false}},
        {QStringLiteral("zoom"), {TS, drawZoom, false}},
        {QStringLiteral("pan"), {TS, drawPan, false}},
        {QStringLiteral("paintbucket"), {TS, drawFill, false}},
        {QStringLiteral("gradient"), {TS, drawGradient, false}},
        {QStringLiteral("paintbrush"), {TS, drawBrush, false}},
        {QStringLiteral("eraser"), {TS, drawEraser, false}},
        {QStringLiteral("pencil"), {TS, drawPencil, false}},
        {QStringLiteral("colorpicker"), {TS, drawColorPicker, false}},
        {QStringLiteral("clonestamp"), {TS, drawCloneStamp, false}},
        {QStringLiteral("recolor"), {TS, drawRecolor, false}},
        {QStringLiteral("text"), {TS, drawText, false}},
        {QStringLiteral("linecurve"), {TS, drawLine, false}},
        {QStringLiteral("shapes"), {TS, drawShape, false}},
        {QStringLiteral("document-new"), {16, drawNewDoc, true}},
        {QStringLiteral("document-open"), {16, drawOpenDoc, true}},
        {QStringLiteral("document-save"), {16, drawSaveDoc, true}},
        {QStringLiteral("document-print"), {16, printAction, true}},
        {QStringLiteral("edit-cut"), {16, drawCut, true}},
        {QStringLiteral("edit-copy"), {16, drawCopy, true}},
        {QStringLiteral("edit-paste"), {16, drawPaste, true}},
        {QStringLiteral("edit-undo"), {16, drawUndo, false}},
        {QStringLiteral("edit-redo"), {16, drawRedo, false}},
        {QStringLiteral("transform-crop"), {16, cropAction, true}},
        {QStringLiteral("edit-select-none"), {16, deselectAction, true}},
        {QStringLiteral("view-grid"), {16, pixelGridAction, true}},
        {QStringLiteral("measure"), {16, rulersAction, true}},
        {QStringLiteral("window-tools"), {16, toolsWindow, true}},
        {QStringLiteral("window-history"), {16, historyWindow, true}},
        {QStringLiteral("window-layers"), {16, layersWindow, true}},
        {QStringLiteral("window-colors"), {16, colorsWindow, true}},
        {QStringLiteral("configure"), {16, settings, true}},
        {QStringLiteral("help-contents"), {16, help, true}},
    };
    return icons;
}

// Se dibuja cada vez al tamaño pedido (nítido en cualquier escala y con el tema actual).
class PdnIconEngine : public QIconEngine {
public:
    explicit PdnIconEngine(const Entry& entry)
        : m_entry(entry)
    {
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        const qreal dpr = std::max<qreal>(1.0, scale);
        QImage image((QSizeF(size) * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return {};
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            const qreal side = std::min(image.width(), image.height());
            p.translate((image.width() - side) / 2, (image.height() - side) / 2);
            p.scale(side / m_entry.design, side / m_entry.design);
            m_entry.paint(p);
        }
        if (m_entry.themed && Theme::isDark())
            image = lightened(image);
        QPixmap pm = QPixmap::fromImage(image);
        pm.setDevicePixelRatio(dpr);
        if (mode == QIcon::Disabled || mode == QIcon::Selected) {
            QStyleOption opt;
            opt.palette = QApplication::palette();
            const QPixmap generated = QApplication::style()->generatedIconPixmap(mode, pm, &opt);
            if (!generated.isNull())
                return generated;
        }
        return pm;
    }

    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
    QIconEngine* clone() const override { return new PdnIconEngine(m_entry); }
    QString key() const override { return QStringLiteral("KaintPdn"); }

private:
    Entry m_entry;
};

} // namespace

void setActiveColors(const QColor& primary, const QColor& secondary)
{
    g_primary = primary;
    g_secondary = secondary;
}

QIcon pdn(const QString& name)
{
    const auto it = table().constFind(name);
    return it == table().constEnd() ? QIcon() : QIcon(new PdnIconEngine(*it));
}

} // namespace Icons
