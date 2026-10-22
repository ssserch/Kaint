#include "app/tools/FillTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/tools/Handles.h"
#include "core/FloodFill.h"
#include "core/Parallel.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QKeyEvent>
#include <QPainter>

#include <cmath>

namespace {

QPointF snapAngle(const QPointF& origin, const QPointF& p)
{
    const QPointF d = p - origin;
    const qreal len = std::hypot(d.x(), d.y());
    const qreal angle = std::round(std::atan2(d.y(), d.x()) / (M_PI / 12)) * (M_PI / 12);
    return origin + QPointF(std::cos(angle), std::sin(angle)) * len;
}

bool isFinishKey(QKeyEvent* e)
{
    return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape;
}

} // namespace

// ---------------------------------------------------------------------------
// Bote de pintura
// ---------------------------------------------------------------------------

PaintBucketTool::PaintBucketTool(QObject* parent)
    : Tool(parent)
{
    auto refresh = [this] {
        if (m_edit.isActive())
            render();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, refresh);
    connect(AppState::instance(), &AppState::colorsChanged, this, refresh);
}

QIcon PaintBucketTool::icon() const { return Icons::tool(QStringLiteral("paintbucket"), Icons::theme({"fill-color", "color-fill"})); }

QString PaintBucketTool::hint() const
{
    return m_edit.isActive()
             ? tr("Cambia la tolerancia o los colores, o arrastra el punto de origen. Intro: terminar.")
             : tr("Clic izquierdo: rellenar con el color principal · clic derecho: con el secundario. Mayús: modo global.");
}

QPointF PaintBucketTool::handlePos() const
{
    return Handles::moveIconPos(QRectF(canvas()->imageToView(QPointF(m_seed) + QPointF(0.5, 0.5)), QSizeF()));
}

void PaintBucketTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (m_edit.isActive() && e.button == Qt::LeftButton && Handles::hit(e.viewPos, handlePos())) {
        m_dragging = true;
        return;
    }
    finish();
    if (!doc->rect().contains(e.pixel()))
        return;
    const ToolSettings& s = AppState::instance()->tools();
    // Se muestrea una sola vez, antes de pintar, para que el propio relleno no cuente.
    m_sample = s.sampleImage ? canvas()->composite().copy() : doc->activeLayer()->image;
    m_seed = e.pixel();
    m_secondary = e.button == Qt::RightButton;
    m_shiftGlobal = e.modifiers & Qt::ShiftModifier;
    m_edit.begin(doc, doc->activeLayerPtr());
    render();
    emit hintChanged(hint());
}

void PaintBucketTool::mouseMove(const ToolEvent& e)
{
    if (!m_dragging)
        return;
    const QPoint p = e.pixel();
    if (p != m_seed && document()->rect().contains(p)) {
        m_seed = p;
        render();
    }
}

void PaintBucketTool::mouseRelease(const ToolEvent&)
{
    m_dragging = false;
}

void PaintBucketTool::render()
{
    const ToolSettings& s = AppState::instance()->tools();
    const AppState* app = AppState::instance();
    Raster::setSelectionAntialias(s.selectionAntialias);
    FloodFill::BucketOptions o;
    o.tolerance = s.tolerance / 100.0;
    o.contiguous = s.floodGlobal == m_shiftGlobal;
    o.toleranceStraight = s.toleranceStraight;
    o.antialias = s.antialias;
    o.blend = s.blend;
    o.pattern = s.fill;
    o.primary = m_secondary ? app->secondary() : app->primary();
    o.secondary = m_secondary ? app->primary() : app->secondary();
    o.sampleIsLayer = !s.sampleImage;
    m_edit.restorePrevious();
    const QRect r = FloodFill::paintBucket(m_edit.image(), m_edit.before(), m_sample, m_seed, o, document()->selection());
    m_edit.touched(r);
    repaint();
}

bool PaintBucketTool::keyPress(QKeyEvent* e)
{
    if (m_edit.isActive() && isFinishKey(e)) {
        finish();
        return true;
    }
    return false;
}

void PaintBucketTool::paintOverlay(QPainter& p)
{
    if (!m_edit.isActive())
        return;
    const QPointF seed = canvas()->imageToView(QPointF(m_seed) + QPointF(0.5, 0.5));
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(seed - QPointF(2, 2), QSizeF(4, 4)));
    Handles::draw(p, handlePos(), Handles::Shape::Move);
}

