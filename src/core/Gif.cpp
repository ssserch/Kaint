#include "core/Gif.h"

#include "core/ImageIO.h"
#include "core/Layer.h"
#include "core/Resample.h"

#include <QBuffer>
#include <QImageReader>
#include <QObject>
#include <QPainter>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace {

void put16(QByteArray& out, int v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void put32(QByteArray& out, quint32 v)
{
    put16(out, int(v & 0xffff));
    put16(out, int(v >> 16));
}

// Compresor LZW de GIF: códigos de longitud variable (hasta 12 bits), empaquetados desde el
// bit menos significativo y en bloques de 255 bytes.
class LzwEncoder {
public:
    LzwEncoder(QByteArray& out, int minCodeSize)
        : m_out(out)
        , m_minCodeSize(minCodeSize)
        , m_clear(1 << minCodeSize)
    {
    }

    void encode(const uchar* data, qsizetype count)
    {
        reset();
        writeCode(m_clear);
        if (count == 0) {
            writeCode(m_clear + 1);
            finish();
            return;
        }
        int prefix = data[0];
        for (qsizetype i = 1; i < count; ++i) {
            const int k = data[i];
            const quint32 key = (quint32(prefix) << 8) | quint32(k);
            const auto it = m_dict.find(key);
            if (it != m_dict.end()) {
                prefix = it->second;
                continue;
            }
            writeCode(prefix);
            const int assigned = m_next++;
            m_dict.emplace(key, assigned);
            if (assigned >= (1 << m_codeSize) && m_codeSize < 12)
                ++m_codeSize;
            if (assigned == 4095) {
                writeCode(m_clear);
                reset();
            }
            prefix = k;
        }
        writeCode(prefix);
        writeCode(m_clear + 1);
        finish();
    }

private:
    void reset()
    {
        m_dict.clear();
        m_next = m_clear + 2;
        m_codeSize = m_minCodeSize + 1;
    }

    void writeCode(int code)
    {
        m_bits |= quint32(code) << m_bitCount;
        m_bitCount += m_codeSize;
        while (m_bitCount >= 8) {
            pushByte(uchar(m_bits & 0xff));
            m_bits >>= 8;
            m_bitCount -= 8;
        }
    }

    void pushByte(uchar b)
    {
        m_block.append(char(b));
        if (m_block.size() == 255)
            flushBlock();
    }

    void flushBlock()
    {
        if (m_block.isEmpty())
            return;
        m_out.append(char(m_block.size()));
        m_out.append(m_block);
        m_block.clear();
    }

    void finish()
    {
        if (m_bitCount > 0)
            pushByte(uchar(m_bits & 0xff));
        m_bits = 0;
        m_bitCount = 0;
        flushBlock();
        m_out.append(char(0));
    }

    QByteArray& m_out;
    QByteArray m_block;
    int m_minCodeSize;
    int m_clear;
    int m_next = 0;
    int m_codeSize = 0;
    quint32 m_bits = 0;
    int m_bitCount = 0;
    std::unordered_map<quint32, int> m_dict;
};

} // namespace

namespace Gif {

namespace {

Animation readFrom(QImageReader& reader, QString* error)
{
    Animation anim;
    reader.setAutoTransform(false);
    while (reader.canRead()) {
        Frame f;
        f.image = reader.read();
        if (f.image.isNull())
            break;
        const int delay = reader.nextImageDelay();
        f.delayMs = delay > 0 ? delay : 100;
        f.image = f.image.convertToFormat(kLayerFormat);
        anim.frames << f;
        if (reader.imageCount() > 0 && anim.frames.size() >= reader.imageCount())
            break;
    }
    if (anim.frames.isEmpty() && error)
        *error = reader.errorString();
    const int loops = reader.loopCount();
    anim.loops = loops < 0 ? 0 : (loops == 0 ? -1 : loops);
    return anim;
}

} // namespace

Animation read(const QString& path, QString* error)
{
    QImageReader reader(path);
    return readFrom(reader, error);
}

Animation decode(const QByteArray& data, QString* error)
{
    QByteArray copy = data;
    QBuffer buffer(&copy);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "gif");
    return readFrom(reader, error);
}

