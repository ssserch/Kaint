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

// Iconos al estilo Paint.NET dibujados por código, adaptados de paint.software
// (https://github.com/Univers4craft/paint.software, licencia MIT, © Univers4craft). Cada uno se
// dibuja en una rejilla de 20 px (herramientas) o 16 px (barras y ventanas) y se escala a
// cualquier tamaño.
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
    aa(p);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(137, 190, 233));
    p.drawRect(4, 4, 12, 12);
    marchingAnts(p);
    p.drawRect(4, 4, 12, 12);
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
    aa(p);
    QLinearGradient bg(16, 2, 7, 12);
    bg.setColorAt(0.0, QColor(255, 208, 88));
    bg.setColorAt(1.0, QColor(236, 158, 40));
    p.setPen(QPen(QColor(122, 78, 20), 0.9));
    p.setBrush(bg);
    QPainterPath body;
    body.moveTo(16.2, 1.4); body.lineTo(18.2, 3.4); body.lineTo(7.6, 13.6); body.lineTo(5.6, 11.6);
    body.closeSubpath();
    p.drawPath(body);
    p.setBrush(QColor(228, 192, 142));
    QPainterPath collar;
    collar.moveTo(7.6, 13.6); collar.lineTo(5.6, 11.6); collar.lineTo(4.2, 13.0); collar.lineTo(6.2, 15.0);
    collar.closeSubpath();
    p.drawPath(collar);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(56, 56, 62));
    QPainterPath tip;
    tip.moveTo(6.2, 15.0); tip.lineTo(4.2, 13.0); tip.lineTo(1.8, 18.4); tip.closeSubpath();
    p.drawPath(tip);
}

void drawEraser(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(88, 42, 100), 1.0));
    p.setBrush(QColor(212, 130, 220));
    QPainterPath top;
    top.moveTo(12.8, 2.4); top.lineTo(18.0, 7.6); top.lineTo(10.4, 13.4); top.lineTo(5.2, 8.2);
    top.closeSubpath();
    p.drawPath(top);
    p.setBrush(QColor(150, 72, 168));
    QPainterPath front;
    front.moveTo(5.2, 8.2); front.lineTo(10.4, 13.4); front.lineTo(10.4, 17.2); front.lineTo(5.2, 12.0);
    front.closeSubpath();
    p.drawPath(front);
    p.setBrush(QColor(180, 100, 196));
    QPainterPath side;
    side.moveTo(10.4, 13.4); side.lineTo(18.0, 7.6); side.lineTo(18.0, 11.0); side.lineTo(10.4, 17.2);
    side.closeSubpath();
    p.drawPath(side);
}

