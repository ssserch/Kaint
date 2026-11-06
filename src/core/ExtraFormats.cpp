#include "core/ExtraFormats.h"

#include <QList>
#include <QObject>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {

// Lo más grande que se abre (como el límite de memoria de QImageReader de Kaint): un archivo
// dañado no puede pedir gigas de memoria.
constexpr qint64 kMaxPixels = qint64(16384) * 16384;

// Lectura con límites: nada se lee fuera del archivo.
class Reader {
public:
    explicit Reader(const QByteArray& data)
        : m_data(reinterpret_cast<const uchar*>(data.constData()))
        , m_size(data.size())
    {
    }

    qint64 size() const { return m_size; }
    bool has(qint64 at, qint64 count) const { return at >= 0 && count >= 0 && at <= m_size && count <= m_size - at; }
    uchar u8(qint64 at) const { return has(at, 1) ? m_data[at] : 0; }
    quint32 u16(qint64 at) const { return has(at, 2) ? quint32(m_data[at]) | quint32(m_data[at + 1]) << 8 : 0; }
    quint32 u32(qint64 at) const
    {
        return has(at, 4) ? quint32(m_data[at]) | quint32(m_data[at + 1]) << 8 | quint32(m_data[at + 2]) << 16
                                | quint32(m_data[at + 3]) << 24
                          : 0;
    }
    quint32 u32be(qint64 at) const
    {
        return has(at, 4) ? quint32(m_data[at]) << 24 | quint32(m_data[at + 1]) << 16 | quint32(m_data[at + 2]) << 8
                                | quint32(m_data[at + 3])
                          : 0;
    }
    bool startsWith(const char* magic) const
    {
        const auto n = qint64(std::strlen(magic));
        return has(0, n) && std::memcmp(m_data, magic, size_t(n)) == 0;
    }

private:
    const uchar* m_data;
    qint64 m_size;
};

void put8(QByteArray& out, quint32 v)
{
    out.append(char(v & 0xff));
}

void put16(QByteArray& out, quint32 v)
{
    put8(out, v);
    put8(out, v >> 8);
}

void put32(QByteArray& out, quint32 v)
{
    put16(out, v);
    put16(out, v >> 16);
}

void put32be(QByteArray& out, quint32 v)
{
    put8(out, v >> 24);
    put8(out, v >> 16);
    put8(out, v >> 8);
    put8(out, v);
}

QImage fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return {};
}

bool tooLarge(qint64 w, qint64 h)
{
    return w <= 0 || h <= 0 || w * h > kMaxPixels;
}

} // namespace

// --- TGA ---------------------------------------------------------------------------------------

