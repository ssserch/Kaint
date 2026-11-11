#include "app/WindowFrame.h"

#include "app/Icons.h"
#include "app/Theme.h"

#include <QApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QPointer>
#include <QWindow>

#ifdef KAINT_HAVE_KWINDOWSHADOW
#include <KWindowEffects>
#include <KWindowShadow>
#endif

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QOperatingSystemVersion>
#include <QTimer>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

namespace WindowFrame {

namespace {

bool g_enabled = false;
Buttons g_buttons = Buttons::Windows10;
bool g_left = false;
bool g_rounded = false;
#ifdef Q_OS_WIN
// Windows 11: esquinas redondeadas (las pone el propio Windows) con la recreación de Windows 11.
bool g_winRound = false;
#endif
// Radio de las esquinas de las ventanas de Windows 11.
constexpr int kRadius = 8;
// Alto de la barra de título de los diálogos (Windows 10 y 98) y ancho de sus botones.
constexpr int kTitle = 31;
constexpr int kTitle98 = 18;
constexpr int kTitleXP = 26;
// Windows 7: el borde de cristal de los diálogos y el alto de su barra (desde el borde de arriba,
// del que cuelgan los botones).
constexpr int kAeroBorder = 6;
constexpr int kTitleAero = 27;
constexpr int kButton = 46;
// Franja de los bordes (dentro de la ventana) desde la que se cambia el tamaño.
constexpr int kGrab = 5;
// Como Windows 10: bajo el puntero, el botón se aclara (u oscurece) un 10 %, un 20 % al pulsarlo;
// el de cerrar, rojo.
const QColor kCloseHover(0xe8, 0x11, 0x23);
const QColor kClosePressed(0xf1, 0x70, 0x7a);

bool classic()
{
    return Theme::active().classic;
}

// Con el tema Windows XP, la barra de título de Luna.
bool luna()
{
    const Theme::Spec s = Theme::active();
    return s.xp && !s.classic;
}

QColor overlay(int alpha)
{
    const QColor title = titleColor();
    return title.lightness() < 128 ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha);
}

QColor borderColor()
{
    const QColor title = Theme::titleBarColor();
    if (title.isValid())
        return title;
    return Theme::isDark() ? QColor(0x66, 0x66, 0x6a) : QColor(0xa0, 0xa0, 0xa0);
}

// Grosor del borde: el fino de Windows 10 o el de cuatro píxeles en relieve de Windows 98.
int borderWidth()
{
    return classic() ? 4 : luna() ? 3 : 1;
}

// --- Windows 98 ------------------------------------------------------------------------------

// Gris de las caras de Windows 98: el del tema clásico o, con los demás temas (los botones de
// Windows 98 sobre una barra de otro color), el gris de siempre.
QColor face98()
{
    return classic() ? Theme::active().panel : QColor(0xc0, 0xc0, 0xc0);
}

// Relieve de dos líneas: 'raised' hacia fuera (botones, marco) o hundido (botón pulsado).
void bevel98(QPainter& p, QRect r, bool raised)
{
    const QColor face = face98();
    const QColor white(Qt::white), light = face.lighter(116), shadow = face.darker(150), dark(Qt::black);
    auto lines = [&p](const QRect& box, const QColor& topLeft, const QColor& bottomRight) {
        p.setPen(topLeft);
        p.drawLine(box.topLeft(), box.topRight() - QPoint(1, 0));
        p.drawLine(box.topLeft(), box.bottomLeft() - QPoint(0, 1));
        p.setPen(bottomRight);
        p.drawLine(box.bottomLeft(), box.bottomRight());
        p.drawLine(box.topRight(), box.bottomRight());
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    if (raised) {
        lines(r, light, dark);
        lines(r.adjusted(1, 1, -1, -1), white, shadow);
    } else {
        lines(r, dark, white);
        lines(r.adjusted(1, 1, -1, -1), shadow, light);
    }
    p.restore();
}

// La barra de título de Windows 98: degradado azul marino (gris si no está activa); con el tema
// clásico, los colores de sus cabeceras (las de Windows 98 ALT: de azul oscuro a celeste).
void paintTitle98(QPainter& p, const QRect& r, bool active)
{
    const Theme::Spec s = Theme::active();
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0, !active ? QColor(0x80, 0x80, 0x80) : s.classic ? s.panelTitle : QColor(0x00, 0x00, 0x80));
    g.setColorAt(1, !active ? QColor(0xb5, 0xb5, 0xb5) : s.classic ? s.panelTitleEnd : QColor(0x10, 0x84, 0xd0));
    p.fillRect(r, g);
}

// La barra de título de Luna (Windows XP), con el degradado de XP.css; sin el foco, el azul apagado.
void paintTitleXP(QPainter& p, const QRect& r, bool active)
{
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    if (active) {
        g.setColorAt(0, QColor(0x09, 0x97, 0xff));
        g.setColorAt(0.08, QColor(0x00, 0x53, 0xee));
        g.setColorAt(0.40, QColor(0x00, 0x50, 0xee));
        g.setColorAt(0.88, QColor(0x00, 0x66, 0xff));
        g.setColorAt(0.93, QColor(0x00, 0x66, 0xff));
        g.setColorAt(0.95, QColor(0x00, 0x5b, 0xff));
        g.setColorAt(0.96, QColor(0x00, 0x3d, 0xd7));
        g.setColorAt(1, QColor(0x00, 0x3d, 0xd7));
    } else {
        g.setColorAt(0, QColor(0xa8, 0xbd, 0xf0));
        g.setColorAt(0.08, QColor(0x7b, 0x97, 0xe0));
        g.setColorAt(0.88, QColor(0x8a, 0xa5, 0xe9));
        g.setColorAt(1, QColor(0x6f, 0x8b, 0xd6));
    }
    p.fillRect(r, g);
}

// El marco de Luna: tres píxeles azules alrededor (más claro por dentro).
void frameXP(QPainter& p, const QRect& r, bool active)
{
    const QColor outer = active ? QColor(0x00, 0x13, 0x8c) : QColor(0x5a, 0x74, 0xc4);
    const QColor middle = active ? QColor(0x00, 0x55, 0xe5) : QColor(0x7b, 0x97, 0xe0);
    const QColor inner = active ? QColor(0x16, 0x6a, 0xee) : QColor(0x9c, 0xb3, 0xeb);
    p.setPen(outer);
    p.setBrush(Qt::NoBrush);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    p.setPen(middle);
    p.drawRect(r.adjusted(1, 1, -2, -2));
    p.setPen(inner);
    p.drawRect(r.adjusted(2, 2, -3, -3));
}

// Botón de la barra de Windows 98 (16 × 14, gris en relieve) con su dibujo en negro: 0 minimizar,
// 1 maximizar (o restaurar), 2 cerrar.
void paintButton98(QPainter& p, const QRect& r, int which, bool maximized, bool down)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(r, face98());
    bevel98(p, r, !down);
    // Los dibujos de Windows 98, píxel a píxel (al pulsar, un píxel más abajo a la derecha).
    const QPoint o = r.topLeft() + (down ? QPoint(1, 1) : QPoint());
    const QColor ink(Qt::black);
    auto px = [&](int x, int y, int w = 1, int h = 1) { p.fillRect(QRect(o + QPoint(x, y), QSize(w, h)), ink); };
    // Una ventana con el borde de arriba doble.
    auto box = [&](int x, int y, int w, int h) {
        px(x, y, w, 2);
        px(x, y + h - 1, w, 1);
        px(x, y, 1, h);
        px(x + w - 1, y, 1, h);
    };
    switch (which) {
    case 0:
        px(4, 9, 6, 2);
        break;
    case 1:
        if (maximized) {
            box(6, 2, 6, 6);
            p.fillRect(QRect(o + QPoint(3, 5), QSize(6, 6)), face98());
            box(3, 5, 6, 6);
        } else {
            box(3, 2, 9, 9);
        }
        break;
    default:
        for (int i = 0; i < 7; ++i) {
            px(4 + i, 3 + i, 2, 1);
            px(10 - i, 3 + i, 2, 1);
        }
        break;
    }
    p.restore();
}

// --- Windows 10 ------------------------------------------------------------------------------

