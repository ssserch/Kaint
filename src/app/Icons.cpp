#include "app/Icons.h"
#include "app/Theme.h"

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
    // Como las figuras del menú de Paint.NET: rellenas de azul con el contorno azul claro, y los
    // polígonos con su número de lados.
    return drawn([type](QPainter& p, QRectF r) {
        p.setPen(QPen(QColor(0xb9, 0xdc, 0xff), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(0x4f, 0x86, 0xb8));
        p.drawPath(shapePath(type, r.adjusted(-1, 0, 1, 0), 5));
        const int sides = type == ShapeType::Pentagon ? 5 : type == ShapeType::Hexagon ? 6 : type == ShapeType::Heptagon ? 7
                          : type == ShapeType::Octagon ? 8 : 0;
        if (sides) {
            QFont f = p.font();
            f.setBold(true);
            f.setPixelSize(11);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(r.adjusted(0, 1, 0, 1), Qt::AlignCenter, QString::number(sides));
        }
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

namespace {

// − y + de la barra de opciones. Con el estilo de Paint.NET y un tema oscuro, como los suyos: un
// cuadrado blanco con el signo oscuro; si no, los PNG de siempre. Se decide al dibujar (el tema
// puede cambiar después de crear el botón).
class StepperIconEngine : public QIconEngine {
public:
    StepperIconEngine(bool plus, QIcon fallback)
        : m_plus(plus)
        , m_fallback(std::move(fallback))
    {
    }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        if (!Theme::paintNetLook() || !Theme::isDark()) {
            m_fallback.paint(p, rect, Qt::AlignCenter, mode, state);
            return;
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal side = std::min(rect.width(), rect.height()) - 1;
        const QRectF box(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0, side, side);
        const bool off = mode == QIcon::Disabled;
        p->setPen(Qt::NoPen);
        // Con el puntero encima, el cuadrado se vuelve azul (sin recuadro alrededor del botón).
        const bool hot = mode == QIcon::Active || mode == QIcon::Selected;
        p->setBrush(off ? QColor(150, 150, 150) : hot ? QColor(0x00, 0x78, 0xd7) : QColor(Qt::white));
        p->drawRoundedRect(box, 1.5, 1.5);
        p->setPen(QPen(hot ? QColor(Qt::white) : QColor(0x1e, 0x1e, 0x1e), std::max<qreal>(1.4, side / 8.0), Qt::SolidLine,
                       Qt::FlatCap));
        const QPointF c = box.center();
        const qreal arm = side * 0.28;
        p->drawLine(c - QPointF(arm, 0), c + QPointF(arm, 0));
        if (m_plus)
            p->drawLine(c - QPointF(0, arm), c + QPointF(0, arm));
        p->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(), size), mode, state);
        return pm;
    }
    QIconEngine* clone() const override { return new StepperIconEngine(m_plus, m_fallback); }

private:
    bool m_plus;
    QIcon m_fallback;
};

} // namespace

QIcon magnet()
{
    // Una herradura roja con las puntas grises, dibujada a cualquier tamaño.
    QPixmap pm(QSize(32, 32));
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(16, 16);
    p.rotate(-45);
    QPainterPath u;
    u.moveTo(-9, -12);
    u.lineTo(-9, 2);
    u.arcTo(QRectF(-9, -7, 18, 18), 180, 180);
    u.lineTo(9, -12);
    p.setPen(QPen(QColor(40, 40, 40), 9.5, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(u);
    p.setPen(QPen(QColor(220, 50, 50), 7, Qt::SolidLine, Qt::FlatCap));
    p.drawPath(u);
    p.setPen(QPen(QColor(200, 205, 210), 7, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(-9, -12), QPointF(-9, -6));
    p.drawLine(QPointF(9, -12), QPointF(9, -6));
    p.end();
    return QIcon(pm);
}

QIcon stepper(bool plus)
{
    return QIcon(new StepperIconEngine(plus, png(plus ? QStringLiteral("plus_button_icon") : QStringLiteral("minus_button_icon"))));
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

namespace {

// Estilo de guion: una raya de 2 px a lo ancho del icono (más ancho que alto, como los de paint.js,
// así se distingue el dibujo), del color del texto del tema.
class DashIconEngine : public QIconEngine {
public:
    explicit DashIconEngine(Qt::PenStyle style) : m_style(style) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        QColor c = fg();
        if (mode == QIcon::Disabled)
            c.setAlphaF(0.4);
        // Largo de cada trazo y de cada hueco, en píxeles.
        QList<int> pattern;
        switch (m_style) {
        case Qt::DashLine: pattern = {6, 3}; break;
        case Qt::DotLine: pattern = {2, 2}; break;
        case Qt::DashDotLine: pattern = {6, 2, 2, 2}; break;
        case Qt::DashDotDotLine: pattern = {6, 2, 2, 2, 2, 2}; break;
        default: break;
        }
        const int y = rect.center().y();
        const int x0 = rect.left() + 1, x1 = rect.right() - 1;
        if (pattern.isEmpty()) {
            painter->fillRect(QRect(x0, y, x1 - x0 + 1, 2), c);
        } else {
            int x = x0;
            for (int i = 0; x <= x1; i = (i + 1) % int(pattern.size())) {
                const int len = pattern[i];
                if (i % 2 == 0)
                    painter->fillRect(QRect(x, y, std::min(len, x1 - x + 1), 2), c);
                x += len;
            }
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(), size), mode, state);
        return pm;
    }

    QIconEngine* clone() const override { return new DashIconEngine(m_style); }

private:
    Qt::PenStyle m_style;
};

} // namespace

QIcon dash(Qt::PenStyle style)
{
    return QIcon(new DashIconEngine(style));
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

namespace {
QIcon g_programIcon;
QString g_programTitle;
} // namespace

QIcon app()
{
    return g_programIcon.isNull() ? kaint() : g_programIcon;
}

void setProgram(const QString& iconFile, const QString& title)
{
    g_programIcon = QIcon();
    if (!iconFile.isEmpty()) {
        const QPixmap pm(iconFile);
        if (!pm.isNull())
            g_programIcon = QIcon(pm);
    }
    g_programTitle = title.trimmed();
}

QString programTitle()
{
    return g_programTitle.isEmpty() ? QStringLiteral("Kaint") : g_programTitle;
}

QIcon kaint()
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

namespace {

class RotatedIconEngine : public QIconEngine {
public:
    explicit RotatedIconEngine(QIcon source)
        : m_source(std::move(source))
    {
    }

    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        p->save();
        p->translate(QRectF(rect).center());
        p->rotate(90);
        p->translate(-QRectF(rect).center());
        m_source.paint(p, rect, Qt::AlignCenter, mode, state);
        p->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        const QPixmap pm = m_source.pixmap(size, mode, state);
        QPixmap out = pm.transformed(QTransform().rotate(90));
        out.setDevicePixelRatio(pm.devicePixelRatio());
        return out;
    }

    QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return m_source.actualSize(size, mode, state);
    }

    QIconEngine* clone() const override { return new RotatedIconEngine(m_source); }

private:
    QIcon m_source;
};

} // namespace

QIcon rotated(const QIcon& icon)
{
    return QIcon(new RotatedIconEngine(icon));
}

namespace {

// Un icono dibujado a 16, 24, 32 y 48 píxeles (y al doble para pantallas de alta densidad).
QIcon drawnIcon(const std::function<void(QPainter&, qreal)>& draw)
{
    QIcon icon;
    for (int side : {16, 24, 32, 48, 64, 96}) {
        QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(side / 16.0, side / 16.0);
        draw(p, side / 16.0);
        p.end();
        icon.addPixmap(QPixmap::fromImage(image));
    }
    return icon;
}

} // namespace

QIcon wineIcon()
{
    static const QIcon icon = drawnIcon([](QPainter& p, qreal scale) {
        const qreal line = std::max(0.75, 1.0 / scale);
        // La copa: un cuenco redondeado, el pie y la base.
        QPainterPath bowl;
        bowl.moveTo(3.6, 1.2);
        bowl.lineTo(12.4, 1.2);
        bowl.cubicTo(12.8, 5.2, 11.6, 8.6, 8.0, 9.0);
        bowl.cubicTo(4.4, 8.6, 3.2, 5.2, 3.6, 1.2);
        bowl.closeSubpath();
        // El vino (la mitad de abajo del cuenco), con un brillo.
        QPainterPath wine;
        wine.addRect(QRectF(0, 4.4, 16, 6));
        p.fillPath(bowl.intersected(wine), QColor(150, 18, 48));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 110));
        p.drawEllipse(QRectF(9.4, 2.2, 1.4, 3.0));
        const QColor glass(118, 128, 140);
        p.setPen(QPen(glass, line));
        p.setBrush(Qt::NoBrush);
        p.drawPath(bowl);
        p.setPen(QPen(glass, 1.2 * line, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(8.0, 9.0), QPointF(8.0, 13.6));
        p.drawLine(QPointF(5.0, 14.4), QPointF(11.0, 14.4));
    });
    return icon;
}

QIcon openD2DIcon()
{
    static const QIcon icon = drawnIcon([](QPainter& p, qreal scale) {
        const qreal line = std::max(0.75, 1.0 / scale);
        // Un cuadrado azul y un círculo naranja superpuestos (con la mezcla de los dos donde se tocan).
        const QRectF square(1.2, 1.2, 9.2, 9.2);
        const QRectF circle(5.6, 5.6, 9.2, 9.2);
        p.setPen(QPen(QColor(16, 86, 170), line));
        p.setBrush(QColor(38, 132, 230));
        p.drawRoundedRect(square, 1.4, 1.4);
        p.setPen(QPen(QColor(196, 92, 10), line));
        p.setBrush(QColor(255, 146, 38, 230));
        p.drawEllipse(circle);
        QPainterPath a, b;
        a.addRoundedRect(square, 1.4, 1.4);
        b.addEllipse(circle);
        p.fillPath(a.intersected(b), QColor(122, 92, 200));
    });
    return icon;
}

} // namespace Icons
