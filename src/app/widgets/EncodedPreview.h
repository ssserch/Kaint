#pragma once

#include "core/Gif.h"

#include <QTimer>
#include <QWidget>

#include <functional>
#include <thread>

class QComboBox;
class QLabel;
class QToolButton;

// Vista previa de lo que se va a guardar: se codifica en segundo plano y se muestra lo que sale
// al volver a leerlo (en el GIF, todos sus fotogramas, con un botón para reproducirlo). La usan
// "Configuración de guardado" y la configuración avanzada del GIF.
class EncodedPreview : public QWidget {
    Q_OBJECT
public:
    // Se ejecuta en otro hilo: solo debe usar copias de lo que necesite.
    using Encoder = std::function<QByteArray(QString* error)>;

    explicit EncodedPreview(const QString& path, QWidget* parent = nullptr);
    ~EncodedPreview() override;

    // Vuelve a codificar (con un pequeño retraso, por si llegan más cambios seguidos). 'loops'
    // son las repeticiones del GIF (0 sin fin, -1 una vez), para reproducirlo igual.
    void request(Encoder encoder, int loops = 0);
    bool isPlaying() const { return m_playTimer.isActive(); }
    void setPlaying(bool playing);
    int frameCount() const { return int(m_frames.size()); }

signals:
    // "Tamaño del archivo: …", "calculando…" o el error al codificar.
    void statusChanged(const QString& text);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void start();
    void onEncoded(const QList<Gif::Frame>& frames, const QList<QImage>& fits, qint64 bytes, const QString& error,
                   int generation);
    void showFrame(int index);
    void nextFrame();

    QString m_path;
    QLabel* m_image;
    QComboBox* m_zoom;
    QToolButton* m_play;
    QLabel* m_frameLabel;
    QTimer m_debounce;
    QTimer m_playTimer;
    std::thread m_thread;
    Encoder m_encoder;
    int m_loops = 0;
    int m_generation = 0;
    bool m_running = false;
    bool m_pending = false;
    QList<Gif::Frame> m_frames;
    // Cada fotograma ya reducido a la vista previa («Ajustar»), hecho en segundo plano: reducir una
    // imagen grande en el hilo de la interfaz trababa la ventana al abrirse.
    QList<QImage> m_fits;
    bool m_shown = false;
    int m_index = 0;
    int m_playsLeft = 0;
};