// Las aspas de cerrar, el cuadrado de maximizar (dos al restaurar) y la raya de minimizar, como
// los de Windows 10 (Segoe MDL2 Assets): los tres de 10 × 10 píxeles, de un píxel de grueso,
// centrados en el botón y alineados con los píxeles. El aspa va de esquina a esquina con los
// extremos planos: así Qt pinta un píxel entero por fila (antes, con los extremos cuadrados, cada
// línea se repartía entre dos píxeles a medias y salía gris y borrosa) y a 200 % sale suavizada,
// como la de Windows. El cuadrado medía 11 px.
void paintGlyph(QPainter& p, const QRect& box, int which, bool maximized, const QColor& color)
{
    p.save();
    constexpr int kSide = 10;
    const int x = box.left() + (box.width() - kSide) / 2, y = box.top() + (box.height() - kSide) / 2;
    // Un recuadro de un píxel de grueso (con rectángulos: igual de nítido a cualquier escala).
    auto outline = [&p, &color](const QRect& r) {
        p.fillRect(QRect(r.left(), r.top(), r.width(), 1), color);
        p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), color);
        p.fillRect(QRect(r.left(), r.top(), 1, r.height()), color);
        p.fillRect(QRect(r.right(), r.top(), 1, r.height()), color);
    };
    switch (which) {
    case 0:
        p.fillRect(QRect(x, y + kSide / 2, kSide, 1), color);
        break;
    case 1:
        if (maximized) {
            // Restaurar: la ventana de delante (8 × 8, abajo a la izquierda) y lo que asoma de la de
            // detrás (arriba y a la derecha).
            outline(QRect(x, y + 2, 8, 8));
            p.fillRect(QRect(x + 2, y, 8, 1), color);
            p.fillRect(QRect(x + kSide - 1, y, 1, 8), color);
            p.fillRect(QRect(x + 2, y, 1, 2), color);
            p.fillRect(QRect(x + 8, y + 7, 2, 1), color);
        } else {
            outline(QRect(x, y, kSide, kSide));
        }
        break;
    default:
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 1, Qt::SolidLine, Qt::FlatCap));
        p.drawLine(QPointF(x, y), QPointF(x + kSide, y + kSide));
        p.drawLine(QPointF(x + kSide, y), QPointF(x, y + kSide));
        break;
    }
    p.restore();
}

// --- Windows 11 ------------------------------------------------------------------------------

// Como los de Windows 10 pero más finos y con las esquinas del cuadrado redondeadas.
void paintGlyph11(QPainter& p, const QRect& box, int which, bool maximized, const QColor& color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(box).center();
    p.setPen(QPen(color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case 0: p.drawLine(QPointF(c.x() - 5, c.y() + 0.5), QPointF(c.x() + 5, c.y() + 0.5)); break;
    case 1:
        if (maximized) {
            p.drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 2.5, 7, 7), 1.5, 1.5);
            QPainterPath back;
            back.moveTo(c.x() - 2.5, c.y() - 4.5);
            back.lineTo(c.x() + 3, c.y() - 4.5);
            back.quadTo(c.x() + 4.5, c.y() - 4.5, c.x() + 4.5, c.y() - 3);
            back.lineTo(c.x() + 4.5, c.y() + 2.5);
            p.drawPath(back);
        } else {
            p.drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9), 1.5, 1.5);
        }
        break;
    default:
        p.drawLine(QPointF(c.x() - 4.5, c.y() - 4.5), QPointF(c.x() + 4.5, c.y() + 4.5));
        p.drawLine(QPointF(c.x() - 4.5, c.y() + 4.5), QPointF(c.x() + 4.5, c.y() - 4.5));
        break;
    }
    p.restore();
}

// --- macOS -----------------------------------------------------------------------------------

// Los semáforos: cerrar rojo, minimizar amarillo y zoom verde, cada uno con su dibujo solo bajo el
// puntero; con la ventana inactiva, grises (salvo con el puntero sobre el grupo).
void paintMac(QPainter& p, const QRect& box, int which, bool hot, bool down, bool active, bool groupHot)
{
    static const QColor fills[] = {QColor(0xfe, 0xbc, 0x2e), QColor(0x28, 0xc8, 0x40), QColor(0xff, 0x5f, 0x57)};
    static const QColor rims[] = {QColor(0xde, 0xa1, 0x23), QColor(0x1a, 0xab, 0x29), QColor(0xe0, 0x44, 0x3e)};
    static const QColor inks[] = {QColor(0x99, 0x57, 0x00), QColor(0x00, 0x65, 0x00), QColor(0x4d, 0x00, 0x00)};
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const qreal d = std::min(box.width(), box.height());
    const QRectF circle(QRectF(box).center() - QPointF(d / 2, d / 2), QSizeF(d, d));
    QColor fill = fills[which], rim = rims[which];
    if (!active && !groupHot) {
        const bool dark = titleColor().lightness() < 128;
        fill = dark ? QColor(0x5a, 0x5a, 0x5c) : QColor(0xdc, 0xdc, 0xdc);
        rim = dark ? QColor(0x4a, 0x4a, 0x4c) : QColor(0xc4, 0xc4, 0xc4);
    } else if (down) {
        fill = fill.darker(125);
    }
    p.setPen(QPen(rim, 0.8));
    p.setBrush(fill);
    p.drawEllipse(circle.adjusted(0.4, 0.4, -0.4, -0.4));
    // El dibujo (la x, la raya o las flechas), solo en el botón que está bajo el puntero.
    if (hot || down) {
        const QPointF c = circle.center();
        const qreal k = d * 0.22;
        p.setPen(QPen(inks[which], std::max(1.0, d / 11), Qt::SolidLine, Qt::RoundCap));
        if (which == 2) {
            p.drawLine(c + QPointF(-k, -k), c + QPointF(k, k));
            p.drawLine(c + QPointF(-k, k), c + QPointF(k, -k));
        } else if (which == 0) {
            p.drawLine(c + QPointF(-k * 1.2, 0), c + QPointF(k * 1.2, 0));
        } else {
            // Zoom: dos triángulos que apuntan a las esquinas.
            p.setPen(Qt::NoPen);
            p.setBrush(inks[which]);
            const qreal a = d * 0.27;
            p.drawPolygon(QPolygonF{c + QPointF(-a, -a), c + QPointF(a * 0.45, -a), c + QPointF(-a, a * 0.45)});
            p.drawPolygon(QPolygonF{c + QPointF(a, a), c + QPointF(-a * 0.45, a), c + QPointF(a, -a * 0.45)});
        }
    }
    p.restore();
}

// --- Windows 7 -------------------------------------------------------------------------------

