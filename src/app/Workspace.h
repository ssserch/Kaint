#pragma once

#include <QFrame>
#include <QPointer>
#include <QVariant>

class QAbstractButton;
class QLabel;
class Canvas;

// Contenido de panel que decide su propio tamaño al redimensionarlo (la rejilla de
// herramientas encaja sus columnas).
class PanelContent {
public:
    virtual ~PanelContent() = default;
    virtual QSize fitSize(QSize proposed, Qt::Orientations changed) const = 0;
    // Distribución que se guarda en vez del tamaño en píxeles (columnas, escala), para que no
    // se estropee si cambian los iconos o la fuente; tamaño del contenido para una guardada.
    virtual QVariant layoutState() const = 0;
    virtual QSize sizeForLayout(const QVariant& state) const = 0;
};

// Panel flotante dentro del área de trabajo (Herramientas, Historial, Capas, Colores).
class FloatingPanel : public QFrame {
    Q_OBJECT
public:
    enum Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    FloatingPanel(const QString& id, const QString& title, QWidget* content, QWidget* parent);

    QString id() const { return m_id; }
    QString title() const { return m_fullTitle; }
    void setAnchor(Corner corner, QPoint offset);
    void reposition();
    void setResizable(bool on) { m_resizable = on; }
    bool isResizable() const { return m_resizable; }

    void saveState() const;
    void restoreState(Corner defaultCorner, QPoint defaultOffset, QSize defaultSize);
    void resetToDefault();
    void setTranslucent(bool on);
    void fitToContent();

    // Grosor del borde que se puede arrastrar para cambiar el tamaño.
    static constexpr int kBorder = 5;

signals:
    void closed();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;
    bool event(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    void updateColors();
    void updateAnchorFromGeometry();
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateEdgeCursor(Qt::Edges edges);
    QRect constrained(const QRect& proposed, Qt::Edges edges) const;
    QSize chromeSize() const;
    void updateTitleText();

    QString m_id;
    QString m_fullTitle;
    QWidget* m_titleBar = nullptr;
    QLabel* m_title = nullptr;
    QAbstractButton* m_close = nullptr;
    QWidget* m_content = nullptr;
    Corner m_corner = TopLeft;
    QPoint m_offset;
    bool m_dragging = false;
    bool m_resizable = true;
    Qt::Edges m_resizeEdges;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;
    QPoint m_dragOffset;
    Corner m_defaultCorner = TopLeft;
    QPoint m_defaultOffset;
    QSize m_defaultSize;
    class QGraphicsOpacityEffect* m_opacity = nullptr;
};

// Contenedor: el lienzo ocupa todo y los paneles flotan encima.
class Workspace : public QWidget {
    Q_OBJECT
public:
    explicit Workspace(Canvas* canvas, QWidget* parent = nullptr);
    FloatingPanel* addPanel(const QString& id, const QString& title, QWidget* content, FloatingPanel::Corner corner,
                            QPoint offset, QSize size, bool resizable = true);
    QList<FloatingPanel*> panels() const { return m_panels; }
    void saveState() const;
    void resetPanels();
    void setTranslucentPanels(bool on);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    Canvas* m_canvas;
    QList<FloatingPanel*> m_panels;
};
