#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

class Document;

// Photoshop: documentos .psd y los de tamaño grande (.psb), con sus capas de píxeles.
//
// Al abrir, las capas conservan el nombre, la visibilidad, la opacidad y el modo de fusión (los diez
// que Kaint y Photoshop tienen en común), en RGB o escala de grises de 8, 16 o 32 bits por canal (sin
// comprimir, PackBits o ZIP, con o sin predicción); también se abren los documentos indexados y de
// mapa de bits, sin capas. Las máscaras de píxeles se aplican a la transparencia y los grupos que no
// cambian nada al acoplarlos (de paso o normales, opacos y sin máscara) se deshacen. Si algo no se
// puede representar con capas de Kaint sin cambiar el aspecto (máscaras de recorte, efectos de capa,
// máscaras vectoriales, capas de ajuste o de relleno, grupos con opacidad o fusión propias u otros
// modos de fusión), se abre la imagen combinada que guarda el archivo, como una sola capa.
//
// Al guardar se escribe un PSD de 8 bits por canal en RGB con transparencia: una capa por cada capa
// de Kaint (nombre, visibilidad, opacidad y modo de fusión) y la imagen combinada.
namespace Psd {

// 'warning': si al abrir hubo que convertir algo (más de 8 bits, máscaras aplicadas, PSB, grupos,
// texto o capas fuera del lienzo...), qué cambió. Entonces el documento se abre sin archivo: guardar
// pide un nombre nuevo y no se pisa el original.
Document* load(const QString& path, QString* error, QString* warning = nullptr);
Document* decode(const QByteArray& data, QString* error, QString* warning = nullptr);

// La imagen combinada (la guardada en el archivo o, si no la tiene, la de sus capas).
QImage readComposite(const QString& path, QString* error = nullptr);
QImage decodeComposite(const QByteArray& data, QString* error = nullptr);

// PSD de 8 bits en RGB con alfa. Falla si una capa usa un modo de fusión que Photoshop no tiene
// (Reflejar, Resplandor, Negación, XOR) o si la imagen pasa de 30000 píxeles por lado.
QByteArray encode(const Document* doc, QString* error);
bool save(const Document* doc, const QString& path, QString* error);

// El modo de fusión de Kaint tiene uno igual en Photoshop.
bool canWriteBlend(int blendMode);

} // namespace Psd
