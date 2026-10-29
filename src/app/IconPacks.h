#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Paquetes de iconos con el formato de los de paint.js: un ZIP con asset-pack.json y las imágenes
// que sustituye (icons/<archivo>.png, cursors/<archivo>.png...), con los mismos nombres de archivo
// que usa Kaint en data/ui-icons. Se instalan en la carpeta de datos de Kaint; el activo
// reemplaza los iconos que trae (también los de herramientas dibujados) y los demás siguen siendo
// los de Kaint.
namespace IconPacks {

struct Pack {
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
    // Carpeta donde está instalado; vacía en el predeterminado (los iconos de Kaint).
    QString folder;
    int images = 0;
};

// Identificador del paquete predeterminado (el mismo que el de paint.js).
QString defaultId();
// Carpeta donde se instalan los paquetes.
QString root();
// El predeterminado primero y después los instalados, por nombre.
QList<Pack> installed();
QString activeId();
Pack active();
// Activa un paquete (el predeterminado si no está instalado) y avisa con notifier()->changed().
void activate(const QString& id);
// Instala un ZIP (o lo sustituye si ya había uno con el mismo id); devuelve su id, o vacío con el
// motivo en error.
QString install(const QString& zipPath, QString* error);
bool remove(const QString& id);

// Ruta de <carpeta>/<nombre>.png (o .webp) en el paquete activo, o vacío si no la trae.
QString file(const QString& folder, const QString& name);
// Cambia con cada activación (para las claves de las cachés de iconos).
int generation();

class Notifier : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void changed();
};
Notifier* notifier();

} // namespace IconPacks
