#pragma once

#include "core/Document.h"

// Edición interactiva de una capa (formas, líneas, texto, degradados...): guarda la imagen
// original, permite redibujar la vista previa tantas veces como haga falta y al final registra
// un único cambio en el historial.

class QUndoCommand;
class LayerEdit {
public:
    void begin(Document* doc, LayerPtr layer);
    bool isActive() const { return m_doc != nullptr; }

    Document* document() const { return m_doc; }
    const LayerPtr& layer() const { return m_layer; }
    const QImage& before() const { return m_before; }
    QImage& image() { return m_layer->image; }

    // Restaura lo dibujado en la vista previa anterior y compone 'overlay' (posicionada en
    // 'pos') con el modo de fusión y la selección actual.
    void showOverlay(const QImage& overlay, QPoint pos, BlendMode mode);

    // Para herramientas que escriben directamente en image(): restaura la región anterior y
    // avisa del área modificada.
    void restorePrevious();
    void touched(const QRect& rect);

    // Paso del historial visible desde que empieza la edición, como el texto de Paint.NET:
    // commit() le pone los píxeles y cancel() lo quita. 'command' es el paso que se apila (para
    // herramientas que guardan en él su estado); si ya hay uno, se descarta.
    void showInHistory(const QString& text, const QString& icon = {}, QUndoCommand* command = nullptr);
    // El paso en vivo, si sigue en lo alto del historial.
    QUndoCommand* liveStep() const { return m_doc && liveOnTop() ? m_live : nullptr; }

    bool commit(const QString& text, const QString& icon = {});
    void cancel();

    // Para herramientas que guardan cada paso en el historial (degradado): retomar una edición
    // con la imagen de antes y lo que ya estaba dibujado, u olvidarla sin tocar los píxeles.
    // Tras retomar, commit() y cancel() parten de la capa tal como estaba al retomar.
    void resume(Document* doc, LayerPtr layer, const QImage& before, const QRect& touched);
    void detach() { reset(); }
    QRect touchedArea() const { return m_total; }

private:
    void reset();
    bool liveOnTop() const;
    void dropLive();

    Document* m_doc = nullptr;
    LayerPtr m_layer;
    QImage m_before;
    QImage m_start;
    QRect m_last;
    QRect m_total;
    QUndoCommand* m_live = nullptr;
};