QList<Frame> applyEffects(QList<Frame> frames, const Effects& effects)
{
    if (frames.size() < 2)
        return frames;
    // El primero, debajo de los demás.
    if (effects.firstAsBackground) {
        const QImage background = frames.first().image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int i = 1; i < frames.size(); ++i) {
            QImage composed = background.copy();
            QPainter p(&composed);
            p.drawImage(0, 0, frames[i].image);
            p.end();
            frames[i].image = composed.convertToFormat(kLayerFormat);
        }
        if (effects.firstOnlyAsBackground)
            frames.removeFirst();
    }
    if (!effects.crossfade || frames.size() < 2)
        return frames;
    // Fundidos intermedios (mezcla premultiplicada: también de transparente a opaco).
    const int steps = std::clamp(effects.fadeFrames, 1, 20);
    const int delay = std::max(kMinDelayMs, effects.fadeDelayMs);
    QList<Frame> out;
    const int n = int(frames.size());
    for (int i = 0; i < n; ++i) {
        out << frames[i];
        if (i == n - 1 && !effects.fadeLastToFirst)
            break;
        const QImage a = frames[i].image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage b = frames[(i + 1) % n].image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        for (int k = 1; k <= steps; ++k) {
            const int t = 256 * k / (steps + 1);
            QImage mixed(a.size(), QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < a.height(); ++y) {
                const QRgb* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const QRgb* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                QRgb* rm = reinterpret_cast<QRgb*>(mixed.scanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    auto lerp = [t](int from, int to) { return (from * (256 - t) + to * t) >> 8; };
                    rm[x] = qRgba(lerp(qRed(ra[x]), qRed(rb[x])), lerp(qGreen(ra[x]), qGreen(rb[x])),
                                  lerp(qBlue(ra[x]), qBlue(rb[x])), lerp(qAlpha(ra[x]), qAlpha(rb[x])));
                }
            }
            out << Frame{mixed.convertToFormat(kLayerFormat), delay};
        }
    }
    return out;
}

QByteArray encode(const QList<Frame>& frames, const Options& options, QString* error)
{
    if (frames.isEmpty()) {
        if (error)
            *error = QObject::tr("No hay fotogramas que guardar.");
        return {};
    }
    const QSize size = frames.first().image.size();
    if (size.width() > 65535 || size.height() > 65535) {
        if (error)
            *error = QObject::tr("El formato GIF no admite imágenes de más de 65535 píxeles.");
        return {};
    }
    // Si algún fotograma tiene transparencia, todos reservan un color transparente: así los
    // lectores borran el fotograma anterior (eliminación "restaurar al fondo") en vez de
    // dejarlo debajo, como los navegadores.
    bool anyTransparent = false;
    for (const Frame& frame : frames) {
        for (int y = 0; y < frame.image.height() && !anyTransparent; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(frame.image.constScanLine(y));
            for (int x = 0; x < frame.image.width(); ++x)
                if (qAlpha(row[x]) < options.alphaThreshold) {
                    anyTransparent = true;
                    break;
                }
        }
    }
    const bool slot = anyTransparent && frames.size() > 1;

    // Paleta global: los fotogramas uno debajo de otro se cuantizan juntos y luego se separan
    // (todos comparten la tabla de colores).
    QList<QImage> indexed;
    if (options.globalPalette && frames.size() > 1) {
        QImage strip(size.width(), size.height() * int(frames.size()), kLayerFormat);
        strip.fill(Qt::transparent);
        {
            QPainter p(&strip);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            for (int i = 0; i < frames.size(); ++i)
                p.drawImage(0, i * size.height(), frames[i].image);
        }
        const QImage all = ImageIO::toIndexed8(strip, options.dither, options.alphaThreshold, slot, options.quantization);
        for (int i = 0; i < frames.size(); ++i)
            indexed << all.copy(0, i * size.height(), size.width(), size.height());
    } else {
        for (const Frame& frame : frames)
            indexed << ImageIO::toIndexed8(frame.image, options.dither, options.alphaThreshold, slot, options.quantization);
    }
    auto bitsFor = [](qsizetype colors) {
        int bits = 1;
        while ((1 << bits) < colors)
            ++bits;
        return bits;
    };
    auto writeTable = [](QByteArray& out, const QList<QRgb>& table, int bits) {
        for (int i = 0; i < (1 << bits); ++i) {
            const QRgb c = i < table.size() ? table[i] : 0;
            out.append(char(qRed(c)));
            out.append(char(qGreen(c)));
            out.append(char(qBlue(c)));
        }
    };
    const bool global = options.globalPalette && frames.size() > 1;
    const QList<QRgb> globalTable = global ? indexed.first().colorTable() : QList<QRgb>();
    const int globalBits = bitsFor(globalTable.size());

    QByteArray out("GIF89a");
    put16(out, size.width());
    put16(out, size.height());
    out.append(char(global ? 0xf0 | (globalBits - 1) : 0x70));
    out.append(char(0));
    out.append(char(0));
    if (global)
        writeTable(out, globalTable, globalBits);
    if (frames.size() > 1 && options.loops >= 0) {
        out.append("\x21\xff\x0b", 3);
        out.append("NETSCAPE2.0");
        out.append("\x03\x01", 2);
        put16(out, std::clamp(options.loops, 0, 65535));
        out.append(char(0));
    }

    for (int f = 0; f < frames.size(); ++f) {
        const QImage& image = indexed[f];
        const QList<QRgb> table = image.colorTable();
        int transparent = -1;
        for (int i = 0; i < table.size(); ++i)
            if (qAlpha(table[i]) == 0) {
                transparent = i;
                break;
            }
        const int sizeBits = global ? globalBits : bitsFor(table.size());

        // Eliminación: 1 deja el fotograma debajo del siguiente (apilar), 2 lo borra.
        out.append("\x21\xf9\x04", 3);
        out.append(char(((options.stack ? 1 : 2) << 2) | (transparent >= 0 ? 1 : 0)));
        put16(out, (std::max(Gif::kMinDelayMs, frames[f].delayMs) + 5) / 10);
        out.append(char(transparent >= 0 ? transparent : 0));
        out.append(char(0));

        out.append(char(0x2c));
        put16(out, 0);
        put16(out, 0);
        put16(out, image.width());
        put16(out, image.height());
        if (global) {
            out.append(char(0));
        } else {
            out.append(char(0x80 | (sizeBits - 1)));
            writeTable(out, table, sizeBits);
        }
        const int minCodeSize = std::max(2, sizeBits);
        out.append(char(minCodeSize));
        QByteArray pixels;
        pixels.reserve(qsizetype(image.width()) * image.height());
        for (int y = 0; y < image.height(); ++y)
            pixels.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width());
        LzwEncoder(out, minCodeSize).encode(reinterpret_cast<const uchar*>(pixels.constData()), pixels.size());
    }
    out.append(char(0x3b));
    return out;
}

