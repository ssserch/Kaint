#include "core/Psd.h"

#include "core/Document.h"

#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QSaveFile>
#include <QStringList>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

// Formato descrito en «Adobe Photoshop File Formats Specification». Todo va en big endian; el PSB
// (versión 2) cambia algunas longitudes de 4 a 8 bytes y las de las filas comprimidas de 2 a 4.
namespace {

enum ColorMode { kBitmap = 0, kGrayscale = 1, kIndexed = 2, kRgb = 3, kCmyk = 4, kMultichannel = 7, kDuotone = 8, kLab = 9 };

// Más píxeles que esto en una capa o en el lienzo (unos 8 GB por capa en RGBA) no caben en Kaint.
constexpr qint64 kMaxPixels = qint64(1) << 31;

class ParseError {
public:
    explicit ParseError(QString m)
        : message(std::move(m))
    {
    }
    QString message;
};

[[noreturn]] void truncated()
{
    throw ParseError(QObject::tr("El archivo está incompleto o dañado."));
}

// Lectura en big endian de un trozo del archivo, sin salirse de él.
class Reader {
public:
    Reader(const uchar* data, qint64 size)
        : m_data(data)
        , m_size(size)
    {
    }

    qint64 pos() const { return m_pos; }
    qint64 remaining() const { return m_size - m_pos; }
    // Posición dentro del archivo entero (para volver a los datos más tarde).
    const uchar* here() const { return m_data + m_pos; }

    void need(qint64 n) const
    {
        if (n < 0 || remaining() < n)
            truncated();
    }
    void skip(qint64 n)
    {
        need(n);
        m_pos += n;
    }
    const uchar* take(qint64 n)
    {
        need(n);
        const uchar* p = m_data + m_pos;
        m_pos += n;
        return p;
    }
    quint8 u8() { return *take(1); }
    quint16 u16()
    {
        const uchar* p = take(2);
        return quint16(p[0] << 8 | p[1]);
    }
    quint32 u32()
    {
        const uchar* p = take(4);
        return quint32(p[0]) << 24 | quint32(p[1]) << 16 | quint32(p[2]) << 8 | p[3];
    }
    quint64 u64()
    {
        const quint64 hi = u32();
        return hi << 32 | u32();
    }
    qint16 i16() { return qint16(u16()); }
    qint32 i32() { return qint32(u32()); }
    QByteArray bytes(qint64 n)
    {
        const uchar* p = take(n);
        return QByteArray(reinterpret_cast<const char*>(p), n);
    }
    QByteArray key() { return bytes(4); }
    QByteArray peekKey() const
    {
        return remaining() >= 4 ? QByteArray(reinterpret_cast<const char*>(m_data + m_pos), 4) : QByteArray();
    }

    // Los 'length' bytes siguientes como otro lector (y este sigue después de ellos).
    Reader sub(quint64 length)
    {
        if (length > quint64(remaining()))
            truncated();
        Reader r(m_data + m_pos, qint64(length));
        m_pos += qint64(length);
        return r;
    }

private:
    const uchar* m_data;
    qint64 m_size;
    qint64 m_pos = 0;
};

// Bloques de información adicional cuya longitud ocupa 8 bytes en un PSB.
bool longBlock(const QByteArray& key)
{
    static const QList<QByteArray> keys{"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph",
                                        "FMsk", "lnk2", "FEid", "FXid", "PxSD", "lnkE", "lnk3"};
    return keys.contains(key);
}

// Entre bloques puede haber relleno (a 2 o a 4 bytes según quién escribió el archivo).
void skipToSignature(Reader& r)
{
    for (int i = 0; i < 4 && r.remaining() >= 4; ++i) {
        const QByteArray sig = r.peekKey();
        if (sig == "8BIM" || sig == "8B64")
            return;
        r.skip(1);
    }
}

QRect rectOf(qint32 top, qint32 left, qint32 bottom, qint32 right)
{
    const qint64 w = qint64(right) - left, h = qint64(bottom) - top;
    if (w <= 0 || h <= 0)
        return {};
    if (w > 1000000 || h > 1000000 || w * h > kMaxPixels)
        throw ParseError(QObject::tr("Una capa es demasiado grande."));
    return QRect(left, top, int(w), int(h));
}

// --- Descompresión de canales ---------------------------------------------------------------

// Una fila de PackBits; lo que falte (un archivo dañado) se queda a cero.
void unpackRow(const uchar* src, qint64 len, uchar* out, qint64 rowBytes)
{
    qint64 i = 0, o = 0;
    while (i < len && o < rowBytes) {
        const int n = qint8(src[i++]);
        if (n >= 0) {
            const qint64 count = std::min<qint64>({qint64(n) + 1, len - i, rowBytes - o});
            std::memcpy(out + o, src + i, size_t(count));
            i += n + 1;
            o += count;
        } else if (n != -128) {
            if (i >= len)
                break;
            const qint64 count = std::min<qint64>(1 - n, rowBytes - o);
            std::memset(out + o, src[i++], size_t(count));
            o += count;
        }
    }
}

// Un flujo de zlib entero en 'out' (hasta 'outLen' bytes). Si se corta antes, el resto queda a cero.
void inflateInto(const uchar* src, qint64 len, uchar* out, qint64 outLen)
{
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK)
        throw ParseError(QObject::tr("No se pudo descomprimir el archivo."));
    qint64 inGiven = 0, outGiven = 0;
    constexpr qint64 kChunk = qint64(1) << 30;
    for (;;) {
        if (zs.avail_in == 0 && inGiven < len) {
            const qint64 c = std::min(len - inGiven, kChunk);
            zs.next_in = const_cast<Bytef*>(src + inGiven);
            zs.avail_in = uInt(c);
            inGiven += c;
        }
        if (zs.avail_out == 0) {
            if (outGiven >= outLen)
                break;
            const qint64 c = std::min(outLen - outGiven, kChunk);
            zs.next_out = out + outGiven;
            zs.avail_out = uInt(c);
            outGiven += c;
        }
        const int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END)
            break;
        if (ret != Z_OK) {
            // Sin más entrada (cortado) o datos dañados: lo que haya.
            if (ret == Z_BUF_ERROR || ret == Z_DATA_ERROR)
                break;
            inflateEnd(&zs);
            throw ParseError(QObject::tr("No se pudo descomprimir el archivo."));
        }
    }
    inflateEnd(&zs);
}

