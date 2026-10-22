#pragma once

#include "app/Ruler.h"

#include <QAbstractScrollArea>
#include <QColorTransform>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include <optional>

class Document;
class Tool;
struct ToolEvent;

// Vista del documento: compone las capas en una caché, la dibuja con zoom y
// desplazamiento y reparte los eventos del ratón/tableta a la herramienta
// activa. El 100 % equivale a un píxel físico por píxel de imagen, también
// con escalado fraccionario en Wayland.
class Canvas : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit Canvas(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    void forgetDocument(Document* doc);

    void setTool(Tool* tool);
    Tool* tool() const { return m_tool; }

    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom, std::optional<QPointF> anchor = std::nullopt);
    void zoomIn(std::optional<QPointF> anchor = std::nullopt);
    void zoomOut(std::optional<QPointF> anchor = std::nullopt);
    void zoomToWindow();
    // Ctrl+B dos veces: la primera ajusta a la ventana y la segunda vuelve al zoom anterior.
    void toggleZoomToWindow();
    void zoomToRect(const QRectF& imageRect);
    void actualSize();
    static const QList<qreal>& zoomPresets();

    qreal scale() const;
    QPointF viewToImage(const QPointF& p) const;
    QPointF imageToView(const QPointF& p) const;
    QRectF imageToView(const QRectF& r) const;
    QTransform imageToViewTransform() const;
    QRect visibleImageRect() const;
    void scrollByView(const QPointF& delta);

    const QImage& composite() const { return m_composite; }
    bool pixelGrid() const { return m_pixelGrid; }
    void setPixelGrid(bool on);

    void updateOverlay();
    void updateCursor();
    // Cursor que fija la herramienta al pasar sobre tiradores, etc.
    void setToolCursor(const QCursor& cursor);

    // Reglas y unidades de medida
    void setRulersVisible(bool on);
    bool rulersVisible() const { return m_rulersVisible; }
    void setUnit(Unit unit);
    Unit unit() const { return m_unit; }
    QPointF cursorImagePos() const { return viewToImage(m_mouseView); }
    bool cursorInside() const { return m_mouseInside; }

    // Ajustes del lienzo (diálogo Configuración)
    void setAppearance(bool dropShadow, const QColor& border, int checkerBrightness);
    // "Desplazamiento más allá del borde de la imagen" (overscroll) de Paint.NET.
    void setOverscroll(bool on);
    bool overscroll() const { return m_overscroll; }
    // "Desplazamiento automático al dibujar en el borde de la ventana".
    void setAutoScroll(bool on) { m_autoScroll = on; }

signals:
    void zoomChanged(qreal zoom);
    void cursorMoved(QPoint imagePos, bool inside);
    // Intro sin nada que terminar: anular la selección (como Paint.NET).
    void deselectRequested();
    // El lápiz de la tableta cambió entre la punta y el extremo borrador.
    void tabletEraserChanged(bool eraser);

protected:
    bool event(QEvent* e) override;
    bool viewportEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    struct ViewState {
        qreal zoom = 1;
        QPointF origin;
    };

    QPointF origin() const;
    // Límites de m_origin en un eje (contenido y vista en píxeles lógicos).
    void originLimits(qreal content, qreal view, qreal* lo, qreal* hi) const;
    void clampOrigin();
    void applyZoom(qreal zoom, std::optional<QPointF> anchor);
    void fitInitial();
    void updateScrollBars();
    void centerOn(const QPointF& imagePoint);
    // Origen que muestra el borde inicial (start) o final de la imagen en un eje.
    qreal edgeOrigin(Qt::Orientation axis, bool start) const;
    ToolEvent makeEvent(QMouseEvent* e) const;
    void onPixelsChanged(const QRect& r);
    void onSizeChanged();
    void onSelectionChanged();
    void onColorSpaceChanged();
    // Compone 'r' en la caché (y la pasa a sRGB si la imagen tiene otro perfil).
    void renderRegion(const QRect& r);
    void drawSelection(QPainter& p) const;
    void drawPixelGrid(QPainter& p, const QRect& src) const;
    QRect brushOutlineRect(const QPointF& center) const;
    void layoutRulers();
    void updateRulers();
    bool keyboardScroll(QKeyEvent* e);
    void updateAutoScroll(const QPointF& viewPos);
    void autoScrollStep();
    const QImage& selectionTint() const;

    Document* m_doc = nullptr;
    Tool* m_tool = nullptr;
    QImage m_composite;
    QColorTransform m_toDisplay; // perfil de la imagen -> sRGB (vacío si ya es sRGB)
    qreal m_zoom = 1.0;
    // Posición de la esquina superior izquierda de la imagen en la vista
    // (píxeles lógicos, sin redondear). Las barras de desplazamiento se
    // calculan a partir de ella, así el zoom no acumula errores de redondeo.
    QPointF m_origin;
    bool m_overscroll = true;
    bool m_syncingBars = false;
    QSize m_lastViewport;
    bool m_fitPending = false; // ajustar al tamaño definitivo de la ventana
    QHash<Document*, ViewState> m_views;

    bool m_panning = false;
    bool m_spaceDown = false;
    QPointF m_panLast;
    Qt::MouseButton m_toolButton = Qt::NoButton;

    qreal m_pressure = 1.0;
    bool m_tabletDown = false;
    bool m_eraserTip = false;

    bool m_autoScroll = true;
    QTimer m_autoScrollTimer;
    QPointF m_autoScrollDelta;
    QPointF m_lastToolPos; // posición (vista) del último movimiento con la herramienta
    Qt::MouseButtons m_lastButtons;
    Qt::KeyboardModifiers m_lastModifiers;
    QPointF m_mouseView;
    bool m_mouseInside = false;

    QTimer m_antsTimer;
    int m_antsOffset = 0;
    bool m_pixelGrid = false;
    QPixmap m_checker;
    qreal m_zoomBeforeFit = 0;
    QPointF m_centerBeforeFit;
    QList<Qt::MouseButton> m_extraButtons;

    bool m_rulersVisible = false;
    Unit m_unit = Unit::Pixels;
    Ruler* m_hRuler = nullptr;
    Ruler* m_vRuler = nullptr;
    QWidget* m_rulerCorner = nullptr;

    bool m_dropShadow = true;
    QColor m_border;
    mutable QImage m_tint;          // interior de la selección teñido (caché)
    mutable const void* m_tintKey = nullptr;
};
