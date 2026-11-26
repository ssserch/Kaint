#include "app/VideoFrames.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#elif defined(KAINT_HAVE_QTMULTIMEDIA)
#include <QMediaPlayer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#endif

namespace {

const QStringList& videoExtensions()
{
    static const QStringList list{QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("webm"),
                                  QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("wmv"), QStringLiteral("mpg"),
                                  QStringLiteral("mpeg"), QStringLiteral("ogv"), QStringLiteral("3gp")};
    return list;
}

// Como mucho unos 40 fotogramas por segundo (los vídeos de 60 salen a 30): más no se nota en el
// fondo y gasta procesador. Con 33 ms justos, los de 30 perdían la mitad por las pequeñas esperas.
constexpr auto kMinFrameGap = std::chrono::milliseconds(25);

QImage fitted(QImage image, int maxSide)
{
    if (maxSide > 0 && (image.width() > maxSide || image.height() > maxSide))
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

} // namespace

#ifdef Q_OS_WIN

// ---------------------------------------------------------------------------
// Windows: Media Foundation (Source Reader con conversión a RGB32), en un hilo propio.

namespace {

template <typename T>
void release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Media Foundation se carga al usarlo: las ediciones N de Windows no lo traen (sin el Media Feature
// Pack) y, enlazado directamente, Kaint ni siquiera arrancaba en ellas. Sin él, no hay fondos de vídeo.
struct MediaFoundation {
    using Startup = HRESULT(WINAPI*)(ULONG, DWORD);
    using Shutdown = HRESULT(WINAPI*)();
    using CreateAttributes = HRESULT(WINAPI*)(IMFAttributes**, UINT32);
    using CreateMediaType = HRESULT(WINAPI*)(IMFMediaType**);
    using GetStride = HRESULT(WINAPI*)(DWORD, DWORD, LONG*);
    using CreateReader = HRESULT(WINAPI*)(LPCWSTR, IMFAttributes*, IMFSourceReader**);
    Startup startup = nullptr;
    Shutdown shutdown = nullptr;
    CreateAttributes createAttributes = nullptr;
    CreateMediaType createMediaType = nullptr;
    GetStride stride = nullptr;
    CreateReader createReader = nullptr;
    bool ok = false;
};

const MediaFoundation& mediaFoundation()
{
    static const MediaFoundation api = [] {
        MediaFoundation a;
        const HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const HMODULE readwrite = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !readwrite)
            return a;
        auto get = [](HMODULE m, const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); };
        a.startup = reinterpret_cast<MediaFoundation::Startup>(get(plat, "MFStartup"));
        a.shutdown = reinterpret_cast<MediaFoundation::Shutdown>(get(plat, "MFShutdown"));
        a.createAttributes = reinterpret_cast<MediaFoundation::CreateAttributes>(get(plat, "MFCreateAttributes"));
        a.createMediaType = reinterpret_cast<MediaFoundation::CreateMediaType>(get(plat, "MFCreateMediaType"));
        a.stride = reinterpret_cast<MediaFoundation::GetStride>(get(plat, "MFGetStrideForBitmapInfoHeader"));
        a.createReader = reinterpret_cast<MediaFoundation::CreateReader>(get(readwrite, "MFCreateSourceReaderFromURL"));
        a.ok = a.startup && a.shutdown && a.createAttributes && a.createMediaType && a.stride && a.createReader;
        return a;
    }();
    return api;
}

QString hresultText(HRESULT hr)
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(hr), 0,
                   reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    const QString message = text ? QString::fromWCharArray(text).trimmed() : QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    LocalFree(text);
    return message;
}

// Tamaño, paso entre filas y zona visible del formato de salida (cambian si cambia el vídeo).
struct FrameFormat {
    UINT32 width = 0;
    UINT32 height = 0;
    LONG stride = 0;
    QRect visible;
};

