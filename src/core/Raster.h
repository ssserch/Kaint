#pragma once

#include "core/BlendMode.h"
#include "core/Selection.h"

#include <QColor>
#include <QImage>
#include <QRect>

// Utilidades de bajo nivel sobre imágenes de capa (ARGB32 con alfa directo).
namespace Raster {

// Copia el rectángulo 'r' de src a dst (mismas coordenadas, mismo formato).
void copyRect(QImage& dst, const QImage& src, const QRect& r);

// Copia todo 'src' dentro de 'dst' en la posición 'at' (recortando).
void blit(QImage& dst, const QImage& src, QPoint at);

// "Calidad de selección" de la barra de herramientas: con el borde suavizado o pixelado
// (umbral al 50 %).
void setSelectionAntialias(bool on);
bool selectionAntialias();
inline int clipCoverage(int maskValue, bool antialias) { return antialias ? maskValue : (maskValue >= 128 ? 255 : 0); }

// Multiplica cada píxel de 'img' (situada en 'origin' dentro del lienzo) por la cobertura de
// la selección (con alfa directo, solo el alfa).
void applySelection(QImage& img, QPoint origin, const Selection& selection);

// Restaura 'before' en 'r' y aplica 'source' (tamaño de r, alfa directo o premultiplicada) con
// el modo de fusión indicado.
void compositeRegion(QImage& layer, const QImage& before, const QImage& source, const QRect& r, BlendMode mode,
                     const Selection& selection, const QImage& coverage = QImage());

// Rellena 'out' con un color o una trama de FillPattern alineada al lienzo ('origin' es la
// posición de out dentro del lienzo). Respeta el formato de out.
void renderFill(QImage& out, QPoint origin, int pattern, QColor fg, QColor bg);

// Pincel de textura con la trama (para dibujar con QPainter); hay que fijar el origen del
// pincel a -posición de la imagen para alinearla al lienzo.
QBrush patternBrush(int pattern, QColor fg, QColor bg);

// Convierte una superposición dibujada con QPainter (premultiplicada, color ya multiplicado
// por la cobertura) en el color "puro" con alfa directo que espera compositeRegion junto a la
// cobertura; así "Sobrescribir" sustituye el color correcto en los bordes.
QImage overlayToSource(const QImage& overlay, const QImage& coverage);

// Borra (hace transparentes) los píxeles seleccionados.
void eraseSelection(QImage& layer, const Selection& selection);

// Recorta la capa a los límites de la selección y aplica su máscara.
QImage extractSelection(const QImage& layer, const Selection& selection);

// Conversiones entre alfa directo (capas) y premultiplicado, en paralelo
// (QImage::convertToFormat usa un solo núcleo).
QImage premultiplied(const QImage& straight);
QImage unpremultiplied(const QImage& premul);

// Tablero de ajedrez para mostrar la transparencia.
// Damero de la transparencia, con los grises del lienzo (como Paint.NET).
QImage checkerboard(int cell, QColor light = QColor(191, 191, 191), QColor dark = QColor(143, 143, 143));

QImage thumbnail(const QImage& image, QSize box, bool checker = true);

} // namespace Raster
