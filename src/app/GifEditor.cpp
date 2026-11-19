#include "app/GifEditor.h"

#include "app/Icons.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Gif.h"
#include "core/ImageIO.h"
#include "core/Layer.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace GifEdit {

namespace {

constexpr int kDefaultDelay = 100;
constexpr int kRulerHeight = 22;
constexpr int kTrackTop = kRulerHeight + 6;
constexpr int kTrackHeight = 74;
constexpr int kLeftMargin = 12;
constexpr int kEdgeGrab = 6;

QImage thumbnail(const QImage& image, QSize box)
{
    if (image.isNull())
        return {};
    return image.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void paintChecker(QPainter& p, const QRect& r)
{
    static const QPixmap tile = [] {
        QPixmap pm(16, 16);
        pm.fill(QColor(255, 255, 255));
        QPainter t(&pm);
        t.fillRect(0, 0, 8, 8, QColor(204, 204, 204));
        t.fillRect(8, 8, 8, 8, QColor(204, 204, 204));
        return pm;
    }();
    p.fillRect(r, QBrush(tile));
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
    explicit Preview(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumSize(200, 150);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(const QImage& image)
    {
        m_image = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(24, 24, 26));
        if (m_image.isNull()) {
            p.setPen(QColor(200, 200, 200));
            p.drawText(rect(), Qt::AlignCenter, GifEditor::tr("Sin fotogramas: arrastra imágenes o GIF a la línea de tiempo"));
            return;
        }
        const QSize s = m_image.size().scaled(rect().size() - QSize(16, 16), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        paintChecker(p, target);
        p.setRenderHint(QPainter::SmoothPixmapTransform, s.width() < m_image.width());
        p.drawImage(target, m_image);
    }

private:
    QImage m_image;
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
        setMinimumHeight(kTrackTop + kTrackHeight + 14);
    }

    void setPixelsPerSecond(int pps)
    {
        m_pps = std::clamp(pps, 40, 3000);
        updateGeometry();
        adjustSize();
        update();
    }
    int pixelsPerSecond() const { return m_pps; }

    QSize sizeHint() const override
    {
        const QList<int> starts = m_editor->frameStarts();
        const int total = starts.isEmpty() ? 0 : starts.last();
        const int w = kLeftMargin * 2 + int(std::ceil(total * pxPerMs())) + 160;
        return {std::max(w, parentWidget() ? parentWidget()->width() : 400), kTrackTop + kTrackHeight + 14};
    }

    int xForTime(int ms) const { return kLeftMargin + int(std::round(ms * pxPerMs())); }
    int timeForX(int x) const { return std::max(0, int(std::round((x - kLeftMargin) / pxPerMs()))); }

    QRect clipRect(int index, const QList<int>& starts) const
    {
        const int x0 = xForTime(starts[index]);
        const int x1 = xForTime(starts[index + 1]);
        return QRect(x0, kTrackTop, std::max(6, x1 - x0) - 2, kTrackHeight);
    }

    void refresh()
    {
        adjustSize();
        update();
    }

protected:
    double pxPerMs() const { return m_pps / 1000.0; }

    QList<int> starts() const
    {
        QList<int> s = m_editor->frameStarts();
        // Mientras se estira un clip, su duración nueva.
        if (m_resizing >= 0 && m_resizing + 1 < s.size()) {
            const int diff = m_resizeDelay - (s[m_resizing + 1] - s[m_resizing]);
            for (int i = m_resizing + 1; i < s.size(); ++i)
                s[i] += diff;
        }
        return s;
    }

    int clipAt(QPoint pos, const QList<int>& s) const
    {
        for (int i = 0; i + 1 < s.size(); ++i)
            if (clipRect(i, s).adjusted(0, 0, 2, 0).contains(pos))
                return i;
        return -1;
    }

    // Dónde quedaría lo que se suelta en x: delante del clip cuya mitad está más allá.
    int insertionAt(int x) const
    {
        const QList<int> s = starts();
        const int n = int(s.size()) - 1;
        for (int i = 0; i < n; ++i)
            if (x < (xForTime(s[i]) + xForTime(s[i + 1])) / 2)
                return i;
        return std::max(0, n);
    }

    void paintEvent(QPaintEvent* e) override
    {
        QPainter p(this);
        const QPalette pal = palette();
        p.fillRect(e->rect(), pal.color(QPalette::Base));
        // Regla del tiempo
        const QRect ruler(0, 0, width(), kRulerHeight);
        p.fillRect(ruler, pal.color(QPalette::Window));
        p.setPen(pal.color(QPalette::Mid));
        p.drawLine(0, kRulerHeight - 1, width(), kRulerHeight - 1);
        // Una marca grande cada cierto tiempo, según el zum (al menos 70 px entre ellas).
        static const int steps[] = {50, 100, 200, 250, 500, 1000, 2000, 5000, 10000};
        int major = steps[std::size(steps) - 1];
        for (int s : steps)
            if (s * pxPerMs() >= 70) {
                major = s;
                break;
            }
        const int minor = major / 5;
        QFont small = font();
        small.setPointSizeF(std::max(6.5, small.pointSizeF() * 0.8));
        p.setFont(small);
        const int endMs = timeForX(width());
        for (int t = 0; t <= endMs; t += minor) {
            const int x = xForTime(t);
            const bool big = t % major == 0;
            p.setPen(pal.color(QPalette::Mid));
            p.drawLine(x, kRulerHeight - (big ? 10 : 5), x, kRulerHeight - 1);
            if (big) {
                p.setPen(pal.color(QPalette::Text));
                p.drawText(x + 3, 2, 80, kRulerHeight - 8, Qt::AlignLeft | Qt::AlignTop, timeText(t));
            }
        }
        // Clips
        const QList<int> s = starts();
        const int selected = m_editor->selectedFrame();
        Document* doc = m_editor->document();
        p.setFont(small);
        for (int i = 0; i + 1 < s.size(); ++i) {
            const QRect r = clipRect(i, s);
            if (!r.intersects(e->rect()))
                continue;
            const bool hidden = m_editor->frameHidden(i);
            QPainterPath path;
            path.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            QColor fill = i == selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Button);
            if (hidden)
                fill.setAlpha(110);
            p.setRenderHint(QPainter::Antialiasing);
            p.fillPath(path, fill);
            p.setPen(QPen(i == selected ? pal.color(QPalette::Highlight).darker(130) : pal.color(QPalette::Mid), 1));
            p.drawPath(path);
            p.setRenderHint(QPainter::Antialiasing, false);
            // La miniatura (la de la capa), dentro del clip.
            if (doc && i < doc->layerCount() && r.width() > 14) {
                const QImage& img = doc->layer(i)->image;
                const QSize box(r.width() - 6, r.height() - 22);
                const QImage t = cachedThumb(img, box);
                if (!t.isNull()) {
                    const QRect tr(r.left() + (r.width() - t.width()) / 2, r.top() + 3 + (box.height() - t.height()) / 2,
                                   t.width(), t.height());
                    paintChecker(p, tr);
                    if (hidden)
                        p.setOpacity(0.4);
                    p.drawImage(tr, t);
                    p.setOpacity(1.0);
                }
            }
            p.setPen(i == selected ? pal.color(QPalette::HighlightedText) : pal.color(QPalette::ButtonText));
            const QRect label = r.adjusted(4, r.height() - 18, -4, -2);
            const QString text = r.width() > 70 ? QStringLiteral("%1 · %2 ms").arg(i + 1).arg(s[i + 1] - s[i])
                                 : r.width() > 26 ? QString::number(i + 1) : QString();
            p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, label.width()));
            // El asa para estirar, a la derecha.
            if (r.width() > 16) {
                p.setPen(Qt::NoPen);
                p.setBrush(pal.color(QPalette::Mid));
                p.drawRect(QRect(r.right() - 3, r.top() + r.height() / 2 - 8, 2, 16));
                p.setBrush(Qt::NoBrush);
            }
        }
        if (s.size() <= 1) {
            p.setPen(pal.color(QPalette::PlaceholderText));
            p.drawText(QRect(kLeftMargin, kTrackTop, std::max(300, width() - 2 * kLeftMargin), kTrackHeight),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       GifEditor::tr("Arrastra aquí imágenes o GIF (de la biblioteca o de tus carpetas)"));
        }
        // Dónde caerá lo que se arrastra
        if (m_dropAt >= 0) {
            const int x = m_dropAt < s.size() ? xForTime(s[m_dropAt]) - 1 : xForTime(s.last());
            p.fillRect(QRect(x - 1, kTrackTop - 4, 3, kTrackHeight + 8), pal.color(QPalette::Highlight));
        }
        // Cabezal
        const int px = xForTime(m_editor->playTime());
        p.setPen(QPen(QColor(230, 60, 60), 2));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor(230, 60, 60));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon{QPoint(px - 6, 0), QPoint(px + 6, 0), QPoint(px, 8)});
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

    void mousePressEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        const QList<int> s = starts();
        if (pos.y() < kRulerHeight || (e->button() == Qt::LeftButton && clipAt(pos, s) < 0)) {
            m_scrubbing = e->button() == Qt::LeftButton;
            m_editor->seek(timeForX(pos.x()));
            return;
        }
        const int index = clipAt(pos, s);
        if (index < 0)
            return;
        m_editor->selectFrame(index);
        if (e->button() == Qt::RightButton) {
            m_editor->showFrameMenu(index, e->globalPosition().toPoint());
            return;
        }
        if (e->button() != Qt::LeftButton)
            return;
        const QRect r = clipRect(index, s);
        if (pos.x() >= r.right() - kEdgeGrab) {
            m_resizing = index;
            m_resizeDelay = s[index + 1] - s[index];
            return;
        }
        m_pressIndex = index;
        m_pressPos = pos;
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const QPoint pos = e->position().toPoint();
        if (m_scrubbing) {
            m_editor->seek(timeForX(pos.x()));
            return;
        }
        if (m_resizing >= 0) {
            const QList<int> s = m_editor->frameStarts();
            const int raw = timeForX(pos.x()) - s[m_resizing];
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
                m_dropAt = insertionAt(pos.x());
                setCursor(Qt::ClosedHandCursor);
                update();
            }
            return;
        }
        // El cursor de estirar sobre el borde derecho de un clip.
        const QList<int> s = starts();
        const int index = clipAt(pos, s);
        setCursor(index >= 0 && pos.x() >= clipRect(index, s).right() - kEdgeGrab ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
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
            int to = insertionAt(e->position().toPoint().x());
            m_reordering = false;
            m_pressIndex = -1;
            m_dropAt = -1;
            setCursor(Qt::ArrowCursor);
            if (to > from)
                --to;
            if (to != from)
                m_editor->moveFrame(from, to);
            update();
            return;
        }
        m_pressIndex = -1;
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        const int index = clipAt(e->position().toPoint(), starts());
        if (index >= 0)
            m_editor->editDelay(index);
    }

    void wheelEvent(QWheelEvent* e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            setPixelsPerSecond(int(m_pps * (e->angleDelta().y() > 0 ? 1.25 : 0.8)));
            e->accept();
            return;
        }
        // La rueda desplaza a lo largo de la línea de tiempo.
        if (auto* area = qobject_cast<QScrollArea*>(parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
            QScrollBar* bar = area->horizontalScrollBar();
            bar->setValue(bar->value() - (e->angleDelta().y() + e->angleDelta().x()) / 2);
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

    void dragMoveEvent(QDragMoveEvent* e) override
    {
        m_dropAt = insertionAt(e->position().toPoint().x());
        update();
        e->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override
    {
        m_dropAt = -1;
        update();
    }

    void dropEvent(QDropEvent* e) override
    {
        const int at = insertionAt(e->position().toPoint().x());
        m_dropAt = -1;
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
                m_editor->insertMedia(i, at + offset);
                offset += m_editor->frameCount() - before;
            }
            e->acceptProposedAction();
            return;
        }
        QStringList paths;
        for (const QUrl& url : data->urls())
            if (url.isLocalFile())
                paths << url.toLocalFile();
        if (!paths.isEmpty()) {
            m_editor->insertFiles(paths, at);
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
    bool m_reordering = false;
    bool m_scrubbing = false;
    int m_dropAt = -1;
    QHash<QString, QImage> m_thumbs;
};

} // namespace GifEdit

using namespace GifEdit;

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

GifEditor::GifEditor(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("GifEditor"));
    setAcceptDrops(false);
    buildUi();
    m_ticker.setInterval(15);
    connect(&m_ticker, &QTimer::timeout, this, &GifEditor::tick);
}

