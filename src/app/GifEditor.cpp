#include "app/GifEditor.h"

#include "app/GifExport.h"
#include "app/GifMotion.h"

#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Gif.h"
#include "core/ImageIO.h"
#include "core/Layer.h"
#include "core/Raster.h"

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDataStream>
#include <QCheckBox>
#include <QComboBox>
#include <QDrag>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFrame>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QKeyEvent>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

#ifdef KAINT_HAVE_QTMULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QTemporaryDir>
#include <QUrl>
#endif

namespace GifEdit {

namespace {

constexpr int kDefaultDelay = 100;
// Las pistas de audio, debajo de las de los fotogramas (con una franja con su nombre entre unas y
// otras).
constexpr int kAudioHeight = 54;
constexpr int kAudioGap = 24;

// Los colores del editor (negro AMOLED, como los editores de vídeo): fondo, paneles, botones,
// líneas, texto, texto apagado y el acento verde azulado.
namespace Look {
const QColor black(0, 0, 0);
const QColor panel(0x0c, 0x0d, 0x0f);
const QColor raised(0x17, 0x18, 0x1c);
const QColor hover(0x22, 0x24, 0x29);
const QColor line(0x2a, 0x2c, 0x32);
const QColor text(0xec, 0xed, 0xef);
const QColor dim(0x8b, 0x8f, 0x97);
const QColor accent(0x1f, 0xd1, 0xb9);
const QColor accentDark(0x0d, 0x4a, 0x43);
} // namespace Look

// Un paso del audio (los clips de antes y de después).
class AudioCommand : public QUndoCommand {
public:
    AudioCommand(Document* doc, const QList<Audio::Clip>& before, const QList<Audio::Clip>& after, const QString& text,
                 QUndoCommand* parent)
        : QUndoCommand(text, parent)
        , m_doc(doc)
        , m_before(before)
        , m_after(after)
    {
    }
    void redo() override
    {
        if (m_doc)
            m_doc->setAudioClips(m_after);
    }
    void undo() override
    {
        if (m_doc)
            m_doc->setAudioClips(m_before);
    }

private:
    QPointer<Document> m_doc;
    QList<Audio::Clip> m_before;
    QList<Audio::Clip> m_after;
};
constexpr int kRulerHeight = 26;
constexpr int kTrackTop = kRulerHeight + 6;
// Cada pista: sus clips (con los rombos de los puntos clave sobre la imagen) y aire entre pistas.
// (Se cambia arrastrando el borde de abajo de una pista en su cabecera o con su menú, y se recuerda.)
int kTrackHeight = 80;
constexpr int kMinTrackHeight = 44, kMaxTrackHeight = 160;
constexpr int kTrackGap = 8;
// La cabecera de las pistas (su nombre y sus botones), a la izquierda.
constexpr int kHeaderWidth = 132;
constexpr int kLeftMargin = kHeaderWidth + 12;
constexpr int kEdgeGrab = 6;
// Distancia (px) a la que el imán pega un clip a una unión.
constexpr int kSnap = 14;

QImage thumbnail(const QImage& image, QSize box)
{
    if (image.isNull())
        return {};
    return image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// El damero de lo transparente, con el brillo de Configuración (el mismo que el del lienzo del
// editor de imágenes: el gris claro y el oscuro, a tres cuartos).
int g_checkerBrightness = 191;
QPixmap checkerTile()
{
    static int made = -1;
    static QPixmap tile;
    if (made != g_checkerBrightness) {
        made = g_checkerBrightness;
        const int light = std::clamp(g_checkerBrightness, 0, 255), dark = int(std::lround(light * 0.75));
        tile = QPixmap::fromImage(Raster::checkerboard(8, QColor(light, light, light), QColor(dark, dark, dark)));
    }
    return tile;
}

void paintChecker(QPainter& p, const QRect& r)
{
    // Empieza en la esquina de la imagen, como en el lienzo.
    p.save();
    p.setBrushOrigin(r.topLeft());
    p.fillRect(r, QBrush(checkerTile()));
    p.restore();
}

QString timeText(int ms)
{
    const int minutes = ms / 60000;
    const int seconds = (ms / 1000) % 60;
    const int hundredths = (ms % 1000) / 10;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(hundredths, 2, 10, QLatin1Char('0'));
}

} // namespace

int delayOf(const Layer* layer)
{
    return layer ? std::max(Gif::kMinDelayMs, Gif::delayFromName(layer->props.name, kDefaultDelay)) : kDefaultDelay;
}

QImage fitted(const QImage& image, QSize canvas)
{
    if (image.isNull() || canvas.isEmpty())
        return {};
    const QImage source = image.convertToFormat(kLayerFormat);
    if (source.size() == canvas)
        return source;
    QImage out(canvas, kLayerFormat);
    out.fill(kTransparent);
    const QImage scaled = source.scaled(canvas, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage((canvas.width() - scaled.width()) / 2, (canvas.height() - scaled.height()) / 2, scaled);
    return out;
}

void framesOf(const Document* doc, QList<QImage>* frames, QList<int>* delays)
{
    frames->clear();
    delays->clear();
    if (!doc)
        return;
    static const QRegularExpression timed(QStringLiteral("\\(\\d+\\s*ms\\)"), QRegularExpression::CaseInsensitiveOption);
    bool animation = false;
    for (const LayerPtr& layer : doc->layers())
        animation = animation || timed.match(layer->props.name).hasMatch();
    if (animation && doc->layerCount() > 1) {
        for (const Gif::Frame& f : ImageIO::animationFrames(doc, kDefaultDelay)) {
            *frames << f.image;
            *delays << std::max(Gif::kMinDelayMs, f.delayMs);
        }
        return;
    }
    *frames << doc->flattened();
    *delays << kDefaultDelay;
}

// ---------------------------------------------------------------------------
// Biblioteca de medios
// ---------------------------------------------------------------------------

MediaList::MediaList(QWidget* parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setIconSize(QSize(84, 64));
    setGridSize(QSize(104, 96));
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setWordWrap(true);
    setUniformItemSizes(true);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

const char* MediaList::mimeType()
{
    return "application/x-kaint-gif-media";
}

QStringList MediaList::mimeTypes() const
{
    return {QString::fromLatin1(mimeType()), QStringLiteral("text/uri-list")};
}

QMimeData* MediaList::mimeData(const QList<QListWidgetItem*>& items) const
{
    auto* data = new QMimeData;
    QByteArray indexes;
    for (const QListWidgetItem* item : items)
        indexes += QByteArray::number(item->data(Qt::UserRole).toInt()) + ' ';
    data->setData(QString::fromLatin1(mimeType()), indexes.trimmed());
    return data;
}

void MediaList::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        return;
    }
    QListWidget::dragEnterEvent(e);
}

void MediaList::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasUrls()) {
        e->acceptProposedAction();
        return;
    }
    // Los medios propios no se reordenan aquí: se arrastran a la línea de tiempo.
    e->ignore();
}

void MediaList::dropEvent(QDropEvent* e)
{
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (!paths.isEmpty()) {
        e->acceptProposedAction();
        emit filesDropped(paths);
        return;
    }
    e->ignore();
}

// ---------------------------------------------------------------------------
// Vista previa
// ---------------------------------------------------------------------------

class Preview : public QWidget {
public:
    explicit Preview(GifEditor* editor, QWidget* parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        setMinimumSize(200, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(GifEditor::tr("Arrastra la imagen para moverla (con puntos clave de posición, se pone uno en el cabezal)"));
    }
    void setImage(const QImage& image)
    {
        m_image = image;
        update();
    }
    // El recorrido de la imagen (su centro, en píxeles de la imagen) y dónde están los puntos.
    void setPath(const QList<QPointF>& path, const QList<QPointF>& keys)
    {
        m_path = path;
        m_keys = keys;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Look::black);
        if (m_image.isNull()) {
            p.setPen(QColor(200, 200, 200));
            p.drawText(rect(), Qt::AlignCenter, GifEditor::tr("Sin fotogramas: arrastra imágenes o GIF a la línea de tiempo"));
            return;
        }
        const QRect target = targetRect();
        paintChecker(p, target);
        p.setRenderHint(QPainter::SmoothPixmapTransform, target.width() < m_image.width());
        p.drawImage(target, m_image);
        // El recorrido del movimiento, punteado, con los puntos clave (como «Path Curve» de Filmora).
        if (m_path.size() > 1) {
            const double k = double(target.width()) / std::max(1, m_image.width());
            auto map = [&](QPointF pt) { return QPointF(target.left() + pt.x() * k, target.top() + pt.y() * k); };
            p.setRenderHint(QPainter::Antialiasing);
            QPolygonF line;
            for (const QPointF& pt : std::as_const(m_path))
                line << map(pt);
            p.setPen(QPen(QColor(255, 255, 255, 200), 1.5, Qt::DashLine));
            p.drawPolyline(line);
            p.setPen(QPen(QColor(0, 0, 0, 120), 1.5, Qt::DotLine));
            p.drawPolyline(line.translated(1, 1));
            for (const QPointF& pt : std::as_const(m_keys))
                paintDiamond(p, map(pt), 5, false, true, palette());
        }
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && !m_image.isNull() && targetRect().contains(e->position().toPoint())) {
            m_dragging = true;
            m_pressPos = e->position();
            setCursor(Qt::ClosedHandCursor);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_dragging)
            m_editor->previewDragged(toImage(e->position() - m_pressPos), false);
        else
            setCursor(!m_image.isNull() && targetRect().contains(e->position().toPoint()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (!m_dragging)
            return;
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        m_editor->previewDragged(toImage(e->position() - m_pressPos), true);
    }

private:
    QRect targetRect() const
    {
        const QSize s = m_image.size().scaled(rect().size() - QSize(16, 16), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }
    QPointF toImage(QPointF widgetDelta) const
    {
        const QRect t = targetRect();
        const double k = t.width() > 0 ? double(m_image.width()) / t.width() : 1.0;
        return widgetDelta * k;
    }

    GifEditor* m_editor;
    QImage m_image;
    QList<QPointF> m_path;
    QList<QPointF> m_keys;
    bool m_dragging = false;
    QPointF m_pressPos;
};

// ---------------------------------------------------------------------------
// Línea de tiempo
// ---------------------------------------------------------------------------

class Timeline : public QWidget {
public:
    Timeline(GifEditor* editor, QWidget* parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        setAcceptDrops(true);
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        kTrackHeight = std::clamp(QSettings().value(QStringLiteral("gifEditor/trackHeight"), 80).toInt(), kMinTrackHeight, kMaxTrackHeight);
        setMinimumHeight(heightFor(1));
    }

    void setPixelsPerSecond(int pps)
    {
        m_pps = std::clamp(pps, 40, 3000);
        updateGeometry();
        adjustSize();
        update();
    }
    int pixelsPerSecond() const { return m_pps; }
    // Al cambiar el zum con la rueda (para la barra del zum).
    std::function<void(int)> onZoom;

    static int rowStep() { return kTrackHeight + kTrackGap; }
    int tracks() const { return std::max(1, m_editor->trackCount()); }
    static int heightFor(int tracks) { return kTrackTop + tracks * rowStep() + 4; }
    // Las pistas de número más alto, arriba (como en Filmora: se ven encima de las de abajo).
    int trackTop(int track) const { return kTrackTop + (tracks() - 1 - track) * rowStep(); }
    int trackAtY(int y) const
    {
        if (y < kTrackTop)
            return -1;
        const int row = (y - kTrackTop) / rowStep();
        return row >= tracks() ? -1 : tracks() - 1 - row;
    }

    QSize sizeHint() const override
    {
        const int w = kLeftMargin + int(std::ceil(m_editor->timelineEnd() * pxPerMs())) + 160;
        return {std::max(w, parentWidget() ? parentWidget()->width() : 400), heightFor(tracks()) + kAudioGap + audioRows() * audioStep()};
    }

    // --- Audio ---
    int audioRows() const { return std::max(1, m_editor->audioRows()); }
    static int audioStep() { return kAudioHeight + kTrackGap; }
    int audioTop(int row) const { return kTrackTop + tracks() * rowStep() + kAudioGap + row * audioStep(); }
    int audioRowAt(int y) const
    {
        const int top = audioTop(0) - kTrackGap / 2;
        if (y < top)
            return -1;
        const int row = (y - top) / audioStep();
        return row < audioRows() ? row : -1;
    }
    // El clip tal como se ve (mientras se mueve o se recorta, con lo nuevo).
    Audio::Clip shownAudio(int i) const
    {
        if (i == m_aEdit && m_aEditing)
            return m_aClip;
        return m_editor->audioClips().value(i);
    }
    QRect audioRect(int i) const
    {
        const Audio::Clip c = shownAudio(i);
        const int x0 = xForTime(c.start), x1 = xForTime(c.end());
        return QRect(x0, audioTop(c.row), std::max(6, x1 - x0) - 2, kAudioHeight);
    }
    int audioAt(QPoint pos) const
    {
        if (pos.x() < headerX() + kHeaderWidth || inRuler(pos))
            return -1;
        const int n = int(m_editor->audioClips().size());
        for (int i = n - 1; i >= 0; --i)
            if (audioRect(i).adjusted(0, 0, 2, 0).contains(pos))
                return i;
        return -1;
    }
    // El volumen (0 a 200 %) a lo alto del clip, debajo de su nombre.
    static double volumeForY(const QRect& r, int y) { return std::clamp((r.bottom() - 4 - y) * 200.0 / std::max(1, r.height() - 22), 0.0, 200.0); }
    static int yForVolume(const QRect& r, double v) { return r.bottom() - 4 - int(std::lround(v * (r.height() - 22) / 200.0)); }
    // El punto de volumen bajo 'pos' (su tiempo dentro del clip) y su clip, o -1.
    int audioKeyAt(QPoint pos, int* clip) const
    {
        if (inRuler(pos) || pos.x() < headerX() + kHeaderWidth)
            return -1;
        const QList<Audio::Clip> clips = m_editor->audioClips();
        for (int i = 0; i < clips.size(); ++i) {
            const QRect r = audioRect(i);
            if (!r.adjusted(-6, 0, 6, 0).contains(pos))
                continue;
            for (const Motion::Key& k : clips[i].volume.keys)
                if (std::abs(xForTime(clips[i].start + k.t) - pos.x()) <= 6 && std::abs(yForVolume(r, k.value) - pos.y()) <= 6) {
                    *clip = i;
                    return k.t;
                }
        }
        return -1;
    }
    QRect audioMuteRect(int row) const { return QRect(headerX() + kHeaderWidth - 62, audioTop(row) + (kAudioHeight - 26) / 2, 26, 26); }
    QRect audioTrashRect(int row) const { return audioMuteRect(row).translated(30, 0); }
    // Los de una pista de audio.
    QList<int> audioOfRow(int row) const
    {
        QList<int> out;
        const QList<Audio::Clip> clips = m_editor->audioClips();
        for (int i = 0; i < clips.size(); ++i)
            if (clips[i].row == row)
                out << i;
        return out;
    }

    void paintAudio(QPainter& p, const QRect& exposed)
    {
        const QPalette pal = palette();
        // La franja «Audio» entre las pistas de los fotogramas y las de audio.
        const int bandTop = audioTop(0) - kAudioGap;
        p.fillRect(QRect(0, bandTop, width(), kAudioGap - kTrackGap / 2), pal.color(QPalette::Window));
        p.setPen(pal.color(QPalette::Mid));
        p.drawLine(0, bandTop, width(), bandTop);
        for (int row = 0; row < audioRows(); ++row) {
            const QRect band(0, audioTop(row) - kTrackGap / 2, width(), audioStep());
            p.fillRect(band, row % 2 ? pal.color(QPalette::Base) : pal.color(QPalette::AlternateBase));
            p.setPen(pal.color(QPalette::Midlight));
            p.drawLine(0, band.bottom(), width(), band.bottom());
            if (audioOfRow(row).isEmpty()) {
                p.setPen(pal.color(QPalette::PlaceholderText));
                p.drawText(QRect(kLeftMargin, audioTop(row), std::max(300, width() - kLeftMargin - 20), kAudioHeight),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           GifEditor::tr("Arrastra aquí un audio (MP3, WAV, OGG...) para los vídeos exportados"));
            }
        }
        const QList<Audio::Clip> clips = m_editor->audioClips();
        for (int i = 0; i < clips.size(); ++i) {
            const QRect r = audioRect(i);
            if (r.intersects(exposed))
                paintAudioClip(p, i, r);
        }
    }

    void paintAudioClip(QPainter& p, int i, const QRect& r)
    {
        const Audio::Clip c = shownAudio(i);
        const bool selected = i == m_editor->selectedAudio();
        p.save();
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        const QColor base = c.muted ? Look::raised : Look::accentDark;
        p.fillPath(shape, base);
        p.setClipPath(shape);
        // La onda: la envolvente del archivo en la parte que suena, a la altura de su volumen.
        const QColor wave = c.muted ? Look::dim : Look::accent;
        const int waveTop = r.top() + 18, waveBottom = r.bottom() - 3;
        const int mid = (waveTop + waveBottom) / 2, half = std::max(2, (waveBottom - waveTop) / 2);
        p.setRenderHint(QPainter::Antialiasing, false);
        QColor waveFill = wave;
        waveFill.setAlpha(150);
        p.setPen(waveFill);
        for (int x = std::max(r.left() + 1, headerX() + kHeaderWidth); x < r.right(); ++x) {
            const int local = timeForX(x) - c.start;
            const int ms = c.offset + local;
            const int idx = ms / Audio::kPeakMs;
            if (idx < 0 || idx >= c.peaks.size())
                continue;
            const double gain = std::min(1.6, c.volumeAt(local) / 100.0);
            const int h = std::max(1, int(std::lround(quint8(c.peaks[idx]) / 255.0 * half * gain)));
            p.drawLine(x, mid - std::min(half, h), x, mid + std::min(half, h));
        }
        p.setRenderHint(QPainter::Antialiasing);
        // La línea del volumen (con los fundidos) y sus puntos clave.
        QPolygonF line;
        const int step = 3;
        for (int x = r.left(); x <= r.right(); x += step)
            line << QPointF(x, yForVolume(r, c.volumeAt(std::clamp(timeForX(x) - c.start, 0, c.duration))));
        p.setPen(QPen(QColor(255, 255, 255, 210), 1.4));
        p.drawPolyline(line);
        for (const Motion::Key& k : c.volume.keys) {
            int t = k.t;
            double v = k.value;
            const bool dragging = m_aKeyClip == i && k.t == m_aKeyFrom;
            if (dragging) {
                t = m_aKeyTo;
                v = m_aKeyValue;
            }
            paintDiamond(p, QPointF(xForTime(c.start + t), yForVolume(r, v)), 5, dragging || selected, true, palette());
        }
        // El nombre, arriba a la izquierda, con una nota musical.
        QFont f = font();
        f.setPointSizeF(std::max(6.5, f.pointSizeF() * 0.85));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Look::text);
        const QRect label(std::max(r.left() + 6, headerX() + kHeaderWidth + 4), r.top() + 2, r.width() - 12, 16);
        const QString name = QStringLiteral("♪ ") + c.name + (c.muted ? QStringLiteral(" · ") + GifEditor::tr("Silenciado") : QString());
        p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(name, Qt::ElideRight, label.width()));
        p.setClipping(false);
        p.setPen(QPen(selected ? Look::accent : base.lighter(150), selected ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
        // Las asas de los bordes (para recortar).
        if (r.width() > 20) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 120));
            p.drawRect(QRect(r.right() - 3, r.top() + r.height() / 2 - 9, 2, 18));
            p.drawRect(QRect(r.left() + 2, r.top() + r.height() / 2 - 9, 2, 18));
        }
        p.restore();
    }

    int xForTime(int ms) const { return kLeftMargin + int(std::round(ms * pxPerMs())); }
    int timeForX(int x) const { return std::max(0, int(std::round((x - kLeftMargin) / pxPerMs()))); }

    // Inicio y final del clip (mientras se estira uno, con su duración nueva y los de detrás
    // corridos en su pista).
    std::pair<int, int> span(int index) const
    {
        int start = m_editor->frameStart(index), end = m_editor->frameEnd(index);
        if (m_resizing >= 0 && m_editor->trackOf(index) == m_editor->trackOf(m_resizing)) {
            const int diff = m_resizeDelay - (m_editor->frameEnd(m_resizing) - m_editor->frameStart(m_resizing));
            if (index == m_resizing)
                end += diff;
            else if (start >= m_editor->frameEnd(m_resizing)) {
                start += diff;
                end += diff;
            }
        }
        return {start, end};
    }

    QRect clipRect(int index) const
    {
        const auto [start, end] = span(index);
        const int x0 = xForTime(start), x1 = xForTime(end);
        return QRect(x0, trackTop(m_editor->trackOf(index)), std::max(6, x1 - x0) - 2, kTrackHeight);
    }

    void refresh()
    {
        adjustSize();
        update();
    }