// Deshace la predicción de ZIP: cada muestra se guardó como la diferencia con la anterior de su fila
// (las de 32 bits, además, separadas en cuatro planos de bytes).
void unpredict(uchar* rows, int w, int h, int depth)
{
    const qint64 rowBytes = qint64(w) * depth / 8;
    std::vector<uchar> tmp(depth == 32 ? size_t(rowBytes) : 0);
    for (int y = 0; y < h; ++y) {
        uchar* row = rows + y * rowBytes;
        if (depth == 8) {
            for (int x = 1; x < w; ++x)
                row[x] = uchar(row[x] + row[x - 1]);
        } else if (depth == 16) {
            quint16 prev = quint16(row[0] << 8 | row[1]);
            for (int x = 1; x < w; ++x) {
                const quint16 v = quint16((row[2 * x] << 8 | row[2 * x + 1]) + prev);
                row[2 * x] = uchar(v >> 8);
                row[2 * x + 1] = uchar(v);
                prev = v;
            }
        } else if (depth == 32) {
            for (qint64 i = 1; i < rowBytes; ++i)
                row[i] = uchar(row[i] + row[i - 1]);
            std::memcpy(tmp.data(), row, size_t(rowBytes));
            for (int x = 0; x < w; ++x)
                for (int k = 0; k < 4; ++k)
                    row[4 * x + k] = tmp[size_t(k) * size_t(w) + size_t(x)];
        }
    }
}

uchar linearToSrgb8(float v)
{
    if (!(v > 0.0f))
        return 0;
    if (v >= 1.0f)
        return 255;
    const float s = v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uchar(std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f));
}

// Filas de muestras big endian → 8 bits por muestra. 16 bits se redondean; 32 bits (lineales, en
// coma flotante) se recortan a 0..1 y, si son de color, pasan a sRGB. 1 bit: 1 es negro.
void toEight(const uchar* src, int w, int h, int depth, bool color, uchar* dst)
{
    const qint64 rowBytes = (qint64(w) * depth + 7) / 8;
    for (int y = 0; y < h; ++y) {
        const uchar* row = src + y * rowBytes;
        uchar* out = dst + qint64(y) * w;
        switch (depth) {
        case 1:
            for (int x = 0; x < w; ++x)
                out[x] = (row[x >> 3] >> (7 - (x & 7))) & 1 ? 0 : 255;
            break;
        case 8: std::memcpy(out, row, size_t(w)); break;
        case 16:
            for (int x = 0; x < w; ++x)
                out[x] = uchar((quint32(row[2 * x] << 8 | row[2 * x + 1]) * 255 + 32767) / 65535);
            break;
        case 32:
            for (int x = 0; x < w; ++x) {
                const quint32 bits = quint32(row[4 * x]) << 24 | quint32(row[4 * x + 1]) << 16 | quint32(row[4 * x + 2]) << 8
                                     | row[4 * x + 3];
                float f;
                std::memcpy(&f, &bits, 4);
                out[x] = color ? linearToSrgb8(f) : uchar(std::lround(std::clamp(std::isfinite(f) ? f : 0.0f, 0.0f, 1.0f) * 255.0f));
            }
            break;
        }
    }
}

std::vector<quint32> readCounts(Reader& r, qint64 n, bool psb)
{
    r.need(n * (psb ? 4 : 2));
    std::vector<quint32> counts(static_cast<size_t>(n));
    for (qint64 i = 0; i < n; ++i)
        counts[size_t(i)] = psb ? r.u32() : r.u16();
    return counts;
}

// Un plano (w × h) a 8 bits desde los datos de un canal ya sin los 2 bytes de la compresión.
QByteArray readPlane(Reader r, int compression, int w, int h, int depth, bool psb, bool color)
{
    if (w <= 0 || h <= 0)
        return {};
    const qint64 rowBytes = (qint64(w) * depth + 7) / 8;
    std::vector<uchar> raw(size_t(rowBytes * h), 0);
    switch (compression) {
    case 0: {
        const qint64 n = std::min<qint64>(r.remaining(), rowBytes * h);
        std::memcpy(raw.data(), r.take(n), size_t(n));
        break;
    }
    case 1: {
        const std::vector<quint32> counts = readCounts(r, h, psb);
        for (int y = 0; y < h; ++y) {
            const qint64 n = std::min<qint64>(counts[size_t(y)], r.remaining());
            unpackRow(r.take(n), n, raw.data() + y * rowBytes, rowBytes);
        }
        break;
    }
    case 2:
    case 3:
        inflateInto(r.here(), r.remaining(), raw.data(), rowBytes * h);
        if (compression == 3)
            unpredict(raw.data(), w, h, depth);
        break;
    default: throw ParseError(QObject::tr("El archivo usa una compresión desconocida."));
    }
    QByteArray plane(qint64(w) * h, Qt::Uninitialized);
    toEight(raw.data(), w, h, depth, color, reinterpret_cast<uchar*>(plane.data()));
    return plane;
}

// --- Estructura del documento ----------------------------------------------------------------

struct Header {
    bool psb = false;
    int channels = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    int mode = 0;
};

struct Resources {
    double dpi = 0;
    QByteArray icc;
    // -1: el archivo no lo dice (entonces sí la tiene).
    int realMergedData = -1;
    int targetLayer = -1;
    int transparentIndex = -1;
    // Nombres de los canales alfa (selecciones guardadas) de la imagen combinada.
    bool alphaNames = false;
};

struct Channel {
    int id = 0;
    quint64 length = 0;
    // Dónde empiezan sus datos (los 2 bytes de la compresión incluidos).
    const uchar* data = nullptr;
};

struct Mask {
    bool present = false;
    QRect rect;
    int background = 0;
    int flags = 0;
    int density = 255;
    double feather = 0;
    bool realPresent = false;
    QRect realRect;
    int realBackground = 0;
    int realFlags = 0;
};

struct Record {
    QRect rect;
    std::vector<Channel> channels;
    QByteArray blend;
    int opacity = 255;
    int clipping = 0;
    int flags = 0;
    QString name;
    Mask mask;
    // 'lsct': 0 capa, 1 o 2 grupo (abierto o cerrado; va encima de sus capas), 3 fin de grupo
    // (debajo de ellas).
    int section = 0;
    QByteArray sectionBlend;
    int fillOpacity = 255;
    bool effects = false;
    bool vectorMask = false;
    bool adjustment = false;
    bool text = false;
    bool smartObject = false;
    bool knockout = false;

    bool hidden() const { return flags & 0x02; }
    const Channel* channel(int id) const
    {
        for (const Channel& c : channels)
            if (c.id == id)
                return &c;
        return nullptr;
    }
};

struct Parsed {
    Header header;
    Resources resources;
    QByteArray palette;
    std::vector<Record> records;
    bool mergedAlpha = false;
    // Datos de la imagen combinada (desde la compresión hasta el final del archivo).
    const uchar* imageData = nullptr;
    qint64 imageDataSize = 0;
};

