#pragma once

#include "core/Motion.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

// Pistas de audio del editor de GIF (van en los vídeos exportados, MP4 y WebM; el GIF no tiene
// sonido). Cada clip es un archivo de audio guardado dentro de la imagen (va dentro del .ora)
// colocado en la línea de tiempo, con su volumen (y puntos clave de volumen) y fundidos.
namespace Audio {

// La envolvente para dibujar la onda: un valor de 0 a 255 cada kPeakMs del archivo.
constexpr int kPeakMs = 20;

struct Clip {
    QString name;
    QByteArray data;      // el archivo tal cual
    QString suffix;       // mp3, wav, ogg...
    int row = 0;          // pista de audio (0, la de arriba)
    int start = 0;        // ms en la línea de tiempo
    int offset = 0;       // ms recortados al principio del archivo
    int duration = 0;     // ms que suena
    int length = 0;       // ms del archivo entero
    // Volumen en % (100 tal cual, hasta 200): 'base' sin puntos clave; con ellos, cambia con el
    // tiempo (ms desde el principio del clip), como los puntos clave de los fotogramas.
    Motion::Track volume{100.0, {}};
    int fadeIn = 0;       // ms
    int fadeOut = 0;      // ms
    bool muted = false;
    QByteArray peaks;

    // El volumen (%) en 't' (ms desde el principio del clip), con los fundidos.
    double volumeAt(int t) const;
    int end() const { return start + duration; }
    // Sin los datos del archivo (van aparte).
    QJsonObject toJson() const;
    static Clip fromJson(const QJsonObject& o);
    bool operator==(const Clip&) const = default;
};

// Los archivos que se tratan como audio (por la extensión).
bool isAudioFile(const QString& path);
QStringList fileExtensions();
// Duración y envolvente: los WAV, directamente; lo demás, con FFmpeg (su ruta en 'ffmpeg', vacía si
// no está).
bool analyze(const QByteArray& data, const QString& suffix, const QString& ffmpeg, int* lengthMs, QByteArray* peaks,
             QString* error);
// La expresión del volumen de FFmpeg (filtro volume, con 't' en segundos desde el principio del
// clip): los puntos clave, en línea recta de uno a otro.
QString ffmpegVolumeExpression(const Clip& clip);

} // namespace Audio
