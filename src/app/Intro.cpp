#include "app/Intro.h"

#include "app/Animations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMutex>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QVariantAnimation>
#include <QWaitCondition>

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

// Cada fotograma del vídeo (1080p a 60 fps) se pasaba a QImage y se escalaba con suavizado en el
// hilo de la interfaz: no daba tiempo y la intro iba a tirones (sobre todo en Windows, mientras
// Kaint termina de abrirse). Ahora se convierte y se escala al tamaño de la ventana en un hilo
// aparte; solo se queda el último que llega (si uno no se ha dibujado aún, se salta) y la interfaz
// solo lo copia en la pantalla.
struct IntroFrames {
    QMutex mutex;
    QWaitCondition wake;
#ifdef KAINT_HAVE_QTMULTIMEDIA
    QVideoFrame pending;
#endif
    bool hasPending = false;
    bool closing = false;
    QSize target;  // en píxeles de la pantalla
    qreal dpr = 1;
    QImage ready;
    QWidget* owner = nullptr;
    std::unique_ptr<QThread> thread;
};

namespace {
#ifdef KAINT_HAVE_QTMULTIMEDIA
// El fotograma escalado (sin deformar) para caber en 'target'.
QImage fittedFrame(const QImage& image, QSize target, qreal dpr)
{
    const QSize size = image.size().scaled(target, Qt::KeepAspectRatio);
    QImage out;
    if (size == image.size() || size.isEmpty()) {
        out = image.convertToFormat(QImage::Format_RGB32);
    } else {
        out = QImage(size, QImage::Format_RGB32);
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(QRect(QPoint(0, 0), size), image);
    }
    out.setDevicePixelRatio(dpr);
    return out;
}

void convertFrames(IntroFrames* f)
{
    QMutexLocker lock(&f->mutex);
    for (;;) {
        while (!f->closing && !f->hasPending)
            f->wake.wait(&f->mutex);
        if (f->closing)
            return;
        QVideoFrame frame = f->pending;
        f->pending = QVideoFrame();
        f->hasPending = false;
        const QSize target = f->target;
        const qreal dpr = f->dpr;
        lock.unlock();
        QImage image = frame.toImage();
        frame = QVideoFrame();
        if (!image.isNull())
            image = fittedFrame(image, target.isEmpty() ? image.size() : target, dpr);
        lock.relock();
        if (f->closing)
            return;
        if (image.isNull())
            continue;
        f->ready = image;
        QMetaObject::invokeMethod(f->owner, [owner = f->owner] { owner->update(); }, Qt::QueuedConnection);
    }
}
#endif
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
    , m_frames(std::make_shared<IntroFrames>())
    , m_skippable(skippable)
    , m_done(std::move(done))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(window->rect());
    window->installEventFilter(this);
    // Intro y Espacio se escuchan en toda la aplicación (filtro en qApp), tenga quien tenga el foco:
    // al arrancar, la ventana aún no estaba activa al pedirlo y el lienzo se lo quedaba después, así
    // que las teclas no llegaban hasta hacer varios clics.
    qApp->installEventFilter(this);
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
    m_frames->owner = this;
    m_frames->dpr = devicePixelRatioF();
    m_frames->target = (QSizeF(size()) * m_frames->dpr).toSize();
    m_frames->thread.reset(QThread::create(convertFrames, m_frames.get()));
    m_frames->thread->start();
    // (Llega desde el hilo del reproductor: solo se guarda el fotograma y se avisa al de convertir.)
    connect(sink, &QVideoSink::videoFrameChanged, sink, [frames = m_frames](const QVideoFrame& frame) {
        {
            QMutexLocker lock(&frames->mutex);
            if (frames->closing)
                return;
            frames->pending = frame;
            frames->hasPending = true;
        }
        frames->wake.wakeOne();
    }, Qt::DirectConnection);
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
    window->activateWindow();
    setFocus();
    m_clock.start();
    m_ticker.start();
}

Intro::~Intro()
{
    {
        QMutexLocker lock(&m_frames->mutex);
        m_frames->closing = true;
#ifdef KAINT_HAVE_QTMULTIMEDIA
        m_frames->pending = QVideoFrame();
#endif
    }
    m_frames->wake.wakeAll();
    if (m_frames->thread)
        m_frames->thread->wait();
}

void Intro::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    QMutexLocker lock(&m_frames->mutex);
    m_frames->dpr = devicePixelRatioF();
    m_frames->target = (QSizeF(size()) * m_frames->dpr).toSize();
}

// Las teclas que saltan el vídeo (manteniéndolas pulsadas).
static bool isSkipKey(const QKeyEvent* e)
{
    return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space;
}

bool Intro::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == parentWidget() && e->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    // Al activarse la ventana (al arrancar, después de mostrarse), el foco vuelve al vídeo.
    if (watched == parentWidget() && e->type() == QEvent::WindowActivate && !m_finishing)
        setFocus();
    // Intro y Espacio en cualquier widget de esta ventana mientras se ve el vídeo: lo saltan (y no
    // llegan a los atajos de debajo).
    const QEvent::Type type = e->type();
    if ((type == QEvent::KeyPress || type == QEvent::KeyRelease || type == QEvent::ShortcutOverride) && !m_finishing && isVisible()) {
        auto* target = qobject_cast<QWidget*>(watched);
        auto* key = static_cast<QKeyEvent*>(e);
        if (target && target->window() == window() && isSkipKey(key)) {
            if (type == QEvent::KeyPress)
                keyPressEvent(key);
            else if (type == QEvent::KeyRelease)
                keyReleaseEvent(key);
            e->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, e);
}

void Intro::paintEvent(QPaintEvent*)
{
    {
        QMutexLocker lock(&m_frames->mutex);
        if (!m_frames->ready.isNull())
            m_frame = m_frames->ready;
    }
    QPainter p(this);
    QRegion bars(rect());
    if (!m_frame.isNull()) {
        // Ya viene a la medida: se copia tal cual (solo tras cambiar el tamaño de la ventana se
        // escala aquí, hasta que llega el siguiente).
        const QSizeF logical = QSizeF(m_frame.size()) / m_frame.devicePixelRatio();
        const QSize s = logical.toSize().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
        if (target.size() == logical.toSize()) {
            p.drawImage(target.topLeft(), m_frame);
        } else {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(target, m_frame);
        }
        bars -= target;
    }
    for (const QRect& r : bars)
        p.fillRect(r, Qt::black);
    // La línea de saltar, de un extremo a otro.
    if (m_progress > 0) {
        p.fillRect(QRect(0, height() - 5, width(), 5), QColor(255, 255, 255, 40));
        p.fillRect(QRect(0, height() - 5, int(width() * std::min(1.0, m_progress)), 5), QColor(0, 120, 215));
    }
}

void Intro::keyPressEvent(QKeyEvent* e)
{
    // Intro o Espacio, mantenidas, la saltan.
    if (m_skippable && isSkipKey(e)) {
        m_holding = true;
        e->accept();
        return;
    }
    e->accept();
}

void Intro::keyReleaseEvent(QKeyEvent* e)
{
    if (isSkipKey(e) && !e->isAutoRepeat())
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
