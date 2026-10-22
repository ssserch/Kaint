#pragma once

#include <QImage>
#include <QRgb>

#include <algorithm>
#include <cmath>
#include <cstdint>

// Accesos crudos a los píxeles. Se obtienen los punteros una sola vez para
// que los hilos de trabajo no llamen a QImage::scanLine() (que no es
// seguro en paralelo porque puede intentar separar los datos compartidos).

struct ConstPixels {
    const uchar* bits = nullptr;
    qsizetype bpl = 0;
    int w = 0;
    int h = 0;

    ConstPixels() = default;
    explicit ConstPixels(const QImage& img)
        : bits(img.constBits()), bpl(img.bytesPerLine()), w(img.width()), h(img.height()) {}

    const QRgb* row(int y) const { return reinterpret_cast<const QRgb*>(bits + y * bpl); }
    QRgb at(int x, int y) const { return row(y)[x]; }
    QRgb clamped(int x, int y) const
    {
        return row(std::clamp(y, 0, h - 1))[std::clamp(x, 0, w - 1)];
    }
};

struct Pixels {
    uchar* bits = nullptr;
    qsizetype bpl = 0;
    int w = 0;
    int h = 0;

    Pixels() = default;
    explicit Pixels(QImage& img)
        : bits(img.bits()), bpl(img.bytesPerLine()), w(img.width()), h(img.height()) {}

    QRgb* row(int y) const { return reinterpret_cast<QRgb*>(bits + y * bpl); }
};

// Máscaras de 8 bits (Format_Alpha8 / Grayscale8).
struct ConstMask {
    const uchar* bits = nullptr;
    qsizetype bpl = 0;
    int w = 0;
    int h = 0;

    ConstMask() = default;
    explicit ConstMask(const QImage& img)
        : bits(img.isNull() ? nullptr : img.constBits()), bpl(img.bytesPerLine()), w(img.width()), h(img.height()) {}

    bool isNull() const { return bits == nullptr; }
    const uchar* row(int y) const { return bits + y * bpl; }
};

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline bool isPremultiplied(const QImage& img) { return img.format() == QImage::Format_ARGB32_Premultiplied; }
// Píxel con alfa directo a partir de uno que puede estar premultiplicado.
inline QRgb toStraight(QRgb p, bool premultiplied) { return premultiplied ? qUnpremultiply(p) : p; }

// --- Píxeles con alfa directo (capas) ----------------------------------------

// Multiplica solo el alfa por m/255; el color se conserva.
inline QRgb scaleAlpha(QRgb p, int m)
{
    if (m >= 255)
        return p;
    const int a = m <= 0 ? 0 : (qAlpha(p) * m + 127) / 255;
    return (p & 0x00ffffffu) | (uint(a) << 24);
}

// Interpolación entre dos píxeles con alfa directo (t = 0..255), ponderada
// por el alfa como si estuvieran premultiplicados. En los extremos devuelve
// el píxel exacto (también su color si es transparente).
inline QRgb lerpStraight(QRgb a, QRgb b, int t)
{
    if (t <= 0)
        return a;
    if (t >= 255)
        return b;
    const int it = 255 - t;
    const int wa = qAlpha(a) * it, wb = qAlpha(b) * t;
    const int w = wa + wb;
    if (w == 0) {
        // Los dos transparentes: se mezcla el color tal cual.
        return qRgba((qRed(a) * it + qRed(b) * t + 127) / 255, (qGreen(a) * it + qGreen(b) * t + 127) / 255,
                     (qBlue(a) * it + qBlue(b) * t + 127) / 255, 0);
    }
    auto ch = [&](int ca, int cb) { return (ca * wa + cb * wb + w / 2) / w; };
    return qRgba(ch(qRed(a), qRed(b)), ch(qGreen(a), qGreen(b)), ch(qBlue(a), qBlue(b)), (w + 127) / 255);
}