protected:
    double pxPerMs() const { return m_pps / 1000.0; }
    // Dónde empieza lo que se ve (la cabecera de las pistas se queda ahí al desplazarse).
    int headerX() const { return std::max(0, -x()); }

    int clipAt(QPoint pos) const
    {
        if (pos.x() < headerX() + kHeaderWidth || inRuler(pos))
            return -1;
        for (int i = 0; i < m_editor->frameCount(); ++i)
            if (clipRect(i).adjusted(0, 0, 2, 0).contains(pos))
                return i;
        return -1;
    }

    // Dónde quedaría (índice de capa) lo que se suelta en x en la pista: delante del clip cuya
    // mitad está más allá; si no, detrás del último de la pista.
    int insertionAt(int x, int track) const
    {
        const QList<int> frames = m_editor->framesOfTrack(track);
        for (int i : frames)
            if (x < (xForTime(m_editor->frameStart(i)) + xForTime(m_editor->frameEnd(i))) / 2)
                return i;
        return frames.isEmpty() ? m_editor->frameCount() : frames.last() + 1;
    }

    // Imán: la unión entre clips (o el final de la pista) más cerca de x en la pista; da dónde
    // quedaría (índice de capa) y su x.
    int nearestJunction(int track, int x, int* jx) const
    {
        const QList<int> frames = m_editor->framesOfTrack(track);
        int best = frames.isEmpty() ? m_editor->frameCount() : frames.last() + 1;
        *jx = frames.isEmpty() ? kLeftMargin : xForTime(m_editor->frameEnd(frames.last()));
        for (int i : frames) {
            const int cx = xForTime(m_editor->frameStart(i));
            if (std::abs(cx - x) < std::abs(*jx - x)) {
                *jx = cx;
                best = i;
            }
        }
        return best;
    }

    // La fila de los rombos dentro del clip, sobre la imagen.
    static int keyY(const QRect& clip) { return std::max(clip.top() + 9, clip.bottom() - 27); }

    // El punto clave bajo 'pos' (su tiempo dentro del fotograma) y su fotograma, o -1.
    int keyAt(QPoint pos, int* frame) const
    {
        if (inRuler(pos))
            return -1;
        for (int i = 0; i < m_editor->frameCount(); ++i) {
            const QRect r = clipRect(i);
            if (!r.adjusted(-6, 0, 6, 0).contains(pos) || std::abs(pos.y() - keyY(r)) > 8)
                continue;
            for (int t : m_editor->motion(i).keyTimes())
                if (std::abs(xForTime(m_editor->frameStart(i) + t) - pos.x()) <= 7) {
                    *frame = i;
                    return t;
                }
        }
        return -1;
    }

    // Los botones de la cabecera: añadir pista (en la fila de la regla) y, en cada pista, ocultarla y
    // eliminarla.
    QRect addTrackRect() const { return QRect(headerX() + 6, rulerY() + 2, kHeaderWidth - 12, kRulerHeight - 4); }
    // Arriba de lo que se ve (la regla se queda ahí al bajar entre las pistas).
    int rulerY() const { return std::max(0, -y()); }
    bool inRuler(QPoint pos) const { return pos.y() >= rulerY() && pos.y() < rulerY() + kRulerHeight; }
    // (Con las pistas bajas, a la derecha del nombre.)
    QRect eyeRect(int track) const
    {
        return kTrackHeight >= 64 ? QRect(headerX() + 8, trackTop(track) + kTrackHeight - 30, 26, 26)
                                  : QRect(headerX() + kHeaderWidth - 62, trackTop(track) + (kTrackHeight - 26) / 2, 26, 26);
    }
    QRect trashRect(int track) const { return eyeRect(track).translated(30, 0); }
    // El borde de abajo de una pista en la cabecera (arrastrándolo cambia la altura de todas), o -1.
    int heightEdgeAt(QPoint pos) const
    {
        if (pos.x() >= headerX() + kHeaderWidth || pos.x() < headerX())
            return -1;
        for (int k = 0; k < tracks(); ++k)
            if (std::abs(pos.y() - (trackTop(k) + kTrackHeight + kTrackGap / 2)) <= 4)
                return k;
        return -1;
    }
    void setTrackHeight(int h)
    {
        kTrackHeight = std::clamp(h, kMinTrackHeight, kMaxTrackHeight);
        m_thumbs.clear();
        updateGeometry();
        adjustSize();
        update();
    }

    void paintEvent(QPaintEvent* e) override
    {
        QPainter p(this);
        const QPalette pal = palette();
        p.fillRect(e->rect(), pal.color(QPalette::Base));
        const int hx = headerX();
        // Las filas de las pistas; la elegida, marcada.
        const int active = m_editor->activeTrack();
        for (int k = 0; k < tracks(); ++k) {
            const QRect row(0, trackTop(k) - kTrackGap / 2, width(), rowStep());
            QColor c = k == active ? pal.color(QPalette::Highlight) : pal.color(QPalette::AlternateBase);
            c.setAlpha(k == active ? 28 : 255);
            p.fillRect(row, k == active ? c : (k % 2 ? pal.color(QPalette::Base) : c));
            p.setPen(pal.color(QPalette::Midlight));
            p.drawLine(0, row.bottom(), width(), row.bottom());
        }
        QFont small = font();
        small.setPointSizeF(std::max(6.5, small.pointSizeF() * 0.85));
        p.setFont(small);
        // Clips
        const int selectedFrame = m_editor->selectedFrame();
        Document* doc = m_editor->document();
        for (int i = 0; doc && i < m_editor->frameCount(); ++i) {
            const QRect r = clipRect(i);
            if (!r.intersects(e->rect()))
                continue;
            const auto [start, end] = span(i);
            const bool hidden = m_editor->frameHidden(i);
            const bool selected = m_editor->isFrameSelected(i);
            QPainterPath path;
            path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            // Elegido: verde azulado oscuro con el borde del acento (un bloque del acento entero
            // deslumbraba sobre el negro).
            QColor fill = selected ? Look::accentDark : pal.color(QPalette::Button);
            if (selected && i != selectedFrame)
                fill = fill.lighter(115);
            if (hidden)
                fill.setAlpha(110);
            p.setRenderHint(QPainter::Antialiasing);
            p.fillPath(path, fill);
            p.setPen(QPen(selected ? Look::accent : pal.color(QPalette::Mid), i == selectedFrame ? 2 : 1));
            p.drawPath(path);
            p.setRenderHint(QPainter::Antialiasing, false);
            // La miniatura (la de la capa), dentro del clip.
            if (r.width() > 14) {
                const QImage& img = doc->layer(i)->image;
                const QSize box(r.width() - 8, r.height() - 24);
                const QImage t = cachedThumb(img, box);
                if (!t.isNull()) {
                    const QRect tr(r.left() + (r.width() - t.width()) / 2, r.top() + 4 + (box.height() - t.height()) / 2,
                                   t.width(), t.height());
                    paintChecker(p, tr);
                    if (hidden)
                        p.setOpacity(0.4);
                    p.drawImage(tr, t);
                    p.setOpacity(1.0);
                }
            }
            p.setPen(selected ? Look::text : pal.color(QPalette::ButtonText));
            const QRect label = r.adjusted(5, r.height() - 19, -5, -2);
            const QString text = r.width() > 70 ? QStringLiteral("%1 · %2 ms").arg(i + 1).arg(end - start)
                                 : r.width() > 26 ? QString::number(i + 1) : QString();
            p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, label.width()));
            // Los puntos clave, sobre la propia imagen del clip (como en Filmora), en una franja
            // oscura para que se vean sobre cualquier imagen.
            const QList<int> keys = m_editor->motion(i).keyTimes();
            if (!keys.isEmpty()) {
                const int y = keyY(r);
                p.save();
                p.setClipRect(r.adjusted(1, 1, -1, -1));
                p.fillRect(QRect(r.left() + 1, y - 8, r.width() - 2, 16), QColor(0, 0, 0, 90));
                p.setPen(QColor(255, 255, 255, 120));
                p.drawLine(r.left() + 2, y, r.right() - 2, y);
                for (int t : keys) {
                    int shown = t;
                    const bool dragging = m_keyDrag >= 0 && m_keyFrame == i && t == m_keyDrag;
                    if (dragging)
                        shown = m_keyDragTo;
                    const bool chosen = dragging || (i == selectedFrame && t == m_editor->selectedKeyTime());
                    paintDiamond(p, QPointF(xForTime(start + shown), y), 6, chosen, true, pal);
                }
                p.restore();
            }
            // El asa para estirar, a la derecha.
            if (r.width() > 16) {
                p.setPen(Qt::NoPen);
                p.setBrush(pal.color(QPalette::Mid));
                p.drawRect(QRect(r.right() - 3, r.top() + r.height() / 2 - 10, 2, 20));
                p.setBrush(Qt::NoBrush);
            }
        }
        // Una pista vacía: cómo llenarla.
        for (int k = 0; k < tracks(); ++k)
            if (m_editor->framesOfTrack(k).isEmpty()) {
                p.setPen(pal.color(QPalette::PlaceholderText));
                p.drawText(QRect(kLeftMargin, trackTop(k), std::max(300, width() - kLeftMargin - 20), kTrackHeight),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           GifEditor::tr("Arrastra aquí imágenes o GIF (de la biblioteca o de tus carpetas)"));
            }
        paintAudio(p, e->rect());
        // El clip que se arrastra, siguiendo al puntero (pegado a una unión al acercarse).
        if (m_reordering && m_pressIndex >= 0 && m_dropTrack >= 0 && doc && m_pressIndex < m_editor->frameCount()) {
            const QRect from = clipRect(m_pressIndex);
            const QRect ghost(m_ghostX, trackTop(m_dropTrack), from.width(), kTrackHeight);
            QColor fill = pal.color(QPalette::Highlight);
            fill.setAlpha(110);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(QRectF(ghost).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            p.fillPath(path, fill);
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.drawPath(path);
            p.setRenderHint(QPainter::Antialiasing, false);
            const QImage t = cachedThumb(doc->layer(m_pressIndex)->image, QSize(std::max(8, ghost.width() - 8), ghost.height() - 24));
            p.setOpacity(0.75);
            p.drawImage(ghost.left() + (ghost.width() - t.width()) / 2, ghost.top() + 4, t);
            p.setOpacity(1.0);
        }
        // Dónde caerá lo que se arrastra
        if (m_dropAt >= 0 && m_dropTrack >= 0) {
            const QList<int> frames = m_editor->framesOfTrack(m_dropTrack);
            const int x = frames.contains(m_dropAt) ? xForTime(m_editor->frameStart(m_dropAt)) - 1
                          : frames.isEmpty()        ? kLeftMargin
                                                    : xForTime(m_editor->frameEnd(frames.last()));
            p.fillRect(QRect(x - 1, trackTop(m_dropTrack) - 4, 3, kTrackHeight + 8), pal.color(QPalette::Highlight));
        }
        // Regla del tiempo: siempre a la vista arriba, también al bajar entre muchas pistas (encima de
        // los clips que pasan por debajo).
        const int ry = rulerY();
        const QRect ruler(0, ry, width(), kRulerHeight);
        p.fillRect(ruler, pal.color(QPalette::Window));
        p.setPen(pal.color(QPalette::Mid));
        p.drawLine(0, ry + kRulerHeight - 1, width(), ry + kRulerHeight - 1);
        // Una marca grande cada cierto tiempo, según el zum (al menos 70 px entre ellas).
        static const int steps[] = {50, 100, 200, 250, 500, 1000, 2000, 5000, 10000};
        int major = steps[std::size(steps) - 1];
        for (int s : steps)
            if (s * pxPerMs() >= 70) {
                major = s;
                break;
            }
        const int minor = major / 5;
        const int endMs = timeForX(width());
        for (int t = 0; t <= endMs; t += minor) {
            const int x = xForTime(t);
            const bool big = t % major == 0;
            p.setPen(pal.color(QPalette::Mid));
            p.drawLine(x, ry + kRulerHeight - (big ? 10 : 5), x, ry + kRulerHeight - 1);
            if (big) {
                p.setPen(pal.color(QPalette::Text));
                p.drawText(x + 3, ry + 2, 80, kRulerHeight - 8, Qt::AlignLeft | Qt::AlignTop, timeText(t));
            }
        }
        // Cabezal
        const int px = xForTime(m_editor->playTime());
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(230, 60, 60), 2));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor(230, 60, 60));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon{QPoint(px - 7, ry), QPoint(px + 7, ry), QPoint(px + 7, ry + 6), QPoint(px, ry + 13), QPoint(px - 7, ry + 6)});
        p.setRenderHint(QPainter::Antialiasing, false);
        paintHeader(p, hx);
    }

    // A la izquierda, fija al desplazarse: el nombre de cada pista y sus botones (como Filmora).
    void paintHeader(QPainter& p, int hx)
    {
        const QPalette pal = palette();
        p.fillRect(QRect(hx, 0, kHeaderWidth, height()), pal.color(QPalette::Window));
        p.setPen(pal.color(QPalette::Mid));
        p.drawLine(hx + kHeaderWidth - 1, 0, hx + kHeaderWidth - 1, height());
        auto button = [&](const QRect& r, const QIcon& icon, const QString& text) {
            const bool hot = r.contains(m_hover);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(hot ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid));
            QColor bg = hot ? pal.color(QPalette::Highlight) : pal.color(QPalette::Button);
            if (hot)
                bg.setAlpha(60);
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.setBrush(Qt::NoBrush);
            p.setRenderHint(QPainter::Antialiasing, false);
            const int side = std::min(16, r.height() - 4);
            if (text.isEmpty()) {
                icon.paint(&p, QRect(r.center().x() - side / 2 + 1, r.center().y() - side / 2 + 1, side, side));
                return;
            }
            icon.paint(&p, QRect(r.left() + 6, r.center().y() - side / 2 + 1, side, side));
            p.setPen(pal.color(QPalette::ButtonText));
            p.drawText(r.adjusted(side + 10, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        };
        QFont small = font();
        small.setPointSizeF(std::max(6.5, small.pointSizeF() * 0.9));
        p.setFont(small);
        button(addTrackRect(), Icons::theme({"list-add"}), GifEditor::tr("Añadir pista"));
        const int active = m_editor->activeTrack();
        for (int k = 0; k < tracks(); ++k) {
            const QRect row(hx, trackTop(k), kHeaderWidth - 1, kTrackHeight);
            if (k == active) {
                QColor c = pal.color(QPalette::Highlight);
                c.setAlpha(55);
                p.fillRect(row, c);
                p.fillRect(QRect(hx, row.top(), 3, row.height()), pal.color(QPalette::Highlight));
            }
            QFont bold = font();
            bold.setBold(true);
            p.setFont(bold);
            p.setPen(pal.color(QPalette::WindowText));
            p.drawText(row.adjusted(10, 6, -6, 0), Qt::AlignLeft | Qt::AlignTop, GifEditor::tr("Pista %1").arg(k + 1));
            p.setFont(small);
            const QList<int> frames = m_editor->framesOfTrack(k);
            p.setPen(pal.color(QPalette::PlaceholderText));
            if (kTrackHeight >= 64)
                p.drawText(row.adjusted(10, 24, -6, 0), Qt::AlignLeft | Qt::AlignTop,
                       GifEditor::tr("%n fotograma(s)", nullptr, int(frames.size())));
            const bool hidden = m_editor->trackHidden(k);
            button(eyeRect(k), hidden ? Icons::theme({"view-hidden"}) : Icons::theme({"visibility", "view-visible"}), QString());
            button(trashRect(k), Icons::theme({"edit-delete", "user-trash"}), QString());
        }
        // La franja «Audio» y cada pista de audio con su nombre, silenciar y quitar.
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRect(hx + 10, audioTop(0) - kAudioGap, kHeaderWidth - 16, kAudioGap - kTrackGap / 2), Qt::AlignLeft | Qt::AlignVCenter,
                   GifEditor::tr("AUDIO"));
        for (int row = 0; row < audioRows(); ++row) {
            const QRect head(hx, audioTop(row), kHeaderWidth - 1, kAudioHeight);
            p.setFont(bold);
            p.setPen(pal.color(QPalette::WindowText));
            p.drawText(head.adjusted(10, 0, -66, 0), Qt::AlignLeft | Qt::AlignVCenter, GifEditor::tr("Audio %1").arg(row + 1));
            p.setFont(small);
            const QList<int> clips = audioOfRow(row);
            if (clips.isEmpty())
                continue;
            const bool muted = std::all_of(clips.cbegin(), clips.cend(), [this](int i) { return m_editor->audioClips()[i].muted; });
            button(audioMuteRect(row), Icons::theme({muted ? "audio-volume-muted" : "audio-volume-high", "player-volume"}), QString());
            button(audioTrashRect(row), Icons::theme({"edit-delete", "user-trash"}), QString());
        }
    }

    QImage cachedThumb(const QImage& img, QSize box)
    {
        const QString key = QStringLiteral("%1:%2x%3").arg(img.cacheKey()).arg(box.width()).arg(box.height());
        auto it = m_thumbs.find(key);
        if (it != m_thumbs.end())
            return *it;
        if (m_thumbs.size() > 600)
            m_thumbs.clear();
        const QImage t = thumbnail(img, box);
        m_thumbs.insert(key, t);
        return t;
    }

    bool headerPress(QMouseEvent* e)
    {
        const QPoint pos = e->position().toPoint();
        if (pos.x() < headerX() + kHeaderWidth && e->button() == Qt::RightButton) {
            // La altura de las pistas, como en Filmora.
            QMenu menu(this);
            for (const auto& [text, h] : {std::pair{GifEditor::tr("Pistas pequeñas"), 48}, std::pair{GifEditor::tr("Pistas medianas"), 80},
                                          std::pair{GifEditor::tr("Pistas grandes"), 120}}) {
                QAction* a = menu.addAction(text, this, [this, height = h] {
                    setTrackHeight(height);
                    QSettings().setValue(QStringLiteral("gifEditor/trackHeight"), kTrackHeight);
                });
                a->setCheckable(true);
                a->setChecked(kTrackHeight == h);
            }
            menu.exec(e->globalPosition().toPoint());
            return true;
        }
        if (pos.x() >= headerX() + kHeaderWidth || e->button() != Qt::LeftButton)
            return pos.x() < headerX() + kHeaderWidth;
        if (const int edge = heightEdgeAt(pos); edge >= 0) {
            m_heightDrag = true;
            m_heightRow = tracks() - 1 - edge;
            m_heightStartY = pos.y();
            m_heightStart = kTrackHeight;
            return true;
        }
        if (addTrackRect().contains(pos)) {
            m_editor->addTrack();
            return true;
        }
        if (inRuler(pos))
            return true;
        if (const int row = audioRowAt(pos.y()); row >= 0) {
            const QList<int> clips = audioOfRow(row);
            if (clips.isEmpty())
                return true;
            QList<Audio::Clip> all = m_editor->audioClips();
            if (audioMuteRect(row).contains(pos)) {
                const bool mute = !std::all_of(clips.cbegin(), clips.cend(), [&all](int i) { return all[i].muted; });
                for (int i : clips)
                    all[i].muted = mute;
                m_editor->setAudioClips(all, mute ? GifEditor::tr("Silenciar el audio") : GifEditor::tr("Activar el sonido"));
            } else if (audioTrashRect(row).contains(pos)) {
                for (int j = int(clips.size()) - 1; j >= 0; --j)
                    all.removeAt(clips[j]);
                // Las de debajo suben una pista.
                for (Audio::Clip& c : all)
                    if (c.row > row)
                        --c.row;
                m_editor->selectAudio(-1);
                m_editor->setAudioClips(all, GifEditor::tr("Quitar el audio"));
            }
            return true;
        }
        const int k = trackAtY(pos.y());
        if (k < 0)
            return true;
        if (eyeRect(k).contains(pos))
            m_editor->toggleTrackHidden(k);
        else if (trashRect(k).contains(pos))
            m_editor->removeTrack(k);
        else
            m_editor->setActiveTrack(k);
        return true;
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (headerPress(e))
            return;
        // El audio: sus puntos de volumen y sus clips.
        int audioClip = -1;
        if (const int key = audioKeyAt(pos, &audioClip); key >= 0) {
            m_editor->selectAudio(audioClip);
            if (e->button() == Qt::RightButton) {
                QMenu menu(this);
                menu.addAction(Icons::theme({"edit-delete"}), GifEditor::tr("Quitar el punto de volumen"), this, [this, audioClip, key] {
                    QList<Audio::Clip> clips = m_editor->audioClips();
                    const int at = clips[audioClip].volume.keyAt(key);
                    if (at >= 0)
                        clips[audioClip].volume.keys.removeAt(at);
                    m_editor->setAudioClips(clips, GifEditor::tr("Quitar el punto de volumen"));
                });
                menu.exec(e->globalPosition().toPoint());
                return;
            }
            if (e->button() == Qt::LeftButton) {
                m_aKeyClip = audioClip;
                m_aKeyFrom = key;
                m_aKeyTo = key;
                const Audio::Clip c = m_editor->audioClips()[audioClip];
                m_aKeyValue = c.volume.keys[c.volume.keyAt(key)].value;
            }
            return;
        }
        if (const int a = audioAt(pos); a >= 0) {
            m_editor->selectAudio(a);
            if (e->button() == Qt::RightButton) {
                m_editor->showAudioMenu(a, e->globalPosition().toPoint());
                return;
            }
            if (e->button() != Qt::LeftButton)
                return;
            m_editor->seek(timeForX(pos.x()));
            const QRect r = audioRect(a);
            m_aMode = pos.x() >= r.right() - kEdgeGrab ? 2 : pos.x() <= r.left() + kEdgeGrab ? 1 : 0;
            m_aEdit = a;
            m_aEditing = false;
            m_aClip = m_editor->audioClips()[a];
            m_aPressPos = pos;
            return;
        }
        if (audioRowAt(pos.y()) >= 0)
            m_editor->selectAudio(-1);
        int keyFrame = -1;
        if (const int key = keyAt(pos, &keyFrame); key >= 0) {
            m_editor->selectKey(keyFrame, key);
            if (e->button() == Qt::RightButton) {
                m_editor->showKeyMenu(key, e->globalPosition().toPoint());
                return;
            }
            if (e->button() == Qt::LeftButton) {
                m_keyFrame = keyFrame;
                m_keyDrag = key;
                m_keyDragTo = key;
            }
            return;
        }
        const int index = clipAt(pos);
        if (inRuler(pos) || (e->button() == Qt::LeftButton && index < 0)) {
            if (const int k = trackAtY(pos.y()); k >= 0 && k != m_editor->activeTrack())
                m_editor->setActiveTrack(k);
            m_scrubbing = e->button() == Qt::LeftButton;
            m_editor->seek(timeForX(pos.x()));
            return;
        }
        if (index < 0)
            return;
        // Ctrl+clic añade o quita de la selección; Mayús+clic elige desde el elegido hasta aquí.
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)) {
            m_editor->toggleFrameSelection(index);
            return;
        }
        if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier)) {
            m_editor->selectFrameRange(index);
            return;
        }
        m_editor->selectAudio(-1);
        // El clic derecho sobre uno de los elegidos conserva la selección (el menú es para todos).
        if (!(e->button() == Qt::RightButton && m_editor->isFrameSelected(index)))
            m_editor->selectFrame(index);
        // El cabezal va donde se hizo clic (no al principio del clip).
        if (e->button() == Qt::LeftButton)
            m_editor->seek(std::clamp(timeForX(pos.x()), m_editor->frameStart(index),
                                      std::max(m_editor->frameStart(index), m_editor->frameEnd(index) - 1)));
        if (e->button() == Qt::RightButton) {
            m_editor->showFrameMenu(index, e->globalPosition().toPoint());
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        const QRect r = clipRect(index);
        if (pos.x() >= r.right() - kEdgeGrab) {
            m_resizing = index;
            m_resizeDelay = m_editor->frameEnd(index) - m_editor->frameStart(index);
            return;
        }
        m_pressIndex = index;
        m_pressPos = pos;
        m_grabDx = pos.x() - r.left();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (m_hover != pos && (pos.x() < headerX() + kHeaderWidth || m_hover.x() < headerX() + kHeaderWidth)) {
            m_hover = pos;
            update(QRect(headerX(), 0, kHeaderWidth, height()));
        }
        m_hover = pos;
        if (m_heightDrag) {
            // Las filas de arriba crecen igual: el borde sigue al puntero.
            setTrackHeight(m_heightStart + (pos.y() - m_heightStartY) / (m_heightRow + 1));
            return;
        }
        if (m_aKeyClip >= 0 && (e->buttons() & Qt::LeftButton)) {
            const Audio::Clip c = m_editor->audioClips().value(m_aKeyClip);
            m_aKeyTo = std::clamp(int(std::lround((timeForX(pos.x()) - c.start) / 10.0)) * 10, 0, c.duration);
            m_aKeyValue = std::round(volumeForY(audioRect(m_aKeyClip), pos.y()));
            setToolTip(QStringLiteral("%1 % · %2").arg(m_aKeyValue).arg(timeText(c.start + m_aKeyTo)));
            update();
            return;
        }
        if (m_aEdit >= 0 && (e->buttons() & Qt::LeftButton)) {
            if (!m_aEditing && (pos - m_aPressPos).manhattanLength() < QApplication::startDragDistance())
                return;
            m_aEditing = true;
            const Audio::Clip orig = m_editor->audioClips().value(m_aEdit);
            Audio::Clip c = orig;
            const int dt = int(std::lround((pos.x() - m_aPressPos.x()) / pxPerMs()));
            if (m_aMode == 0) {
                c.start = std::max(0, orig.start + dt);
                // Imán: al cabezal, al principio y a los bordes de los fotogramas.
                int best = kSnap + 1;
                auto snap = [&](int t) {
                    for (const int candidate : {t, t - c.duration})
                        if (candidate >= 0 && std::abs(xForTime(candidate) - xForTime(c.start)) < best) {
                            best = std::abs(xForTime(candidate) - xForTime(c.start));
                            c.start = candidate;
                        }
                };
                const int raw = c.start;
                snap(0);
                snap(m_editor->playTime());
                for (int i = 0; i < m_editor->frameCount(); ++i) {
                    snap(m_editor->frameStart(i));
                    snap(m_editor->frameEnd(i));
                }
                if (best > kSnap)
                    c.start = raw;
                if (const int row = audioRowAt(pos.y()); row >= 0)
                    c.row = row;
                setToolTip(timeText(c.start));
            } else if (m_aMode == 2) {
                c.duration = std::clamp(orig.duration + dt, 100, std::max(100, orig.length - orig.offset));
                setToolTip(timeText(c.duration));
            } else {
                const int d = std::clamp(dt, std::max(-orig.offset, -orig.start), orig.duration - 100);
                c.start = orig.start + d;
                c.offset = orig.offset + d;
                c.duration = orig.duration - d;
                for (Motion::Key& k : c.volume.keys)
                    k.t = std::max(0, k.t - d);
                setToolTip(timeText(c.start));
            }
            m_aClip = c;
            update();
            return;
        }
        if (m_keyDrag >= 0) {
            // Más lejos un rombo del otro, más dura el movimiento entre ellos (de 10 en 10 ms). Más allá
            // del final, el fotograma dura más.
            m_keyDragTo = std::max(0, int(std::round((timeForX(pos.x()) - m_editor->frameStart(m_keyFrame)) / 10.0)) * 10);
            setToolTip(QStringLiteral("%1 ms").arg(m_keyDragTo));
            update();
            return;
        }
        if (m_scrubbing) {
            m_editor->seek(timeForX(pos.x()));
            return;
        }
        if (m_resizing >= 0) {
            // Imán: el final se pega a las uniones de las otras pistas y al cabezal.
            int endTime = timeForX(pos.x());
            int bestDistance = kSnap / 2 + 1;
            auto snap = [&](int t) {
                const int d = std::abs(xForTime(t) - pos.x());
                if (d < bestDistance) {
                    bestDistance = d;
                    endTime = t;
                }
            };
            snap(m_editor->playTime());
            for (int i = 0; i < m_editor->frameCount(); ++i)
                if (m_editor->trackOf(i) != m_editor->trackOf(m_resizing)) {
                    snap(m_editor->frameStart(i));
                    snap(m_editor->frameEnd(i));
                }
            const int raw = endTime - m_editor->frameStart(m_resizing);
            // De 10 en 10 ms, como la duración de los GIF (centésimas de segundo).
            m_resizeDelay = std::max(Gif::kMinDelayMs, int(std::round(raw / 10.0)) * 10);
            update();
            setToolTip({});
            return;
        }
        if (m_pressIndex >= 0 && (e->buttons() & Qt::LeftButton)) {
            if (!m_reordering && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
                m_reordering = true;
            if (m_reordering) {
                // El clip va donde se suelte, en el tiempo (con espacio delante si se deja, como en
                // Filmora) y en la pista de esa fila. Como con un imán, se pega al cabezal y a los
                // bordes de los demás clips al acercarse.
                const int k = trackAtY(pos.y());
                m_dropTrack = k >= 0 ? k : m_editor->trackOf(m_pressIndex);
                const int left = pos.x() - m_grabDx, width = clipRect(m_pressIndex).width() + 2;
                m_ghostX = std::max(kLeftMargin, left);
                int best = kSnap + 1;
                auto snap = [&](int t) {
                    const int x = xForTime(t);
                    for (const int candidate : {x, x - width}) {
                        if (candidate >= kLeftMargin && std::abs(candidate - left) < best) {
                            best = std::abs(candidate - left);
                            m_ghostX = candidate;
                        }
                    }
                };
                snap(0);
                snap(m_editor->playTime());
                for (int i = 0; i < m_editor->frameCount(); ++i)
                    if (i != m_pressIndex) {
                        snap(m_editor->frameStart(i));
                        snap(m_editor->frameEnd(i));
                    }
                m_dropAt = -1;
                setToolTip(timeText(timeForX(m_ghostX)));
                setCursor(Qt::ClosedHandCursor);
                update();
            }
            return;
        }
        // El cursor de estirar sobre el borde derecho de un clip; la mano sobre un rombo; el de la
        // altura sobre el borde de abajo de una pista, en la cabecera.
        int keyFrame = -1, audioKey = -1;
        const int index = clipAt(pos);
        const int audio = audioAt(pos);
        if (audioKeyAt(pos, &audioKey) >= 0) {
            setCursor(Qt::PointingHandCursor);
            return;
        }
        if (audio >= 0) {
            const QRect r = audioRect(audio);
            setCursor(pos.x() >= r.right() - kEdgeGrab || pos.x() <= r.left() + kEdgeGrab ? Qt::SizeHorCursor : Qt::OpenHandCursor);
            return;
        }
        setCursor(heightEdgeAt(pos) >= 0         ? Qt::SizeVerCursor
                  : keyAt(pos, &keyFrame) >= 0                                       ? Qt::PointingHandCursor
                  : index >= 0 && pos.x() >= clipRect(index).right() - kEdgeGrab ? Qt::SizeHorCursor
                                                                                  : Qt::ArrowCursor);
    }

    void leaveEvent(QEvent*) override
    {
        m_hover = QPoint(-1, -1);
        update(QRect(headerX(), 0, kHeaderWidth, height()));
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (m_heightDrag) {
            m_heightDrag = false;
            QSettings().setValue(QStringLiteral("gifEditor/trackHeight"), kTrackHeight);
            return;
        }
        if (m_aKeyClip >= 0) {
            const int clip = m_aKeyClip;
            m_aKeyClip = -1;
            setToolTip({});
            QList<Audio::Clip> clips = m_editor->audioClips();
            if (clip < clips.size()) {
                Motion::Track& v = clips[clip].volume;
                const int at = v.keyAt(m_aKeyFrom);
                if (at >= 0)
                    v.keys.removeAt(at);
                v.setKey(m_aKeyTo, m_aKeyValue);
                m_editor->setAudioClips(clips, GifEditor::tr("Cambiar el volumen"));
            }
            update();
            return;
        }
        if (m_aEdit >= 0) {
            const int clip = m_aEdit;
            const bool edited = m_aEditing;
            m_aEdit = -1;
            m_aEditing = false;
            setToolTip({});
            QList<Audio::Clip> clips = m_editor->audioClips();
            if (edited && clip < clips.size()) {
                clips[clip] = m_aClip;
                m_editor->setAudioClips(clips, m_aMode == 0 ? GifEditor::tr("Mover el audio") : GifEditor::tr("Recortar el audio"));
            }
            update();
            return;
        }
        if (m_keyDrag >= 0) {
            const int from = m_keyDrag, to = m_keyDragTo;
            m_keyDrag = -1;
            setToolTip({});
            if (from != to)
                m_editor->moveKeyTime(from, to);
            update();
            return;
        }
        if (m_scrubbing) {
            m_scrubbing = false;
            return;
        }
        if (m_resizing >= 0) {
            const int index = m_resizing, delay = m_resizeDelay;
            m_resizing = -1;
            m_editor->setFrameDelay(index, delay);
            update();
            return;
        }
        if (m_reordering) {
            const int from = m_pressIndex;
            const int track = m_dropTrack >= 0 ? m_dropTrack : m_editor->trackOf(from);
            const int time = timeForX(m_ghostX);
            m_reordering = false;
            m_pressIndex = -1;
            m_dropAt = -1;
            m_dropTrack = -1;
            setToolTip({});
            setCursor(Qt::ArrowCursor);
            m_editor->placeFrame(from, track, time);
            update();
            return;
        }
        m_pressIndex = -1;
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        // En un clip de audio: un punto de volumen donde se hizo doble clic.
        const QPoint at = e->position().toPoint();
        int audioKey = -1;
        if (audioKeyAt(at, &audioKey) < 0) {
            if (const int a = audioAt(at); a >= 0) {
                QList<Audio::Clip> clips = m_editor->audioClips();
                Audio::Clip& c = clips[a];
                const int t = std::clamp(int(std::lround((timeForX(at.x()) - c.start) / 10.0)) * 10, 0, c.duration);
                c.volume.setKey(t, std::round(volumeForY(audioRect(a), at.y())));
                m_editor->setAudioClips(clips, GifEditor::tr("Añadir un punto de volumen"));
                return;
            }
        }
        // Sobre un rombo, el doble clic solo lo elige.
        int keyFrame = -1;
        if (const int key = keyAt(e->position().toPoint(), &keyFrame); key >= 0) {
            m_editor->selectKey(keyFrame, key);
            return;
        }
        // En un fotograma: un punto clave donde se hizo doble clic (como en los de audio). La
        // duración se cambia con su clic derecho o en la barra de arriba.
        const int index = clipAt(at);
        if (index >= 0 && at.x() >= headerX() + kHeaderWidth) {
            const int t = int(std::lround((timeForX(at.x()) - m_editor->frameStart(index)) / 10.0)) * 10;
            m_editor->addGlobalKey(index, t);
        }
    }

    void wheelEvent(QWheelEvent* e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            setPixelsPerSecond(int(m_pps * (e->angleDelta().y() > 0 ? 1.25 : 0.8)));
            if (onZoom)
                onZoom(m_pps);
            e->accept();
            return;
        }
        // La rueda desplaza a lo largo de la línea de tiempo; si no hay a dónde (cabe entera) o con
        // Mayús, entre las pistas. Sin nada que desplazar, acerca o aleja como con Ctrl.
        if (auto* area = qobject_cast<QScrollArea*>(parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
            QScrollBar* across = area->horizontalScrollBar();
            QScrollBar* down = area->verticalScrollBar();
            const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
            QScrollBar* bar = (e->modifiers() & Qt::ShiftModifier) || across->maximum() <= 0 ? down : across;
            if (e->angleDelta().x() != 0 && e->angleDelta().y() == 0)
                bar = across;
            if (bar->maximum() <= 0) {
                setPixelsPerSecond(int(m_pps * (delta > 0 ? 1.25 : 0.8)));
                if (onZoom)
                    onZoom(m_pps);
            } else {
                bar->setValue(bar->value() - delta / 2);
            }
            e->accept();
            return;
        }
        QWidget::wheelEvent(e);
    }

    void dragEnterEvent(QDragEnterEvent* e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(MediaList::mimeType())) || e->mimeData()->hasUrls())
            e->acceptProposedAction();
    }

    int dropTrackAt(int y) const
    {
        const int k = trackAtY(y);
        return k >= 0 ? k : m_editor->activeTrack();
    }

    void dragMoveEvent(QDragMoveEvent* e) override
    {
        if (audioRowAt(e->position().toPoint().y()) >= 0) {
            m_dropAt = -1;
            m_dropTrack = -1;
            update();
            e->acceptProposedAction();
            return;
        }
        m_dropTrack = dropTrackAt(e->position().toPoint().y());
        m_dropAt = insertionAt(e->position().toPoint().x(), m_dropTrack);
        update();
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override
    {
        m_dropAt = -1;
        m_dropTrack = -1;
        update();
    }

    void dropEvent(QDropEvent* e) override
    {
        const int track = dropTrackAt(e->position().toPoint().y());
        const int at = insertionAt(e->position().toPoint().x(), track);
        m_dropAt = -1;
        m_dropTrack = -1;
        update();
        const QMimeData* data = e->mimeData();
        if (data->hasFormat(QString::fromLatin1(MediaList::mimeType()))) {
            QList<int> indexes;
            for (const QByteArray& n : data->data(QString::fromLatin1(MediaList::mimeType())).split(' '))
                indexes << n.toInt();
            // En el orden de la biblioteca, uno detrás de otro.
            int offset = 0;
            for (int i : std::as_const(indexes)) {
                const int before = m_editor->frameCount();
                m_editor->insertMedia(i, at + offset, track);
                offset += m_editor->frameCount() - before;
            }
            e->acceptProposedAction();
            return;
        }
        QStringList paths, audio;
        for (const QUrl& url : data->urls())
            if (url.isLocalFile())
                (Audio::isAudioFile(url.toLocalFile()) ? audio : paths) << url.toLocalFile();
        // Los audios, donde se sueltan (en su pista de audio o en la primera libre).
        if (!audio.isEmpty()) {
            m_editor->addAudioFiles(audio, timeForX(e->position().toPoint().x()), audioRowAt(e->position().toPoint().y()));
            e->acceptProposedAction();
        }
        if (!paths.isEmpty()) {
            m_editor->insertFiles(paths, at, track);
            e->acceptProposedAction();
        }
    }

private:
    GifEditor* m_editor;
    int m_pps = 600;
    int m_resizing = -1;
    int m_resizeDelay = 0;
    int m_pressIndex = -1;
    QPoint m_pressPos;
    QPoint m_hover{-1, -1};
    bool m_reordering = false;
    bool m_scrubbing = false;
    int m_dropAt = -1;
    int m_dropTrack = -1;
    // Arrastrando un clip: dónde se agarró y dónde se dibuja.
    int m_grabDx = 0;
    int m_ghostX = 0;
    // Cambiando la altura de las pistas desde la cabecera.
    bool m_heightDrag = false;
    int m_heightRow = 0;
    int m_heightStartY = 0;
    int m_heightStart = 0;
    int m_keyFrame = -1;
    int m_keyDrag = -1;
    int m_keyDragTo = 0;
    // Audio: el clip que se mueve (0) o se recorta por el principio (1) o el final (2), y el punto
    // de volumen que se arrastra.
    int m_aEdit = -1;
    int m_aMode = 0;
    bool m_aEditing = false;
    Audio::Clip m_aClip;
    QPoint m_aPressPos;
    int m_aKeyClip = -1;
    int m_aKeyFrom = 0;
    int m_aKeyTo = 0;
    double m_aKeyValue = 100;
    QHash<QString, QImage> m_thumbs;
};

