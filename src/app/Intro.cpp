#include "app/Intro.h"

#include "app/Animations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QSettings>
#include <QStandardPaths>
#include <QVariantAnimation>

#ifdef KAINT_HAVE_QTMULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#endif

namespace {
// Lo que tarda la línea en llegar al otro extremo con Intro pulsada, y en volver al soltarla.
constexpr double kHoldMs = 1400;
constexpr double kBackMs = 260;

// El vídeo va en los recursos: el reproductor lo lee de una copia en la caché.
QString introFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/intro.webm");
    QFile res(QStringLiteral(":/kaint/video/intro.webm"));
    if (!res.exists())
        return {};
    if (QFileInfo(path).size() != res.size()) {
        QFile::remove(path);
        if (!res.copy(path))
            return {};
    }
    return path;
}
} // namespace

void Intro::start(QWidget* window, bool always, const QString& customVideo, std::function<void()> done)
{
    QSettings settings;
    const bool first = !settings.value(QStringLiteral("intro/shown")).toBool();
    const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
#ifdef KAINT_HAVE_QTMULTIMEDIA
    if ((first || always) && !offscreen && window) {
        settings.setValue(QStringLiteral("intro/shown"), true);
        const QString video = always && !customVideo.isEmpty() && QFileInfo::exists(customVideo) ? customVideo : introFile();
        if (!video.isEmpty()) {
            new Intro(window, always, video, std::move(done));
            return;
        }
    }
#else
    Q_UNUSED(first);
    Q_UNUSED(always);
    Q_UNUSED(customVideo);
    Q_UNUSED(offscreen);
#endif
    if (done)
        done();
}

Intro::Intro(QWidget* window, bool skippable, const QString& video, std::function<void()> done)
    : QWidget(window)
    , m_skippable(skippable)
    , m_done(std::move(done))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(window->rect());
    window->installEventFilter(this);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &Intro::step);
#ifndef KAINT_HAVE_QTMULTIMEDIA
    Q_UNUSED(video);
#else
    auto* player = new QMediaPlayer(this);
    auto* audio = new QAudioOutput(this);
    auto* sink = new QVideoSink(this);
    player->setAudioOutput(audio);
    player->setVideoSink(sink);
    m_player = player;
    connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& frame) {
        const QImage image = frame.toImage();
        if (!image.isNull()) {
            m_frame = image;
            update();
        }
    });
    // Al terminar, desde su último fotograma; si no se puede reproducir, se va sin más.
    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia)
            finish();
    });
    connect(player, &QMediaPlayer::errorOccurred, this, [this] { finish(); });
    player->setSource(QUrl::fromLocalFile(video));
    player->play();
#endif
    show();
    raise();
    setFocus();
    m_clock.start();
    m_ticker.start();
}

bool Intro::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == parentWidget() && e->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, e);
}

void Intro::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_frame.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QSize s = m_frame.size().scaled(size(), Qt::KeepAspectRatio);
        p.drawImage(QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s), m_frame);
    }
    // La línea de saltar, de un extremo a otro.
    if (m_progress > 0) {
        p.fillRect(QRect(0, height() - 5, width(), 5), QColor(255, 255, 255, 40));
        p.fillRect(QRect(0, height() - 5, int(width() * std::min(1.0, m_progress)), 5), QColor(0, 120, 215));
    }
}

void Intro::keyPressEvent(QKeyEvent* e)
{
    // Intro o Espacio, mantenidas, la saltan.
    if (m_skippable && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space)) {
        m_holding = true;
        e->accept();
        return;
    }
    e->accept();
}

void Intro::keyReleaseEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) && !e->isAutoRepeat())
        m_holding = false;
    e->accept();
}

void Intro::step()
{
    const double dt = std::max<qint64>(1, m_clock.restart());
    const double before = m_progress;
    m_progress = m_holding ? m_progress + dt / kHoldMs : std::max(0.0, m_progress - dt / kBackMs);
    if (m_progress >= 1.0) {
        m_progress = 1.0;
        finish();
    }
    if (m_progress != before)
        update(QRect(0, height() - 6, width(), 6));
}

void Intro::finish()
{
    if (m_finishing)
        return;
    m_finishing = true;
    m_holding = false;
#ifdef KAINT_HAVE_QTMULTIMEDIA
    if (auto* player = qobject_cast<QMediaPlayer*>(m_player))
        player->pause();
#endif
    // Desde lo último que se vio, se desvanece y deja ver Kaint.
    auto end = [this] {
        if (m_done)
            m_done();
        m_done = nullptr;
        close();
    };
    if (!Animations::enabled()) {
        end();
        return;
    }
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);
    Animations::run(this, 1.0, 0.0, 700, QEasingCurve::OutCubic, [effect](qreal v) { effect->setOpacity(v); }, end);
}
