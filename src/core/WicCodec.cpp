#include "core/WicCodec.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QColorSpace>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QTransform>

#include <windows.h>
#include <objidl.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#endif

namespace Wic {

#ifdef Q_OS_WIN
namespace {

// Puntero a una interfaz COM que se libera solo.
template <typename T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T* get() const { return m_p; }
    T* operator->() const { return m_p; }
    T** put()
    {
        reset();
        return &m_p;
    }
    explicit operator bool() const { return m_p != nullptr; }
    void reset()
    {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }

private:
    T* m_p = nullptr;
};

// COM en el hilo que llama: el de la interfaz ya lo tiene (Qt lo inicia); los demás (guardar en
// segundo plano, miniaturas) lo inician y lo cierran aquí.
class ComScope {
public:
    ComScope()
        : m_owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
    }
    ~ComScope()
    {
        if (m_owned)
            CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool m_owned;
};

bool createFactory(Com<IWICImagingFactory>& factory)
{
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())));
}

// Opciones de compresión del códec HEIF (WICHeifCompressionOption).
constexpr BYTE kHeifHevc = 2;
constexpr BYTE kHeifAv1 = 3;

struct Codecs {
    // Extensión sin punto, en minúsculas → formato contenedor del códec.
    QHash<QString, GUID> decoders;
    QHash<QString, GUID> encoders;
};

void enumerate(IWICImagingFactory* factory, DWORD type, QHash<QString, GUID>& out)
{
    Com<IEnumUnknown> list;
    if (FAILED(factory->CreateComponentEnumerator(type, WICComponentEnumerateDefault, list.put())))
        return;
    for (;;) {
        Com<IUnknown> item;
        ULONG fetched = 0;
        if (list->Next(1, item.put(), &fetched) != S_OK || !fetched)
            break;
        Com<IWICBitmapCodecInfo> info;
        if (FAILED(item->QueryInterface(IID_PPV_ARGS(info.put()))))
            continue;
        GUID container;
        UINT length = 0;
        if (FAILED(info->GetContainerFormat(&container)) || FAILED(info->GetFileExtensions(0, nullptr, &length)) || !length)
            continue;
        std::vector<WCHAR> text(length + 1, 0);
        if (FAILED(info->GetFileExtensions(length, text.data(), &length)))
            continue;
        for (const QString& ext : QString::fromWCharArray(text.data()).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString e = ext.trimmed().toLower().remove(QLatin1Char('.'));
            if (!e.isEmpty() && !out.contains(e))
                out.insert(e, container);
        }
    }
}

// Los códecs instalados (se miran una vez: una extensión de Store nueva se ve al reiniciar Kaint).
const Codecs& codecs()
{
    static const Codecs c = [] {
        ComScope com;
        Codecs out;
        Com<IWICImagingFactory> factory;
        if (createFactory(factory)) {
            enumerate(factory.get(), WICDecoder, out.decoders);
            enumerate(factory.get(), WICEncoder, out.encoders);
        }
        return out;
    }();
    return c;
}

// Las extensiones con que Windows registra cada formato que Kaint le pide.
QStringList aliases(const QString& suffix)
{
    if (suffix == QLatin1String("jxr"))
        return {QStringLiteral("jxr"), QStringLiteral("wdp"), QStringLiteral("hdp"), QStringLiteral("wmp")};
    if (suffix == QLatin1String("heic"))
        return {QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("hif")};
    if (suffix == QLatin1String("avif"))
        return {QStringLiteral("avif")};
    if (suffix == QLatin1String("jxl"))
        return {QStringLiteral("jxl")};
    return {};
}

std::optional<GUID> containerFor(const QHash<QString, GUID>& table, const QString& suffix)
{
    for (const QString& a : aliases(suffix))
        if (const auto it = table.constFind(a); it != table.cend())
            return *it;
    return std::nullopt;
}

// Cómo se escribe cada formato: el códec y, con el de HEIF, la compresión (AVIF es HEIF con AV1).
struct Writer {
    GUID container;
    int heifCompression = -1;
};