// Los modos de fusión que Kaint y Photoshop tienen igual.
struct BlendKey {
    BlendMode mode;
    const char* key;
};
constexpr BlendKey kBlendKeys[] = {
    {BlendMode::Normal, "norm"},     {BlendMode::Multiply, "mul "},  {BlendMode::Additive, "lddg"},
    {BlendMode::ColorBurn, "idiv"},  {BlendMode::ColorDodge, "div "}, {BlendMode::Overlay, "over"},
    {BlendMode::Difference, "diff"}, {BlendMode::Lighten, "lite"},   {BlendMode::Darken, "dark"},
    {BlendMode::Screen, "scrn"},
};

std::optional<BlendMode> blendFromKey(const QByteArray& key)
{
    for (const BlendKey& b : kBlendKeys)
        if (key == b.key)
            return b.mode;
    return std::nullopt;
}

const char* keyFromBlend(BlendMode mode)
{
    for (const BlendKey& b : kBlendKeys)
        if (b.mode == mode)
            return b.key;
    return nullptr;
}

// Nombre de un modo de Photoshop que Kaint no tiene (para el aviso).
QString photoshopBlendName(const QByteArray& key)
{
    static const QHash<QByteArray, const char*> names{
        {"diss", QT_TRANSLATE_NOOP("QObject", "Disolver")},
        {"lbrn", QT_TRANSLATE_NOOP("QObject", "Subexposición lineal")},
        {"dkCl", QT_TRANSLATE_NOOP("QObject", "Color más oscuro")},
        {"lgCl", QT_TRANSLATE_NOOP("QObject", "Color más claro")},
        {"sLit", QT_TRANSLATE_NOOP("QObject", "Luz suave")},
        {"hLit", QT_TRANSLATE_NOOP("QObject", "Luz fuerte")},
        {"vLit", QT_TRANSLATE_NOOP("QObject", "Luz intensa")},
        {"lLit", QT_TRANSLATE_NOOP("QObject", "Luz lineal")},
        {"pLit", QT_TRANSLATE_NOOP("QObject", "Luz focal")},
        {"hMix", QT_TRANSLATE_NOOP("QObject", "Mezcla definida")},
        {"smud", QT_TRANSLATE_NOOP("QObject", "Exclusión")},
        {"fsub", QT_TRANSLATE_NOOP("QObject", "Restar")},
        {"fdiv", QT_TRANSLATE_NOOP("QObject", "Dividir")},
        {"hue ", QT_TRANSLATE_NOOP("QObject", "Tono")},
        {"sat ", QT_TRANSLATE_NOOP("QObject", "Saturación")},
        {"colr", QT_TRANSLATE_NOOP("QObject", "Color")},
        {"lum ", QT_TRANSLATE_NOOP("QObject", "Luminosidad")},
    };
    const auto it = names.constFind(key);
    return it != names.cend() ? QObject::tr(it.value()) : QString::fromLatin1(key).trimmed();
}

// Efectos de capa (descriptor 'lfx2' o 'lmfx'): encendidos si el interruptor general no está apagado
// y alguno tiene «enab» a verdadero.
bool effectsOn(const QByteArray& descriptor)
{
    const int master = int(descriptor.indexOf("masterFXSwitchbool"));
    if (master >= 0 && master + 18 < descriptor.size() && descriptor[master + 18] == 0)
        return false;
    return descriptor.contains(QByteArrayLiteral("enabbool\x01"));
}

void readResources(Reader r, Resources& res)
{
    while (r.remaining() >= 12) {
        const QByteArray sig = r.key();
        if (sig != "8BIM" && sig != "MeSa" && sig != "AgHg" && sig != "PHUT" && sig != "DCSR")
            break;
        const int id = r.u16();
        const int nameLen = r.u8();
        r.skip(nameLen + ((nameLen + 1) & 1));
        const quint32 size = r.u32();
        Reader d = r.sub(std::min<quint64>(size, quint64(r.remaining())));
        if (size & 1 && r.remaining() > 0)
            r.skip(1);
        switch (id) {
        case 1005: // ResolutionInfo: siempre en píxeles por pulgada (16.16).
            if (d.remaining() >= 4)
                res.dpi = d.u32() / 65536.0;
            break;
        case 1039: res.icc = d.bytes(d.remaining()); break;
        case 1057: // Información de la versión: si la imagen combinada es de verdad.
            if (d.remaining() >= 5) {
                d.skip(4);
                res.realMergedData = d.u8();
            }
            break;
        case 1024:
            if (d.remaining() >= 2)
                res.targetLayer = d.u16();
            break;
        case 1047:
            if (d.remaining() >= 2)
                res.transparentIndex = d.u16();
            break;
        case 1006:
        case 1045: res.alphaNames = d.remaining() > 0; break;
        default: break;
        }
    }
}