namespace Tga {

QByteArray encode(const QImage& source)
{
    if (source.isNull() || source.width() > 0xffff || source.height() > 0xffff)
        return {};
    const bool indexed = source.format() == QImage::Format_Indexed8;
    const int depth = indexed ? 8 : source.hasAlphaChannel() ? 32 : 24;
    const QImage image = indexed ? source : source.convertToFormat(depth == 32 ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    const int w = image.width(), h = image.height();
    const QList<QRgb> table = indexed ? image.colorTable() : QList<QRgb>();
    const bool mapAlpha = std::any_of(table.cbegin(), table.cend(), [](QRgb c) { return qAlpha(c) < 255; });

    QByteArray out;
    out.reserve(18 + 1024 + qsizetype(w) * h * (depth / 8) + 26);
    put8(out, 0);
    put8(out, indexed ? 1 : 0);
    put8(out, indexed ? 1 : 2);
    put16(out, 0);
    put16(out, indexed ? 256 : 0);
    put8(out, indexed ? (mapAlpha ? 32 : 24) : 0);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint32(w));
    put16(out, quint32(h));
    put8(out, quint32(depth));
    // De arriba abajo; los bits de alfa de cada píxel (o de la paleta).
    put8(out, 0x20 | ((depth == 32 || mapAlpha) ? 8 : 0));
    if (indexed)
        for (int i = 0; i < 256; ++i) {
            const QRgb c = i < table.size() ? table[i] : qRgb(0, 0, 0);
            put8(out, quint32(qBlue(c)));
            put8(out, quint32(qGreen(c)));
            put8(out, quint32(qRed(c)));
            if (mapAlpha)
                put8(out, quint32(qAlpha(c)));
        }
    for (int y = 0; y < h; ++y) {
        const uchar* line = image.constScanLine(y);
        if (indexed) {
            out.append(reinterpret_cast<const char*>(line), w);
        } else if (depth == 24) {
            for (int x = 0; x < w; ++x) {
                put8(out, line[x * 3 + 2]);
                put8(out, line[x * 3 + 1]);
                put8(out, line[x * 3]);
            }
        } else {
            const auto* row = reinterpret_cast<const QRgb*>(line);
            for (int x = 0; x < w; ++x) {
                put8(out, quint32(qBlue(row[x])));
                put8(out, quint32(qGreen(row[x])));
                put8(out, quint32(qRed(row[x])));
                put8(out, quint32(qAlpha(row[x])));
            }
        }
    }
    // Pie de TGA 2.0 (sin él, algunos lectores, como el de Qt, no lo abren).
    put32(out, 0);
    put32(out, 0);
    out.append("TRUEVISION-XFILE.", 17);
    put8(out, 0);
    return out;
}

QImage decode(const QByteArray& data, QString* error)
{
    const Reader b(data);
    const QString damaged = QObject::tr("El archivo TGA no es válido o está dañado.");
    if (b.size() < 18)
        return fail(error, damaged);
    const int idLength = b.u8(0), mapType = b.u8(1), type = b.u8(2);
    const int mapFirst = int(b.u16(3)), mapLength = int(b.u16(5)), mapBits = b.u8(7);
    const int w = int(b.u16(12)), h = int(b.u16(14)), bits = b.u8(16), desc = b.u8(17);
    const bool mapped = type == 1 || type == 9, gray = type == 3 || type == 11, rle = type >= 9;
    if (!(mapped || gray || type == 2 || type == 10) || mapType > 1 || w == 0 || h == 0)
        return fail(error, damaged);
    if ((mapped && (mapType != 1 || bits != 8)) || (gray && bits != 8 && bits != 16)
        || (!mapped && !gray && bits != 15 && bits != 16 && bits != 24 && bits != 32))
        return fail(error, QObject::tr("Este tipo de archivo TGA no se puede abrir."));
    if (tooLarge(w, h))
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    const int alphaBits = desc & 15;

    auto color = [&b, alphaBits](int bitsPer, qint64 at, bool keepAlpha) -> QRgb {
        if (bitsPer == 15 || bitsPer == 16) {
            const quint32 v = b.u16(at);
            auto c = [v](int shift) { return int(((v >> shift) & 31) * 255 / 31); };
            return qRgba(c(10), c(5), c(0), bitsPer == 16 && alphaBits && !(v & 0x8000) ? 0 : 255);
        }
        if (bitsPer == 8)
            return qRgb(b.u8(at), b.u8(at), b.u8(at));
        // Sin bits de alfa declarados, el cuarto byte no es transparencia.
        return qRgba(b.u8(at + 2), b.u8(at + 1), b.u8(at), bitsPer == 32 && (keepAlpha || alphaBits) ? b.u8(at + 3) : 255);
    };

    qint64 pos = 18 + idLength;
    QList<QRgb> map;
    if (mapType == 1) {
        const int entryBytes = (mapBits + 7) / 8;
        if (entryBytes < 1 || entryBytes > 4 || !b.has(pos, qint64(mapLength) * entryBytes))
            return fail(error, damaged);
        map.reserve(mapLength);
        for (int i = 0; i < mapLength; ++i)
            map.append(color(mapBits == 24 || mapBits == 32 || mapBits == 15 || mapBits == 16 ? mapBits : 24, pos + qint64(i) * entryBytes,
                             true));
        pos += qint64(mapLength) * entryBytes;
    }
    const int pixelBytes = (bits + 7) / 8;
    auto pixel = [&](qint64 at) -> QRgb {
        if (mapped)
            return map.value(int(b.u8(at)) - mapFirst, qRgb(0, 0, 0));
        if (gray) {
            const int v = b.u8(at);
            return bits == 16 ? qRgba(v, v, v, b.u8(at + 1)) : qRgb(v, v, v);
        }
        return color(bits, at, false);
    };

    const qint64 n = qint64(w) * h;
    std::vector<QRgb> pixels(size_t(n), 0);
    if (!rle) {
        if (!b.has(pos, n * pixelBytes))
            return fail(error, damaged);
        for (qint64 i = 0; i < n; ++i)
            pixels[size_t(i)] = pixel(pos + i * pixelBytes);
    } else {
        for (qint64 i = 0; i < n;) {
            if (!b.has(pos, 1))
                return fail(error, damaged);
            const int head = b.u8(pos++), count = (head & 127) + 1;
            if (head & 128) {
                if (!b.has(pos, pixelBytes))
                    return fail(error, damaged);
                const QRgb c = pixel(pos);
                pos += pixelBytes;
                for (int k = 0; k < count && i < n; ++k)
                    pixels[size_t(i++)] = c;
            } else {
                if (!b.has(pos, qint64(count) * pixelBytes))
                    return fail(error, damaged);
                for (int k = 0; k < count && i < n; ++k, pos += pixelBytes)
                    pixels[size_t(i++)] = pixel(pos);
            }
        }
    }
    QImage out(w, h, QImage::Format_ARGB32);
    if (out.isNull())
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    const bool top = desc & 0x20, right = desc & 0x10;
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(top ? y : h - 1 - y));
        const QRgb* src = pixels.data() + qsizetype(y) * w;
        for (int x = 0; x < w; ++x)
            line[right ? w - 1 - x : x] = src[x];
    }
    return out;
}

} // namespace Tga

