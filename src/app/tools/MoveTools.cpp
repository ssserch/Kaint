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

MoveSelectedTool::MoveSelectedTool(QObject* parent)
    : Tool(parent)
{
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, &MoveSelectedTool::applySampling);
}

void MoveSelectedTool::applySampling()
{
    // El muestreo y la corrección gamma de la barra se aplican al momento a lo que se mueve.
    Document* doc = document();
    if (!doc)
        return;
    QList<FloatingPtr> floating;
    if (m_active && m_float)
        floating << m_float << m_extra;
    else if (FloatingPtr f = currentFloating(doc))
        floating << f;
    const ToolSettings& s = AppState::instance()->tools();
    for (const FloatingPtr& f : std::as_const(floating)) {
        if (f->interpolation == s.moveInterpolation && f->gammaCorrect == s.moveGamma)
            continue;
        f->interpolation = s.moveInterpolation;
        f->gammaCorrect = s.moveGamma;
        const QTransform t = m_active ? m_current : f->transform;
        const QRect r = f->boundsFor(t);
        f->render(t, r);
        doc->notifyPixelsChanged(f->layer.get(), r);
    }
}

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
    f->replace = AppState::instance()->preferences().replacePixels;
    HistoryIcons::registerIcon(text, QStringLiteral("edit-paste"));
    doc->undoStack()->push(new MovePixelsCommand(doc, f, QTransform(), QTransform(), true, true, doc->selection(), text));
}

FloatingPtr MoveSelectedTool::lift(bool keepCopy, LayerPtr layer)
{
    Document* doc = document();
    auto f = std::make_shared<FloatingPixels>();
    f->layer = layer ? layer : doc->activeLayerPtr();
    f->original = f->layer->image;
    const Selection sel = doc->selection().isEmpty() ? Selection::fromRect(doc->rect(), doc->size()) : doc->selection();
    f->selectionBase = sel;
    f->pixels = Raster::extractSelection(f->original, sel);
    f->origin = sel.bounds().topLeft();
    f->base = f->original.copy();
    f->interpolation = AppState::instance()->tools().moveInterpolation;
    f->gammaCorrect = AppState::instance()->tools().moveGamma;
    f->replace = AppState::instance()->preferences().replacePixels;
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

    // Sin selección, como en Paint.NET, el clic selecciona todo el lienzo (ya se ve al pulsar).
    if (doc->selection().isEmpty() && !currentFloating(doc)) {
        const QString text = tr("Seleccionar todo");
        HistoryIcons::registerIcon(text, QStringLiteral("edit-select-all"));
        doc->undoStack()->push(
            new SelectionCommand(doc, doc->selection(), Selection::fromRect(doc->rect(), doc->size()), text));
        m_selectionBefore = doc->selection();
    }

    // Ctrl sobre píxeles ya flotantes deja una copia donde están. Con otras capas vinculadas se
    // levanta todo de nuevo, para que compartan origen y transformación.
    QList<LayerPtr> linked;
    for (const LayerPtr& layer : doc->linkedLayers())
        if (layer != doc->activeLayerPtr() && doc->indexOf(layer.get()) >= 0)
            linked << layer;
    m_float = copy || !linked.isEmpty() ? nullptr : currentFloating(doc);
    m_first = !m_float;
    if (m_first)
        m_float = lift(copy);
    m_extra.clear();
    for (const LayerPtr& layer : std::as_const(linked))
        m_extra << lift(copy, layer);
    m_startTransform = m_float->transform;
    m_current = m_startTransform;
    m_box.set(QRectF(m_float->pixels.rect()), m_float->pixelsToCanvas(m_startTransform));
    m_box.begin(mode, handle, e.pos);
    m_active = true;
    repaint();
}

void MoveSelectedTool::applyLive(const QTransform& t)
{
    Document* doc = document();
    QRect r = m_float->boundsFor(m_current) | m_float->boundsFor(t);
    if (m_first)
        r |= m_float->boundsFor(QTransform());
    m_float->render(t, r);
    for (const FloatingPtr& extra : std::as_const(m_extra)) {
        extra->render(t, r);
        doc->notifyPixelsChanged(extra->layer.get(), r);
    }
    m_current = t;
    doc->notifyPixelsChanged(m_float->layer.get(), r);
    doc->setSelection(m_float->placedSelection(t));
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
    QTransform canvasMap = m_box.update(e.pos, e.modifiers);
    // Píxel a píxel (por defecto): nada queda a medio píxel, como la selección.
    if (!AppState::instance()->tools().selectionAntialias) {
        canvasMap = TransformBox::snappedToPixels(canvasMap, m_box.localRect());
        m_box.set(m_box.localRect(), canvasMap);
    }
    const QTransform t = QTransform::fromTranslate(-m_float->origin.x(), -m_float->origin.y()) * canvasMap;
    applyLive(t);
    if (m_box.mode() == TransformBox::Mode::Rotate)
        emit hintChanged(tr("Ángulo: %1°").arg(m_box.rotationDegrees(), 0, 'f', 2));
    repaint();
}