// Los dibujos de los botones de Windows 7 de 7.css (https://khang-nd.github.io/7.css/, licencia
// MIT, © 2021 Khang Nguyen Duy): blancos con contorno oscuro.
QImage glyph7(int which, bool maximized)
{
    static const QImage images[] = {
        QImage::fromData(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAwAAAAFCAYAAABxeg0vAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAA7SURBVHgBlY+xDQAgDMNaiQ9B8AHcxAlI/NYtKwj6QOohUzxYc+1HAqQ3e03qXNpwwcyE5QsAaEGjDRf8ZAza6Bz6VQAAAABJRU5ErkJggg=="), "PNG"),
        QImage::fromData(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAwAAAAKCAYAAACALL/6AAAACXBIWXMAAAsTAAALEwEAmpwYAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAABsSURBVHgBlZHBCYAwDEVT6YaKbqArOILgBLqBosP1VkhAaQ6S1lKad+ihPy8kxLT9+IACG57r2KqKu2GCBpTYtEMOOQELzrnv4z53I4vDjjJnwXsPJWTOAiJGHVNB5pGwLjPk+AlEBLUY7eFebCosBHOR7vYAAAAASUVORK5CYII="), "PNG"),
        QImage::fromData(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAwAAAAMCAYAAABWdVznAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAACVSURBVHgBlVDLCoMwENwU/7ClvRYK7b2lCIJHEQQ/QEHwqujfBXJIQMnihg34iHPJ7GNm2AhYcL2/JwhAxIuhq3aXb48PiFBna2YFERVHzoQLnAQmSCmxeL5+3rBtSsdpBwVKKTcY+1rY197G+8RRoLVejed94igwxrgB/zXeJ+4Jijz2Erjg+8/WE7ZAt6EgSUsIxQwaJEWv7G6SSgAAAABJRU5ErkJggg=="), "PNG"),
        QImage::fromData(QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAsAAAAKCAYAAABi8KSDAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAACUSURBVHgBlZFNCoQwDIVbmRvOMHOAAd0K/uDGlYKiCG4VPICi9yu0oCSQoFUXFtKEl6+voZXvr7sKay1TL690FOexY+Hz87i2decOuDKQsNGVe8AGYTRJIh0Y+orBvxszCPllOymlxN06jNE26Qnwg4zdHQLrMhFaawwAqAadDPE1ijwUxhiMKCmxCZk06OMYTz5lAx3xWDeXkLNAAAAAAElFTkSuQmCC"), "PNG"),
    };
    return images[which == 0 ? 0 : which == 1 ? (maximized ? 2 : 1) : 3];
}

// Los botones de cristal de Windows 7 como los de 7.css: cuelgan del borde de arriba, con
// separadores oscuros, las esquinas de abajo de fuera redondeadas y, con la ventana activa, el
// brillo de cristal (celeste bajo el puntero y rojo en el de cerrar). 'roundLeft' y 'roundRight':
// las esquinas de abajo redondeadas (las de fuera del grupo).
void paintGlass7(QPainter& p, const QRect& box, int which, bool maximized, bool hot, bool down, bool active, bool roundLeft,
                 bool roundRight, bool roundTop = false)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(box);
    constexpr qreal rad = 5;
    QPainterPath shape;
    // Suelto (el de cerrar de las ventanas flotantes): con las cuatro esquinas redondeadas.
    if (roundTop) {
        roundLeft = roundRight = false;
        shape.addRoundedRect(r, 3, 3);
    } else {
        shape.moveTo(r.topLeft());
        shape.lineTo(r.topRight());
        if (roundRight) {
            shape.lineTo(r.right(), r.bottom() - rad);
            shape.arcTo(QRectF(r.right() - 2 * rad, r.bottom() - 2 * rad, 2 * rad, 2 * rad), 0, -90);
        } else {
            shape.lineTo(r.bottomRight());
        }
        if (roundLeft) {
            shape.lineTo(r.left() + rad, r.bottom());
            shape.arcTo(QRectF(r.left(), r.bottom() - 2 * rad, 2 * rad, 2 * rad), 270, -90);
        } else {
            shape.lineTo(r.bottomLeft());
        }
        shape.closeSubpath();
    }
    p.setClipPath(shape);
    const bool close = which == 2;
    // El fondo del grupo: un velo blanco sobre la barra.
    p.fillRect(r, QColor(255, 255, 255, 0x33));
    auto vertical = [&r](std::initializer_list<std::pair<qreal, QColor>> stops) {
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        for (const auto& [at, c] : stops)
            g.setColorAt(at, c);
        return g;
    };
    // Dos colores partidos por la mitad, como los linear-gradient(a 50%, b 0) de 7.css.
    auto halves = [&](const QColor& top, const QColor& bottom) {
        return vertical({{0, top}, {0.5, top}, {0.5001, bottom}, {1, bottom}});
    };
    auto glow = [&](const QPointF& center, qreal radius, const QColor& c, qreal from = 0, qreal to = 0.65) {
        QRadialGradient g(center, radius);
        g.setColorAt(from, c);
        g.setColorAt(to, QColor(c.red(), c.green(), c.blue(), 0));
        p.fillRect(r, g);
    };
    auto edges = [&](int alpha) {
        // Las orillas de cerrar algo más oscuras.
        glow(QPointF(r.left() - r.width() * 0.6, r.center().y()), r.width(), QColor(0, 0, 0, alpha), 0.1, 0.5);
        glow(QPointF(r.right() + r.width() * 0.6, r.center().y()), r.width(), QColor(0, 0, 0, alpha), 0.1, 0.5);
    };
    const QLinearGradient sheen = vertical({{0, QColor(255, 255, 255, 0x80)},
                                            {0.45, QColor(255, 255, 255, 0x4d)},
                                            {0.5, QColor(0, 0, 0, 0x1a)},
                                            {0.75, QColor(0, 0, 0, 0x1a)},
                                            {1, QColor(255, 255, 255, 0x80)}});
    if (active || hot || down) {
        if (close) {
            if (down) {
                p.fillRect(r, halves(QColor(0xb6, 0x75, 0x62), QColor(0x7d, 0x0d, 0x01)));
                edges(255);
                glow(QPointF(r.center().x(), r.bottom() + r.height() * 0.7), r.height() * 1.4, QColor(0xdc, 0xc0, 0x3f), 0.1, 0.6);
            } else if (hot) {
                p.fillRect(r, halves(QColor(0xee, 0x6d, 0x56), QColor(0xd4, 0x28, 0x09)));
                edges(0xaa);
                glow(QPointF(r.center().x(), r.bottom() + r.height() * 0.7), r.height() * 1.4, QColor(0xf4, 0xe6, 0x76), 0.1, 0.6);
            } else {
                p.fillRect(r, halves(QColor(0xcf, 0x79, 0x6a), QColor(0xd5, 0x4f, 0x36)));
                edges(0x77);
            }
            p.fillRect(r, sheen);
        } else if (down) {
            p.fillRect(r, halves(QColor(0x86, 0xa7, 0xbc), QColor(0x09, 0x27, 0x47)));
            glow(QPointF(r.center().x(), r.bottom()), r.height() * 1.2, QColor(0x0b, 0xfd, 0xfa));
        } else if (hot) {
            p.fillRect(r, halves(QColor(0xb6, 0xd9, 0xee), QColor(0x1a, 0x6c, 0xa1)));
            glow(QPointF(r.center().x(), r.bottom()), r.height() * 1.2, QColor(0x2a, 0xce, 0xda));
        } else {
            p.fillRect(r, sheen);
        }
    }
    // Brillo interior y bordes: los separadores y el contorno del grupo (sin el de arriba).
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, active ? 0xaa : 0x55), 1));
    if (roundTop)
        p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 2, 2);
    else
        p.drawRect(r.adjusted(1.5, 0.5, -1.5, -1.5));
    p.setClipping(false);
    const QColor line = active ? QColor(0, 0, 0, 0xb3) : QColor(0, 0, 0, 0x4d);
    if (roundTop) {
        p.setPen(QPen(line, 1));
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(roundTop ? Qt::NoPen : QPen(line));
    if (roundLeft)
        p.drawLine(box.topLeft(), QPoint(box.left(), box.bottom() - int(rad)));
    p.drawLine(box.topRight(), QPoint(box.right(), box.bottom() - (roundRight ? int(rad) : 0)));
    p.drawLine(QPoint(box.left() + (roundLeft ? int(rad) : 0), box.bottom()), QPoint(box.right() - (roundRight ? int(rad) : 0), box.bottom()));
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(roundTop ? Qt::NoPen : QPen(line, 1));
    if (roundRight)
        p.drawArc(QRectF(box.right() - 2 * rad + 0.5, box.bottom() - 2 * rad + 0.5, 2 * rad, 2 * rad), 0, -90 * 16);
    if (roundLeft)
        p.drawArc(QRectF(box.left() + 0.5, box.bottom() - 2 * rad + 0.5, 2 * rad, 2 * rad), 180 * 16, 90 * 16);
    // El dibujo de 7.css, centrado (el de minimizar, abajo).
    const QImage g = glyph7(which, maximized);
    const int x = box.left() + (box.width() - g.width()) / 2;
    const int y = which == 0 ? box.top() + box.height() * 10 / 19 : box.top() + (box.height() - g.height()) / 2;
    p.setOpacity(active || hot ? 1.0 : 0.85);
    p.drawImage(QPoint(x, y), g);
    p.restore();
}

// --- Windows XP ------------------------------------------------------------------------------

// Los de Luna: cuadrados con las esquinas redondeadas y borde blanco, azules (cerrar, naranja
// rojizo) en degradado, con el dibujo blanco y grueso.
void paintLuna(QPainter& p, const QRect& box, int which, bool maximized, bool hot, bool down, bool active)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool close = which == 2;
    QColor light = close ? QColor(0xf0, 0x9a, 0x7c) : QColor(0x8c, 0xb6, 0xfb);
    QColor base = close ? QColor(0xd9, 0x4a, 0x24) : QColor(0x2f, 0x66, 0xe0);
    QColor dark = close ? QColor(0xb5, 0x2d, 0x0e) : QColor(0x16, 0x45, 0xbe);
    if (hot && !down) {
        light = light.lighter(112), base = base.lighter(118), dark = dark.lighter(118);
    } else if (down) {
        light = base.darker(110), base = dark, dark = dark.darker(115);
    }
    if (!active && !hot) {
        // Inactiva: más clara y apagada, como las de Luna sin foco.
        light = QColor(0xc4, 0xd3, 0xf5), base = close ? QColor(0xe3, 0xa5, 0x92) : QColor(0x9d, 0xb6, 0xed);
        dark = close ? QColor(0xd2, 0x8c, 0x78) : QColor(0x86, 0xa2, 0xe1);
    }
    QRadialGradient g(r.topLeft() + QPointF(r.width() * 0.35, r.height() * 0.3), r.width() * 0.95);
    g.setColorAt(0, light);
    g.setColorAt(0.55, base);
    g.setColorAt(1, dark);
    p.setPen(QPen(QColor(255, 255, 255, 230), 1));
    p.setBrush(g);
    p.drawRoundedRect(r, 3, 3);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect in = box.adjusted(5, 5, -5, -5);
    const QColor ink(Qt::white);
    switch (which) {
    case 0: p.fillRect(QRect(in.left(), in.bottom() - 2, std::max(5, in.width() * 2 / 3), 3), ink); break;
    case 1: {
        if (maximized) {
            const QRect back(in.left() + 3, in.top(), in.width() - 3, in.height() - 3);
            const QRect front(in.left(), in.top() + 3, in.width() - 3, in.height() - 3);
            p.fillRect(QRect(back.left(), back.top(), back.width(), 2), ink);
            p.fillRect(QRect(back.right(), back.top(), 1, back.height()), ink);
            p.fillRect(QRect(front.left(), front.top(), front.width(), 3), ink);
            p.fillRect(QRect(front.left(), front.top(), 1, front.height()), ink);
            p.fillRect(QRect(front.right(), front.top(), 1, front.height()), ink);
            p.fillRect(QRect(front.left(), front.bottom(), front.width(), 1), ink);
        } else {
            p.fillRect(QRect(in.left(), in.top(), in.width(), 3), ink);
            p.fillRect(QRect(in.left(), in.top(), 1, in.height()), ink);
            p.fillRect(QRect(in.right(), in.top(), 1, in.height()), ink);
            p.fillRect(QRect(in.left(), in.bottom(), in.width(), 1), ink);
        }
        break;
    }
    default: {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(ink, 2.2, Qt::SolidLine, Qt::SquareCap));
        const QRectF x = QRectF(in).adjusted(1, 1, -1, -1);
        p.drawLine(x.topLeft(), x.bottomRight());
        p.drawLine(x.bottomLeft(), x.topRight());
        break;
    }
    }
    p.restore();
}

