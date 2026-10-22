#include "app/Icons.h"

#include "core/CustomShapes.h"
#include "core/FillPattern.h"
#include "core/Raster.h"

#include <QApplication>
#include <QConicalGradient>
#include <QFile>
#include <QIconEngine>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QRadialGradient>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Icons {

namespace {

constexpr int kSize = 32;

QColor fg() { return QApplication::palette().color(QPalette::WindowText); }
QColor accent() { return QApplication::palette().color(QPalette::Highlight); }

// Icono dibujado por código. Se pinta cada vez con la paleta actual, así cambia de color con
// el tema (claro/oscuro) sin tener que recrearlo.
class DrawnIconEngine : public QIconEngine {
public:
    using PaintFn = std::function<void(QPainter&, QRectF)>;
    explicit DrawnIconEngine(PaintFn fn) : m_paint(std::move(fn)) {}

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
        QPixmap pm(size * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(size.width() / qreal(kSize), size.height() / qreal(kSize));
            m_paint(p, QRectF(3, 3, kSize - 6, kSize - 6));
        }
        if (mode == QIcon::Disabled || mode == QIcon::Selected) {
            QStyleOption opt;
            opt.palette = QApplication::palette();
            const QPixmap generated = QApplication::style()->generatedIconPixmap(mode, pm, &opt);
            if (!generated.isNull())
                return generated;
        }
        return pm;
    }

    QIconEngine* clone() const override { return new DrawnIconEngine(m_paint); }
    QString key() const override { return QStringLiteral("KaintDrawn"); }

private:
    PaintFn m_paint;
};

template <typename F>
QIcon drawn(F paint)
{
    return QIcon(new DrawnIconEngine(std::move(paint)));
}

} // namespace

void setupTheme()
{
    QStringList paths = QIcon::themeSearchPaths();
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"),
                                                       QStandardPaths::LocateDirectory);
    for (const QString& dir : dirs)
        if (!paths.contains(dir))
            paths.append(dir);
    QIcon::setThemeSearchPaths(paths);
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
}

QIcon theme(std::initializer_list<const char*> names)
{
    for (const char* n : names)
        if (QIcon icon = pdn(QString::fromLatin1(n)); !icon.isNull())
            return icon;
    for (const char* n : names) {
        const QString name = QString::fromLatin1(n);
        if (QIcon::hasThemeIcon(name))
            return QIcon::fromTheme(name);
    }
    return {};
}

QIcon theme(const QString& name)
{
    if (QIcon icon = pdn(name); !icon.isNull())
        return icon;
    return name.isEmpty() || !QIcon::hasThemeIcon(name) ? QIcon() : QIcon::fromTheme(name);
}

QIcon gradient(GradientType type)
{
    return drawn([type](QPainter& p, QRectF r) {
        const QColor a = fg();
        const QColor b = QColor(a.red(), a.green(), a.blue(), 0);
        QBrush brush;
        switch (type) {
        case GradientType::Linear: {
            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            brush = g;
            break;
        }
        case GradientType::Reflected: {
            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, b);
            g.setColorAt(0.5, a);
            g.setColorAt(1, b);
            brush = g;
            break;
        }
        case GradientType::Diamond: {
            QPainterPath d;
            d.addPolygon(QPolygonF({QPointF(r.center().x(), r.top()), QPointF(r.right(), r.center().y()),
                                    QPointF(r.center().x(), r.bottom()), QPointF(r.left(), r.center().y())}));
            QRadialGradient g(r.center(), r.width() / 2);
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            p.fillPath(d, g);
            p.setPen(QPen(fg(), 1));
            p.drawRect(r);
            return;
        }
        case GradientType::Radial: {
            QRadialGradient g(r.center(), r.width() / 2);
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            brush = g;
            break;
        }
        case GradientType::Conical: {
            QConicalGradient g(r.center(), 90);
            g.setColorAt(0, a);
            g.setColorAt(1, b);
            brush = g;
            break;
        }
        case GradientType::SpiralClockwise:
        case GradientType::SpiralCounterClockwise: {
            QPainterPath spiral;
            const bool cw = type == GradientType::SpiralClockwise;
            for (int i = 0; i <= 60; ++i) {
                const double t = i / 60.0;
                const double ang = (cw ? 1 : -1) * t * 4 * M_PI;
                const QPointF pt = r.center() + QPointF(std::cos(ang), std::sin(ang)) * (t * r.width() / 2);
                i == 0 ? spiral.moveTo(pt) : spiral.lineTo(pt);
            }
            p.setPen(QPen(fg(), 2));
            p.drawPath(spiral);
            p.setPen(QPen(fg(), 1));
            p.drawRect(r);
            return;
        }
        }
        p.fillRect(r, brush);
        p.setPen(QPen(fg(), 1));
        p.drawRect(r);
    });
}

