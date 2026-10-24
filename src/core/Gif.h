#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

// GIF animados: cada fotograma es una capa (la de abajo es el primero, como en GIMP y los
// complementos de animación de Paint.NET) y su duración va en el nombre de la capa, "Fotograma
// 1 (100ms)", el mismo convenio de GIMP.
namespace Gif {

struct Frame {
    QImage image;
    int delayMs = 100;
    // Viene de una capa oculta (solo va en el GIF si se piden las capas ocultas).
    bool hidden = false;
};

struct Animation {
    QList<Frame> frames;
    int loops = 0;
};

// Lee todos los fotogramas ya compuestos (con su duración).
Animation read(const QString& path, QString* error);
// Lo mismo desde un GIF en memoria (la vista previa al guardar).
Animation decode(const QByteArray& data, QString* error = nullptr);

struct Options {
    int loops = 0;
    int dither = 7;
    int alphaThreshold = 128;
    // Paleta de 8 bits: 0 corte a la mediana, 1 octree.
    int quantization = 1;
    // Una sola paleta para todos los fotogramas (archivo más pequeño) en vez de una por fotograma.
    bool globalPalette = false;
    // Cada fotograma se dibuja sobre el anterior; si no (por defecto), el anterior se borra al
    // mostrar el siguiente ("no apilar", para fondos transparentes).
    bool stack = false;
};
// Codifica los fotogramas (hasta 255 colores y uno transparente por paleta).
QByteArray encode(const QList<Frame>& frames, const Options& options, QString* error);

// Efectos de la configuración avanzada (como los de ezgif).
struct Effects {
    // Fundido: entre cada fotograma y el siguiente, 'fadeFrames' fotogramas intermedios de
    // 'fadeDelayMs' cada uno.
    bool crossfade = false;
    int fadeDelayMs = 60;
    int fadeFrames = 10;
    bool fadeLastToFirst = true;
    // El primer fotograma queda debajo de todos los demás; con 'firstOnlyAsBackground' no se
    // muestra solo (es únicamente el fondo).
    bool firstAsBackground = false;
    bool firstOnlyAsBackground = false;

    bool operator==(const Effects&) const = default;
};
QList<Frame> applyEffects(QList<Frame> frames, const Effects& effects);

// Duración escrita en el nombre de una capa ("(120ms)"); 'fallback' si no hay.
int delayFromName(const QString& name, int fallback);
// El nombre con la duración puesta (o cambiada).
QString nameWithDelay(const QString& name, int delayMs);
// Duración mínima que respetan los navegadores (20 ms).
constexpr int kMinDelayMs = 20;

} // namespace Gif

// Iconos de Windows con varios tamaños (Qt solo escribe uno).
namespace Ico {

// Tamaños estándar (en píxeles) de un icono.
const QList<int>& standardSizes();
// Un icono con la imagen en cada tamaño de 'sizes' (cuadrada, centrada). El de 256 va
// comprimido en PNG y el resto como mapa de bits de 32 bits.
QByteArray encode(const QImage& image, const QList<int>& sizes, QString* error);
// La imagen más grande del archivo.
QImage readLargest(const QString& path, QString* error);

} // namespace Ico
