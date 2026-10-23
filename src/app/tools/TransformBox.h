#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QTransform>

class QPainter;

// Cuadro de transformación al estilo de las herramientas Mover de Paint.NET 5: un rectángulo
// en coordenadas locales más una transformación local -> lienzo.
class TransformBox {
public:
    enum class Mode { None, Translate, Scale, Rotate, Pivot, ResetPivot };
    static constexpr double kRotateBand = 30;

    // Conserva el punto de giro si el rectángulo no cambia.
    void set(const QRectF& localRect, const QTransform& toCanvas);
    void resetPivot();
    bool pivotMoved() const;
    QRectF localRect() const { return m_rect; }
    QTransform transform() const { return m_transform; }
    QPolygonF canvasCorners() const;

    // 'inside' indica si el punto cae dentro del cuadro (aunque no toque un tirador).
    Mode hit(const QPointF& viewPos, const QTransform& imageToView, bool rightButton, int* handle, bool* inside = nullptr) const;
    void begin(Mode mode, int handle, const QPointF& canvasPos);
    Mode mode() const { return m_mode; }
    QTransform update(const QPointF& canvasPos, Qt::KeyboardModifiers modifiers);
    void end() { m_mode = Mode::None; }
    double rotationDegrees() const;
    QPointF pivot() const { return m_transform.map(m_localPivot); }

    void draw(QPainter& p, const QTransform& imageToView, bool handles) const;

    // Píxel a píxel: sin giro, las esquinas del rectángulo transformado quedan en píxeles enteros
    // (con giro, solo el desplazamiento).
    static QTransform snappedToPixels(const QTransform& t, const QRectF& localRect);

private:
    QRectF m_rect;
    QTransform m_transform;
    Mode m_mode = Mode::None;
    int m_handle = -1;
    QTransform m_start;
    QPointF m_startPos;
    QPointF m_pivot;
    QPointF m_localPivot;
    QPointF resetButtonView(const QTransform& imageToView) const;
};