bool readFrameFormat(IMFSourceReader* reader, const MediaFoundation& api, FrameFormat* f)
{
    IMFMediaType* current = nullptr;
    if (FAILED(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current)))
        return false;
    MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &f->width, &f->height);
    UINT32 s = 0;
    if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)))
        f->stride = LONG(INT32(s));
    else
        api.stride(MFVideoFormat_RGB32.Data1, f->width, &f->stride);
    f->visible = QRect(0, 0, int(f->width), int(f->height));
    MFVideoArea area{};
    if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr)))
        f->visible = QRect(area.OffsetX.value, area.OffsetY.value, int(area.Area.cx), int(area.Area.cy))
                         .intersected(QRect(0, 0, int(f->width), int(f->height)));
    current->Release();
    return f->width > 0 && f->height > 0 && f->stride != 0;
}

// Un lector del primer vídeo del archivo que da los fotogramas en RGB32 (MF ya iniciado).
HRESULT openReader(const QString& path, const MediaFoundation& api, IMFAttributes** attributes, IMFSourceReader** reader,
                   FrameFormat* format)
{
    HRESULT hr = api.createAttributes(attributes, 1);
    if (SUCCEEDED(hr))
        hr = (*attributes)->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr))
        hr = api.createReader(reinterpret_cast<LPCWSTR>(path.utf16()), *attributes, reader);
    if (SUCCEEDED(hr)) {
        (*reader)->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        hr = (*reader)->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
    }
    IMFMediaType* type = nullptr;
    if (SUCCEEDED(hr))
        hr = api.createMediaType(&type);
    if (SUCCEEDED(hr))
        hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr))
        hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr))
        hr = (*reader)->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
    release(type);
    if (SUCCEEDED(hr) && !readFrameFormat(*reader, api, format))
        hr = MF_E_INVALIDMEDIATYPE;
    return hr;
}

// El fotograma de una muestra, opaco (Media Foundation no siempre deja el alfa a 255) o nulo.
QImage frameImage(IMFSample* sample, const FrameFormat& f)
{
    QImage image;
    IMFMediaBuffer* buffer = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
        return image;
    BYTE* data = nullptr;
    DWORD length = 0;
    if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
        const qsizetype rowBytes = qsizetype(std::abs(f.stride));
        if (qsizetype(length) >= rowBytes * qsizetype(f.height) && rowBytes >= qsizetype(f.width) * 4 && !f.visible.isEmpty()) {
            image = QImage(f.visible.size(), QImage::Format_RGB32);
            for (int y = 0; y < f.visible.height(); ++y) {
                const int row = f.visible.top() + y;
                // Paso negativo: las filas van de abajo arriba.
                const BYTE* line = f.stride > 0 ? data + qsizetype(row) * rowBytes : data + qsizetype(int(f.height) - 1 - row) * rowBytes;
                auto* out = reinterpret_cast<quint32*>(image.scanLine(y));
                std::memcpy(out, line + qsizetype(f.visible.left()) * 4, size_t(f.visible.width()) * 4);
                for (int x = 0; x < f.visible.width(); ++x)
                    out[x] |= 0xff000000u;
            }
        }
        buffer->Unlock();
    }
    buffer->Release();
    return image;
}

} // namespace

struct VideoFrames::Private {
    QString path;
    int maxSide = 0;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool paused = false;
    std::atomic_bool stop{false};
    QString error;
};

