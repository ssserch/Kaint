#pragma once

#include "core/effects/Effect.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class Document;

// Complementos de paint.js (paquetes .paintjs: un ZIP con plugin.json, plugin.js y una carpeta
// assets/ opcional), como los de la versión web: añaden ajustes, efectos y formatos de archivo
// con la API de JavaScript de esos paquetes (paintDotJs.registerPlugin, versión 1).
//
// El código se ejecuta con QuickJS-ng (third_party/quickjs-ng) en un entorno aislado: sin
// archivos, procesos ni red. Para que funcionen los paquetes hechos para el navegador hay un
// subconjunto de lo que este les da (canvas 2D dibujado con QPainter, ImageData, TextEncoder y
// TextDecoder, Blob...). Lo que necesita una página web de verdad (las ventanas propias en HTML
// de paintDotJs.dialogs.create y el resto del DOM) no está: esos efectos se marcan como «solo en
// la versión web» y no se añaden a los menús.
namespace PaintJs {

constexpr int kApiVersion = 1;

bool isPackage(const QString& path);

// Un formato de archivo de un complemento.
struct FileType {
    QString plugin;  // ruta del paquete
    QString id;
    QString name;
    QStringList extensions;  // en minúsculas y con el punto (".psd")
    bool canRead = false;
    bool canWrite = false;
    bool layers = false;

    QString filter() const;  // "Photoshop (*.psd)"
};

// Lo que se leyó de un paquete.
struct Info {
    QString id;  // el de plugin.json ("autor.nombre")
    QString name;
    QString version;
    QString author;
    QString copyright;
    QString website;
    QString description;
    QString license;
    QList<Effect*> effects;  // la propiedad pasa al que llama
    // Efectos que abren una ventana propia hecha para el navegador: solo en la versión web.
    QStringList browserOnly;
    QList<QImage> browserOnlyIcons;
    QList<FileType> fileTypes;
    QStringList errors;
};

Info load(const QString& path);

// Formatos de los complementos cargados (los registra PluginManager y los usa ImageIO). Con
// 'conflict', la extensión que ya tenía otro complemento.
bool registerFileTypes(const QString& plugin, const QList<FileType>& types, QString* conflict = nullptr);
void unregisterFileTypes(const QString& plugin);
QList<FileType> fileTypes();
std::optional<FileType> fileTypeFor(const QString& path, bool forWriting);

Document* decode(const FileType& type, const QString& path, QString* error);
bool encode(const FileType& type, const Document* doc, const QString& path, QString* error);

// Pruebas: ejecuta 'code' en un entorno como el de los complementos (sin paquete) y devuelve el
// resultado como texto (o "Error: ...").
QString evaluateForTesting(const QString& code);

} // namespace PaintJs
