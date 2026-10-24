#pragma once

#include "app/widgets/EncodedPreview.h"
#include "core/ImageIO.h"

#include <QDialog>
#include <QImage>

class BarSlider;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

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

        bool operator==(const FrameEntry&) const = default;
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
    // Pone en los controles las opciones 'o' (Predeterminado).
    void loadOptions(const ImageIO::SaveOptions& o);
    void updateControls();
    void showAdvanced();
    // "Mostrar capas ocultas": añade o quita sus fotogramas de la lista.
    void setShowHidden(bool on);
    void updateHiddenButton();
    // Los fotogramas que quedan con 'a' (sin omitidos, con copias y duraciones).
    QList<Gif::Frame> framesOf(const Advanced& a) const;
    // 'o' con las opciones del GIF de 'a' (paleta, apilado, efectos).
    static ImageIO::SaveOptions withAdvanced(ImageIO::SaveOptions o, const Advanced& a);
    // Lo que codifica la vista previa con esas opciones y fotogramas (vacíos: la imagen acoplada).
    EncodedPreview::Encoder encoder(const ImageIO::SaveOptions& options, const QList<Gif::Frame>& frames) const;
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
    QPushButton* m_hiddenButton = nullptr;
    bool m_showHidden = false;
    // Se editaron los fotogramas en la configuración avanzada con las capas ocultas a la vista:
    // se quedan en la lista hasta cerrar este diálogo.
    bool m_hiddenLocked = false;
    QLabel* m_size;
    EncodedPreview* m_previewPane;
};