GifEditor::~GifEditor() = default;

void GifEditor::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Cabecera: el nombre y volver al editor de imágenes.
    auto* head = new QHBoxLayout;
    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_title->setFont(titleFont);
    auto* back = new QPushButton(Icons::theme({"draw-brush", "applications-graphics"}), tr("Editor de imágenes"), this);
    back->setToolTip(tr("Vuelve a editar esta imagen con las herramientas (cada fotograma es una capa)."));
    connect(back, &QPushButton::clicked, this, &GifEditor::imageEditorRequested);
    auto* exportButton = new QPushButton(Icons::theme({"document-export", "document-save-as"}), tr("Exportar GIF..."), this);
    exportButton->setToolTip(tr("Guarda la animación como GIF, con la configuración de siempre (paleta, duración, "
                                "repeticiones, efectos...)."));
    connect(exportButton, &QPushButton::clicked, this, &GifEditor::exportRequested);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::theme({"video-x-generic", "image-gif", "media-playback-start"}).pixmap(20, 20));
    head->addWidget(icon);
    head->addWidget(m_title, 1);
    head->addWidget(back);
    head->addWidget(exportButton);
    root->addLayout(head);

    auto* vertical = new QSplitter(Qt::Vertical, this);
    vertical->setChildrenCollapsible(false);
    auto* top = new QSplitter(Qt::Horizontal, vertical);
    top->setChildrenCollapsible(false);

    // Biblioteca
    auto* bin = new QWidget(top);
    auto* binLayout = new QVBoxLayout(bin);
    binLayout->setContentsMargins(0, 0, 0, 0);
    auto* binTitle = new QLabel(tr("Medios"), bin);
    binTitle->setFont(titleFont);
    m_mediaList = new MediaList(bin);
    m_mediaList->setToolTip(tr("Las imágenes abiertas en Kaint y las que importes. Arrástralas a la línea de tiempo "
                               "(o haz doble clic para añadirlas al final)."));
    connect(m_mediaList, &MediaList::filesDropped, this, &GifEditor::importFiles);
    connect(m_mediaList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { insertMedia(item->data(Qt::UserRole).toInt(), -1); });
    auto* binButtons = new QHBoxLayout;
    auto* import = new QPushButton(Icons::theme({"document-import", "document-open"}), tr("Importar..."), bin);
    connect(import, &QPushButton::clicked, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Importar imágenes o GIF"), QString(), ImageIO::openFilter());
        importFiles(paths);
    });
    auto* add = new QPushButton(Icons::theme({"list-add"}), tr("Añadir"), bin);
    add->setToolTip(tr("Añade los medios elegidos detrás del fotograma elegido."));
    connect(add, &QPushButton::clicked, this, [this] {
        int at = selectedFrame() >= 0 ? selectedFrame() + 1 : -1;
        for (QListWidgetItem* item : m_mediaList->selectedItems()) {
            const int before = frameCount();
            insertMedia(item->data(Qt::UserRole).toInt(), at);
            if (at >= 0)
                at += frameCount() - before;
        }
    });
    binButtons->addWidget(import);
    binButtons->addWidget(add);
    binLayout->addWidget(binTitle);
    binLayout->addWidget(m_mediaList, 1);
    binLayout->addLayout(binButtons);

    // Vista previa y reproductor
    auto* player = new QWidget(top);
    auto* playerLayout = new QVBoxLayout(player);
    playerLayout->setContentsMargins(0, 0, 0, 0);
    m_preview = new Preview(player);
    auto* transport = new QHBoxLayout;
    auto button = [this, player](std::initializer_list<const char*> icons, const QString& tip, auto fn) {
        auto* b = new QToolButton(player);
        b->setIcon(Icons::theme(icons));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20, 20));
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };
    transport->addWidget(button({"media-skip-backward"}, tr("Primer fotograma (Inicio)"), [this] { seek(0); }));
    transport->addWidget(button({"media-seek-backward"}, tr("Fotograma anterior (←)"), [this] { step(-1); }));
    m_play = button({"media-playback-start"}, tr("Reproducir o pausar (Espacio)"), [this] { togglePlay(); });
    transport->addWidget(m_play);
    transport->addWidget(button({"media-playback-stop"}, tr("Detener"), [this] { stop(); seek(0); }));
    transport->addWidget(button({"media-seek-forward"}, tr("Fotograma siguiente (→)"), [this] { step(1); }));
    transport->addWidget(button({"media-skip-forward"}, tr("Último fotograma (Fin)"), [this] {
        const QList<int> s = frameStarts();
        if (s.size() > 1)
            seek(s[s.size() - 2]);
    }));
    m_timeLabel = new QLabel(player);
    m_timeLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    transport->addSpacing(8);
    transport->addWidget(m_timeLabel);
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
    playerLayout->addLayout(transport);

    top->addWidget(bin);
    top->addWidget(player);
    top->setStretchFactor(0, 2);
    top->setStretchFactor(1, 5);

    // Línea de tiempo con su barra
    auto* timelineBox = new QWidget(vertical);
    auto* timelineLayout = new QVBoxLayout(timelineBox);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timelineLayout->setSpacing(2);
    auto* bar = new QHBoxLayout;
    auto tool = [this, timelineBox](std::initializer_list<const char*> icons, const QString& text, const QString& tip, auto fn,
                                    bool frame) {
        auto* b = new QToolButton(timelineBox);
        b->setIcon(Icons::theme(icons));
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
        connect(b, &QToolButton::clicked, this, fn);
        if (frame)
            m_frameButtons << b;
        return b;
    };
    bar->addWidget(tool({"edit-undo"}, {}, tr("Deshacer"), [this] {
        if (m_doc)
            m_doc->undoStack()->undo();
    }, false));
    bar->addWidget(tool({"edit-redo"}, {}, tr("Rehacer"), [this] {
        if (m_doc)
            m_doc->undoStack()->redo();
    }, false));
    bar->addSpacing(8);
    bar->addWidget(tool({"edit-copy", "edit-duplicate"}, tr("Duplicar"), tr("Duplica el fotograma elegido"),
                        [this] { duplicateFrame(selectedFrame()); }, true));
    bar->addWidget(tool({"edit-delete", "list-remove"}, tr("Eliminar"), tr("Quita el fotograma elegido (Supr)"),
                        [this] { removeFrame(selectedFrame()); }, true));
    bar->addWidget(tool({"view-hidden", "visibility"}, {}, tr("Ocultar o mostrar el fotograma (los ocultos no van en el GIF)"),
                        [this] { toggleHidden(selectedFrame()); }, true));
    bar->addWidget(tool({"draw-brush", "edit-image"}, {}, tr("Editar el fotograma con las herramientas"), [this] {
        if (selectedFrame() >= 0)
            emit editFrameRequested(selectedFrame());
    }, true));
    bar->addSpacing(8);
    auto* durationLabel = new QLabel(tr("Duración:"), timelineBox);
    m_delay = new QSpinBox(timelineBox);
    m_delay->setRange(Gif::kMinDelayMs, 60000);
    m_delay->setSingleStep(10);
    m_delay->setSuffix(tr(" ms"));
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
    bar->addSpacing(8);
    bar->addWidget(tool({"media-seek-backward", "go-previous"}, tr("Invertir"), tr("El último fotograma pasa a ser el primero"),
                        [this] { reverseFrames(); }, false));
    bar->addWidget(tool({"media-playlist-repeat", "view-refresh"}, tr("Ida y vuelta"),
                        tr("Añade los fotogramas al revés al final: la animación va y vuelve"), [this] { boomerang(); }, false));
    bar->addStretch(1);
    bar->addWidget(new QLabel(tr("Zum:"), timelineBox));
    m_zoom = new QSlider(Qt::Horizontal, timelineBox);
    m_zoom->setRange(40, 3000);
    m_zoom->setValue(600);
    m_zoom->setFixedWidth(140);
    m_zoom->setToolTip(tr("Zum de la línea de tiempo (también con Ctrl y la rueda)"));
    bar->addWidget(m_zoom);
    timelineLayout->addLayout(bar);

    m_timelineScroll = new QScrollArea(timelineBox);
    m_timelineScroll->setWidgetResizable(false);
    m_timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timeline = new Timeline(this, m_timelineScroll);
    m_timelineScroll->setWidget(m_timeline);
    m_timelineScroll->setMinimumHeight(m_timeline->minimumHeight() + 20);
    connect(m_zoom, &QSlider::valueChanged, this, [this](int v) { m_timeline->setPixelsPerSecond(v); });
    timelineLayout->addWidget(m_timelineScroll, 1);

    vertical->addWidget(top);
    vertical->addWidget(timelineBox);
    vertical->setStretchFactor(0, 3);
    vertical->setStretchFactor(1, 1);
    root->addWidget(vertical, 1);

    setFocusPolicy(Qt::StrongFocus);
    updateState();
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
    if (m_doc) {
        auto refresh = [this] { updateState(); };
        connect(m_doc, &Document::layersChanged, this, refresh);
        connect(m_doc, &Document::layerPixelsChanged, this, refresh);
        connect(m_doc, &Document::pixelsChanged, this, refresh);
        connect(m_doc, &Document::activeLayerChanged, this, [this] {
            // Al elegir otra capa (fotograma), el cabezal va a su principio (si no se reproduce).
            if (!m_playing) {
                const QList<int> s = frameStarts();
                const int i = selectedFrame();
                if (i >= 0 && i + 1 < s.size() && frameAt(m_time) != i)
                    m_time = s[i];
            }
            updateState();
        });
        connect(m_doc, &Document::sizeChanged, this, refresh);
        connect(m_doc, &QObject::destroyed, this, [this] { setDocument(nullptr); });
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
    if (ImageIO::isAnimation(path)) {
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
    m_doc->setActiveIndex(index);
    if (!m_playing) {
        const QList<int> s = frameStarts();
        m_time = s[index];
    }
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

QList<int> GifEditor::frameStarts() const
{
    QList<int> starts{0};
    if (!m_doc)
        return starts;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        starts << starts.last() + delayOf(m_doc->layer(i));
    return starts;
}

int GifEditor::frameAt(int ms) const
{
    const QList<int> s = frameStarts();
    for (int i = 0; i + 1 < s.size(); ++i)
        if (ms < s[i + 1])
            return i;
    return int(s.size()) - 2;
}

void GifEditor::seek(int ms)
{
    const QList<int> s = frameStarts();
    if (s.size() <= 1) {
        m_time = 0;
        updateState();
        return;
    }
    m_time = std::clamp(ms, 0, std::max(0, s.last() - 1));
    const int index = frameAt(m_time);
    if (m_doc && index >= 0 && index != m_doc->activeIndex())
        m_doc->setActiveIndex(index);
    updateState();
}

void GifEditor::step(int delta)
{
    stop();
    const QList<int> s = frameStarts();
    if (s.size() <= 1)
        return;
    const int count = int(s.size()) - 1;
    int index = frameAt(m_time) + delta;
    if (index < 0)
        index = m_loop->isChecked() ? count - 1 : 0;
    if (index >= count)
        index = m_loop->isChecked() ? 0 : count - 1;
    seek(s[index]);
}

void GifEditor::togglePlay()
{
    if (m_playing) {
        stop();
        return;
    }
    if (frameCount() == 0)
        return;
    const QList<int> s = frameStarts();
    if (m_time >= s.last() - 1)
        m_time = 0;
    m_playing = true;
    m_clock.start();
    m_lastTick = 0;
    m_ticker.start();
    m_play->setIcon(Icons::theme({"media-playback-pause"}));
}

void GifEditor::stop()
{
    m_ticker.stop();
    m_playing = false;
    if (m_play)
        m_play->setIcon(Icons::theme({"media-playback-start"}));
}

void GifEditor::tick()
{
    const QList<int> s = frameStarts();
    if (!m_doc || s.size() <= 1) {
        stop();
        return;
    }
    const qint64 now = m_clock.elapsed();
    const double speed = m_speed->currentData().toDouble();
    m_time += int(std::round((now - m_lastTick) * speed));
    m_lastTick = now;
    if (m_time >= s.last()) {
        if (!m_loop->isChecked()) {
            m_time = s[s.size() - 2];
            stop();
        } else {
            m_time %= std::max(1, s.last());
        }
    }
    // Los fotogramas ocultos no se ven (tampoco van en el GIF): se saltan.
    int index = frameAt(m_time);
    const int count = int(s.size()) - 1;
    for (int tries = 0; tries < count && frameHidden(index); ++tries) {
        index = (index + 1) % count;
        m_time = s[index];
    }
    if (index != m_doc->activeIndex())
        m_doc->setActiveIndex(index);
    else
        updateState();
}

void GifEditor::updateState()
{
    const bool has = m_doc && m_doc->layerCount() > 0;
    m_title->setText(m_doc ? tr("Editor de GIF: %1").arg(m_doc->displayName()) : tr("Editor de GIF"));
    const int index = selectedFrame();
    m_preview->setImage(has && index >= 0 ? m_doc->layer(index)->image : QImage());
    for (QWidget* w : std::as_const(m_frameButtons))
        w->setEnabled(has && index >= 0);
    if (has && index >= 0 && !m_delay->hasFocus()) {
        const QSignalBlocker block(m_delay);
        m_delay->setValue(frameDelay(index));
    }
    updateTimeLabel();
    m_timeline->refresh();
    // El cabezal siempre a la vista mientras se reproduce.
    if (m_playing)
        m_timelineScroll->ensureVisible(m_timeline->xForTime(m_time), kTrackTop, 60, 0);
}

void GifEditor::updateTimeLabel()
{
    const QList<int> s = frameStarts();
    const int index = selectedFrame();
    QString text = QStringLiteral("%1 / %2").arg(timeText(m_time), timeText(s.last()));
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
    const DocumentState before = DocumentState::capture(m_doc);
    DocumentState after = before;
    after.layers = layers;
    after.images.clear();
    for (const LayerPtr& l : layers)
        after.images << l->image;
    after.props = props;
    after.active = std::clamp(active, 0, std::max(0, int(layers.size()) - 1));
    HistoryIcons::registerIcon(text, icon);
    m_doc->undoStack()->push(new DocumentStateCommand(m_doc, before, after, text));
    updateState();
}

void GifEditor::insertFrames(int at, const QList<QImage>& images, const QList<int>& delays, const QString& text)
{
    if (!m_doc || images.isEmpty())
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    if (at < 0 || at > layers.size())
        at = int(layers.size());
    for (int i = 0; i < images.size(); ++i) {
        const QString name = Gif::nameWithDelay(tr("Fotograma %1").arg(at + i + 1), delays.value(i, kDefaultDelay));
        auto layer = std::make_shared<Layer>(name, fitted(images[i], m_doc->size()));
        layers.insert(at + i, layer);
        props.insert(at + i, layer->props);
    }
    applyFrames(layers, props, at, text, QStringLiteral("list-add"));
}

void GifEditor::insertMedia(int mediaIndex, int at)
{
    if (mediaIndex < 0 || mediaIndex >= m_media.size())
        return;
    const Media& m = m_media[mediaIndex];
    QList<QImage> frames = m.frames;
    QList<int> delays = m.delays;
    if (m.document)
        framesOf(m.document, &frames, &delays);
    insertFrames(at, frames, delays, tr("Añadir «%1»").arg(m.name));
}

void GifEditor::insertFiles(const QStringList& paths, int at)
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
        insertFrames(at, frames, delays, names.size() == 1 ? tr("Añadir «%1»").arg(names.first()) : tr("Añadir imágenes"));
}

void GifEditor::moveFrame(int from, int to)
{
    if (!m_doc || from < 0 || from >= m_doc->layerCount() || to < 0 || to >= m_doc->layerCount() || from == to)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    layers.move(from, to);
    props.move(from, to);
    applyFrames(layers, props, to, tr("Mover fotograma"), QStringLiteral("transform-move"));
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
    if (!m_doc || m_doc->layerCount() < 2)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    std::reverse(layers.begin(), layers.end());
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    applyFrames(layers, props, m_doc->layerCount() - 1 - m_doc->activeIndex(), tr("Invertir el orden"),
                QStringLiteral("media-seek-backward"));
}

void GifEditor::boomerang()
{
    // Como en la configuración avanzada del GIF: los fotogramas al revés al final, sin repetir el
    // último ni el primero.
    if (!m_doc || m_doc->layerCount() < 3)
        return;
    QList<LayerPtr> layers = m_doc->layers();
    QList<LayerProps> props;
    for (const LayerPtr& l : std::as_const(layers))
        props << l->props;
    for (int i = int(m_doc->layerCount()) - 2; i >= 1; --i) {
        auto copy = std::make_shared<Layer>(layers[i]->props.name, layers[i]->image);
        copy->props = layers[i]->props;
        layers << copy;
        props << copy->props;
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
    menu.addAction(Icons::theme({"chronometer", "clock"}), tr("Duración..."), this, [this, index] { editDelay(index); });
    menu.addAction(Icons::theme({"edit-copy"}), tr("Duplicar"), this, [this, index] { duplicateFrame(index); });
    menu.addAction(Icons::theme({"view-hidden", "visibility"}), frameHidden(index) ? tr("Mostrar") : tr("Ocultar"), this,
                   [this, index] { toggleHidden(index); });
    menu.addAction(Icons::theme({"draw-brush", "edit-image"}), tr("Editar con las herramientas"), this,
                   [this, index] { emit editFrameRequested(index); });
    menu.addSeparator();
    QAction* del = menu.addAction(Icons::theme({"edit-delete"}), tr("Eliminar"), this, [this, index] { removeFrame(index); });
    del->setEnabled(frameCount() > 1);
    menu.exec(globalPos);
}

// Teclado: espacio reproduce, las flechas pasan de fotograma, Inicio va al principio y Supr quita
// el fotograma. Se piden antes que los atajos de la ventana (Supr borra la selección): si no, la
// misma tecla en los dos sitios no hacía nada. Un campo de texto (la duración) se queda las suyas.
static bool isEditorKey(const QKeyEvent* e)
{
    if (e->modifiers() & ~Qt::KeypadModifier)
        return false;
    switch (e->key()) {
    case Qt::Key_Space:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Home:
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
    switch (e->key()) {
    case Qt::Key_Space: togglePlay(); break;
    case Qt::Key_Left: step(-1); break;
    case Qt::Key_Right: step(1); break;
    case Qt::Key_Home: seek(0); break;
    case Qt::Key_Delete: removeFrame(selectedFrame()); break;
    default: break;
    }
    e->accept();
}

void GifEditor::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    rebuildMedia();
    updateState();
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
    QWidget::changeEvent(e);
}