void drawFill(QPainter& p)
{
    // Cubo metálico inclinado del que se derrama pintura del color principal, como el de Paint.NET.
    aa(p);
    const QColor color = tint(g_primary, QColor(40, 110, 215));
    QLinearGradient paint(0, 8, 0, 18);
    paint.setColorAt(0, mix(color, Qt::white, 0.3));
    paint.setColorAt(1, color);
    QPainterPath drip;
    drip.moveTo(4.2, 7.6);
    drip.cubicTo(2.0, 10.6, 1.0, 13.6, 2.2, 16.0);
    drip.cubicTo(3.2, 18.2, 6.4, 17.8, 6.4, 15.4);
    drip.cubicTo(6.4, 13.4, 5.2, 11.6, 6.6, 9.2);
    drip.closeSubpath();
    p.setPen(QPen(outlineFor(color), 0.8));
    p.setBrush(paint);
    p.drawPath(drip);

    p.save();
    p.translate(11.5, 10.5);
    p.rotate(-40);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(70, 78, 90), 1.3));
    p.drawArc(QRectF(-5.0, -10.5, 10.0, 11.0), 20 * 16, 140 * 16);
    QLinearGradient body(-5.5, 0, 5.5, 0);
    body.setColorAt(0, QColor(232, 236, 241));
    body.setColorAt(1, QColor(128, 138, 152));
    QPainterPath bucket;
    bucket.moveTo(-5.6, -4.2);
    bucket.lineTo(5.6, -4.2);
    bucket.lineTo(4.2, 5.4);
    bucket.quadTo(0, 6.8, -4.2, 5.4);
    bucket.closeSubpath();
    p.setPen(QPen(QColor(64, 72, 84), 0.9));
    p.setBrush(body);
    p.drawPath(bucket);
    p.setBrush(QColor(242, 245, 248));
    p.drawEllipse(QRectF(-5.6, -5.8, 11.2, 3.2));
    p.setPen(Qt::NoPen);
    p.setBrush(paint);
    p.drawEllipse(QRectF(-4.4, -5.2, 8.8, 2.0));
    p.restore();
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
    aa(p);
    QFont font;
    font.setPixelSize(18);
    font.setBold(true);
    font.setFamily("Serif");
    p.setFont(font);
    p.setPen(QColor(146, 162, 186));
    p.drawText(QRect(0, 0, TS, TS), Qt::AlignCenter, "T");
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
    aa(p);
    QPen hp(QColor(78, 88, 104), 2.7);
    hp.setCapStyle(Qt::RoundCap);
    p.setPen(hp);
    p.drawLine(QPointF(12.4, 12.4), QPointF(17.6, 17.6));
    QRadialGradient lg(7.2, 6.6, 7.0);
    lg.setColorAt(0.0, QColor(212, 236, 252));
    lg.setColorAt(1.0, QColor(118, 176, 226));
    p.setPen(QPen(QColor(36, 72, 118), 1.6));
    p.setBrush(lg);
    p.drawEllipse(QRectF(2.2, 2.2, 11.6, 11.6));
    QPen pp(QColor(36, 72, 118), 1.5);
    pp.setCapStyle(Qt::RoundCap);
    p.setPen(pp);
    p.drawLine(QPointF(8.0, 4.9), QPointF(8.0, 11.1));
    p.drawLine(QPointF(4.9, 8.0), QPointF(11.1, 8.0));
}

void drawPan(QPainter& p)
{
    aa(p);
    const QColor ink(150, 98, 54);
    const QColor skin(243, 208, 168);
    p.setPen(QPen(ink, 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(skin);

    QPainterPath hand;
    hand.addRoundedRect(QRectF(5.5, 8.5, 9.0, 9.5), 3.0, 3.0);
    p.drawPath(hand);

    p.setBrush(skin);
    p.drawRoundedRect(QRectF(6.2, 3.2, 2.0, 7.0), 1.0, 1.0);
    p.drawRoundedRect(QRectF(8.6, 2.4, 2.0, 8.0), 1.0, 1.0);
    p.drawRoundedRect(QRectF(11.0, 3.0, 2.0, 7.4), 1.0, 1.0);
    p.drawRoundedRect(QRectF(13.2, 4.6, 2.0, 6.0), 1.0, 1.0);
    p.drawRoundedRect(QRectF(3.2, 9.0, 3.4, 2.1), 1.0, 1.0);
}

void drawNewDoc(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(Qt::white);
    QPainterPath page;
    page.moveTo(3, 1); page.lineTo(10, 1); page.lineTo(13, 4); page.lineTo(13, 15); page.lineTo(3, 15); page.closeSubpath();
    p.drawPath(page);
    p.setBrush(QColor(200, 210, 230));
    QPainterPath fold;
    fold.moveTo(10, 1); fold.lineTo(10, 4); fold.lineTo(13, 4); fold.closeSubpath();
    p.drawPath(fold);
}

void drawOpenDoc(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(150, 120, 30), 1));
    p.setBrush(QColor(230, 190, 60));
    p.drawRoundedRect(1, 3, 14, 11, 1, 1);
    p.drawRoundedRect(1, 1, 6, 3, 1, 1);
    p.setBrush(QColor(250, 215, 80));
    QPainterPath front;
    front.moveTo(1, 7); front.lineTo(3, 14); front.lineTo(15, 14); front.lineTo(15, 7); front.closeSubpath();
    p.drawPath(front);
}

