#include "core/LayerEdit.h"

#include "core/Commands.h"
#include "core/Raster.h"

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

bool LayerEdit::commit(const QString& text, const QString& icon)
{
    if (!m_doc)
        return false;
    const bool changed = pushPixelPatch(m_doc, m_layer, m_before, m_total, text, icon);
    reset();
    return changed;
}

void LayerEdit::cancel()
{
    if (!m_doc)
        return;
    Raster::copyRect(m_layer->image, m_before, m_total);
    m_doc->notifyPixelsChanged(m_layer.get(), m_total);
    reset();
}

void LayerEdit::reset()
{
    m_doc = nullptr;
    m_layer.reset();
    m_before = QImage();
    m_last = QRect();
    m_total = QRect();
}
