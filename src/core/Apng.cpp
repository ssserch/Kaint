#include "core/Apng.h"

#include <QImage>
#include <QObject>
#include <QtEndian>

#include <array>
#include <cstdlib>

namespace Apng {

namespace {

quint32 crc32(const QByteArray& type, const QByteArray& data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 c = 0xffffffffu;
    for (const QByteArray* part : {&type, &data})
        for (char ch : *part)
            c = table[(c ^ quint8(ch)) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

void u32(QByteArray& out, quint32 v)
{
    char b[4];
    qToBigEndian(v, b);
    out.append(b, 4);
}

void u16(QByteArray& out, quint16 v)
{
    char b[2];
    qToBigEndian(v, b);
    out.append(b, 2);
}

void chunk(QByteArray& out, const char* type, const QByteArray& data)
{
    const QByteArray t(type, 4);
    u32(out, quint32(data.size()));
    out.append(t);
    out.append(data);
    u32(out, crc32(t, data));
}

int paeth(int a, int b, int c)
{
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Las filas con su filtro (el que deja los números más pequeños, como hacen los compresores de
// PNG) y comprimidas con zlib.
QByteArray imageData(const QImage& rgba)
{
    const int w = rgba.width(), h = rgba.height(), stride = w * 4;
    QByteArray raw;
    raw.reserve((stride + 1) * h);
    QByteArray candidate(stride, 0), best;
    for (int y = 0; y < h; ++y) {
        const uchar* row = rgba.constScanLine(y);
        const uchar* up = y > 0 ? rgba.constScanLine(y - 1) : nullptr;
        long bestScore = -1;
        int bestFilter = 0;
        for (int f = 0; f < 5; ++f) {
            long score = 0;
            for (int i = 0; i < stride; ++i) {
                const int a = i >= 4 ? row[i - 4] : 0, b = up ? up[i] : 0, c = up && i >= 4 ? up[i - 4] : 0;
                const int predictor = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
                const auto v = char(row[i] - predictor);
                candidate[i] = v;
                score += std::abs(int(qint8(v)));
            }
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestFilter = f;
                best = candidate;
            }
        }
        raw.append(char(bestFilter));
        raw.append(best);
    }
    // qCompress pone delante el tamaño (4 bytes); lo de después es el flujo de zlib.
    return qCompress(raw, 9).mid(4);
}

} // namespace

QByteArray encode(const QList<Gif::Frame>& frames, int loops, QString* error)
{
    QList<const Gif::Frame*> shown;
    for (const Gif::Frame& f : frames)
        if (!f.hidden && !f.image.isNull())
            shown << &f;
    if (shown.isEmpty()) {
        if (error)
            *error = QObject::tr("No hay fotogramas que guardar.");
        return {};
    }
    const QSize size = shown.first()->image.size();
    QByteArray out("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    u32(ihdr, quint32(size.width()));
    u32(ihdr, quint32(size.height()));
    ihdr.append(char(8)).append(char(6)).append(char(0)).append(char(0)).append(char(0));
    chunk(out, "IHDR", ihdr);
    QByteArray actl;
    u32(actl, quint32(shown.size()));
    u32(actl, quint32(std::max(0, loops)));
    chunk(out, "acTL", actl);
    quint32 sequence = 0;
    for (int i = 0; i < shown.size(); ++i) {
        QImage image = shown[i]->image.convertToFormat(QImage::Format_RGBA8888);
        if (image.size() != size)
            image = image.copy(QRect(QPoint(0, 0), size));
        QByteArray fctl;
        u32(fctl, sequence++);
        u32(fctl, quint32(size.width()));
        u32(fctl, quint32(size.height()));
        u32(fctl, 0);
        u32(fctl, 0);
        // La duración en milésimas (numerador / 1000); cada fotograma sustituye al anterior.
        u16(fctl, quint16(std::clamp(shown[i]->delayMs, 1, 65535)));
        u16(fctl, 1000);
        fctl.append(char(0)).append(char(0));
        chunk(out, "fcTL", fctl);
        const QByteArray data = imageData(image);
        if (i == 0) {
            chunk(out, "IDAT", data);
        } else {
            QByteArray fdat;
            u32(fdat, sequence++);
            fdat.append(data);
            chunk(out, "fdAT", fdat);
        }
    }
    chunk(out, "IEND", {});
    return out;
}

int frameCount(const QByteArray& data)
{
    if (!data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)))
        return 0;
    for (qsizetype at = 8; at + 12 <= data.size();) {
        const quint32 length = qFromBigEndian<quint32>(data.constData() + at);
        const QByteArray type = data.mid(at + 4, 4);
        if (type == "acTL" && length >= 8)
            return int(qFromBigEndian<quint32>(data.constData() + at + 8));
        if (type == "IDAT")
            break;
        at += 12 + qsizetype(length);
    }
    return 1;
}

} // namespace Apng
