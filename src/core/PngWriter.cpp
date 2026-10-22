#include "core/PngWriter.h"

#include <zlib.h>

#include <array>
#include <cstdlib>
#include <vector>

namespace Png {

namespace {

void putU32(QByteArray& out, quint32 v)
{
    out.append(char(v >> 24));
    out.append(char(v >> 16));
    out.append(char(v >> 8));
    out.append(char(v));
}

void chunk(QByteArray& out, const char* type, const QByteArray& data)
{
    putU32(out, quint32(data.size()));
    const qsizetype start = out.size();
    out.append(type, 4);
    out.append(data);
    const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(out.constData() + start), uInt(data.size() + 4));
    putU32(out, quint32(crc));
}

int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Filtra una fila con el filtro que deja la suma de valores (con signo) más pequeña, la
// heurística que recomienda la especificación de PNG.
void filterRow(const std::vector<uchar>& row, const std::vector<uchar>& prev, int bpp, QByteArray& out)
{
    const int n = int(row.size());
    std::array<std::vector<uchar>, 5> candidates;
    long best = -1;
    int bestType = 0;
    for (int type = 0; type < 5; ++type) {
        std::vector<uchar>& f = candidates[type];
        f.resize(n);
        long sum = 0;
        for (int i = 0; i < n; ++i) {
            const int a = i >= bpp ? row[i - bpp] : 0;
            const int b = prev.empty() ? 0 : prev[i];
            const int c = i >= bpp && !prev.empty() ? prev[i - bpp] : 0;
            int predicted = 0;
            switch (type) {
            case 1: predicted = a; break;
            case 2: predicted = b; break;
            case 3: predicted = (a + b) / 2; break;
            case 4: predicted = paeth(a, b, c); break;
            default: break;
            }
            f[i] = uchar(row[i] - predicted);
            sum += std::abs(int(qint8(f[i])));
        }
        if (best < 0 || sum < best) {
            best = sum;
            bestType = type;
        }
    }
    out.append(char(bestType));
    out.append(reinterpret_cast<const char*>(candidates[bestType].data()), n);
}

} // namespace

QByteArray encode(const QImage& source, bool interlaced)
{
    if (source.isNull())
        return {};
    // Formato de salida: indexado, RGB o RGBA.
    int colorType = 6;
    int bpp = 4;
    QImage image;
    if (source.format() == QImage::Format_Indexed8) {
        image = source;
        colorType = 3;
        bpp = 1;
    } else if (source.hasAlphaChannel()) {
        image = source.convertToFormat(QImage::Format_RGBA8888);
    } else {
        image = source.convertToFormat(QImage::Format_RGB888);
        colorType = 2;
        bpp = 3;
    }
    const int w = image.width(), h = image.height();

    QByteArray out("\x89PNG\r\n\x1a\n", 8);
    QByteArray ihdr;
    putU32(ihdr, quint32(w));
    putU32(ihdr, quint32(h));
    ihdr.append(char(8));
    ihdr.append(char(colorType));
    ihdr.append(char(0));
    ihdr.append(char(0));
    ihdr.append(char(interlaced ? 1 : 0));
    chunk(out, "IHDR", ihdr);

    if (colorType == 3) {
        QByteArray plte, trns;
        bool anyAlpha = false;
        for (QRgb c : image.colorTable()) {
            plte.append(char(qRed(c)));
            plte.append(char(qGreen(c)));
            plte.append(char(qBlue(c)));
            trns.append(char(qAlpha(c)));
            anyAlpha = anyAlpha || qAlpha(c) < 255;
        }
        chunk(out, "PLTE", plte);
        if (anyAlpha)
            chunk(out, "tRNS", trns);
    }
    if (image.dotsPerMeterX() > 0 && image.dotsPerMeterY() > 0) {
        QByteArray phys;
        putU32(phys, quint32(image.dotsPerMeterX()));
        putU32(phys, quint32(image.dotsPerMeterY()));
        phys.append(char(1));
        chunk(out, "pHYs", phys);
    }

    // Filas filtradas, en las siete pasadas de Adam7 o de arriba abajo.
    static constexpr int x0[] = {0, 4, 0, 2, 0, 1, 0}, y0[] = {0, 0, 4, 0, 2, 0, 1};
    static constexpr int dx[] = {8, 8, 4, 4, 2, 2, 1}, dy[] = {8, 8, 8, 4, 4, 2, 2};
    QByteArray raw;
    const int passes = interlaced ? 7 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        const int sx = interlaced ? x0[pass] : 0, sy = interlaced ? y0[pass] : 0;
        const int stepX = interlaced ? dx[pass] : 1, stepY = interlaced ? dy[pass] : 1;
        const int pw = (w - sx + stepX - 1) / stepX, ph = (h - sy + stepY - 1) / stepY;
        if (pw <= 0 || ph <= 0)
            continue;
        std::vector<uchar> prev, row(size_t(pw) * bpp);
        for (int y = sy; y < h; y += stepY) {
            const uchar* line = image.constScanLine(y);
            for (int i = 0, x = sx; x < w; x += stepX, ++i)
                for (int k = 0; k < bpp; ++k)
                    row[size_t(i) * bpp + k] = line[size_t(x) * bpp + k];
            filterRow(row, prev, bpp, raw);
            prev = row;
        }
    }
    uLongf size = compressBound(uLong(raw.size()));
    QByteArray compressed(qsizetype(size), Qt::Uninitialized);
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &size, reinterpret_cast<const Bytef*>(raw.constData()),
                  uLong(raw.size()), 9)
        != Z_OK)
        return {};
    compressed.resize(qsizetype(size));
    chunk(out, "IDAT", compressed);
    chunk(out, "IEND", {});
    return out;
}

} // namespace Png
