#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QTransform>

// Remuestreo de imágenes (Imagen > Cambiar tamaño y herramientas de mover).
namespace Resample {

enum class Method {
    Bicubic,
    BicubicSmooth,
    Bilinear,
    BilinearLow,
    Adaptive,
    Lanczos,
    Supersampling,
    NearestNeighbor,
};

QString name(Method method);
const QList<Method>& all();

// Escala una imagen (capa con alfa directo o premultiplicada; el resultado conserva el
// formato).
QImage scale(const QImage& src, QSize size, Method method, bool gammaCorrect = false);

// Interpolación para transformaciones libres (rotar/escalar con la herramienta Mover): vecino
// más cercano, bilineal o bicúbica.
enum class Interpolation { NearestNeighbor, Bilinear, Bicubic };
QString name(Interpolation interpolation);

// Dibuja 'src' transformada por 'transform' sobre 'dst' (src-over), limitado a 'clip'.
void drawTransformed(QImage& dst, const QImage& src, const QTransform& transform, Interpolation interpolation,
                     const QRect& clip, bool gammaCorrect = false);

} // namespace Resample
