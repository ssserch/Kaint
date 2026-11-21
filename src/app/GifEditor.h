#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QListWidget>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "core/Motion.h"

#include <memory>

class Document;
class QCheckBox;
class QComboBox;
class QLabel;
class QScrollArea;
class QSlider;
class QSpinBox;
class QToolButton;
struct Layer;
struct LayerProps;
using LayerPtr = std::shared_ptr<Layer>;

// Archivo > Editor de GIF: la imagen como una animación, con una interfaz de editor de vídeo
// sencillo (como Filmora): la biblioteca de medios (las imágenes abiertas en Kaint y las que se
// importan o se arrastran), la vista previa con su reproductor y la línea de tiempo con un clip
// por fotograma. Es otra vista de la misma imagen: cada fotograma es una capa (la de abajo, el
// primero) con su duración en el nombre («Fotograma 1 (100ms)»), como al abrir y guardar un GIF;
// cada cambio es un paso del historial. Se pasa de una vista a otra en cada imagen.
namespace GifEdit {

// Un medio de la biblioteca: una imagen abierta (se lee al usarla) o un archivo importado (sus
// fotogramas, uno si es una imagen fija).
struct Media {
    QString name;
    QPointer<Document> document;
    QList<QImage> frames;
    QList<int> delays;
    QImage thumbnail;
};

// Los fotogramas de una imagen abierta: sus capas, si es una animación (alguna lleva la duración
// en el nombre); si no, la imagen acoplada.
void framesOf(const Document* doc, QList<QImage>* frames, QList<int>* delays);
// Un fotograma de otro tamaño, ajustado al lienzo (sin deformarlo, centrado y con transparente
// alrededor).
QImage fitted(const QImage& image, QSize canvas);
int delayOf(const Layer* layer);

class Preview;
class Timeline;
class Inspector;
class SizeMeter;

class MediaList : public QListWidget {
    Q_OBJECT
public:
    explicit MediaList(QWidget* parent = nullptr);
    static const char* mimeType();

signals:
    void filesDropped(const QStringList& paths);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

} // namespace GifEdit

class GifEditor : public QWidget {
    Q_OBJECT
public:
    explicit GifEditor(QWidget* parent = nullptr);
    ~GifEditor() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    // Las imágenes abiertas en Kaint, para la biblioteca.
    void setOpenDocuments(const QList<Document*>& docs);
    void stop();

    // Lo que hacen los botones (también desde la línea de tiempo).
    int frameCount() const;
    int selectedFrame() const;
    void selectFrame(int index);
    int frameDelay(int index) const;
    bool frameHidden(int index) const;
    // Inserta en 'at' (-1: al final) los fotogramas de un medio o de unos archivos.
    void insertMedia(int mediaIndex, int at);
    void insertFiles(const QStringList& paths, int at);
    void moveFrame(int from, int to);
    void setFrameDelay(int index, int delayMs);
    void removeFrame(int index);
    void duplicateFrame(int index);
    // Selección de varios fotogramas (Ctrl+clic, Mayús+clic): el elegido (la capa activa) y los demás.
    QList<int> selectedFrames() const;
    bool isFrameSelected(int index) const;
    void toggleFrameSelection(int index);
    void selectFrameRange(int index);
    void selectAllFrames();
    void removeFrames(const QList<int>& indexes);
    void duplicateFrames(const QList<int>& indexes);
    // Portapapeles: copiar, cortar y pegar fotogramas (con su duración); pegar una imagen de otro
    // programa la añade como fotograma.
    void copyFrames();
    void cutFrames();
    void pasteFrames();
    static const char* clipboardMimeType();
    // El fotograma que se ve: el elegido o, mientras se reproduce, el del cabezal.
    int shownFrame() const;

