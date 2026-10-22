#pragma once

#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QAction;
class QListWidget;
class Document;

// Ventana Capas: miniatura, nombre y casilla de visibilidad (a la derecha, como en Paint.NET).
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    LayersPanel(const QList<QAction*>& actions, QWidget* parent = nullptr);
    void setDocument(Document* doc);

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
    void updateThumbnails();
    int rowForLayer(int index) const;
    int layerForRow(int row) const;

    QPointer<Document> m_doc;
    QListWidget* m_list;
    QTimer m_thumbTimer;
    QSet<int> m_dirty;
    int m_dragRow = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
};
