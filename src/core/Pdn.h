#pragma once

#include <QImage>
#include <QString>

class Document;

// Lectura de imágenes de Paint.NET (.pdn).
namespace Pdn {

Document* load(const QString& path, QString* error);
// La miniatura de la cabecera (sin leer las capas).
QImage thumbnail(const QString& path);

} // namespace Pdn
