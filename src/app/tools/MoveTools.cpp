#include "app/tools/MoveTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "core/Raster.h"

#include <QKeyEvent>
#include <QPainter>

#include <cmath>

namespace {
constexpr int kMergeMoveSelection = 0x4b53;

QPoint arrowDelta(QKeyEvent* e)
{
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return {};
    }
    return (e->modifiers() & Qt::ControlModifier) ? d * 10 : d;
}
} // namespace

QCursor transformCursor(TransformBox::Mode mode)
{
    switch (mode) {
    case TransformBox::Mode::Scale: return Qt::PointingHandCursor;
    case TransformBox::Mode::Pivot:
    case TransformBox::Mode::ResetPivot: return Qt::ArrowCursor;
    case TransformBox::Mode::Rotate: {
        static const QCursor rotate = [] {
            const QIcon icon = Icons::theme({"transform-rotate", "object-rotate-right"});
            return icon.isNull() ? QCursor(Qt::CrossCursor) : QCursor(icon.pixmap(QSize(22, 22)), 11, 11);
        }();
        return rotate;
    }
    default: return Qt::SizeAllCursor;
    }
}

// ---------------------------------------------------------------------------
// Mover píxeles seleccionados
// ---------------------------------------------------------------------------

QIcon MoveSelectedTool::icon() const { return Icons::tool(QStringLiteral("moveselectedpixels"), Icons::theme({"transform-move"})); }

QString MoveSelectedTool::hint() const
{
    return tr("Arrastra la selección para moverla. Arrastre los controles para cambiar el tamaño. Arrastra con el "
              "botón derecho del ratón, o justo fuera del borde, para rotar.");
}

FloatingPtr MoveSelectedTool::currentFloating(Document* doc)
{
    if (!doc)
        return nullptr;
    QUndoStack* stack = doc->undoStack();
    if (stack->index() <= 0)
        return nullptr;
    const auto* cmd = dynamic_cast<const MovePixelsCommand*>(stack->command(stack->index() - 1));
    if (!cmd || !cmd->floating() || cmd->floating()->layer != doc->activeLayerPtr() || cmd->floating()->base.size() != doc->size())
        return nullptr;
    return cmd->floating();
}

void MoveSelectedTool::startPaste(Document* doc, const QImage& image, QPoint pos, const QString& text)
{
    auto f = std::make_shared<FloatingPixels>();
    f->layer = doc->activeLayerPtr();
    f->original = f->layer->image;
    f->base = f->original;
    f->pixels = image.convertToFormat(kLayerFormat);
    f->origin = pos;
    f->selectionBase = Selection::fromRect(QRect(pos, image.size()), doc->size());
    f->interpolation = AppState::instance()->tools().moveInterpolation;
    f->gammaCorrect = AppState::instance()->tools().moveGamma;
    HistoryIcons::registerIcon(text, QStringLiteral("edit-paste"));
    doc->undoStack()->push(new MovePixelsCommand(doc, f, QTransform(), QTransform(), true, true, doc->selection(), text));
}

FloatingPtr MoveSelectedTool::lift(bool keepCopy)
{
    Document* doc = document();
    auto f = std::make_shared<FloatingPixels>();
    f->layer = doc->activeLayerPtr();
    f->original = f->layer->image;
    const Selection sel = doc->selection().isEmpty() ? Selection::fromRect(doc->rect(), doc->size()) : doc->selection();
    f->selectionBase = sel;
    f->pixels = Raster::extractSelection(f->original, sel);
    f->origin = sel.bounds().topLeft();
    f->base = f->original.copy();
    f->interpolation = AppState::instance()->tools().moveInterpolation;
    f->gammaCorrect = AppState::instance()->tools().moveGamma;
    if (!keepCopy)
        Raster::eraseSelection(f->base, sel);
    return f;
}

void MoveSelectedTool::boxForCurrentState(TransformBox& box) const
{
    Document* doc = document();
    if (m_active && m_float) {
        box.set(QRectF(m_float->pixels.rect()), m_float->pixelsToCanvas(m_current));
        return;
    }
    if (FloatingPtr f = currentFloating(doc)) {
        box.set(QRectF(f->pixels.rect()), f->pixelsToCanvas(f->transform));
        return;
    }
    const QRect b = doc->selection().isEmpty() ? doc->rect() : doc->selection().bounds();
    box.set(QRectF(QPointF(0, 0), QSizeF(b.size())), QTransform::fromTranslate(b.left(), b.top()));
}

void MoveSelectedTool::syncBox()
{
    Document* doc = document();
    boxForCurrentState(m_box);
    if (doc->selection() != m_pivotFor) {
        m_box.resetPivot();
        m_pivotFor = doc->selection();
    }
}

void MoveSelectedTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    m_selectionBefore = doc->selection();
    const bool copy = e.modifiers & Qt::ControlModifier;

    syncBox();
    int handle = -1;
    TransformBox::Mode mode = m_box.hit(e.viewPos, canvas()->imageToViewTransform(), e.button == Qt::RightButton, &handle);
    if (doc->selection().isEmpty() && !currentFloating(doc) && e.button == Qt::LeftButton) {
        mode = TransformBox::Mode::Translate;
        handle = -1;
    }
    if (mode == TransformBox::Mode::ResetPivot) {
        m_box.resetPivot();
        repaint();
        return;
    }
    if (mode == TransformBox::Mode::Pivot) {
        m_box.begin(mode, -1, e.pos);
        m_pivotDrag = true;
        m_active = true;
        return;
    }
    if (mode == TransformBox::Mode::None)
        mode = TransformBox::Mode::Translate;

    // Ctrl sobre píxeles ya flotantes deja una copia donde están.
    m_float = copy ? nullptr : currentFloating(doc);
    m_first = !m_float;
    if (m_first)
        m_float = lift(copy);
    m_startTransform = m_float->transform;
    m_current = m_startTransform;
    m_box.set(QRectF(m_float->pixels.rect()), m_float->pixelsToCanvas(m_startTransform));
    m_box.begin(mode, handle, e.pos);
    m_active = true;
}

void MoveSelectedTool::applyLive(const QTransform& t)
{
    Document* doc = document();
    QRect r = m_float->boundsFor(m_current) | m_float->boundsFor(t);
    if (m_first)
        r |= m_float->boundsFor(QTransform());
    m_float->render(t, r);
    m_current = t;
    doc->notifyPixelsChanged(m_float->layer.get(), r);
    doc->setSelection(m_float->selectionBase.transformed(t));
}

void MoveSelectedTool::mouseMove(const ToolEvent& e)
{
    if (!m_active) {
        if (!document())
            return;
        if (document()->selection().isEmpty() && !currentFloating(document())) {
            canvas()->setToolCursor(transformCursor(TransformBox::Mode::Translate));
            return;
        }
        syncBox();
        int handle = -1;
        const auto mode = m_box.hit(e.viewPos, canvas()->imageToViewTransform(), false, &handle);
        canvas()->setToolCursor(transformCursor(mode));
        return;
    }
    if (m_pivotDrag) {
        m_box.update(e.pos, e.modifiers);
        repaint();
        return;
    }
    const QTransform canvasMap = m_box.update(e.pos, e.modifiers);
    const QTransform t = QTransform::fromTranslate(-m_float->origin.x(), -m_float->origin.y()) * canvasMap;
    applyLive(t);
    if (m_box.mode() == TransformBox::Mode::Rotate)
        emit hintChanged(tr("Ángulo: %1°").arg(m_box.rotationDegrees(), 0, 'f', 2));
    repaint();
}

void MoveSelectedTool::push()
{
    Document* doc = document();
    HistoryIcons::registerIcon(name(), QStringLiteral("transform-move"));
    doc->undoStack()->push(new MovePixelsCommand(doc, m_float, m_startTransform, m_current, m_first, false,
                                                 m_selectionBefore, name()));
}

void MoveSelectedTool::mouseRelease(const ToolEvent&)
{
    if (!m_active)
        return;
    m_active = false;
    m_box.end();
    if (m_pivotDrag) {
        m_pivotDrag = false;
        repaint();
        return;
    }
    if (m_current != m_startTransform) {
        push();
    } else if (m_first) {
        Document* doc = document();
        m_float->layer->image = m_float->original;
        doc->notifyPixelsChanged(m_float->layer.get(), m_float->boundsFor(m_current));
        doc->setSelection(m_selectionBefore);
    }
    m_float.reset();
    m_pivotFor = document()->selection();
    emit hintChanged(hint());
    repaint();
}

void MoveSelectedTool::nudge(QPoint d)
{
    Document* doc = document();
    m_selectionBefore = doc->selection();
    m_float = currentFloating(doc);
    m_first = !m_float;
    if (m_first)
        m_float = lift(false);
    m_startTransform = m_float->transform;
    m_current = m_startTransform;
    applyLive(m_startTransform * QTransform::fromTranslate(d.x(), d.y()));
    push();
    m_float.reset();
    m_pivotFor = doc->selection();
    repaint();
}

bool MoveSelectedTool::keyPress(QKeyEvent* e)
{
    const QPoint d = arrowDelta(e);
    if (d.isNull())
        return false;
    Document* doc = document();
    if (doc && doc->activeLayer() && !m_active)
        nudge(d);
    return true;
}

void MoveSelectedTool::deactivate()
{
    if (m_active) {
        m_active = false;
        if (!m_pivotDrag && m_current != m_startTransform)
            push();
        m_pivotDrag = false;
        m_float.reset();
    }
    Tool::deactivate();
}

