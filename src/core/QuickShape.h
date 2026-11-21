#pragma once

#include <QList>
#include <QPointF>
#include <QString>

// Formas rápidas (como QuickShape de Procreate): al acabar un trazo a mano y dejar el puntero quieto,
// lo dibujado se reconoce como una línea, una elipse (un círculo si casi lo es), un rectángulo o un
// polígono, y se sustituye por la forma perfecta. Así se dibujan formas con el pincel o el lápiz,
// también con el dedo.
namespace QuickShape {

enum class Kind { None, Line, Ellipse, Circle, Rectangle, Triangle, Polygon };

struct Result {
    Kind kind = Kind::None;
    // La línea quebrada de la forma (las cerradas repiten el primer punto al final).
    QList<QPointF> points;
};

Result recognize(const QList<QPointF>& stroke);
// Su nombre, para el aviso junto al puntero («Círculo», «Rectángulo»...).
QString name(Kind kind);

} // namespace QuickShape
