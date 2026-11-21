#include "app/tools/PaintTools.h"

#include "core/QuickShape.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "core/Document.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <cmath>

// ---------------------------------------------------------------------------

StrokeTool::StrokeTool(QObject* parent)
    : Tool(parent)
{
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(550);
    connect(&m_holdTimer, &QTimer::timeout, this, &StrokeTool::snapShape);
    // Al cambiar la simetría, las guías se ven al momento.
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, [this] {
        if (canvas() && usesSymmetry())
            repaint();
    });
}

void StrokeTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (!canStart(e))
        return;
    StrokeParams p = params(e);
    applySymmetry(p);
    m_engine.begin(doc, doc->activeLayerPtr(), p);
    m_engine.moveTo(e.pos, e.pressure);
    m_snapped = false;
    m_raw = {e.pos};
    m_holdPos = e.pos;
    if (usesSymmetry() && AppState::instance()->tools().quickShape)
        m_holdTimer.start();
}

void StrokeTool::mouseMove(const ToolEvent& e)
{
    if (!m_engine.isActive())
        return;
    // Ya convertido en una forma: hasta soltar, no sigue.
    if (m_snapped)
        return;
    m_engine.lineTo(e.pos, e.pressure);
    m_raw << e.pos;
    // Quieto (moviéndose menos de unos píxeles en pantalla): sigue contando para la forma rápida.
    if (m_holdTimer.isActive() || (usesSymmetry() && AppState::instance()->tools().quickShape)) {
        const qreal scale = canvas() ? canvas()->scale() : 1.0;
        if (QLineF(m_holdPos, e.pos).length() * scale > 3) {
            m_holdPos = e.pos;
            m_holdTimer.start();
        }
    }
}

void StrokeTool::applySymmetry(StrokeParams& p) const
{
    if (!usesSymmetry())
        return;
    const ToolSettings& s = AppState::instance()->tools();
    p.symmetry = s.symmetry;
    p.symmetryCount = s.symmetryCount;
}

void StrokeTool::snapShape()
{
    if (!m_engine.isActive() || m_snapped || m_raw.size() < 3)
        return;
    const QuickShape::Result shape = QuickShape::recognize(m_raw);
    if (shape.kind == QuickShape::Kind::None)
        return;
    m_engine.replaceWith(shape.points);
    m_snapped = true;
    notice(QuickShape::name(shape.kind));
    repaint();
}

