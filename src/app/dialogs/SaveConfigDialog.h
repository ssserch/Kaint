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
class QSpinBox;
class QLabel;
class QScrollArea;

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

    // Hay algo que configurar para este formato.
    static bool hasOptions(const QString& path);

private:
    void schedule();
    void start();
    void onEncoded(const QImage& preview, qint64 bytes, const QString& error, int generation);
    void updateControls();

    QImage m_image;
    QString m_path;
    BarSlider* m_quality = nullptr;
    QComboBox* m_depth = nullptr;
    BarSlider* m_dither = nullptr;
    BarSlider* m_threshold = nullptr;
    QCheckBox* m_animation = nullptr;
    QSpinBox* m_delay = nullptr;
    QCheckBox* m_loopForever = nullptr;
    QSpinBox* m_loops = nullptr;
    QList<QCheckBox*> m_icoSizes;
    QList<Gif::Frame> m_frames;
    QLabel* m_size;
    QLabel* m_preview;
    QComboBox* m_zoom;
    QTimer m_debounce;
    std::thread m_thread;
    int m_generation = 0;
    bool m_running = false;
    bool m_pending = false;
    QImage m_lastPreview;
};
