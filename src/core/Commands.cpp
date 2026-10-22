#include "core/Commands.h"

#include "core/Raster.h"

#include <QHash>
#include <QPainter>

#include <cstring>

namespace HistoryIcons {

static QHash<QString, QString>& table()
{
    static QHash<QString, QString> t;
    return t;
}

void registerIcon(const QString& text, const QString& iconName)
{
    if (!iconName.isEmpty())
        table().insert(text, iconName);
}

QString iconFor(const QString& text)
{
    return table().value(text);
}

} // namespace HistoryIcons

// ---------------------------------------------------------------------------

namespace {

qint64 imageBytes(const QImage& img) { return img.isNull() ? 0 : qint64(img.sizeInBytes()); }
qint64 selectionBytes(const Selection& s) { return s.isEmpty() ? 0 : imageBytes(s.mask()); }

} // namespace

namespace History {

qint64 memory(const QUndoCommand* cmd)
{
    if (!cmd)
        return 0;
    qint64 total = 0;
    if (const auto* step = dynamic_cast<const HistoryStep*>(cmd))
        total += step->memoryBytes();
    for (int i = 0; i < cmd->childCount(); ++i)
        total += memory(cmd->child(i));
    return total;
}

qint64 memory(const QUndoStack* stack)
{
    qint64 total = 0;
    for (int i = 0; i < stack->count(); ++i)
        total += memory(stack->command(i));
    return total;
}

static void release(QUndoCommand* cmd)
{
    if (auto* step = dynamic_cast<HistoryStep*>(cmd))
        step->releaseMemory();
    for (int i = 0; i < cmd->childCount(); ++i)
        release(const_cast<QUndoCommand*>(cmd->child(i)));
}

int barrier(const QUndoStack* stack)
{
    return stack->property("kaintHistoryBarrier").toInt();
}

int trim(QUndoStack* stack, qint64 limit)
{
    if (limit <= 0)
        return 0;
    qint64 total = memory(stack);
    int first = barrier(stack);
    const int start = first;
    while (total > limit && first < stack->index() - 1) {
        auto* cmd = const_cast<QUndoCommand*>(stack->command(first));
        total -= memory(cmd);
        release(cmd);
        ++first;
    }
    if (first == start)
        return first - start;
    if (start == 0) {
        QObject::connect(stack, &QUndoStack::indexChanged, stack, [stack](int index) {
            const int b = barrier(stack);
            if (index < b)
                QMetaObject::invokeMethod(stack, [stack, b] { stack->setIndex(b); }, Qt::QueuedConnection);
        });
    }
    stack->setProperty("kaintHistoryBarrier", first);
    return first - start;
}

} // namespace History

// ---------------------------------------------------------------------------

namespace {
constexpr int kTile = 64;

bool tileDiffers(const QImage& a, const QImage& b, const QRect& r)
{
    const size_t bytes = size_t(r.width()) * 4;
    for (int y = r.top(); y <= r.bottom(); ++y)
        if (std::memcmp(a.constScanLine(y) + r.left() * 4, b.constScanLine(y) + r.left() * 4, bytes) != 0)
            return true;
    return false;
}
} // namespace

PixelPatchCommand::PixelPatchCommand(Document* doc, LayerPtr layer, const QImage& before, const QRect& rect,
                                     const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_layer(std::move(layer))
{
    const QImage& after = m_layer->image;
    const QRect area = rect.intersected(after.rect()).intersected(before.rect());
    if (area.isEmpty() || before.size() != after.size())
        return;

    const int tx0 = area.left() / kTile;
    const int ty0 = area.top() / kTile;
    const int tx1 = area.right() / kTile;
    const int ty1 = area.bottom() / kTile;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const QRect tile = QRect(tx * kTile, ty * kTile, kTile, kTile).intersected(after.rect());
            if (!tileDiffers(before, after, tile))
                continue;
            m_tiles.append({tile, before.copy(tile), after.copy(tile)});
            m_bounds |= tile;
        }
    }
}

qint64 PixelPatchCommand::memoryBytes() const
{
    qint64 total = 0;
    for (const Tile& t : m_tiles)
        total += imageBytes(t.before) + imageBytes(t.after);
    return total;
}

void PixelPatchCommand::releaseMemory()
{
    m_tiles.clear();
    m_released = true;
}

void PixelPatchCommand::apply(bool after)
{
    if (m_released)
        return;
    for (const Tile& t : m_tiles)
        Raster::blit(m_layer->image, after ? t.after : t.before, t.rect.topLeft());
    m_doc->notifyPixelsChanged(m_layer.get(), m_bounds);
}

