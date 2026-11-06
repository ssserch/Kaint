#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

// Formatos que Kaint lee y escribe por su cuenta, sin depender de los complementos de imagen de Qt
// (qt6-imageformats o kimageformats no siempre están, y el de TGA de Qt solo lee): así se pueden
// guardar en cualquier sistema, como en Paint.NET. Los mismos que escribe Kaint para la web
// (KaintHTML/js/core/Codecs.js).
namespace Tga {

// 32 bits (con alfa), 24 bits u 8 bits (con mapa de colores), según el formato de 'image'
// (ARGB32, RGB888 o Indexed8), sin comprimir y de arriba abajo, con el pie de TGA 2.0.
QByteArray encode(const QImage& image);
// Sin comprimir o RLE, en color, grises o con mapa de colores, de 8, 15, 16, 24 o 32 bits.
QImage decode(const QByteArray& data, QString* error = nullptr);

} // namespace Tga

namespace Dds {

// Superficie de DirectDraw sin comprimir (A8R8G8B8), la que leen todos los programas.
QByteArray encode(const QImage& image);
// Sin comprimir (con sus máscaras de color), DXT1, DXT3 y DXT5 (BC1, BC2 y BC3), también con la
// cabecera DX10. La primera superficie (sin mipmaps).
QImage decode(const QByteArray& data, QString* error = nullptr);

} // namespace Dds

// QOI («Quite OK Image»): sin pérdida, rápido y sencillo (https://qoiformat.org).
namespace Qoi {

QByteArray encode(const QImage& image);
QImage decode(const QByteArray& data, QString* error = nullptr);

} // namespace Qoi