void drawSaveDoc(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(30, 60, 120), 1));
    p.setBrush(QColor(70, 130, 210));
    p.drawRoundedRect(1, 1, 14, 14, 1, 1);
    p.setBrush(QColor(180, 185, 195));
    p.drawRect(4, 1, 8, 5);
    p.setBrush(QColor(70, 130, 210));
    p.drawRect(9, 1, 2, 4);
    p.setBrush(Qt::white);
    p.drawRect(3, 9, 10, 5);
    p.setPen(QPen(QColor(150, 150, 150), 0.5));
    p.drawLine(5, 11, 11, 11);
    p.drawLine(5, 13, 11, 13);
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
    aa(p);
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(QColor(220, 225, 235));
    p.drawRect(4, 1, 11, 11);
    p.setBrush(Qt::white);
    p.drawRect(1, 4, 11, 11);
    p.setPen(QPen(QColor(170, 170, 170), 0.7));
    p.drawLine(3, 8, 10, 8);
    p.drawLine(3, 10, 10, 10);
    p.drawLine(3, 12, 8, 12);
}

void drawPaste(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(140, 110, 50), 1));
    p.setBrush(QColor(210, 180, 100));
    p.drawRoundedRect(1, 2, 11, 13, 1, 1);
    p.setBrush(QColor(160, 140, 80));
    p.drawRoundedRect(4, 1, 5, 3, 1, 1);
    p.setPen(QPen(QColor(100, 100, 100), 0.8));
    p.setBrush(Qt::white);
    p.drawRect(3, 5, 8, 8);
    p.setPen(QPen(QColor(170, 170, 170), 0.7));
    p.drawLine(5, 7, 9, 7);
    p.drawLine(5, 9, 9, 9);
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
    aa(p);
    p.setPen(QPen(QColor(60, 60, 70), 1.4));
    p.drawLine(4, 1, 4, 12);
    p.drawLine(1, 11, 12, 11);
    p.setPen(QPen(QColor(60, 60, 70), 1.4, Qt::DashLine));
    p.drawLine(11, 4, 11, 15);
    p.drawLine(4, 4, 15, 4);
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
    aa(p);
    p.setPen(QPen(QColor(90, 90, 100), 1));
    p.setBrush(QColor(250, 244, 200));
    p.drawRect(1, 5, 14, 6);
    p.setPen(QPen(QColor(90, 90, 100), 1));
    for (int x = 3; x < 15; x += 3) p.drawLine(x, 5, x, 8);
}

void toolsWindow(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(70, 120, 185), 1.2));
    p.setBrush(QColor(70, 120, 185));
    p.drawRect(2, 11, 12, 2);
    p.drawRect(7, 3, 2, 8);
    p.drawRect(5, 2, 6, 2);
}

void historyWindow(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(70, 120, 185), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(2, 2, 12, 12);
    p.drawLine(8, 4, 8, 8);
    p.drawLine(8, 8, 11, 9);
}

void layersWindow(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(70, 120, 185), 1));
    p.setBrush(QColor(180, 210, 235));
    p.drawRect(3, 3, 9, 6);
    p.setBrush(QColor(140, 185, 220));
    p.drawRect(5, 7, 9, 6);
}

void colorsWindow(QPainter& p)
{
    aa(p);
    QLinearGradient g(2, 2, 14, 14);
    g.setColorAt(0.0, QColor(255, 80, 80));
    g.setColorAt(0.2, QColor(255, 210, 80));
    g.setColorAt(0.4, QColor(80, 220, 80));
    g.setColorAt(0.6, QColor(80, 220, 220));
    g.setColorAt(0.8, QColor(80, 120, 255));
    g.setColorAt(1.0, QColor(200, 80, 255));
    p.setPen(QPen(QColor(90, 90, 100), 1));
    p.setBrush(g);
    p.drawEllipse(2, 2, 12, 12);
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
    aa(p);
    p.setPen(QPen(QColor(70, 120, 185), 1.3));
    p.setBrush(QColor(225, 238, 250));
    p.drawEllipse(1, 1, 14, 14);
    QFont f("Sans", 9, QFont::Bold);
    p.setFont(f);
    p.setPen(QColor(40, 90, 160));
    p.drawText(QRect(1, 1, 14, 14), Qt::AlignCenter, "?");
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
