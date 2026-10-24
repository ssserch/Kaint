#include "core/ImageIO.h"

#include "core/Document.h"
#include "core/Gif.h"
#include "core/Ora.h"
#include "core/PngWriter.h"
#include "core/Pdn.h"
#include "core/effects/Effect.h"

#include <QBuffer>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>
#include <QObject>
#include <QPainter>
#include <QSaveFile>

#include <cmath>

namespace {

// Extensión en minúsculas y con los alias unificados (.jpe/.jfif -> jpg, .tif -> tiff, .heif
// -> heic...), como las agrupa Paint.NET.
QString suffixOf(const QString& path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    static const QHash<QString, QString> aliases{
        {QStringLiteral("jpeg"), QStringLiteral("jpg")},  {QStringLiteral("jpe"), QStringLiteral("jpg")},
        {QStringLiteral("jfif"), QStringLiteral("jpg")},  {QStringLiteral("exif"), QStringLiteral("jpg")},
        {QStringLiteral("tif"), QStringLiteral("tiff")},  {QStringLiteral("heif"), QStringLiteral("heic")},
        {QStringLiteral("hif"), QStringLiteral("heic")},  {QStringLiteral("dib"), QStringLiteral("bmp")},
        {QStringLiteral("rle"), QStringLiteral("bmp")},   {QStringLiteral("wdp"), QStringLiteral("jxr")},
        {QStringLiteral("wmp"), QStringLiteral("jxr")},
    };
    return aliases.value(s, s);
}

// Nombre del formato de Qt para escribir 'suffix' (HEIC puede llamarse "heif").
QByteArray writerFormat(const QString& suffix)
{
    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    const QByteArray f = suffix.toLatin1();
    if (f == "heic" && !writable.contains(f) && writable.contains("heif"))
        return QByteArrayLiteral("heif");
    return f;
}

struct FormatInfo {
    const char* suffix;
    const char* label;
    const char* patterns;
};

// Tipos de los diálogos Abrir y Guardar como, en el orden de Paint.NET. Solo aparecen los que
// el sistema puede leer o escribir.
const FormatInfo kOpenFormats[] = {
    {"pdn", QT_TRANSLATE_NOOP("QObject", "Paint.NET"), "*.pdn"},
    {"ora", QT_TRANSLATE_NOOP("QObject", "OpenRaster"), "*.ora"},
    {"png", QT_TRANSLATE_NOOP("QObject", "PNG"), "*.png"},
    {"jpg", QT_TRANSLATE_NOOP("QObject", "JPEG"), "*.jpg *.jpeg *.jpe *.jfif *.exif"},
    {"jxl", QT_TRANSLATE_NOOP("QObject", "JPEG XL"), "*.jxl"},
    {"avif", QT_TRANSLATE_NOOP("QObject", "AV1 (AVIF)"), "*.avif"},
    {"heic", QT_TRANSLATE_NOOP("QObject", "HEIC"), "*.heic *.heif *.hif"},
    {"webp", QT_TRANSLATE_NOOP("QObject", "WebP"), "*.webp"},
    {"dds", QT_TRANSLATE_NOOP("QObject", "Superficie de DirectDraw (DDS)"), "*.dds"},
    {"tiff", QT_TRANSLATE_NOOP("QObject", "TIFF"), "*.tiff *.tif"},
    {"gif", QT_TRANSLATE_NOOP("QObject", "GIF"), "*.gif"},
    {"bmp", QT_TRANSLATE_NOOP("QObject", "BMP"), "*.bmp *.dib *.rle"},
    {"tga", QT_TRANSLATE_NOOP("QObject", "TGA"), "*.tga"},
    {"jxr", QT_TRANSLATE_NOOP("QObject", "JPEG XR"), "*.jxr *.wdp *.wmp"},
    {"ico", QT_TRANSLATE_NOOP("QObject", "Icono de Windows"), "*.ico"},
};

const FormatInfo kSaveFormats[] = {
    {"ora", QT_TRANSLATE_NOOP("QObject", "OpenRaster con capas"), "*.ora"},
    {"png", QT_TRANSLATE_NOOP("QObject", "PNG"), "*.png"},
    {"jpg", QT_TRANSLATE_NOOP("QObject", "JPEG"), "*.jpg *.jpeg *.jpe *.jfif"},
    {"jxl", QT_TRANSLATE_NOOP("QObject", "JPEG XL"), "*.jxl"},
    {"avif", QT_TRANSLATE_NOOP("QObject", "AV1 (AVIF)"), "*.avif"},
    {"heic", QT_TRANSLATE_NOOP("QObject", "HEIC"), "*.heic *.heif"},
    {"webp", QT_TRANSLATE_NOOP("QObject", "WebP"), "*.webp"},
    {"dds", QT_TRANSLATE_NOOP("QObject", "Superficie de DirectDraw (DDS)"), "*.dds"},
    {"tiff", QT_TRANSLATE_NOOP("QObject", "TIFF"), "*.tiff *.tif"},
    {"gif", QT_TRANSLATE_NOOP("QObject", "GIF"), "*.gif"},
    {"bmp", QT_TRANSLATE_NOOP("QObject", "BMP"), "*.bmp"},
    {"tga", QT_TRANSLATE_NOOP("QObject", "TGA"), "*.tga"},
    {"ico", QT_TRANSLATE_NOOP("QObject", "Icono de Windows"), "*.ico"},
};

// Formatos que escribe Kaint sin depender de los complementos de Qt.
bool ownWriter(const QByteArray& suffix)
{
    return suffix == "ora" || suffix == "gif" || suffix == "ico";
}

// Se escribe a un archivo temporal y se renombra: un error no destruye el original.
bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace

namespace ImageIO {

QStringList supportedReadSuffixes()
{
    QStringList list{QStringLiteral("ora"), QStringLiteral("pdn")};
    for (const QByteArray& f : QImageReader::supportedImageFormats()) {
        const QString s = suffixOf(QStringLiteral("x.") + QString::fromLatin1(f));
        if (!list.contains(s))
            list.append(s);
    }
    return list;
}

QString openFilter()
{
    const QStringList readable = supportedReadSuffixes();
    QStringList all, filters;
    for (const FormatInfo& f : kOpenFormats) {
        if (!readable.contains(QLatin1String(f.suffix)))
            continue;
        const QString patterns = QLatin1String(f.patterns);
        all << patterns;
        filters << QStringLiteral("%1 (%2)").arg(QObject::tr(f.label), patterns);
    }
    for (const QString& s : readable) {
        const QString pattern = QStringLiteral("*.%1").arg(s);
        if (!all.join(QLatin1Char(' ')).split(QLatin1Char(' ')).contains(pattern))
            all << pattern;
    }
    filters.prepend(QObject::tr("Todos los tipos de imagen (%1)").arg(all.join(QLatin1Char(' '))));
    filters << QObject::tr("Todos los archivos (*)");
    return filters.join(QStringLiteral(";;"));
}

QString saveFilter(QString* defaultFilter)
{
    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    QStringList filters;
    for (const FormatInfo& f : kSaveFormats) {
        const QByteArray suffix(f.suffix);
        if (!ownWriter(suffix) && !writable.contains(writerFormat(QLatin1String(f.suffix))))
            continue;
        const QString filter = QStringLiteral("%1 (%2)").arg(QObject::tr(f.label), QLatin1String(f.patterns));
        if (suffix == "png" && defaultFilter)
            *defaultFilter = filter;
        filters << filter;
    }
    return filters.join(QStringLiteral(";;"));
}

bool isLayered(const QString& path)
{
    return suffixOf(path) == QLatin1String("ora");
}

bool isWritable(const QString& path)
{
    const QByteArray suffix = suffixOf(path).toLatin1();
    if (ownWriter(suffix))
        return true;
    return QImageWriter::supportedImageFormats().contains(writerFormat(suffixOf(path)));
}

bool isAnimation(const QString& path)
{
    return suffixOf(path) == QLatin1String("gif");
}

bool isIcon(const QString& path)
{
    return suffixOf(path) == QLatin1String("ico");
}

bool supportsQuality(const QString& path)
{
    static const QStringList lossy{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                   QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("heif"),
                                   QStringLiteral("heic")};
    return lossy.contains(suffixOf(path));
}

bool supportsAlpha(const QString& path)
{
    static const QStringList opaque{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("bmp")};
    return !opaque.contains(suffixOf(path));
}

Document* load(const QString& path, QString* error)
{
    Document* doc = nullptr;
    if (isLayered(path)) {
        doc = Ora::load(path, error);
    } else if (suffixOf(path) == QLatin1String("pdn")) {
        doc = Pdn::load(path, error);
    } else if (isAnimation(path)) {
        const Gif::Animation anim = Gif::read(path, error);
        if (anim.frames.isEmpty())
            return nullptr;
        if (anim.frames.size() == 1) {
            doc = Document::createFromImage(anim.frames.first().image, QObject::tr("Fondo"));
        } else {
            doc = Document::createFromImage(anim.frames.first().image,
                                            Gif::nameWithDelay(QObject::tr("Fotograma 1"), anim.frames.first().delayMs));
            for (int i = 1; i < anim.frames.size(); ++i) {
                const QString name = Gif::nameWithDelay(QObject::tr("Fotograma %1").arg(i + 1), anim.frames[i].delayMs);
                doc->insertLayer(i, std::make_shared<Layer>(name, anim.frames[i].image));
            }
            doc->setActiveIndex(0);
        }
        doc->setGifLoops(anim.loops);
    } else if (isIcon(path) || suffixOf(path) == QLatin1String("cur")) {
        const QImage image = Ico::readLargest(path, error);
        if (image.isNull())
            return nullptr;
        doc = Document::createFromImage(image, QObject::tr("Fondo"));
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            if (error)
                *error = reader.errorString();
            return nullptr;
        }
        doc = Document::createFromImage(image, QObject::tr("Fondo"));
        if (image.dotsPerMeterX() > 0)
            doc->setDpi(image.dotsPerMeterX() * 0.0254);
        if (image.colorSpace().isValid())
            doc->setColorSpace(image.colorSpace());
    }
    if (doc) {
        doc->setFilePath(path);
        doc->undoStack()->setClean();
    }
    return doc;
}

bool hasBitDepth(const QString& path)
{
    static const QStringList list{QStringLiteral("png"), QStringLiteral("bmp"), QStringLiteral("tga")};
    return list.contains(suffixOf(path));
}

int automaticBitDepth(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) != 255)
                return 32;
    }
    return 24;
}