void MoveSelectedTool::push()
{
    Document* doc = document();
    HistoryIcons::registerIcon(name(), QStringLiteral("moveselectedpixels"));
    if (m_extra.isEmpty()) {
        doc->undoStack()->push(new MovePixelsCommand(doc, m_float, m_startTransform, m_current, m_first, false,
                                                     m_selectionBefore, name()));
        return;
    }
    // Varias capas: un solo paso con el movimiento de cada una.
    auto* macro = new QUndoCommand(name());
    new MovePixelsCommand(doc, m_float, m_startTransform, m_current, m_first, false, m_selectionBefore, name(), macro);
    for (const FloatingPtr& extra : std::as_const(m_extra))
        new MovePixelsCommand(doc, extra, QTransform(), m_current, true, false, m_selectionBefore, name(), macro);
    doc->undoStack()->push(macro);
}

void MoveSelectedTool::restoreUnmoved()
{
    // Se soltó sin mover: las capas vuelven a estar como antes.
    Document* doc = document();
    if (m_first) {
        m_float->layer->image = m_float->original;
        doc->notifyPixelsChanged(m_float->layer.get(), m_float->boundsFor(m_current));
    }
    for (const FloatingPtr& extra : std::as_const(m_extra)) {
        extra->layer->image = extra->original;
        doc->notifyPixelsChanged(extra->layer.get(), extra->boundsFor(m_current));
    }
    if (m_first)
        doc->setSelection(m_selectionBefore);
}

void MoveSelectedTool::endMove()
{
    // Tras mover varias capas se vuelve solo a la del principio.
    m_float.reset();
    if (!m_extra.isEmpty()) {
        m_extra.clear();
        document()->setLinkedLayers({});
    }
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
    if (m_current != m_startTransform)
        push();
    else
        restoreUnmoved();
    endMove();
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

void MoveSelectedTool::finish()
{
    // Otra acción (Supr, copiar, un efecto...) a mitad de arrastre: se registra lo movido hasta
    // ahí; si no, al seguir arrastrando volverían a pintarse los píxeles flotantes.
    if (!m_active)
        return;
    m_active = false;
    m_box.end();
    if (!m_pivotDrag && m_current != m_startTransform)
        push();
    else if (!m_pivotDrag)
        restoreUnmoved();
    m_pivotDrag = false;
    endMove();
    repaint();
}

void MoveSelectedTool::deactivate()
{
    finish();
    Tool::deactivate();
}

bool MoveSelectedTool::hidesSelectionOutline() const
{
    return m_active && !m_pivotDrag && AppState::instance()->preferences().hideBoxWhileDragging;
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
    // El contorno es el de la selección (lo dibuja el lienzo): el del cuadro encima se veía
    // como una línea doble. Como en Paint.NET, mientras se arrastra solo queda el icono de
    // mover (y, al girar, el punto de giro).
    int parts = TransformBox::AllParts & ~TransformBox::Outline;
    if (hidesSelectionOutline())
        parts = TransformBox::MoveIcon | (m_box.mode() == TransformBox::Mode::Rotate ? TransformBox::PivotPoint : 0);
    m_box.draw(p, canvas()->imageToViewTransform(), parts);
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
    // Sin selección, como Mover píxeles seleccionados: el clic selecciona todo el lienzo.
    if (doc->selection().isEmpty()) {
        if (e.button != Qt::LeftButton)
            return;
        const QString text = tr("Seleccionar todo");
        HistoryIcons::registerIcon(text, QStringLiteral("edit-select-all"));
        doc->undoStack()->push(new SelectionCommand(doc, doc->selection(), Selection::fromRect(doc->rect(), doc->size()), text));
    }
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
    repaint();
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
    if (!AppState::instance()->tools().moveSelectionAntialias) {
        m_transform = TransformBox::snappedToPixels(m_transform, m_box.localRect());
        m_box.set(m_box.localRect(), m_transform);
    }
    m_result = m_base.transformed(m_transform, AppState::instance()->tools().moveSelectionAntialias);
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
    HistoryIcons::registerIcon(name(), QStringLiteral("moveselection"));
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

void MoveSelectionTool::finish()
{
    if (!m_active)
        return;
    m_active = false;
    m_box.end();
    if (!m_pivotDrag)
        push();
    m_pivotDrag = false;
    repaint();
}

void MoveSelectionTool::deactivate()
{
    finish();
    Tool::deactivate();
}

void MoveSelectionTool::paintOverlay(QPainter& p)
{
    Document* doc = document();
    if (!doc || doc->selection().isEmpty())
        return;
    if (!m_active)
        syncBox();
    // El contorno es el de la selección (lo dibuja el lienzo). Mientras se arrastra solo
    // desaparecen los tiradores (el punto de giro sigue a la vista al girar).
    int parts = TransformBox::AllParts & ~TransformBox::Outline;
    if (m_active && !m_pivotDrag && AppState::instance()->preferences().hideBoxWhileDragging)
        parts = TransformBox::MoveIcon | (m_box.mode() == TransformBox::Mode::Rotate ? TransformBox::PivotPoint : 0);
    m_box.draw(p, canvas()->imageToViewTransform(), parts);
}
