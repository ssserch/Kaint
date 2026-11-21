#include "core/ModernFormats.h"

#include <QObject>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <mutex>

#ifdef KAINT_HAVE_WEBP
#include <webp/decode.h>
#include <webp/encode.h>
#endif
#ifdef KAINT_HAVE_WEBPMUX
#include <webp/mux.h>
#endif
#ifdef KAINT_HAVE_AVIF
#include <avif/avif.h>
#endif
#ifdef KAINT_HAVE_JXL
#include <jxl/decode.h>
#include <jxl/encode.h>
#endif
#ifdef KAINT_HAVE_HEIF
#include <libheif/heif.h>
#endif

namespace ModernFormats {

namespace {

QString lower(const QString& s)
{
    return s.toLower();
}

[[maybe_unused]] QImage straight(const QImage& image)
{
    return image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
}

[[maybe_unused]] void setError(QString* error, const QString& text)
{
    if (error)
        *error = text;
}

#ifdef KAINT_HAVE_WEBP
QImage decodeWebp(const QByteArray& data, QString* error)
{
    int w = 0, h = 0;
    if (!WebPGetInfo(reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()), &w, &h) || w <= 0 || h <= 0) {
        setError(error, QObject::tr("No es un WebP válido."));
        return {};
    }
    QImage image(w, h, QImage::Format_ARGB32);
    // ARGB32 en memoria es BGRA (en los sistemas de siempre, little endian).
    if (!WebPDecodeBGRAInto(reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()), image.bits(),
                            size_t(image.sizeInBytes()), image.bytesPerLine())) {
        // Un WebP animado no se lee con la API sencilla: lo intentará Qt.
        setError(error, QObject::tr("No se pudo leer el WebP."));
        return {};
    }
    return image;
}

QByteArray encodeWebp(const QImage& source, int quality, QString* error)
{
    const QImage image = straight(source);
    uint8_t* out = nullptr;
    size_t size = quality >= 100 ? WebPEncodeLosslessBGRA(image.constBits(), image.width(), image.height(), image.bytesPerLine(), &out)
                                 : WebPEncodeBGRA(image.constBits(), image.width(), image.height(), image.bytesPerLine(),
                                                  float(std::clamp(quality, 0, 100)), &out);
    if (size == 0 || !out) {
        setError(error, QObject::tr("No se pudo comprimir el WebP."));
        return {};
    }
    QByteArray data(reinterpret_cast<const char*>(out), qsizetype(size));
    WebPFree(out);
    return data;
}
#endif

#ifdef KAINT_HAVE_AVIF
QImage decodeAvif(const QByteArray& data, QString* error)
{
    avifDecoder* decoder = avifDecoderCreate();
    if (!decoder)
        return {};
    QImage image;
    avifResult r = avifDecoderSetIOMemory(decoder, reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()));
    if (r == AVIF_RESULT_OK)
        r = avifDecoderParse(decoder);
    if (r == AVIF_RESULT_OK)
        r = avifDecoderNextImage(decoder);
    if (r == AVIF_RESULT_OK) {
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, decoder->image);
        rgb.format = AVIF_RGB_FORMAT_BGRA;
        rgb.depth = 8;
        image = QImage(int(decoder->image->width), int(decoder->image->height), QImage::Format_ARGB32);
        rgb.pixels = image.bits();
        rgb.rowBytes = uint32_t(image.bytesPerLine());
        r = avifImageYUVToRGB(decoder->image, &rgb);
        if (r != AVIF_RESULT_OK)
            image = QImage();
    }
    if (r != AVIF_RESULT_OK)
        setError(error, QObject::tr("No se pudo leer el AVIF: %1").arg(QString::fromUtf8(avifResultToString(r))));
    avifDecoderDestroy(decoder);
    return image;
}

