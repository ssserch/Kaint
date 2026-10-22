#pragma once

#include "core/BlendMode.h"
#include "core/Selection.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPoint>

namespace FloodFill {

// Umbral de comparación de color a partir de una tolerancia 0..1.
int threshold(qreal tolerance);

// Compara dos colores sin premultiplicar con la métrica de Paint.NET/Pinta: la diferencia de
// color pesa según la opacidad del candidato.
inline bool similar(QRgb candidate, QRgb reference, int threshold, bool straight = false)
{
    if (straight) {
        const int dr = qRed(candidate) - qRed(reference), dg = qGreen(candidate) - qGreen(reference);
        const int db = qBlue(candidate) - qBlue(reference), da = qAlpha(candidate) - qAlpha(reference);
        return dr * dr + dg * dg + db * db + da * da <= threshold;
    }
    const int a = qAlpha(candidate);
    int sum = 0;
    int diff = qRed(candidate) - qRed(reference);
    sum += (1 + diff * diff) * a / 256;
    diff = qGreen(candidate) - qGreen(reference);
    sum += (1 + diff * diff) * a / 256;
    diff = qBlue(candidate) - qBlue(reference);
    sum += (1 + diff * diff) * a / 256;
    diff = a - qAlpha(reference);
    sum += diff * diff;
    return sum <= threshold;
}

// Devuelve una máscara Alpha8 del tamaño de 'source' con 255 en los píxeles parecidos al de
// 'seed'.
QImage mask(const QImage& source, QPoint seed, qreal tolerance, bool contiguous, bool straight = false);

// Píxel del borde de una zona rellenada cuyo color mezcla el de la zona con el del contorno
// (borde suavizado).
struct EdgePixel {
    QPoint pos;
    int coverage = 0;
    QRgb interior = 0;
};

// Busca el borde suavizado de 'mask' en 'sample': la fila de píxeles de la máscara que tocan
// el exterior y hasta dos píxeles por fuera.
QList<EdgePixel> antialiasEdge(const QImage& sample, const QImage& mask);

// "Desmezcla": sustituye en 'pixel' (alfa directo) la parte de color 'interior'
// (premultiplicado) por 'fill' (alfa directo), en la proporción 'coverage'.
QRgb replaceInterior(QRgb pixel, QRgb interior, QRgb fill, int coverage);

// Opciones del bote de pintura.
struct BucketOptions {
    qreal tolerance = 0.5;
    bool contiguous = true;
    bool toleranceStraight = false;
    bool antialias = true;
    BlendMode blend = BlendMode::Normal;
    int pattern = 0;
    QColor primary = Qt::black;
    QColor secondary = Qt::white;
    bool sampleIsLayer = true;
};

// Bote de pintura: restaura 'before' y rellena en 'layer' (alfa directo) la zona de 'sample'
// que parte de 'seed', recortada por la selección.
QRect paintBucket(QImage& layer, const QImage& before, const QImage& sample, QPoint seed, const BucketOptions& o,
                  const Selection& selection);

} // namespace FloodFill