Record readRecord(Reader& r, bool psb)
{
    Record rec;
    const qint32 top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
    rec.rect = rectOf(top, left, bottom, right);
    const int count = r.u16();
    if (count > 56)
        throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
    for (int i = 0; i < count; ++i) {
        Channel c;
        c.id = r.i16();
        c.length = psb ? r.u64() : r.u32();
        rec.channels.push_back(c);
    }
    if (r.key() != "8BIM")
        throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
    rec.blend = r.key();
    rec.opacity = r.u8();
    rec.clipping = r.u8();
    rec.flags = r.u8();
    r.skip(1);
    Reader extra = r.sub(r.u32());

    // Máscara de capa.
    if (const quint32 maskLen = extra.u32(); maskLen > 0) {
        Reader m = extra.sub(maskLen);
        if (m.remaining() >= 18) {
            Mask& mask = rec.mask;
            const qint32 t = m.i32(), l = m.i32(), b = m.i32(), rt = m.i32();
            mask.rect = rectOf(t, l, b, rt);
            mask.background = m.u8();
            mask.flags = m.u8();
            mask.present = true;
            if (maskLen > 20) {
                if (mask.flags & 0x10 && m.remaining() >= 1) {
                    const int params = m.u8();
                    if (params & 1)
                        mask.density = m.u8();
                    if (params & 2) {
                        const quint64 bits = m.u64();
                        std::memcpy(&mask.feather, &bits, 8);
                    }
                    if (params & 4)
                        m.skip(1);
                    if (params & 8)
                        m.skip(8);
                }
                if (m.remaining() >= 18) {
                    mask.realFlags = m.u8();
                    mask.realBackground = m.u8();
                    const qint32 t2 = m.i32(), l2 = m.i32(), b2 = m.i32(), r2 = m.i32();
                    mask.realRect = rectOf(t2, l2, b2, r2);
                    mask.realPresent = true;
                }
            }
        }
    }
    // Rangos de fusión (no se usan).
    extra.skip(extra.u32());
    // Nombre en Pascal, con relleno hasta un múltiplo de 4.
    const int nameLen = extra.u8();
    rec.name = QString::fromLatin1(extra.bytes(nameLen));
    extra.skip(std::min<qint64>((4 - (nameLen + 1) % 4) % 4, extra.remaining()));

    static const QList<QByteArray> adjustments{"SoCo", "GdFl", "PtFl", "brit", "levl", "curv", "expA",
                                               "vibA", "hue ", "hue2", "blnc", "blwh", "phfl", "mixr",
                                               "clrL", "nvrt", "post", "thrs", "grdm", "selc", "CgEd"};
    while (extra.remaining() >= 12) {
        const QByteArray sig = extra.key();
        if (sig != "8BIM" && sig != "8B64")
            break;
        const QByteArray key = extra.key();
        const quint64 len = psb && longBlock(key) ? extra.u64() : extra.u32();
        Reader b = extra.sub(std::min<quint64>(len, quint64(extra.remaining())));
        if (key == "luni" && b.remaining() >= 4) {
            const quint32 n = b.u32();
            if (n <= quint32(b.remaining() / 2)) {
                QString name(int(n), Qt::Uninitialized);
                for (quint32 i = 0; i < n; ++i)
                    name[i] = QChar(b.u16());
                // Algunos programas terminan el nombre con un cero.
                while (name.endsWith(QChar(0)))
                    name.chop(1);
                rec.name = name;
            }
        } else if ((key == "lsct" || key == "lsdk") && b.remaining() >= 4) {
            rec.section = int(b.u32());
            if (b.remaining() >= 8 && b.key() == "8BIM")
                rec.sectionBlend = b.key();
        } else if (key == "lfx2" || key == "lmfx") {
            rec.effects = rec.effects || effectsOn(b.bytes(b.remaining()));
        } else if ((key == "vmsk" || key == "vsms") && b.remaining() >= 8) {
            b.skip(4);
            // Bit 2: desactivada.
            rec.vectorMask = rec.vectorMask || !(b.u32() & 4);
        } else if (key == "iOpa" && b.remaining() >= 1) {
            rec.fillOpacity = b.u8();
        } else if (key == "TySh" || key == "tySh") {
            rec.text = true;
        } else if (key == "SoLd" || key == "PlLd" || key == "plLd" || key == "SoLE") {
            rec.smartObject = true;
        } else if (key == "knko" && b.remaining() >= 1) {
            rec.knockout = b.u8() != 0;
        } else if (adjustments.contains(key)) {
            rec.adjustment = true;
        }
        skipToSignature(extra);
    }
    return rec;
}

// Las capas: sus registros y después los datos de sus canales (se descomprimen más tarde, solo si
// hacen falta).
void readLayerInfo(Reader r, bool psb, Parsed& out)
{
    if (r.remaining() < 2)
        return;
    int count = r.i16();
    if (count < 0) {
        out.mergedAlpha = true;
        count = -count;
    }
    std::vector<Record> records;
    records.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        records.push_back(readRecord(r, psb));
    for (Record& rec : records)
        for (Channel& c : rec.channels) {
            if (c.length > quint64(r.remaining()))
                truncated();
            c.data = r.here();
            r.skip(qint64(c.length));
        }
    out.records = std::move(records);
}

Parsed parse(const uchar* data, qint64 size)
{
    Parsed p;
    Reader r(data, size);
    if (size < 26 || r.key() != "8BPS")
        throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
    const int version = r.u16();
    if (version != 1 && version != 2)
        throw ParseError(QObject::tr("Versión de Photoshop desconocida."));
    Header& h = p.header;
    h.psb = version == 2;
    r.skip(6);
    h.channels = r.u16();
    h.height = int(std::min<quint32>(r.u32(), 0x7fffffff));
    h.width = int(std::min<quint32>(r.u32(), 0x7fffffff));
    h.depth = r.u16();
    h.mode = r.u16();
    const int maxSide = h.psb ? 300000 : 30000;
    if (h.channels < 1 || h.channels > 56 || h.width < 1 || h.height < 1 || h.width > maxSide || h.height > maxSide)
        throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
    if (qint64(h.width) * h.height > kMaxPixels)
        throw ParseError(QObject::tr("La imagen es demasiado grande para Kaint (%1 × %2 píxeles).").arg(h.width).arg(h.height));
    if (h.depth != 1 && h.depth != 8 && h.depth != 16 && h.depth != 32)
        throw ParseError(QObject::tr("Profundidad de color no admitida (%1 bits).").arg(h.depth));
    switch (h.mode) {
    case kRgb:
    case kGrayscale:
    case kDuotone:
        if (h.depth == 1)
            throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
        break;
    case kIndexed:
        if (h.depth != 8)
            throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
        break;
    case kBitmap:
        if (h.depth != 1)
            throw ParseError(QObject::tr("No es un documento de Photoshop válido."));
        break;
    case kCmyk: throw ParseError(QObject::tr("Kaint no abre documentos de Photoshop en CMYK. Pásalo a RGB en Photoshop."));
    case kLab: throw ParseError(QObject::tr("Kaint no abre documentos de Photoshop en color Lab. Pásalo a RGB en Photoshop."));
    default: throw ParseError(QObject::tr("Kaint no abre documentos de Photoshop multicanal. Pásalo a RGB en Photoshop."));
    }

    // Datos del modo de color (la paleta de los indexados).
    Reader colorData = r.sub(r.u32());
    if (h.mode == kIndexed && colorData.remaining() >= 768)
        p.palette = colorData.bytes(768);
    readResources(r.sub(r.u32()), p.resources);

    Reader layers = r.sub(h.psb ? r.u64() : r.u32());
    if (layers.remaining() > 0) {
        const quint64 infoLen = h.psb ? layers.u64() : layers.u32();
        readLayerInfo(layers.sub(std::min<quint64>(infoLen, quint64(layers.remaining()))), h.psb, p);
        if (layers.remaining() >= 4) {
            const qint64 globalMask = layers.u32();
            layers.skip(std::min(globalMask, layers.remaining()));
        }
        // Con 16 y 32 bits las capas van en un bloque al final ('Lr16', 'Lr32').
        skipToSignature(layers);
        while (layers.remaining() >= 12) {
            const QByteArray sig = layers.key();
            if (sig != "8BIM" && sig != "8B64")
                break;
            const QByteArray key = layers.key();
            const quint64 len = h.psb && longBlock(key) ? layers.u64() : layers.u32();
            Reader b = layers.sub(std::min<quint64>(len, quint64(layers.remaining())));
            if ((key == "Lr16" || key == "Lr32" || key == "Layr") && p.records.empty())
                readLayerInfo(b, h.psb, p);
            skipToSignature(layers);
        }
    }
    p.imageData = r.here();
    p.imageDataSize = r.remaining();
    return p;
}