// A la derecha, con un clip de audio elegido: su volumen (con su punto clave en el cabezal), los
// fundidos de entrada y salida y silenciarlo.
class AudioInspector : public QWidget {
public:
    AudioInspector(GifEditor* editor, QWidget* parent)
        : QWidget(parent)
        , m_editor(editor)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);
        m_title = new QLabel(this);
        QFont f = m_title->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.15);
        m_title->setFont(f);
        m_title->setWordWrap(true);
        layout->addWidget(m_title);
        m_info = new QLabel(this);
        m_info->setWordWrap(true);
        m_info->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(m_info);

        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        grid->addWidget(new QLabel(GifEditor::tr("Volumen:"), this), 0, 0);
        m_volume = new QDoubleSpinBox(this);
        m_volume->setRange(0, 200);
        m_volume->setDecimals(0);
        m_volume->setSingleStep(5);
        m_volume->setSuffix(QStringLiteral(" %"));
        grid->addWidget(m_volume, 0, 1);
        m_key = new QToolButton(this);
        m_key->setAutoRaise(true);
        m_key->setToolTip(GifEditor::tr("Punto clave de volumen en el cabezal: con varios, el volumen cambia de uno a otro"));
        grid->addWidget(m_key, 0, 2);
        m_slider = new QSlider(Qt::Horizontal, this);
        m_slider->setRange(0, 200);
        grid->addWidget(m_slider, 1, 0, 1, 3);
        grid->addWidget(new QLabel(GifEditor::tr("Fundido de entrada:"), this), 2, 0);
        m_fadeIn = new QDoubleSpinBox(this);
        grid->addWidget(m_fadeIn, 2, 1, 1, 2);
        grid->addWidget(new QLabel(GifEditor::tr("Fundido de salida:"), this), 3, 0);
        m_fadeOut = new QDoubleSpinBox(this);
        grid->addWidget(m_fadeOut, 3, 1, 1, 2);
        for (QDoubleSpinBox* fade : {m_fadeIn, m_fadeOut}) {
            fade->setRange(0, 30);
            fade->setDecimals(1);
            fade->setSingleStep(0.1);
            fade->setSuffix(GifEditor::tr(" s"));
        }
        layout->addLayout(grid);
        m_mute = new QCheckBox(GifEditor::tr("Silenciar"), this);
        layout->addWidget(m_mute);
        auto* remove = new QPushButton(Icons::theme({"edit-delete", "user-trash"}), GifEditor::tr("Quitar el audio"), this);
        layout->addWidget(remove);
        auto* note = new QLabel(GifEditor::tr("El audio va en los vídeos exportados (MP4 y WebM): el GIF no tiene sonido. Doble clic en "
                                              "el clip añade un punto de volumen; arrastra los puntos para cambiarlo."),
                                this);
        note->setWordWrap(true);
        note->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(note);
        layout->addStretch();

        connect(m_volume, &QDoubleSpinBox::editingFinished, this, [this] { setVolume(m_volume->value()); });
        connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
            if (m_updating)
                return;
            const QSignalBlocker block(m_volume);
            m_volume->setValue(v);
        });
        connect(m_slider, &QSlider::sliderReleased, this, [this] { setVolume(m_slider->value()); });
        connect(m_key, &QToolButton::clicked, this, [this] { m_editor->toggleAudioKey(m_editor->selectedAudio()); });
        connect(m_fadeIn, &QDoubleSpinBox::editingFinished, this, [this] { edit([this](Audio::Clip& c) { c.fadeIn = int(m_fadeIn->value() * 1000); }); });
        connect(m_fadeOut, &QDoubleSpinBox::editingFinished, this,
                [this] { edit([this](Audio::Clip& c) { c.fadeOut = int(m_fadeOut->value() * 1000); }); });
        connect(m_mute, &QCheckBox::toggled, this, [this](bool on) {
            if (!m_updating)
                edit([on](Audio::Clip& c) { c.muted = on; }, on ? GifEditor::tr("Silenciar el audio") : GifEditor::tr("Activar el sonido"));
        });
        connect(remove, &QPushButton::clicked, this, [this] { m_editor->removeAudio(m_editor->selectedAudio()); });
    }

    void refresh()
    {
        const int i = m_editor->selectedAudio();
        const QList<Audio::Clip> clips = m_editor->audioClips();
        if (i < 0 || i >= clips.size())
            return;
        const Audio::Clip& c = clips[i];
        m_updating = true;
        m_title->setText(QStringLiteral("♪ ") + c.name);
        m_info->setText(GifEditor::tr("Empieza en %1 y suena %2 (de %3).").arg(timeText(c.start), timeText(c.duration), timeText(c.length)));
        const int local = std::clamp(m_editor->playTime() - c.start, 0, c.duration);
        const double v = c.volume.value(local);
        if (!m_volume->hasFocus())
            m_volume->setValue(v);
        m_slider->setValue(int(std::lround(v)));
        m_fadeIn->setValue(c.fadeIn / 1000.0);
        m_fadeOut->setValue(c.fadeOut / 1000.0);
        m_mute->setChecked(c.muted);
        // El rombo: lleno si hay un punto en el cabezal.
        QPixmap pm(QSize(20, 20) * devicePixelRatioF());
        pm.setDevicePixelRatio(devicePixelRatioF());
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paintDiamond(p, QPointF(10, 10), 7, c.volume.keyAt(local) >= 0, c.volume.keyAt(local) >= 0, palette());
        p.end();
        m_key->setIcon(QIcon(pm));
        m_updating = false;
    }