#ifdef KAINT_HAVE_KWINDOWSHADOW
// La sombra la dibuja el gestor de ventanas (KWin), como la de las ventanas con decoración: ocho
// piezas de una imagen con un difuminado alrededor de un punto. Igual por los cuatro lados: con
// la sombra desplazada, KWin dejaba una franja vacía bajo la ventana.
KWindowShadow* makeShadow(QObject* parent)
{
    constexpr int kSize = 18;
    const int side = 2 * kSize + 1;
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = std::max(0, std::abs(x - kSize) - 1), dy = std::max(0, std::abs(y - kSize) - 1);
            const double t = std::clamp(1 - std::sqrt(dx * dx + dy * dy) / (kSize - 1), 0.0, 1.0);
            img.setPixel(x, y, qPremultiply(qRgba(0, 0, 0, int(110 * t * t))));
        }
    auto tile = [&img](int x, int y, int w, int h) {
        auto t = KWindowShadowTile::Ptr::create();
        t->setImage(img.copy(x, y, w, h));
        return t;
    };
    auto* shadow = new KWindowShadow(parent);
    shadow->setTopLeftTile(tile(0, 0, kSize, kSize));
    shadow->setTopTile(tile(kSize, 0, 1, kSize));
    shadow->setTopRightTile(tile(kSize + 1, 0, kSize, kSize));
    shadow->setLeftTile(tile(0, kSize, kSize, 1));
    shadow->setRightTile(tile(kSize + 1, kSize, kSize, 1));
    shadow->setBottomLeftTile(tile(0, kSize + 1, kSize, kSize));
    shadow->setBottomTile(tile(kSize, kSize + 1, 1, kSize));
    shadow->setBottomRightTile(tile(kSize + 1, kSize + 1, kSize, kSize));
    shadow->setPadding(QMargins(kSize, kSize, kSize, kSize));
    return shadow;
}
#endif

#ifdef Q_OS_WIN
// Windows: la ventana con el marco de Kaint sigue siendo una ventana normal para Windows (su sombra,
// Aero Snap, las animaciones de minimizar y maximizar, minimizarla desde la barra de tareas y su
// menú), pero sin la barra de título ni los bordes del sistema: WM_NCCALCSIZE deja toda la ventana
// para su contenido, como espera Qt con una ventana sin marco (Qt::FramelessWindowHint).
bool handleNativeMessage(QWidget* window, const MSG* msg, qintptr* result);

class NativeFrame : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override
    {
        if (type != "windows_generic_MSG")
            return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message != WM_NCCALCSIZE && msg->message != WM_NCACTIVATE && msg->message != WM_NCHITTEST)
            return false;
        QWidget* w = QWidget::find(WId(msg->hwnd));
        if (!w || !w->isWindow() || !isDecorated(w))
            return false;
        return handleNativeMessage(w, msg, result);
    }
};

// Windows 11 redondea las esquinas él mismo (DWMWA_WINDOW_CORNER_PREFERENCE); Windows 10 no sabe:
// allí las recorta Kaint con la región de la ventana.
bool isWindows11()
{
    static const bool eleven = QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000);
    return eleven;
}

void applyNativeFrame(QWidget* window)
{
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    // El borde de cambiar el tamaño (que Windows ya no dibuja) trae la sombra y Aero Snap; en la
    // principal, también minimizar y maximizar. Sin WS_CAPTION: con él, Qt contaría una barra de
    // título que no hay.
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR wanted = WS_THICKFRAME | WS_SYSMENU;
    if (window->inherits("QMainWindow"))
        wanted |= WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    // Los diálogos que cambian de tamaño se maximizan con un doble clic en el título (lo hace
    // Windows, que ahora sabe dónde está la barra de título: WM_NCHITTEST).
    else if (window->minimumSize() != window->maximumSize())
        wanted |= WS_MAXIMIZEBOX;
    if ((style & wanted) != wanted) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | wanted);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    // La sombra de Windows: el marco del sistema extendido un píxel dentro (lo tapa el contenido).
    const MARGINS shadow{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    // Windows 11: esquinas redondeadas con la recreación de Windows 11 y rectas con la de Windows 10
    // (DWMWA_WINDOW_CORNER_PREFERENCE; Windows 10 no lo conoce y no hace nada).
    const DWORD corners = g_winRound ? 2 : 1;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof corners);
}

