#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Fuentes que el usuario añade para la herramienta Texto (Configuración > Fuentes), como en
// paint.js: se copian a la carpeta de datos de Kaint y se cargan solo en Kaint, sin instalarlas
// en el sistema.
namespace UserFonts {

struct Font {
    QString file;
    QStringList families;
};

QString folder();
// Carga las fuentes de la carpeta (al abrir Kaint).
void loadAll();
QList<Font> installed();
// Copia y carga los archivos; devuelve las familias nuevas y, en errors, los que no se pudieron.
QStringList add(const QStringList& files, QStringList* errors);
bool remove(const QString& file);
void removeAll();

} // namespace UserFonts