private:
    void edit(const std::function<void(Audio::Clip&)>& fn, const QString& text = {})
    {
        const int i = m_editor->selectedAudio();
        QList<Audio::Clip> clips = m_editor->audioClips();
        if (m_updating || i < 0 || i >= clips.size())
            return;
        fn(clips[i]);
        m_editor->setAudioClips(clips, text.isEmpty() ? GifEditor::tr("Cambiar el audio") : text);
    }
    // Sin puntos clave, el volumen de todo el clip; con ellos, el del punto del cabezal (se pone uno).
    void setVolume(double v)
    {
        edit(
            [this, v](Audio::Clip& c) {
                if (c.volume.keys.isEmpty())
                    c.volume.base = v;
                else
                    c.volume.setKey(std::clamp(m_editor->playTime() - c.start, 0, c.duration), v);
            },
            GifEditor::tr("Cambiar el volumen"));
    }

    GifEditor* m_editor;
    QLabel* m_title;
    QLabel* m_info;
    QDoubleSpinBox* m_volume;
    QToolButton* m_key;
    QSlider* m_slider;
    QDoubleSpinBox* m_fadeIn;
    QDoubleSpinBox* m_fadeOut;
    QCheckBox* m_mute;
    bool m_updating = false;
};

} // namespace GifEdit

using namespace GifEdit;

// Las voces del audio mientras se reproduce (con Qt Multimedia): un reproductor por clip, con su
// archivo en una carpeta temporal.
struct GifEditor::AudioVoices {
#ifdef KAINT_HAVE_QTMULTIMEDIA
    struct Voice {
        QMediaPlayer* player = nullptr;
        QAudioOutput* output = nullptr;
        QByteArray data;
    };
    QTemporaryDir dir;
    QList<Voice> voices;
    int files = 0;
    ~AudioVoices()
    {
        for (Voice& v : voices) {
            delete v.player;
            delete v.output;
        }
    }
#endif
};

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

GifEditor::GifEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("GifEditor"));
    setAcceptDrops(false);
    buildUi();
    applyLook();
    // Preciso: con el de grano grueso, en algunos sistemas los fotogramas cortos se saltaban.
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(10);
    connect(&m_ticker, &QTimer::timeout, this, &GifEditor::tick);
}

GifEditor::~GifEditor() = default;

