#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>

// Windows: los formatos que Qt no trae allí (JPEG XR, HEIC, AVIF y JPEG XL) con los códecs de
// Windows Imaging Component. JPEG XR viene con Windows; HEIC y AVIF, con las extensiones de imagen
// HEIF, HEVC y AV1 de Microsoft Store, y JPEG XL con la suya (Windows 11). Solo aparecen los que el
// sistema tiene instalados. En los demás sistemas no hace nada (allí están los de kimageformats).
namespace Wic {

// Extensiones (sin punto y unificadas: "jxr", "heic", "avif", "jxl") que Windows puede leer o
// escribir ahora mismo.
bool canRead(const QString& suffix);
bool canWrite(const QString& suffix);
QStringList readableSuffixes();

// La primera imagen del archivo (girada según su orientación), con su resolución y su perfil de color.
QImage decode(const QByteArray& data, QString* error = nullptr);
// 'quality' 1..100; con 100, sin pérdida si el formato lo permite.
QByteArray encode(const QImage& image, const QString& suffix, int quality, QString* error = nullptr);

} // namespace Wic
