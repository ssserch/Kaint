#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

// Los fotogramas de un vídeo para el fondo del lienzo, en bucle y sin sonido: con Media Foundation
// en Windows (sus códecs: MP4/H.264, MOV, WMV, AVI, MKV y, con sus extensiones, WebM y HEVC) y con
// Qt Multimedia en las demás plataformas, si Kaint se compiló con él.
class VideoFrames : public QObject {
    Q_OBJECT
public:
    // 'maxSide': los fotogramas más grandes se reducen a ese lado (no hace falta más para el fondo).
    VideoFrames(const QString& path, int maxSide, QObject* parent = nullptr);
    ~VideoFrames() override;

    // Si esta compilación de Kaint puede reproducir vídeos.
    static bool supported();
    // Si el archivo es un vídeo (por su extensión).
    static bool isVideo(const QString& path);
    // Extensiones de vídeo para el diálogo de abrir ("*.mp4 *.webm...").
    static QString filePatterns();

    // Leer un vídeo entero de una vez (para abrirlo como animación) sin FFmpeg: solo en Windows,
    // con Media Foundation. Como mucho 'maxFps' fotogramas por segundo y 'maxFrames' en total (más
    // largo, menos por segundo), todos con la misma duración (1 / '*fps' segundos), opacos y
    // derechos (los de móvil en vertical se giran). Mientras lee, llama a 'progress' (hechos y
    // total, 0 si no se sabe) en este hilo; si devuelve false, se cancela y 'error' queda vacío.
    static bool canDecode();
    static bool decode(const QString& path, double maxFps, int maxFrames, QList<QImage>* frames, double* fps,
                       const std::function<bool(int, int)>& progress, QString* error);

    void setPaused(bool paused);
    QString error() const;

signals:
    // Un fotograma nuevo (en el hilo de la interfaz).
    void frame(const QImage& image);
    // No se pudo abrir o leer el vídeo.
    void failed(const QString& error);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