// --- DDS ---------------------------------------------------------------------------------------

namespace {

constexpr quint32 fourCC(const char (&s)[5])
{
    return quint32(uchar(s[0])) | quint32(uchar(s[1])) << 8 | quint32(uchar(s[2])) << 16 | quint32(uchar(s[3])) << 24;
}

// Un canal de un píxel sin comprimir con su máscara (de los bits que tenga a 8).
int channel(quint32 v, quint32 mask, int fallback)
{
    if (!mask)
        return fallback;
    int shift = 0;
    while (!((mask >> shift) & 1))
        ++shift;
    const quint32 raw = (v & mask) >> shift;
    int size = 0;
    while ((mask >> shift) >> size & 1)
        ++size;
    if (size >= 8)
        return int(raw >> (size - 8));
    return int((raw * 255 + ((1u << size) - 1) / 2) / ((1u << size) - 1));
}

// Los colores de un bloque BC1 (y de la parte de color de BC2 y BC3).
void colorBlock(const Reader& b, qint64 at, bool bc1, std::array<QRgb, 4>& colors)
{
    const quint32 v0 = b.u16(at), v1 = b.u16(at + 2);
    auto rgb = [](quint32 v) {
        const int r = int(v >> 11 & 31), g = int(v >> 5 & 63), bl = int(v & 31);
        return std::array<int, 3>{r << 3 | r >> 2, g << 2 | g >> 4, bl << 3 | bl >> 2};
    };
    const auto c0 = rgb(v0), c1 = rgb(v1);
    colors[0] = qRgb(c0[0], c0[1], c0[2]);
    colors[1] = qRgb(c1[0], c1[1], c1[2]);
    if (v0 > v1 || !bc1) {
        colors[2] = qRgb((2 * c0[0] + c1[0]) / 3, (2 * c0[1] + c1[1]) / 3, (2 * c0[2] + c1[2]) / 3);
        colors[3] = qRgb((c0[0] + 2 * c1[0]) / 3, (c0[1] + 2 * c1[1]) / 3, (c0[2] + 2 * c1[2]) / 3);
    } else {
        colors[2] = qRgb((c0[0] + c1[0]) / 2, (c0[1] + c1[1]) / 2, (c0[2] + c1[2]) / 2);
        colors[3] = qRgba(0, 0, 0, 0);
    }
}

// 1: BC1 (DXT1), 2: BC2 (DXT3), 3: BC3 (DXT5).
QImage decodeBlocks(const Reader& b, qint64 pos, int w, int h, int kind, QString* error)
{
    const qint64 bw = (qint64(w) + 3) / 4, bh = (qint64(h) + 3) / 4, blockBytes = kind == 1 ? 8 : 16;
    if (!b.has(pos, bw * bh * blockBytes))
        return fail(error, QObject::tr("El archivo DDS no es válido o está dañado."));
    QImage out(w, h, QImage::Format_ARGB32);
    if (out.isNull())
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    std::array<QRgb, 4> colors{};
    std::array<int, 16> alpha{};
    for (qint64 by = 0; by < bh; ++by)
        for (qint64 bx = 0; bx < bw; ++bx, pos += blockBytes) {
            const bool hasAlpha = kind != 1;
            if (kind == 2) {
                for (int i = 0; i < 16; ++i)
                    alpha[size_t(i)] = (b.u8(pos + (i >> 1)) >> ((i & 1) * 4) & 15) * 17;
            } else if (kind == 3) {
                const int a0 = b.u8(pos), a1 = b.u8(pos + 1);
                std::array<int, 8> table{a0, a1, 0, 0, 0, 0, 0, 0};
                if (a0 > a1) {
                    for (int i = 1; i < 7; ++i)
                        table[size_t(i + 1)] = ((7 - i) * a0 + i * a1) / 7;
                } else {
                    for (int i = 1; i < 5; ++i)
                        table[size_t(i + 1)] = ((5 - i) * a0 + i * a1) / 5;
                    table[6] = 0;
                    table[7] = 255;
                }
                quint64 bits = 0;
                for (int i = 5; i >= 0; --i)
                    bits = bits << 8 | b.u8(pos + 2 + i);
                for (int i = 0; i < 16; ++i)
                    alpha[size_t(i)] = table[size_t(bits >> (i * 3) & 7)];
            }
            const qint64 c = hasAlpha ? pos + 8 : pos;
            colorBlock(b, c, kind == 1, colors);
            const quint32 indices = b.u32(c + 4);
            for (int i = 0; i < 16; ++i) {
                const int x = int(bx * 4) + (i & 3), y = int(by * 4) + (i >> 2);
                if (x >= w || y >= h)
                    continue;
                QRgb px = colors[indices >> (i * 2) & 3];
                if (hasAlpha)
                    px = qRgba(qRed(px), qGreen(px), qBlue(px), alpha[size_t(i)]);
                reinterpret_cast<QRgb*>(out.scanLine(y))[x] = px;
            }
        }
    return out;
}

} // namespace