void GifEditor::buildUi()
{
    // Más aire que el resto de Kaint: botones grandes con su texto, como un editor de vídeo.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(8);

    // Cabecera: el nombre y volver al editor de imágenes.
    auto* headBox = new QFrame(this);
    headBox->setObjectName(QStringLiteral("GifEditorHeader"));
    headBox->setProperty("gifPanel", true);
    headBox->setFrameShape(QFrame::StyledPanel);
    auto* head = new QHBoxLayout(headBox);
    head->setContentsMargins(10, 6, 8, 6);
    head->setSpacing(8);
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    m_title->setFont(titleFont);
    auto* back = new QPushButton(Icons::theme({"draw-brush", "applications-graphics"}), tr("Editor de imágenes"), this);
    back->setToolTip(tr("Vuelve a editar esta imagen con las herramientas (cada fotograma es una capa)."));
    back->setIconSize(QSize(20, 20));
    back->setMinimumHeight(34);
    connect(back, &QPushButton::clicked, this, &GifEditor::imageEditorRequested);
    // Exportar: el GIF (con la configuración de siempre) o, en el menú de la flecha, otros formatos.
    auto* exportButton = new QToolButton(this);
    exportButton->setIcon(Icons::theme({"document-export", "document-save-as"}));
    exportButton->setIconSize(QSize(20, 20));
    exportButton->setMinimumHeight(34);
    exportButton->setText(tr("Exportar GIF..."));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton->setPopupMode(QToolButton::MenuButtonPopup);
    exportButton->setToolTip(tr("Guarda la animación como GIF, con la configuración de siempre (paleta, duración, "
                                "repeticiones, efectos...). En la flecha, otros formatos: PNG animado, WebP animado y vídeo "
                                "MP4 o WebM."));
    connect(exportButton, &QToolButton::clicked, this, [this] { emit exportRequested(QStringLiteral("gif")); });
    auto* exportMenu = new QMenu(exportButton);
    for (const auto& [format, text] : {std::pair{QStringLiteral("gif"), tr("GIF...")},
                                       std::pair{QStringLiteral("apng"), tr("PNG animado (APNG)...")},
                                       std::pair{QStringLiteral("webp"), tr("WebP animado...")},
                                       std::pair{QStringLiteral("mp4"), tr("Vídeo MP4...")},
                                       std::pair{QStringLiteral("webm"), tr("Vídeo WebM...")}})
        exportMenu->addAction(text, this, [this, format] { emit exportRequested(format); });
    exportButton->setMenu(exportMenu);
    // Cuánto pesará el GIF y cuánto dura, con una barrita que avisa si pesa demasiado.
    m_sizeMeter = new GifEdit::SizeMeter(this);
    m_sizeTimer.setSingleShot(true);
    m_sizeTimer.setInterval(600);
    connect(&m_sizeTimer, &QTimer::timeout, this, &GifEditor::estimateSize);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::theme({"video-x-generic", "image-gif", "media-playback-start"}).pixmap(26, 26));
    head->addWidget(icon);
    head->addWidget(m_title, 1);
    head->addWidget(m_sizeMeter);
    head->addSpacing(6);
    head->addWidget(back);
    head->addWidget(exportButton);
    root->addWidget(headBox);

    auto* vertical = new QSplitter(Qt::Vertical, this);
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(8);
    auto* top = new QSplitter(Qt::Horizontal, vertical);
    top->setChildrenCollapsible(false);
    top->setHandleWidth(8);

    // A la izquierda, en pestañas: la biblioteca de medios y el historial del editor de GIF.
    m_leftTabs = new QTabWidget(top);
    m_leftTabs->setDocumentMode(true);
    // Sin flechas de desplazamiento: con el panel estrecho, los nombres se acortan.
    m_leftTabs->tabBar()->setUsesScrollButtons(false);
    m_leftTabs->tabBar()->setElideMode(Qt::ElideRight);
    m_leftTabs->tabBar()->setExpanding(true);
    auto* bin = new QWidget(m_leftTabs);
    auto* binLayout = new QVBoxLayout(bin);
    binLayout->setContentsMargins(0, 6, 0, 0);
    binLayout->setSpacing(6);
    m_mediaList = new MediaList(bin);
    m_mediaList->setToolTip(tr("Las imágenes abiertas en Kaint y las que importes. Arrástralas a la línea de tiempo "
                               "(o haz doble clic para añadirlas al final de la pista elegida)."));
    connect(m_mediaList, &MediaList::filesDropped, this, &GifEditor::importFiles);
    connect(m_mediaList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { insertMedia(item->data(Qt::UserRole).toInt(), -1, activeTrack()); });
    auto* binButtons = new QHBoxLayout;
    auto* import = new QPushButton(Icons::theme({"document-import", "document-open"}), tr("Importar..."), bin);
    import->setMinimumHeight(32);
    connect(import, &QPushButton::clicked, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Importar imágenes o GIF"), QString(),
                                                                ImageIO::openFilter() + QStringLiteral(";;") + tr("Vídeo (%1)").arg(GifExport::videoPatterns()));
        importFiles(paths);
    });
    auto* add = new QPushButton(Icons::theme({"list-add"}), tr("Añadir"), bin);
    add->setMinimumHeight(32);
    add->setToolTip(tr("Añade los medios elegidos a la pista elegida, detrás del fotograma elegido."));
    connect(add, &QPushButton::clicked, this, [this] {
        const int track = activeTrack();
        int at = selectedFrame() >= 0 && trackOf(selectedFrame()) == track ? selectedFrame() + 1 : -1;
        for (QListWidgetItem* item : m_mediaList->selectedItems()) {
            const int before = frameCount();
            insertMedia(item->data(Qt::UserRole).toInt(), at, track);
            if (at >= 0)
                at += frameCount() - before;
        }
    });
    binButtons->addWidget(import);
    binButtons->addWidget(add);
    binLayout->addWidget(m_mediaList, 1);
    binLayout->addLayout(binButtons);
    m_leftTabs->addTab(bin, Icons::theme({"folder-pictures", "image-x-generic"}), tr("Medios"));

    // Vista previa y reproductor
    auto* player = new QWidget(top);
    auto* playerLayout = new QVBoxLayout(player);
    playerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayout->setSpacing(6);
    m_preview = new Preview(this, player);
    auto* transportBox = new QFrame(player);
    transportBox->setFrameShape(QFrame::StyledPanel);
    transportBox->setProperty("gifPanel", true);
    auto* transport = new QHBoxLayout(transportBox);
    transport->setContentsMargins(8, 4, 8, 4);
    transport->setSpacing(4);
    auto button = [this, player](std::initializer_list<const char*> icons, const QString& tip, auto fn, int side = 24) {
        auto* b = new QToolButton(player);
        b->setIcon(Icons::theme(icons));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(side, side));
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };
    m_timeLabel = new QLabel(player);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(std::max(mono.pointSizeF(), font().pointSizeF()));
    m_timeLabel->setFont(mono);
    transport->addWidget(m_timeLabel);
    transport->addStretch(1);
    transport->addWidget(button({"media-skip-backward"}, tr("Primer fotograma (Inicio)"), [this] { seek(0); }));
    transport->addWidget(button({"media-seek-backward"}, tr("Fotograma anterior (←)"), [this] { step(-1); }));
    m_play = button({"media-playback-start"}, tr("Reproducir o pausar (Espacio)"), [this] { togglePlay(); }, 32);
    transport->addWidget(m_play);
    transport->addWidget(button({"media-playback-stop"}, tr("Detener"), [this] { stop(); seek(0); }));
    transport->addWidget(button({"media-seek-forward"}, tr("Fotograma siguiente (→)"), [this] { step(1); }));
    transport->addWidget(button({"media-skip-forward"}, tr("Último fotograma (Fin)"), [this] {
        const QList<int> frames = framesOfTrack(activeTrack());
        if (!frames.isEmpty())
            seek(frameStart(frames.last()));
    }));
    transport->addStretch(1);
    m_loop = new QCheckBox(tr("Repetir"), player);
    m_loop->setChecked(true);
    m_speed = new QComboBox(player);
    for (const auto& [label, factor] : {std::pair{"0.25×", 0.25}, {"0.5×", 0.5}, {"1×", 1.0}, {"1.5×", 1.5}, {"2×", 2.0}, {"4×", 4.0}})
        m_speed->addItem(QString::fromUtf8(label), factor);
    m_speed->setCurrentIndex(2);
    m_speed->setToolTip(tr("Velocidad de la vista previa (no cambia el GIF)"));
    transport->addWidget(m_loop);
    transport->addWidget(m_speed);
    playerLayout->addWidget(m_preview, 1);
    playerLayout->addWidget(transportBox);

    // A la derecha, como en Filmora: la animación del fotograma elegido.
    auto* inspectorScroll = new QScrollArea(top);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inspector = new Inspector(this, inspectorScroll);
    inspectorScroll->setWidget(m_inspector);
    inspectorScroll->setMinimumWidth(260);
    // Con un clip de audio elegido, en su lugar, lo del audio.
    m_rightStack = new QStackedWidget(top);
    m_rightStack->setProperty("gifPanel", true);
    m_rightStack->addWidget(inspectorScroll);
    auto* audioScroll = new QScrollArea(m_rightStack);
    audioScroll->setWidgetResizable(true);
    audioScroll->setFrameShape(QFrame::NoFrame);
    audioScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_audioInspector = new AudioInspector(this, audioScroll);
    audioScroll->setWidget(m_audioInspector);
    m_rightStack->addWidget(audioScroll);

    top->addWidget(m_leftTabs);
    top->addWidget(player);
    top->addWidget(m_rightStack);
    top->setStretchFactor(0, 2);
    top->setStretchFactor(1, 5);
    top->setStretchFactor(2, 2);

    // Línea de tiempo con su barra
    auto* timelineBox = new QFrame(vertical);
    timelineBox->setFrameShape(QFrame::StyledPanel);
    timelineBox->setProperty("gifPanel", true);
    auto* timelineLayout = new QVBoxLayout(timelineBox);
    timelineLayout->setContentsMargins(6, 6, 6, 6);
    timelineLayout->setSpacing(6);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    auto tool = [this, timelineBox](std::initializer_list<const char*> icons, const QString& text, const QString& tip, auto fn,
                                    bool frame) {
        auto* b = new QToolButton(timelineBox);
        b->setIcon(Icons::theme(icons));
        b->setIconSize(QSize(20, 20));
        b->setMinimumHeight(32);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        connect(b, &QToolButton::clicked, this, fn);
        if (frame)
            m_frameButtons << b;
        return b;
    };
    auto separator = [timelineBox, bar] {
        auto* line = new QFrame(timelineBox);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);
        bar->addSpacing(4);
        bar->addWidget(line);
        bar->addSpacing(4);
    };
    bar->addWidget(tool({"edit-undo"}, {}, tr("Deshacer (Ctrl+Z)"), [this] { emit undoRequested(); }, false));
    bar->addWidget(tool({"edit-redo"}, {}, tr("Rehacer (Ctrl+Y)"), [this] { emit redoRequested(); }, false));
    separator();
    bar->addWidget(tool({"edit-copy"}, {}, tr("Copiar los fotogramas elegidos (Ctrl+C)"), [this] { copyFrames(); }, true));
    bar->addWidget(tool({"edit-paste"}, {}, tr("Pegar fotogramas o una imagen detrás de los elegidos (Ctrl+V)"),
                        [this] { pasteFrames(); }, false));
    bar->addWidget(tool({"edit-copy", "edit-duplicate"}, tr("Duplicar"), tr("Duplica los fotogramas elegidos (Ctrl+D)"),
                        [this] { duplicateFrames(selectedFrames()); }, true));
    bar->addWidget(tool({"edit-delete", "list-remove"}, tr("Eliminar"), tr("Quita los fotogramas elegidos (Supr)"),
                        [this] { removeFrames(selectedFrames()); }, true));
    bar->addWidget(tool({"view-hidden", "visibility"}, {}, tr("Ocultar o mostrar el fotograma (los ocultos no van en el GIF)"),
                        [this] { toggleHidden(selectedFrame()); }, true));
    bar->addWidget(tool({"draw-brush", "edit-image"}, {}, tr("Editar el fotograma con las herramientas"), [this] {
        if (selectedFrame() >= 0)
            emit editFrameRequested(selectedFrame());
    }, true));
    separator();
    // El punto clave global (todos los valores en el cabezal), como en Filmora.
    {
        QPixmap pm(QSize(20, 20) * devicePixelRatioF());
        pm.setDevicePixelRatio(devicePixelRatioF());
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paintDiamond(p, QPointF(10, 10), 7, true, true, palette());
        p.end();
        QToolButton* key = tool({}, tr("Punto clave"),
                                tr("Punto clave global: todos los valores (posición, escala, rotación y opacidad) en el "
                                   "cabezal; si ya hay uno ahí, lo quita (K)"),
                                [this] { toggleGlobalKey(); }, true);
        key->setIcon(QIcon(pm));
        bar->addWidget(key);
    }
    separator();
    auto* durationLabel = new QLabel(tr("Duración:"), timelineBox);
    m_delay = new QSpinBox(timelineBox);
    m_delay->setRange(Gif::kMinDelayMs, 60000);
    m_delay->setSingleStep(10);
    m_delay->setSuffix(tr(" ms"));
    m_delay->setMinimumHeight(30);
    m_delay->setToolTip(tr("Cuánto se ve el fotograma elegido"));
    m_frameButtons << m_delay;
    connect(m_delay, &QSpinBox::editingFinished, this, [this] {
        if (selectedFrame() >= 0 && m_delay->value() != frameDelay(selectedFrame()))
            setFrameDelay(selectedFrame(), m_delay->value());
    });
    bar->addWidget(durationLabel);
    bar->addWidget(m_delay);
    bar->addWidget(tool({"chronometer", "clock"}, tr("A todos"), tr("La misma duración para todos los fotogramas"),
                        [this] { setAllDelays(m_delay->value()); }, true));
    separator();
    bar->addWidget(tool({"media-seek-backward", "go-previous"}, tr("Invertir"),
                        tr("El último fotograma de la pista elegida pasa a ser el primero"), [this] { reverseFrames(); }, false));
    bar->addWidget(tool({"media-playlist-repeat", "view-refresh"}, tr("Ida y vuelta"),
                        tr("Añade los fotogramas de la pista elegida al revés al final: la animación va y vuelve"),
                        [this] { boomerang(); }, false));
    bar->addWidget(tool({"list-add"}, tr("Pista"), tr("Añade una pista encima de las demás (lo de las pistas de arriba se ve "
                                                     "encima)"),
                        [this] { addTrack(); }, false));
    bar->addWidget(tool({"audio-x-generic", "media-playback-start"}, tr("Audio"),
                        tr("Añade un archivo de audio (MP3, WAV, OGG, FLAC...) para los vídeos exportados. También se puede "
                           "arrastrar a la línea de tiempo."),
                        [this] { chooseAudioFiles(); }, false));
    bar->addStretch(1);
    bar->addWidget(new QLabel(tr("Zum:"), timelineBox));
    m_zoom = new QSlider(Qt::Horizontal, timelineBox);
    m_zoom->setRange(40, 3000);
    m_zoom->setValue(600);
    m_zoom->setFixedWidth(160);
    m_zoom->setToolTip(tr("Zum de la línea de tiempo (también con Ctrl y la rueda)"));
    bar->addWidget(m_zoom);
    timelineLayout->addLayout(bar);

    m_timelineScroll = new QScrollArea(timelineBox);
    m_timelineScroll->setWidgetResizable(false);
    m_timelineScroll->setFrameShape(QFrame::NoFrame);
    m_timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timeline = new Timeline(this, m_timelineScroll);
    m_timelineScroll->setWidget(m_timeline);
    // Una pista de fotogramas y una de audio a la vista.
    m_timelineScroll->setMinimumHeight(Timeline::heightFor(1) + kAudioGap + kAudioHeight + kTrackGap + 24);
    // La cabecera de las pistas se queda a la izquierda al desplazarse: se vuelve a dibujar ahí.
    connect(m_timelineScroll->horizontalScrollBar(), &QScrollBar::valueChanged, m_timeline, [this] { m_timeline->update(); });
    // Y la regla, arriba al bajar entre las pistas.
    connect(m_timelineScroll->verticalScrollBar(), &QScrollBar::valueChanged, m_timeline, [this] { m_timeline->update(); });
    connect(m_zoom, &QSlider::valueChanged, this, [this](int v) { m_timeline->setPixelsPerSecond(v); });
    m_timeline->onZoom = [this](int v) {
        const QSignalBlocker block(m_zoom);
        m_zoom->setValue(v);
    };
    timelineLayout->addWidget(m_timelineScroll, 1);

    vertical->addWidget(top);
    vertical->addWidget(timelineBox);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 2);
    root->addWidget(vertical, 1);

    setFocusPolicy(Qt::StrongFocus);
    updateState();
}

void GifEditor::setCheckerBrightness(int brightness)
{
    if (GifEdit::g_checkerBrightness == brightness)
        return;
    GifEdit::g_checkerBrightness = brightness;
    if (m_preview)
        m_preview->update();
    if (m_timeline)
        m_timeline->update();
}

void GifEditor::setHistoryPanel(QWidget* panel)
{
    if (!panel || !m_leftTabs)
        return;
    m_leftTabs->addTab(panel, Icons::theme({"view-history", "document-open-recent"}), tr("Historial"));
}

void GifEditor::setDocument(Document* doc)
{
    if (doc == m_doc)
        return;
    stop();
    if (m_doc)
        m_doc->disconnect(this);
    m_doc = doc;
    m_time = 0;
    m_extraSelection.clear();
    m_selectedAudio = -1;
    if (m_rightStack)
        m_rightStack->setCurrentIndex(0);
    invalidateStarts();
    if (m_doc) {
        auto refresh = [this] { updateState(); };
        auto relayout = [this] {
            invalidateStarts();
            // Otras capas: la selección de varios ya no vale.
            m_extraSelection.removeIf([this](int i) { return i >= frameCount(); });
            updateState();
        };
        connect(m_doc, &Document::layersChanged, this, relayout);
        connect(m_doc, &Document::layerPixelsChanged, this, refresh);
        connect(m_doc, &Document::pixelsChanged, this, refresh);
        connect(m_doc, &Document::activeLayerChanged, this, [this] {
            // Al elegir otra capa (fotograma), su pista pasa a ser la elegida y el cabezal va a su
            // principio (si no se reproduce).
            const int i = selectedFrame();
            if (i >= 0)
                m_activeTrack = trackOf(i);
            if (!m_playing && i >= 0 && frameAt(m_time) != i)
                m_time = frameStart(i);
            updateState();
        });
        connect(m_doc, &Document::sizeChanged, this, relayout);
        connect(m_doc, &Document::audioChanged, this, [this] {
            if (m_selectedAudio >= audioClips().size())
                selectAudio(-1);
            m_timeline->refresh();
            if (m_audioInspector && m_selectedAudio >= 0)
                m_audioInspector->refresh();
        });
        connect(m_doc, &QObject::destroyed, this, [this] { setDocument(nullptr); });
        // El cabezal, al principio del fotograma elegido (la capa activa), y su pista, la elegida.
        const int i = selectedFrame();
        m_activeTrack = trackOf(i);
        if (i >= 0)
            m_time = frameStart(i);
    }
    rebuildMedia();
    updateState();
}

void GifEditor::setOpenDocuments(const QList<Document*>& docs)
{
    m_openDocs.clear();
    for (Document* d : docs)
        m_openDocs << d;
    rebuildMedia();
}

void GifEditor::rebuildMedia()
{
    if (!m_mediaList)
        return;
    m_media.clear();
    for (const QPointer<Document>& d : std::as_const(m_openDocs)) {
        if (!d || d == m_doc)
            continue;
        Media m;
        m.name = d->displayName();
        m.document = d;
        m.thumbnail = thumbnail(d->flattened(), QSize(84, 64));
        m_media << m;
    }
    m_media << m_imported;
    m_mediaList->clear();
    for (int i = 0; i < m_media.size(); ++i) {
        const Media& m = m_media[i];
        QPixmap icon(QSize(84, 64));
        icon.fill(Qt::transparent);
        {
            QPainter p(&icon);
            const QRect r(QPoint((84 - m.thumbnail.width()) / 2, (64 - m.thumbnail.height()) / 2), m.thumbnail.size());
            paintChecker(p, r);
            p.drawImage(r, m.thumbnail);
        }
        QString label = m.name;
        if (m.document)
            label = tr("%1 (abierta)").arg(m.name);
        else if (m.frames.size() > 1)
            label = tr("%1 (%n fotogramas)", nullptr, int(m.frames.size())).arg(m.name);
        auto* item = new QListWidgetItem(QIcon(icon), label, m_mediaList);
        item->setData(Qt::UserRole, i);
        item->setToolTip(label);
    }
}

bool GifEditor::loadFile(const QString& path, Media* media, QString* error) const
{
    media->name = QFileInfo(path).fileName();
    media->frames.clear();
    media->delays.clear();
    if (GifExport::isVideoFile(path)) {
        // Vídeo (con FFmpeg): sus fotogramas.
        int delay = 100;
        QList<QImage> frames;
        if (GifExport::decodeVideo(path, &frames, &delay, const_cast<GifEditor*>(this), error))
            for (const QImage& f : std::as_const(frames)) {
                media->frames << f.convertToFormat(kLayerFormat);
                media->delays << delay;
            }
    } else if (ImageIO::isAnimation(path)) {
        const Gif::Animation anim = Gif::read(path, error);
        for (const Gif::Frame& f : anim.frames) {
            media->frames << f.image;
            media->delays << std::max(Gif::kMinDelayMs, f.delayMs);
        }
    } else {
        // WebP o APNG animados, con los lectores de Qt; el resto, una imagen.
        QImageReader reader(path);
        if (reader.supportsAnimation() && reader.imageCount() > 1) {
            QImage frame;
            while (reader.read(&frame)) {
                media->frames << frame.convertToFormat(kLayerFormat);
                media->delays << std::max(Gif::kMinDelayMs, reader.nextImageDelay() > 0 ? reader.nextImageDelay() : 100);
            }
        }
        if (media->frames.isEmpty()) {
            const QImage image = ImageIO::readImage(path, error);
            if (!image.isNull()) {
                media->frames << image;
                media->delays << 100;
            }
        }
    }
    if (media->frames.isEmpty())
        return false;
    media->thumbnail = thumbnail(media->frames.first(), QSize(84, 64));
    return true;
}

void GifEditor::importFiles(const QStringList& paths)
{
    QStringList failed;
    for (const QString& path : paths) {
        Media m;
        QString error;
        if (loadFile(path, &m, &error))
            m_imported << m;
        else
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error.isEmpty() ? tr("no es una imagen") : error);
    }
    rebuildMedia();
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Importar"), tr("No se pudieron importar:\n%1").arg(failed.join(QLatin1Char('\n'))));
}

int GifEditor::frameCount() const
{
    return m_doc ? m_doc->layerCount() : 0;
}

int GifEditor::selectedFrame() const
{
    return m_doc && m_doc->layerCount() > 0 ? m_doc->activeIndex() : -1;
}

void GifEditor::selectFrame(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    m_extraSelection.clear();
    if (index != m_doc->activeIndex())
        m_selectedKey = -1;
    m_activeTrack = trackOf(index);
    m_doc->setActiveIndex(index);
    if (!m_playing)
        m_time = frameStart(index);
    updateState();
}

int GifEditor::frameDelay(int index) const
{
    return m_doc && index >= 0 && index < m_doc->layerCount() ? delayOf(m_doc->layer(index)) : kDefaultDelay;
}

bool GifEditor::frameHidden(int index) const
{
    return m_doc && index >= 0 && index < m_doc->layerCount() && !m_doc->layer(index)->props.visible;
}

void GifEditor::computeTimes() const
{
    // Se calcula una vez por cambio de capas (cada nombre lleva su duración): la reproducción lo
    // pide en cada paso. Cada pista empieza en 0 y sus fotogramas van uno detrás de otro.
    if (m_startsValid && m_doc && m_starts.size() == m_doc->layerCount())
        return;
    m_starts.clear();
    m_ends.clear();
    m_total = 0;
    if (m_doc) {
        QList<int> ends;
        for (int i = 0; i < m_doc->layerCount(); ++i) {
            const Layer* layer = m_doc->layer(i);
            const int k = std::max(0, layer->props.track);
            if (ends.size() <= k)
                ends.resize(k + 1, 0);
            // (Con el espacio vacío que tenga delante.)
            ends[k] += std::max(0, layer->props.gap);
            m_starts << ends[k];
            ends[k] += delayOf(layer);
            m_ends << ends[k];
            m_total = std::max(m_total, ends[k]);
        }
    }
    m_startsValid = m_doc != nullptr;
}

int GifEditor::frameStart(int index) const
{
    computeTimes();
    return index >= 0 && index < m_starts.size() ? m_starts[index] : 0;
}

int GifEditor::frameEnd(int index) const
{
    computeTimes();
    return index >= 0 && index < m_ends.size() ? m_ends[index] : 0;
}

int GifEditor::totalTime() const
{
    computeTimes();
    return m_total;
}

int GifEditor::trackOf(int index) const
{
    return m_doc && index >= 0 && index < m_doc->layerCount() ? std::max(0, m_doc->layer(index)->props.track) : 0;
}

int GifEditor::trackCount() const
{
    if (!m_doc)
        return 1;
    // Las que tienen fotogramas y las vacías que se añadieron (se recuerdan en la imagen).
    int count = std::max(1, m_doc->property("kaintGifTracks").toInt());
    for (const LayerPtr& l : m_doc->layers())
        count = std::max(count, l->props.track + 1);
    return count;
}

QList<int> GifEditor::framesOfTrack(int track) const
{
    QList<int> frames;
    for (int i = 0; m_doc && i < m_doc->layerCount(); ++i)
        if (trackOf(i) == track)
            frames << i;
    return frames;
}

int GifEditor::activeTrack() const
{
    return std::clamp(m_activeTrack, 0, trackCount() - 1);
}

void GifEditor::setActiveTrack(int track)
{
    m_activeTrack = std::clamp(track, 0, trackCount() - 1);
    // El fotograma elegido pasa a ser el de esta pista en el cabezal.
    if (m_doc && !m_playing && trackOf(selectedFrame()) != m_activeTrack) {
        const int frame = frameAt(m_time);
        if (frame >= 0) {
            m_extraSelection.clear();
            m_selectedKey = -1;
            m_doc->setActiveIndex(frame);
        }
    }
    updateState();
}

void GifEditor::addTrack()
{
    if (!m_doc)
        return;
    const int count = trackCount();
    if (count >= 16) {
        QApplication::beep();
        return;
    }
    m_doc->setProperty("kaintGifTracks", count + 1);
    m_activeTrack = count;
    updateState();
}