std::optional<Writer> writerFor(const QString& suffix)
{
    const Codecs& c = codecs();
    if (suffix == QLatin1String("avif")) {
        if (const auto own = containerFor(c.encoders, suffix))
            return Writer{*own, -1};
        if (const auto heif = containerFor(c.encoders, QStringLiteral("heic")))
            return Writer{*heif, kHeifAv1};
        return std::nullopt;
    }
    if (const auto container = containerFor(c.encoders, suffix))
        return Writer{*container, suffix == QLatin1String("heic") ? int(kHeifHevc) : -1};
    return std::nullopt;
}

QString hresultText(HRESULT hr)
{
    if (hr == WINCODEC_ERR_COMPONENTNOTFOUND || hr == WINCODEC_ERR_UNKNOWNIMAGEFORMAT)
        return QObject::tr("Windows no tiene el códec de este formato. Instala la extensión de imagen correspondiente "
                           "desde Microsoft Store (HEIF, HEVC, AV1 o JPEG XL).");
    // MF_E_TOPO_CODEC_NOT_FOUND: está el lector de HEIF, pero no el códec de vídeo de la imagen.
    if (hr == HRESULT(0xC00D5212))
        return QObject::tr("Windows tiene el lector de HEIF, pero le falta el códec de vídeo de esta imagen: «Extensiones "
                           "de vídeo HEVC» para HEIC o «Extensión de vídeo AV1» para AVIF, en Microsoft Store.");
    LPWSTR buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, DWORD(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = n && buffer ? QString::fromWCharArray(buffer, int(n)).trimmed() : QString();
    if (buffer)
        LocalFree(buffer);
    const QString code = QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    return text.isEmpty() ? QObject::tr("Error de Windows %1.").arg(code) : QStringLiteral("%1 (%2)").arg(text, code);
}

bool failed(HRESULT hr, QString* error)
{
    if (SUCCEEDED(hr))
        return false;
    if (error)
        *error = hresultText(hr);
    return true;
}

// Orientación EXIF (1..8) aplicada a la imagen.
QImage oriented(const QImage& image, int orientation)
{
    QTransform t;
    switch (orientation) {
    case 2: t.scale(-1, 1); break;
    case 3: t.rotate(180); break;
    case 4: t.scale(1, -1); break;
    case 5: t = QTransform(0, 1, 1, 0, 0, 0); break;
    case 6: t.rotate(90); break;
    case 7: t = QTransform(0, -1, -1, 0, 0, 0); break;
    case 8: t.rotate(270); break;
    default: return image;
    }
    return image.transformed(t);
}

QByteArray profileOf(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame)
{
    UINT count = 0;
    if (FAILED(frame->GetColorContexts(0, nullptr, &count)) || !count)
        return {};
    std::vector<IWICColorContext*> contexts(count, nullptr);
    for (UINT i = 0; i < count; ++i)
        factory->CreateColorContext(&contexts[i]);
    QByteArray icc;
    if (SUCCEEDED(frame->GetColorContexts(count, contexts.data(), &count)))
        for (UINT i = 0; i < count && icc.isEmpty(); ++i) {
            WICColorContextType type;
            UINT size = 0;
            if (contexts[i] && SUCCEEDED(contexts[i]->GetType(&type)) && type == WICColorContextProfile
                && SUCCEEDED(contexts[i]->GetProfileBytes(0, nullptr, &size)) && size) {
                icc.resize(size);
                if (FAILED(contexts[i]->GetProfileBytes(size, reinterpret_cast<BYTE*>(icc.data()), &size)))
                    icc.clear();
            }
        }
    for (IWICColorContext* c : contexts)
        if (c)
            c->Release();
    return icc;
}

void setOption(IPropertyBag2* bag, const wchar_t* name, VARIANT value)
{
    PROPBAG2 option{};
    option.pstrName = const_cast<LPOLESTR>(name);
    // Los códecs que no tienen la opción la rechazan: no pasa nada.
    bag->Write(1, &option, &value);
}

bool hasAlpha(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) != 255)
                return true;
    }
    return false;
}

