#include "app/VideoFrames.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <atomic>
#include <chrono>
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
        IMFMediaType* type = nullptr;
        auto fail = [&](const QString& why) {
            {
                std::lock_guard lock(p->mutex);
                p->error = why;
            }
            QMetaObject::invokeMethod(this, [this, why] { emit failed(why); }, Qt::QueuedConnection);
        };
        HRESULT hr = mf ? api.createAttributes(&attributes, 1) : E_FAIL;
        if (SUCCEEDED(hr))
            hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr))
            hr = api.createReader(reinterpret_cast<LPCWSTR>(p->path.utf16()), attributes, &reader);
        if (SUCCEEDED(hr)) {
            reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            hr = reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);
        }
        if (SUCCEEDED(hr))
            hr = api.createMediaType(&type);
        if (SUCCEEDED(hr))
            hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr))
            hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr))
            hr = reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type);
        release(type);

        // Tamaño, paso entre filas y zona visible del formato de salida (cambian si cambia el vídeo).
        UINT32 width = 0, height = 0;
        LONG stride = 0;
        QRect visible;
        auto readFormat = [&] {
            IMFMediaType* current = nullptr;
            if (FAILED(reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current)))
                return false;
            MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &width, &height);
            UINT32 s = 0;
            if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)))
                stride = LONG(INT32(s));
            else
                api.stride(MFVideoFormat_RGB32.Data1, width, &stride);
            visible = QRect(0, 0, int(width), int(height));
            MFVideoArea area{};
            if (SUCCEEDED(current->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), nullptr)))
                visible = QRect(area.OffsetX.value, area.OffsetY.value, int(area.Area.cx), int(area.Area.cy))
                              .intersected(QRect(0, 0, int(width), int(height)));
            current->Release();
            return width > 0 && height > 0 && stride != 0;
        };
        if (SUCCEEDED(hr) && !readFormat())
            hr = MF_E_INVALIDMEDIATYPE;
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
                    readFormat();
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
                IMFMediaBuffer* buffer = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                    BYTE* data = nullptr;
                    DWORD length = 0;
                    if (SUCCEEDED(buffer->Lock(&data, nullptr, &length))) {
                        const qsizetype rowBytes = qsizetype(std::abs(stride));
                        if (qsizetype(length) >= rowBytes * qsizetype(height) && rowBytes >= qsizetype(width) * 4) {
                            QImage image(visible.size(), QImage::Format_RGB32);
                            for (int y = 0; y < visible.height(); ++y) {
                                const int row = visible.top() + y;
                                // Paso negativo: las filas van de abajo arriba.
                                const BYTE* line = stride > 0 ? data + qsizetype(row) * rowBytes
                                                              : data + qsizetype(int(height) - 1 - row) * rowBytes;
                                std::memcpy(image.scanLine(y), line + qsizetype(visible.left()) * 4, size_t(visible.width()) * 4);
                            }
                            buffer->Unlock();
                            image = fitted(std::move(image), p->maxSide);
                            lastFrame = Clock::now();
                            QMetaObject::invokeMethod(this, [this, image] { emit frame(image); }, Qt::QueuedConnection);
                        } else {
                            buffer->Unlock();
                        }
                    }
                    buffer->Release();
                }
                release(sample);
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
