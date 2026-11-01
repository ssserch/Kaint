#pragma once

#include "core/effects/Effect.h"

#include <QList>
#include <QString>
#include <QStringList>

// Complementos de PhotoCraft y LightCraft, que van en la misma carpeta que los demás:
// · Filtros .wasm con la interfaz de complementos v1 de PhotoCraft (pc_abi_version, pc_manifest,
//   pc_alloc y pc_filter): se ejecutan en el intérprete de WebAssembly de Kaint, sin acceso a nada
//   de fuera, y su diálogo sale de los parámetros que declaran.
// · "Looks" .cube y .3dl (tablas de color 3D, o 1D en .cube), con una Cantidad.
// · Ajustes de revelado .xmp (los de Camera Raw y Lightroom): Kaint aplica los suyos (exposición,
//   contraste, luces y sombras, blancos y negros, temperatura, claridad, intensidad, saturación,
//   HSL, curvas, virado, gradación de color, blanco y negro, viñeta y grano), con una Cantidad.
namespace CraftPlugins {

// Lo que se leyó de un archivo: sus datos para Configuración > Complementos y sus efectos (la
// propiedad pasa al que llama).
struct Info {
    QString kind;
    QString name;
    QString version;
    QString author;
    QString description;
    QList<Effect*> effects;
    QStringList errors;
};

// Las extensiones que se reconocen (".wasm", ".cube", ".3dl", ".xmp").
QStringList fileFilters();
bool isCraftFile(const QString& path);
Info load(const QString& path);

// Para las pruebas: una tabla de color aplicada a un color (0-1), y los ajustes de un .xmp.
bool parseLut(const QByteArray& data, bool threeDl, QString* error, int* size = nullptr);

} // namespace CraftPlugins
