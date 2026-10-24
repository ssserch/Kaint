#include "app/widgets/EncodedPreview.h"

#include "app/Icons.h"
#include "core/ImageIO.h"
#include "core/Raster.h"

#include <QBuffer>
#include <QComboBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

EncodedPreview::EncodedPreview(const QString& path, QWidget* parent)
    : QWidget(parent)
    , m_path(path)
{
    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(m_image);
    scroll->setWidgetResizable(true);
    scroll->setMinimumSize(360, 280);
    m_zoom = new QComboBox(this);
    m_zoom->addItem(tr("Ajustar"), 0);
    for (int z : {100, 200, 400})
        m_zoom->addItem(QStringLiteral("%1 %").arg(z), z);
    connect(m_zoom, &QComboBox::currentIndexChanged, this, [this] { showFrame(m_index); });

    // Reproducir el GIF (con sus duraciones y repeticiones).
    m_play = new QToolButton(this);
    m_play->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_play->setAutoRaise(true);
    m_play->setEnabled(false);
    m_frameLabel = new QLabel(this);
    m_playTimer.setSingleShot(true);
    connect(&m_playTimer, &QTimer::timeout, this, &EncodedPreview::nextFrame);
    connect(m_play, &QToolButton::clicked, this, [this] { setPlaying(!isPlaying()); });
    setPlaying(false);

    auto* zoomRow = new QHBoxLayout;
    zoomRow->addWidget(new QLabel(tr("Vista previa:"), this));
    zoomRow->addStretch();
    zoomRow->addWidget(m_zoom);
    auto* playRow = new QHBoxLayout;
    playRow->addWidget(m_play);
    playRow->addWidget(m_frameLabel);
    playRow->addStretch();
    const bool animation = ImageIO::isAnimation(path);
    m_play->setVisible(animation);
    m_frameLabel->setVisible(animation);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(zoomRow);
    layout->addWidget(scroll, 1);
    layout->addLayout(playRow);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &EncodedPreview::start);
}

EncodedPreview::~EncodedPreview()
{
    m_playTimer.stop();
    ++m_generation;
    if (m_thread.joinable())
        m_thread.join();
}

void EncodedPreview::request(Encoder encoder, int loops)
{
    m_encoder = std::move(encoder);
    m_loops = loops;
    emit statusChanged(tr("Tamaño del archivo: calculando…"));
    m_pending = true;
    m_debounce.start();
}

void EncodedPreview::start()
{
    if (m_running || !m_pending || !m_encoder)
        return;
    if (m_thread.joinable())
        m_thread.join();
    m_pending = false;
    m_running = true;
    const int generation = ++m_generation;
    m_thread = std::thread([this, generation, encoder = m_encoder, path = m_path] {
        QString error;
        QByteArray data = encoder(&error);
        // Se vuelve a leer lo codificado: así la vista previa es exactamente el archivo.
        QList<Gif::Frame> frames;
        if (!data.isEmpty() && ImageIO::isAnimation(path)) {
            frames = Gif::decode(data).frames;
        } else if (!data.isEmpty() && ImageIO::isIcon(path)) {
            QBuffer buffer(&data);
            QImageReader reader(&buffer, "ico");
            QImage largest;
            for (int i = 0; i < std::max(1, reader.imageCount()); ++i) {
                if (i > 0 && !reader.jumpToImage(i))
                    break;
                const QImage img = reader.read();
                if (img.width() > largest.width())
                    largest = img;
            }
            if (!largest.isNull())
                frames << Gif::Frame{largest, 100};
        } else if (!data.isEmpty()) {
            QImage decoded;
            if (decoded.loadFromData(data))
                frames << Gif::Frame{decoded, 100};
        }
        const qint64 bytes = data.size();
        QMetaObject::invokeMethod(
            this, [this, frames, bytes, error, generation] { onEncoded(frames, bytes, error, generation); },
            Qt::QueuedConnection);
    });
}

void EncodedPreview::onEncoded(const QList<Gif::Frame>& frames, qint64 bytes, const QString& error, int generation)
{
    if (generation != m_generation)
        return;
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    m_frames = frames;
    emit statusChanged(error.isEmpty() ? tr("Tamaño del archivo: %1").arg(QLocale().formattedDataSize(bytes))
                                       : tr("No se pudo codificar: %1").arg(error));
    m_index = std::clamp(m_index, 0, std::max(0, int(m_frames.size()) - 1));
    m_play->setEnabled(m_frames.size() > 1);
    if (m_frames.size() < 2)
        setPlaying(false);
    showFrame(m_index);
    if (m_pending && !m_debounce.isActive())
        start();
}

void EncodedPreview::showFrame(int index)
{
    m_frameLabel->setText(m_frames.size() > 1 ? tr("Fotograma %1 de %2").arg(index + 1).arg(m_frames.size()) : QString());
    if (index < 0 || index >= m_frames.size())
        return;
    m_index = index;
    const QImage& image = m_frames[index].image;
    const int zoom = m_zoom->currentData().toInt();
    QSize s = image.size();
    if (zoom == 0)
        s = s.scaled(QSize(340, 260), Qt::KeepAspectRatio).boundedTo(image.size()).expandedTo(QSize(1, 1));
    else
        s = s * zoom / 100;
    QPixmap pm(s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.fillRect(pm.rect(), QBrush(Raster::checkerboard(8)));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom == 0);
    p.drawImage(pm.rect(), image);
    p.end();
    m_image->setPixmap(pm);
}

void EncodedPreview::setPlaying(bool playing)
{
    if (playing && m_frames.size() > 1) {
        // Las repeticiones del archivo: sin fin, una vez o tantas más.
        m_playsLeft = m_loops == 0 ? -1 : (m_loops < 0 ? 1 : m_loops + 1);
        if (m_index >= m_frames.size() - 1)
            showFrame(0);
        m_playTimer.start(std::max(Gif::kMinDelayMs, m_frames[m_index].delayMs));
    } else {
        m_playTimer.stop();
    }
    const bool on = isPlaying();
    m_play->setIcon(Icons::theme({on ? "media-playback-pause" : "media-playback-start"}));
    m_play->setText(on ? tr("Pausa") : tr("Reproducir"));
}

void EncodedPreview::nextFrame()
{
    if (m_frames.size() < 2)
        return setPlaying(false);
    int next = m_index + 1;
    if (next >= m_frames.size()) {
        if (m_playsLeft > 0 && --m_playsLeft == 0)
            return setPlaying(false);
        next = 0;
    }
    showFrame(next);
    m_playTimer.start(std::max(Gif::kMinDelayMs, m_frames[next].delayMs));
}
