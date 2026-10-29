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

// Vista del documento: compone las capas en una caché, la dibuja con zoom y desplazamiento y
// reparte los eventos del ratón/tableta a la herramienta activa.
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
    // Rectángulo de la imagen dibujado como el contorno de una selección rectangular: tinte
    // azul y hormigas en los bordes de los píxeles (la herramienta Zum lo usa para su recuadro).
    void drawMarchingRect(QPainter& p, const QRectF& imageRect, bool tint) const;
    // Hormigas en movimiento también sin selección, mientras una herramienta las dibuja.
    void setOverlayAnts(bool on);
    void updateCursor();
    void setToolCursor(const QCursor& cursor);

    // Reglas y unidades de medida
    void setRulersVisible(bool on);
    void setRulerStyle(Ruler::Style style);
    bool rulersVisible() const { return m_rulersVisible; }
    void setUnit(Unit unit);
    Unit unit() const { return m_unit; }
    QPointF cursorImagePos() const { return viewToImage(m_mouseView); }
    bool cursorInside() const { return m_mouseInside; }

    // Ajustes del lienzo (diálogo Configuración)
    void setAppearance(bool dropShadow, const QColor& border, int checkerBrightness);
    void setOverscroll(bool on);
    bool overscroll() const { return m_overscroll; }
    void setAutoScroll(bool on) { m_autoScroll = on; }

signals:
    void zoomChanged(qreal zoom);
    void rulersVisibilityChanged(bool visible);
    void cursorMoved(QPoint imagePos, bool inside);
    void deselectRequested();
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
    void originLimits(qreal content, qreal view, qreal* lo, qreal* hi) const;
    void clampOrigin();
    void applyZoom(qreal zoom, std::optional<QPointF> anchor);
    void fitInitial();
    void updateScrollBars();
    void centerOn(const QPointF& imagePoint);
    qreal edgeOrigin(Qt::Orientation axis, bool start) const;
    ToolEvent makeEvent(QMouseEvent* e) const;
    void onPixelsChanged(const QRect& r);
    void onSizeChanged();
    void onSelectionChanged();
    void onColorSpaceChanged();
    void renderRegion(const QRect& r);
    void drawSelection(QPainter& p) const;
    void drawPixelGrid(QPainter& p, const QRect& src) const;
    QRect brushOutlineRect(const QPointF& center) const;
    void layoutRulers();
    void updateRulers();
    bool keyboardScroll(QKeyEvent* e);
    void updateAutoScroll(const QPointF& viewPos);
    void autoScrollStep();
    void followPointer();
    const QImage& selectionTint() const;

    Document* m_doc = nullptr;
    Tool* m_tool = nullptr;
    QImage m_composite;
    QColorTransform m_toDisplay;
    qreal m_zoom = 1.0;
    QPointF m_origin;
    bool m_overscroll = true;
    bool m_syncingBars = false;
    QSize m_lastViewport;
    bool m_fitPending = false;
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
    QPointF m_lastToolPos;
    Qt::MouseButtons m_lastButtons;
    Qt::KeyboardModifiers m_lastModifiers;
    QPointF m_mouseView;
    bool m_mouseInside = false;

    QTimer m_antsTimer;
    int m_antsOffset = 0;
    bool m_overlayAnts = false;
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
    mutable QImage m_tint;
    mutable const void* m_tintKey = nullptr;
};