VideoFrames::VideoFrames(const QString& path, int maxSide, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->path = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    d->maxSide = maxSide;
    Private* p = d.get();
    d->thread = std::thread([this, p] {
        // Prioridad baja: el fondo no le quita tiempo a la interfaz ni a los efectos.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const MediaFoundation& api = mediaFoundation();
        const bool mf = api.ok && SUCCEEDED(api.startup(MF_VERSION, MFSTARTUP_LITE));
        IMFAttributes* attributes = nullptr;
        IMFSourceReader* reader = nullptr;
        auto fail = [&](const QString& why) {
            {
                std::lock_guard lock(p->mutex);
                p->error = why;
            }
            QMetaObject::invokeMethod(this, [this, why] { emit failed(why); }, Qt::QueuedConnection);
        };
        FrameFormat format;
        HRESULT hr = mf ? openReader(p->path, api, &attributes, &reader, &format) : E_FAIL;
        if (!api.ok) {
            fail(tr("Windows no tiene Media Foundation (en las ediciones N se instala con el «Media Feature Pack»)."));
        } else if (FAILED(hr)) {
            fail(hresultText(hr));
        } else {
            using Clock = std::chrono::steady_clock;
            Clock::time_point start = Clock::now(), lastFrame = Clock::time_point();
            LONGLONG firstStamp = -1;
            int emptyReads = 0;
            while (!p->stop) {
                {
                    std::unique_lock lock(p->mutex);
                    if (p->paused) {
                        const auto pausedAt = Clock::now();
                        p->wake.wait(lock, [p] { return !p->paused || p->stop; });
                        // Sigue donde estaba, sin correr para alcanzar el tiempo perdido.
                        start += Clock::now() - pausedAt;
                        continue;
                    }
                }
                DWORD flags = 0;
                LONGLONG stamp = 0;
                IMFSample* sample = nullptr;
                hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &stamp, &sample);
                if (FAILED(hr)) {
                    fail(hresultText(hr));
                    break;
                }
                if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                    readFrameFormat(reader, api, &format);
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                    release(sample);
                    // Otra vez desde el principio (si el vídeo no tenía fotogramas, se deja).
                    if (firstStamp < 0 || ++emptyReads > 3)
                        break;
                    PROPVARIANT position;
                    PropVariantInit(&position);
                    position.vt = VT_I8;
                    position.hVal.QuadPart = 0;
                    reader->SetCurrentPosition(GUID_NULL, position);
                    PropVariantClear(&position);
                    firstStamp = -1;
                    start = Clock::now();
                    continue;
                }
                if (!sample)
                    continue;
                emptyReads = 0;
                if (firstStamp < 0)
                    firstStamp = stamp;
                // A su tiempo: la marca del fotograma (en unidades de 100 ns) desde el principio.
                const auto due = start + std::chrono::microseconds((stamp - firstStamp) / 10);
                const auto now = Clock::now();
                if (due > now) {
                    std::unique_lock lock(p->mutex);
                    p->wake.wait_until(lock, due, [p] { return p->stop.load(); });
                } else if (now - due > std::chrono::milliseconds(500)) {
                    // Muy atrasado (el equipo iba lento): se vuelve a contar desde aquí.
                    start = now - std::chrono::microseconds((stamp - firstStamp) / 10);
                }
                if (p->stop) {
                    release(sample);
                    break;
                }
                // Se salta si el anterior acaba de salir (vídeos de 60 fotogramas por segundo o más).
                if (Clock::now() - lastFrame < kMinFrameGap) {
                    release(sample);
                    continue;
                }
                QImage image = frameImage(sample, format);
                release(sample);
                if (!image.isNull()) {
                    image = fitted(std::move(image), p->maxSide);
                    lastFrame = Clock::now();
                    QMetaObject::invokeMethod(this, [this, image] { emit frame(image); }, Qt::QueuedConnection);
                }
            }
        }
        release(reader);
        release(attributes);
        if (mf)
            api.shutdown();
        if (SUCCEEDED(co))
            CoUninitialize();
    });
}

VideoFrames::~VideoFrames()
{
    {
        std::lock_guard lock(d->mutex);
        d->stop = true;
        d->paused = false;
    }
    d->wake.notify_all();
    if (d->thread.joinable())
        d->thread.join();
}

bool VideoFrames::supported()
{
    return mediaFoundation().ok;
}

void VideoFrames::setPaused(bool paused)
{
    {
        std::lock_guard lock(d->mutex);
        d->paused = paused;
    }
    d->wake.notify_all();
}

QString VideoFrames::error() const
{
    std::lock_guard lock(d->mutex);
    return d->error;
}

bool VideoFrames::canDecode()
{
    return mediaFoundation().ok;
}