void PaintBucketTool::finish()
{
    if (!m_edit.isActive())
        return;
    m_edit.commit(name(), QStringLiteral("fill-color"));
    m_sample = QImage();
    m_dragging = false;
    emit hintChanged(hint());
    repaint();
}

void PaintBucketTool::deactivate()
{
    finish();
    Tool::deactivate();
}

// ---------------------------------------------------------------------------
// Degradado
// ---------------------------------------------------------------------------

GradientTool::GradientTool(QObject* parent)
    : Tool(parent)
{
    auto refresh = [this] {
        if (m_edit.isActive())
            render();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, refresh);
    connect(AppState::instance(), &AppState::colorsChanged, this, refresh);
}

QIcon GradientTool::icon() const { return Icons::tool(QStringLiteral("gradient"), Icons::theme({"color-gradient", "gradient"})); }

QString GradientTool::hint() const
{
    return m_edit.isActive()
             ? tr("Arrastra los extremos o el icono de mover; clic derecho en un extremo invierte los colores. Intro: terminar.")
             : tr("Arrastra para dibujar el degradado del color principal al secundario (clic derecho: al revés). "
                  "Mayús: ángulos de 15°.");
}

QPointF GradientTool::moveHandle() const
{
    const QPointF a = canvas()->imageToView(m_start), b = canvas()->imageToView(m_end);
    return (a + b) / 2 + QPointF(18, 18);
}

void GradientTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (m_edit.isActive()) {
        const bool onStart = Handles::hit(e.viewPos, canvas()->imageToView(m_start));
        const bool onEnd = Handles::hit(e.viewPos, canvas()->imageToView(m_end));
        if (e.button == Qt::RightButton && (onStart || onEnd)) {
            m_reversed = !m_reversed; // clic derecho en un extremo: intercambiar colores
            render();
            return;
        }
        if (onStart || onEnd) {
            m_drag = onStart ? Drag::Start : Drag::End;
            return;
        }
        if (Handles::hit(e.viewPos, moveHandle(), 11)) {
            m_drag = Drag::Both;
            m_grab = e.pos;
            return;
        }
        finish();
    }
    m_edit.begin(doc, doc->activeLayerPtr());
    m_start = m_end = e.pos;
    m_reversed = e.button == Qt::RightButton;
    m_drag = Drag::End;
}

void GradientTool::mouseMove(const ToolEvent& e)
{
    if (!m_edit.isActive() || m_drag == Drag::None)
        return;
    const bool snap = e.modifiers & Qt::ShiftModifier;
    switch (m_drag) {
    case Drag::Start: m_start = snap ? snapAngle(m_end, e.pos) : e.pos; break;
    case Drag::End: m_end = snap ? snapAngle(m_start, e.pos) : e.pos; break;
    case Drag::Both: {
        const QPointF d = e.pos - m_grab;
        m_start += d;
        m_end += d;
        m_grab = e.pos;
        break;
    }
    case Drag::None: break;
    }
    render();
    repaint();
}

void GradientTool::mouseRelease(const ToolEvent&)
{
    m_drag = Drag::None;
    emit hintChanged(hint());
}

bool GradientTool::keyPress(QKeyEvent* e)
{
    if (m_edit.isActive() && isFinishKey(e)) {
        finish();
        return true;
    }
    return false;
}

void GradientTool::finish()
{
    if (!m_edit.isActive())
        return;
    m_edit.commit(name(), QStringLiteral("color-gradient"));
    m_drag = Drag::None;
    emit hintChanged(hint());
    repaint();
}

void GradientTool::deactivate()
{
    finish();
    Tool::deactivate();
}

