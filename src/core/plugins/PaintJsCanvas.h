#pragma once

// Canvas 2D de los complementos de paint.js (PaintJs.cpp): las funciones nativas que usan las
// clases HTMLCanvasElement, OffscreenCanvas y CanvasRenderingContext2D del entorno de los
// complementos. Dibuja con QPainter sobre una QImage premultiplicada.

#include "quickjs.h"

#include <QImage>

namespace PaintJs::detail {

// Añade las funciones canvas* al objeto 'native' que recibe el preludio.
void installCanvas(JSContext* ctx, JSValueConst native);
// La imagen (RGBA premultiplicado) de un objeto de canvasNew; null si no lo es.
const QImage* canvasImage(JSValueConst value);

} // namespace PaintJs::detail