// "Source over" con alfa directo (fórmula Normal de Paint.NET).
inline QRgb overStraight(QRgb dst, QRgb src)
{
    const int sa = qAlpha(src);
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    const int da = qAlpha(dst);
    if (da == 0)
        return src;
    const int y = (da * (255 - sa) + 127) / 255; // parte visible del fondo
    const int total = y + sa;
    auto ch = [&](int d, int s) { return (d * y + s * sa + total / 2) / total; };
    return qRgba(ch(qRed(dst), qRed(src)), ch(qGreen(dst), qGreen(src)), ch(qBlue(dst), qBlue(src)), total);
}
inline int clamp255f(float v) { return v <= 0.f ? 0 : (v >= 255.f ? 255 : int(v + 0.5f)); }

// --- Píxeles premultiplicados (composición, efectos) ---------------------------

// Multiplica todos los canales de un píxel premultiplicado por m/255.
inline QRgb scalePixel(QRgb p, int m)
{
    if (m >= 255)
        return p;
    if (m <= 0)
        return 0;
    uint t = (p & 0x00ff00ffu) * uint(m);
    t = ((t + ((t >> 8) & 0x00ff00ffu) + 0x00800080u) >> 8) & 0x00ff00ffu;
    uint x = ((p >> 8) & 0x00ff00ffu) * uint(m);
    x = (x + ((x >> 8) & 0x00ff00ffu) + 0x00800080u) & 0xff00ff00u;
    return x | t;
}

// Interpolación lineal entre dos píxeles premultiplicados (t = 0..255).
inline QRgb lerpPixel(QRgb a, QRgb b, int t)
{
    if (t <= 0)
        return a;
    if (t >= 255)
        return b;
    const int it = 255 - t;
    const int ra = (qRed(a) * it + qRed(b) * t + 127) / 255;
    const int ga = (qGreen(a) * it + qGreen(b) * t + 127) / 255;
    const int ba = (qBlue(a) * it + qBlue(b) * t + 127) / 255;
    const int aa = (qAlpha(a) * it + qAlpha(b) * t + 127) / 255;
    return qRgba(ra, ga, ba, aa);
}

// "Source over" con píxeles premultiplicados.
inline QRgb overPixel(QRgb dst, QRgb src)
{
    const int sa = qAlpha(src);
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    const QRgb d = scalePixel(dst, 255 - sa);
    return qRgba(std::min(255, qRed(src) + qRed(d)), std::min(255, qGreen(src) + qGreen(d)),
                 std::min(255, qBlue(src) + qBlue(d)), std::min(255, sa + qAlpha(d)));
}

// Muestreo bilineal con coordenadas fuera de la imagen recortadas al borde.
inline QRgb sampleBilinear(const ConstPixels& src, float x, float y)
{
    // Acotadas antes de pasar a int (coordenadas enormes o NaN de las distorsiones).
    x = std::fmin(std::fmax(x, -1.f), float(src.w) + 1.f) - 0.5f;
    y = std::fmin(std::fmax(y, -1.f), float(src.h) + 1.f) - 0.5f;
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int x0 = int(fx);
    const int y0 = int(fy);
    const int tx = int((x - fx) * 256.f);
    const int ty = int((y - fy) * 256.f);
    const QRgb p00 = src.clamped(x0, y0);
    const QRgb p10 = src.clamped(x0 + 1, y0);
    const QRgb p01 = src.clamped(x0, y0 + 1);
    const QRgb p11 = src.clamped(x0 + 1, y0 + 1);
    auto mix = [&](int c00, int c10, int c01, int c11) {
        const int top = c00 * (256 - tx) + c10 * tx;
        const int bottom = c01 * (256 - tx) + c11 * tx;
        return (top * (256 - ty) + bottom * ty + 32768) >> 16;
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)),
                 mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)),
                 mix(qAlpha(p00), qAlpha(p10), qAlpha(p01), qAlpha(p11)));
}

// Generador pseudoaleatorio sin estado: el mismo (x, y, semilla) siempre
// produce el mismo valor, así el resultado no depende del reparto en hilos.
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

inline float hashUnit(uint32_t x, uint32_t y, uint32_t seed)
{
    return float(hash3(x, y, seed) & 0xffffffu) / float(0x1000000);
}

inline int luma(int r, int g, int b) { return (r * 299 + g * 587 + b * 114 + 500) / 1000; }