QByteArray encodeAvif(const QImage& source, int quality, QString* error)
{
    const QImage image = straight(source);
    avifImage* avif = avifImageCreate(uint32_t(image.width()), uint32_t(image.height()), 8,
                                      quality >= 90 ? AVIF_PIXEL_FORMAT_YUV444 : AVIF_PIXEL_FORMAT_YUV420);
    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avif);
    rgb.format = AVIF_RGB_FORMAT_BGRA;
    rgb.depth = 8;
    rgb.pixels = const_cast<uint8_t*>(image.constBits());
    rgb.rowBytes = uint32_t(image.bytesPerLine());
    avifResult r = avifImageRGBToYUV(avif, &rgb);
    QByteArray data;
    if (r == AVIF_RESULT_OK) {
        avifEncoder* encoder = avifEncoderCreate();
        encoder->quality = std::clamp(quality, 0, 100);
        encoder->qualityAlpha = std::clamp(quality, 0, 100);
        encoder->speed = 6;
        avifRWData out = AVIF_DATA_EMPTY;
        r = avifEncoderWrite(encoder, avif, &out);
        if (r == AVIF_RESULT_OK)
            data = QByteArray(reinterpret_cast<const char*>(out.data), qsizetype(out.size));
        avifRWDataFree(&out);
        avifEncoderDestroy(encoder);
    }
    if (r != AVIF_RESULT_OK)
        setError(error, QObject::tr("No se pudo comprimir el AVIF: %1").arg(QString::fromUtf8(avifResultToString(r))));
    avifImageDestroy(avif);
    return data;
}
#endif

#ifdef KAINT_HAVE_JXL
QImage decodeJxl(const QByteArray& data, QString* error)
{
    JxlDecoder* decoder = JxlDecoderCreate(nullptr);
    if (!decoder)
        return {};
    JxlDecoderSubscribeEvents(decoder, JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE);
    JxlDecoderSetInput(decoder, reinterpret_cast<const uint8_t*>(data.constData()), size_t(data.size()));
    JxlDecoderCloseInput(decoder);
    const JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlBasicInfo info{};
    QByteArray rgba;
    bool done = false, failed = false;
    while (!done && !failed) {
        switch (JxlDecoderProcessInput(decoder)) {
        case JXL_DEC_BASIC_INFO:
            failed = JxlDecoderGetBasicInfo(decoder, &info) != JXL_DEC_SUCCESS;
            break;
        case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
            size_t size = 0;
            if (JxlDecoderImageOutBufferSize(decoder, &format, &size) != JXL_DEC_SUCCESS) {
                failed = true;
                break;
            }
            rgba.resize(qsizetype(size));
            failed = JxlDecoderSetImageOutBuffer(decoder, &format, rgba.data(), size) != JXL_DEC_SUCCESS;
            break;
        }
        case JXL_DEC_FULL_IMAGE:
        case JXL_DEC_SUCCESS:
            done = true;  // la primera imagen
            break;
        default:
            failed = true;
            break;
        }
    }
    JxlDecoderDestroy(decoder);
    if (failed || info.xsize == 0 || rgba.size() < qsizetype(info.xsize) * info.ysize * 4) {
        setError(error, QObject::tr("No se pudo leer el JPEG XL."));
        return {};
    }
    const QImage view(reinterpret_cast<const uchar*>(rgba.constData()), int(info.xsize), int(info.ysize), int(info.xsize) * 4,
                      QImage::Format_RGBA8888);
    return view.convertToFormat(QImage::Format_ARGB32);
}

QByteArray encodeJxl(const QImage& source, int quality, QString* error)
{
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    JxlEncoder* encoder = JxlEncoderCreate(nullptr);
    if (!encoder)
        return {};
    const bool lossless = quality >= 100;
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = uint32_t(image.width());
    info.ysize = uint32_t(image.height());
    info.bits_per_sample = 8;
    info.num_color_channels = 3;
    info.num_extra_channels = 1;
    info.alpha_bits = 8;
    info.uses_original_profile = lossless ? JXL_TRUE : JXL_FALSE;
    bool ok = JxlEncoderSetBasicInfo(encoder, &info) == JXL_ENC_SUCCESS;
    JxlExtraChannelInfo alpha;
    JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &alpha);
    alpha.bits_per_sample = 8;
    ok = ok && JxlEncoderSetExtraChannelInfo(encoder, 0, &alpha) == JXL_ENC_SUCCESS;
    JxlColorEncoding color;
    JxlColorEncodingSetToSRGB(&color, JXL_FALSE);
    ok = ok && JxlEncoderSetColorEncoding(encoder, &color) == JXL_ENC_SUCCESS;
    JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(encoder, nullptr);
    if (lossless)
        ok = ok && JxlEncoderSetFrameLossless(settings, JXL_TRUE) == JXL_ENC_SUCCESS;
    else
        ok = ok && JxlEncoderSetFrameDistance(settings, JxlEncoderDistanceFromQuality(float(std::clamp(quality, 0, 99)))) == JXL_ENC_SUCCESS;
    const JxlPixelFormat format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    // Las filas, juntas (QImage puede dejar relleno al final de cada una).
    QByteArray pixels(qsizetype(image.width()) * image.height() * 4, Qt::Uninitialized);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(pixels.data() + qsizetype(y) * image.width() * 4, image.constScanLine(y), size_t(image.width()) * 4);
    ok = ok && JxlEncoderAddImageFrame(settings, &format, pixels.constData(), size_t(pixels.size())) == JXL_ENC_SUCCESS;
    JxlEncoderCloseInput(encoder);
    QByteArray out;
    if (ok) {
        out.resize(64 * 1024);
        uint8_t* next = reinterpret_cast<uint8_t*>(out.data());
        size_t avail = size_t(out.size());
        JxlEncoderStatus status = JXL_ENC_NEED_MORE_OUTPUT;
        while (status == JXL_ENC_NEED_MORE_OUTPUT) {
            status = JxlEncoderProcessOutput(encoder, &next, &avail);
            if (status == JXL_ENC_NEED_MORE_OUTPUT) {
                const qsizetype used = qsizetype(next - reinterpret_cast<uint8_t*>(out.data()));
                out.resize(out.size() * 2);
                next = reinterpret_cast<uint8_t*>(out.data()) + used;
                avail = size_t(out.size() - used);
            }
        }
        ok = status == JXL_ENC_SUCCESS;
        out.resize(qsizetype(next - reinterpret_cast<uint8_t*>(out.data())));
    }
    JxlEncoderDestroy(encoder);
    if (!ok) {
        setError(error, QObject::tr("No se pudo comprimir el JPEG XL."));
        return {};
    }
    return out;
}
#endif

