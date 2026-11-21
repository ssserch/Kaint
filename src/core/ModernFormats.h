#pragma once

#include "core/Gif.h"

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

// Formatos modernos con códecs propios (como paint.rs), si Kaint se compiló con sus bibliotecas:
// WebP (libwebp), AVIF (libavif), JPEG XL (libjxl) y HEIC/HEIF (libheif, solo lectura). Así se
// abren y se guardan aunque el sistema no tenga qt6-imageformats ni kimageformats (el AppImage, la
// versión de Windows sin las extensiones de Microsoft Store...). Sin las bibliotecas, Qt o WIC.
namespace ModernFormats {

bool canRead(const QString& suffix);
bool canWrite(const QString& suffix);
// La imagen (ARGB32 de alfa directo) o nula (con el porqué en 'error').
QImage decode(const QString& suffix, const QByteArray& data, QString* error);
// 'quality' de 0 a 100; 100, sin pérdida (WebP y JPEG XL) o la máxima calidad (AVIF).
QByteArray encode(const QString& suffix, const QImage& image, int quality, QString* error);
// WebP animado (libwebpmux), para exportar desde el editor de GIF: 'quality' 100, sin pérdida;
// 'loops' 0, se repite siempre.
bool canWriteAnimatedWebP();
QByteArray encodeAnimatedWebP(const QList<Gif::Frame>& frames, int quality, int loops, QString* error);
// Para Ayuda > Acerca de y los diagnósticos: los que están ("WebP, AVIF, JPEG XL, HEIC").
QString available();

} // namespace ModernFormats