void GifEditor::removeTrack(int track)
{
    if (!m_doc || track < 0 || track >= trackCount() || trackCount() <= 1) {
        QApplication::beep();
        return;
    }
    QList<LayerPtr> layers;
    QList<LayerProps> props;
    int removed = 0;
    for (const LayerPtr& l : m_doc->layers()) {
        if (l->props.track == track) {
            ++removed;
            continue;
        }
        LayerProps p = l->props;
        if (p.track > track)
            --p.track;
        layers << l;
        props << p;
    }
    // Una imagen necesita al menos una capa: la única pista con fotogramas no se puede quitar.
    if (layers.isEmpty()) {
        QApplication::beep();
        return;
    }
    if (removed > 0) {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(this, tr("Eliminar la pista"),
                                  tr("¿Eliminar la pista %1 y sus %n fotograma(s)?", nullptr, removed).arg(track + 1));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_doc->setProperty("kaintGifTracks", std::max(1, m_doc->property("kaintGifTracks").toInt() - 1));
    m_activeTrack = std::max(0, track - 1);
    if (removed == 0) {
        // Vacía: no cambia la imagen (ni el historial).
        updateState();
        return;
    }
    applyFrames(layers, props, 0, tr("Eliminar la pista %1").arg(track + 1), QStringLiteral("edit-delete"));
}

bool GifEditor::trackHidden(int track) const
{
    const QList<int> frames = framesOfTrack(track);
    for (int i : frames)
        if (!frameHidden(i))
            return false;
    return !frames.isEmpty();
}

void GifEditor::toggleTrackHidden(int track)
{
    const QList<int> frames = framesOfTrack(track);
    if (!m_doc || frames.isEmpty())
        return;
    const bool show = trackHidden(track);
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    for (int i : frames)
        props[i].visible = show;
    applyFrames(m_doc->layers(), props, m_doc->activeIndex(),
                show ? tr("Mostrar la pista %1").arg(track + 1) : tr("Ocultar la pista %1").arg(track + 1),
                show ? QStringLiteral("visibility") : QStringLiteral("view-hidden"));
}

void GifEditor::invalidateStarts()
{
    m_startsValid = false;
    m_opacityCache.clear();
}

int GifEditor::frameAt(int ms) const
{
    const QList<int> frames = framesOfTrack(activeTrack());
    for (int i : frames)
        if (ms < frameEnd(i))
            return i;
    return frames.isEmpty() ? -1 : frames.last();
}

int GifEditor::frameAtTrack(int track, int ms) const
{
    for (int i : framesOfTrack(track))
        if (ms >= frameStart(i) && ms < frameEnd(i))
            return i;
    return -1;
}

void GifEditor::seek(int ms)
{
    if (frameCount() == 0) {
        m_time = 0;
        updateState();
        return;
    }
    m_time = std::clamp(ms, 0, std::max(0, totalTime() - 1));
    const int index = frameAt(m_time);
    if (m_playing) {
        showPlayingFrame(index);
        return;
    }
    if (m_doc && index >= 0 && index != m_doc->activeIndex()) {
        m_extraSelection.clear();
        m_doc->setActiveIndex(index);
    }
    updateState();
}

void GifEditor::step(int delta)
{
    stop();
    // Por los fotogramas de la pista elegida.
    const QList<int> frames = framesOfTrack(activeTrack());
    if (frames.isEmpty())
        return;
    const int count = int(frames.size());
    int index = int(frames.indexOf(frameAt(m_time))) + delta;
    if (index < 0)
        index = m_loop->isChecked() ? count - 1 : 0;
    if (index >= count)
        index = m_loop->isChecked() ? 0 : count - 1;
    seek(frameStart(frames[index]));
}

void GifEditor::togglePlay()
{
    if (m_playing) {
        stop();
        return;
    }
    if (frameCount() == 0)
        return;
    if (m_time >= totalTime() - 1)
        m_time = 0;
    m_playing = true;
    m_playFrame = frameAt(m_time);
    m_clock.start();
    m_lastTick = 0;
    m_ticker.start();
    m_play->setIcon(Icons::theme({"media-playback-pause"}));
}

void GifEditor::stop()
{
    m_ticker.stop();
    const bool wasPlaying = m_playing;
    m_playing = false;
    syncAudio();
    if (m_play)
        m_play->setIcon(Icons::theme({"media-playback-start"}));
    // Al parar, el fotograma del cabezal pasa a ser el elegido (la capa activa).
    const int frame = m_playFrame;
    m_playFrame = -1;
    if (wasPlaying && m_doc && frame >= 0 && frame < m_doc->layerCount()) {
        m_extraSelection.clear();
        if (frame != m_doc->activeIndex())
            m_doc->setActiveIndex(frame);
        else
            updateState();
    }
}

void GifEditor::tick()
{
    const int total = totalTime();
    if (!m_doc || frameCount() == 0 || total <= 0) {
        stop();
        return;
    }
    const qint64 now = m_clock.elapsed();
    const double speed = m_speed->currentData().toDouble();
    m_time += int(std::round((now - m_lastTick) * speed));
    m_lastTick = now;
    if (m_time >= total) {
        if (!m_loop->isChecked()) {
            m_time = total - 1;
            stop();
        } else {
            m_time %= std::max(1, total);
        }
    }
    // Con una sola pista, los fotogramas ocultos no se ven (tampoco van en el GIF): se saltan. Con
    // varias, el tiempo sigue (las otras pistas pueden tener algo).
    int index = frameAt(m_time);
    if (trackCount() == 1) {
        const QList<int> frames = framesOfTrack(0);
        const int count = int(frames.size());
        int at = int(frames.indexOf(index));
        for (int tries = 0; at >= 0 && tries < count && frameHidden(frames[at]); ++tries) {
            at = (at + 1) % count;
            index = frames[at];
            m_time = frameStart(index);
        }
    }
    showPlayingFrame(index);
    syncAudio();
}

void GifEditor::showPlayingFrame(int index)
{
    // Solo lo que cambia: la imagen (si es otro fotograma), el cabezal y el tiempo. Sin tocar la
    // capa activa (eso movía el panel de capas, la ventana y el editor en cada fotograma).
    // (También al entrar en el fotograma desde un hueco de delante, o al revés.)
    const bool gap = inGap(index);
    if (index != m_playFrame || gap != m_playGap || trackCount() > 1
        || (m_doc && index >= 0 && index < m_doc->layerCount() && m_doc->layer(index)->props.motion)) {
        m_playFrame = index;
        m_playGap = gap;
        m_preview->setImage(shownImage());
    }
    if (index == selectedFrame())
        m_inspector->refreshValues();
    updateTimeLabel();
    // (Sin desplazar la línea de tiempo: reproducir no mueve lo que se está mirando.)
    m_timeline->update();
}

int GifEditor::shownFrame() const
{
    return m_playing && m_playFrame >= 0 ? m_playFrame : selectedFrame();
}

bool GifEditor::inGap(int index) const
{
    return m_doc && index >= 0 && index < m_doc->layerCount() && frameAtTrack(trackOf(index), m_time) < 0;
}

QImage GifEditor::shownImage() const
{
    if (trackCount() <= 1) {
        // En un hueco de la pista (el fotograma empieza más adelante), nada, como en Filmora.
        const int frame = shownFrame();
        if (inGap(frame)) {
            QImage empty(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
            empty.fill(Qt::transparent);
            return empty;
        }
        return frameImage(frame);
    }
    // Las pistas, de la de abajo (la 1) a la de arriba, con lo que toca en el cabezal.
    QImage canvas(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    for (int k = 0; k < trackCount(); ++k) {
        const int frame = frameAtTrack(k, m_time);
        if (frame >= 0 && !frameHidden(frame))
            p.drawImage(0, 0, frameImageAt(frame, m_time));
    }
    p.end();
    return canvas;
}

QImage GifEditor::frameImage(int index) const
{
    return frameImageAt(index, m_time);
}

QImage GifEditor::frameImageAt(int index, int time) const
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return {};
    const Layer* layer = m_doc->layer(index);
    // Con su animación (en el tiempo del cabezal), como en el GIF exportado.
    const Motion::Clip clip = motion(index);
    if (!clip.isEmpty() || (index == selectedFrame() && !m_dragOffset.isNull())) {
        const int duration = frameEnd(index) - frameStart(index);
        int t = std::clamp(time - frameStart(index), 0, std::max(0, duration - 1));
        if (clip.animated())
            t = t / std::max(10, clip.stepMs) * std::max(10, clip.stepMs);
        Motion::Transform tr = clip.sample(t);
        if (index == selectedFrame()) {
            tr.x += m_dragOffset.x();
            tr.y += m_dragOffset.y();
        }
        const QString key = QStringLiteral("%1:%2:%3:%4:%5:%6:%7").arg(layer->image.cacheKey()).arg(layer->props.opacity)
                                .arg(tr.x).arg(tr.y).arg(tr.scaleX).arg(tr.scaleY).arg(tr.rotation) + QString::number(tr.opacity);
        if (key == m_motionKey)
            return m_motionImage;
        QImage base = layer->image;
        if (layer->props.opacity < 255) {
            base = QImage(layer->image.size(), kLayerFormat);
            base.fill(kTransparent);
            QPainter p(&base);
            p.setOpacity(layer->props.opacity / 255.0);
            p.drawImage(0, 0, layer->image);
        }
        m_motionKey = key;
        m_motionImage = Motion::render(base, tr);
        return m_motionImage;
    }
    // Con la opacidad de la capa, como en el GIF exportado.
    if (layer->props.opacity >= 255)
        return layer->image;
    const qint64 key = layer->image.cacheKey() ^ (qint64(layer->props.opacity) << 56);
    auto it = m_opacityCache.constFind(key);
    if (it != m_opacityCache.constEnd())
        return *it;
    QImage out(layer->image.size(), kLayerFormat);
    out.fill(kTransparent);
    QPainter p(&out);
    p.setOpacity(layer->props.opacity / 255.0);
    p.drawImage(0, 0, layer->image);
    p.end();
    if (m_opacityCache.size() > 64)
        m_opacityCache.clear();
    m_opacityCache.insert(key, out);
    return out;
}

void GifEditor::updateState()
{
    const bool has = m_doc && m_doc->layerCount() > 0;
    m_title->setText(m_doc ? tr("Editor de GIF: %1").arg(m_doc->displayName()) : tr("Editor de GIF"));
    const int index = selectedFrame();
    m_preview->setImage(has ? shownImage() : QImage());
    for (QWidget* w : std::as_const(m_frameButtons))
        w->setEnabled(has && index >= 0);
    if (has && index >= 0 && !m_delay->hasFocus()) {
        const QSignalBlocker block(m_delay);
        m_delay->setValue(frameDelay(index));
    }
    updateTimeLabel();
    m_timeline->refresh();
    if (m_inspector)
        m_inspector->refresh();
    if (m_audioInspector && m_selectedAudio >= 0)
        m_audioInspector->refresh();
    updateMotionPath();
    m_sizeTimer.start();
}

void GifEditor::estimateSize()
{
    if (!m_doc || !isVisible() || !m_sizeMeter)
        return;
    // Solo si algo cambió (otro paso del historial, otro tamaño u otras duraciones).
    QString key = QStringLiteral("%1:%2x%3:").arg(m_doc->undoStack()->index()).arg(m_doc->size().width()).arg(m_doc->size().height());
    for (const LayerPtr& l : m_doc->layers())
        key += l->props.name + QLatin1Char(l->props.visible ? '1' : '0');
    if (key == m_sizeKey)
        return;
    m_sizeKey = key;
    // Los fotogramas que tendrá (con los de los puntos clave, sin dibujarlos) y unos de muestra.
    QList<const Layer*> shown;
    int frames = 0, duration = 0;
    for (const LayerPtr& l : m_doc->layers()) {
        if (!l->props.visible)
            continue;
        const int delay = GifEdit::delayOf(l.get());
        duration += delay;
        const auto& m = l->props.motion;
        frames += m && m->animated() ? std::max(1, (delay + std::max(10, m->stepMs) - 1) / std::max(10, m->stepMs)) : 1;
        shown << l.get();
    }
    if (shown.isEmpty()) {
        m_sizeMeter->setEstimate(0, 0, 0);
        return;
    }
    const int samples = std::min<int>(4, shown.size());
    qint64 sampled = 0;
    for (int i = 0; i < samples; ++i) {
        const Layer* layer = shown[int(qint64(i) * shown.size() / samples)];
        QImage image = layer->image;
        // Las grandes, reducidas (el peso crece con el área).
        double factor = 1;
        const qint64 area = qint64(image.width()) * image.height();
        if (area > 300000) {
            factor = double(area) / 300000;
            image = image.scaled(image.size() / std::sqrt(factor), Qt::IgnoreAspectRatio, Qt::FastTransformation);
        }
        Gif::Frame f;
        f.image = image;
        QString error;
        sampled += qint64(Gif::encode({f}, Gif::Options(), &error).size() * factor);
    }
    m_sizeMeter->setEstimate(800 + sampled / samples * frames, duration, frames);
}

void GifEditor::updateTimeLabel()
{
    const int index = shownFrame();
    QString text = QStringLiteral("%1 / %2").arg(timeText(m_time), timeText(totalTime()));
    if (index >= 0)
        text += QStringLiteral("   ") + tr("Fotograma %1 de %2").arg(index + 1).arg(frameCount());
    m_timeLabel->setText(text);
}

void GifEditor::applyFrames(const QList<LayerPtr>& layers, const QList<LayerProps>& props, int active, const QString& text,
                            const QString& icon)
{
    if (!m_doc)
        return;
    stop();
    m_extraSelection.clear();
    invalidateStarts();
    const DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    after.layers = layers;
    after.images.clear();
    for (const LayerPtr& l : layers)
        after.images << l->image;
    after.props = props;
    after.active = std::clamp(active, 0, std::max(0, int(layers.size()) - 1));
    HistoryIcons::registerIcon(text, icon);
    // Como paso del editor de GIF (su historial lo muestra; el del editor de imágenes, no).
    auto* step = new GifEditCommand(text);
    new DocumentStateCommand(m_doc, before, after, text, step);
    m_doc->undoStack()->push(step);
    updateState();
}

void GifEditor::insertFrames(int at, const QList<QImage>& images, const QList<int>& delays, const QString& text, int track)
{
    if (!m_doc || images.isEmpty())
        return;
    if (track < 0)
        track = activeTrack();
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    if (at < 0 || at > layers.size())
        at = int(layers.size());
    for (int i = 0; i < images.size(); ++i) {
        const QString name = Gif::nameWithDelay(tr("Fotograma %1").arg(at + i + 1), delays.value(i, kDefaultDelay));
        auto layer = std::make_shared<Layer>(name, fitted(images[i], m_doc->size()));
        layer->props.track = track;
        layers.insert(at + i, layer);
        props.insert(at + i, layer->props);
    }
    applyFrames(layers, props, at, text, QStringLiteral("list-add"));
}

void GifEditor::insertMedia(int mediaIndex, int at, int track)
{
    if (mediaIndex < 0 || mediaIndex >= m_media.size())
        return;
    const Media& m = m_media[mediaIndex];
    QList<QImage> frames = m.frames;
    QList<int> delays = m.delays;
    if (m.document)
        framesOf(m.document, &frames, &delays);
    insertFrames(at, frames, delays, tr("Añadir «%1»").arg(m.name), track);
}

void GifEditor::insertFiles(const QStringList& paths, int at, int track)
{
    QList<QImage> frames;
    QList<int> delays;
    QStringList names;
    for (const QString& path : paths) {
        Media m;
        QString error;
        if (!loadFile(path, &m, &error))
            continue;
        frames << m.frames;
        delays << m.delays;
        names << m.name;
        // También quedan en la biblioteca, para volver a usarlos.
        m_imported << m;
    }
    rebuildMedia();
    if (!frames.isEmpty())
        insertFrames(at, frames, delays, names.size() == 1 ? tr("Añadir «%1»").arg(names.first()) : tr("Añadir imágenes"),
                     track);
}

void GifEditor::moveFrame(int from, int to, int track)
{
    if (!m_doc || from < 0 || from >= m_doc->layerCount() || to < 0 || to >= m_doc->layerCount())
        return;
    const int target = track < 0 ? trackOf(from) : std::min(track, trackCount() - 1);
    const bool otherTrack = target != trackOf(from);
    if (from == to && !otherTrack)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    props[from].track = target;
    layers.move(from, to);
    props.move(from, to);
    m_activeTrack = target;
    applyFrames(layers, props, to, otherTrack ? tr("Mover a la pista %1").arg(target + 1) : tr("Mover fotograma"),
                QStringLiteral("transform-move"));
}

void GifEditor::placeFrame(int from, int track, int startMs)
{
    const int count = frameCount();
    if (!m_doc || from < 0 || from >= count)
        return;
    track = std::clamp(track, 0, trackCount() - 1);
    startMs = std::max(0, int(std::round(startMs / 10.0)) * 10);
    if (track == trackOf(from) && startMs == frameStart(from))
        return;
    // Dónde empieza cada fotograma (en su pista) y cuánto dura, antes del cambio.
    QList<int> start(count), duration(count), trackOfFrame(count);
    for (int i = 0; i < count; ++i) {
        start[i] = frameStart(i);
        duration[i] = frameEnd(i) - frameStart(i);
        trackOfFrame[i] = trackOf(i);
    }
    trackOfFrame[from] = track;
    // En la pista de destino, por orden de tiempo. Si cae dentro de otro clip, va delante o detrás
    // según en qué mitad; lo que se solaparía se corre hacia la derecha.
    QList<int> order;
    for (int i = 0; i < count; ++i)
        if (i != from && trackOfFrame[i] == track)
            order << i;
    double key = startMs;
    for (int i : std::as_const(order))
        if (startMs >= start[i] && startMs < start[i] + duration[i])
            key = startMs < start[i] + duration[i] / 2.0 ? start[i] - 0.5 : start[i] + duration[i] - 0.5;
    int at = 0;
    while (at < order.size() && start[order[at]] <= key)
        ++at;
    order.insert(at, from);
    start[from] = startMs;
    int cursor = 0;
    for (int i : std::as_const(order)) {
        start[i] = std::max(start[i], cursor);
        cursor = start[i] + duration[i];
    }
    // La lista de capas: el fotograma, detrás del que le precede en la pista (o delante del
    // siguiente); los demás, en su sitio.
    QList<LayerPtr> layers = m_doc->layers();
    const LayerPtr moved = layers.takeAt(from);
    int index = int(layers.size());
    if (at > 0)
        index = int(layers.indexOf(m_doc->layers()[order[at - 1]])) + 1;
    else if (at + 1 < order.size())
        index = int(layers.indexOf(m_doc->layers()[order[at + 1]]));
    layers.insert(index, moved);
    // Los espacios vacíos que salen de las posiciones nuevas (en todas las pistas: en la de origen,
    // los de detrás no se mueven).
    QHash<const Layer*, int> startOf, trackOfLayer;
    for (int i = 0; i < count; ++i) {
        startOf.insert(m_doc->layers()[i].get(), start[i]);
        trackOfLayer.insert(m_doc->layers()[i].get(), trackOfFrame[i]);
    }
    QList<LayerProps> props;
    QHash<int, int> ends;
    for (const LayerPtr& l : std::as_const(layers)) {
        LayerProps p = l->props;
        p.track = trackOfLayer.value(l.get());
        const int s = startOf.value(l.get());
        p.gap = std::max(0, s - ends.value(p.track));
        ends[p.track] = s + delayOf(l.get());
        props << p;
    }
    m_activeTrack = track;
    applyFrames(layers, props, index, track != trackOf(from) ? tr("Mover a la pista %1").arg(track + 1) : tr("Mover fotograma"),
                QStringLiteral("transform-move"));
}

void GifEditor::setFrameDelay(int index, int delayMs)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount() || delayMs == frameDelay(index))
        return;
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    props[index].name = Gif::nameWithDelay(props[index].name, std::max(Gif::kMinDelayMs, delayMs));
    applyFrames(m_doc->layers(), props, index, tr("Duración del fotograma"), QStringLiteral("chronometer"));
}

