#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QRectF>

class QPainter;

// Tiradores ("nubs" en Paint.NET) para editar formas, líneas, degradados,
// rellenos y selecciones antes de terminarlos. Se dibujan y se prueban en
// coordenadas del widget para que tengan siempre el mismo tamaño en pantalla.
namespace Handles {

enum class Shape { Square, Circle, Move, Rotate, ResetPivot };

constexpr qreal kHitRadius = 8;

void draw(QPainter& p, const QPointF& viewPos, Shape shape);
// Punto de giro (círculo con una cruz girada 'degrees').
void drawPivot(QPainter& p, const QPointF& viewPos, double degrees);
inline bool hit(const QPointF& viewPos, const QPointF& handleView, qreal radius = kHitRadius)
{
    const QPointF d = viewPos - handleView;
    return d.x() * d.x() + d.y() * d.y() <= radius * radius;
}

// Posición del icono de mover (cuatro flechas), justo fuera de la esquina
// inferior derecha de unos límites en pantalla.
QPointF moveIconPos(const QRectF& viewBounds);

// Cuadro de transformación: 8 tiradores sobre un rectángulo transformado.
// Índices: 0 sup-izq, 1 sup, 2 sup-der, 3 der, 4 inf-der, 5 inf, 6 inf-izq, 7 izq.
QPointF boxHandle(const QRectF& r, int index);
QPointF oppositeHandle(const QRectF& r, int index);
bool isCorner(int index);
// Signo del eje que mueve cada tirador (-1, 0, +1).
QPointF handleAxes(int index);

void drawBox(QPainter& p, const QPolygonF& viewCorners);

} // namespace Handles
