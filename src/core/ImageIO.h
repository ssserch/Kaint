#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/Gif.h"

class Document;

namespace ImageIO {

QString openFilter();
QString saveFilter(QString* defaultFilter = nullptr);
QStringList supportedReadSuffixes();

bool isLayered(const QString& path);           // .ora conserva las capas
bool isWritable(const QString& path);          // Kaint puede guardar en este formato (.pdn no)
bool supportsQuality(const QString& path);     // jpg, webp, avif...
bool supportsAlpha(const QString& path);

// Opciones del diálogo "Configuración de guardado" de Paint.NET.
struct SaveOptions {
    int quality = 95;       // formatos con pérdida
    int bitDepth = 0;       // PNG/BMP/TGA: 0 = automática, 32, 24 u 8 (paleta)
    int dither = 7;         // 8 bits (y GIF): tramado 0..8
    int alphaThreshold = 128; // 8 bits (y GIF): por debajo, píxel transparente
    // GIF
    bool animation = true;  // cada capa visible es un fotograma (la de abajo, el primero)
    int frameDelay = 100;   // ms de las capas sin "(Nms)" en el nombre
    int loops = 0;          // 0 = repetir siempre, n = n veces más, -1 = una sola vez
    // ICO
    QList<int> icoSizes{16, 24, 32, 48, 64, 128, 256};
};

bool isAnimation(const QString& path);           // gif: las capas son fotogramas
bool isIcon(const QString& path);                // ico
// Paleta de 8 bits óptima (255 colores y uno transparente si hace falta;
// con 'transparentSlot' siempre se reserva el transparente).
QImage toIndexed8(const QImage& image, int dither, int alphaThreshold, bool transparentSlot = false);
// Fotogramas de un GIF animado: las capas visibles, la de abajo primero.
QList<Gif::Frame> animationFrames(const Document* doc, int defaultDelay);

bool hasBitDepth(const QString& path);           // png, bmp, tga
int automaticBitDepth(const QImage& flattened);  // 32 si hay transparencia, 24 si no

Document* load(const QString& path, QString* error);
// Imagen tal como se escribirá (acoplada, sin alfa si el formato no lo
// admite y con la profundidad elegida).
QImage prepare(const QImage& flattened, const QString& path, const SaveOptions& options);
// Codifica en memoria (vista previa y tamaño del archivo en el diálogo).
QByteArray encode(const QImage& prepared, const QString& path, const SaveOptions& options, QString* error);
bool save(const Document* doc, const QString& path, const SaveOptions& options, QString* error);
inline bool save(const Document* doc, const QString& path, int quality, QString* error)
{
    SaveOptions o;
    o.quality = quality;
    return save(doc, path, o, error);
}

} // namespace ImageIO
