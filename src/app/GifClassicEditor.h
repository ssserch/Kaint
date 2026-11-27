#pragma once

#include <QList>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>

class Document;
class EncodedPreview;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QToolButton;
struct Layer;
struct LayerProps;
using LayerPtr = std::shared_ptr<Layer>;

// Archivo > Editor de GIF > Clásico: la configuración avanzada del GIF a pantalla completa (como el
// estudio) y sobre la propia imagen: los fotogramas uno por uno en tarjetas (duración, omitir,
// copiar, moverlos y quitarlos), rangos, el orden y la velocidad, con la vista previa del GIF. Cada
// fotograma es una capa (con su duración en el nombre) y cada cambio es un paso del historial. Con
// «+» se añaden fotogramas. Con el mismo negro AMOLED que el estudio.
class GifClassicEditor : public QWidget {
    Q_OBJECT
public:
    explicit GifClassicEditor(QWidget* parent = nullptr);
    ~GifClassicEditor() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }

signals:
    // Como los del estudio: 'format' es "gif", "apng", "webp", "mp4" o "webm".
    void exportRequested(const QString& format);
    void imageEditorRequested();
    void studioRequested();
    void editFrameRequested(int layerIndex);

protected:
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    // Una tarjeta por fotograma (capa).
    struct Card {
        QFrame* frame = nullptr;
        QLabel* number = nullptr;
        QToolButton* eye = nullptr;
        QLabel* thumb = nullptr;
        QSpinBox* delay = nullptr;
        QPushButton* skip = nullptr;
        QToolButton* left = nullptr;
        QToolButton* right = nullptr;
        QToolButton* remove = nullptr;
        const Layer* layer = nullptr;
        qint64 imageKey = 0;
    };

    void buildUi();
    void applyLook();
    // Las tarjetas: se rehacen si cambian los fotogramas (número u orden); si no, se ponen al día.
    void refresh();
    void rebuildCards();
    void updateCard(Card& card, int index);
    void updatePreview();
    void updateTitle();

    // Cambia las capas (fotogramas) en un paso del historial, como el estudio.
    void apply(const QList<LayerPtr>& layers, const QList<LayerProps>& props, int active, const QString& text, const QString& icon);
    QList<LayerProps> currentProps() const;
    int delayOf(int index) const;
    void setDelay(int index, int delayMs);
    // La duración de todos ('fn' da la nueva a partir de la de cada uno).
    void setAllDelays(const std::function<int(int)>& fn, const QString& text);
    void setShown(const QList<int>& frames, bool shown, const QString& text);
    void duplicate(int index);
    void remove(int index);
    void move(int from, int to);
    void reverse();
    void boomerang();
    // «+»: fotogramas de archivos, uno vacío o una copia del último, al final.
    void addFromFiles();
    void addBlank();
    void append(const QList<QImage>& images, const QList<int>& delays, const QString& text);

    QPointer<Document> m_doc;
    QLabel* m_title = nullptr;
    QScrollArea* m_framesScroll = nullptr;
    QWidget* m_framesBox = nullptr;
    QHBoxLayout* m_framesRow = nullptr;
    QList<Card> m_cards;
    QToolButton* m_add = nullptr;
    QSpinBox* m_from = nullptr;
    QSpinBox* m_to = nullptr;
    QSpinBox* m_every = nullptr;
    QSpinBox* m_delayAll = nullptr;
    QComboBox* m_fps = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLineEdit* m_plays = nullptr;
    EncodedPreview* m_preview = nullptr;
    QLabel* m_status = nullptr;
    QTimer m_refreshTimer;
    QTimer m_previewTimer;
};
