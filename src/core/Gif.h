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
};

struct Animation {
    QList<Frame> frames;
    int loops = 0;
};

// Lee todos los fotogramas ya compuestos (con su duración).
Animation read(const QString& path, QString* error);

struct Options {
    int loops = 0;
    int dither = 7;
    int alphaThreshold = 128;
};
// Codifica los fotogramas con una paleta propia por fotograma (hasta 255 colores y uno
// transparente).
QByteArray encode(const QList<Frame>& frames, const Options& options, QString* error);

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