void PixelPatchCommand::undo() { apply(false); }
void PixelPatchCommand::redo() { apply(true); }

bool pushPixelPatch(Document* doc, const LayerPtr& layer, const QImage& before, const QRect& rect,
                    const QString& text, const QString& icon)
{
    auto* cmd = new PixelPatchCommand(doc, layer, before, rect, text);
    if (cmd->isEmpty()) {
        delete cmd;
        return false;
    }
    HistoryIcons::registerIcon(text, icon);
    doc->undoStack()->push(cmd);
    return true;
}

// ---------------------------------------------------------------------------

SelectionCommand::SelectionCommand(Document* doc, const Selection& before, const Selection& after,
                                   const QString& text, int mergeKey, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_before(before)
    , m_after(after)
    , m_mergeKey(mergeKey)
{
}

void SelectionCommand::undo()
{
    if (!m_released)
        m_doc->setSelection(m_before);
}

void SelectionCommand::redo()
{
    if (!m_released)
        m_doc->setSelection(m_after);
}

qint64 SelectionCommand::memoryBytes() const
{
    return selectionBytes(m_before) + selectionBytes(m_after);
}

void SelectionCommand::releaseMemory()
{
    m_before = Selection();
    m_after = Selection();
    m_released = true;
}

bool SelectionCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = static_cast<const SelectionCommand*>(other);
    if (o->m_mergeKey != m_mergeKey || m_released)
        return false;
    m_after = o->m_after;
    return true;
}

// ---------------------------------------------------------------------------