QIcon gradientRepeat(GradientRepeat repeat)
{
    return drawn([repeat](QPainter& p, QRectF r) {
        const QColor a = fg();
        const QColor b = QColor(a.red(), a.green(), a.blue(), 20);
        QLinearGradient g(QPointF(r.left() + r.width() * 0.3, 0), QPointF(r.left() + r.width() * 0.6, 0));
        g.setColorAt(0, a);
        g.setColorAt(1, b);
        g.setSpread(repeat == GradientRepeat::None ? QGradient::PadSpread
                    : repeat == GradientRepeat::Wrapped ? QGradient::RepeatSpread : QGradient::ReflectSpread);
        p.fillRect(r, g);
        p.setPen(QPen(fg(), 1));
        p.drawRect(r);
    });
}

QIcon fillPattern(int pattern)
{
    return drawn([pattern](QPainter& p, QRectF r) {
        const QRect ir = r.toRect();
        QImage img(ir.size(), QImage::Format_ARGB32_Premultiplied);
        Raster::renderFill(img, QPoint(0, 0), pattern, fg(), QApplication::palette().color(QPalette::Base));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.drawImage(ir.topLeft(), img);
        p.setPen(QPen(fg(), 1));
        p.drawRect(ir);
    });
}

QIcon shape(ShapeType type)
{
    return drawn([type](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shapePath(type, r.adjusted(1, 2, -1, -2), 4));
    });
}

QIcon customShape(const QPainterPath& path)
{
    return drawn([path](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(CustomShapes::fitted(path, r.adjusted(1.5, 1.5, -1.5, -1.5)));
    });
}

QIcon shapeStyle(ShapeStyle style)
{
    return drawn([style](QPainter& p, QRectF r) {
        const QRectF box = r.adjusted(2, 5, -2, -5);
        if (style != ShapeStyle::Outline)
            p.fillRect(box, style == ShapeStyle::Fill ? fg() : accent());
        if (style != ShapeStyle::Fill) {
            p.setPen(QPen(fg(), 2.5));
            p.drawRect(box);
        }
    });
}

QIcon curveType(CurveType type)
{
    return drawn([type](QPainter& p, QRectF r) {
        const QPointF a(r.left(), r.bottom() - 4), b(r.left() + r.width() * 0.35, r.top() + 2);
        const QPointF c(r.left() + r.width() * 0.65, r.bottom() - 2), d(r.right(), r.top() + 4);
        QPainterPath path(a);
        if (type == CurveType::Straight) {
            path.lineTo(d);
        } else if (type == CurveType::Spline) {
            path.cubicTo(b, c, d);
        } else {
            path.cubicTo(b, c, d);
            p.setPen(QPen(accent(), 1, Qt::DotLine));
            p.drawLine(a, b);
            p.drawLine(c, d);
        }
        p.setPen(QPen(fg(), 2));
        p.drawPath(path);
    });
}