QByteArray encodeWith(const QImage& source, const Writer& writer, int quality, QString* error)
{
    ComScope com;
    Com<IWICImagingFactory> factory;
    if (failed(createFactory(factory) ? S_OK : E_FAIL, error))
        return {};
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const bool alpha = hasAlpha(image);

    Com<IStream> stream;
    if (failed(CreateStreamOnHGlobal(nullptr, TRUE, stream.put()), error))
        return {};
    Com<IWICBitmapEncoder> encoder;
    if (failed(factory->CreateEncoder(writer.container, nullptr, encoder.put()), error)
        || failed(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), error))
        return {};
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> options;
    if (failed(encoder->CreateNewFrame(frame.put(), options.put()), error))
        return {};
    if (options) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = std::clamp(quality, 1, 100) / 100.0f;
        setOption(options.get(), L"ImageQuality", v);
        if (quality >= 100) {
            VariantInit(&v);
            v.vt = VT_BOOL;
            v.boolVal = VARIANT_TRUE;
            setOption(options.get(), L"Lossless", v);
        }
        if (writer.heifCompression >= 0) {
            VariantInit(&v);
            v.vt = VT_UI1;
            v.bVal = BYTE(writer.heifCompression);
            setOption(options.get(), L"HeifCompressionMethod", v);
        }
    }
    if (failed(frame->Initialize(options.get()), error) || failed(frame->SetSize(UINT(image.width()), UINT(image.height())), error))
        return {};
    if (image.dotsPerMeterX() > 0 && image.dotsPerMeterY() > 0)
        frame->SetResolution(image.dotsPerMeterX() * 0.0254, image.dotsPerMeterY() * 0.0254);
    const WICPixelFormatGUID wanted = alpha ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppBGR;
    WICPixelFormatGUID format = wanted;
    if (failed(frame->SetPixelFormat(&format), error))
        return {};
    // El perfil de color, si no es sRGB.
    if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
        const QByteArray icc = image.colorSpace().iccProfile();
        Com<IWICColorContext> context;
        if (!icc.isEmpty() && SUCCEEDED(factory->CreateColorContext(context.put()))
            && SUCCEEDED(context->InitializeFromMemory(reinterpret_cast<const BYTE*>(icc.constData()), UINT(icc.size())))) {
            IWICColorContext* list[] = {context.get()};
            frame->SetColorContexts(1, list);
        }
    }
    Com<IWICBitmap> bitmap;
    if (failed(factory->CreateBitmapFromMemory(UINT(image.width()), UINT(image.height()), wanted, UINT(image.bytesPerLine()),
                                               UINT(image.sizeInBytes()), const_cast<BYTE*>(image.constBits()), bitmap.put()),
               error))
        return {};
    // El códec pide otro formato de píxeles (sin alfa, de 24 bits...): se convierte.
    Com<IWICBitmapSource> converted;
    IWICBitmapSource* pixels = bitmap.get();
    if (!IsEqualGUID(format, wanted)) {
        if (failed(WICConvertBitmapSource(format, bitmap.get(), converted.put()), error))
            return {};
        pixels = converted.get();
    }
    if (failed(frame->WriteSource(pixels, nullptr), error) || failed(frame->Commit(), error) || failed(encoder->Commit(), error))
        return {};

    STATSTG stat{};
    HGLOBAL global = nullptr;
    if (failed(stream->Stat(&stat, STATFLAG_NONAME), error) || failed(GetHGlobalFromStream(stream.get(), &global), error))
        return {};
    const void* data = GlobalLock(global);
    if (!data) {
        if (error)
            *error = QObject::tr("No se pudo comprimir la imagen.");
        return {};
    }
    QByteArray out(static_cast<const char*>(data), qsizetype(stat.cbSize.QuadPart));
    GlobalUnlock(global);
    return out;
}

} // namespace

bool canRead(const QString& suffix)
{
    return containerFor(codecs().decoders, suffix).has_value();
}