bool VideoFrames::decode(const QString& path, double maxFps, int maxFrames, QList<QImage>* frames, double* fps,
                         const std::function<bool(int, int)>& progress, QString* error)
{
    if (error)
        error->clear();
    if (!mediaFoundation().ok) {
        if (error)
            *error = tr("Windows no tiene Media Foundation (en las ediciones N se instala con el «Media Feature Pack»).");
        return false;
    }
    // Se lee en otro hilo: este sigue atendiendo la interfaz (y la barra de progreso).
    struct Shared {
        std::mutex mutex;
        QList<QImage> frames;
        int expected = 0;
        double fps = 0;
        QString error;
        std::atomic_bool cancel{false};
        std::atomic_bool done{false};
    };
    auto s = std::make_shared<Shared>();
    const QString file = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    maxFrames = std::max(1, maxFrames);
    std::thread worker([s, file, maxFps, maxFrames] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const MediaFoundation& api = mediaFoundation();
        const bool mf = SUCCEEDED(api.startup(MF_VERSION, MFSTARTUP_LITE));
        IMFAttributes* attributes = nullptr;
        IMFSourceReader* reader = nullptr;
        FrameFormat format;
        HRESULT hr = mf ? openReader(file, api, &attributes, &reader, &format) : E_FAIL;
        if (FAILED(hr)) {
            std::lock_guard lock(s->mutex);
            s->error = hresultText(hr);
        } else {
            // Los fotogramas por segundo del vídeo, su giro (los de móvil en vertical) y su duración.
            double nativeFps = 0;
            UINT32 rotation = 0;
            IMFMediaType* native = nullptr;
            if (SUCCEEDED(reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &native))) {
                UINT32 num = 0, den = 0;
                if (SUCCEEDED(MFGetAttributeRatio(native, MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
                    nativeFps = double(num) / den;
                UINT32 degrees = 0;
                if (SUCCEEDED(native->GetUINT32(MF_MT_VIDEO_ROTATION, &degrees)))
                    rotation = degrees % 360;
                native->Release();
            }
            double duration = 0;
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value))
                && value.vt == VT_UI8)
                duration = double(value.uhVal.QuadPart) / 1e7;
            PropVariantClear(&value);
            double rate = nativeFps > 0 ? std::min(maxFps, nativeFps) : maxFps;
            if (duration > 0 && rate * duration > maxFrames)
                rate = maxFrames / duration;
            rate = std::max(0.5, rate);
            {
                std::lock_guard lock(s->mutex);
                s->fps = rate;
                s->expected = duration > 0 ? std::min(maxFrames, int(std::ceil(duration * rate - 1e-6))) : 0;
            }
            // Como el filtro fps de FFmpeg: el fotograma n de la salida (a n / rate segundos) es el
            // que se ve en ese momento (uno se repite si el vídeo va a menos, o se salta si va a más).
            QImage held;
            double heldAt = 0;
            LONGLONG first = -1;
            int next = 0;
            auto emitUntil = [&](double until) {
                // (Medio milisegundo antes: uno que cae justo en su momento, con redondeos, es el suyo.)
                while (!held.isNull() && next < maxFrames && next / rate < until - 0.0005) {
                    std::lock_guard lock(s->mutex);
                    s->frames.append(held);
                    ++next;
                }
            };
            while (!s->cancel && next < maxFrames) {
                DWORD flags = 0;
                LONGLONG stamp = 0;
                IMFSample* sample = nullptr;
                hr = reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, &stamp, &sample);
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                    release(sample);
                    std::lock_guard lock(s->mutex);
                    s->error = hresultText(FAILED(hr) ? hr : E_FAIL);
                    break;
                }
                if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                    readFrameFormat(reader, api, &format);
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                    release(sample);
                    break;
                }
                if (!sample)
                    continue;
                if (first < 0)
                    first = stamp;
                const double at = double(stamp - first) / 1e7;
                emitUntil(at);
                QImage image = frameImage(sample, format);
                release(sample);
                if (image.isNull())
                    continue;
                if (rotation != 0)
                    image = image.transformed(QTransform().rotate(rotation));
                // (Ya es opaco: RGB32 y ARGB32 premultiplicado son entonces los mismos bytes.)
                if (!image.reinterpretAsFormat(QImage::Format_ARGB32_Premultiplied))
                    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                held = image;
                heldAt = at;
            }
            // El último, hasta el final del vídeo.
            if (!s->cancel && !held.isNull()) {
                emitUntil(duration > 0 ? duration - double(std::max<LONGLONG>(0, first)) / 1e7 : heldAt + 1.0 / rate);
                std::lock_guard lock(s->mutex);
                if (s->frames.isEmpty())
                    s->frames.append(held);
            }
        }
        release(reader);
        release(attributes);
        if (mf)
            api.shutdown();
        if (SUCCEEDED(co))
            CoUninitialize();
        s->done = true;
    });
    bool canceled = false;
    while (!s->done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        int count = 0, expected = 0;
        {
            std::lock_guard lock(s->mutex);
            count = int(s->frames.size());
            expected = s->expected;
        }
        if (progress && !canceled && !progress(count, expected)) {
            canceled = true;
            s->cancel = true;
        }
    }
    worker.join();
    if (canceled) {
        if (error)
            error->clear();
        return false;
    }
    if (s->frames.isEmpty()) {
        if (error)
            *error = s->error.isEmpty() ? tr("El vídeo no tiene fotogramas.") : s->error;
        return false;
    }
    *frames = std::move(s->frames);
    *fps = s->fps;
    return true;
}