QImage toIndexed8(const QImage& image, int dither, int alphaThreshold, bool transparentSlot, int algorithm)
{
    // Paleta óptima con el mismo cuantificador que el efecto Cuantificar.
    const Effect* quantize = Effects::find(QStringLiteral("quantize"));
    QImage img = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) < alphaThreshold)
                row[x] = 0;
            else
                row[x] = qUnpremultiply(row[x]) | 0xff000000u;
        }
    }
    bool transparent = false;
    for (int y = 0; y < img.height() && !transparent; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (qAlpha(row[x]) == 0) {
                transparent = true;
                break;
            }
    }
    if (quantize) {
        ParamValues v = Effects::defaults(*quantize);
        v.insert(QStringLiteral("colors"), transparent || transparentSlot ? 255 : 256);
        v.insert(QStringLiteral("algorithm"), std::clamp(algorithm, 0, 1));
        v.insert(QStringLiteral("dither"), std::clamp(dither, 0, 8));
        const std::atomic_bool never{false};
        img = Effects::apply(*quantize, img, Selection(), v, EffectContext(), never);
    }
    QImage indexed = img.convertToFormat(transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32)
                         .convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither | Qt::AvoidDither);
    if (transparentSlot && !transparent && indexed.colorCount() < 256) {
        QList<QRgb> table = indexed.colorTable();
        table << 0u;
        indexed.setColorTable(table);
    }
    return indexed;
}