namespace Dds {

QByteArray encode(const QImage& source)
{
    if (source.isNull())
        return {};
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width(), h = image.height();
    QByteArray out;
    out.reserve(128 + qsizetype(w) * h * 4);
    out.append("DDS ", 4);
    // Cabecera: tamaño, lo que lleva (CAPS, HEIGHT, WIDTH, PITCH, PIXELFORMAT), alto, ancho, bytes
    // por fila, profundidad, mipmaps y lo reservado.
    put32(out, 124);
    put32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000);
    put32(out, quint32(h));
    put32(out, quint32(w));
    put32(out, quint32(w) * 4);
    put32(out, 0);
    put32(out, 0);
    for (int i = 0; i < 11; ++i)
        put32(out, 0);
    // Formato del píxel: ALPHAPIXELS | RGB, 32 bits, A8R8G8B8.
    put32(out, 32);
    put32(out, 0x41);
    put32(out, 0);
    put32(out, 32);
    put32(out, 0x00ff0000);
    put32(out, 0x0000ff00);
    put32(out, 0x000000ff);
    put32(out, 0xff000000);
    // TEXTURE y lo demás a cero.
    put32(out, 0x1000);
    for (int i = 0; i < 4; ++i)
        put32(out, 0);
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            put8(out, quint32(qBlue(row[x])));
            put8(out, quint32(qGreen(row[x])));
            put8(out, quint32(qRed(row[x])));
            put8(out, quint32(qAlpha(row[x])));
        }
    }
    return out;
}

