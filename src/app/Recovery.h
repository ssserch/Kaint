#pragma once

#include <QList>
#include <QString>

class Document;

// Copias de recuperación: cada pocos minutos se guardan en OpenRaster las imágenes con cambios
// sin guardar, en una carpeta por proceso con un bloqueo (QLockFile). Al cerrar Kaint con
// normalidad se borran; si se cierra de golpe, al abrirlo otra vez se ofrecen para recuperarlas.
namespace Recovery {

struct Entry {
    QString file;
    QString originalPath;
    QString name;
};

QString rootDir();
void setRootForTesting(const QString& dir);

// Guarda las imágenes con cambios y borra las copias de las que ya no los tienen.
void save(const QList<Document*>& docs);
void clear();
// Copias de sesiones que ya no están abiertas (Kaint se cerró de golpe).
QList<Entry> orphaned();
void discard(const QList<Entry>& entries);

} // namespace Recovery
