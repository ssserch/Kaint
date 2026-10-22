#pragma once

#include <QByteArray>
#include <QImage>

// Codificador PNG propio para lo que el de Qt no ofrece: imágenes entrelazadas (Adam7), que se
// ven completas a baja resolución mientras se descargan. Escribe color indexado (con
// transparencia), RGB o RGBA de 8 bits por canal y la resolución (pHYs).
namespace Png {

QByteArray encode(const QImage& image, bool interlaced);

} // namespace Png
