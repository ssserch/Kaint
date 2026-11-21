#pragma once

#include "core/Gif.h"

#include <QColor>
#include <QList>
#include <QString>

class Document;
class QWidget;

// Exportar desde el editor de GIF a otros formatos (además del GIF, que sigue siendo el de
// siempre): PNG animado y WebP animado (con los códecs de Kaint) y vídeo MP4 o WebM (con FFmpeg,
// si está instalado). Los fotogramas son los del GIF: las capas visibles con su duración y la
// animación de sus puntos clave.
namespace GifExport {

enum class Format { Apng, WebP, Mp4, WebM };

struct VideoOptions {
    // Cuántas veces seguidas va la animación (un vídeo no se repite solo).
    int repeat = 1;
    // Aumento sin suavizar (pixel art): 1, 2, 3 o 4.
    int scale = 1;
    // 0 alta, 1 media, 2 baja.
    int quality = 0;
    // Lo transparente, sobre este color (el MP4 no tiene transparencia; el WebM sí la conserva).
    QColor background = Qt::white;
};

// El ejecutable de FFmpeg (junto a Kaint o en el PATH), o vacío.
QString ffmpegPath();
QString extension(Format format);
QString filter(Format format);

// Codifica a 'path' (sin preguntar nada): para las pruebas y para exportAnimation.
bool encode(const QList<Gif::Frame>& frames, Format format, const QString& path, int loops, int webpQuality,
            const VideoOptions& video, QWidget* progressParent, QString* error);

// Pregunta las opciones y dónde guardar, y exporta. Devuelve la ruta guardada (vacía si se canceló o falló).
QString exportAnimation(QWidget* parent, Document* doc, Format format, const QString& directory);

} // namespace GifExport
