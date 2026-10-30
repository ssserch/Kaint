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

// Interpolación para transformaciones libres (rotar/escalar con la herramienta Mover), como el
// Muestreo de Paint.NET: vecino más próximo, bilineal, bicúbica, multimuestra bilineal (cuatro
// muestras bilineales por píxel, bordes más suaves) y anisotrópica (más muestras en el eje en que
// la imagen se encoge más). Los valores se guardan en la configuración.
enum class Interpolation { NearestNeighbor, Bilinear, Bicubic, BilinearMultisample, Anisotropic };
QString name(Interpolation interpolation);
// En el orden del menú de Paint.NET.
const QList<Interpolation>& interpolations();

// Para reducir sin dientes de sierra: si 'transform' encoge la imagen, la devuelve ya reducida
// promediando por áreas (como Paint.NET) y ajusta 'transform' a la nueva imagen. Guarda el
// último resultado, así arrastrar no la recalcula.
QImage prefiltered(const QImage& src, QTransform* transform, bool gammaCorrect = false);

// Dibuja 'src' transformada por 'transform' sobre 'dst' (src-over), limitado a 'clip'.
void drawTransformed(QImage& dst, const QImage& src, const QTransform& transform, Interpolation interpolation,
                     const QRect& clip, bool gammaCorrect = false);

} // namespace Resample