// Maximizada en otra pantalla, Windows deja la ventana más grande que el área de trabajo (lo que
// ocuparía el borde que ya no tiene): lo que sobresale por cada lado, en píxeles de la ventana.
QMargins maximizedOverhang(QWidget* window)
{
    if (!window->isMaximized() || !window->testAttribute(Qt::WA_WState_Created))
        return {};
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    RECT r;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    if (!GetWindowRect(hwnd, &r) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        return {};
    const qreal dpr = std::max<qreal>(1, window->devicePixelRatioF());
    auto px = [dpr](LONG v) { return v > 0 ? int(std::ceil(v / dpr)) : 0; };
    return QMargins(px(monitor.rcWork.left - r.left), px(monitor.rcWork.top - r.top), px(r.right - monitor.rcWork.right),
                    px(r.bottom - monitor.rcWork.bottom));
}
#endif

// Lo de detrás de una ventana translúcida, difuminado como el cristal de Aero: en Windows 10 y 11
// con el «acento» de la composición de ventanas (lo que usa la barra de tareas) y en KDE Plasma con
// el difuminado de KWin. Donde no se puede, solo translúcido.
void enableBlurBehind(QWidget* window)
{
#ifdef Q_OS_WIN
    struct AccentPolicy {
        int state;
        int flags;
        DWORD color;
        int animation;
    };
    struct CompositionData {
        int attribute;
        void* data;
        SIZE_T size;
    };
    using SetCompositionAttribute = BOOL(WINAPI*)(HWND, CompositionData*);
    static const auto set = reinterpret_cast<SetCompositionAttribute>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute")));
    if (!set)
        return;
    AccentPolicy accent{3 /* ACCENT_ENABLE_BLURBEHIND */, 0, 0, 0};
    CompositionData data{19 /* WCA_ACCENT_POLICY */, &accent, sizeof accent};
    set(reinterpret_cast<HWND>(window->winId()), &data);
#elif defined(KAINT_HAVE_KWINDOWSHADOW)
    if (QWindow* handle = window->windowHandle())
        KWindowEffects::enableBlurBehind(handle, true);
#else
    Q_UNUSED(window);
#endif
}

// Esquinas redondeadas que pone Kaint: las de Linux (ventana translúcida) o, en Windows 10, la
// región de la ventana (Windows 11 las redondea él solo).
bool ownCorners()
{
#ifdef Q_OS_WIN
    return g_winRound && !isWindows11();
#else
    return g_rounded;
#endif
}

// El marco de una ventana: pinta el borde y (en los diálogos) la barra de título, y se ocupa de
// moverla, de cambiarle el tamaño desde los bordes y del botón de cerrar. La ventana es opaca: la
// sombra, si el escritorio la ofrece (KWin), la pone él.
class Frame : public QObject {
public:
    Frame(QWidget* window, bool titleBar, bool glass = false)
        : QObject(window)
        , m_window(window)
        , m_titleBar(titleBar)
        , m_glass(glass)
    {
        setObjectName(QStringLiteral("kaintWindowFrame"));
        window->setMouseTracking(true);
        // Un filtro de toda la aplicación: recibe los eventos de la ventana y los de lo que tiene
        // dentro (los bordes también se agarran encima de las barras y los paneles que llegan
        // hasta ellos).
        qApp->installEventFilter(this);
        updateMargins();
        connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
            updateMargins();
            m_window->update();
        });
#ifdef KAINT_HAVE_KWINDOWSHADOW
        m_shadow = makeShadow(this);
#endif
    }

    ~Frame() override { restoreCursor(); }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (edgeFilter(watched, event))
            return true;
        // Las barras de la ventana que cambian de sitio o de tamaño se vuelven a recortar.
        if (g_rounded && watched != m_window && (event->type() == QEvent::Resize || event->type() == QEvent::Move)
            && watched->isWidgetType() && static_cast<QWidget*>(watched)->parentWidget() == m_window)
            maskChild(static_cast<QWidget*>(watched));
        if (watched != m_window)
            return false;
        switch (event->type()) {
        case QEvent::Paint:
            paint();
            break;
        case QEvent::Show:
            installShadow();
            break;
        case QEvent::Hide:
#ifdef KAINT_HAVE_KWINDOWSHADOW
            m_shadow->destroy();
#endif
            break;
        case QEvent::WindowStateChange:
#ifdef Q_OS_WIN
            // Qt vuelve a poner sus estilos al salir de pantalla completa (antes de mostrarse, al
            // abrirse maximizada, aún no hay ventana: los pone el Show).
            if (m_window->testAttribute(Qt::WA_WState_Created))
                applyNativeFrame(m_window);
            // Windows termina de colocar la ventana (maximizada o no) después de avisar del cambio:
            // los márgenes y las esquinas se vuelven a mirar entonces.
            QTimer::singleShot(0, this, [this] { refresh(); });
#endif
            updateMargins();
            updateMask();
            m_window->update();
            break;
        case QEvent::Resize:
            updateMask();
#ifdef Q_OS_WIN
            // Lo que sobresale al maximizar se sabe cuando ya tiene su tamaño.
            updateMargins();
#endif
            break;
        case QEvent::ActivationChange:
        case QEvent::WindowTitleChange:
        case QEvent::WindowIconChange:
            m_window->update(QRect(0, 0, m_window->width(), titleRect().bottom() + 2));
            break;
        case QEvent::MouseMove:
            setHover(closeRect().contains(static_cast<QMouseEvent*>(event)->position().toPoint()));
            break;
        case QEvent::Leave:
            setHover(false);
            restoreCursor();
            break;
        case QEvent::MouseButtonPress:
            return mousePress(static_cast<QMouseEvent*>(event));
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_pressed) {
                m_pressed = false;
                m_window->update(closeRect());
                if (closeRect().contains(me->position().toPoint()))
                    m_window->close();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && titleRect().contains(me->position().toPoint())
                && !closeRect().contains(me->position().toPoint()) && resizable()) {
                m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    // Lo que depende del estado y de las opciones (al cambiarlas en Configuración).
    void refresh()
    {
        updateMargins();
        updateMask();
        m_window->update();
    }

#ifdef Q_OS_WIN
    // WM_NCHITTEST: qué hay en 'pos' (en coordenadas de la ventana) para Windows. Los bordes y la
    // barra de título los maneja él (cambiar el tamaño, mover, Aero Snap, el doble clic que maximiza
    // o restaura y el menú de la ventana con el clic derecho); el resto es de Kaint.
    LRESULT hitTest(const QPoint& pos) const
    {
        const Qt::Edges e = edgesAt(pos);
        if (e == (Qt::LeftEdge | Qt::TopEdge))
            return HTTOPLEFT;
        if (e == (Qt::RightEdge | Qt::TopEdge))
            return HTTOPRIGHT;
        if (e == (Qt::LeftEdge | Qt::BottomEdge))
            return HTBOTTOMLEFT;
        if (e == (Qt::RightEdge | Qt::BottomEdge))
            return HTBOTTOMRIGHT;
        if (e & Qt::LeftEdge)
            return HTLEFT;
        if (e & Qt::RightEdge)
            return HTRIGHT;
        if (e & Qt::TopEdge)
            return HTTOP;
        if (e & Qt::BottomEdge)
            return HTBOTTOM;
        if (m_titleBar)
            return titleRect().contains(pos) && !closeRect().contains(pos) ? HTCAPTION : HTCLIENT;
        // La ventana principal: su fila del título y los huecos de la franja de los menús.
        const QWidget* child = m_window->childAt(pos);
        return child && child->property("kaintCaption").toBool() ? HTCAPTION : HTCLIENT;
    }
#endif

private:
    void installShadow()
    {
#ifdef Q_OS_WIN
        applyNativeFrame(m_window);
#endif
        if (m_glass)
            enableBlurBehind(m_window);
#ifdef KAINT_HAVE_KWINDOWSHADOW
        if (!m_window->windowHandle())
            return;
        if (m_shadow->isCreated())
            m_shadow->destroy();
        m_shadow->setWindow(m_window->windowHandle());
        m_shadow->create();
#endif
    }

    // Los diálogos con el tema Windows 7: el marco de cristal de Aero (como las ventanas flotantes).
    bool aero() const
    {
        return m_titleBar && Theme::active().win7 && !classic();
    }

    int border() const
    {
        return aero() ? kAeroBorder : borderWidth();
    }

    QRect titleRect() const
    {
        if (!m_titleBar)
            return {};
        const int b = border();
        // Windows 7: la barra empieza en el borde de arriba (los botones cuelgan de él).
        if (aero())
            return QRect(b, 1, m_window->width() - 2 * b, kTitleAero);
        return QRect(b, b, m_window->width() - 2 * b, classic() ? kTitle98 : luna() ? kTitleXP : kTitle);
    }

    QRect closeRect() const
    {
        const QRect t = titleRect();
        if (t.isEmpty())
            return {};
        return buttonRects(t, true).value(2);
    }

    // Windows 11 en modo ventana: las esquinas redondeadas (lo de fuera no se pinta ni recibe clics).
    bool rounded() const
    {
        return ownCorners() && !m_window->isMaximized() && !m_window->isFullScreen();
    }

    QRegion roundedRegion() const
    {
        QPainterPath path;
        path.addRoundedRect(QRectF(m_window->rect()), kRadius, kRadius);
        return QRegion(path.toFillPolygon().toPolygon());
    }

    void updateMask()
    {
        if (!ownCorners() && !m_masked)
            return;
        if (!rounded()) {
            if (m_masked)
                m_window->clearMask();
            m_masked = false;
            for (QWidget* child : m_window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                if (child->property("kaintCornerMask").toBool()) {
                    child->clearMask();
                    child->setProperty("kaintCornerMask", false);
                }
            return;
        }
        // La ventana (para los clics; en Windows es la región de la ventana, que recorta también lo
        // de dentro) y, como en Wayland el compositor no recorta la ventana con su máscara, también
        // las barras que llegan a las esquinas: lo de fuera de la curva queda sin pintar
        // (transparente).
        m_window->setMask(roundedRegion());
        m_masked = true;
#ifndef Q_OS_WIN
        for (QWidget* child : m_window->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            maskChild(child);
#endif
    }

    // Una barra o panel de la ventana que llega a una esquina: recortada por la curva.
    void maskChild(QWidget* child)
    {
        if (child->isWindow())
            return;
        const QRect g = child->geometry();
        const QRect w = m_window->rect();
        const bool corner = (g.left() < w.left() + kRadius || g.right() > w.right() - kRadius)
                            && (g.top() < w.top() + kRadius || g.bottom() > w.bottom() - kRadius);
        if (!rounded() || !corner) {
            if (child->property("kaintCornerMask").toBool()) {
                child->clearMask();
                child->setProperty("kaintCornerMask", false);
            }
            return;
        }
        child->setMask(roundedRegion().translated(-g.topLeft()) & QRegion(child->rect()));
        child->setProperty("kaintCornerMask", true);
    }

    bool resizable() const
    {
        return m_window->minimumSize() != m_window->maximumSize() && !m_window->isFullScreen();
    }

    void updateMargins()
    {
        const int b = border();
        QMargins extra;
#ifdef Q_OS_WIN
        extra = maximizedOverhang(m_window);
#endif
        if (aero()) {
            m_window->setContentsMargins(QMargins(b, titleRect().bottom() + 2, b, b) + extra);
            return;
        }
        m_window->setContentsMargins(QMargins(b, b + (m_titleBar ? (classic() ? kTitle98 + 1 : luna() ? kTitleXP : kTitle) : 0), b, b)
                                     + extra);
    }

    // 7.css: el cristal (un azul con brillos), el contorno oscuro con otro blanco por dentro y el
    // contenido en su recuadro. 'shape': el contorno de la ventana (redondeado en Windows 11).
    void paintAero(QPainter& p, const QPainterPath& shape, bool active)
    {
        const QRectF r = shape.boundingRect();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        // El azul del cristal de las ventanas flotantes; con «Habilitar transparencia», translúcido
        // sobre lo que haya detrás (la ventana lo es), y si no sobre un fondo claro. Con el tema
        // oscuro, ahumado. Sin el foco, más apagado.
        const bool dark = Theme::active().dark;
        QColor glass;
        if (m_glass)
            glass = dark ? (active ? QColor(30, 38, 50, 185) : QColor(40, 44, 50, 150))
                         : (active ? QColor(69, 128, 196, 158) : QColor(150, 175, 205, 120));
        else
            glass = dark ? (active ? QColor(0x3a, 0x44, 0x52) : QColor(0x44, 0x48, 0x4e))
                         : (active ? QColor(0x7e, 0xa7, 0xd6) : QColor(0xb4, 0xc8, 0xe0));
        p.fillPath(shape, glass);
        QLinearGradient across(r.topLeft(), r.topRight());
        across.setColorAt(0, QColor(255, 255, 255, 102));
        across.setColorAt(0.5, QColor(0, 0, 0, 26));
        across.setColorAt(1, QColor(255, 255, 255, 51));
        p.fillPath(shape, across);
        QLinearGradient shine(r.topLeft(), r.bottomLeft());
        shine.setColorAt(0, Qt::transparent);
        shine.setColorAt(0.2, Qt::transparent);
        shine.setColorAt(0.4, QColor(255, 255, 255, 179));
        shine.setColorAt(0.41, Qt::transparent);
        shine.setColorAt(1, Qt::transparent);
        p.fillPath(shape, shine);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, active ? 179 : 110), 1));
        p.drawPath(shape);
        p.setPen(QPen(QColor(255, 255, 255, 170), 1));
        const qreal inner = rounded() ? kRadius - 1 : 0;
        p.drawRoundedRect(r.adjusted(1, 1, -1, -1), inner, inner);
        p.setRenderHint(QPainter::Antialiasing, false);
        const QRect body = m_window->contentsRect();
        p.fillRect(body, m_window->palette().color(QPalette::Window));
        p.setPen(QColor(255, 255, 255, 153));
        p.drawRect(body.adjusted(-2, -2, 1, 1));
        p.setPen(QColor(0, 0, 0, active ? 179 : 110));
        p.drawRect(body.adjusted(-1, -1, 0, 0));
        p.restore();
    }

    // Los bordes desde los que se cambia el tamaño: una franja de unos píxeles por dentro y las
    // esquinas algo más largas. 'pos', en coordenadas de la ventana.
    Qt::Edges edgesAt(const QPoint& pos) const
    {
        if (!resizable() || m_window->isMaximized())
            return {};
        const QRect f = m_window->rect();
        constexpr int corner = 16;
        Qt::Edges e;
        if (pos.x() < f.left() + kGrab)
            e |= Qt::LeftEdge;
        else if (pos.x() > f.right() - kGrab)
            e |= Qt::RightEdge;
        if (pos.y() < f.top() + kGrab)
            e |= Qt::TopEdge;
        else if (pos.y() > f.bottom() - kGrab)
            e |= Qt::BottomEdge;
        if (e & (Qt::LeftEdge | Qt::RightEdge)) {
            if (pos.y() < f.top() + corner)
                e |= Qt::TopEdge;
            else if (pos.y() > f.bottom() - corner)
                e |= Qt::BottomEdge;
        }
        if (e & (Qt::TopEdge | Qt::BottomEdge)) {
            if (pos.x() < f.left() + corner)
                e |= Qt::LeftEdge;
            else if (pos.x() > f.right() - corner)
                e |= Qt::RightEdge;
        }
        return e;
    }

    static Qt::CursorShape cursorFor(Qt::Edges e)
    {
        if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
            return Qt::SizeFDiagCursor;
        if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge))
            return Qt::SizeBDiagCursor;
        return e & (Qt::LeftEdge | Qt::RightEdge) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    }

    void restoreCursor()
    {
        if (m_cursorSet) {
            QGuiApplication::restoreOverrideCursor();
            m_cursorSet = false;
        }
    }

    // Movimientos y clics de toda la ventana (también encima de sus barras): cerca del borde, el
    // cursor de cambiar el tamaño y, al pulsar, se cambia.
    bool edgeFilter(QObject* watched, QEvent* event)
    {
        const QEvent::Type type = event->type();
        if ((type != QEvent::MouseMove && type != QEvent::MouseButtonPress) || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (w->window() != m_window)
            return false;
        auto* me = static_cast<QMouseEvent*>(event);
        if (type == QEvent::MouseMove && me->buttons() != Qt::NoButton)
            return false;
        const QPoint pos = m_window->mapFromGlobal(me->globalPosition().toPoint());
        const Qt::Edges edges = edgesAt(pos);
        if (type == QEvent::MouseMove) {
            if (edges) {
                const Qt::CursorShape shape = cursorFor(edges);
                if (!m_cursorSet)
                    QGuiApplication::setOverrideCursor(shape);
                else if (QGuiApplication::overrideCursor()->shape() != shape)
                    QGuiApplication::changeOverrideCursor(shape);
                m_cursorSet = true;
            } else {
                restoreCursor();
            }
            return false;
        }
        if (edges && me->button() == Qt::LeftButton && m_window->windowHandle()) {
            restoreCursor();
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        return false;
    }

    void setHover(bool on)
    {
        if (on == m_hover)
            return;
        m_hover = on;
        m_window->update(closeRect());
    }

    bool mousePress(QMouseEvent* e)
    {
        if (e->button() != Qt::LeftButton)
            return false;
        const QPoint pos = e->position().toPoint();
        if (closeRect().contains(pos)) {
            m_pressed = true;
            m_window->update(closeRect());
            return true;
        }
        if (titleRect().contains(pos) && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemMove();
            return true;
        }
        return false;
    }

    void paint()
    {
        QPainter p(m_window);
        const bool active = m_window->isActiveWindow();
        const QRect f = m_window->rect();
        if (m_glass && aero()) {
            // Windows 7 con transparencia: el cristal sobre lo que haya detrás de la ventana.
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(f, Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            QPainterPath path;
            if (rounded())
                path.addRoundedRect(QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
            else
                path.addRect(QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5));
            paintAero(p, path, active);
        } else if (rounded()) {
            // Windows 11: el fondo y el borde siguen las esquinas redondeadas.
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(f, Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
            p.fillPath(path, m_window->palette().color(QPalette::Window));
            if (aero()) {
                paintAero(p, path, active);
            } else if (m_titleBar) {
                p.save();
                p.setClipPath(path);
                if (luna())
                    paintTitleXP(p, titleRect().adjusted(-1, -1, 1, 0), active);
                else
                    p.fillRect(titleRect().adjusted(-1, -1, 1, 0), classic() ? QColor() : titleColor());
                p.restore();
            }
            if (!aero()) {
                p.setPen(borderColor());
                p.drawPath(path);
            }
            p.setRenderHint(QPainter::Antialiasing, false);
        } else {
            p.fillRect(f, m_window->palette().color(QPalette::Window));
            if (aero()) {
                QPainterPath path;
                path.addRect(QRectF(f).adjusted(0.5, 0.5, -0.5, -0.5));
                paintAero(p, path, active);
            } else if (classic()) {
                bevel98(p, f, true);
            } else if (luna()) {
                frameXP(p, f, active);
            } else {
                p.setPen(borderColor());
                p.drawRect(f.adjusted(0, 0, -1, -1));
            }
        }
        if (!m_titleBar)
            return;
        const QRect t = titleRect();
        const QRect close = closeRect();
        const bool left = g_left;
        int x = (left ? close.right() + 1 : t.left()) + (classic() ? 2 : luna() ? 5 : 8);
        if (classic())
            paintTitle98(p, t, active);
        else if (luna() && !rounded())
            paintTitleXP(p, t, active);
        else if (!rounded() && !aero())
            p.fillRect(t, titleColor());
        // El icono solo si el diálogo tiene el suyo (si no, Windows tampoco lo muestra).
        if (m_window->testAttribute(Qt::WA_SetWindowIcon)) {
            m_window->windowIcon().paint(&p, QRect(x, t.top() + (t.height() - 16) / 2, 16, 16));
            x += 16 + (classic() || luna() ? 3 : 7);
        }
        paintButton(p, close, 2, false, m_hover, m_pressed && m_hover, active, m_hover, true);
        // Como en Paint.NET, el título largo se corta con «…».
        const int end = left ? t.right() - 6 : close.left() - 6;
        const QRect text(x, t.top(), end - x, t.height());
        QFont font = m_window->font();
        font.setBold(classic() || luna());
        p.setFont(font);
        const QString title = p.fontMetrics().elidedText(m_window->windowTitle(), Qt::ElideRight, std::max(0, text.width()));
        // Windows 7: el título negro con el halo blanco de Aero sobre el cristal.
        if (aero()) {
            p.setPen(QColor(255, 255, 255, 110));
            for (const QPoint& d : {QPoint(-1, 0), QPoint(1, 0), QPoint(0, -1), QPoint(0, 1), QPoint(-1, -1), QPoint(1, 1),
                                    QPoint(-1, 1), QPoint(1, -1)})
                p.drawText(text.translated(d), Qt::AlignVCenter | Qt::AlignLeft, title);
        }
        p.setPen(titleTextColor(active));
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, title);
    }

    QWidget* m_window;
    bool m_titleBar;
    // Ventana translúcida para el cristal de Windows 7 («Habilitar transparencia»).
    bool m_glass = false;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_cursorSet = false;
    bool m_masked = false;
#ifdef KAINT_HAVE_KWINDOWSHADOW
    KWindowShadow* m_shadow = nullptr;
#endif
};

#ifdef Q_OS_WIN
Frame* frameOf(const QWidget* window)
{
    return static_cast<Frame*>(window->findChild<QObject*>(QStringLiteral("kaintWindowFrame"), Qt::FindDirectChildrenOnly));
}

bool handleNativeMessage(QWidget* window, const MSG* msg, qintptr* result)
{
    switch (msg->message) {
    case WM_NCCALCSIZE:
        // Toda la ventana para el contenido (sin barra de título ni bordes del sistema).
        *result = 0;
        return true;
    case WM_NCACTIVATE:
        // Sin repintar el marco del sistema (lParam -1): al activarse otra ventana (al abrir
        // Configuración, por ejemplo) Windows dibujaba un instante su borde, blanco, encima.
        *result = DefWindowProcW(msg->hwnd, WM_NCACTIVATE, msg->wParam, -1);
        return true;
    case WM_NCHITTEST: {
        const Frame* frame = frameOf(window);
        if (!frame)
            return false;
        POINT pt{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(msg->hwnd, &pt);
        const qreal dpr = std::max<qreal>(1, window->devicePixelRatioF());
        *result = frame->hitTest(QPoint(int(std::floor(pt.x / dpr)), int(std::floor(pt.y / dpr))));
        return true;
    }
    default:
        return false;
    }
}
#endif

// Los diálogos (también los de Qt: avisos, preguntas, progreso...) se preparan cuando reciben su
// título, antes de crearse su ventana: después ya no se puede quitar la decoración del sistema.
class Installer : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::WindowTitleChange || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (w->isWindow() && w->windowType() == Qt::Dialog && !w->testAttribute(Qt::WA_WState_Created)
            && !(w->windowFlags() & Qt::FramelessWindowHint) && Theme::current() != Theme::Scheme::Kde)
            decorate(w, true);
        return false;
    }
};

} // namespace

