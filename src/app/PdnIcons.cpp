#include "app/Icons.h"

#include "app/Theme.h"

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStyle>
#include <QStyleOption>

#include <cmath>

// Iconos al estilo Paint.NET dibujados por código, adaptados de paint.software
// (https://github.com/Univers4craft/paint.software, licencia MIT, © Univers4craft). Cada uno se
// dibuja en una rejilla de 20 px (herramientas) o 16 px (barras y ventanas) y se escala a
// cualquier tamaño.
namespace Icons {

namespace {

constexpr int TS = 20;

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

void drawBrush(QPainter& p)
{
    aa(p);
    QLinearGradient hg(18, 1, 8, 11);
    hg.setColorAt(0.0, QColor(126, 180, 238));
    hg.setColorAt(1.0, QColor(54, 106, 178));
    p.setPen(QPen(QColor(28, 58, 98), 0.9));
    p.setBrush(hg);
    QPainterPath h;
    h.moveTo(17.4, 1.2); h.lineTo(19.0, 2.8); h.lineTo(10.2, 11.6); h.lineTo(8.4, 9.8);
    h.closeSubpath();
    p.drawPath(h);
    p.setPen(QPen(QColor(92, 98, 110), 0.9));
    p.setBrush(QColor(198, 205, 216));
    QPainterPath f;
    f.moveTo(10.2, 11.6); f.lineTo(8.4, 9.8); f.lineTo(6.2, 12.0); f.lineTo(8.0, 13.8);
    f.closeSubpath();
    p.drawPath(f);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 92, 170));
    QPainterPath b;
    b.moveTo(6.2, 12.0); b.lineTo(8.0, 13.8); b.lineTo(1.8, 18.6);
    b.closeSubpath();
    p.drawPath(b);
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
    aa(p);

    p.save();
    p.translate(10.0, 9.5);
    p.rotate(-38);
    p.translate(-10.0, -9.5);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(76, 96, 122), 1.2));
    p.drawArc(QRectF(6.0, 1.6, 8.0, 7.0), 15 * 16, 150 * 16);

    QLinearGradient bg(5.5, 0, 14.5, 0);
    bg.setColorAt(0.0, QColor(112, 170, 234));
    bg.setColorAt(1.0, QColor(40, 96, 172));
    p.setPen(QPen(QColor(26, 58, 104), 1.0));
    p.setBrush(bg);
    QPainterPath body;
    body.moveTo(5.2, 6.2); body.lineTo(14.8, 6.2);
    body.lineTo(12.9, 16.0); body.lineTo(7.1, 16.0);
    body.closeSubpath();
    p.drawPath(body);

    p.setBrush(QColor(158, 206, 246));
    p.drawEllipse(QRectF(5.2, 4.1, 9.6, 4.2));
    p.restore();

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(48, 112, 198));
    QPainterPath drip;
    drip.moveTo(12.6, 11.4);
    drip.quadTo(17.6, 14.0, 15.8, 18.4);
    drip.quadTo(13.4, 19.6, 12.2, 15.6);
    drip.closeSubpath();
    p.drawPath(drip);
}

void drawColorPicker(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(28, 60, 102), 0.9));
    p.setBrush(QColor(92, 150, 216));
    p.drawEllipse(QRectF(10.8, 1.0, 7.8, 7.8));
    p.setPen(QPen(QColor(92, 98, 110), 0.9));
    p.setBrush(QColor(202, 210, 222));
    QPainterPath shaft;
    shaft.moveTo(12.4, 7.4); shaft.lineTo(14.8, 9.8); shaft.lineTo(7.4, 15.6); shaft.lineTo(5.2, 13.4);
    shaft.closeSubpath();
    p.drawPath(shaft);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(50, 54, 62));
    QPainterPath tip;
    tip.moveTo(7.4, 15.6); tip.lineTo(5.2, 13.4); tip.lineTo(1.8, 18.6); tip.closeSubpath();
    p.drawPath(tip);
}

void drawRecolor(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(26, 58, 100), 0.9));
    p.setBrush(QColor(72, 128, 202));
    p.drawEllipse(QRectF(2.6, 5.4, 11.2, 11.2));
    QPen ap(QColor(212, 64, 42), 2.1);
    ap.setCapStyle(Qt::RoundCap);
    p.setPen(ap);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(4.6, 2.4, 13.0, 13.0), 15 * 16, 205 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(212, 64, 42));
    QPointF ah[] = { QPointF(18.6, 7.6), QPointF(13.6, 6.9), QPointF(16.3, 11.4) };
    p.drawPolygon(ah, 3);
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
    aa(p);
    QLinearGradient grad(3, 3, 17, 17);
    grad.setColorAt(0.0, QColor(166, 100, 220));
    grad.setColorAt(1.0, QColor(56, 104, 198));
    p.setPen(QPen(QColor(44, 54, 74), 1.1));
    p.setBrush(grad);
    p.drawRoundedRect(QRectF(3, 3, 14, 14), 1.6, 1.6);
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
    p.setPen(QPen(QColor(50, 100, 200), 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRect(3, 4, 10, 10), 45*16, 225*16);
    p.setBrush(QColor(50, 100, 200));
    p.setPen(Qt::NoPen);
    QPointF arr[] = {QPointF(3, 4), QPointF(7, 3), QPointF(5, 7)};
    p.drawPolygon(arr, 3);
}

void drawRedo(QPainter& p)
{
    aa(p);
    p.setPen(QPen(QColor(50, 100, 200), 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRect(3, 4, 10, 10), -45*16, -225*16);
    p.setBrush(QColor(50, 100, 200));
    p.setPen(Qt::NoPen);
    QPointF arr[] = {QPointF(13, 4), QPointF(9, 3), QPointF(11, 7)};
    p.drawPolygon(arr, 3);
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
        {QStringLiteral("edit-undo"), {16, drawUndo, true}},
        {QStringLiteral("edit-redo"), {16, drawRedo, true}},
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

QIcon pdn(const QString& name)
{
    const auto it = table().constFind(name);
    return it == table().constEnd() ? QIcon() : QIcon(new PdnIconEngine(*it));
}

} // namespace Icons
