#pragma once

#include "core/Document.h"
#include "core/Resample.h"

#include <QTransform>
#include <QUndoCommand>
#include <QVector>

// Iconos asociados a cada texto del historial (el panel los muestra).
namespace HistoryIcons {
void registerIcon(const QString& text, const QString& iconName);
QString iconFor(const QString& text);
}

// Paso del historial que sabe cuánta memoria ocupa y puede liberarla (límite de memoria del
// historial).
class HistoryStep {
public:
    virtual ~HistoryStep() = default;
    virtual qint64 memoryBytes() const = 0;
    virtual void releaseMemory() = 0;
    bool isReleased() const { return m_released; }

protected:
    bool m_released = false;
};

namespace History {
// Memoria de un paso (con sus hijos, p. ej. los de una macro).
qint64 memory(const QUndoCommand* cmd);
// Memoria de todos los pasos de la pila.
qint64 memory(const QUndoStack* stack);
// Libera los pasos más antiguos (por debajo del actual) hasta que la pila ocupe como mucho
// 'limit' bytes. Devuelve cuántos pasos se liberaron.
int trim(QUndoStack* stack, qint64 limit);
// Primer índice al que se puede volver (los pasos de debajo están liberados).
int barrier(const QUndoStack* stack);
// Vuelve a dibujar el historial (tras cambiar el texto de un paso ya apilado).
void refresh(QUndoStack* stack);
// Capa que cambia un paso (píxeles, mover píxeles; en una macro, la de su primer hijo), o nula.
const Layer* layerOf(const QUndoCommand* cmd);
} // namespace History

// Cambio de píxeles en una capa. Solo guarda los tiles (64x64) que difieren entre 'before' y
// el estado actual de la capa dentro de 'rect'.
class PixelPatchCommand : public QUndoCommand, public HistoryStep {
public:
    PixelPatchCommand(Document* doc, LayerPtr layer, const QImage& before, const QRect& rect,
                      const QString& text, QUndoCommand* parent = nullptr);
    bool isEmpty() const { return m_tiles.isEmpty(); }
    const LayerPtr& targetLayer() const { return m_layer; }
    void undo() override;
    void redo() override;
    qint64 memoryBytes() const override;
    void releaseMemory() override;

private:
    struct Tile {
        QRect rect;
        QImage before;
        QImage after;
    };
    void apply(bool after);

    Document* m_doc;
    LayerPtr m_layer;
    QVector<Tile> m_tiles;
    QRect m_bounds;
};

// Crea y apila un PixelPatchCommand si de verdad hubo cambios.
bool pushPixelPatch(Document* doc, const LayerPtr& layer, const QImage& before, const QRect& rect,
                    const QString& text, const QString& icon = {});

class SelectionCommand : public QUndoCommand, public HistoryStep {
public:
    SelectionCommand(Document* doc, const Selection& before, const Selection& after, const QString& text,
                     int mergeKey = -1, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override { return m_mergeKey; }
    bool mergeWith(const QUndoCommand* other) override;
    qint64 memoryBytes() const override;
    void releaseMemory() override;

private:
    Document* m_doc;
    Selection m_before;
    Selection m_after;
    int m_mergeKey;
};

class InsertLayerCommand : public QUndoCommand {
public:
    InsertLayerCommand(Document* doc, int index, LayerPtr layer, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document* m_doc;
    int m_index;
    int m_previousActive;
    LayerPtr m_layer;
};

class RemoveLayerCommand : public QUndoCommand, public HistoryStep {
public:
    RemoveLayerCommand(Document* doc, int index, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    qint64 memoryBytes() const override;
    void releaseMemory() override;

private:
    Document* m_doc;
    int m_index;
    LayerPtr m_layer;
};

class MoveLayerCommand : public QUndoCommand {
public:
    MoveLayerCommand(Document* doc, int from, int to, const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document* m_doc;
    int m_from;
    int m_to;
};

class LayerPropsCommand : public QUndoCommand {
public:
    LayerPropsCommand(Document* doc, int index, const LayerProps& before, const LayerProps& after,
                      const QString& text, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    Document* m_doc;
    LayerPtr m_layer;
    LayerProps m_before;
    LayerProps m_after;
};

// Estado completo del documento, para operaciones globales (redimensionar, girar, acoplar...).
struct DocumentState {
    QSize size;
    double dpi = 96;
    QColorSpace colorSpace = QColorSpace(QColorSpace::SRgb);
    QList<LayerPtr> layers;
    QList<QImage> images;
    QList<LayerProps> props;
    int active = 0;
    Selection selection;

    static DocumentState capture(const Document* doc);
    void apply(Document* doc) const;
};

class DocumentStateCommand : public QUndoCommand, public HistoryStep {
public:
    DocumentStateCommand(Document* doc, DocumentState before, DocumentState after, const QString& text,
                         QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    qint64 memoryBytes() const override;
    void releaseMemory() override;

private:
    Document* m_doc;
    DocumentState m_before;
    DocumentState m_after;
};

// Píxeles "flotantes": los que se están moviendo con la herramienta Mover píxeles
// seleccionados o los recién pegados.
struct FloatingPixels {
    LayerPtr layer;
    QImage original;
    QImage base;
    QImage pixels;
    QPoint origin;
    Selection selectionBase;
    QTransform transform;
    Resample::Interpolation interpolation = Resample::Interpolation::Bicubic;
    bool gammaCorrect = true;

    QTransform pixelsToCanvas(const QTransform& t) const;
    QRect boundsFor(const QTransform& t) const;
    void render(const QTransform& t, const QRect& region) const;
};
using FloatingPtr = std::shared_ptr<FloatingPixels>;

class MovePixelsCommand : public QUndoCommand, public HistoryStep {
public:
    MovePixelsCommand(Document* doc, FloatingPtr floating, const QTransform& before, const QTransform& after,
                      bool first, bool paste, const Selection& selectionBefore, const QString& text,
                      QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
    // Cada arrastre es un paso propio, como en Paint.NET (no se fusionan).
    const FloatingPtr& floating() const { return m_floating; }
    qint64 memoryBytes() const override;
    void releaseMemory() override;

private:
    QRect region() const;

    Document* m_doc;
    FloatingPtr m_floating;
    QTransform m_before;
    QTransform m_after;
    bool m_first;
    bool m_paste;
    Selection m_selectionBefore;
};