void setEnabled(bool on)
{
#if defined(Q_OS_MACOS)
    on = false;
#endif
    if (on == g_enabled)
        return;
    g_enabled = on;
#ifdef Q_OS_WIN
    static NativeFrame* native = nullptr;
    if (on && !native && qApp) {
        native = new NativeFrame;
        qApp->installNativeEventFilter(native);
    }
#endif
    static Installer* installer = nullptr;
    if (on && !installer && qApp) {
        installer = new Installer(qApp);
        qApp->installEventFilter(installer);
    } else if (!on && installer) {
        qApp->removeEventFilter(installer);
        delete installer;
        installer = nullptr;
    }
}

bool enabled()
{
    return g_enabled;
}

void decorate(QWidget* window, bool titleBar)
{
    if (!g_enabled || !window || isDecorated(window))
        return;
    window->setProperty("kaintWindowFrame", true);
    window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
    // Windows 7 con «Habilitar transparencia»: el cristal del marco de los diálogos deja ver lo de
    // detrás (la ventana principal sigue opaca: dibujarla translúcida costaría mucho).
    const bool glass = titleBar && Theme::aeroTransparency() && !classic();
    // Windows 11: las esquinas redondeadas necesitan el fondo translúcido (se pinta todo menos ellas).
    if (g_rounded || glass)
        window->setAttribute(Qt::WA_TranslucentBackground);
    new Frame(window, titleBar, glass);
}