bool supportsInterlace(const QString& path)
{
    return suffixOf(path) == QLatin1String("png");
}

QImage prepare(const QImage& flattened, const QString& path, const SaveOptions& options)
{
    // GIF e ICO se preparan al codificar (paleta por fotograma, varios tamaños).
    if (isAnimation(path) || isIcon(path))
        return flattened.convertToFormat(QImage::Format_ARGB32);
    const QString suffix = suffixOf(path);
    int depth = hasBitDepth(path) ? options.bitDepth : 32;
    if (depth == 0)
        depth = automaticBitDepth(flattened);
    if (!supportsAlpha(path) && depth == 32)
        depth = 24;

    if (depth == 8)
        return toIndexed8(flattened, options.dither, options.alphaThreshold, false, options.quantization);
    if (depth == 24) {
        QImage opaque(flattened.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter p(&opaque);
        p.drawImage(0, 0, flattened);
        p.end();
        return suffix == QLatin1String("png") || suffix == QLatin1String("bmp") || suffix == QLatin1String("tga")
                   ? opaque.convertToFormat(QImage::Format_RGB888)
                   : opaque;
    }
    return flattened.convertToFormat(QImage::Format_ARGB32);
}

QByteArray encode(const QImage& prepared, const QString& path, const SaveOptions& options, QString* error)
{
    if (isAnimation(path))
        return encodeAnimation({Gif::Frame{prepared, options.frameDelay}}, options, error);
    if (isIcon(path))
        return Ico::encode(prepared, options.icoSizes, error);
    if (options.interlaced && supportsInterlace(path)) {
        QByteArray png = Png::encode(prepared, true);
        if (png.isEmpty() && error)
            *error = QObject::tr("No se pudo comprimir la imagen.");
        return png;
    }
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    const QString format = suffixOf(path);
    QImageWriter writer(&buffer, writerFormat(format));
    if (supportsQuality(path))
        writer.setQuality(options.quality);
    if (format == QLatin1String("tiff"))
        writer.setCompression(1);
    if (!writer.write(prepared)) {
        if (error)
            *error = writer.errorString();
        return {};
    }
    return data;
}

bool save(const Document* doc, const QString& path, const SaveOptions& options, QString* error)
{
    if (isLayered(path))
        return Ora::save(doc, path, error);

    QByteArray data;
    if (isAnimation(path) && options.animation && doc->layerCount() > 1)
        return saveAnimation(animationFrames(doc, options.frameDelay), path, options, error);
    QImage image = prepare(doc->flattened(), path, options);
    const int dpm = int(std::round(doc->dpi() / 0.0254));
    image.setDotsPerMeterX(dpm);
    image.setDotsPerMeterY(dpm);
    if (doc->colorSpace().isValid() && doc->colorSpace() != QColorSpace(QColorSpace::SRgb))
        image.setColorSpace(doc->colorSpace());
    data = encode(image, path, options, error);
    if (data.isEmpty())
        return false;
    return writeFile(path, data, error);
}

QByteArray encodeAnimation(QList<Gif::Frame> frames, const SaveOptions& options, QString* error)
{
    for (Gif::Frame& f : frames)
        if (f.delayMs <= 0)
            f.delayMs = options.frameDelay;
    Gif::Options o;
    o.dither = options.dither;
    o.alphaThreshold = options.alphaThreshold;
    o.loops = options.loops;
    o.quantization = options.quantization;
    o.globalPalette = options.gifGlobalPalette;
    o.stack = options.gifStack;
    return Gif::encode(Gif::applyEffects(frames, options.gifEffects), o, error);
}

bool saveAnimation(const QList<Gif::Frame>& frames, const QString& path, const SaveOptions& options, QString* error)
{
    const QByteArray data = encodeAnimation(frames, options, error);
    return !data.isEmpty() && writeFile(path, data, error);
}

QList<Gif::Frame> animationFrames(const Document* doc, int defaultDelay)
{
    // Las capas visibles de abajo arriba, cada una con su opacidad.
    QList<Gif::Frame> frames;
    for (const LayerPtr& layer : doc->layers()) {
        if (!layer->props.visible)
            continue;
        Gif::Frame f;
        f.delayMs = Gif::delayFromName(layer->props.name, defaultDelay);
        f.image = layer->image;
        if (layer->props.opacity < 255) {
            f.image = layer->image.copy();
            for (int y = 0; y < f.image.height(); ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(f.image.scanLine(y));
                for (int x = 0; x < f.image.width(); ++x)
                    row[x] = (row[x] & 0x00ffffffu) | (quint32(qAlpha(row[x]) * layer->props.opacity / 255) << 24);
            }
        }
        frames << f;
    }
    if (frames.isEmpty())
        frames << Gif::Frame{doc->flattened(), defaultDelay};
    return frames;
}

} // namespace ImageIO