#ifdef KAINT_HAVE_HEIF
QImage decodeHeif(const QByteArray& data, QString* error)
{
    static std::once_flag once;
    std::call_once(once, [] { heif_init(nullptr); });
    heif_context* ctx = heif_context_alloc();
    QImage image;
    heif_image_handle* handle = nullptr;
    heif_image* img = nullptr;
    heif_error e = heif_context_read_from_memory_without_copy(ctx, data.constData(), size_t(data.size()), nullptr);
    if (e.code == heif_error_Ok)
        e = heif_context_get_primary_image_handle(ctx, &handle);
    if (e.code == heif_error_Ok)
        e = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    if (e.code == heif_error_Ok) {
        int stride = 0;
        const uint8_t* plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
        const int w = heif_image_get_width(img, heif_channel_interleaved);
        const int h = heif_image_get_height(img, heif_channel_interleaved);
        if (plane && w > 0 && h > 0)
            image = QImage(plane, w, h, stride, QImage::Format_RGBA8888).convertToFormat(QImage::Format_ARGB32);
    } else {
        setError(error, QObject::tr("No se pudo leer el HEIC: %1").arg(QString::fromUtf8(e.message)));
    }
    if (img)
        heif_image_release(img);
    if (handle)
        heif_image_handle_release(handle);
    heif_context_free(ctx);
    return image;
}
#endif

} // namespace

bool canRead(const QString& suffix)
{
    const QString s = lower(suffix);
    Q_UNUSED(s);
#ifdef KAINT_HAVE_WEBP
    if (s == QLatin1String("webp"))
        return true;
#endif
#ifdef KAINT_HAVE_AVIF
    if (s == QLatin1String("avif") || s == QLatin1String("avifs"))
        return true;
#endif
#ifdef KAINT_HAVE_JXL
    if (s == QLatin1String("jxl"))
        return true;
#endif
#ifdef KAINT_HAVE_HEIF
    if (s == QLatin1String("heic") || s == QLatin1String("heif") || s == QLatin1String("hif"))
        return true;
#endif
    return false;
}

bool canWrite(const QString& suffix)
{
    const QString s = lower(suffix);
    Q_UNUSED(s);
#ifdef KAINT_HAVE_WEBP
    if (s == QLatin1String("webp"))
        return true;
#endif
#ifdef KAINT_HAVE_AVIF
    if (s == QLatin1String("avif"))
        return true;
#endif
#ifdef KAINT_HAVE_JXL
    if (s == QLatin1String("jxl"))
        return true;
#endif
    return false;
}

QImage decode(const QString& suffix, const QByteArray& data, QString* error)
{
    const QString s = lower(suffix);
    Q_UNUSED(s);
    Q_UNUSED(data);
    Q_UNUSED(error);
#ifdef KAINT_HAVE_WEBP
    if (s == QLatin1String("webp"))
        return decodeWebp(data, error);
#endif
#ifdef KAINT_HAVE_AVIF
    if (s == QLatin1String("avif") || s == QLatin1String("avifs"))
        return decodeAvif(data, error);
#endif
#ifdef KAINT_HAVE_JXL
    if (s == QLatin1String("jxl"))
        return decodeJxl(data, error);
#endif
#ifdef KAINT_HAVE_HEIF
    if (s == QLatin1String("heic") || s == QLatin1String("heif") || s == QLatin1String("hif"))
        return decodeHeif(data, error);
#endif
    return {};
}