void setButtons(Buttons style)
{
    g_buttons = style;
}

Buttons buttons()
{
    return g_buttons;
}

Buttons effectiveButtons()
{
    // Con los temas Windows 98 (y 98 ALT), XP y 7, sus botones (si no se eligieron otros que los
    // de Windows 10, los predeterminados).
    if (g_buttons == Buttons::Windows10 && classic())
        return Buttons::Windows98;
    if (g_buttons == Buttons::Windows10 && Theme::active().win7)
        return Buttons::Windows7;
    if (g_buttons == Buttons::Windows10 && Theme::active().xp)
        return Buttons::WindowsXP;
    return g_buttons;
}

void setButtonsOnLeft(bool on)
{
    g_left = on;
}

bool buttonsOnLeft()
{
    return g_left;
}

void setRoundedCorners(bool on)
{
#ifdef Q_OS_WIN
    // En Windows las redondea el propio Windows 11 (sin ventanas translúcidas).
    g_winRound = on;
    on = false;
#elif defined(Q_OS_MACOS)
    on = false;
#endif
    g_rounded = on;
}

bool roundedCorners()
{
    return g_rounded;
}

void refreshAll()
{
    for (QWidget* w : QApplication::topLevelWidgets())
        if (isDecorated(w)) {
#ifdef Q_OS_WIN
            if (w->testAttribute(Qt::WA_WState_Created))
                applyNativeFrame(w);
            // Las esquinas de Windows 10 (la región de la ventana) cambian al momento.
            if (Frame* frame = frameOf(w))
                frame->refresh();
#endif
            w->update();
            for (QWidget* child : w->findChildren<QWidget*>())
                if (dynamic_cast<CaptionButtons*>(child) || dynamic_cast<CaptionTitle*>(child)) {
                    child->updateGeometry();
                    child->update();
                }
        }
}

namespace {

// Medidas de cada estilo en una fila de alto 'h': ancho de minimizar y maximizar, de cerrar, alto,
// separación entre botones, separación extra antes de cerrar, margen con el borde y si cuelgan
// de arriba (Windows 7).
struct Metrics {
    int w, closeW, h, gap, closeGap, margin;
    bool hang;
};

Metrics metricsFor(Buttons style, int h)
{
    switch (style) {
    case Buttons::Windows11:
    case Buttons::Windows10: return {kButton, kButton, h, 0, 0, 0, false};
    case Buttons::MacOS: {
        const int d = h >= 26 ? 12 : std::max(8, h - 6);
        return {d, d, d, 8, 0, 9, false};
    }
    case Buttons::Windows7: {
        // Las medidas de 7.css: 29 x 19 y cerrar de 48.
        const int bh = std::min(19, h - 2);
        return {29, 48, bh, 0, 0, 6, true};
    }
    case Buttons::WindowsXP: {
        const int side = std::min(21, h - 4);
        return {side, side, side, 2, 0, 4, false};
    }
    case Buttons::Windows98: return {16, 16, 14, 0, 2, 2, false};
    }
    return {kButton, kButton, h, 0, 0, 0, false};
}

} // namespace

QList<QRect> buttonRects(const QRect& area, bool closeOnly)
{
    const Metrics m = metricsFor(effectiveButtons(), area.height());
    QList<QRect> rects(3);
    // A la derecha: minimizar, maximizar, cerrar; a la izquierda: cerrar, minimizar, maximizar. Los
    // de macOS, siempre rojo, amarillo y verde.
    const bool macOrder = g_left || effectiveButtons() == Buttons::MacOS;
    const QList<int> order = closeOnly ? QList<int>{2} : macOrder ? QList<int>{2, 0, 1} : QList<int>{0, 1, 2};
    const int y = m.hang ? area.top() : area.top() + (area.height() - m.h) / 2;
    if (g_left) {
        int x = area.left() + m.margin;
        for (int k = 0; k < order.size(); ++k) {
            const int i = order[k];
            const int w = i == 2 ? m.closeW : m.w;
            rects[i] = QRect(x, y, w, m.h);
            x += w + m.gap + (i == 2 ? m.closeGap : 0);
        }
    } else {
        int x = area.right() + 1 - m.margin;
        for (int k = int(order.size()) - 1; k >= 0; --k) {
            const int i = order[k];
            const int w = i == 2 ? m.closeW : m.w;
            x -= w;
            rects[i] = QRect(x, y, w, m.h);
            x -= m.gap + (i == 2 ? m.closeGap : 0);
        }
    }
    return rects;
}