QIcon lineCap(LineCap cap, bool start)
{
    return drawn([cap, start](QPainter& p, QRectF r) {
        const qreal y = r.center().y();
        const QPointF tip = start ? QPointF(r.left() + 2, y) : QPointF(r.right() - 2, y);
        const qreal dir = start ? 1 : -1;
        p.setPen(QPen(fg(), 4, Qt::SolidLine, cap == LineCap::Round ? Qt::RoundCap : Qt::FlatCap));
        p.drawLine(tip + QPointF(dir * (cap == LineCap::Flat || cap == LineCap::Round ? 0 : 6), 0),
                   QPointF(start ? r.right() : r.left(), y));
        if (cap == LineCap::Arrow || cap == LineCap::ArrowFilled) {
            QPolygonF head{tip, tip + QPointF(dir * 10, -7), tip + QPointF(dir * 10, 7)};
            if (cap == LineCap::ArrowFilled) {
                p.setPen(Qt::NoPen);
                p.setBrush(fg());
                p.drawPolygon(head);
            } else {
                p.setPen(QPen(fg(), 2.5));
                p.drawPolyline(QPolygonF{head[1], tip, head[2]});
            }
        }
    });
}

QIcon antialias(bool on)
{
    // Como Paint.NET: una curva con sus puntos de control; escalonada sin suavizado.
    return drawn([on](QPainter& p, QRectF r) {
        p.setRenderHint(QPainter::Antialiasing, on);
        p.setPen(QPen(fg(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (on) {
            QPainterPath path(QPointF(r.left() + 2, r.top() + 3));
            path.cubicTo(QPointF(r.center().x(), r.top() + 2), QPointF(r.center().x() - 2, r.bottom() - 2),
                         QPointF(r.right() - 2, r.bottom() - 3));
            p.drawPath(path);
        } else {
            QPolygonF steps;
            const int n = 6;
            for (int i = 0; i <= n; ++i) {
                const qreal x = r.left() + 2 + (r.width() - 4) * i / n;
                const qreal y = r.top() + 3 + (r.height() - 6) * i / n;
                steps << QPointF(x, y) << QPointF(x + (r.width() - 4) / n, y);
            }
            p.drawPolyline(steps);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        for (const QPointF c : {QPointF(r.left() + 2, r.top() + 3), QPointF(r.right() - 2, r.bottom() - 3)})
            p.drawEllipse(c, 2.6, 2.6);
    });
}

QIcon smoothing(bool on)
{
    return drawn([on](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (on) {
            QPainterPath path(QPointF(r.left() + 1, r.center().y() + 4));
            path.cubicTo(QPointF(r.left() + 8, r.top()), QPointF(r.right() - 10, r.bottom()), QPointF(r.right() - 1, r.top() + 5));
            p.drawPath(path);
        } else {
            p.drawPolyline(QPolygonF{QPointF(r.left() + 1, r.center().y() + 4), QPointF(r.left() + 7, r.top() + 4),
                                     QPointF(r.center().x() - 1, r.center().y() + 2), QPointF(r.center().x() + 4, r.bottom() - 4),
                                     QPointF(r.right() - 5, r.center().y()), QPointF(r.right() - 1, r.top() + 5)});
        }
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        p.drawEllipse(QPointF(r.right() - 4, r.bottom() - 4), 3.5, 3.5);
    });
}

QIcon gamma(bool corrected)
{
    // Una curva de gamma (corregida) o una recta (se ignora la gamma).
    return drawn([corrected](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 1.2));
        p.drawLine(r.bottomLeft(), r.topLeft());
        p.drawLine(r.bottomLeft(), r.bottomRight());
        p.setPen(QPen(accent(), 2.4, Qt::SolidLine, Qt::RoundCap));
        if (corrected) {
            QPainterPath path(r.bottomLeft());
            path.quadTo(QPointF(r.left() + r.width() * 0.15, r.top() + r.height() * 0.15), r.topRight());
            p.drawPath(path);
        } else {
            p.drawLine(r.bottomLeft(), r.topRight());
        }
    });
}

QIcon fontMetric(FontMetric metric)
{
    // "ƒ" con un subíndice: los puntos dependen de los PPP de la imagen; fijo, 96.
    return drawn([metric](QPainter& p, QRectF r) {
        QFont f = p.font();
        f.setItalic(true);
        f.setBold(true);
        f.setPixelSize(int(r.height() * 0.9));
        p.setFont(f);
        p.setPen(fg());
        p.drawText(QRectF(r.left(), r.top() - 2, r.width() * 0.6, r.height()), Qt::AlignCenter, QStringLiteral("ƒ"));
        const QRectF box(r.center().x() + 1, r.center().y() + 1, r.width() / 2 - 1, r.height() / 2 - 1);
        if (metric == FontMetric::Fixed96) {
            p.fillRect(box, accent());
        } else {
            p.setPen(QPen(accent(), 2));
            p.drawRect(box.adjusted(1, 1, -1, -1));
        }
    });
}

QIcon recolorSampling(RecolorSampling sampling)
{
    return drawn([sampling](QPainter& p, QRectF r) {
        if (sampling == RecolorSampling::Once) {
            p.setPen(QPen(fg(), 2));
            p.drawEllipse(r.center(), r.width() * 0.32, r.height() * 0.32);
            p.drawLine(QPointF(r.center().x(), r.top()), QPointF(r.center().x(), r.top() + 6));
            p.drawLine(QPointF(r.center().x(), r.bottom()), QPointF(r.center().x(), r.bottom() - 6));
            p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.left() + 6, r.center().y()));
            p.drawLine(QPointF(r.right(), r.center().y()), QPointF(r.right() - 6, r.center().y()));
            p.setPen(Qt::NoPen);
            p.setBrush(accent());
            p.drawEllipse(r.center(), 2.5, 2.5);
        } else {
            const QRectF back(r.left(), r.top(), r.width() * 0.62, r.height() * 0.62);
            const QRectF front(r.right() - r.width() * 0.62, r.bottom() - r.height() * 0.62, r.width() * 0.62,
                               r.height() * 0.62);
            p.setPen(QPen(fg(), 1.2));
            p.setBrush(Qt::black);
            p.drawRect(back);
            p.setBrush(Qt::white);
            p.drawRect(front);
        }
    });
}