QByteArray encode(const QString& suffix, const QImage& image, int quality, QString* error)
{
    const QString s = lower(suffix);
    Q_UNUSED(s);
    Q_UNUSED(image);
    Q_UNUSED(quality);
    Q_UNUSED(error);
#ifdef KAINT_HAVE_WEBP
    if (s == QLatin1String("webp"))
        return encodeWebp(image, quality, error);
#endif
#ifdef KAINT_HAVE_AVIF
    if (s == QLatin1String("avif"))
        return encodeAvif(image, quality, error);
#endif
#ifdef KAINT_HAVE_JXL
    if (s == QLatin1String("jxl"))
        return encodeJxl(image, quality, error);
#endif
    return {};
}

bool canWriteAnimatedWebP()
{
#ifdef KAINT_HAVE_WEBPMUX
    return true;
#else
    return false;
#endif
}

QByteArray encodeAnimatedWebP(const QList<Gif::Frame>& frames, int quality, int loops, QString* error)
{
#ifdef KAINT_HAVE_WEBPMUX
    QList<const Gif::Frame*> shown;
    for (const Gif::Frame& f : frames)
        if (!f.hidden && !f.image.isNull())
            shown << &f;
    if (shown.isEmpty()) {
        setError(error, QObject::tr("No hay fotogramas que guardar."));
        return {};
    }
    const QSize size = shown.first()->image.size();
    WebPAnimEncoderOptions options;
    if (!WebPAnimEncoderOptionsInit(&options))
        return {};
    options.anim_params.loop_count = std::max(0, loops);
    // (Sin mezclar tipos: con calidad 100, todo sin pérdida.)
    options.allow_mixed = 0;
    WebPAnimEncoder* encoder = WebPAnimEncoderNew(size.width(), size.height(), &options);
    if (!encoder) {
        setError(error, QObject::tr("No se pudo comprimir el WebP."));
        return {};
    }
    WebPConfig config;
    WebPConfigInit(&config);
    config.lossless = quality >= 100 ? 1 : 0;
    config.quality = float(std::clamp(quality, 0, 100));
    int timestamp = 0;
    bool ok = true;
    for (const Gif::Frame* f : std::as_const(shown)) {
        QImage image = straight(f->image);
        if (image.size() != size)
            image = image.copy(QRect(QPoint(0, 0), size));
        WebPPicture picture;
        WebPPictureInit(&picture);
        picture.use_argb = 1;
        picture.width = size.width();
        picture.height = size.height();
        ok = WebPPictureImportBGRA(&picture, image.constBits(), image.bytesPerLine())
             && WebPAnimEncoderAdd(encoder, &picture, timestamp, &config);
        WebPPictureFree(&picture);
        if (!ok)
            break;
        timestamp += std::max(1, f->delayMs);
    }
    WebPData data;
    WebPDataInit(&data);
    QByteArray out;
    if (ok && WebPAnimEncoderAdd(encoder, nullptr, timestamp, nullptr) && WebPAnimEncoderAssemble(encoder, &data))
        out = QByteArray(reinterpret_cast<const char*>(data.bytes), qsizetype(data.size));
    WebPDataClear(&data);
    WebPAnimEncoderDelete(encoder);
    if (out.isEmpty())
        setError(error, QObject::tr("No se pudo comprimir el WebP."));
    return out;
#else
    Q_UNUSED(frames);
    Q_UNUSED(quality);
    Q_UNUSED(loops);
    setError(error, QObject::tr("Kaint se compiló sin libwebpmux."));
    return {};
#endif
}

QString available()
{
    QStringList list;
#ifdef KAINT_HAVE_WEBP
    list << QStringLiteral("WebP");
#endif
#ifdef KAINT_HAVE_AVIF
    list << QStringLiteral("AVIF");
#endif
#ifdef KAINT_HAVE_JXL
    list << QStringLiteral("JPEG XL");
#endif
#ifdef KAINT_HAVE_HEIF
    list << QStringLiteral("HEIC");
#endif
    return list.join(QStringLiteral(", "));
}

} // namespace ModernFormats