void MoveSelectedTool::paintOverlay(QPainter& p)
{
    Document* doc = document();
    if (!doc)
        return;
    if (!m_active && doc->selection().isEmpty() && !currentFloating(doc))
        return;
    if (!m_active)
        syncBox();
    m_box.draw(p, canvas()->imageToViewTransform(), true);
}

// ---------------------------------------------------------------------------
// Mover selección
// ---------------------------------------------------------------------------

QIcon MoveSelectionTool::icon() const { return Icons::tool(QStringLiteral("moveselection"), Icons::theme({"edit-select", "transform-move-horizontal"})); }

QString MoveSelectionTool::hint() const
{
    return tr("Arrastra la selección para moverla. Arrastre los controles para cambiar el tamaño. Arrastra con el "
              "botón derecho del ratón, o justo fuera del borde, para rotar.");
}

void MoveSelectionTool::syncBox()
{
    Document* doc = document();
    if (continuing())
        m_box.set(QRectF(m_base.bounds()), m_transform);
    else
        m_box.set(QRectF(doc->selection().bounds()), QTransform());
    if (doc->selection() != m_pivotFor) {
        m_box.resetPivot();
        m_pivotFor = doc->selection();
    }
}

bool MoveSelectionTool::continuing() const
{
    // Se sigue transformando la misma selección mientras nadie la cambie.
    Document* doc = document();
    return doc && !m_result.isEmpty() && doc->selection() == m_result;
}

void MoveSelectionTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (doc->selection().isEmpty())
        return;
    syncBox();
    int handle = -1;
    TransformBox::Mode mode = m_box.hit(e.viewPos, canvas()->imageToViewTransform(), e.button == Qt::RightButton, &handle);
    if (mode == TransformBox::Mode::ResetPivot) {
        m_box.resetPivot();
        repaint();
        return;
    }
    if (!continuing()) {
        m_base = doc->selection();
        m_transform = QTransform();
        m_result = m_base;
    }
    m_before = doc->selection();
    m_box.set(QRectF(m_base.bounds()), m_transform);
    if (mode == TransformBox::Mode::None)
        mode = TransformBox::Mode::Translate;
    m_pivotDrag = mode == TransformBox::Mode::Pivot;
    m_box.begin(mode, handle, e.pos);
    m_active = true;
}

void MoveSelectionTool::mouseMove(const ToolEvent& e)
{
    if (!m_active) {
        if (!document() || document()->selection().isEmpty())
            return;
        syncBox();
        int handle = -1;
        canvas()->setToolCursor(transformCursor(m_box.hit(e.viewPos, canvas()->imageToViewTransform(), false, &handle)));
        return;
    }
    if (m_pivotDrag) {
        m_box.update(e.pos, e.modifiers);
        repaint();
        return;
    }
    m_transform = m_box.update(e.pos, e.modifiers);
    m_result = m_base.transformed(m_transform);
    document()->setSelection(m_result);
    if (m_box.mode() == TransformBox::Mode::Rotate)
        emit hintChanged(tr("Ángulo: %1°").arg(m_box.rotationDegrees(), 0, 'f', 2));
    repaint();
}

void MoveSelectionTool::push()
{
    Document* doc = document();
    if (doc->selection() == m_before)
        return;
    HistoryIcons::registerIcon(name(), QStringLiteral("edit-select"));
    doc->undoStack()->push(new SelectionCommand(doc, m_before, doc->selection(), name(), kMergeMoveSelection));
}

void MoveSelectionTool::mouseRelease(const ToolEvent&)
{
    if (!m_active)
        return;
    m_active = false;
    m_box.end();
    if (m_pivotDrag) {
        m_pivotDrag = false;
        repaint();
        return;
    }
    push();
    m_pivotFor = document()->selection();
    emit hintChanged(hint());
    repaint();
}

bool MoveSelectionTool::keyPress(QKeyEvent* e)
{
    const QPoint d = arrowDelta(e);
    if (d.isNull())
        return false;
    Document* doc = document();
    if (!doc || doc->selection().isEmpty() || m_active)
        return true;
    if (!continuing()) {
        m_base = doc->selection();
        m_transform = QTransform();
    }
    m_before = doc->selection();
    m_transform = m_transform * QTransform::fromTranslate(d.x(), d.y());
    m_result = m_base.transformed(m_transform);
    doc->setSelection(m_result);
    push();
    m_pivotFor = doc->selection();
    repaint();
    return true;
}

void MoveSelectionTool::deactivate()
{
    if (m_active) {
        m_active = false;
        if (!m_pivotDrag)
            push();
        m_pivotDrag = false;
    }
    Tool::deactivate();
}

void MoveSelectionTool::paintOverlay(QPainter& p)
{
    Document* doc = document();
    if (!doc || doc->selection().isEmpty())
        return;
    if (!m_active)
        syncBox();
    m_box.draw(p, canvas()->imageToViewTransform(), true);
}
