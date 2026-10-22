#pragma once

#include <QImage>
#include <QString>

class Document;

// Lectura de imágenes de Paint.NET (.pdn).
//
// Formato: "PDN3", tamaño de 24 bits y una cabecera XML (con la miniatura en
// PNG); luego la serialización binaria de .NET (MS-NRBF) del documento
// (PaintDotNet.Document > LayerList > BitmapLayer > Surface > MemoryBlock) y,
// detrás, los píxeles de cada MemoryBlock en bloques comprimidos con gzip.
// Los archivos de Paint.NET 2.0 y anteriores son esa serialización entera
// comprimida con gzip. Sirve para los .pdn de Paint.NET 3.x, 4.x y 5.x.
//
// Kaint no escribe .pdn: se guardan con capas en OpenRaster (.ora).
namespace Pdn {

Document* load(const QString& path, QString* error);
// La miniatura de la cabecera (sin leer las capas).
QImage thumbnail(const QString& path);

} // namespace Pdn