bool canWrite(const QString& suffix)
{
    // Que haya códec no basta (HEIC necesita además el de HEVC, AVIF el de AV1): se prueba una vez
    // con una imagen pequeña.
    static QMutex mutex;
    static QHash<QString, bool> known;
    QMutexLocker lock(&mutex);
    if (const auto it = known.constFind(suffix); it != known.cend())
        return *it;
    bool ok = false;
    if (const auto writer = writerFor(suffix)) {
        QImage probe(16, 16, QImage::Format_ARGB32);
        probe.fill(qRgba(40, 120, 200, 255));
        ok = !encodeWith(probe, *writer, 90, nullptr).isEmpty();
    }
    known.insert(suffix, ok);
    return ok;
}

QStringList readableSuffixes()
{
    QStringList list;
    for (const char* s : {"jxr", "heic", "avif", "jxl"})
        if (canRead(QLatin1String(s)))
            list << QLatin1String(s);
    return list;
}

QImage decode(const QByteArray& data, QString* error)
{
    ComScope com;
    Com<IWICImagingFactory> factory;
    if (failed(createFactory(factory) ? S_OK : E_FAIL, error))
        return {};
    Com<IStream> stream;
    *stream.put() = SHCreateMemStream(reinterpret_cast<const BYTE*>(data.constData()), UINT(data.size()));
    if (!stream) {
        if (error)
            *error = QObject::tr("No hay memoria suficiente para abrir la imagen.");
        return {};
    }
    Com<IWICBitmapDecoder> decoder;
    if (failed(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()), error))
        return {};
    Com<IWICBitmapFrameDecode> frame;
    if (failed(decoder->GetFrame(0, frame.put()), error))
        return {};
    UINT w = 0, h = 0;
    if (failed(frame->GetSize(&w, &h), error))
        return {};
    if (!w || !h || qint64(w) * h > (qint64(1) << 30)) {
        if (error)
            *error = QObject::tr("Tamaño de imagen no válido.");
        return {};
    }
    Com<IWICBitmapSource> bgra;
    if (failed(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.get(), bgra.put()), error))
        return {};
    QImage image(int(w), int(h), QImage::Format_ARGB32);
    if (image.isNull()) {
        if (error)
            *error = QObject::tr("No hay memoria suficiente para abrir la imagen.");
        return {};
    }
    if (failed(bgra->CopyPixels(nullptr, UINT(image.bytesPerLine()), UINT(image.sizeInBytes()), image.bits()), error))
        return {};

    double dpiX = 0, dpiY = 0;
    if (SUCCEEDED(frame->GetResolution(&dpiX, &dpiY)) && dpiX >= 1 && dpiY >= 1) {
        image.setDotsPerMeterX(int(std::lround(dpiX / 0.0254)));
        image.setDotsPerMeterY(int(std::lround(dpiY / 0.0254)));
    }
    const QByteArray icc = profileOf(factory.get(), frame.get());
    if (!icc.isEmpty()) {
        const QColorSpace space = QColorSpace::fromIccProfile(icc);
        if (space.isValid())
            image.setColorSpace(space);
    }
    int orientation = 1;
    Com<IWICMetadataQueryReader> query;
    if (SUCCEEDED(frame->GetMetadataQueryReader(query.put()))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(query->GetMetadataByName(L"System.Photo.Orientation", &v)) && v.vt == VT_UI2)
            orientation = v.uiVal;
        PropVariantClear(&v);
    }
    return oriented(image, orientation);
}

QByteArray encode(const QImage& image, const QString& suffix, int quality, QString* error)
{
    const auto writer = writerFor(suffix);
    if (!writer) {
        if (error)
            *error = hresultText(WINCODEC_ERR_COMPONENTNOTFOUND);
        return {};
    }
    return encodeWith(image, *writer, quality, error);
}

#else

bool canRead(const QString&)
{
    return false;
}

bool canWrite(const QString&)
{
    return false;
}

QStringList readableSuffixes()
{
    return {};
}

QImage decode(const QByteArray&, QString*)
{
    return {};
}

QByteArray encode(const QImage&, const QString&, int, QString*)
{
    return {};
}

#endif

} // namespace Wic
