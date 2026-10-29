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
    // Aspecto del marco: como las ventanas de paint.js (Windows 10: cabecera alta, botón de cerrar
    // cuadrado y esquinas rectas) o el compacto de Kaint.
    enum class Style { Windows10, Compact };

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
    // Se muestra delante de las demás con un fundido (si hay animaciones).
    void showAnimated();
    void fitToContent();

    // Grosor mínimo del borde que se puede arrastrar para cambiar el tamaño.
    static constexpr int kBorder = 5;
    // Estilo y grosor (en píxeles) del borde de todas las ventanas; se aplica con applyChrome().
    static void setChrome(Style style, int border);
    static Style chromeStyle();
    static int chromeBorder();
    void applyChrome();

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
    int titleInset() const;
    // Zona del área de trabajo donde puede estar la ventana (sin tapar las reglas).
    QRect area() const;
    // Con los temas de Paint.NET y el personalizado (no con el de KDE ni el clásico).
    bool windows10() const;
    // Grosor del borde dibujado y de la franja para cambiar el tamaño.
    int borderWidth() const;
    int grip() const;
    void updateTitleText();
    qreal restingOpacity() const;
    void fadeOpacity(qreal from, qreal to);

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
    bool m_translucent = false;
    QPointer<class QVariantAnimation> m_fade;
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
    void setPanelChrome(FloatingPanel::Style style, int border);
    // Zona para las ventanas: todo el área de trabajo menos las reglas, si se ven.
    QRect panelArea() const;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    Canvas* m_canvas;
    QList<FloatingPanel*> m_panels;
};
