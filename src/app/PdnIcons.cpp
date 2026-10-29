#include "app/Icons.h"

#include "app/IconPacks.h"
#include "app/SystemCursors.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOption>
#include <QtMath>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Iconos de las herramientas dibujados por código imitando los de Paint.NET (el pincel, el cubo
// de pintura, cambiar color y el degradado llevan los colores activos) y la mano, abierta y
// agarrando. Cada uno se dibuja en una rejilla de 20 px y se escala a cualquier tamaño. Los
// demás iconos son los PNG de la interfaz (PngIcons.cpp). Un paquete de iconos que traiga el
// archivo de paint.js de una herramienta la sustituye.
namespace Icons {

namespace {

constexpr int TS = 20;

// Colores activos para las partes que se tiñen (inválidos: los colores propios del icono).
QColor g_primary;
QColor g_secondary;
// Cursores con miniatura: la flecha (o la I) del tema de cursores del sistema o la de Kaint.
bool g_systemPointer = true;

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

void drawPanGrab(QPainter& p)
{
    // La misma mano agarrando, vista por el dorso: los dedos se doblan hacia dentro (solo se ven
    // los nudillos) y el pulgar queda recogido detrás, asomando por la izquierda.
    aa(p);
    const QColor outline(110, 66, 28);
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QTransform thumb;
    thumb.translate(6.5, 14.5);
    thumb.rotate(-60);
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(QRectF(-3.5, -1.6, 6.5, 3.2), 1.6, 1.6);
    p.setBrush(QColor(226, 168, 98));
    p.drawPath(thumb.map(thumbPath));

    QPainterPath hand;
    hand.setFillRule(Qt::WindingFill);
    hand.addRoundedRect(QRectF(5.5, 7, 3, 5), 1.5, 1.5);
    hand.addRoundedRect(QRectF(8.5, 6, 3, 6), 1.5, 1.5);
    hand.addRoundedRect(QRectF(11.5, 6, 3, 6), 1.5, 1.5);
    hand.addRoundedRect(QRectF(14.5, 7, 3, 5), 1.5, 1.5);
    hand.addRoundedRect(QRectF(5.5, 8.5, 12, 9.5), 3, 3);
    hand = hand.simplified();
    QLinearGradient skin(0, 5, 0, 18);
    skin.setColorAt(0, QColor(246, 206, 148));
    skin.setColorAt(1, QColor(222, 160, 88));
    p.setBrush(skin);
    p.drawPath(hand);
    // Separaciones entre los nudillos.
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(8.5, 8.5), QPointF(8.5, 10.5));
    p.drawLine(QPointF(11.5, 7.5), QPointF(11.5, 10.5));
    p.drawLine(QPointF(14.5, 8.5), QPointF(14.5, 10.5));
}

// Herramientas sin los colores activos, con los dibujos de siempre de Kaint.

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

// Nombre de Kaint → dibujo, rejilla en la que se dibuja y archivo de paint.js con el que lo puede
// sustituir un paquete de iconos.
struct Entry {
    int design;
    void (*paint)(QPainter&);
    const char* packFile;
};

const QHash<QString, Entry>& table()
{
    static const QHash<QString, Entry> icons{
        {QStringLiteral("rectangleselect"), {TS, drawRectSelect, "rectangle_select_tool_icon"}},
        {QStringLiteral("ellipseselect"), {TS, drawEllipseSelect, "ellipse_select_tool_icon"}},
        {QStringLiteral("lassoselect"), {TS, drawLasso, "lasso_select_tool_icon"}},
        {QStringLiteral("magicwand"), {TS, drawMagicWand, "magic_wand_tool_icon"}},
        {QStringLiteral("moveselectedpixels"), {TS, drawMove, "move_tool_icon"}},
        {QStringLiteral("moveselection"), {TS, drawMoveSelection, "move_selection_tool_icon"}},
        {QStringLiteral("zoom"), {TS, drawZoom, "zoom_tool_icon"}},
        {QStringLiteral("pan"), {TS, drawPan, "pan_tool_icon"}},
        {QStringLiteral("pan-grab"), {TS, drawPanGrab, ""}},
        {QStringLiteral("paintbucket"), {TS, drawFill, "paint_bucket_icon"}},
        {QStringLiteral("gradient"), {TS, drawGradient, "gradient_tool_icon"}},
        {QStringLiteral("paintbrush"), {TS, drawBrush, "paint_brush_tool_icon"}},
        {QStringLiteral("eraser"), {TS, drawEraser, "eraser_tool_icon"}},
        {QStringLiteral("pencil"), {TS, drawPencil, "pencil_tool_icon"}},
        {QStringLiteral("colorpicker"), {TS, drawColorPicker, "color_picker_tool_icon"}},
        {QStringLiteral("clonestamp"), {TS, drawCloneStamp, "clone_stamp_tool_icon"}},
        {QStringLiteral("recolor"), {TS, drawRecolor, "recoloring_tool_icon"}},
        {QStringLiteral("text"), {TS, drawText, "text_tool_icon"}},
        {QStringLiteral("linecurve"), {TS, drawLine, "line_tool_icon"}},
        {QStringLiteral("shapes"), {TS, drawShape, "shapes_tool_icon"}},
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
            const QString packed = IconPacks::file(QStringLiteral("icons"), QLatin1String(m_entry.packFile));
            const QImage source = packed.isEmpty() ? QImage() : QImage(packed);
            if (!source.isNull()) {
                // La del paquete de iconos, centrada y sin deformar.
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                const QImage scaled = source.scaled(image.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawImage(QPoint((image.width() - scaled.width()) / 2, (image.height() - scaled.height()) / 2), scaled);
            } else {
                const qreal side = std::min(image.width(), image.height());
                p.translate((image.width() - side) / 2, (image.height() - side) / 2);
                p.scale(side / m_entry.design, side / m_entry.design);
                m_entry.paint(p);
            }
        }
        QPixmap pm = QPixmap::fromImage(image);
        pm.setDevicePixelRatio(dpr);
        if (mode == QIcon::Disabled) {
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
    if (it != table().constEnd())
        return QIcon(new PdnIconEngine(*it));
    return named(name);
}

void paintUnsavedStar(QPainter& p, const QRectF& r)
{
    // Ocho rayos desde el centro con un contorno blanco y un punto blanco en medio, dibujados en
    // una rejilla de 32 px.
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal scale = std::min(r.width(), r.height()) / 32.0;
    const QPointF c = r.center();
    const qreal radius = std::min(r.width(), r.height()) / 2.0;
    QPainterPath rays;
    for (int i = 0; i < 8; ++i) {
        const qreal a = i * 2 * M_PI / 8;
        rays.moveTo(c);
        rays.lineTo(c + QPointF(std::sin(a), std::cos(a)) * radius);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::white, 8 * scale, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(rays);
    p.setPen(QPen(QColor(0xff, 0xa5, 0x00), 3 * scale, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(rays);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(c, 3 * scale, 3 * scale);
    p.restore();
}

int cursorSize()
{
    static const int size = [] {
        bool ok = false;
        const int env = qEnvironmentVariableIntValue("XCURSOR_SIZE", &ok);
        if (ok && env >= 16 && env <= 256)
            return env;
#ifdef Q_OS_WIN
        const qreal dpr = std::max<qreal>(1.0, qApp ? qApp->devicePixelRatio() : 1.0);
        return std::max(16, int(std::lround(GetSystemMetrics(SM_CXCURSOR) / dpr)));
#else
        return 24;
#endif
    }();
    return size;
}

QCursor toolCursor(const QString& name, Qt::CursorShape fallback)
{
    // Punta de cada icono en su rejilla de 20 px (la del lápiz, la del pincel, el chorro del cubo...).
    static const QHash<QString, QPointF> tips{
        {QStringLiteral("rectangleselect"), {3, 3}},      {QStringLiteral("ellipseselect"), {3, 3}},
        {QStringLiteral("lassoselect"), {12.2, 18.6}},    {QStringLiteral("magicwand"), {15.5, 4}},
        {QStringLiteral("moveselectedpixels"), {2, 1}},   {QStringLiteral("moveselection"), {2, 1}},
        {QStringLiteral("zoom"), {8, 8}},                 {QStringLiteral("paintbucket"), {4, 18.6}},
        {QStringLiteral("gradient"), {2.4, 2.4}},         {QStringLiteral("paintbrush"), {2.4, 18.4}},
        {QStringLiteral("eraser"), {3.1, 14.8}},          {QStringLiteral("pencil"), {2, 18}},
        {QStringLiteral("colorpicker"), {2.2, 17.8}},     {QStringLiteral("clonestamp"), {10, 17.2}},
        {QStringLiteral("recolor"), {6, 14}},             {QStringLiteral("linecurve"), {3.4, 16.4}},
        {QStringLiteral("shapes"), {1.6, 2.8}},
    };
    const QIcon icon = pdn(name);
    if (icon.isNull())
        return QCursor(fallback);
    // Como el puntero del sistema: el dibujo ocupa unos 5/6 del tamaño del cursor (20 px con 24).
    const int side = std::max(16, cursorSize() * 5 / 6);
    const qreal dpr = std::max<qreal>(1.0, qApp ? qApp->devicePixelRatio() : 1.0);
    const QString key = QStringLiteral("%1/%2/%3/%4/%5/%6")
                            .arg(name)
                            .arg(side)
                            .arg(dpr)
                            .arg(g_primary.rgba())
                            .arg(g_secondary.rgba())
                            .arg(IconPacks::generation());
    static QHash<QString, QCursor> cache;
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;
    QPixmap pm = icon.pixmap(QSize(side, side), dpr);
    pm.setDevicePixelRatio(dpr);
    const QPointF tip = tips.value(name, QPointF(10, 10)) * side / 20.0;
    const QCursor cursor(pm, int(std::lround(tip.x())), int(std::lround(tip.y())));
    if (cache.size() > 256)
        cache.clear();
    cache.insert(key, cursor);
    return cursor;
}

void setSystemPointer(bool on)
{
    g_systemPointer = on;
}

namespace {

// La flecha (o la I) del tema de cursores del sistema con la miniatura al lado, colocada respecto
// a lo que se ve del puntero como la de Kaint (abajo a la derecha de la flecha); nulo si el
// sistema no tiene un tema que leer.
QCursor systemMiniCursor(const std::function<void(QPainter&, const QRectF&)>& paint, bool ibeam, qreal dpr)
{
    const SystemCursors::Image sys = SystemCursors::load(ibeam ? SystemCursors::Shape::IBeam : SystemCursors::Shape::Arrow,
                                                         int(std::lround(cursorSize() * dpr)));
    if (sys.image.isNull())
        return {};
    // Parte visible del puntero (los temas dejan márgenes transparentes).
    const QImage& src = sys.image;
    QRect shape;
    for (int y = 0; y < src.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x)
            if (qAlpha(line[x]) > 32)
                shape |= QRect(x, y, 1, 1);
    }
    if (shape.isEmpty())
        return {};
    const QRectF visible(shape.left() / dpr, shape.top() / dpr, shape.width() / dpr, shape.height() / dpr);
    // En la rejilla de Kaint: miniatura de 14 px junto a una flecha de 18 de alto (o una I de 14),
    // a la derecha y algo por debajo de la mitad.
    const qreal side = std::round(visible.height() * (ibeam ? 14.0 / 14.0 : 14.0 / 18.0));
    const QPointF at = ibeam ? QPointF(visible.right() + 2, visible.top() + std::round(visible.height() * 10.0 / 14.0))
                             : QPointF(visible.right() + 1, visible.top() + std::round(visible.height() * 13.0 / 18.0));
    const QSizeF logical(std::max(src.width() / dpr, at.x() + side), std::max(src.height() / dpr, at.y() + side));
    QImage image((logical * dpr).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QImage pointer = src;
        pointer.setDevicePixelRatio(dpr);
        p.drawImage(QPointF(0, 0), pointer);
        paint(p, QRectF(at, QSizeF(side, side)));
    }
    return QCursor(QPixmap::fromImage(image), int(std::lround(sys.hotSpot.x() / dpr)), int(std::lround(sys.hotSpot.y() / dpr)));
}

} // namespace

QCursor miniCursor(const QString& key, const std::function<void(QPainter&, const QRectF&)>& paint, bool ibeam)
{
    // Rejilla de 28 px (con el cursor de 32 px de Windows): flecha de 12×19 y miniatura de 14.
    const qreal scale = std::max(1.0, cursorSize() / 32.0);
    const qreal dpr = std::max<qreal>(1.0, qApp ? qApp->devicePixelRatio() : 1.0);
    const QString cacheKey = QStringLiteral("%1/%2/%3/%4/%5/%6/%7/%8")
                                 .arg(key)
                                 .arg(ibeam)
                                 .arg(scale)
                                 .arg(dpr)
                                 .arg(g_primary.rgba())
                                 .arg(g_secondary.rgba())
                                 .arg(IconPacks::generation())
                                 .arg(g_systemPointer);
    static QHash<QString, QCursor> cache;
    if (const auto it = cache.constFind(cacheKey); it != cache.cend())
        return *it;
    if (g_systemPointer) {
        const QCursor cursor = systemMiniCursor(paint, ibeam, dpr);
        if (!cursor.pixmap().isNull()) {
            if (cache.size() > 256)
                cache.clear();
            cache.insert(cacheKey, cursor);
            return cursor;
        }
    }
    const int side = int(std::ceil(28 * scale));
    QImage image(QSize(side, side) * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(scale, scale);
        p.save();
        p.translate(0.5, 0.5);
        if (ibeam) {
            QPainterPath path;
            path.moveTo(1, 1);
            path.quadTo(4, 1, 4, 3);
            path.quadTo(4, 1, 7, 1);
            path.moveTo(4, 3);
            path.lineTo(4, 13);
            path.moveTo(1, 15);
            path.quadTo(4, 15, 4, 13);
            path.quadTo(4, 15, 7, 15);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
            p.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        } else {
            static const QPointF arrow[] = {{0, 0}, {0, 16}, {4, 12}, {7, 18}, {9, 17}, {6, 11}, {11, 11}};
            p.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
            p.setBrush(Qt::white);
            p.drawPolygon(arrow, int(std::size(arrow)));
        }
        p.restore();
        paint(p, ibeam ? QRectF(9, 11, 14, 14) : QRectF(12, 13, 14, 14));
    }
    const QPointF hot = ibeam ? QPointF(4, 8) : QPointF(0, 0);
    const QCursor cursor(QPixmap::fromImage(image), int(std::lround(hot.x() * scale)), int(std::lround(hot.y() * scale)));
    if (cache.size() > 256)
        cache.clear();
    cache.insert(cacheKey, cursor);
    return cursor;
}

QCursor miniToolCursor(const QString& name, bool ibeam)
{
    const QIcon icon = pdn(name);
    return miniCursor(QStringLiteral("tool/") + name, [&icon](QPainter& p, const QRectF& r) {
        // A la resolución final (escala del cursor y de la pantalla), para que se vea nítido.
        const qreal k = p.transform().m11() * p.device()->devicePixelRatio();
        const QPixmap pm = icon.pixmap(QSize(int(std::lround(r.width() * k)), int(std::lround(r.height() * k))), 1.0);
        p.drawPixmap(r, pm, QRectF(pm.rect()));
    }, ibeam);
}

QCursor rotateCursor(qreal angle)
{
    // La flecha de girar de paint.js: un trozo de circunferencia de 32° con una punta en cada
    // extremo, que pasa por el puntero y se curva alrededor del centro del giro.
    const int steps = 72;
    const int index = ((int(std::lround(angle / (2 * M_PI) * steps)) % steps) + steps) % steps;
    static QHash<int, QCursor> cache;
    if (const auto it = cache.constFind(index); it != cache.cend())
        return *it;
    const qreal a = index * 2 * M_PI / steps;
    const qreal scale = cursorSize() / 24.0;
    const int side = int(std::ceil(40 * scale));
    const qreal dpr = std::max<qreal>(1.0, qApp ? qApp->devicePixelRatio() : 1.0);
    QImage image(QSize(side, side) * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(side / 2.0, side / 2.0);
        p.scale(scale, scale);
        const qreal radius = 50, halfArc = qDegreesToRadians(16.0);
        const QPointF center(std::cos(a) * radius, std::sin(a) * radius);
        const qreal middle = a + M_PI, start = middle - halfArc, end = middle + halfArc;
        auto onArc = [&](qreal t) { return center + QPointF(std::cos(t), std::sin(t)) * radius; };
        QPainterPath path;
        const int segments = 12;
        for (int i = 0; i <= segments; ++i) {
            const QPointF q = onArc(start + (end - start) * i / segments);
            i ? path.lineTo(q) : path.moveTo(q);
        }
        auto arrow = [&](qreal t, qreal direction) {
            const QPointF tip = onArc(t);
            const QPointF tangent(direction * -std::sin(t), direction * std::cos(t));
            const QPointF normal(-tangent.y(), tangent.x());
            const QPointF base = tip - tangent * 4;
            path.moveTo(base + normal * 2.25);
            path.lineTo(tip);
            path.lineTo(base - normal * 2.25);
        };
        arrow(start, -1);
        arrow(end, 1);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
    const QCursor cursor(QPixmap::fromImage(image), side / 2, side / 2);
    cache.insert(index, cursor);
    return cursor;
}

QCursor handCursor(bool grabbing)
{
    // La del paquete de iconos (los cursores de la mano de paint.js) o la dibujada.
    const QString packed = IconPacks::file(QStringLiteral("cursors"), grabbing ? QStringLiteral("hand_closed_cursor")
                                                                                : QStringLiteral("hand_open_cursor"));
    if (const QImage image(packed); !packed.isEmpty() && !image.isNull()) {
        const QPixmap pm = QPixmap::fromImage(image.scaled(QSize(24, 24), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        return QCursor(pm, pm.width() / 2, pm.height() / 2);
    }
    const QPixmap pm = pdn(grabbing ? QStringLiteral("pan-grab") : QStringLiteral("pan")).pixmap(QSize(24, 24));
    return QCursor(pm, 12, 12);
}

} // namespace Icons
