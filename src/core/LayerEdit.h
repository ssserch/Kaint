#pragma once

#include "core/Document.h"

// Edición interactiva de una capa (formas, líneas, texto, degradados...):
// guarda la imagen original, permite redibujar la vista previa tantas veces
// como haga falta y al final registra un único cambio en el historial.
class LayerEdit {
public:
    void begin(Document* doc, LayerPtr layer);
    bool isActive() const { return m_doc != nullptr; }

    Document* document() const { return m_doc; }
    const LayerPtr& layer() const { return m_layer; }
    const QImage& before() const { return m_before; }
    QImage& image() { return m_layer->image; }

    // Restaura lo dibujado en la vista previa anterior y compone 'overlay'
    // (posicionada en 'pos') con el modo de fusión y la selección actual.
    void showOverlay(const QImage& overlay, QPoint pos, BlendMode mode);

    // Para herramientas que escriben directamente en image(): restaura la
    // región anterior y avisa del área modificada.
    void restorePrevious();
    void touched(const QRect& rect);

    bool commit(const QString& text, const QString& icon = {});
    void cancel();

private:
    void reset();

    Document* m_doc = nullptr;
    LayerPtr m_layer;
    QImage m_before;
    QRect m_last;
    QRect m_total;
};
