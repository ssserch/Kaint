#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QListWidget>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "core/Audio.h"
#include "core/Motion.h"

#include <memory>

class Document;
class QCheckBox;
class QComboBox;
class QLabel;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTabWidget;
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
class AudioInspector;
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
    // Inserta en 'at' (índice de capa; -1: al final) los fotogramas de un medio o de unos archivos,
    // en la pista 'track' (-1: la elegida).
    void insertMedia(int mediaIndex, int at, int track = -1);
    void insertFiles(const QStringList& paths, int at, int track = -1);
    // Mueve el fotograma 'from' a 'to' (índices de capa) y, si se da, a otra pista.
    void moveFrame(int from, int to, int track = -1);
    // Lleva el fotograma a la pista 'track' empezando en 'startMs' (con espacio vacío delante si
    // hace falta, como en Filmora). Los demás no se mueven salvo si se solaparían: entonces se corren.
    void placeFrame(int from, int track, int startMs);

    // Pistas (las capas de la línea de tiempo, como en Filmora): cada fotograma va en una
    // (LayerProps::track) y cada pista es una sucesión de fotogramas; las de número más alto se ven
    // encima. La elegida recibe lo que se añade.
    int trackCount() const;
    int trackOf(int index) const;
    QList<int> framesOfTrack(int track) const;
    int activeTrack() const;
    void setActiveTrack(int track);
    void addTrack();
    void removeTrack(int track);
    bool trackHidden(int track) const;
    void toggleTrackHidden(int track);
    // Inicio y final (ms) de un fotograma dentro de su pista; la duración total (la pista más larga).
    int frameStart(int index) const;
    int frameEnd(int index) const;
    int totalTime() const;
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
    // Mientras se arrastra un punto en la gráfica: la animación como quedaría (motion() la devuelve y
    // todo se dibuja con ella), sin pasar todavía al historial.
    void setMotionPreview(int index, const Motion::Clip& clip);
    void clearMotionPreview();
    // Tiempo (ms) del cabezal dentro del fotograma elegido.
    int localTime() const;
    void seekLocal(int ms);
    // El punto clave elegido (su tiempo dentro del fotograma; -1 ninguno): lleva el cabezal allí.
    int selectedKeyTime() const { return m_selectedKey; }
    void selectKeyTime(int ms);
    // Elige el fotograma (si es otro) y su punto clave en 'ms'.
    void selectKey(int index, int ms);
    // Punto clave global (como el de Filmora): todos los valores (posición, escala, rotación y
    // opacidad) en el cabezal; si ya hay uno ahí, lo quita.
    void toggleGlobalKey();
    bool hasKeyAtPlayhead() const;
    // La vista del historial del editor de GIF (solo sus pasos), en una pestaña junto a los medios.
    void setHistoryPanel(QWidget* panel);
    // El brillo del damero de lo transparente (el de Configuración, como el del lienzo).
    void setCheckerBrightness(int brightness);
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
    int playTime() const { return m_time; }
    void seek(int ms);

    // Pistas de audio (para los vídeos exportados; el GIF no tiene sonido): debajo de las de los
    // fotogramas. Se ven las usadas y una vacía debajo, donde soltar más.
    QList<Audio::Clip> audioClips() const;
    int audioRows() const;
    // El final de lo que hay en la línea de tiempo (fotogramas o audio).
    int timelineEnd() const;
    // Cambia los clips de audio en un paso del historial del editor de GIF.
    void setAudioClips(const QList<Audio::Clip>& clips, const QString& text);
    // Añade archivos de audio en 'startMs' (-1: el cabezal) y en la pista 'row' (-1: la primera libre).
    void addAudioFiles(const QStringList& paths, int startMs = -1, int row = -1);
    void chooseAudioFiles();
    void removeAudio(int index);
    int selectedAudio() const { return m_selectedAudio; }
    void selectAudio(int index);
    // Pone o quita el punto de volumen del clip en el cabezal.
    void toggleAudioKey(int index);
    void showAudioMenu(int index, const QPoint& globalPos);

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
    void insertFrames(int at, const QList<QImage>& images, const QList<int>& delays, const QString& text, int track = -1);
    void importFiles(const QStringList& paths);
    bool loadFile(const QString& path, GifEdit::Media* media, QString* error) const;
    void reverseFrames();
    void boomerang();
    void setAllDelays(int delayMs);
    void togglePlay();
    void step(int delta);
    void tick();
    // El fotograma de la pista elegida en 'ms' (pasado su final, el último; -1 si está vacía) y el de
    // la pista 'track' (-1 fuera de sus fotogramas).
    int frameAt(int ms) const;
    int frameAtTrack(int track, int ms) const;
    void computeTimes() const;
    void invalidateStarts();
    QImage frameImage(int index) const;
    QImage frameImageAt(int index, int time) const;
    // Lo que se ve en el cabezal: el fotograma o, con varias pistas, todas una encima de otra.
    QImage shownImage() const;
    void showPlayingFrame(int index);
    // El peso aproximado del GIF (unos fotogramas de muestra codificados) para la barrita.
    void estimateSize();
    // Lo que suena mientras se reproduce: cada clip de audio en su sitio, con su volumen.
    void syncAudio();
    // Negro AMOLED con el acento verde azulado, como los editores de vídeo.
    void applyLook();
    void applyPalettes();

    QPointer<Document> m_doc;
    QList<QPointer<Document>> m_openDocs;
    QList<GifEdit::Media> m_imported;
    QList<GifEdit::Media> m_media;

    QLabel* m_title = nullptr;
    QTabWidget* m_leftTabs = nullptr;
    GifEdit::MediaList* m_mediaList = nullptr;
    GifEdit::Preview* m_preview = nullptr;
    GifEdit::Inspector* m_inspector = nullptr;
    GifEdit::AudioInspector* m_audioInspector = nullptr;
    class QStackedWidget* m_rightStack = nullptr;
    int m_selectedAudio = -1;
    struct AudioVoices;
    std::unique_ptr<AudioVoices> m_voices;
    bool m_introShown = false;
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
    mutable QList<int> m_ends;
    mutable int m_total = 0;
    mutable bool m_startsValid = false;
    int m_activeTrack = 0;
    int m_previewIndex = -1;
    Motion::Clip m_previewClip;
    mutable QHash<qint64, QImage> m_opacityCache;
    int m_selectedKey = -1;
    QPointF m_dragOffset;
    // La última imagen animada que se calculó (fotograma, tiempo, animación) para no repetirla.
    mutable QString m_motionKey;
    mutable QImage m_motionImage;
    void updateMotionPath();
};
