#pragma once

#include <QImage>
#include <QString>

class Document;

// OpenRaster (.ora): formato abierto con capas usado por Krita, GIMP y MyPaint. Es un ZIP con
// stack.xml (estructura y propiedades) y un PNG por capa.
namespace Ora {

bool save(const Document* doc, const QString& path, QString* error);
Document* load(const QString& path, QString* error);
// Miniatura guardada en el archivo (o la imagen combinada).
QImage readThumbnail(const QString& path);

} // namespace Ora