QIcon rectSelectMode(RectSelectMode mode)
{
    return drawn([mode](QPainter& p, QRectF r) {
        const QRectF box = r.adjusted(1, 4, -6, -4);
        p.setPen(QPen(fg(), 1.5, Qt::DashLine));
        p.setBrush(QColor(accent().red(), accent().green(), accent().blue(), 90));
        p.drawRect(box);
        if (mode == RectSelectMode::Normal)
            return;
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        const QRectF corner(r.right() - 11, r.bottom() - 11, 11, 11);
        if (mode == RectSelectMode::FixedRatio) {
            p.drawRect(corner.adjusted(0, 5, -5, 0));
            p.drawRect(corner.adjusted(5, 0, 0, -5));
        } else {
            p.drawRect(corner.adjusted(0, 4, 0, 0));
            p.setPen(QPen(accent(), 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(corner.left() + 2, corner.top(), corner.width() - 4, 8), 0, 180 * 16);
        }
    });
}

QIcon afterSelect(AfterSelect after)
{
    if (after == AfterSelect::MoveSelection)
        return theme({"transform-move", "edit-select"});
    if (after == AfterSelect::MovePixels)
        return theme({"transform-move", "object-move"});
    return drawn([](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 1.5, Qt::DashLine));
        p.drawRect(r.adjusted(2, 4, -2, -4));
    });
}