QImage decode(const QByteArray& data, QString* error)
{
    const Reader b(data);
    const QString damaged = QObject::tr("El archivo DDS no es válido o está dañado.");
    const QString unsupported = QObject::tr("Este tipo de archivo DDS no se puede abrir.");
    if (!b.startsWith("DDS ") || b.size() < 128)
        return fail(error, damaged);
    const quint32 h = b.u32(12), w = b.u32(16), flags = b.u32(80), code = b.u32(84), bitCount = b.u32(88);
    const std::array<quint32, 4> masks{b.u32(92), b.u32(96), b.u32(100), b.u32(104)};
    if (tooLarge(w, h) || w > 65536 || h > 65536)
        return fail(error, w == 0 || h == 0 ? damaged : QObject::tr("La imagen es demasiado grande."));
    qint64 pos = 128;
    if (flags & 0x4) {
        int kind = 0;
        if (code == fourCC("DXT1"))
            kind = 1;
        else if (code == fourCC("DXT2") || code == fourCC("DXT3"))
            kind = 2;
        else if (code == fourCC("DXT4") || code == fourCC("DXT5"))
            kind = 3;
        if (code == fourCC("DX10")) {
            // Cabecera DX10: el formato DXGI.
            const quint32 dxgi = b.u32(128);
            pos = 148;
            switch (dxgi) {
            case 71: case 72: kind = 1; break;   // BC1
            case 74: case 75: kind = 2; break;   // BC2
            case 77: case 78: kind = 3; break;   // BC3
            case 28: case 29: kind = 10; break;  // R8G8B8A8
            case 87: case 91: kind = 11; break;  // B8G8R8A8
            case 88: case 93: kind = 12; break;  // B8G8R8X8
            default: return fail(error, unsupported);
            }
        }
        if (kind >= 1 && kind <= 3)
            return decodeBlocks(b, pos, int(w), int(h), kind, error);
        if (kind < 10)
            return fail(error, unsupported);
        if (!b.has(pos, qint64(w) * h * 4))
            return fail(error, damaged);
        QImage out(int(w), int(h), QImage::Format_ARGB32);
        if (out.isNull())
            return fail(error, QObject::tr("La imagen es demasiado grande."));
        for (int y = 0; y < int(h); ++y) {
            auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < int(w); ++x, pos += 4) {
                const int c0 = b.u8(pos), c1 = b.u8(pos + 1), c2 = b.u8(pos + 2), a = b.u8(pos + 3);
                line[x] = kind == 10 ? qRgba(c0, c1, c2, a) : qRgba(c2, c1, c0, kind == 12 ? 255 : a);
            }
        }
        return out;
    }
    // Sin comprimir: cada canal con su máscara (también grises, con LUMINANCE).
    if (bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32)
        return fail(error, unsupported);
    const int bytes = int(bitCount / 8);
    const qint64 pitch = qint64(w) * bytes;
    if (!b.has(pos, pitch * h))
        return fail(error, damaged);
    const bool luminance = flags & 0x20000, alphaPixels = flags & 0x1, alphaOnly = flags & 0x2;
    QImage out(int(w), int(h), QImage::Format_ARGB32);
    if (out.isNull())
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    for (int y = 0; y < int(h); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < int(w); ++x, pos += bytes) {
            quint32 v = 0;
            for (int k = bytes - 1; k >= 0; --k)
                v = v << 8 | b.u8(pos + k);
            const int a = alphaPixels || alphaOnly ? channel(v, masks[3], 255) : 255;
            if (alphaOnly && !masks[0]) {
                line[x] = qRgba(0, 0, 0, a);
            } else if (luminance) {
                const int l = channel(v, masks[0], 0);
                line[x] = qRgba(l, l, l, a);
            } else {
                line[x] = qRgba(channel(v, masks[0], 0), channel(v, masks[1], 0), channel(v, masks[2], 0), a);
            }
        }
    }
    return out;
}

} // namespace Dds

// --- QOI ---------------------------------------------------------------------------------------

