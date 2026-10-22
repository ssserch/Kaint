#pragma once

#include <QFrame>
#include <QPointer>

class QLabel;
class QToolButton;
class Canvas;

// Contenido de panel que decide su propio tamaño al redimensionarlo (la
// rejilla de herramientas encaja sus columnas). 'proposed' es el tamaño del
// contenido que resultaría del arrastre y 'changed' los ejes arrastrados.
class PanelContent {
public:
    virtual ~PanelContent() = default;
    virtual QSize fitSize(QSize proposed, Qt::Orientations changed) const = 0;
};

// Panel flotante dentro del área de trabajo (Herramientas, Historial, Capas,
// Colores). Son widgets hijos y no ventanas: en Wayland una aplicación no
// puede colocar sus propias ventanas, y así los paneles se comportan como en
// Paint.NET (se arrastran por el título, se anclan a la esquina más cercana
// y cambian de tamaño arrastrando cualquiera de sus bordes).
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
    // Vuelve a la posición y el tamaño iniciales (Ventana > Restablecer).
    void resetToDefault();
    // Translúcido mientras el puntero no está encima (opción de Paint.NET).
    void setTranslucent(bool on);
    // Ajusta el tamaño a lo que pide el contenido (p. ej. tras un cambio de tema).
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

private:
    void updateAnchorFromGeometry();
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateEdgeCursor(Qt::Edges edges);
    // Aplica el ajuste del contenido a una geometría propuesta, manteniendo
    // fijos los bordes que no se arrastran.
    QRect constrained(const QRect& proposed, Qt::Edges edges) const;
    QSize chromeSize() const;
    void updateTitleText();

    QString m_id;
    QString m_fullTitle;
    QWidget* m_titleBar;
    QLabel* m_title;
    QToolButton* m_close;
    QWidget* m_content;
    Corner m_corner = TopLeft;
    QPoint m_offset;
    bool m_dragging = false;
    bool m_resizable = true;
    Qt::Edges m_resizeEdges;   // bordes que se están arrastrando
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
