#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QTransform>

class QPainter;

// Cuadro de transformación al estilo de las herramientas Mover de Paint.NET 5:
// un rectángulo en coordenadas locales más una transformación local -> lienzo.
//   · 8 tiradores redondos: escalar (Mayús = proporcional, Alt = desde el centro)
//   · arrastrar en cualquier otro sitio (o el icono de cuatro flechas): mover
//   · girar alrededor del punto central (Mayús = de 15 en 15°): con el botón
//     derecho, o con el izquierdo fuera del cuadro y cerca de su borde
//   · el punto central se ve siempre, gira con el cuadro y se puede mover;
//     si se aparta del centro aparece un botón para devolverlo.
class TransformBox {
public:
    enum class Mode { None, Translate, Scale, Rotate, Pivot, ResetPivot };
    // Franja (en píxeles de pantalla) alrededor del cuadro en la que se gira.
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

private:
    QRectF m_rect;
    QTransform m_transform;
    // Estado del arrastre
    Mode m_mode = Mode::None;
    int m_handle = -1;
    QTransform m_start;
    QPointF m_startPos;
    QPointF m_pivot;      // en el lienzo, durante un giro
    QPointF m_localPivot; // punto de giro en coordenadas locales
    QPointF resetButtonView(const QTransform& imageToView) const;
};