void GradientTool::render()
{
    Document* doc = m_edit.document();
    const ToolSettings& s = AppState::instance()->tools();
    const AppState* app = AppState::instance();
    Raster::setSelectionAntialias(s.selectionAntialias);
    const QPointF d = m_end - m_start;
    const qreal len2 = d.x() * d.x() + d.y() * d.y();
    if (len2 < 1.0) {
        m_edit.restorePrevious();
        m_edit.touched(QRect());
        return;
    }
    const qreal len = std::sqrt(len2);
    const QRect area = doc->selection().isEmpty() ? doc->rect() : doc->selection().bounds();
    const GradientType type = s.gradient;
    const GradientRepeat repeat = s.gradientRepeat;
    const QPointF start = m_start;

    auto shape = [](double t, GradientRepeat r) {
        switch (r) {
        case GradientRepeat::None: return std::clamp(t, 0.0, 1.0);
        case GradientRepeat::Wrapped: return t - std::floor(t);
        case GradientRepeat::Reflected: {
            const double m = std::fmod(std::abs(t), 2.0);
            return m <= 1 ? m : 2 - m;
        }
        }
        return t;
    };
    auto parameter = [&](qreal px, qreal py) {
        const qreal vx = px - start.x(), vy = py - start.y();
        const qreal dot = (vx * d.x() + vy * d.y()) / len2;
        const qreal cross = (vx * d.y() - vy * d.x()) / len2;
        switch (type) {
        case GradientType::Linear: return shape(dot, repeat);
        case GradientType::Reflected: return shape(std::abs(dot), repeat);
        case GradientType::Diamond: return shape(std::abs(dot) + std::abs(cross), repeat);
        case GradientType::Radial: return shape(std::hypot(vx, vy) / len, repeat);
        case GradientType::Conical: return std::abs(std::atan2(vx * d.y() - vy * d.x(), vx * d.x() + vy * d.y())) / M_PI;
        case GradientType::SpiralClockwise:
        case GradientType::SpiralCounterClockwise: {
            const double ang = std::atan2(vx * d.y() - vy * d.x(), vx * d.x() + vy * d.y()) / (2 * M_PI);
            const double t = std::hypot(vx, vy) / len + (type == GradientType::SpiralClockwise ? -ang : ang);
            return t - std::floor(t);
        }
        }
        return 0.0;
    };

    QColor c0 = app->primary(), c1 = app->secondary();
    if (m_reversed)
        std::swap(c0, c1);

    if (!s.gradientTransparency) {
        QImage overlay(area.size(), kLayerFormat);
        const Pixels out(overlay);
        parallelRows(0, area.height(), [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                QRgb* row = out.row(y);
                for (int x = 0; x < area.width(); ++x) {
                    const float t = float(parameter(area.left() + x + 0.5, area.top() + y + 0.5));
                    auto mix = [t](int a, int b) { return int(a + (b - a) * t + 0.5f); };
                    row[x] = qRgba(mix(c0.red(), c1.red()), mix(c0.green(), c1.green()), mix(c0.blue(), c1.blue()),
                                   mix(c0.alpha(), c1.alpha()));
                }
            }
        });
        m_edit.showOverlay(overlay, area.topLeft(), s.blend);
        return;
    }

    // Modo transparencia: del alfa del principal al inverso del alfa del secundario.
    const double a0 = c0.alpha() / 255.0, a1 = 1.0 - c1.alpha() / 255.0;
    m_edit.restorePrevious();
    const ConstPixels in(m_edit.before());
    const Pixels out(m_edit.image());
    const ConstMask sel(doc->selection().mask());
    const bool aa = s.selectionAntialias;
    parallelRows(area.top(), area.bottom() + 1, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* src = in.row(y);
            QRgb* dst = out.row(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                const double t = parameter(x + 0.5, y + 0.5);
                const QRgb faded = scaleAlpha(src[x], int((a0 + (a1 - a0) * t) * 255 + 0.5));
                dst[x] = sel.isNull() ? faded : lerpStraight(src[x], faded, Raster::clipCoverage(sel.row(y)[x], aa));
            }
        }
    });
    m_edit.touched(area);
}

void GradientTool::paintOverlay(QPainter& p)
{
    if (!m_edit.isActive())
        return;
    const QPointF a = canvas()->imageToView(m_start);
    const QPointF b = canvas()->imageToView(m_end);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 220), 3));
    p.drawLine(a, b);
    p.setPen(QPen(QColor(0, 0, 0, 220), 1));
    p.drawLine(a, b);
    const QColor first = m_reversed ? AppState::instance()->secondary() : AppState::instance()->primary();
    const QColor second = m_reversed ? AppState::instance()->primary() : AppState::instance()->secondary();
    // Anillo claro con borde oscuro y el color del extremo dentro, para que
    // se vea sobre cualquier fondo.
    const QColor dark(30, 30, 30, 230);
    for (const auto& [pos, color] : {std::pair{a, first}, std::pair{b, second}}) {
        p.setPen(QPen(dark, 1));
        p.setBrush(QColor(255, 255, 255, 240));
        p.drawEllipse(pos, 7, 7);
        p.setBrush(color);
        p.drawEllipse(pos, 4, 4);
    }
    if (m_drag == Drag::None)
        Handles::draw(p, moveHandle(), Handles::Shape::Move);
}
