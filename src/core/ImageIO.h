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

bool isLayered(const QString& path);
bool isWritable(const QString& path);
bool supportsQuality(const QString& path);
bool supportsAlpha(const QString& path);

// Opciones del diálogo "Configuración de guardado" de Paint.NET.
struct SaveOptions {
    int quality = 95;
    int bitDepth = 0;
    int dither = 7;
    int alphaThreshold = 128;
    bool animation = true;
    int frameDelay = 100;
    int loops = 0;
    QList<int> icoSizes{16, 24, 32, 48, 64, 128, 256};
};

bool isAnimation(const QString& path);
bool isIcon(const QString& path);
// Paleta de 8 bits óptima (255 colores y uno transparente si hace falta; con 'transparentSlot'
// siempre se reserva el transparente).
QImage toIndexed8(const QImage& image, int dither, int alphaThreshold, bool transparentSlot = false);
// Fotogramas de un GIF animado: las capas visibles, la de abajo primero.
QList<Gif::Frame> animationFrames(const Document* doc, int defaultDelay);

bool hasBitDepth(const QString& path);
int automaticBitDepth(const QImage& flattened);

Document* load(const QString& path, QString* error);
// Imagen tal como se escribirá (acoplada, sin alfa si el formato no lo admite y con la
// profundidad elegida).
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