void GifEditor::setAllDelays(int delayMs)
{
    if (!m_doc || m_doc->layerCount() == 0)
        return;
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers()) {
        LayerProps p = l->props;
        p.name = Gif::nameWithDelay(p.name, std::max(Gif::kMinDelayMs, delayMs));
        props << p;
    }
    applyFrames(m_doc->layers(), props, m_doc->activeIndex(), tr("Duración de todos los fotogramas"), QStringLiteral("chronometer"));
}

void GifEditor::removeFrame(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    // Una imagen necesita al menos una capa.
    if (m_doc->layerCount() == 1) {
        QApplication::beep();
        return;
    }
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    layers.removeAt(index);
    props.removeAt(index);
    applyFrames(layers, props, std::min(index, int(layers.size()) - 1), tr("Eliminar fotograma"), QStringLiteral("edit-delete"));
}

void GifEditor::duplicateFrame(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    auto copy = std::make_shared<Layer>(layers[index]->props.name, layers[index]->image);
    copy->props = layers[index]->props;
    layers.insert(index + 1, copy);
    props.insert(index + 1, copy->props);
    applyFrames(layers, props, index + 1, tr("Duplicar fotograma"), QStringLiteral("edit-copy"));
}

void GifEditor::setMotionPreview(int index, const Motion::Clip& clip)
{
    m_previewIndex = index;
    m_previewClip = clip;
    m_preview->setImage(shownImage());
    updateMotionPath();
    m_inspector->refreshValues();
    m_timeline->update();
}

void GifEditor::clearMotionPreview()
{
    m_previewIndex = -1;
}

Motion::Clip GifEditor::motion(int index) const
{
    if (index >= 0 && index == m_previewIndex)
        return m_previewClip;
    if (!m_doc || index < 0 || index >= m_doc->layerCount() || !m_doc->layer(index)->props.motion)
        return Motion::Clip();
    return *m_doc->layer(index)->props.motion;
}

void GifEditor::setMotion(int index, const Motion::Clip& clip, const QString& text)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    const Motion::Clip before = motion(index);
    if (before == clip)
        return;
    const int time = m_time;
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    props[index].motion = clip.isEmpty() ? nullptr : std::make_shared<const Motion::Clip>(clip);
    applyFrames(m_doc->layers(), props, index, text, QStringLiteral("chronometer"));
    // El cabezal se queda donde estaba (dentro del fotograma).
    m_time = time;
    updateState();
}

int GifEditor::localTime() const
{
    const int index = selectedFrame();
    if (index < 0)
        return 0;
    return std::clamp(m_time - frameStart(index), 0, std::max(0, frameEnd(index) - frameStart(index) - 1));
}

void GifEditor::seekLocal(int ms)
{
    const int index = selectedFrame();
    if (index < 0)
        return;
    stop();
    m_time = frameStart(index) + std::clamp(ms, 0, std::max(0, frameEnd(index) - frameStart(index) - 1));
    updateState();
}

void GifEditor::selectKey(int index, int ms)
{
    if (index != selectedFrame())
        selectFrame(index);
    selectKeyTime(ms);
}

void GifEditor::addGlobalKey(int index, int ms)
{
    if (!m_doc || index < 0 || index >= frameCount())
        return;
    stop();
    if (index != selectedFrame())
        selectFrame(index);
    ms = std::clamp(ms, 0, frameDelay(index));
    Motion::Clip clip = motion(index);
    if (!clip.keyTimes().contains(ms)) {
        // Todos los valores tal como están en ese momento.
        for (Motion::Track& track : clip.tracks)
            track.setKey(ms, track.value(ms));
        setMotion(index, clip, tr("Punto clave global"));
    }
    selectKeyTime(ms);
}

bool GifEditor::hasKeyAtPlayhead() const
{
    const int index = selectedFrame();
    return index >= 0 && motion(index).keyTimes().contains(localTime());
}

void GifEditor::toggleGlobalKey()
{
    const int index = selectedFrame();
    if (!m_doc || index < 0)
        return;
    stop();
    Motion::Clip clip = motion(index);
    const int t = localTime();
    if (clip.keyTimes().contains(t)) {
        clip.removeKeys(t);
        m_selectedKey = -1;
        setMotion(index, clip, tr("Quitar el punto clave global"));
        return;
    }
    // Todos los valores tal como están en el cabezal.
    for (Motion::Track& track : clip.tracks)
        track.setKey(t, track.value(t));
    m_selectedKey = t;
    setMotion(index, clip, tr("Punto clave global"));
}

void GifEditor::selectKeyTime(int ms)
{
    m_selectedKey = ms;
    seekLocal(ms);
}

void GifEditor::moveKeyTime(int from, int to)
{
    const int index = selectedFrame();
    if (!m_doc || index < 0)
        return;
    Motion::Clip clip = motion(index);
    to = std::max(0, to);
    clip.moveKeys(from, to);
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    props[index].motion = clip.isEmpty() ? nullptr : std::make_shared<const Motion::Clip>(clip);
    // Un punto más allá del final: el fotograma dura hasta él (y un poco más, para que se vea).
    if (to >= frameDelay(index))
        props[index].name = Gif::nameWithDelay(props[index].name, to + 10);
    const int start = frameStart(index);
    applyFrames(m_doc->layers(), props, index, tr("Mover punto clave"), QStringLiteral("transform-move"));
    m_selectedKey = to;
    m_time = start + to;
    updateState();
}

void GifEditor::removeKeyTime(int ms)
{
    const int index = selectedFrame();
    Motion::Clip clip = motion(index);
    clip.removeKeys(ms);
    m_selectedKey = -1;
    setMotion(index, clip, tr("Quitar punto clave"));
}

void GifEditor::showKeyMenu(int ms, const QPoint& globalPos)
{
    const int index = selectedFrame();
    const Motion::Clip clip = motion(index);
    // La forma del primer punto que haya en ese tiempo (se aplica a todos los de ese tiempo).
    Motion::Interp interp = Motion::Interp::Linear;
    bool easeIn = false, easeOut = false;
    for (const Motion::Track& track : clip.tracks)
        if (const int i = track.keyAt(ms); i >= 0) {
            interp = track.keys[i].interp;
            easeIn = track.keys[i].easeIn;
            easeOut = track.keys[i].easeOut;
            break;
        }
    QMenu menu(this);
    fillInterpMenu(
        &menu, interp, easeIn, easeOut,
        [this, index, ms](Motion::Interp kind) {
            Motion::Clip c = motion(index);
            c.setInterp(ms, kind);
            setMotion(index, c, tr("Forma del movimiento"));
        },
        [this, index, ms](bool in, bool on) {
            Motion::Clip c = motion(index);
            c.setEase(ms, in, on);
            setMotion(index, c, tr("Forma del movimiento"));
        });
    menu.addSeparator();
    menu.addAction(Icons::theme({"edit-delete"}), tr("Quitar el punto clave"), QKeySequence::Delete, this,
                   [this, ms] { removeKeyTime(ms); });
    menu.exec(globalPos);
}

void GifEditor::bakeMotion(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    const Motion::Clip clip = motion(index);
    if (clip.isEmpty())
        return;
    const auto parts = Motion::expand(m_doc->layer(index)->image, clip, frameDelay(index));
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    const LayerProps original = props[index];
    layers.removeAt(index);
    props.removeAt(index);
    for (int i = 0; i < parts.size(); ++i) {
        LayerProps p = original;
        p.motion = nullptr;
        p.name = Gif::nameWithDelay(tr("Fotograma %1").arg(index + i + 1), parts[i].second);
        auto layer = std::make_shared<Layer>(p.name, parts[i].first);
        layer->props = p;
        layers.insert(index + i, layer);
        props.insert(index + i, p);
    }
    m_selectedKey = -1;
    applyFrames(layers, props, index, tr("Convertir la animación en fotogramas"), QStringLiteral("insert-image"));
}

void GifEditor::previewDragged(QPointF delta, bool finished)
{
    const int index = selectedFrame();
    // (En un hueco no se ve: no se mueve a ciegas.)
    if (!m_doc || index < 0 || inGap(index))
        return;
    stop();
    if (!finished) {
        m_dragOffset = delta;
        m_preview->setImage(shownImage());
        return;
    }
    m_dragOffset = QPointF();
    if (std::abs(delta.x()) < 0.5 && std::abs(delta.y()) < 0.5) {
        m_preview->setImage(shownImage());
        return;
    }
    Motion::Clip clip = motion(index);
    const int t = localTime();
    for (const auto& [prop, d] : {std::pair{Motion::PosX, delta.x()}, std::pair{Motion::PosY, delta.y()}}) {
        Motion::Track& track = clip.tracks[prop];
        const double v = std::round(track.value(t) + d);
        if (track.animated())
            track.setKey(t, v);
        else
            track.base = v;
    }
    setMotion(index, clip, tr("Mover la imagen"));
}

void GifEditor::updateMotionPath()
{
    const int index = selectedFrame();
    const Motion::Clip clip = motion(index);
    QList<QPointF> path, keys;
    if (m_doc && index >= 0 && (clip.tracks[Motion::PosX].keys.size() > 1 || clip.tracks[Motion::PosY].keys.size() > 1)) {
        const QPointF center(m_doc->size().width() / 2.0, m_doc->size().height() / 2.0);
        const int duration = frameDelay(index);
        for (int t = 0; t <= duration; t += 10) {
            const Motion::Transform tr = clip.sample(t);
            path << center + QPointF(tr.x, tr.y);
        }
        for (int t : clip.keyTimes()) {
            const Motion::Transform tr = clip.sample(t);
            keys << center + QPointF(tr.x, tr.y);
        }
    }
    m_preview->setPath(path, keys);
}

QList<int> GifEditor::selectedFrames() const
{
    QList<int> list;
    const int active = selectedFrame();
    if (active >= 0)
        list << active;
    for (int i : m_extraSelection)
        if (i >= 0 && i < frameCount() && !list.contains(i))
            list << i;
    std::sort(list.begin(), list.end());
    return list;
}

bool GifEditor::isFrameSelected(int index) const
{
    return index >= 0 && (index == selectedFrame() || m_extraSelection.contains(index));
}

void GifEditor::toggleFrameSelection(int index)
{
    if (!m_doc || index < 0 || index >= frameCount())
        return;
    if (index == selectedFrame()) {
        // Quitar el elegido: pasa a serlo otro de la selección (si queda alguno).
        if (m_extraSelection.isEmpty())
            return;
        const int next = m_extraSelection.takeFirst();
        m_doc->setActiveIndex(next);
    } else if (m_extraSelection.contains(index)) {
        m_extraSelection.removeAll(index);
    } else {
        m_extraSelection << index;
    }
    updateState();
}

void GifEditor::selectFrameRange(int index)
{
    if (!m_doc || index < 0 || index >= frameCount())
        return;
    const int anchor = std::max(0, selectedFrame());
    m_extraSelection.clear();
    for (int i = std::min(anchor, index); i <= std::max(anchor, index); ++i)
        if (i != anchor)
            m_extraSelection << i;
    updateState();
}

void GifEditor::selectAllFrames()
{
    m_extraSelection.clear();
    for (int i = 0; i < frameCount(); ++i)
        if (i != selectedFrame())
            m_extraSelection << i;
    updateState();
}

void GifEditor::removeFrames(const QList<int>& indexes)
{
    if (!m_doc || indexes.isEmpty())
        return;
    if (indexes.size() == 1) {
        removeFrame(indexes.first());
        return;
    }
    // Una imagen necesita al menos una capa.
    if (indexes.size() >= m_doc->layerCount()) {
        QApplication::beep();
        return;
    }
    QList<LayerPtr> layers;
    QList<LayerProps> props;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (!indexes.contains(i)) {
            layers << m_doc->layers()[i];
            props << m_doc->layers()[i]->props;
        }
    const int first = *std::min_element(indexes.begin(), indexes.end());
    applyFrames(layers, props, std::min(first, int(layers.size()) - 1), tr("Eliminar fotogramas"), QStringLiteral("edit-delete"));
}

void GifEditor::duplicateFrames(const QList<int>& indexes)
{
    if (!m_doc || indexes.isEmpty())
        return;
    if (indexes.size() == 1) {
        duplicateFrame(indexes.first());
        return;
    }
    // Las copias, juntas, detrás del último elegido.
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    const int at = *std::max_element(indexes.begin(), indexes.end()) + 1;
    int offset = 0;
    for (int i : indexes) {
        auto copy = std::make_shared<Layer>(layers[i]->props.name, layers[i]->image);
        copy->props = layers[i]->props;
        layers.insert(at + offset, copy);
        props.insert(at + offset, copy->props);
        ++offset;
    }
    applyFrames(layers, props, at, tr("Duplicar fotogramas"), QStringLiteral("edit-copy"));
    for (int i = 1; i < offset; ++i)
        m_extraSelection << at + i;
    updateState();
}

const char* GifEditor::clipboardMimeType()
{
    return "application/x-kaint-gif-frames";
}

void GifEditor::copyFrames()
{
    const QList<int> indexes = selectedFrames();
    if (!m_doc || indexes.isEmpty())
        return;
    // Los fotogramas con su nombre (la duración va en él), si se ven, su opacidad y su fusión;
    // además, el primero como imagen para pegarlo en otros programas.
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << quint32(1) << qint32(indexes.size());
    for (int i : indexes) {
        const Layer* l = m_doc->layer(i);
        out << l->image << l->props.name << l->props.visible << qint32(l->props.opacity) << qint32(l->props.blend);
    }
    auto* data = new QMimeData;
    data->setData(QString::fromLatin1(clipboardMimeType()), bytes);
    const QImage first = m_doc->layer(indexes.first())->image;
    data->setImageData(first);
    QByteArray png;
    QBuffer buffer(&png);
    if (buffer.open(QIODevice::WriteOnly) && first.save(&buffer, "PNG"))
        data->setData(QStringLiteral("image/png"), png);
    QApplication::clipboard()->setMimeData(data);
}

void GifEditor::cutFrames()
{
    const QList<int> indexes = selectedFrames();
    if (!m_doc || indexes.isEmpty())
        return;
    if (indexes.size() >= frameCount()) {
        // No se pueden quitar todos: se copian.
        copyFrames();
        QApplication::beep();
        return;
    }
    copyFrames();
    removeFrames(indexes);
}

void GifEditor::pasteFrames()
{
    if (!m_doc)
        return;
    const QMimeData* data = QApplication::clipboard()->mimeData();
    if (!data)
        return;
    const QList<int> selected = selectedFrames();
    const int at = selected.isEmpty() ? frameCount() : selected.last() + 1;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    int added = 0;
    if (data->hasFormat(QString::fromLatin1(clipboardMimeType()))) {
        QDataStream in(data->data(QString::fromLatin1(clipboardMimeType())));
        quint32 version = 0;
        qint32 count = 0;
        in >> version >> count;
        for (int i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
            QImage image;
            LayerProps p;
            qint32 opacity = 255, blend = 0;
            in >> image >> p.name >> p.visible >> opacity >> blend;
            if (in.status() != QDataStream::Ok || image.isNull())
                break;
            p.opacity = std::clamp(int(opacity), 0, 255);
            p.blend = BlendMode(blend);
            p.track = activeTrack();
            auto layer = std::make_shared<Layer>(p.name, fitted(image, m_doc->size()));
            layer->props = p;
            layers.insert(at + added, layer);
            props.insert(at + added, p);
            ++added;
        }
    } else if (data->hasImage()) {
        // Una imagen de otro programa (o de Kaint): un fotograma con la duración del elegido.
        const QImage image = qvariant_cast<QImage>(data->imageData());
        if (!image.isNull()) {
            const int delay = selected.isEmpty() ? kDefaultDelay : frameDelay(selected.last());
            const QString name = Gif::nameWithDelay(tr("Fotograma %1").arg(at + 1), delay);
            auto layer = std::make_shared<Layer>(name, fitted(image, m_doc->size()));
            layer->props.track = activeTrack();
            layers.insert(at, layer);
            props.insert(at, layer->props);
            added = 1;
        }
    }
    if (added == 0) {
        QApplication::beep();
        return;
    }
    applyFrames(layers, props, at, added == 1 ? tr("Pegar fotograma") : tr("Pegar fotogramas"), QStringLiteral("edit-paste"));
    for (int i = 1; i < added; ++i)
        m_extraSelection << at + i;
    updateState();
}

void GifEditor::toggleHidden(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    QList<LayerProps> props;
    for (const LayerPtr& l : m_doc->layers())
        props << l->props;
    props[index].visible = !props[index].visible;
    applyFrames(m_doc->layers(), props, index, props[index].visible ? tr("Mostrar fotograma") : tr("Ocultar fotograma"),
                props[index].visible ? QStringLiteral("visibility") : QStringLiteral("view-hidden"));
}

void GifEditor::reverseFrames()
{
    // Los de la pista elegida (las demás se quedan como están).
    const QList<int> frames = framesOfTrack(activeTrack());
    if (!m_doc || frames.size() < 2)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    const QList<LayerPtr> before = layers;
    for (int j = 0; j < frames.size(); ++j)
        layers[frames[j]] = before[frames[frames.size() - 1 - j]];
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    int active = m_doc->activeIndex();
    if (const int at = int(frames.indexOf(active)); at >= 0)
        active = frames[frames.size() - 1 - at];
    applyFrames(layers, props, active, tr("Invertir el orden"), QStringLiteral("media-seek-backward"));
}

void GifEditor::boomerang()
{
    // Como en la configuración avanzada del GIF: los fotogramas de la pista elegida al revés al
    // final de ella, sin repetir el último ni el primero.
    const QList<int> frames = framesOfTrack(activeTrack());
    if (!m_doc || frames.size() < 3)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    int at = frames.last() + 1;
    for (int j = int(frames.size()) - 2; j >= 1; --j) {
        const LayerPtr& source = m_doc->layers()[frames[j]];
        auto copy = std::make_shared<Layer>(source->props.name, source->image);
        copy->props = source->props;
        layers.insert(at, copy);
        props.insert(at, copy->props);
        ++at;
    }
    applyFrames(layers, props, m_doc->activeIndex(), tr("Ida y vuelta"), QStringLiteral("media-playlist-repeat"));
}

void GifEditor::editDelay(int index)
{
    if (!m_doc || index < 0 || index >= m_doc->layerCount())
        return;
    bool ok = false;
    const int value = QInputDialog::getInt(this, tr("Duración del fotograma"), tr("Duración del fotograma %1 (ms):").arg(index + 1),
                                           frameDelay(index), Gif::kMinDelayMs, 60000, 10, &ok);
    if (ok)
        setFrameDelay(index, value);
}