int delayFromName(const QString& name, int fallback)
{
    static const QRegularExpression re(QStringLiteral("\\((\\d+)\\s*ms\\)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name);
    return m.hasMatch() ? std::max(1, m.captured(1).toInt()) : fallback;
}

QString nameWithDelay(const QString& name, int delayMs)
{
    static const QRegularExpression re(QStringLiteral("\\s*\\(\\d+\\s*ms\\)"), QRegularExpression::CaseInsensitiveOption);
    QString base = name;
    base.remove(re);
    return QStringLiteral("%1 (%2ms)").arg(base.trimmed()).arg(delayMs);
}

} // namespace Gif

namespace Ico {

const QList<int>& standardSizes()
{
    static const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    return sizes;
}

QByteArray encode(const QImage& image, const QList<int>& sizes, QString* error)
{
    if (image.isNull() || sizes.isEmpty()) {
        if (error)
            *error = QObject::tr("No hay tamaños que guardar en el icono.");
        return {};
    }
    const int side = std::max(image.width(), image.height());
    QImage square(side, side, QImage::Format_ARGB32_Premultiplied);
    square.fill(Qt::transparent);
    {
        QPainter p(&square);
        p.drawImage((side - image.width()) / 2, (side - image.height()) / 2, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    QList<QByteArray> entries;
    QList<int> used;
    for (int s : sizes) {
        s = std::clamp(s, 1, 256);
        if (used.contains(s))
            continue;
        used << s;
        const QImage scaled = (s == side ? square : Resample::scale(square, QSize(s, s), Resample::Method::Adaptive))
                                  .convertToFormat(QImage::Format_ARGB32);
        QByteArray data;
        if (s == 256) {
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            scaled.save(&buffer, "PNG");
        } else {
            put32(data, 40);
            put32(data, quint32(s));
            put32(data, quint32(s * 2));
            put16(data, 1);
            put16(data, 32);
            put32(data, 0);
            const int maskStride = ((s + 31) / 32) * 4;
            put32(data, quint32(s * s * 4 + maskStride * s));
            put32(data, 0);
            put32(data, 0);
            put32(data, 0);
            put32(data, 0);
            for (int y = s - 1; y >= 0; --y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
                for (int x = 0; x < s; ++x)
                    put32(data, row[x]);
            }
            for (int y = s - 1; y >= 0; --y) {
                QByteArray mask(maskStride, char(0));
                const QRgb* row = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
                for (int x = 0; x < s; ++x)
                    if (qAlpha(row[x]) == 0)
                        mask[x / 8] = char(uchar(mask[x / 8]) | (0x80 >> (x % 8)));
                data.append(mask);
            }
        }
        entries << data;
    }
    QByteArray out;
    put16(out, 0);
    put16(out, 1);
    put16(out, int(entries.size()));
    quint32 offset = 6 + 16 * quint32(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const int s = used[i];
        out.append(char(s >= 256 ? 0 : s));
        out.append(char(s >= 256 ? 0 : s));
        out.append(char(0));
        out.append(char(0));
        put16(out, 1);
        put16(out, 32);
        put32(out, quint32(entries[i].size()));
        put32(out, offset);
        offset += quint32(entries[i].size());
    }
    for (const QByteArray& e : std::as_const(entries))
        out.append(e);
    return out;
}

QImage readLargest(const QString& path, QString* error)
{
    QImageReader reader(path);
    QImage best;
    const int count = std::max(1, reader.imageCount());
    for (int i = 0; i < count; ++i) {
        if (i > 0 && !reader.jumpToImage(i))
            break;
        const QImage img = reader.read();
        if (!img.isNull() && qint64(img.width()) * img.height() > qint64(best.width()) * best.height())
            best = img;
    }
    if (best.isNull() && error)
        *error = reader.errorString();
    return best;
}

} // namespace Ico