#elif defined(KAINT_HAVE_QTMULTIMEDIA)

// ---------------------------------------------------------------------------
// Linux y otros: Qt Multimedia, sin salida de sonido.

struct VideoFrames::Private {
    QMediaPlayer* player = nullptr;
    QVideoSink* sink = nullptr;
    int maxSide = 0;
    std::chrono::steady_clock::time_point lastFrame;
    QString error;
};

VideoFrames::VideoFrames(const QString& path, int maxSide, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->maxSide = maxSide;
    d->player = new QMediaPlayer(this);
    d->sink = new QVideoSink(this);
    d->player->setVideoSink(d->sink);
    d->player->setLoops(QMediaPlayer::Infinite);
    connect(d->sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame& f) {
        const auto now = std::chrono::steady_clock::now();
        if (!f.isValid() || now - d->lastFrame < kMinFrameGap)
            return;
        d->lastFrame = now;
        const QImage image = fitted(f.toImage(), d->maxSide);
        if (!image.isNull())
            emit frame(image);
    });
    connect(d->player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) {
        d->error = text;
        emit failed(text);
    });
    d->player->setSource(QUrl::fromLocalFile(path));
    d->player->play();
}

VideoFrames::~VideoFrames()
{
    d->player->stop();
}

bool VideoFrames::supported()
{
    return true;
}

void VideoFrames::setPaused(bool paused)
{
    if (paused)
        d->player->pause();
    else
        d->player->play();
}

QString VideoFrames::error() const
{
    return d->error;
}

#else

// ---------------------------------------------------------------------------
// Sin reproductor de vídeo.

struct VideoFrames::Private {
    QString error;
};

VideoFrames::VideoFrames(const QString& path, int maxSide, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    Q_UNUSED(path);
    Q_UNUSED(maxSide);
    d->error = tr("Esta compilación de Kaint no reproduce vídeos (falta Qt Multimedia).");
    QMetaObject::invokeMethod(this, [this] { emit failed(d->error); }, Qt::QueuedConnection);
}

VideoFrames::~VideoFrames() = default;

bool VideoFrames::supported()
{
    return false;
}

void VideoFrames::setPaused(bool)
{
}

QString VideoFrames::error() const
{
    return d->error;
}

#endif

#ifndef Q_OS_WIN
bool VideoFrames::canDecode()
{
    return false;
}

bool VideoFrames::decode(const QString&, double, int, QList<QImage>*, double*, const std::function<bool(int, int)>&, QString* error)
{
    if (error)
        *error = tr("Para abrir vídeo hace falta FFmpeg.");
    return false;
}
#endif

bool VideoFrames::isVideo(const QString& path)
{
    return videoExtensions().contains(QFileInfo(path).suffix().toLower());
}

QString VideoFrames::filePatterns()
{
    QStringList patterns;
    for (const QString& ext : videoExtensions())
        patterns << QStringLiteral("*.") + ext;
    return patterns.join(QLatin1Char(' '));
}