void GifEditor::showFrameMenu(int index, const QPoint& globalPos)
{
    QMenu menu(this);
    const QList<int> chosen = isFrameSelected(index) ? selectedFrames() : QList<int>{index};
    menu.addAction(Icons::theme({"edit-cut"}), tr("Cortar"), QKeySequence::Cut, this, [this] { cutFrames(); });
    menu.addAction(Icons::theme({"edit-copy"}), tr("Copiar"), QKeySequence::Copy, this, [this] { copyFrames(); });
    menu.addAction(Icons::theme({"edit-paste"}), tr("Pegar detrás"), QKeySequence::Paste, this, [this] { pasteFrames(); });
    menu.addSeparator();
    menu.addAction(Icons::theme({"chronometer", "clock"}), tr("Duración..."), this, [this, index] { editDelay(index); });
    menu.addAction(Icons::theme({"edit-copy"}), tr("Duplicar"), QKeySequence(Qt::CTRL | Qt::Key_D), this,
                   [this, chosen] { duplicateFrames(chosen); });
    menu.addAction(Icons::theme({"view-hidden", "visibility"}), frameHidden(index) ? tr("Mostrar") : tr("Ocultar"), this,
                   [this, index] { toggleHidden(index); });
    menu.addAction(Icons::theme({"draw-brush", "edit-image"}), tr("Editar con las herramientas"), this,
                   [this, index] { emit editFrameRequested(index); });
    menu.addSeparator();
    QAction* del = menu.addAction(Icons::theme({"edit-delete"}), tr("Eliminar"), QKeySequence::Delete, this,
                                  [this, chosen] { removeFrames(chosen); });
    del->setEnabled(frameCount() > chosen.size());
    menu.exec(globalPos);
}

// Teclado: espacio reproduce, las flechas pasan de fotograma, Inicio va al principio y Supr quita
// el fotograma. Se piden antes que los atajos de la ventana (Supr borra la selección): si no, la
// misma tecla en los dos sitios no hacía nada. Un campo de texto (la duración) se queda las suyas.
static bool isEditorKey(const QKeyEvent* e)
{
    // Ctrl+C, Ctrl+X, Ctrl+V (fotogramas, no la imagen), Ctrl+D (duplicar) y Ctrl+A (todos).
    if ((e->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier)
        return e->key() == Qt::Key_C || e->key() == Qt::Key_X || e->key() == Qt::Key_V || e->key() == Qt::Key_D
               || e->key() == Qt::Key_A;
    if (e->modifiers() & ~Qt::KeypadModifier)
        return false;
    switch (e->key()) {
    case Qt::Key_Space:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
    case Qt::Key_K:
    case Qt::Key_Delete: return true;
    default: return false;
    }
}

bool GifEditor::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride && isEditorKey(static_cast<QKeyEvent*>(e))) {
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void GifEditor::keyPressEvent(QKeyEvent* e)
{
    if (!isEditorKey(e)) {
        QWidget::keyPressEvent(e);
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        switch (e->key()) {
        case Qt::Key_C: copyFrames(); break;
        case Qt::Key_X: cutFrames(); break;
        case Qt::Key_V: pasteFrames(); break;
        case Qt::Key_D: duplicateFrames(selectedFrames()); break;
        case Qt::Key_A: selectAllFrames(); break;
        default: break;
        }
        e->accept();
        return;
    }
    switch (e->key()) {
    case Qt::Key_Space: togglePlay(); break;
    case Qt::Key_Left: step(-1); break;
    case Qt::Key_Right: step(1); break;
    case Qt::Key_Home: seek(0); break;
    case Qt::Key_K: toggleGlobalKey(); break;
    case Qt::Key_Delete:
        // Con un audio elegido, se quita; con un punto clave elegido, ese; si no, los fotogramas elegidos.
        if (m_selectedAudio >= 0)
            removeAudio(m_selectedAudio);
        else if (m_selectedKey >= 0)
            removeKeyTime(m_selectedKey);
        else
            removeFrames(selectedFrames());
        break;
    default: break;
    }
    e->accept();
}

void GifEditor::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    rebuildMedia();
    updateState();
    // La primera vez: para qué está pensado (sin la ventana en las pruebas, que van sin pantalla).
    QSettings settings;
    if (!m_introShown && !settings.value(QStringLiteral("gifEditor/introShown")).toBool()
        && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        m_introShown = true;
        settings.setValue(QStringLiteral("gifEditor/introShown"), true);
        QTimer::singleShot(0, this, [this] {
            QMessageBox box(QMessageBox::Information, tr("Editor de GIF"), tr("El editor de GIF está pensado para hacer GIF animados."),
                            QMessageBox::Ok, this);
            box.setInformativeText(tr("También exporta vídeo (MP4 o WebM) y admite pistas de audio para esos vídeos, pero no es un "
                                      "editor de vídeo completo: para vídeos largos o con mucha edición, usa un programa de "
                                      "edición de vídeo."));
            box.exec();
        });
    }
}

void GifEditor::hideEvent(QHideEvent* e)
{
    stop();
    QWidget::hideEvent(e);
}

void GifEditor::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange || e->type() == QEvent::PaletteChange)
        update();
    // Otro tema (otra hoja de estilo): la paleta del editor otra vez.
    if (e->type() == QEvent::StyleChange)
        QTimer::singleShot(0, this, &GifEditor::applyPalettes);
    QWidget::changeEvent(e);
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

QList<Audio::Clip> GifEditor::audioClips() const
{
    return m_doc ? m_doc->audioClips() : QList<Audio::Clip>();
}

int GifEditor::audioRows() const
{
    int used = 0;
    for (const Audio::Clip& c : audioClips())
        used = std::max(used, c.row + 1);
    return used + 1;
}

int GifEditor::timelineEnd() const
{
    int end = totalTime();
    for (const Audio::Clip& c : audioClips())
        end = std::max(end, c.end());
    return end;
}

void GifEditor::setAudioClips(const QList<Audio::Clip>& clips, const QString& text)
{
    if (!m_doc || m_doc->audioClips() == clips)
        return;
    HistoryIcons::registerIcon(text, QStringLiteral("audio-x-generic"));
    auto* step = new GifEditCommand(text);
    new AudioCommand(m_doc, m_doc->audioClips(), clips, text, step);
    m_doc->undoStack()->push(step);
}

void GifEditor::chooseAudioFiles()
{
    QStringList patterns;
    for (const QString& ext : Audio::fileExtensions())
        patterns << QStringLiteral("*.") + ext;
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Añadir audio"), QString(),
                                                            tr("Audio (%1)").arg(patterns.join(QLatin1Char(' '))));
    addAudioFiles(paths);
}

void GifEditor::addAudioFiles(const QStringList& paths, int startMs, int row)
{
    if (!m_doc || paths.isEmpty())
        return;
    QList<Audio::Clip> clips = audioClips();
    const int before = int(clips.size());
    const QString ffmpeg = GifExport::ffmpegPath();
    int at = std::max(0, startMs >= 0 ? startMs : m_time);
    QStringList failed;
    for (const QString& path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 512LL * 1024 * 1024) {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), file.errorString());
            continue;
        }
        Audio::Clip c;
        c.name = QFileInfo(path).completeBaseName();
        c.suffix = QFileInfo(path).suffix().toLower();
        c.data = file.readAll();
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ok = Audio::analyze(c.data, c.suffix, ffmpeg, &c.length, &c.peaks, &error);
        QApplication::restoreOverrideCursor();
        if (!ok) {
            failed << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error);
            continue;
        }
        c.duration = c.length;
        c.start = at;
        // En la pista pedida o en la primera donde no se monte con otro.
        c.row = row;
        if (c.row < 0)
            for (c.row = 0;; ++c.row) {
                const bool busy = std::any_of(clips.cbegin(), clips.cend(), [&c](const Audio::Clip& o) {
                    return o.row == c.row && o.start < c.end() && c.start < o.end();
                });
                if (!busy)
                    break;
            }
        clips << c;
        at += c.length;
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Añadir audio"), failed.join(QLatin1Char('\n')));
    if (clips.size() > before) {
        setAudioClips(clips, clips.size() - before > 1 ? tr("Añadir audios") : tr("Añadir audio"));
        selectAudio(int(clips.size()) - 1);
    }
}

void GifEditor::removeAudio(int index)
{
    QList<Audio::Clip> clips = audioClips();
    if (index < 0 || index >= clips.size())
        return;
    clips.removeAt(index);
    selectAudio(-1);
    setAudioClips(clips, tr("Quitar el audio"));
}

void GifEditor::selectAudio(int index)
{
    if (index >= audioClips().size())
        index = -1;
    m_selectedAudio = index;
    if (m_rightStack)
        m_rightStack->setCurrentIndex(index >= 0 ? 1 : 0);
    if (m_audioInspector && index >= 0)
        m_audioInspector->refresh();
    m_timeline->update();
}

void GifEditor::toggleAudioKey(int index)
{
    QList<Audio::Clip> clips = audioClips();
    if (index < 0 || index >= clips.size())
        return;
    Audio::Clip& c = clips[index];
    const int t = std::clamp(m_time - c.start, 0, c.duration);
    const int at = c.volume.keyAt(t);
    if (at >= 0) {
        c.volume.keys.removeAt(at);
        setAudioClips(clips, tr("Quitar el punto de volumen"));
    } else {
        c.volume.setKey(t, c.volume.value(t));
        setAudioClips(clips, tr("Añadir un punto de volumen"));
    }
}

void GifEditor::showAudioMenu(int index, const QPoint& globalPos)
{
    const QList<Audio::Clip> clips = audioClips();
    if (index < 0 || index >= clips.size())
        return;
    const Audio::Clip c = clips[index];
    QMenu menu(this);
    menu.addAction(Icons::theme({c.muted ? "audio-volume-high" : "audio-volume-muted"}), c.muted ? tr("Activar el sonido") : tr("Silenciar"),
                   this, [this, index] {
                       QList<Audio::Clip> all = audioClips();
                       all[index].muted = !all[index].muted;
                       setAudioClips(all, all[index].muted ? tr("Silenciar el audio") : tr("Activar el sonido"));
                   });
    QAction* key = menu.addAction(tr("Punto de volumen en el cabezal"), this, [this, index] { toggleAudioKey(index); });
    key->setEnabled(m_time >= c.start && m_time <= c.end());
    QAction* clear = menu.addAction(tr("Quitar los puntos de volumen"), this, [this, index] {
        QList<Audio::Clip> all = audioClips();
        all[index].volume.keys.clear();
        setAudioClips(all, tr("Quitar los puntos de volumen"));
    });
    clear->setEnabled(!c.volume.keys.isEmpty());
    menu.addSeparator();
    menu.addAction(Icons::theme({"edit-delete", "user-trash"}), tr("Quitar el audio"), this, [this, index] { removeAudio(index); });
    menu.exec(globalPos);
}

void GifEditor::syncAudio()
{
#ifdef KAINT_HAVE_QTMULTIMEDIA
    const QList<Audio::Clip> clips = audioClips();
    if (!m_voices) {
        if (!m_playing || clips.isEmpty())
            return;
        m_voices = std::make_unique<AudioVoices>();
    }
    // Un reproductor por clip (se rehacen si cambian los archivos).
    bool same = m_voices->voices.size() == clips.size();
    for (int i = 0; same && i < clips.size(); ++i)
        same = m_voices->voices[i].data.constData() == clips[i].data.constData() && m_voices->voices[i].data.size() == clips[i].data.size();
    if (!same) {
        for (AudioVoices::Voice& v : m_voices->voices) {
            delete v.player;
            delete v.output;
        }
        m_voices->voices.clear();
        for (const Audio::Clip& c : clips) {
            AudioVoices::Voice v;
            v.data = c.data;
            const QString path = m_voices->dir.filePath(QStringLiteral("v%1.%2").arg(++m_voices->files).arg(c.suffix.isEmpty() ? QStringLiteral("bin") : c.suffix));
            QFile file(path);
            if (file.open(QIODevice::WriteOnly))
                file.write(c.data);
            file.close();
            v.player = new QMediaPlayer;
            v.output = new QAudioOutput;
            v.player->setAudioOutput(v.output);
            v.player->setSource(QUrl::fromLocalFile(path));
            m_voices->voices << v;
        }
    }
    const double speed = m_speed ? m_speed->currentData().toDouble() : 1.0;
    for (int i = 0; i < clips.size(); ++i) {
        const Audio::Clip& c = clips[i];
        QMediaPlayer* player = m_voices->voices[i].player;
        const int local = m_time - c.start;
        const bool on = m_playing && !c.muted && local >= 0 && local < c.duration;
        if (!on) {
            if (player->playbackState() == QMediaPlayer::PlayingState)
                player->pause();
            continue;
        }
        m_voices->voices[i].output->setVolume(float(std::min(1.0, c.volumeAt(local) / 100.0)));
        if (!qFuzzyCompare(player->playbackRate(), speed))
            player->setPlaybackRate(speed);
        const qint64 want = c.offset + local;
        if (player->playbackState() != QMediaPlayer::PlayingState) {
            player->setPosition(want);
            player->play();
        } else if (std::abs(player->position() - want) > 250) {
            player->setPosition(want);
        }
    }
#endif
}

void GifEditor::applyPalettes()
{
    // Negro AMOLED: el fondo, la vista previa y la línea de tiempo negros, los paneles casi negros y el
    // acento verde azulado en lo elegido. Después de la hoja de estilo (al aplicarla, Qt vuelve a pulir
    // los widgets y perdían la paleta).
    QPalette pal = palette();
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(g, QPalette::Window, Look::black);
        pal.setColor(g, QPalette::WindowText, Look::text);
        pal.setColor(g, QPalette::Base, Look::black);
        pal.setColor(g, QPalette::AlternateBase, Look::panel);
        pal.setColor(g, QPalette::Button, Look::raised);
        pal.setColor(g, QPalette::ButtonText, Look::text);
        pal.setColor(g, QPalette::Text, Look::text);
        pal.setColor(g, QPalette::Highlight, Look::accent);
        pal.setColor(g, QPalette::HighlightedText, QColor(0x00, 0x1a, 0x17));
        pal.setColor(g, QPalette::Mid, Look::line);
        pal.setColor(g, QPalette::Midlight, QColor(0x16, 0x17, 0x1a));
        pal.setColor(g, QPalette::Light, QColor(0x3a, 0x3d, 0x44));
        pal.setColor(g, QPalette::Dark, Look::black);
        pal.setColor(g, QPalette::PlaceholderText, Look::dim);
    }
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5b, 0x5e, 0x65));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5b, 0x5e, 0x65));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5b, 0x5e, 0x65));
    setPalette(pal);
    setAutoFillBackground(true);
    // Lo que se dibuja a mano (línea de tiempo, vista previa, gráfica, medidor, paneles) y las áreas
    // de desplazamiento, con esta paleta: si no, heredaban la del tema (con el claro, texto oscuro
    // sobre negro y la gráfica blanca).
    QList<QWidget*> painted{m_timeline, m_preview, m_inspector, m_audioInspector, m_sizeMeter};
    for (KeyGraph* graph : findChildren<KeyGraph*>())
        painted << graph;
    for (QAbstractScrollArea* area : findChildren<QAbstractScrollArea*>())
        painted << area->viewport();
    for (QWidget* w : std::as_const(painted))
        if (w) {
            w->ensurePolished();
            w->setPalette(pal);
        }
}

void GifEditor::applyLook()
{
    const QString accent = Look::accent.name(), dark = Look::accentDark.name(), line = Look::line.name(), panel = Look::panel.name(),
                  raised = Look::raised.name(), hover = Look::hover.name(), text = Look::text.name(), dim = Look::dim.name();
    setStyleSheet(QStringLiteral(R"(
GifEditor QFrame[gifPanel="true"], GifEditor QStackedWidget[gifPanel="true"] { background: %4; border: 1px solid #1b1c20; border-radius: 12px; }
GifEditor QLabel, GifEditor QCheckBox { color: %7; background: transparent; }
GifEditor QPushButton, GifEditor QToolButton { background: %5; color: %7; border: 1px solid %3; border-radius: 8px; padding: 4px 10px; }
GifEditor QToolButton[autoRaise="true"] { background: transparent; border-color: transparent; }
GifEditor QPushButton:hover, GifEditor QToolButton:hover { background: %6; border-color: #3a3d44; }
GifEditor QPushButton:pressed, GifEditor QToolButton:pressed, GifEditor QToolButton:checked { background: %2; border-color: %1; }
GifEditor QPushButton:disabled, GifEditor QToolButton:disabled { color: #5b5e65; }
GifEditor QToolButton::menu-button { border: none; border-left: 1px solid %3; width: 18px; }
GifEditor QComboBox, GifEditor QSpinBox, GifEditor QDoubleSpinBox { background: #08090a; color: %7; border: 1px solid %3;
    border-radius: 7px; padding: 3px 6px; selection-background-color: %1; selection-color: #001a17; }
GifEditor QComboBox:hover, GifEditor QSpinBox:hover, GifEditor QDoubleSpinBox:hover { border-color: #3a3d44; }
GifEditor QComboBox:focus, GifEditor QSpinBox:focus, GifEditor QDoubleSpinBox:focus { border-color: %1; }
GifEditor QComboBox QAbstractItemView { background: %4; color: %7; border: 1px solid %3; selection-background-color: %1;
    selection-color: #001a17; }
GifEditor QListWidget, GifEditor QListView, GifEditor QTreeView { background: #000000; color: %7; border: 1px solid #1b1c20;
    border-radius: 10px; }
GifEditor QListView::item:selected, GifEditor QTreeView::item:selected { background: %2; color: #ffffff; border-radius: 6px; }
GifEditor QListView::item:hover { background: %5; border-radius: 6px; }
GifEditor QTabWidget::pane { border: none; }
GifEditor QTabBar::tab { background: transparent; color: %8; padding: 6px 8px; border: none; border-bottom: 2px solid transparent; }
GifEditor QTabBar QToolButton { background: %4; border: none; border-radius: 0px; padding: 0px; }
GifEditor QTabBar::tab:selected { color: #ffffff; border-bottom-color: %1; }
GifEditor QTabBar::tab:hover:!selected { color: %7; }
GifEditor QScrollArea { background: transparent; border: none; }
GifEditor QSplitter::handle { background: #000000; }
GifEditor QScrollBar:horizontal { background: #000000; height: 10px; margin: 0; border: none; }
GifEditor QScrollBar:vertical { background: #000000; width: 10px; margin: 0; border: none; }
GifEditor QScrollBar::handle { background: %3; border-radius: 4px; min-width: 24px; min-height: 24px; margin: 2px; }
GifEditor QScrollBar::handle:hover { background: #4a4d55; }
GifEditor QScrollBar::add-line, GifEditor QScrollBar::sub-line { width: 0; height: 0; }
GifEditor QScrollBar::add-page, GifEditor QScrollBar::sub-page { background: transparent; }
GifEditor QSlider::groove:horizontal { height: 4px; background: %3; border-radius: 2px; }
GifEditor QSlider::sub-page:horizontal { background: %1; border-radius: 2px; }
GifEditor QSlider::handle:horizontal { background: #ffffff; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
GifEditor QFrame[frameShape="5"] { color: %3; }
)")
                      .arg(accent, dark, line, panel, raised, hover, text, dim));
    applyPalettes();
}