// La imagen combinada que guarda el archivo; nula si no está o no se entiende.
QImage readMerged(const Parsed& p, bool withAlpha)
{
    const Header& h = p.header;
    try {
        Reader r(p.imageData, p.imageDataSize);
        const int compression = r.u16();
        const int colors = h.mode == kRgb ? 3 : 1;
        if (h.channels < colors)
            return {};
        const int used = std::min(h.channels, colors + (withAlpha ? 1 : 0));
        const qint64 rowBytes = (qint64(h.width) * h.depth + 7) / 8;
        const qint64 planeBytes = rowBytes * h.height;
        std::vector<uchar> raw(size_t(planeBytes * used), 0);
        switch (compression) {
        case 0:
            for (int c = 0; c < used; ++c) {
                const qint64 n = std::min(r.remaining(), planeBytes);
                std::memcpy(raw.data() + c * planeBytes, r.take(n), size_t(n));
            }
            break;
        case 1: {
            const std::vector<quint32> counts = readCounts(r, qint64(h.channels) * h.height, h.psb);
            for (int c = 0; c < used; ++c)
                for (int y = 0; y < h.height; ++y) {
                    const qint64 n = std::min<qint64>(counts[size_t(c) * h.height + y], r.remaining());
                    unpackRow(r.take(n), n, raw.data() + c * planeBytes + y * rowBytes, rowBytes);
                }
            break;
        }
        case 2:
        case 3:
            inflateInto(r.here(), r.remaining(), raw.data(), planeBytes * used);
            if (compression == 3)
                for (int c = 0; c < used; ++c)
                    unpredict(raw.data() + c * planeBytes, h.width, h.height, h.depth);
            break;
        default: return {};
        }
        const qint64 n = qint64(h.width) * h.height;
        std::vector<uchar> planes(size_t(n * used));
        for (int c = 0; c < used; ++c)
            toEight(raw.data() + c * planeBytes, h.width, h.height, h.depth, c < colors, planes.data() + c * n);
        raw = {};
        QImage image(h.width, h.height, kLayerFormat);
        if (image.isNull())
            return {};
        const uchar* p0 = planes.data();
        const uchar* p1 = colors == 3 ? p0 + n : p0;
        const uchar* p2 = colors == 3 ? p0 + 2 * n : p0;
        const uchar* pa = used > colors ? p0 + colors * n : nullptr;
        const uchar* pal = reinterpret_cast<const uchar*>(p.palette.constData());
        const bool indexed = h.mode == kIndexed && p.palette.size() >= 768;
        for (int y = 0; y < h.height; ++y) {
            QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));
            const qint64 row = qint64(y) * h.width;
            for (int x = 0; x < h.width; ++x) {
                const qint64 i = row + x;
                if (indexed) {
                    const int v = p0[i];
                    out[x] = qRgba(pal[v], pal[256 + v], pal[512 + v], v == p.resources.transparentIndex ? 0 : 255);
                } else {
                    out[x] = qRgba(p0[i], p1[i], p2[i], pa ? pa[i] : 255);
                }
            }
        }
        return image;
    } catch (const ParseError&) {
        return {};
    }
}

// Si la imagen combinada lleva transparencia: con capas, cuando el número de capas era negativo;
// sin capas, el canal de más (si no es un canal alfa con nombre, una selección guardada).
bool mergedHasAlpha(const Parsed& p)
{
    if (p.mergedAlpha)
        return true;
    const int colors = p.header.mode == kRgb ? 3 : 1;
    return p.records.empty() && p.header.channels > colors && !p.resources.alphaNames
           && (p.header.mode == kRgb || p.header.mode == kGrayscale);
}

// Lo que cambió al pasar las capas a Kaint (para el aviso).
struct Changes {
    bool cropped = false;
    bool masked = false;
    bool maskDropped = false;
};

// Una capa de Kaint (del tamaño del lienzo) con los canales de 'rec' y su máscara aplicada a la
// transparencia.
QImage layerImage(const Parsed& p, const Record& rec, Changes& changes)
{
    const Header& h = p.header;
    QImage image(h.width, h.height, kLayerFormat);
    if (image.isNull())
        throw ParseError(QObject::tr("No hay memoria suficiente para abrir la imagen."));
    image.fill(kTransparent);
    const QRect canvas(0, 0, h.width, h.height);
    const QRect visible = rec.rect & canvas;
    if (!rec.rect.isEmpty() && visible != rec.rect)
        changes.cropped = true;
    if (rec.mask.present && (rec.mask.flags & 0x02))
        changes.maskDropped = true;
    if (visible.isEmpty())
        return image;

    const bool gray = h.mode != kRgb;
    auto plane = [&](int id, const QRect& r) -> QByteArray {
        const Channel* c = rec.channel(id);
        if (!c || c->length < 2 || r.isEmpty())
            return {};
        Reader cr(c->data, qint64(c->length));
        const int compression = cr.u16();
        return readPlane(cr, compression, r.width(), r.height(), h.depth, h.psb, id >= 0);
    };
    const QByteArray c0 = plane(0, rec.rect);
    const QByteArray c1 = gray ? QByteArray() : plane(1, rec.rect);
    const QByteArray c2 = gray ? QByteArray() : plane(2, rec.rect);
    const QByteArray ca = plane(-1, rec.rect);

    // La máscara de píxeles (con una vectorial también, es la «real», -3).
    QByteArray mask;
    QRect maskRect;
    int maskBackground = 255, maskFlags = 0;
    if (rec.mask.present) {
        const bool real = rec.mask.realPresent && rec.channel(-3);
        maskRect = real ? rec.mask.realRect : rec.mask.rect;
        maskBackground = real ? rec.mask.realBackground : rec.mask.background;
        maskFlags = real ? rec.mask.realFlags : rec.mask.flags;
        // Bit 1: desactivada.
        if (!(maskFlags & 0x02)) {
            mask = plane(real ? -3 : -2, maskRect);
            // Sin píxeles y con el fondo blanco no oculta nada.
            if (mask.isEmpty() && maskBackground == 255 && !(maskFlags & 0x04))
                maskFlags |= 0x02;
            else
                changes.masked = true;
        }
    }
    const bool useMask = rec.mask.present && !(maskFlags & 0x02);
    const bool invert = maskFlags & 0x04;
    const int density = rec.mask.density;

    const int rw = rec.rect.width();
    for (int y = visible.top(); y <= visible.bottom(); ++y) {
        QRgb* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        const qint64 row = qint64(y - rec.rect.top()) * rw;
        const bool maskRow = useMask && y >= maskRect.top() && y <= maskRect.bottom();
        for (int x = visible.left(); x <= visible.right(); ++x) {
            const qint64 i = row + (x - rec.rect.left());
            const int r = c0.isEmpty() ? 0 : uchar(c0[i]);
            const int g = gray ? r : c1.isEmpty() ? 0 : uchar(c1[i]);
            const int b = gray ? r : c2.isEmpty() ? 0 : uchar(c2[i]);
            int a = ca.isEmpty() ? 255 : uchar(ca[i]);
            if (useMask) {
                int m = maskBackground;
                if (maskRow && x >= maskRect.left() && x <= maskRect.right() && !mask.isEmpty())
                    m = uchar(mask[qint64(y - maskRect.top()) * maskRect.width() + (x - maskRect.left())]);
                if (invert)
                    m = 255 - m;
                if (density < 255)
                    m = 255 - (255 - m) * density / 255;
                a = (a * m + 127) / 255;
            }
            out[x] = qRgba(r, g, b, a);
        }
    }
    return image;
}

