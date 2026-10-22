#include "core/Document.h"

#include "core/Raster.h"

#include <QFileInfo>
#include <QPainter>

#include <cstring>

Document::Document(QSize size, QObject* parent)
    : QObject(parent)
    , m_size(size)
    , m_undo(new QUndoStack(this))
    , m_historyRoot(tr("Imagen nueva"))
{
    connect(m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) { emit modifiedChanged(!clean); });
}

Document::~Document() = default;

Document* Document::createBlank(QSize size, QColor background)
{
    auto* doc = new Document(size);
    doc->m_layers.append(std::make_shared<Layer>(tr("Fondo"), size, background));
    doc->m_active = 0;
    return doc;
}

Document* Document::createFromImage(const QImage& image, const QString& layerName)
{
    auto* doc = new Document(image.size());
    doc->m_layers.append(std::make_shared<Layer>(layerName, image));
    doc->m_active = 0;
    doc->m_historyRoot = tr("Abrir imagen");
    return doc;
}

Layer* Document::layer(int index) const
{
    return index >= 0 && index < m_layers.size() ? m_layers[index].get() : nullptr;
}

LayerPtr Document::layerPtr(int index) const
{
    return index >= 0 && index < m_layers.size() ? m_layers[index] : nullptr;
}

int Document::indexOf(const Layer* layer) const
{
    for (int i = 0; i < m_layers.size(); ++i)
        if (m_layers[i].get() == layer)
            return i;
    return -1;
}

void Document::setActiveIndex(int index)
{
    index = std::clamp(index, 0, int(m_layers.size()) - 1);
    if (index == m_active)
        return;
    m_active = index;
    emit activeLayerChanged(index);
}

void Document::insertLayer(int index, LayerPtr layer)
{
    index = std::clamp(index, 0, int(m_layers.size()));
    m_layers.insert(index, std::move(layer));
    m_active = index;
    emit layersChanged();
    emit activeLayerChanged(m_active);
    emit pixelsChanged(rect());
}

LayerPtr Document::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size())
        return nullptr;
    LayerPtr removed = m_layers.takeAt(index);
    if (m_previewLayer == removed.get())
        clearPreview();
    m_active = std::clamp(m_active >= index ? m_active - 1 : m_active, 0, int(m_layers.size()) - 1);
    emit layersChanged();
    emit activeLayerChanged(m_active);
    emit pixelsChanged(rect());
    return removed;
}

void Document::moveLayer(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_layers.size() || to >= m_layers.size())
        return;
    m_layers.move(from, to);
    m_active = to;
    emit layersChanged();
    emit activeLayerChanged(m_active);
    emit pixelsChanged(rect());
}

void Document::setLayerProps(int index, const LayerProps& props)
{
    Layer* l = layer(index);
    if (!l || l->props == props)
        return;
    l->props = props;
    emit layersChanged();
    emit pixelsChanged(rect());
}

void Document::replaceState(QSize size, const QList<LayerPtr>& layers, int active)
{
    const bool resized = size != m_size;
    clearPreview();
    m_size = size;
    m_layers = layers;
    m_active = std::clamp(active, 0, int(m_layers.size()) - 1);
    if (resized)
        emit sizeChanged(m_size);
    emit layersChanged();
    emit activeLayerChanged(m_active);
    emit pixelsChanged(rect());
}

void Document::notifyPixelsChanged(const Layer* layer, const QRect& r)
{
    const int index = indexOf(layer);
    if (index < 0)
        return;
    emit layerPixelsChanged(index);
    if (layer->props.visible)
        emit pixelsChanged(r.intersected(rect()));
}

void Document::setDpi(double dpi)
{
    dpi = std::clamp(dpi, 1.0, 100000.0);
    if (qFuzzyCompare(dpi, m_dpi))
        return;
    m_dpi = dpi;
    emit resolutionChanged(m_dpi);
}

void Document::setColorSpace(const QColorSpace& space)
{
    const QColorSpace cs = space.isValid() ? space : QColorSpace(QColorSpace::SRgb);
    if (cs == m_colorSpace)
        return;
    m_colorSpace = cs;
    emit colorSpaceChanged();
}

void Document::setSelection(const Selection& selection)
{
    if (selection == m_selection)
        return;
    m_selection = selection;
    emit selectionChanged();
}

void Document::setPreview(const Layer* layer, const QImage& image)
{
    m_previewLayer = layer;
    m_previewImage = image;
    emit pixelsChanged(rect());
}

void Document::clearPreview()
{
    if (!m_previewLayer)
        return;
    m_previewLayer = nullptr;
    m_previewImage = QImage();
    emit pixelsChanged(rect());
}

void Document::renderComposite(QImage& target, const QRect& area) const
{
    const QRect r = area.intersected(rect());
    if (r.isEmpty())
        return;
    for (int y = r.top(); y <= r.bottom(); ++y)
        std::memset(target.scanLine(y) + r.left() * 4, 0, size_t(r.width()) * 4);
    for (const LayerPtr& l : m_layers) {
        if (!l->props.visible || l->props.opacity == 0)
            continue;
        const QImage& img = (l.get() == m_previewLayer) ? m_previewImage : l->image;
        blendImage(target, r.topLeft(), img, r, l->props.blend, l->props.opacity);
    }
}

QImage Document::flattened() const
{
    // Una sola capa visible, opaca y Normal: sus píxeles tal cual (así abrir y
    // guardar un PNG conserva también el color de los píxeles transparentes).
    if (m_layers.size() == 1 && !m_previewLayer) {
        const LayerProps& p = m_layers.first()->props;
        if (p.visible && p.opacity == 255 && p.blend == BlendMode::Normal)
            return m_layers.first()->image;
    }
    QImage out(m_size, kPremulFormat);
    out.fill(Qt::transparent);
    renderComposite(out, rect());
    return Raster::unpremultiplied(out);
}

void Document::setFilePath(const QString& path)
{
    if (path == m_filePath)
        return;
    m_filePath = path;
    emit filePathChanged();
}

QString Document::displayName() const
{
    return m_filePath.isEmpty() ? tr("Sin título") : QFileInfo(m_filePath).fileName();
}

QString Document::nextLayerName() const
{
    // "Capa N" con el primer N libre, como hace Paint.NET.
    for (int n = m_layers.size() + 1;; ++n) {
        const QString name = tr("Capa %1").arg(n);
        bool used = false;
        for (const LayerPtr& l : m_layers)
            used = used || l->props.name == name;
        if (!used)
            return name;
    }
}
