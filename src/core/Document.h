#pragma once

#include "core/Layer.h"
#include "core/Selection.h"

#include <QColorSpace>
#include <QList>
#include <QObject>
#include <QUndoStack>

// Documento: pila de capas (índice 0 = fondo), selección, historial propio y ruta del archivo.
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QSize size, QObject* parent = nullptr);
    ~Document() override;

    static Document* createBlank(QSize size, QColor background);
    static Document* createFromImage(const QImage& image, const QString& layerName);

    QSize size() const { return m_size; }
    QRect rect() const { return QRect(QPoint(0, 0), m_size); }

    // Resolución en puntos por pulgada: convierte píxeles en pulgadas o centímetros (reglas,
    // barra de estado, impresión). Paint.NET usa 96.
    double dpi() const { return m_dpi; }
    void setDpi(double dpi);

    // Perfil de color de la imagen (Imagen > Perfil de colores). Por defecto sRGB; los
    // archivos con otro perfil lo conservan al guardar.
    QColorSpace colorSpace() const { return m_colorSpace; }
    void setColorSpace(const QColorSpace& space);

    int layerCount() const { return int(m_layers.size()); }
    const QList<LayerPtr>& layers() const { return m_layers; }
    Layer* layer(int index) const;
    LayerPtr layerPtr(int index) const;
    int indexOf(const Layer* layer) const;

    int activeIndex() const { return m_active; }
    Layer* activeLayer() const { return layer(m_active); }
    LayerPtr activeLayerPtr() const { return layerPtr(m_active); }
    void setActiveIndex(int index);

    // Modificaciones sin historial.
    void insertLayer(int index, LayerPtr layer);
    LayerPtr removeLayer(int index);
    void moveLayer(int from, int to);
    void setLayerProps(int index, const LayerProps& props);
    void replaceState(QSize size, const QList<LayerPtr>& layers, int active);
    void notifyPixelsChanged(const Layer* layer, const QRect& rect);

    const Selection& selection() const { return m_selection; }
    void setSelection(const Selection& selection);

    // Vista previa de efectos: sustituye temporalmente la imagen de una capa al componer, sin
    // tocar los datos reales.
    void setPreview(const Layer* layer, const QImage& image);
    void clearPreview();
    bool hasPreview() const { return m_previewLayer != nullptr; }

    // Compone las capas visibles en 'target' (premultiplicado) dentro de 'rect'.
    void renderComposite(QImage& target, const QRect& rect) const;
    QImage flattened() const;

    QUndoStack* undoStack() const { return m_undo; }

    QString filePath() const { return m_filePath; }
    int gifLoops() const { return m_gifLoops; }
    void setGifLoops(int loops) { m_gifLoops = loops; }
    void setFilePath(const QString& path);
    QString displayName() const;
    bool isModified() const { return !m_undo->isClean(); }
    void markSaved() { m_undo->setClean(); }

    QString historyRootLabel() const { return m_historyRoot; }
    void setHistoryRootLabel(const QString& label) { m_historyRoot = label; }

    QString nextLayerName() const;

signals:
    void pixelsChanged(const QRect& rect);
    void layerPixelsChanged(int index);
    void layersChanged();
    void activeLayerChanged(int index);
    void selectionChanged();
    void sizeChanged(QSize size);
    void resolutionChanged(double dpi);
    void colorSpaceChanged();
    void filePathChanged();
    void modifiedChanged(bool modified);

private:
    QSize m_size;
    double m_dpi = 96.0;
    QColorSpace m_colorSpace = QColorSpace(QColorSpace::SRgb);
    QList<LayerPtr> m_layers;
    int m_active = -1;
    Selection m_selection;
    QUndoStack* m_undo;
    QString m_filePath;
    int m_gifLoops = 0;
    QString m_historyRoot;
    const Layer* m_previewLayer = nullptr;
    QImage m_previewImage;
};