// Una capa de Kaint lista, con sus propiedades.
struct Built {
    const Record* record;
    bool hidden;
};

Document* build(const Parsed& p, QString* warning)
{
    const Header& h = p.header;
    QStringList converted, unsupported;
    auto note = [](QStringList& list, const QString& text) {
        if (!list.contains(text))
            list << text;
    };
    if (h.psb)
        note(converted, QObject::tr("es un documento de tamaño grande (PSB), que Kaint no guarda"));
    if (h.depth == 16 || h.depth == 32)
        note(converted, QObject::tr("tiene %1 bits por canal y Kaint trabaja con 8").arg(h.depth));
    if (h.mode == kGrayscale || h.mode == kDuotone)
        note(converted, QObject::tr("está en escala de grises y se pasó a RGB"));
    if (h.mode == kIndexed)
        note(converted, QObject::tr("tiene una paleta de colores indexados y se pasó a RGB"));
    if (h.mode == kBitmap)
        note(converted, QObject::tr("es un mapa de bits en blanco y negro y se pasó a RGB"));

    // Grupos: los registros van de abajo arriba; el fin de un grupo (sección 3) va debajo de sus
    // capas y el propio grupo (1 o 2), encima, con sus propiedades.
    std::vector<std::vector<Built>> stack(1);
    bool groups = false;
    for (const Record& rec : p.records) {
        if (rec.section == 3) {
            stack.emplace_back();
            continue;
        }
        if (rec.section == 1 || rec.section == 2) {
            groups = true;
            if (stack.size() < 2)
                continue;
            std::vector<Built> children = std::move(stack.back());
            stack.pop_back();
            const QByteArray blend = rec.sectionBlend.isEmpty() ? rec.blend : rec.sectionBlend;
            if (rec.hidden()) {
                for (Built& b : children)
                    b.hidden = true;
            } else {
                // Igual que sin el grupo: de paso (o normal con todo normal dentro), opaco, sin
                // máscara activa ni efectos.
                bool allNormal = true;
                for (const Built& b : children)
                    allNormal = allNormal && (b.record->blend == "norm" || b.hidden);
                const bool plain = (blend == "pass" || (blend == "norm" && allNormal)) && rec.opacity == 255
                                   && rec.fillOpacity == 255 && !rec.effects && !rec.vectorMask
                                   && !(rec.mask.present && !(rec.mask.flags & 0x02));
                if (!plain)
                    note(unsupported, QObject::tr("grupos con opacidad, máscara, efectos o modo de fusión propios"));
            }
            for (Built& b : children)
                stack.back().push_back(b);
            continue;
        }
        stack.back().push_back({&rec, rec.hidden()});
    }
    // Grupos sin cerrar: sus capas quedan como están.
    while (stack.size() > 1) {
        std::vector<Built> rest = std::move(stack.back());
        stack.pop_back();
        for (Built& b : rest)
            stack.back().push_back(b);
    }
    const std::vector<Built>& layers = stack.front();
    if (groups)
        note(converted, QObject::tr("tenía grupos de capas, que Kaint no tiene (sus capas se conservan)"));

    for (const Built& b : layers) {
        const Record& rec = *b.record;
        if (b.hidden) {
            if (!blendFromKey(rec.blend))
                note(converted, QObject::tr("capas ocultas con modos de fusión que Kaint no tiene pasaron a Normal"));
            continue;
        }
        if (rec.clipping)
            note(unsupported, QObject::tr("máscaras de recorte"));
        if (rec.effects)
            note(unsupported, QObject::tr("efectos de capa (sombras, trazos, resplandores…)"));
        if (rec.vectorMask)
            note(unsupported, QObject::tr("máscaras vectoriales o capas de forma"));
        if (rec.adjustment)
            note(unsupported, QObject::tr("capas de ajuste o de relleno"));
        if (rec.knockout)
            note(unsupported, QObject::tr("capas que calan a las de debajo"));
        if (rec.mask.present && !(rec.mask.flags & 0x02) && rec.mask.feather > 0)
            note(unsupported, QObject::tr("máscaras de capa difuminadas"));
        if (!blendFromKey(rec.blend))
            note(unsupported, QObject::tr("el modo de fusión «%1»").arg(photoshopBlendName(rec.blend)));
    }

    const bool alpha = mergedHasAlpha(p);
    const int colors = h.mode == kRgb ? 3 : 1;
    if (h.channels > colors + (alpha ? 1 : 0))
        note(converted, QObject::tr("tiene canales alfa o de tinta plana, que no se conservan"));

    auto finish = [&](Document* doc, bool fallback) {
        if (p.resources.dpi >= 1 && p.resources.dpi <= 100000)
            doc->setDpi(p.resources.dpi);
        if (!p.resources.icc.isEmpty()) {
            const QColorSpace space = QColorSpace::fromIccProfile(p.resources.icc);
            if (space.isValid())
                doc->setColorSpace(space);
        }
        if (warning && (fallback || !converted.isEmpty())) {
            QString text;
            if (fallback) {
                text = QObject::tr("Este documento de Photoshop usa cosas que las capas de Kaint no pueden representar sin "
                                   "cambiar su aspecto:")
                       + QStringLiteral("\n• ") + unsupported.join(QStringLiteral("\n• "))
                       + QStringLiteral("\n\n")
                       + QObject::tr("Se abrió la imagen combinada que guarda el archivo, en una sola capa.");
                if (!converted.isEmpty())
                    text += QStringLiteral("\n\n") + QObject::tr("Además, el documento:") + QStringLiteral("\n• ")
                            + converted.join(QStringLiteral("\n• "));
            } else {
                text = QObject::tr("Al abrir este documento de Photoshop, Kaint tuvo que convertirlo porque:")
                       + QStringLiteral("\n• ") + converted.join(QStringLiteral("\n• "));
            }
            text += QStringLiteral("\n\n")
                    + QObject::tr("Se abre como una imagen nueva: al guardar se pide un nombre y el archivo original no se "
                                  "pisa.");
            *warning = text;
        }
        return doc;
    };

    // Sin capas, o con algo que no se puede representar: la imagen combinada.
    if (layers.empty() || !unsupported.isEmpty()) {
        const bool fallback = !layers.empty();
        if (p.resources.realMergedData != 0 || layers.empty()) {
            const QImage merged = readMerged(p, alpha);
            if (!merged.isNull())
                return finish(Document::createFromImage(merged, QObject::tr("Fondo")), fallback);
            if (layers.empty())
                throw ParseError(QObject::tr("No se pudo leer la imagen combinada del documento."));
        }
        // Sin imagen combinada: las capas, lo más parecido posible.
        note(converted, QObject::tr("usa cosas de Photoshop que Kaint no tiene (%1), que se ignoraron")
                            .arg(unsupported.join(QStringLiteral(", "))));
        unsupported.clear();
    }

    Changes changes;
    bool rasterized = false, fill = false;
    std::unique_ptr<Document> doc(new Document(QSize(h.width, h.height)));
    doc->setHistoryRootLabel(QObject::tr("Abrir imagen"));
    int active = -1;
    for (const Built& b : layers) {
        const Record& rec = *b.record;
        auto layer = std::make_shared<Layer>(rec.name.isEmpty() ? doc->nextLayerName() : rec.name, layerImage(p, rec, changes));
        layer->props.visible = !b.hidden;
        layer->props.blend = blendFromKey(rec.blend).value_or(BlendMode::Normal);
        layer->props.opacity = rec.opacity;
        if (rec.fillOpacity < 255) {
            layer->props.opacity = (rec.opacity * rec.fillOpacity + 127) / 255;
            fill = true;
        }
        rasterized = rasterized || rec.text || rec.smartObject;
        if (p.resources.targetLayer >= 0 && &rec == &p.records[size_t(std::min<qint64>(p.resources.targetLayer, qint64(p.records.size()) - 1))])
            active = doc->layerCount();
        doc->insertLayer(doc->layerCount(), layer);
    }
    doc->setActiveIndex(active >= 0 ? active : doc->layerCount() - 1);
    if (changes.masked)
        note(converted, QObject::tr("tenía máscaras de capa, que se aplicaron a la transparencia"));
    if (changes.maskDropped)
        note(converted, QObject::tr("tenía máscaras de capa desactivadas, que no se conservan"));
    if (changes.cropped)
        note(converted, QObject::tr("tenía partes de capas fuera del lienzo, que se recortaron"));
    if (rasterized)
        note(converted, QObject::tr("tenía texto u objetos inteligentes, que quedan como píxeles"));
    if (fill)
        note(converted, QObject::tr("la opacidad de relleno se combinó con la opacidad de la capa"));
    return finish(doc.release(), false);
}