    // Animación por puntos clave del fotograma (GifMotion): se guarda en sus propiedades, así que
    // cada cambio es un paso del historial.
    Motion::Clip motion(int index) const;
    void setMotion(int index, const Motion::Clip& clip, const QString& text);
    // Tiempo (ms) del cabezal dentro del fotograma elegido.
    int localTime() const;
    void seekLocal(int ms);
    // El punto clave elegido (su tiempo dentro del fotograma; -1 ninguno): lleva el cabezal allí.
    int selectedKeyTime() const { return m_selectedKey; }
    void selectKeyTime(int ms);
    // Mueve los puntos de un tiempo a otro (si pasa del final, el fotograma dura más).
    void moveKeyTime(int from, int to);
    void removeKeyTime(int ms);
    void showKeyMenu(int ms, const QPoint& globalPos);
    // Sustituye el fotograma por los que salen de su animación.
    void bakeMotion(int index);
    // Arrastrar la imagen en la vista previa: la mueve (Posición X e Y) en el cabezal.
    void previewDragged(QPointF delta, bool finished);
    void toggleHidden(int index);
    void showFrameMenu(int index, const QPoint& globalPos);
    void editDelay(int index);
    // Tiempo (ms) donde empieza cada fotograma; el último valor es la duración total.
    QList<int> frameStarts() const;
    int playTime() const { return m_time; }
    void seek(int ms);

signals:
    // 'format': "gif" (la configuración de siempre), "apng", "webp", "mp4" o "webm".
    void exportRequested(const QString& format);
    void imageEditorRequested();
    void editFrameRequested(int layerIndex);
    // Deshacer y rehacer como los de la ventana (que avisan a la herramienta antes).
    void undoRequested();
    void redoRequested();

protected:
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void buildUi();
    void rebuildMedia();
    void updateState();
    void updateTimeLabel();
    // Cambia la lista de capas (fotogramas) en un paso del historial.
    void applyFrames(const QList<LayerPtr>& layers, const QList<LayerProps>& props, int active, const QString& text,
                     const QString& icon);
    void insertFrames(int at, const QList<QImage>& images, const QList<int>& delays, const QString& text);
    void importFiles(const QStringList& paths);
    bool loadFile(const QString& path, GifEdit::Media* media, QString* error) const;
    void reverseFrames();
    void boomerang();
    void setAllDelays(int delayMs);
    void togglePlay();
    void step(int delta);
    void tick();
    int frameAt(int ms) const;
    void invalidateStarts();
    QImage frameImage(int index) const;
    void showPlayingFrame(int index);
    // El peso aproximado del GIF (unos fotogramas de muestra codificados) para la barrita.
    void estimateSize();

    QPointer<Document> m_doc;
    QList<QPointer<Document>> m_openDocs;
    QList<GifEdit::Media> m_imported;
    QList<GifEdit::Media> m_media;

    QLabel* m_title = nullptr;
    GifEdit::MediaList* m_mediaList = nullptr;
    GifEdit::Preview* m_preview = nullptr;
    GifEdit::Inspector* m_inspector = nullptr;
    GifEdit::SizeMeter* m_sizeMeter = nullptr;
    QTimer m_sizeTimer;
    QString m_sizeKey;
    GifEdit::Timeline* m_timeline = nullptr;
    QScrollArea* m_timelineScroll = nullptr;
    QToolButton* m_play = nullptr;
    QLabel* m_timeLabel = nullptr;
    QCheckBox* m_loop = nullptr;
    QComboBox* m_speed = nullptr;
    QSpinBox* m_delay = nullptr;
    QSlider* m_zoom = nullptr;
    QList<QWidget*> m_frameButtons;

    QTimer m_ticker;
    QElapsedTimer m_clock;
    bool m_playing = false;
    int m_time = 0;
    qint64 m_lastTick = 0;
    // Mientras se reproduce, el fotograma del cabezal (la capa activa no cambia en cada uno).
    int m_playFrame = -1;
    // Los otros fotogramas elegidos (además de la capa activa).
    QList<int> m_extraSelection;
    mutable QList<int> m_starts;
    mutable bool m_startsValid = false;
    mutable QHash<qint64, QImage> m_opacityCache;
    int m_selectedKey = -1;
    QPointF m_dragOffset;
    // La última imagen animada que se calculó (fotograma, tiempo, animación) para no repetirla.
    mutable QString m_motionKey;
    mutable QImage m_motionImage;
    void updateMotionPath();
};
