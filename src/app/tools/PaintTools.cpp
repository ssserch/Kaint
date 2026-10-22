#include "app/tools/PaintTools.h"

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

void StrokeTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (!canStart(e))
        return;
    m_engine.begin(doc, doc->activeLayerPtr(), params(e));
    m_engine.moveTo(e.pos, e.pressure);
}

void StrokeTool::mouseMove(const ToolEvent& e)
{
    if (m_engine.isActive())
        m_engine.lineTo(e.pos, e.pressure);
}

void StrokeTool::mouseRelease(const ToolEvent&)
{
    finish();
}

void StrokeTool::finish()
{
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

QIcon CloneStampTool::icon() const { return Icons::tool(QStringLiteral("clonestamp"), Icons::cloneStamp()); }

QString CloneStampTool::hint() const
{
    return m_hasSource ? tr("Pinta para copiar desde el origen (puede estar en otra capa). Ctrl+clic: elegir otro origen.")
                       : tr("Ctrl+clic para elegir el punto de origen y luego pinta donde quieras copiarlo.");
}

QCursor CloneStampTool::cursor() const
{
    return QCursor(Qt::CrossCursor);
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
    if (!m_hasSource)
        return false;
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