// --- Escritura ---------------------------------------------------------------------------------

void put16(QByteArray& out, quint32 v)
{
    out.append(char(v >> 8));
    out.append(char(v));
}

void put32(QByteArray& out, quint32 v)
{
    out.append(char(v >> 24));
    out.append(char(v >> 16));
    out.append(char(v >> 8));
    out.append(char(v));
}

void set32(QByteArray& out, qsizetype at, quint32 v)
{
    out[at] = char(v >> 24);
    out[at + 1] = char(v >> 16);
    out[at + 2] = char(v >> 8);
    out[at + 3] = char(v);
}

void padTo(QByteArray& out, qsizetype from, int multiple)
{
    while ((out.size() - from) % multiple)
        out.append('\0');
}

// PackBits: repeticiones de 3 o más bytes como (1 - n, byte) y el resto tal cual, en tramos de 128.
void packBits(const uchar* src, int n, QByteArray& out)
{
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i])
            ++run;
        if (run >= 3) {
            out.append(char(1 - run));
            out.append(char(src[i]));
            i += run;
            continue;
        }
        const int start = i;
        int len = 0;
        while (i < n && len < 128) {
            if (i + 2 < n && src[i] == src[i + 1] && src[i] == src[i + 2])
                break;
            ++i;
            ++len;
        }
        out.append(char(len - 1));
        out.append(reinterpret_cast<const char*>(src + start), len);
    }
}

// Un canal (0 rojo, 1 verde, 2 azul, 3 alfa) del rectángulo 'r' de 'image', comprimido con PackBits:
// la longitud de cada fila y luego las filas.
void packChannel(const QImage& image, const QRect& r, int channel, QByteArray& counts, QByteArray& data)
{
    std::vector<uchar> row(size_t(r.width()));
    const int shift = channel == 0 ? 16 : channel == 1 ? 8 : channel == 2 ? 0 : 24;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(image.constScanLine(y)) + r.left();
        for (int x = 0; x < r.width(); ++x)
            row[size_t(x)] = uchar(src[x] >> shift);
        const qsizetype before = data.size();
        packBits(row.data(), r.width(), data);
        put16(counts, quint32(data.size() - before));
    }
}

// El rectángulo con algo visible (alfa > 0) de la capa.
QRect usedRect(const QImage& image)
{
    int top = -1, bottom = -1, left = image.width(), right = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        int first = -1, last = -1;
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x])) {
                if (first < 0)
                    first = x;
                last = x;
            }
        if (first < 0)
            continue;
        if (top < 0)
            top = y;
        bottom = y;
        left = std::min(left, first);
        right = std::max(right, last);
    }
    if (top < 0)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

void putResource(QByteArray& out, int id, const QByteArray& data)
{
    out.append("8BIM", 4);
    put16(out, quint32(id));
    put16(out, 0); // nombre vacío (longitud y relleno)
    put32(out, quint32(data.size()));
    out.append(data);
    if (data.size() & 1)
        out.append('\0');
}

} // namespace

namespace Psd {

bool canWriteBlend(int blendMode)
{
    return keyFromBlend(BlendMode(blendMode)) != nullptr;
}

Document* decode(const QByteArray& data, QString* error, QString* warning)
{
    try {
        const Parsed p = parse(reinterpret_cast<const uchar*>(data.constData()), data.size());
        return build(p, warning);
    } catch (const ParseError& e) {
        if (error)
            *error = e.message;
    } catch (const std::bad_alloc&) {
        if (error)
            *error = QObject::tr("No hay memoria suficiente para abrir la imagen.");
    }
    return nullptr;
}

Document* load(const QString& path, QString* error, QString* warning)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return nullptr;
    }
    // Proyectado en memoria (un PSB puede ocupar varios GB): sin copiarlo entero.
    if (uchar* mapped = file.map(0, file.size())) {
        const QByteArray view = QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), file.size());
        return decode(view, error, warning);
    }
    return decode(file.readAll(), error, warning);
}

QImage decodeComposite(const QByteArray& data, QString* error)
{
    try {
        const Parsed p = parse(reinterpret_cast<const uchar*>(data.constData()), data.size());
        if (p.resources.realMergedData != 0) {
            const QImage merged = readMerged(p, mergedHasAlpha(p));
            if (!merged.isNull())
                return merged;
        }
        const std::unique_ptr<Document> doc(build(p, nullptr));
        return doc->flattened();
    } catch (const ParseError& e) {
        if (error)
            *error = e.message;
    } catch (const std::bad_alloc&) {
        if (error)
            *error = QObject::tr("No hay memoria suficiente para abrir la imagen.");
    }
    return {};
}

