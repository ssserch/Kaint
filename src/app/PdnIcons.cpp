#include "app/Icons.h"

#include "app/Theme.h"

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

// Iconos dibujados por código imitando los de Paint.NET: los de las herramientas que llevan los
// colores activos (pincel, cubo de pintura, cambiar color y degradado), la mano, abierta y
// agarrando, y los de la barra principal. Cada uno se dibuja en una rejilla de 20 px
// (herramientas) o 16 px (barra) y se escala a cualquier tamaño. Los demás iconos son los PNG de
// la interfaz (PngIcons.cpp).
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

// Iconos de la barra principal (Nuevo, Abrir, Guardar...), dibujados como los de Paint.NET; los
// menús llevan los PNG.

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
    // Tijeras abiertas hacia arriba, como en Paint.NET: hojas grises en V que se cruzan en el
    // eje y mangos azules con dos anillas abajo.
    aa(p);
    const QColor blade(112, 112, 112), handle(60, 118, 178);
    p.setBrush(blade);
    p.setPen(Qt::NoPen);
    QPainterPath left;
    left.moveTo(1.9, 0.2); left.lineTo(3.6, 0.2); left.lineTo(9.2, 8.4); left.lineTo(7.4, 9.6); left.closeSubpath();
    p.drawPath(left);
    QPainterPath right;
    right.moveTo(14.1, 0.2); right.lineTo(12.4, 0.2); right.lineTo(6.8, 8.4); right.lineTo(8.6, 9.6); right.closeSubpath();
    p.drawPath(right);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(handle, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawEllipse(QPointF(3.5, 12.8), 2.4, 2.2);
    p.drawEllipse(QPointF(12.5, 12.8), 2.4, 2.2);
    p.setPen(QPen(handle, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(7.4, 9.6), QPointF(5.7, 12.6));
    p.drawLine(QPointF(8.6, 9.6), QPointF(10.3, 12.6));
    p.setPen(Qt::NoPen);
    p.setBrush(handle);
    p.drawRoundedRect(QRectF(6.3, 9.0, 3.4, 2.4), 0.8, 0.8);
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
    bool themed = false;
};

const QHash<QString, Entry>& table()
{
    static const QHash<QString, Entry> icons{
        {QStringLiteral("pan"), {TS, drawPan}},
        {QStringLiteral("pan-grab"), {TS, drawPanGrab}},
        {QStringLiteral("paintbucket"), {TS, drawFill}},
        {QStringLiteral("gradient"), {TS, drawGradient}},
        {QStringLiteral("paintbrush"), {TS, drawBrush}},
        {QStringLiteral("recolor"), {TS, drawRecolor}},
        {QStringLiteral("toolbar-document-new"), {16, drawNewDoc, true}},
        {QStringLiteral("toolbar-document-open"), {16, drawOpenDoc, true}},
        {QStringLiteral("toolbar-document-save"), {16, drawSaveDoc, true}},
        {QStringLiteral("toolbar-document-print"), {16, printAction, true}},
        {QStringLiteral("toolbar-edit-cut"), {16, drawCut, true}},
        {QStringLiteral("toolbar-edit-copy"), {16, drawCopy, true}},
        {QStringLiteral("toolbar-edit-paste"), {16, drawPaste, true}},
        {QStringLiteral("toolbar-transform-crop"), {16, cropAction, true}},
        {QStringLiteral("toolbar-edit-select-none"), {16, deselectAction, true}},
        {QStringLiteral("toolbar-edit-undo"), {16, drawUndo, false}},
        {QStringLiteral("toolbar-edit-redo"), {16, drawRedo, false}},
        {QStringLiteral("toolbar-view-grid"), {16, pixelGridAction, true}},
        {QStringLiteral("toolbar-measure"), {16, rulersAction, true}},
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
    if (QIcon icon = named(name); !icon.isNull())
        return icon;
    const auto it = table().constFind(name);
    return it == table().constEnd() ? QIcon() : QIcon(new PdnIconEngine(*it));
}

QCursor handCursor(bool grabbing)
{
    const QPixmap pm = pdn(grabbing ? QStringLiteral("pan-grab") : QStringLiteral("pan")).pixmap(QSize(24, 24));
    return QCursor(pm, 12, 12);
}

} // namespace Icons
