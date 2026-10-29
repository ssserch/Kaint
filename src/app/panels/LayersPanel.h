#pragma once

#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QAction;
class QListWidget;
class QVariantAnimation;
class Document;
struct Layer;

// Ventana Capas: miniatura, nombre y casilla de visibilidad (a la derecha, como en Paint.NET).
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    LayersPanel(const QList<QAction*>& actions, QWidget* parent = nullptr);
    void setDocument(Document* doc);
    // Con Mover píxeles seleccionados: Ctrl+clic añade capas que se mueven junto con la activa.
    void setLinkingEnabled(bool on);
    // Al arrastrar una capa: con la línea, la fila se queda en su sitio y una línea marca dónde
    // quedará; si no, la fila sigue al ratón y las demás le van dejando sitio.
    void setDropLineMode(bool on) { m_dropLineMode = on; }

signals:
    void visibilityToggled(int layerIndex);
    void soloRequested(int layerIndex);
    void propertiesRequested();
    void moveRequested(int from, int to);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void rebuild();
    void syncSelection();
    void syncLinked();
    void showDropLine(int targetRow);
    int targetRowAt(int y) const;
    void startLiveDrag();
    void updateLiveDrag(int y);
    void finishLiveDrag();
    void updateThumbnails();
    int rowForLayer(int index) const;
    int layerForRow(int row) const;
    void slideMovedRows(const QList<const Layer*>& before);

    QPointer<Document> m_doc;
    QListWidget* m_list;
    QTimer m_thumbTimer;
    QSet<int> m_dirty;
    int m_dragRow = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
    bool m_linking = false;
    bool m_linkClick = false;
    QWidget* m_dropLine = nullptr;
    bool m_dropLineMode = false;
    // Arrastre en vivo: la imagen de la fila que sigue al ratón, la fila donde quedaría, la
    // distancia del clic al borde de arriba de la fila y el desplazamiento de las demás.
    class QLabel* m_ghost = nullptr;
    int m_liveTarget = -1;
    int m_grabOffset = 0;
    int m_lastDragY = 0;
    QTimer m_dragScroll;
    QPointer<QVariantAnimation> m_makeRoom;
    // Capa de cada fila en la última reconstrucción (para deslizar las que cambian de sitio).
    QList<const Layer*> m_rowLayers;
    QPointer<QVariantAnimation> m_slide;
};
