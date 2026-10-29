#include "app/Icons.h"

#include "core/CustomShapes.h"
#include "core/FillPattern.h"
#include "core/Raster.h"

#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
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

bool themeInstalled(const QString& name)
{
    for (const QString& path : QIcon::themeSearchPaths())
        if (QFile::exists(path + QLatin1Char('/') + name + QStringLiteral("/index.theme")))
            return true;
    return false;
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
    paths.append(QStringLiteral(":/kaint/icon-themes"));
    QIcon::setThemeSearchPaths(paths);
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(breezeTheme(false));
    QIcon::setFallbackThemeName(breezeTheme(false));
}

QString breezeTheme(bool dark)
{
    const QString system = dark ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
    if (themeInstalled(system))
        return system;
    return dark ? QStringLiteral("kaint-breeze-dark") : QStringLiteral("kaint-breeze");
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

QIcon system(std::initializer_list<const char*> names)
{
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
    switch (type) {
    case GradientType::Linear: return png(QStringLiteral("enum_gradient_type_linear_clamped"));
    case GradientType::Reflected: return png(QStringLiteral("enum_gradient_type_linear_reflected"));
    case GradientType::Diamond: return png(QStringLiteral("enum_gradient_type_linear_diamond"));
    case GradientType::Radial: return png(QStringLiteral("enum_gradient_type_radial"));
    case GradientType::Conical: return png(QStringLiteral("enum_gradient_type_conical"));
    case GradientType::SpiralClockwise: return png(QStringLiteral("enum_gradient_type_spiral"));
    case GradientType::SpiralCounterClockwise: return png(QStringLiteral("enum_gradient_type_spiral_counter_clockwise"));
    }
    return {};
}

QIcon gradientRepeat(GradientRepeat repeat)
{
    switch (repeat) {
    case GradientRepeat::None: return png(QStringLiteral("enum_gradient_repeat_type_no_repeat"));
    case GradientRepeat::Wrapped: return png(QStringLiteral("enum_gradient_repeat_type_repeat_wrapped"));
    case GradientRepeat::Reflected: return png(QStringLiteral("enum_gradient_repeat_type_repeat_reflected"));
    }
    return {};
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
    switch (style) {
    case ShapeStyle::Outline: return png(QStringLiteral("enum_shape_draw_type_outline_dark"));
    case ShapeStyle::Fill: return png(QStringLiteral("enum_shape_draw_type_interior"));
    case ShapeStyle::OutlineFill: return png(QStringLiteral("enum_shape_draw_type_both_dark"));
    }
    return {};
}

QIcon curveType(CurveType type)
{
    switch (type) {
    case CurveType::Straight: return png(QStringLiteral("enum_curve_type_straight"));
    case CurveType::Spline: return png(QStringLiteral("enum_curve_type_spline"));
    case CurveType::Bezier: return png(QStringLiteral("enum_curve_type_bezier"));
    }
    return {};
}

QIcon lineCap(LineCap cap, bool start)
{
    const char* name = "flat";
    switch (cap) {
    case LineCap::Flat: name = "flat"; break;
    case LineCap::Round: name = "rounded"; break;
    case LineCap::Arrow: name = "arrow"; break;
    case LineCap::ArrowFilled: name = "arrow_filled"; break;
    }
    return png(QStringLiteral("enum_line_curve_cap_%1_%2").arg(QLatin1String(name), start ? QStringLiteral("start") : QStringLiteral("end")));
}

QIcon antialias(bool on)
{
    return png(on ? QStringLiteral("anti_aliasing_true") : QStringLiteral("anti_aliasing_false"));
}

QIcon smoothing(bool on)
{
    return png(on ? QStringLiteral("enum_input_path_smoothing_enabled") : QStringLiteral("enum_input_path_smoothing_disabled"));
}

QIcon eye(bool open)
{
    // Ojo de visibilidad: abierto con la pupila; cerrado, el párpado con pestañas.
    return drawn([open](QPainter& p, QRectF r) {
        const QPointF c = r.center();
        const qreal w = r.width() / 2, h = r.height() * 0.32;
        p.setPen(QPen(fg(), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath lower(QPointF(c.x() - w, c.y()));
        lower.quadTo(QPointF(c.x(), c.y() + 2 * h), QPointF(c.x() + w, c.y()));
        if (open) {
            QPainterPath upper(QPointF(c.x() - w, c.y()));
            upper.quadTo(QPointF(c.x(), c.y() - 2 * h), QPointF(c.x() + w, c.y()));
            p.drawPath(upper);
            p.drawPath(lower);
            p.setPen(Qt::NoPen);
            p.setBrush(fg());
            p.drawEllipse(c, h * 0.8, h * 0.8);
            return;
        }
        p.drawPath(lower);
        for (qreal t : {-0.6, 0.0, 0.6}) {
            const QPointF from(c.x() + t * w, c.y() + h * (1 - t * t) * 0.95);
            p.drawLine(from, from + QPointF(t * 2.5, 3.5));
        }
    });
}

QIcon gamma(bool corrected)
{
    // Como en paint.js: corregido es la mezcla lineal; sin corregir, la comprimida (sRGB).
    return png(corrected ? QStringLiteral("enum_tool_gamma_mode_linear") : QStringLiteral("enum_tool_gamma_mode_companded"));
}

QIcon fontMetric(FontMetric metric)
{
    return png(metric == FontMetric::Points ? QStringLiteral("enum_font_size_metric_points")
                                            : QStringLiteral("enum_font_size_metric_fixed96_dpi"));
}

QIcon recolorSampling(RecolorSampling sampling)
{
    return png(sampling == RecolorSampling::Once ? QStringLiteral("enum_recolor_tool_sampling_mode_once")
                                                 : QStringLiteral("enum_recolor_tool_sampling_mode_secondary_color"));
}

QIcon rectSelectMode(RectSelectMode mode)
{
    switch (mode) {
    case RectSelectMode::Normal: return png(QStringLiteral("enum_selection_draw_mode_normal"));
    case RectSelectMode::FixedRatio: return png(QStringLiteral("enum_selection_draw_mode_fixed_ratio"));
    case RectSelectMode::FixedSize: return png(QStringLiteral("enum_selection_draw_mode_fixed_size"));
    }
    return {};
}

QIcon afterSelect(AfterSelect after)
{
    if (after == AfterSelect::MoveSelection)
        return tool(QStringLiteral("moveselection"), theme({"edit-select"}));
    if (after == AfterSelect::MovePixels)
        return tool(QStringLiteral("moveselectedpixels"), theme({"transform-move"}));
    return drawn([](QPainter& p, QRectF r) {
        p.setPen(QPen(fg(), 1.5, Qt::DashLine));
        p.drawRect(r.adjusted(2, 4, -2, -4));
    });
}

QIcon sampleSize(int pixels)
{
    // Los tamaños de Paint.NET tienen su icono; otro tamaño, el marco con el cuadrado muestreado
    // en el centro, mayor cuantos más píxeles.
    if (pixels == 1)
        return png(QStringLiteral("enum_pixel_sample_mode_point_sample"));
    if (pixels == 3 || pixels == 5 || pixels == 11 || pixels == 31 || pixels == 51)
        return png(QStringLiteral("enum_pixel_sample_mode_average%1x%1").arg(pixels));
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
    return png(plus ? QStringLiteral("plus_button_icon") : QStringLiteral("minus_button_icon"));
}

QIcon blendMode()
{
    // Matraz, como el de Paint.NET.
    return png(QStringLiteral("alpha_blending_true"));
}

QIcon selectionQuality(bool antialias)
{
    return png(antialias ? QStringLiteral("enum_selection_rendering_quality_high_quality_antialiased")
                         : QStringLiteral("enum_selection_rendering_quality_aliased"));
}

QIcon selectionMode(SelectionMode mode)
{
    switch (mode) {
    case SelectionMode::Replace: return png(QStringLiteral("enum_selection_combine_mode_replace"));
    case SelectionMode::Union: return png(QStringLiteral("enum_selection_combine_mode_union"));
    case SelectionMode::Exclude: return png(QStringLiteral("enum_selection_combine_mode_exclude"));
    case SelectionMode::Intersect: return png(QStringLiteral("enum_selection_combine_mode_intersect"));
    case SelectionMode::Xor: return png(QStringLiteral("enum_selection_combine_mode_xor"));
    }
    return {};
}

QIcon dash(Qt::PenStyle style)
{
    switch (style) {
    case Qt::DashLine: return png(QStringLiteral("enum_dash_style_dash"));
    case Qt::DotLine: return png(QStringLiteral("enum_dash_style_dot"));
    case Qt::DashDotLine: return png(QStringLiteral("enum_dash_style_dash_dot"));
    case Qt::DashDotDotLine: return png(QStringLiteral("enum_dash_style_dash_dot_dot"));
    default: return png(QStringLiteral("enum_dash_style_solid"));
    }
}

QIcon floodMode(bool global)
{
    return png(global ? QStringLiteral("enum_flood_mode_global") : QStringLiteral("enum_flood_mode_local"));
}

QIcon toleranceAlpha(bool straight)
{
    return png(straight ? QStringLiteral("enum_tolerance_alpha_mode_straight")
                        : QStringLiteral("enum_tolerance_alpha_mode_premultiplied"));
}

QIcon fillAccumulate(bool on)
{
    // Dos manchas translúcidas: con acumulación, donde se solapan el color se refuerza.
    return drawn([on](QPainter& p, QRectF r) {
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = accent();
        c.setAlpha(150);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal d = r.width() * 0.62;
        const QRectF a(r.left() + 1, r.top() + 1, d, d), b(r.right() - 1 - d, r.bottom() - 1 - d, d, d);
        if (on) {
            p.drawEllipse(a);
            p.drawEllipse(b);
        } else {
            QPainterPath both;
            both.addEllipse(a);
            both.addEllipse(b);
            p.drawPath(both.simplified());
        }
        p.setPen(QPen(fg(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(a);
        p.drawEllipse(b);
    });
}

QIcon textAlign(TextAlign align)
{
    switch (align) {
    case TextAlign::Left: return png(QStringLiteral("enum_text_alignment_left"));
    case TextAlign::Center: return png(QStringLiteral("enum_text_alignment_center"));
    case TextAlign::Right: return png(QStringLiteral("enum_text_alignment_right"));
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
    return QIcon::fromTheme(QStringLiteral(KAINT_APP_ID), theme({"applications-graphics", "kolourpaint"}));
}

} // namespace Icons