QImage readComposite(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    if (uchar* mapped = file.map(0, file.size()))
        return decodeComposite(QByteArray::fromRawData(reinterpret_cast<const char*>(mapped), file.size()), error);
    return decodeComposite(file.readAll(), error);
}

QByteArray encode(const Document* doc, QString* error)
{
    const int w = doc->size().width(), h = doc->size().height();
    if (w > 30000 || h > 30000) {
        if (error)
            *error = QObject::tr("PSD admite imágenes de hasta 30000 × 30000 píxeles. Guárdala como PDN u ORA para "
                                 "conservar las capas.");
        return {};
    }
    for (const LayerPtr& layer : doc->layers())
        if (!keyFromBlend(layer->props.blend)) {
            if (error)
                *error = QObject::tr("La capa «%1» usa el modo de fusión «%2», que Photoshop no tiene. Cámbialo en sus "
                                     "propiedades o guarda la imagen como PDN u ORA.")
                             .arg(layer->props.name, blendModeName(layer->props.blend));
            return {};
        }

    const QImage composite = doc->flattened().convertToFormat(kLayerFormat);
    bool alpha = false;
    for (int y = 0; y < h && !alpha; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(composite.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(row[x]) != 255) {
                alpha = true;
                break;
            }
    }
    const int channels = alpha ? 4 : 3;

    QByteArray out;
    out.append("8BPS", 4);
    put16(out, 1);
    out.append(6, '\0');
    put16(out, quint32(channels));
    put32(out, quint32(h));
    put32(out, quint32(w));
    put16(out, 8);
    put16(out, kRgb);
    put32(out, 0); // sin datos del modo de color

    // Recursos: resolución, capa activa y perfil de color (si no es sRGB).
    QByteArray resources;
    {
        QByteArray res;
        const quint32 fixed = quint32(std::lround(std::clamp(doc->dpi(), 1.0, 30000.0) * 65536.0));
        put32(res, fixed);
        put16(res, 1); // píxeles por pulgada
        put16(res, 1); // ancho en pulgadas
        put32(res, fixed);
        put16(res, 1);
        put16(res, 1);
        putResource(resources, 1005, res);
        QByteArray target;
        put16(target, quint32(std::max(0, doc->activeIndex())));
        putResource(resources, 1024, target);
        if (doc->colorSpace().isValid() && doc->colorSpace() != QColorSpace(QColorSpace::SRgb)) {
            const QByteArray icc = doc->colorSpace().iccProfile();
            if (!icc.isEmpty())
                putResource(resources, 1039, icc);
        }
    }
    put32(out, quint32(resources.size()));
    out.append(resources);

    // Capas y máscaras.
    const qsizetype sectionAt = out.size();
    put32(out, 0);
    const qsizetype infoAt = out.size();
    put32(out, 0);
    const int count = doc->layerCount();
    // Negativo: el primer canal alfa de la imagen combinada es su transparencia.
    put16(out, quint32(quint16(alpha ? -count : count)));
    struct Pixels {
        QByteArray channel[4];
    };
    std::vector<Pixels> pixels(static_cast<size_t>(count));
    static const int kIds[4] = {-1, 0, 1, 2};
    for (int i = 0; i < count; ++i) {
        const Layer* layer = doc->layer(i);
        const QRect r = usedRect(layer->image);
        Pixels& px = pixels[size_t(i)];
        for (int k = 0; k < 4; ++k) {
            QByteArray& c = px.channel[k];
            if (r.isEmpty()) {
                put16(c, 0); // sin comprimir y sin datos
                continue;
            }
            put16(c, 1);
            QByteArray counts, data;
            packChannel(layer->image, r, kIds[k] < 0 ? 3 : kIds[k], counts, data);
            c.append(counts);
            c.append(data);
        }
        put32(out, quint32(r.isEmpty() ? 0 : r.top()));
        put32(out, quint32(r.isEmpty() ? 0 : r.left()));
        put32(out, quint32(r.isEmpty() ? 0 : r.bottom() + 1));
        put32(out, quint32(r.isEmpty() ? 0 : r.right() + 1));
        put16(out, 4);
        for (int k = 0; k < 4; ++k) {
            put16(out, quint32(quint16(kIds[k])));
            put32(out, quint32(px.channel[k].size()));
        }
        out.append("8BIM", 4);
        out.append(keyFromBlend(layer->props.blend), 4);
        out.append(char(std::clamp(layer->props.opacity, 0, 255)));
        out.append('\0');                                    // sin recorte
        out.append(char(0x08 | (layer->props.visible ? 0 : 0x02))); // el bit 4 tiene sentido (y es 0)
        out.append('\0');
        const qsizetype extraAt = out.size();
        put32(out, 0);
        put32(out, 0); // sin máscara
        // Rangos de fusión: todo (gris y los tres canales).
        put32(out, 32);
        for (int k = 0; k < 8; ++k)
            put32(out, 0x0000ffff);
        // Nombre en Pascal (Latin-1) con relleno a 4 y, entero, en Unicode ('luni').
        const QByteArray latin = layer->props.name.toLatin1().left(255);
        const qsizetype nameAt = out.size();
        out.append(char(latin.size()));
        out.append(latin);
        padTo(out, nameAt, 4);
        const QString name = layer->props.name;
        out.append("8BIMluni", 8);
        const qsizetype lenAt = out.size();
        put32(out, 0);
        const qsizetype dataAt = out.size();
        put32(out, quint32(name.size()));
        for (QChar ch : name)
            put16(out, ch.unicode());
        padTo(out, dataAt, 4);
        set32(out, lenAt, quint32(out.size() - dataAt));
        set32(out, extraAt, quint32(out.size() - extraAt - 4));
    }
    for (const Pixels& px : pixels)
        for (const QByteArray& c : px.channel)
            out.append(c);
    padTo(out, infoAt + 4, 4);
    set32(out, infoAt, quint32(out.size() - infoAt - 4));
    put32(out, 0); // sin máscara global
    set32(out, sectionAt, quint32(out.size() - sectionAt - 4));

    // Imagen combinada, con PackBits.
    put16(out, 1);
    QByteArray counts, data;
    const QRect all(0, 0, w, h);
    for (int c = 0; c < channels; ++c)
        packChannel(composite, all, c, counts, data);
    out.append(counts);
    out.append(data);
    return out;
}

bool save(const Document* doc, const QString& path, QString* error)
{
    const QByteArray data = encode(doc, error);
    if (data.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace Psd