int buttonsWidth(int rowHeight, bool closeOnly)
{
    const Metrics m = metricsFor(effectiveButtons(), rowHeight);
    if (closeOnly)
        return m.closeW + 2 * m.margin;
    return 2 * m.w + m.closeW + 2 * m.gap + m.closeGap + 2 * m.margin;
}

void paintButton(QPainter& p, const QRect& box, int which, bool maximized, bool hot, bool down, bool active, bool groupHot,
                 bool alone)
{
    const Buttons style = effectiveButtons();
    switch (style) {
    case Buttons::Windows98: paintButton98(p, box, which, maximized, down); return;
    case Buttons::MacOS: paintMac(p, box, which, hot, down, active, groupHot); return;
    case Buttons::WindowsXP: paintLuna(p, box, which, maximized, hot, down, active); return;
    case Buttons::Windows7: {
        // Las esquinas de abajo de fuera del grupo, redondeadas.
        // Solo (el de cerrar de los diálogos), con las dos esquinas redondeadas.
        const bool first = alone || (g_left ? which == 2 : which == 0);
        const bool last = alone || (g_left ? which == 1 : which == 2);
        paintGlass7(p, box, which, maximized, hot, down, active, first, last);
        return;
    }
    case Buttons::Windows11: {
        // Rojo de cerrar de Windows 11 y un fondo muy suave en los demás.
        QColor glyph = titleTextColor(active);
        if (which == 2 && (hot || down)) {
            p.fillRect(box, down ? QColor(0xc4, 0x2b, 0x1c, 0xe6) : QColor(0xc4, 0x2b, 0x1c));
            glyph = Qt::white;
        } else if (hot || down) {
            p.fillRect(box, overlay(down ? 15 : 24));
        }
        paintGlyph11(p, box, which, maximized, glyph);
        return;
    }
    case Buttons::Windows10: {
        QColor glyph = titleTextColor(active);
        if (which == 2 && (hot || down)) {
            p.fillRect(box, down ? kClosePressed : kCloseHover);
            glyph = Qt::white;
        } else if (hot || down) {
            p.fillRect(box, overlay(down ? 51 : 26));
        }
        paintGlyph(p, box, which, maximized, glyph);
        return;
    }
    }
}

bool isDecorated(const QWidget* window)
{
    return window && window->property("kaintWindowFrame").toBool();
}

QColor titleColor()
{
    // La de la franja de los menús (negra con los temas oscuros de Paint.NET), así la barra de
    // título y la franja se ven seguidas, como en paint.js; o el color propio de la barra superior.
    const QColor title = Theme::titleBarColor();
    return title.isValid() ? title : Theme::active().menuBar;
}

QColor titleTextColor(bool active)
{
    if (classic())
        return active ? QColor(Qt::white) : QColor(0xc0, 0xc0, 0xc0);
    if (luna())
        return active ? QColor(Qt::white) : QColor(0xd8, 0xe4, 0xf8);
    QColor c = titleColor().lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    // Inactiva, el título y los botones se apagan.
    if (!active)
        c.setAlphaF(0.45);
    return c;
}

bool classicTitle()
{
    return classic() || luna();
}

bool lunaTitle()
{
    return luna();
}

void paintClassicTitle(QPainter& p, const QRect& r, bool active)
{
    if (luna())
        paintTitleXP(p, r, active);
    else
        paintTitle98(p, r, active);
}

void paintPanelClose(QPainter& p, const QRect& box, Buttons style, bool hot, bool down)
{
    switch (style) {
    case Buttons::Windows98: paintButton98(p, box, 2, false, down); return;
    case Buttons::WindowsXP: paintLuna(p, box, 2, false, hot, down, true); return;
    case Buttons::Windows7: paintGlass7(p, box, 2, false, hot, down, true, true, true, true); return;
    default: paintButton(p, box, 2, false, hot, down, true, hot, true); return;
    }
}

} // namespace WindowFrame

// ---------------------------------------------------------------------------

CaptionTitle::CaptionTitle(QWidget* parent)
    : QWidget(parent)
{
    // En Windows es la barra de título para el propio Windows (WM_NCHITTEST): mover, Aero Snap,
    // doble clic para maximizar o restaurar y su menú con el clic derecho.
    setProperty("kaintCaption", true);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

QSize CaptionTitle::sizeHint() const
{
    return {fontMetrics().horizontalAdvance(window()->windowTitle()) + 40, CaptionButtons::rowHeight()};
}

QSize CaptionTitle::minimumSizeHint() const
{
    return {40, CaptionButtons::rowHeight()};
}

bool CaptionTitle::event(QEvent* e)
{
    // El título y el estado activo son los de la ventana.
    if (e->type() == QEvent::Polish && !m_watching) {
        window()->installEventFilter(this);
        m_watching = true;
    }
    return QWidget::event(e);
}

bool CaptionTitle::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == window() && (e->type() == QEvent::WindowTitleChange || e->type() == QEvent::ActivationChange))
        update();
    return false;
}

void CaptionTitle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool active = window()->isActiveWindow();
    const bool classic = WindowFrame::classicTitle();
    const QRect r = rect();
    // Con los botones a la izquierda, el icono y el título van después de ellos.
    const int x0 = (WindowFrame::buttonsOnLeft() ? WindowFrame::buttonsWidth(CaptionButtons::rowHeight(), false) : 0) + (classic ? 3 : 8);
    Icons::app().paint(&p, QRect(x0, (r.height() - 16) / 2, 16, 16));
    const QRect text = r.adjusted(x0 + 16 + (classic ? 4 : 8), 0, -4, 0);
    QFont font = this->font();
    font.setBold(classic);
    p.setFont(font);
    p.setPen(WindowFrame::titleTextColor(active));
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(window()->windowTitle(), Qt::ElideRight, std::max(0, text.width())));
}

void CaptionTitle::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CaptionTitle::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return QWidget::mouseDoubleClickEvent(e);
    QWidget* w = window();
    w->isMaximized() ? w->showNormal() : w->showMaximized();
}

// ---------------------------------------------------------------------------

CaptionButtons::CaptionButtons(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        updateGeometry();
        update();
    });
}

int CaptionButtons::rowHeight()
{
    // Windows 98: la barra de 18 px con un poco de aire; XP: 28 px; Windows 10: 32 px.
    return WindowFrame::lunaTitle() ? 28 : WindowFrame::classicTitle() ? 20 : 32;
}

QSize CaptionButtons::sizeHint() const
{
    return QSize(WindowFrame::buttonsWidth(rowHeight(), false), rowHeight());
}

bool CaptionButtons::event(QEvent* e)
{
    if (e->type() == QEvent::Polish && !m_watching) {
        window()->installEventFilter(this);
        m_watching = true;
    }
    return QWidget::event(e);
}

bool CaptionButtons::eventFilter(QObject* watched, QEvent* e)
{
    // Maximizar pasa a restaurar al cambiar el estado; inactiva, los iconos se apagan.
    if (watched == window() && (e->type() == QEvent::WindowStateChange || e->type() == QEvent::ActivationChange))
        update();
    return false;
}

QRect CaptionButtons::buttonRect(int i) const
{
    return WindowFrame::buttonRects(rect(), false).value(i);
}

bool CaptionButtons::groupHot() const
{
    return m_hover >= 0 || m_pressed >= 0;
}

int CaptionButtons::buttonAt(const QPoint& pos) const
{
    for (int i = 0; i < 3; ++i)
        if (buttonRect(i).contains(pos))
            return i;
    return -1;
}

void CaptionButtons::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool active = window()->isActiveWindow();
    const bool maximized = window()->isMaximized();
    for (int i = 0; i < 3; ++i) {
        const QRect box = buttonRect(i);
        const bool hot = m_hover == i, down = m_pressed == i && hot;
        WindowFrame::paintButton(p, box, i, maximized, hot, down, active, groupHot());
    }
}

void CaptionButtons::mouseMoveEvent(QMouseEvent* e)
{
    const int b = buttonAt(e->position().toPoint());
    if (b != m_hover) {
        m_hover = b;
        update();
    }
}

void CaptionButtons::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

void CaptionButtons::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = buttonAt(e->position().toPoint());
    // Fuera de los botones (el hueco entre ellos), se arrastra la ventana.
    if (m_pressed < 0 && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    update();
}

void CaptionButtons::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int b = m_pressed;
    m_pressed = -1;
    update();
    if (b < 0 || b != buttonAt(e->position().toPoint()))
        return;
    QWidget* w = window();
    if (b == 0)
        w->showMinimized();
    else if (b == 1)
        w->isMaximized() ? w->showNormal() : w->showMaximized();
    else
        w->close();
}
