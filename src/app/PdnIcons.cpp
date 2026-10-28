#include "app/Icons.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <cmath>

// Iconos dibujados por código imitando los de Paint.NET: los de las herramientas que llevan los
// colores activos (pincel, cubo de pintura, cambiar color y degradado) y la mano, abierta y
// agarrando. Cada uno se dibuja en una rejilla de 20 px y se escala a cualquier tamaño. Los
// demás iconos son los PNG de la interfaz (PngIcons.cpp).
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
    // La misma mano agarrando: los dedos doblados (solo asoman los nudillos) y el pulgar
    // recogido delante de la palma.
    aa(p);
    QPainterPath hand;
    hand.setFillRule(Qt::WindingFill);
    hand.addRoundedRect(QRectF(5.5, 6.5, 3, 6), 1.5, 1.5);
    hand.addRoundedRect(QRectF(8.5, 5.5, 3, 7), 1.5, 1.5);
    hand.addRoundedRect(QRectF(11.5, 5.5, 3, 7), 1.5, 1.5);
    hand.addRoundedRect(QRectF(14.5, 6.5, 3, 6), 1.5, 1.5);
    hand.addRoundedRect(QRectF(5.5, 8.5, 12, 9.5), 3, 3);
    hand = hand.simplified();

    const QColor outline(110, 66, 28);
    QLinearGradient skin(0, 5, 0, 18);
    skin.setColorAt(0, QColor(246, 206, 148));
    skin.setColorAt(1, QColor(222, 160, 88));
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(skin);
    p.drawPath(hand);
    // Separaciones entre los nudillos y el pliegue de los dedos doblados.
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(8.5, 8), QPointF(8.5, 11.5));
    p.drawLine(QPointF(11.5, 7), QPointF(11.5, 11.5));
    p.drawLine(QPointF(14.5, 8), QPointF(14.5, 11.5));
    p.setPen(QPen(outline, 0.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(7, 11.5), QPointF(16.5, 11.5));

    // Pulgar recogido sobre la palma.
    QTransform thumb;
    thumb.translate(4.2, 13.6);
    thumb.rotate(-12);
    QPainterPath thumbPath;
    thumbPath.addRoundedRect(QRectF(-0.8, -1.7, 8.6, 3.4), 1.7, 1.7);
    p.setPen(QPen(outline, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(240, 192, 128));
    p.drawPath(thumb.map(thumbPath));
}

struct Entry {
    int design;
    void (*paint)(QPainter&);
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
