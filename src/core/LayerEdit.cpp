#include "core/LayerEdit.h"

#include "core/Commands.h"
#include "core/Raster.h"

#include <QUndoStack>

void LayerEdit::begin(Document* doc, LayerPtr layer)
{
    reset();
    if (!doc || !layer)
        return;
    m_doc = doc;
    m_layer = std::move(layer);
    m_before = m_layer->image;
    m_layer->image.detach();
}

void LayerEdit::resume(Document* doc, LayerPtr layer, const QImage& before, const QRect& touched)
{
    reset();
    if (!doc || !layer)
        return;
    m_doc = doc;
    m_layer = std::move(layer);
    m_before = before;
    m_last = m_total = touched;
}

void LayerEdit::restorePrevious()
{
    if (!m_last.isEmpty())
        Raster::copyRect(m_layer->image, m_before, m_last);
}

void LayerEdit::touched(const QRect& rect)
{
    const QRect dirty = rect | m_last;
    m_last = rect;
    m_total |= rect;
    m_doc->notifyPixelsChanged(m_layer.get(), dirty);
}

void LayerEdit::showOverlay(const QImage& overlay, QPoint pos, BlendMode mode)
{
    if (!m_doc)
        return;
    restorePrevious();
    const QRect r = QRect(pos, overlay.size()).intersected(m_layer->image.rect());
    if (!r.isEmpty())
        Raster::compositeRegion(m_layer->image, m_before, overlay.copy(QRect(r.topLeft() - pos, r.size())), r, mode,
                                m_doc->selection());
    touched(r);
}

void LayerEdit::showInHistory(const QString& text, const QString& icon)
{
    if (!m_doc || m_live)
        return;
    HistoryIcons::registerIcon(text, icon);
    m_live = new QUndoCommand(text);
    m_doc->undoStack()->push(m_live);
}

bool LayerEdit::liveOnTop() const
{
    const QUndoStack* stack = m_doc->undoStack();
    return m_live && stack->index() == stack->count() && stack->index() > 0
        && stack->command(stack->index() - 1) == m_live;
}

void LayerEdit::dropLive()
{
    // Un paso obsoleto se borra de la pila al deshacerlo.
    if (!m_live)
        return;
    m_live->setObsolete(true);
    if (liveOnTop())
        m_doc->undoStack()->undo();
    m_live = nullptr;
}

bool LayerEdit::commit(const QString& text, const QString& icon)
{
    if (!m_doc)
        return false;
    bool changed = false;
    if (liveOnTop()) {
        HistoryIcons::registerIcon(text, icon);
        auto* patch = new PixelPatchCommand(m_doc, m_layer, m_before, m_total, text, m_live);
        changed = !patch->isEmpty();
        if (!changed) {
            dropLive();
        } else if (m_live->text() != text) {
            m_live->setText(text);
            History::refresh(m_doc->undoStack());
        }
    } else {
        dropLive();
        changed = pushPixelPatch(m_doc, m_layer, m_before, m_total, text, icon);
    }
    reset();
    return changed;
}

void LayerEdit::cancel()
{
    if (!m_doc)
        return;
    Raster::copyRect(m_layer->image, m_before, m_total);
    m_doc->notifyPixelsChanged(m_layer.get(), m_total);
    dropLive();
    reset();
}

void LayerEdit::reset()
{
    m_doc = nullptr;
    m_layer.reset();
    m_before = QImage();
    m_last = QRect();
    m_total = QRect();
    m_live = nullptr;
}
