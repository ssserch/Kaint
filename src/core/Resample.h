#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QTransform>

// Remuestreo de imágenes (Imagen > Cambiar tamaño y herramientas de mover).
namespace Resample {

enum class Method {
    Bicubic,        // Catmull-Rom (b=0, c=0,5): el predeterminado de Paint.NET
    BicubicSmooth,  // B-spline (b=1, c=0): sin realce ni halos
    Bilinear,       // triángulo con prefiltro al reducir
    BilinearLow,    // bilineal simple de 2x2
    Adaptive,       // elige según la proporción de escalado
    Lanczos,        // Lanczos de 3 lóbulos
    Supersampling,  // media ponderada del área (Fant)
    NearestNeighbor,
};

QString name(Method method);
const QList<Method>& all();

// Escala una imagen (capa con alfa directo o premultiplicada; el resultado
// conserva el formato). Con gammaCorrect los cálculos se hacen en luz
// lineal, lo que conserva el brillo de los detalles finos.
QImage scale(const QImage& src, QSize size, Method method, bool gammaCorrect = false);

// Interpolación para transformaciones libres (rotar/escalar con la
// herramienta Mover): vecino más cercano, bilineal o bicúbica.
enum class Interpolation { NearestNeighbor, Bilinear, Bicubic };
QString name(Interpolation interpolation);

// Dibuja 'src' transformada por 'transform' sobre 'dst' (src-over), limitado
// a 'clip'. 'dst' puede tener alfa directo o premultiplicado. Con
// gammaCorrect se interpola en luz lineal ("Gamma corregido" de Paint.NET):
// conserva la luminancia de los detalles finos.
void drawTransformed(QImage& dst, const QImage& src, const QTransform& transform, Interpolation interpolation,
                     const QRect& clip, bool gammaCorrect = false);

} // namespace Resample
