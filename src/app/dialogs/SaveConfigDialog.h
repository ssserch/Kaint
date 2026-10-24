#pragma once

#include "core/ImageIO.h"

#include <QDialog>
#include <QImage>
#include <QTimer>

#include <atomic>
#include <memory>
#include <thread>

class BarSlider;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QToolButton;

// "Configuración de guardado" de Paint.NET: opciones del formato (calidad, profundidad de
// bits, tramado, umbral de transparencia) con una vista previa del resultado y el tamaño que
// ocupará el archivo.
class SaveConfigDialog : public QDialog {
    Q_OBJECT
public:
    SaveConfigDialog(const QImage& flattened, const QString& path, const ImageIO::SaveOptions& initial,
                     QWidget* parent = nullptr, const QList<Gif::Frame>& frames = {});
    ~SaveConfigDialog() override;
    ImageIO::SaveOptions options() const;
    // Fotogramas del GIF animado tal como quedaron en la configuración avanzada (sin los omitidos,
    // con las copias y sus duraciones); vacío si no se guarda como animación.
    QList<Gif::Frame> animationFrames() const;

    // Hay algo que configurar para este formato.
    static bool hasOptions(const QString& path);

private:
    // Un fotograma de la configuración avanzada: cuál de los originales, su duración (0: la
    // predeterminada) y si se omite.
    struct FrameEntry {
        int source = 0;
        int delayMs = 0;
        bool skip = false;
    };
    // Lo que se edita en la configuración avanzada.
    struct Advanced {
        QList<FrameEntry> entries;
        int quantization = 1;
        bool globalPalette = false;
        bool stack = false;
        Gif::Effects effects;
    };

    void schedule();
    void start();
    void onEncoded(const QList<Gif::Frame>& preview, qint64 bytes, const QString& error, int generation);
    void showFrame(int index);
    void setPlaying(bool playing);
    void nextFrame();
    void updateControls();
    void showAdvanced();
    Advanced defaultAdvanced() const;

    QImage m_image;
    QString m_path;
    BarSlider* m_quality = nullptr;
    QComboBox* m_depth = nullptr;
    BarSlider* m_dither = nullptr;
    BarSlider* m_threshold = nullptr;
    QComboBox* m_quantization = nullptr;
    QLabel* m_thresholdNote = nullptr;
    QCheckBox* m_interlaced = nullptr;
    QCheckBox* m_animation = nullptr;
    QSpinBox* m_delay = nullptr;
    QCheckBox* m_loopForever = nullptr;
    QSpinBox* m_loops = nullptr;
    QList<QCheckBox*> m_icoSizes;
    QList<Gif::Frame> m_frames;
    Advanced m_advanced;
    QPushButton* m_advancedButton = nullptr;
    QLabel* m_size;
    QLabel* m_preview;
    QComboBox* m_zoom;
    QToolButton* m_play = nullptr;
    QLabel* m_frameLabel = nullptr;
    QTimer m_debounce;
    QTimer m_playTimer;
    std::thread m_thread;
    int m_generation = 0;
    bool m_running = false;
    bool m_pending = false;
    QList<Gif::Frame> m_previewFrames;
    int m_previewIndex = 0;
    int m_playsLeft = 0;
};