namespace {

struct Px {
    uchar r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Px& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

int qoiHash(const Px& p)
{
    return (p.r * 3 + p.g * 5 + p.b * 7 + p.a * 11) % 64;
}

constexpr uchar kOpIndex = 0x00, kOpDiff = 0x40, kOpLuma = 0x80, kOpRun = 0xc0, kOpRgb = 0xfe, kOpRgba = 0xff;

} // namespace

namespace Qoi {

QByteArray encode(const QImage& source)
{
    if (source.isNull())
        return {};
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width(), h = image.height();
    bool alpha = false;
    for (int y = 0; y < h && !alpha; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x)
            if (qAlpha(row[x]) != 255) {
                alpha = true;
                break;
            }
    }
    QByteArray out;
    out.reserve(14 + qsizetype(w) * h * 2 + 8);
    out.append("qoif", 4);
    put32be(out, quint32(w));
    put32be(out, quint32(h));
    put8(out, alpha ? 4 : 3);
    put8(out, 0);
    std::array<Px, 64> index{};
    for (Px& p : index)
        p.a = 0;
    Px prev;
    int run = 0;
    const qint64 last = qint64(w) * h - 1;
    qint64 i = 0;
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < w; ++x, ++i) {
            const Px px{uchar(qRed(row[x])), uchar(qGreen(row[x])), uchar(qBlue(row[x])), uchar(qAlpha(row[x]))};
            if (px == prev) {
                if (++run == 62 || i == last) {
                    put8(out, kOpRun | (run - 1));
                    run = 0;
                }
                continue;
            }
            if (run > 0) {
                put8(out, kOpRun | (run - 1));
                run = 0;
            }
            const int slot = qoiHash(px);
            if (index[size_t(slot)] == px) {
                put8(out, kOpIndex | quint32(slot));
            } else {
                index[size_t(slot)] = px;
                if (px.a == prev.a) {
                    const auto vr = static_cast<signed char>(px.r - prev.r), vg = static_cast<signed char>(px.g - prev.g),
                               vb = static_cast<signed char>(px.b - prev.b);
                    const int vgr = vr - vg, vgb = vb - vg;
                    if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2) {
                        put8(out, kOpDiff | quint32((vr + 2) << 4 | (vg + 2) << 2 | (vb + 2)));
                    } else if (vgr > -9 && vgr < 8 && vg > -33 && vg < 32 && vgb > -9 && vgb < 8) {
                        put8(out, kOpLuma | quint32(vg + 32));
                        put8(out, quint32((vgr + 8) << 4 | (vgb + 8)));
                    } else {
                        put8(out, kOpRgb);
                        put8(out, px.r);
                        put8(out, px.g);
                        put8(out, px.b);
                    }
                } else {
                    put8(out, kOpRgba);
                    put8(out, px.r);
                    put8(out, px.g);
                    put8(out, px.b);
                    put8(out, px.a);
                }
            }
            prev = px;
        }
    }
    out.append(QByteArray(7, '\0'));
    put8(out, 1);
    return out;
}

QImage decode(const QByteArray& data, QString* error)
{
    const Reader b(data);
    const QString damaged = QObject::tr("El archivo QOI no es válido o está dañado.");
    if (!b.startsWith("qoif") || b.size() < 14 + 8)
        return fail(error, damaged);
    const quint32 w = b.u32be(4), h = b.u32be(8);
    const int channels = b.u8(12);
    if ((channels != 3 && channels != 4) || w == 0 || h == 0)
        return fail(error, damaged);
    if (tooLarge(w, h))
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    QImage out(int(w), int(h), QImage::Format_ARGB32);
    if (out.isNull())
        return fail(error, QObject::tr("La imagen es demasiado grande."));
    std::array<Px, 64> index{};
    for (Px& p : index)
        p.a = 0;
    Px px;
    int run = 0;
    qint64 pos = 14;
    const qint64 end = b.size() - 8;
    for (int y = 0; y < int(h); ++y) {
        auto* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < int(w); ++x) {
            if (run > 0) {
                --run;
            } else if (pos < end) {
                const uchar op = b.u8(pos++);
                if (op == kOpRgb) {
                    px.r = b.u8(pos);
                    px.g = b.u8(pos + 1);
                    px.b = b.u8(pos + 2);
                    pos += 3;
                } else if (op == kOpRgba) {
                    px.r = b.u8(pos);
                    px.g = b.u8(pos + 1);
                    px.b = b.u8(pos + 2);
                    px.a = b.u8(pos + 3);
                    pos += 4;
                } else if ((op & 0xc0) == kOpIndex) {
                    px = index[op];
                } else if ((op & 0xc0) == kOpDiff) {
                    px.r = uchar(px.r + ((op >> 4) & 3) - 2);
                    px.g = uchar(px.g + ((op >> 2) & 3) - 2);
                    px.b = uchar(px.b + (op & 3) - 2);
                } else if ((op & 0xc0) == kOpLuma) {
                    const int next = b.u8(pos++);
                    const int vg = (op & 0x3f) - 32;
                    px.r = uchar(px.r + vg - 8 + ((next >> 4) & 0x0f));
                    px.g = uchar(px.g + vg);
                    px.b = uchar(px.b + vg - 8 + (next & 0x0f));
                } else {
                    run = op & 0x3f;
                }
                index[size_t(qoiHash(px))] = px;
            } else {
                return fail(error, damaged);
            }
            line[x] = qRgba(px.r, px.g, px.b, channels == 4 ? px.a : 255);
        }
    }
    return out;
}

} // namespace Qoi
