#include "app/tools/ViewTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "core/Document.h"

#include <QLineF>
#include <QPainter>

#include <cmath>

namespace {

QCursor iconCursor(const QIcon& icon, QPoint hotspot, Qt::CursorShape fallback)
{
    if (icon.isNull())
        return QCursor(fallback);
    return QCursor(icon.pixmap(QSize(24, 24)), hotspot.x(), hotspot.y());
}

} // namespace

// ---------------------------------------------------------------------------

QIcon ZoomTool::icon() const { return Icons::tool(QStringLiteral("zoom"), Icons::theme({"zoom-in"})); }

QString ZoomTool::hint() const
{
    return tr("Clic izquierdo: acercar · clic derecho: alejar · arrastrar: acercar a un rectángulo.");
}

QCursor ZoomTool::cursor() const
{
    // El cursor es la lupa de paint.js (la del botón es la dibujada de Kaint).
    return iconCursor(Icons::named(QStringLiteral("zoom")), QPoint(9, 9), Qt::CrossCursor);
}

void ZoomTool::mousePress(const ToolEvent& e)
{
    m_pressed = true;
    m_dragging = false;
    m_start = m_current = e.pos;
    m_startView = e.viewPos;
}

QRect ZoomTool::dragRect() const
{
    // Esquinas en el borde de píxel más cercano y dentro del lienzo, como la selección
    // rectangular.
    if (!m_dragging || !document())
        return {};
    const QPoint a(int(std::lround(m_start.x())), int(std::lround(m_start.y())));
    const QPoint b(int(std::lround(m_current.x())), int(std::lround(m_current.y())));
    const QRect r(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())),
                  QSize(std::abs(b.x() - a.x()), std::abs(b.y() - a.y())));
    return r.intersected(document()->rect());
}

void ZoomTool::mouseMove(const ToolEvent& e)
{
    if (!m_pressed)
        return;
    m_current = e.pos;
    if (!m_dragging && QLineF(m_startView, e.viewPos).length() > 4) {
        m_dragging = true;
        canvas()->setOverlayAnts(true);
    }
    if (!m_dragging)
        return;
    const QRect r = dragRect();
    emit hintChanged(tr("Zum a: %1, %2 · %3 × %4").arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height()));
    repaint();
}

void ZoomTool::mouseRelease(const ToolEvent& e)
{
    if (!m_pressed)
        return;
    m_pressed = false;
    const QRect r = dragRect();
    const bool dragged = m_dragging;
    stopDragging();
    if (dragged && e.button == Qt::LeftButton && !r.isEmpty())
        canvas()->zoomToRect(QRectF(r));
    else if (e.button == Qt::LeftButton)
        canvas()->zoomIn(e.viewPos);
    else if (e.button == Qt::RightButton)
        canvas()->zoomOut(e.viewPos);
}

void ZoomTool::deactivate()
{
    m_pressed = false;
    stopDragging();
    Tool::deactivate();
}

void ZoomTool::stopDragging()
{
    if (!m_dragging)
        return;
    m_dragging = false;
    canvas()->setOverlayAnts(false);
    emit hintChanged(hint());
    repaint();
}

void ZoomTool::paintOverlay(QPainter& p)
{
    const QRect r = dragRect();
    if (!r.isEmpty())
        canvas()->drawMarchingRect(p, QRectF(r), true);
}

// ---------------------------------------------------------------------------

QIcon PanTool::icon() const { return Icons::tool(QStringLiteral("pan"), Icons::theme({"transform-browse", "hand"})); }

QCursor PanTool::cursor() const
{
    // La mano de la herramienta, abierta o agarrando mientras se arrastra.
    return Icons::handCursor(m_dragging);
}

void PanTool::mousePress(const ToolEvent& e)
{
    m_dragging = true;
    m_last = e.viewPos;
    canvas()->updateCursor();
}

void PanTool::mouseMove(const ToolEvent& e)
{
    if (!m_dragging)
        return;
    canvas()->scrollByView(m_last - e.viewPos);
    m_last = e.viewPos;
}

void PanTool::mouseRelease(const ToolEvent&)
{
    m_dragging = false;
    canvas()->updateCursor();
}

// ---------------------------------------------------------------------------

QIcon ColorPickerTool::icon() const { return Icons::tool(QStringLiteral("colorpicker"), Icons::theme({"color-picker"})); }

QString ColorPickerTool::hint() const
{
    return tr("Clic izquierdo: tomar el color principal · clic derecho: el secundario. "
              "Ctrl: muestrear la imagen en lugar de la capa (o al revés).");
}

QCursor ColorPickerTool::cursor() const
{
    // El cuentagotas de paint.js, como el de la lupa.
    return iconCursor(Icons::named(QStringLiteral("colorpicker")), QPoint(2, 21), Qt::CrossCursor);
}

void ColorPickerTool::pick(const ToolEvent& e, Qt::MouseButton button)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer())
        return;
    const ToolSettings& s = AppState::instance()->tools();
    const bool image = s.pickerSampleImage != bool(e.modifiers & Qt::ControlModifier);
    const QImage& src = image ? canvas()->composite() : doc->activeLayer()->image;
    const int half = std::max(0, s.pickerSize / 2);
    const QRect area = QRect(e.pixel() - QPoint(half, half), QSize(2 * half + 1, 2 * half + 1)).intersected(src.rect());
    if (area.isEmpty())
        return;
    const bool pm = src.format() == QImage::Format_ARGB32_Premultiplied;
    auto setColor = [button](const QColor& c) {
        if (button == Qt::RightButton)
            AppState::instance()->setSecondary(c);
        else
            AppState::instance()->setPrimary(c);
    };
    if (area.width() * area.height() == 1 && !pm) {
        setColor(QColor::fromRgba(src.pixel(area.topLeft())));
        return;
    }
    qint64 r = 0, g = 0, b = 0, a = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb p = pm ? row[x] : qPremultiply(row[x]);
            r += qRed(p), g += qGreen(p), b += qBlue(p), a += qAlpha(p);
        }
    }
    const qint64 n = qint64(area.width()) * area.height();
    setColor(QColor::fromRgba(qUnpremultiply(qRgba(int(r / n), int(g / n), int(b / n), int(a / n)))));
}

void ColorPickerTool::mousePress(const ToolEvent& e)
{
    if (e.button != Qt::LeftButton && e.button != Qt::RightButton)
        return;
    m_button = e.button;
    pick(e, m_button);
}

void ColorPickerTool::mouseMove(const ToolEvent& e)
{
    if (m_button != Qt::NoButton)
        pick(e, m_button);
}

void ColorPickerTool::mouseRelease(const ToolEvent&)
{
    if (m_button == Qt::NoButton)
        return;
    m_button = Qt::NoButton;
    switch (AppState::instance()->tools().pickerAfter) {
    case PickerAfter::DoNotSwitch: break;
    case PickerAfter::PreviousTool: emit switchBackRequested(); break;
    case PickerAfter::Pencil: emit switchToPencilRequested(); break;
    }
}