InsertLayerCommand::InsertLayerCommand(Document* doc, int index, LayerPtr layer, const QString& text,
                                       QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_index(index)
    , m_previousActive(doc->activeIndex())
    , m_layer(std::move(layer))
{
}

void InsertLayerCommand::undo()
{
    m_doc->removeLayer(m_index);
    m_doc->setActiveIndex(m_previousActive);
}

void InsertLayerCommand::redo() { m_doc->insertLayer(m_index, m_layer); }

RemoveLayerCommand::RemoveLayerCommand(Document* doc, int index, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_index(index)
    , m_layer(doc->layerPtr(index))
{
}

void RemoveLayerCommand::undo()
{
    if (!m_released)
        m_doc->insertLayer(m_index, m_layer);
}

void RemoveLayerCommand::redo()
{
    if (!m_released)
        m_doc->removeLayer(m_index);
}

qint64 RemoveLayerCommand::memoryBytes() const
{
    // Solo cuenta si la capa ya no está en el documento (la guarda este paso).
    return m_layer && m_doc->indexOf(m_layer.get()) < 0 ? imageBytes(m_layer->image) : 0;
}

void RemoveLayerCommand::releaseMemory()
{
    m_layer.reset();
    m_released = true;
}

MoveLayerCommand::MoveLayerCommand(Document* doc, int from, int to, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_from(from)
    , m_to(to)
{
}

void MoveLayerCommand::undo() { m_doc->moveLayer(m_to, m_from); }
void MoveLayerCommand::redo() { m_doc->moveLayer(m_from, m_to); }

LayerPropsCommand::LayerPropsCommand(Document* doc, int index, const LayerProps& before, const LayerProps& after,
                                     const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_layer(doc->layerPtr(index))
    , m_before(before)
    , m_after(after)
{
}

void LayerPropsCommand::undo() { m_doc->setLayerProps(m_doc->indexOf(m_layer.get()), m_before); }
void LayerPropsCommand::redo() { m_doc->setLayerProps(m_doc->indexOf(m_layer.get()), m_after); }

// ---------------------------------------------------------------------------

DocumentState DocumentState::capture(const Document* doc)
{
    DocumentState s;
    s.size = doc->size();
    s.dpi = doc->dpi();
    s.colorSpace = doc->colorSpace();
    s.layers = doc->layers();
    for (const LayerPtr& l : s.layers) {
        s.images.append(l->image);
        s.props.append(l->props);
    }
    s.active = doc->activeIndex();
    s.selection = doc->selection();
    return s;
}

void DocumentState::apply(Document* doc) const
{
    for (int i = 0; i < layers.size(); ++i) {
        layers[i]->image = images[i];
        layers[i]->props = props[i];
    }
    doc->replaceState(size, layers, active);
    doc->setDpi(dpi);
    doc->setColorSpace(colorSpace);
    doc->setSelection(selection);
}

DocumentStateCommand::DocumentStateCommand(Document* doc, DocumentState before, DocumentState after,
                                           const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_doc(doc)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void DocumentStateCommand::undo()
{
    if (!m_released)
        m_before.apply(m_doc);
}

void DocumentStateCommand::redo()
{
    if (!m_released)
        m_after.apply(m_doc);
}

qint64 DocumentStateCommand::memoryBytes() const
{
    qint64 total = 0;
    for (const DocumentState* state : {&m_before, &m_after}) {
        for (const QImage& img : state->images)
            total += imageBytes(img);
        total += selectionBytes(state->selection);
    }
    return total;
}

void DocumentStateCommand::releaseMemory()
{
    m_before = DocumentState();
    m_after = DocumentState();
    m_released = true;
}

// ---------------------------------------------------------------------------

QTransform FloatingPixels::pixelsToCanvas(const QTransform& t) const
{
    return QTransform::fromTranslate(origin.x(), origin.y()) * t;
}

QRect FloatingPixels::boundsFor(const QTransform& t) const
{
    const QRect r = pixelsToCanvas(t).mapRect(QRectF(pixels.rect())).toAlignedRect().adjusted(-1, -1, 1, 1);
    return r.intersected(base.rect());
}

void FloatingPixels::render(const QTransform& t, const QRect& region) const
{
    const QRect r = region.intersected(base.rect());
    if (r.isEmpty())
        return;
    QImage& img = layer->image;
    Raster::copyRect(img, base, r);
    const QTransform m = pixelsToCanvas(t);
    const bool exact = m.type() <= QTransform::TxTranslate && m.dx() == std::floor(m.dx()) && m.dy() == std::floor(m.dy());
    if (exact) {
        const QPoint at(int(m.dx()), int(m.dy()));
        blendImage(img, r.topLeft(), pixels, r.translated(-at), BlendMode::Normal);
        return;
    }
    QImage tmp(r.size(), kPremulFormat);
    tmp.fill(Qt::transparent);
    const QTransform toTmp = m * QTransform::fromTranslate(-r.left(), -r.top());
    if (interpolation == Resample::Interpolation::Bicubic
        || (gammaCorrect && interpolation == Resample::Interpolation::Bilinear)) {
        Resample::drawTransformed(tmp, pixels, toTmp, interpolation, tmp.rect(), gammaCorrect);
    } else {
        QPainter p(&tmp);
        p.setRenderHint(QPainter::SmoothPixmapTransform, interpolation != Resample::Interpolation::NearestNeighbor);
        p.setTransform(toTmp);
        p.drawImage(0, 0, pixels);
    }
    blendImage(img, r.topLeft(), tmp, BlendMode::Normal);
}

MovePixelsCommand::MovePixelsCommand(Document* doc, FloatingPtr floating, const QTransform& before,
                                     const QTransform& after, bool first, bool paste,
                                     const Selection& selectionBefore, const QString& text)
    : QUndoCommand(text)
    , m_doc(doc)
    , m_floating(std::move(floating))
    , m_before(before)
    , m_after(after)
    , m_first(first)
    , m_paste(paste)
    , m_selectionBefore(selectionBefore)
{
}

QRect MovePixelsCommand::region() const
{
    QRect r = m_floating->boundsFor(m_before) | m_floating->boundsFor(m_after);
    if (m_first)
        r |= m_floating->boundsFor(QTransform());
    return r;
}

qint64 MovePixelsCommand::memoryBytes() const
{
    if (!m_floating || !m_first)
        return selectionBytes(m_selectionBefore);
    return imageBytes(m_floating->original) + imageBytes(m_floating->base) + imageBytes(m_floating->pixels)
         + selectionBytes(m_floating->selectionBase) + selectionBytes(m_selectionBefore);
}

void MovePixelsCommand::releaseMemory()
{
    m_floating.reset();
    m_selectionBefore = Selection();
    m_released = true;
}

void MovePixelsCommand::undo()
{
    if (m_released)
        return;
    const QRect r = region();
    if (m_first) {
        Raster::copyRect(m_floating->layer->image, m_floating->original, r);
        m_doc->setSelection(m_selectionBefore);
    } else {
        m_floating->render(m_before, r);
        m_doc->setSelection(m_floating->selectionBase.transformed(m_before));
    }
    m_floating->transform = m_before;
    m_doc->notifyPixelsChanged(m_floating->layer.get(), r);
}

void MovePixelsCommand::redo()
{
    if (m_released)
        return;
    const QRect r = region();
    m_floating->render(m_after, r);
    m_doc->setSelection(m_floating->selectionBase.transformed(m_after));
    m_floating->transform = m_after;
    m_doc->notifyPixelsChanged(m_floating->layer.get(), r);
}

bool MovePixelsCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = static_cast<const MovePixelsCommand*>(other);
    if (m_released || o->m_floating != m_floating || o->m_first)
        return false;
    m_after = o->m_after;
    return true;
}