void StrokeTool::paintOverlay(QPainter& p)
{
    const ToolSettings& s = AppState::instance()->tools();
    Document* doc = document();
    if (!usesSymmetry() || s.symmetry == 0 || !doc || !canvas())
        return;
    const QSizeF size(doc->size());
    const QPointF c(size.width() / 2, size.height() / 2);
    QList<QLineF> lines;
    if (s.symmetry == 1 || s.symmetry == 3)
        lines << QLineF(c.x(), 0, c.x(), size.height());
    if (s.symmetry == 2 || s.symmetry == 3)
        lines << QLineF(0, c.y(), size.width(), c.y());
    if (s.symmetry == 4) {
        const int n = std::clamp(s.symmetryCount, 2, 32);
        const qreal r = std::hypot(size.width(), size.height());
        for (int i = 0; i < n; ++i) {
            const qreal a = 2 * M_PI * i / n - M_PI / 2;
            lines << QLineF(c, c + QPointF(std::cos(a), std::sin(a)) * r);
        }
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF image(canvas()->imageToView(QPointF(0, 0)), canvas()->imageToView(QPointF(size.width(), size.height())));
    p.setClipRect(image.normalized());
    for (const QLineF& l : std::as_const(lines)) {
        const QLineF v(canvas()->imageToView(l.p1()), canvas()->imageToView(l.p2()));
        p.setPen(QPen(QColor(255, 255, 255, 170), 3));
        p.drawLine(v);
        p.setPen(QPen(QColor(30, 130, 230, 220), 1.2, Qt::DashLine));
        p.drawLine(v);
    }
    p.restore();
}

void StrokeTool::mouseRelease(const ToolEvent&)
{
    finish();
}

void StrokeTool::finish()
{
    m_holdTimer.stop();
    m_snapped = false;
    m_raw.clear();
    if (m_engine.isActive())
        m_engine.end(name(), historyIcon());
}

void StrokeTool::deactivate()
{
    finish();
    Tool::deactivate();
}

qreal StrokeTool::brushOutlineDiameter() const
{
    return AppState::instance()->tools().brushSize;
}

StrokeParams StrokeTool::baseParams() const
{
    const ToolSettings& s = AppState::instance()->tools();
    StrokeParams p;
    p.diameter = s.brushSize;
    p.hardness = s.hardness / 100.0;
    p.antialias = s.antialias;
    p.pressureSize = s.pressure;
    p.spacing = s.spacing / 100.0;
    p.smoothing = s.smoothing;
    if (s.advancedBrush) {
        p.advanced = true;
        p.tipShape = s.tipShape;
        p.angle = s.tipAngle;
        p.roundness = s.tipRoundness / 100.0;
        p.flow = s.flow / 100.0;
        p.sizeJitter = s.sizeJitter / 100.0;
        p.opacityJitter = s.opacityJitter / 100.0;
        p.angleJitter = s.angleJitter / 100.0;
        p.scatter = s.scatter / 100.0;
        p.pressureGamma = s.pressureCurve / 100.0;
        p.minSize = s.minSize / 100.0;
        p.pressureOpacity = s.pressureOpacity;
        p.stabilizer = s.stabilizer;
        p.taperIn = s.taperIn;
        p.taperOut = s.taperOut;
        p.speedPressure = s.speedPressure;
    }
    return p;
}

// ---------------------------------------------------------------------------

QIcon BrushTool::icon() const { return Icons::tool(QStringLiteral("paintbrush"), Icons::theme({"draw-brush", "tool_brush"})); }

StrokeParams BrushTool::params(const ToolEvent& e) const
{
    const ToolSettings& s = AppState::instance()->tools();
    const AppState* app = AppState::instance();
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Paint;
    p.blend = s.blend;
    p.fill = s.fill;
    const bool secondary = e.button == Qt::RightButton;
    p.color = secondary ? app->secondary() : app->primary();
    p.background = secondary ? app->primary() : app->secondary();
    return p;
}

QIcon EraserTool::icon() const { return Icons::tool(QStringLiteral("eraser"), Icons::theme({"draw-eraser", "tool_eraser"})); }

StrokeParams EraserTool::params(const ToolEvent& e) const
{
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Erase;
    const AppState* app = AppState::instance();
    p.color = e.button == Qt::RightButton ? app->secondary() : app->primary();
    return p;
}

QIcon PencilTool::icon() const { return Icons::tool(QStringLiteral("pencil"), Icons::theme({"draw-freehand", "tool_pen"})); }

StrokeParams PencilTool::params(const ToolEvent& e) const
{
    const AppState* app = AppState::instance();
    StrokeParams p;
    p.kind = StrokeKind::Paint;
    p.pixelExact = true;
    p.antialias = false;
    p.pressureSize = false;
    p.diameter = 1;
    p.blend = app->tools().blend;
    p.color = e.button == Qt::RightButton ? app->secondary() : app->primary();
    p.smoothing = false;
    return p;
}


// ---------------------------------------------------------------------------

QIcon CloneStampTool::icon() const { return Icons::tool(QStringLiteral("clonestamp"), Icons::theme({"edit-clone"})); }

QString CloneStampTool::hint() const
{
    return m_hasSource ? tr("Pinta para copiar desde el origen (puede estar en otra capa). Ctrl+clic: elegir otro origen.")
                       : tr("Ctrl+clic para elegir el punto de origen y luego pinta donde quieras copiarlo.");
}

bool CloneStampTool::canStart(const ToolEvent& e)
{
    Document* doc = document();
    if (e.modifiers & Qt::ControlModifier) {
        m_source = e.pos;
        m_hasSource = true;
        m_hasOffset = false;
        m_sourceLayer = doc->activeLayerPtr();
        m_sourceDoc = doc;
        emit hintChanged(hint());
        repaint();
        return false;
    }
    const LayerPtr src = m_sourceLayer.lock();
    if (m_hasSource && (m_sourceDoc != doc || !src || doc->indexOf(src.get()) < 0)) {
        m_hasSource = false;
        emit hintChanged(hint());
    }
    if (!m_hasSource) {
        // Sin punto de origen no hay nada que copiar: se avisa junto al puntero.
        const QString missing = tr("Primero Ctrl+clic para elegir el punto de origen.");
        emit hintChanged(missing);
        notice(missing);
        return false;
    }
    if (!m_hasOffset) {
        const QPointF d = m_source - e.pos;
        m_offset = QPoint(int(std::round(d.x())), int(std::round(d.y())));
        m_hasOffset = true;
    }
    return true;
}

StrokeParams CloneStampTool::params(const ToolEvent& e) const
{
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Clone;
    p.blend = AppState::instance()->tools().blend;
    p.cloneOffset = m_offset;
    p.color = e.button == Qt::RightButton ? AppState::instance()->secondary() : AppState::instance()->primary();
    const LayerPtr src = m_sourceLayer.lock();
    if (src && document() && src != document()->activeLayerPtr())
        p.cloneSource = src->image;
    return p;
}

void CloneStampTool::mouseMove(const ToolEvent& e)
{
    m_hover = e.pos;
    StrokeTool::mouseMove(e);
    if (m_hasSource)
        repaint();
}

void CloneStampTool::paintOverlay(QPainter& p)
{
    if (!m_hasSource)
        return;
    const QPointF src = m_hasOffset ? m_hover + QPointF(m_offset) : m_source;
    const QPointF v = canvas()->imageToView(src);
    const qreal r = std::max(4.0, AppState::instance()->tools().brushSize * canvas()->scale() / 2.0);
    p.setRenderHint(QPainter::Antialiasing);
    for (const QColor& c : {QColor(255, 255, 255, 200), QColor(0, 0, 0, 200)}) {
        p.setPen(QPen(c, 0, c == Qt::black ? Qt::DashLine : Qt::SolidLine));
        p.drawEllipse(v, r, r);
        p.drawLine(v - QPointF(6, 0), v + QPointF(6, 0));
        p.drawLine(v - QPointF(0, 6), v + QPointF(0, 6));
    }
}

// ---------------------------------------------------------------------------

QIcon RecolorTool::icon() const { return Icons::tool(QStringLiteral("recolor"), Icons::theme({"tool_color_eraser", "color-mode-hue-shift-positive"})); }

QString RecolorTool::hint() const
{
    return AppState::instance()->tools().recolorSampling == RecolorSampling::Once
             ? tr("Pinta para cambiar los tonos parecidos al del primer clic por el color principal (clic derecho: secundario).")
             : tr("Clic izquierdo: cambia los tonos parecidos al color secundario por el principal · clic derecho: al revés.");
}

StrokeParams RecolorTool::params(const ToolEvent& e) const
{
    const AppState* app = AppState::instance();
    StrokeParams p = baseParams();
    p.kind = StrokeKind::Recolor;
    p.tolerance = app->tools().tolerance / 100.0;
    p.toleranceStraight = app->tools().toleranceStraight;
    p.recolorSampleOnce = app->tools().recolorSampling == RecolorSampling::Once;
    const bool secondary = e.button == Qt::RightButton;
    p.color = secondary ? app->secondary() : app->primary();
    p.background = secondary ? app->primary() : app->secondary();
    return p;
}