QIcon sampleSize(int pixels)
{
    // Marco con el cuadrado muestreado en el centro, mayor cuantos más píxeles.
    return drawn([pixels](QPainter& p, QRectF r) {
        const QRectF frame = r.adjusted(1.5, 1.5, -1.5, -1.5);
        p.setPen(QPen(fg(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(frame);
        const double side = std::min(frame.width() - 4, 2.5 + std::log2(double(std::max(1, pixels))) * 1.6);
        p.setPen(Qt::NoPen);
        p.setBrush(accent());
        p.drawRect(QRectF(frame.center().x() - side / 2, frame.center().y() - side / 2, side, side));
    });
}

QIcon stepper(bool plus)
{
    return drawn([plus](QPainter& p, QRectF r) {
        const QRectF box = r.adjusted(1.5, 1.5, -1.5, -1.5);
        p.setPen(QPen(fg(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box);
        const QPointF c = box.center();
        const double arm = box.width() * 0.28;
        p.setPen(QPen(fg(), 1.5));
        p.drawLine(QPointF(c.x() - arm, c.y()), QPointF(c.x() + arm, c.y()));
        if (plus)
            p.drawLine(QPointF(c.x(), c.y() - arm), QPointF(c.x(), c.y() + arm));
    });
}

QIcon blendMode()
{
    // Matraz, como el de Paint.NET.
    return drawn([](QPainter& p, QRectF r) {
        QPainterPath flask;
        const qreal cx = r.center().x();
        flask.moveTo(cx - 3, r.top());
        flask.lineTo(cx - 3, r.top() + r.height() * 0.38);
        flask.lineTo(r.left() + 1, r.bottom());
        flask.lineTo(r.right() - 1, r.bottom());
        flask.lineTo(cx + 3, r.top() + r.height() * 0.38);
        flask.lineTo(cx + 3, r.top());
        flask.closeSubpath();
        QPainterPath liquid;
        liquid.addRect(QRectF(r.left(), r.top() + r.height() * 0.62, r.width(), r.height()));
        p.fillPath(flask.intersected(liquid), accent());
        p.setPen(QPen(fg(), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(flask);
    });
}

QIcon selectionQuality(bool antialias)
{
    return drawn([antialias](QPainter& p, QRectF r) {
        p.setRenderHint(QPainter::Antialiasing, antialias);
        QPainterPath path;
        path.addEllipse(r.adjusted(2, 2, -2, -2));
        p.fillPath(path, QColor(accent().red(), accent().green(), accent().blue(), 120));
        p.setPen(QPen(fg(), 1, Qt::DashLine));
        p.drawPath(path);
    });
}

QIcon selectionMode(SelectionMode mode)
{
    return drawn([mode](QPainter& p, QRectF r) {
        const QRectF a(r.left(), r.top(), r.width() * 0.62, r.height() * 0.62);
        const QRectF b(r.right() - r.width() * 0.62, r.bottom() - r.height() * 0.62, r.width() * 0.62, r.height() * 0.62);
        QPainterPath pa, pb;
        pa.addRect(a);
        pb.addRect(b);
        QPainterPath fill;
        switch (mode) {
        case SelectionMode::Replace: fill = pb; break;
        case SelectionMode::Union: fill = pa.united(pb); break;
        case SelectionMode::Exclude: fill = pa.subtracted(pb); break;
        case SelectionMode::Intersect: fill = pa.intersected(pb); break;
        case SelectionMode::Xor: fill = pa.subtracted(pb).united(pb.subtracted(pa)); break;
        }
        p.fillPath(fill, accent());
        p.setPen(QPen(fg(), 1, Qt::DashLine));
        p.drawRect(a);
        p.drawRect(b);
    });
}

QIcon dash(Qt::PenStyle style)
{
    return drawn([style](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 2.5, style, Qt::FlatCap));
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    });
}

QIcon floodMode(bool global)
{
    return drawn([global](QPainter& p, QRectF r) {
        p.setRenderHint(QPainter::Antialiasing, false);
        const qreal c = r.width() / 4;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                const bool same = (i + j) % 3 == 0 || (i < 2 && j < 2);
                const bool filled = global ? same : (i < 2 && j < 2) || (i == 2 && j == 0);
                p.fillRect(QRectF(r.left() + i * c, r.top() + j * c, c - 1, c - 1),
                           filled ? accent() : (same ? fg() : QColor(128, 128, 128, 80)));
            }
    });
}

QIcon toleranceAlpha(bool straight)
{
    return drawn([straight](QPainter& p, QRectF r) {
        p.fillRect(r, QBrush(Raster::checkerboard(4)));
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0, QColor(220, 40, 40, 255));
        g.setColorAt(1, QColor(220, 40, 40, 0));
        p.fillRect(r.adjusted(0, 0, 0, -r.height() / 2), g);
        QLinearGradient g2(r.topLeft(), r.topRight());
        g2.setColorAt(0, QColor(40, 80, 220, 255));
        g2.setColorAt(1, straight ? QColor(40, 80, 220, 0) : QColor(220, 40, 40, 0));
        p.fillRect(r.adjusted(0, r.height() / 2, 0, 0), g2);
        p.setPen(QPen(fg(), 1));
        p.drawRect(r);
    });
}

QIcon textAlign(TextAlign align)
{
    switch (align) {
    case TextAlign::Left: return theme({"format-justify-left"});
    case TextAlign::Center: return theme({"format-justify-center"});
    case TextAlign::Right: return theme({"format-justify-right"});
    }
    return {};
}

QIcon swatch(const QColor& color)
{
    return drawn([color](QPainter& p, QRectF r) {
        p.fillRect(r, QBrush(Raster::checkerboard(4)));
        p.fillRect(r, color);
        p.setPen(QPen(fg(), 1));
        p.drawRect(r);
    });
}

QIcon cloneStamp()
{
    // Breeze no tiene un icono de tampón: se dibuja uno.
    return drawn([](QPainter& p, QRectF r) {
        const qreal s = r.width() / 26.0;
        p.translate(r.topLeft());
        p.scale(s, s);
        p.setPen(Qt::NoPen);
        p.setBrush(fg());
        p.drawEllipse(QRectF(8, 0, 10, 9));
        p.drawRect(QRectF(11, 8, 4, 7));
        p.drawRoundedRect(QRectF(3, 14, 20, 6), 1.5, 1.5);
        p.setBrush(accent());
        p.drawRect(QRectF(2, 22, 22, 3));
    });
}

QIcon tool(const QString& name, const QIcon& fallback)
{
    if (QIcon icon = pdn(name); !icon.isNull())
        return icon;
    // Iconos de img/tools incluidos como recursos al compilar (24 px y 48 px para HiDPI).
    QIcon icon;
    for (const QString& ext : {QStringLiteral("svg"), QStringLiteral("png")}) {
        const QString base = QStringLiteral(":/kaint/tools/%1").arg(name);
        if (QFile::exists(base + QLatin1Char('.') + ext))
            icon.addFile(base + QLatin1Char('.') + ext);
        if (QFile::exists(base + QStringLiteral("@2x.") + ext))
            icon.addFile(base + QStringLiteral("@2x.") + ext);
    }
    return icon.isNull() ? fallback : icon;
}

QIcon app()
{
    // El icono de img/ (o el de emergencia de data/) va incluido en el programa.
    QIcon icon;
    for (int size : {16, 22, 24, 32, 48, 64, 128, 256, 512}) {
        const QString png = QStringLiteral(":/kaint/icons/kaint-%1.png").arg(size);
        if (QFile::exists(png))
            icon.addFile(png, QSize(size, size));
    }
    if (QFile::exists(QStringLiteral(":/kaint/icons/kaint.svg")))
        icon.addFile(QStringLiteral(":/kaint/icons/kaint.svg"));
    if (!icon.isNull())
        return icon;
    return QIcon::fromTheme(QStringLiteral("io.github.kaint.Kaint"), theme({"applications-graphics", "kolourpaint"}));
}

} // namespace Icons
