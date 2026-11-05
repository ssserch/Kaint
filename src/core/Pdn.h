#pragma once

#include <QImage>
#include <QString>

class Document;

// Lectura y escritura de imágenes de Paint.NET (.pdn).
namespace Pdn {

Document* load(const QString& path, QString* error);
// Guarda con todas las capas (nombre, visibilidad, opacidad y modo de fusión), la resolución y la
// miniatura, con la misma estructura que Paint.NET 5: se abre en Paint.NET y en Kaint.
QByteArray encode(const Document* doc, QString* error);
bool save(const Document* doc, const QString& path, QString* error);
// La miniatura de la cabecera (sin leer las capas).
QImage thumbnail(const QString& path);

} // namespace Pdn
