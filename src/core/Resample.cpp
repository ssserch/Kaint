#include "core/Resample.h"

#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QObject>

#include <array>
#include <cmath>
#include <vector>

namespace Resample {

QString name(Method method)
{
    switch (method) {
    case Method::Bicubic: return QObject::tr("Bicúbico");
    case Method::BicubicSmooth: return QObject::tr("Bicúbico (suave)");
    case Method::Bilinear: return QObject::tr("Bilineal");
    case Method::BilinearLow: return QObject::tr("Bilineal (baja calidad)");
    case Method::Adaptive: return QObject::tr("Adaptativo (nítido)");
    case Method::Lanczos: return QObject::tr("Lanczos");
    case Method::Supersampling: return QObject::tr("Supermuestreo (Fant)");
    case Method::NearestNeighbor: return QObject::tr("Vecino más cercano");
    }
    return {};
}

const QList<Method>& all()
{
    static const QList<Method> list{Method::Bicubic, Method::BicubicSmooth, Method::Bilinear, Method::BilinearLow,
                                    Method::Adaptive, Method::Lanczos, Method::Supersampling, Method::NearestNeighbor};
    return list;
}

QString name(Interpolation interpolation)
{
    switch (interpolation) {
    case Interpolation::NearestNeighbor: return QObject::tr("Vecino más próximo");
    case Interpolation::Bilinear: return QObject::tr("Bilineal");
    case Interpolation::Bicubic: return QObject::tr("Bicúbico");
    case Interpolation::BilinearMultisample: return QObject::tr("Multimuestra bilineal");
    case Interpolation::Anisotropic: return QObject::tr("Anisotrópico");
    }
    return {};
}

const QList<Interpolation>& interpolations()
{
    static const QList<Interpolation> list{Interpolation::NearestNeighbor, Interpolation::Bilinear,
                                           Interpolation::BilinearMultisample, Interpolation::Anisotropic,
                                           Interpolation::Bicubic};
    return list;
}

namespace {

// --- Núcleos ---------------------------------------------------------------

double mitchell(double x, double b, double c)
{
    x = std::abs(x);
    if (x < 1)
        return ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + (6 - 2 * b)) / 6;
    if (x < 2)
        return ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x + (8 * b + 24 * c)) / 6;
    return 0;
}

double sinc(double x)
{
    if (x == 0)
        return 1;
    x *= M_PI;
    return std::sin(x) / x;
}

using Kernel = double (*)(double);
double kTriangle(double x) { x = std::abs(x); return x < 1 ? 1 - x : 0; }
double kCatmullRom(double x) { return mitchell(x, 0, 0.5); }
double kBSpline(double x) { return mitchell(x, 1, 0); }
double kLanczos3(double x) { x = std::abs(x); return x < 3 ? sinc(x) * sinc(x / 3) : 0; }

struct Contribution {
    int first = 0;
    std::vector<float> weights;
};

std::vector<Contribution> kernelWeights(int srcN, int dstN, Kernel kernel, double support, bool prefilter)
{
    std::vector<Contribution> out(static_cast<size_t>(dstN));
    const double f = double(srcN) / dstN;
    const double scale = prefilter ? std::max(1.0, f) : 1.0;
    const double s = support * scale;
    for (int i = 0; i < dstN; ++i) {
        const double center = (i + 0.5) * f - 0.5;
        const int lo = int(std::ceil(center - s));
        const int hi = int(std::floor(center + s));
        const int first = std::clamp(lo, 0, srcN - 1);
        const int last = std::clamp(hi, 0, srcN - 1);
        std::vector<float> w(size_t(last - first + 1), 0.f);
        double sum = 0;
        for (int j = lo; j <= hi; ++j) {
            const double v = kernel((j - center) / scale);
            w[size_t(std::clamp(j, first, last) - first)] += float(v);
            sum += v;
        }
        if (std::abs(sum) > 1e-9)
            for (float& x : w)
                x = float(x / sum);
        out[size_t(i)] = {first, std::move(w)};
    }
    return out;
}

// Media exacta del área cubierta (Fant); al ampliar equivale a bilineal.
std::vector<Contribution> areaWeights(int srcN, int dstN)
{
    if (dstN >= srcN)
        return kernelWeights(srcN, dstN, kTriangle, 1, false);
    std::vector<Contribution> out(static_cast<size_t>(dstN));
    const double f = double(srcN) / dstN;
    for (int i = 0; i < dstN; ++i) {
        const double a = i * f, b = (i + 1) * f;
        const int first = int(std::floor(a));
        const int last = std::min(srcN - 1, int(std::ceil(b)) - 1);
        std::vector<float> w;
        for (int j = first; j <= last; ++j)
            w.push_back(float((std::min(b, j + 1.0) - std::max(a, double(j))) / f));
        out[size_t(i)] = {first, std::move(w)};
    }
    return out;
}

// --- Conversión de gamma -----------------------------------------------------

constexpr int kOne = 16320;

const std::array<int, 256>& toLinearLut()
{
    static const std::array<int, 256> lut = [] {
        std::array<int, 256> l{};
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            const double lin = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            l[size_t(i)] = int(std::round(lin * kOne));
        }
        return l;
    }();
    return lut;
}

const std::vector<uint8_t>& toSrgbLut()
{
    static const std::vector<uint8_t> lut = [] {
        std::vector<uint8_t> l(kOne + 1);
        for (int i = 0; i <= kOne; ++i) {
            const double lin = double(i) / kOne;
            const double c = lin <= 0.0031308 ? lin * 12.92 : 1.055 * std::pow(lin, 1 / 2.4) - 0.055;
            l[size_t(i)] = uint8_t(clamp255f(float(c * 255.0)));
        }
        return l;
    }();
    return lut;
}

QImage separable(const QImage& src, QSize size, const std::vector<Contribution>& wx,
                 const std::vector<Contribution>& wy, bool gamma)
{
    const int sw = src.width(), sh = src.height();
    const int dw = size.width(), dh = size.height();
    std::vector<int16_t> mid(size_t(dw) * sh * 4);
    const ConstPixels in(src);
    const auto& lin = toLinearLut();

    // Pasada horizontal: fuente -> búfer intermedio de 16 bits.
    parallelRows(0, sh, [&](int y0, int y1) {
        std::vector<int> row(size_t(sw) * 4);
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = in.row(y);
            for (int x = 0; x < sw; ++x) {
                int* c = &row[size_t(x) * 4];
                const QRgb p = s[x];
                const int a = qAlpha(p);
                c[3] = a * 64;
                if (!gamma) {
                    c[0] = qRed(p) * 64, c[1] = qGreen(p) * 64, c[2] = qBlue(p) * 64;
                } else if (a == 0) {
                    c[0] = c[1] = c[2] = 0;
                } else {
                    const QRgb u = qUnpremultiply(p);
                    c[0] = lin[qRed(u)] * a / 255, c[1] = lin[qGreen(u)] * a / 255, c[2] = lin[qBlue(u)] * a / 255;
                }
            }
            int16_t* out = &mid[size_t(y) * dw * 4];
            for (int x = 0; x < dw; ++x) {
                const Contribution& k = wx[size_t(x)];
                float acc[4] = {0, 0, 0, 0};
                for (size_t j = 0; j < k.weights.size(); ++j) {
                    const int* c = &row[size_t(k.first + int(j)) * 4];
                    const float w = k.weights[j];
                    acc[0] += c[0] * w, acc[1] += c[1] * w, acc[2] += c[2] * w, acc[3] += c[3] * w;
                }
                for (int c = 0; c < 4; ++c)
                    out[x * 4 + c] = int16_t(std::clamp(acc[c], -32000.f, 32000.f));
            }
        }
    });

    // Pasada vertical: búfer intermedio -> resultado de 8 bits (premultiplicado).
    QImage dst(size, kPremulFormat);
    const Pixels outPx(dst);
    const auto& srgb = toSrgbLut();
    parallelRows(0, dh, [&](int y0, int y1) {
        std::vector<float> acc(size_t(dw) * 4);
        for (int y = y0; y < y1; ++y) {
            std::fill(acc.begin(), acc.end(), 0.f);
            const Contribution& k = wy[size_t(y)];
            for (size_t j = 0; j < k.weights.size(); ++j) {
                const int16_t* m = &mid[size_t(k.first + int(j)) * dw * 4];
                const float w = k.weights[j];
                for (int i = 0; i < dw * 4; ++i)
                    acc[size_t(i)] += m[i] * w;
            }
            QRgb* d = outPx.row(y);
            for (int x = 0; x < dw; ++x) {
                const float* c = &acc[size_t(x) * 4];
                const int a = clamp255f(c[3] / 64.f);
                if (a == 0) {
                    d[x] = 0;
                    continue;
                }
                if (!gamma) {
                    d[x] = qRgba(std::min(a, clamp255f(c[0] / 64.f)), std::min(a, clamp255f(c[1] / 64.f)),
                                 std::min(a, clamp255f(c[2] / 64.f)), a);
                } else {
                    auto ch = [&](float v) {
                        const int l = std::clamp(int(v * 255.f / a + 0.5f), 0, kOne);
                        return srgb[size_t(l)];
                    };
                    d[x] = qPremultiply(qRgba(ch(c[0]), ch(c[1]), ch(c[2]), a));
                }
            }
        }
    });
    return dst;
}

QImage nearest(const QImage& src, QSize size)
{
    QImage dst(size, src.format());
    const ConstPixels in(src);
    const Pixels out(dst);
    const double fx = double(src.width()) / size.width();
    const double fy = double(src.height()) / size.height();
    parallelRows(0, size.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = in.row(std::min(src.height() - 1, int((y + 0.5) * fy)));
            QRgb* d = out.row(y);
            for (int x = 0; x < size.width(); ++x)
                d[x] = s[std::min(src.width() - 1, int((x + 0.5) * fx))];
        }
    });
    return dst;
}

QImage bilinearLow(const QImage& src, QSize size)
{
    QImage dst(size, kPremulFormat);
    const ConstPixels in(src);
    const Pixels out(dst);
    const float fx = float(src.width()) / size.width();
    const float fy = float(src.height()) / size.height();
    parallelRows(0, size.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = out.row(y);
            for (int x = 0; x < size.width(); ++x)
                d[x] = sampleBilinear(in, (x + 0.5f) * fx, (y + 0.5f) * fy);
        }
    });
    return dst;
}

} // namespace

QImage scale(const QImage& source, QSize size, Method method, bool gammaCorrect)
{
    if (source.isNull() || size.isEmpty())
        return {};
    const bool straight = !isPremultiplied(source);
    const QImage::Format outFormat = straight ? kLayerFormat : kPremulFormat;
    if (size == source.size())
        return source.convertToFormat(outFormat).copy();
    if (method == Method::NearestNeighbor)
        return nearest(source.convertToFormat(outFormat), size);
    const QImage src = Raster::premultiplied(source);
    auto result = [&](const QImage& premul) { return straight ? Raster::unpremultiplied(premul) : premul; };

    if (method == Method::Adaptive) {
        const double f = std::max(double(src.width()) / size.width(), double(src.height()) / size.height());
        method = f >= 2.0 ? Method::Supersampling : Method::Bicubic;
    }

    switch (method) {
    case Method::BilinearLow:
        return result(bilinearLow(src, size));
    case Method::Supersampling:
        return result(separable(src, size, areaWeights(src.width(), size.width()),
                                areaWeights(src.height(), size.height()), gammaCorrect));
    default:
        break;
    }

    Kernel k = kCatmullRom;
    double support = 2;
    if (method == Method::Bilinear) {
        k = kTriangle;
        support = 1;
    } else if (method == Method::BicubicSmooth) {
        k = kBSpline;
    } else if (method == Method::Lanczos) {
        k = kLanczos3;
        support = 3;
    }
    return result(separable(src, size, kernelWeights(src.width(), size.width(), k, support, true),
                            kernelWeights(src.height(), size.height(), k, support, true), gammaCorrect));
}

// ---------------------------------------------------------------------------

namespace {

inline QRgb pixelOrTransparent(const ConstPixels& p, int x, int y)
{
    return (x < 0 || y < 0 || x >= p.w || y >= p.h) ? 0 : p.at(x, y);
}

QRgb sampleBicubic(const ConstPixels& src, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const double tx = x - ix, ty = y - iy;
    double wx[4], wy[4];
    for (int i = 0; i < 4; ++i) {
        wx[i] = kCatmullRom(tx - (i - 1));
        wy[i] = kCatmullRom(ty - (i - 1));
    }
    double acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const QRgb p = pixelOrTransparent(src, ix + i - 1, iy + j - 1);
            const double w = wx[i] * wy[j];
            acc[0] += qRed(p) * w, acc[1] += qGreen(p) * w, acc[2] += qBlue(p) * w, acc[3] += qAlpha(p) * w;
        }
    }
    const int a = clamp255f(float(acc[3]));
    return qRgba(std::min(a, clamp255f(float(acc[0]))), std::min(a, clamp255f(float(acc[1]))),
                 std::min(a, clamp255f(float(acc[2]))), a);
}

QRgb sampleBilinearTransparent(const ConstPixels& src, double x, double y)
{
    x -= 0.5;
    y -= 0.5;
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const int tx = int((x - ix) * 256), ty = int((y - iy) * 256);
    const QRgb p00 = pixelOrTransparent(src, ix, iy), p10 = pixelOrTransparent(src, ix + 1, iy);
    const QRgb p01 = pixelOrTransparent(src, ix, iy + 1), p11 = pixelOrTransparent(src, ix + 1, iy + 1);
    auto mix = [&](int c00, int c10, int c01, int c11) {
        const int top = c00 * (256 - tx) + c10 * tx;
        const int bottom = c01 * (256 - tx) + c11 * tx;
        return (top * (256 - ty) + bottom * ty + 32768) >> 16;
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)), mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)), mix(qAlpha(p00), qAlpha(p10), qAlpha(p01), qAlpha(p11)));
}

} // namespace

namespace {

// Imagen en luz lineal con alfa premultiplicado (float), para interpolar con gamma corregida.
struct LinearImage {
    int w = 0, h = 0;
    std::vector<float> px;

    explicit LinearImage(const QImage& premul)
        : w(premul.width())
        , h(premul.height())
        , px(size_t(w) * h * 4)
    {
        const auto& lin = toLinearLut();
        const ConstPixels in(premul);
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const QRgb* s = in.row(y);
                float* d = &px[size_t(y) * w * 4];
                for (int x = 0; x < w; ++x, d += 4) {
                    const QRgb u = qUnpremultiply(s[x]);
                    const float a = qAlpha(u) / 255.f;
                    d[0] = lin[size_t(qRed(u))] / float(kOne) * a;
                    d[1] = lin[size_t(qGreen(u))] / float(kOne) * a;
                    d[2] = lin[size_t(qBlue(u))] / float(kOne) * a;
                    d[3] = a;
                }
            }
        });
    }

    const float* at(int x, int y) const
    {
        static const float zero[4] = {0, 0, 0, 0};
        return (x < 0 || y < 0 || x >= w || y >= h) ? zero : &px[(size_t(y) * w + x) * 4];
    }

    // De vuelta a sRGB premultiplicado de 8 bits.
    static QRgb toPremul(const float* c)
    {
        const float a = std::clamp(c[3], 0.f, 1.f);
        if (a <= 0.5f / 255.f)
            return 0;
        const auto& srgb = toSrgbLut();
        auto ch = [&](float v) {
            const float lin = std::clamp(v / a, 0.f, 1.f);
            return int(srgb[size_t(std::lround(lin * kOne))]);
        };
        return qPremultiply(qRgba(ch(c[0]), ch(c[1]), ch(c[2]), int(std::lround(a * 255))));
    }

    // Suma a 'acc' la muestra bilineal de (x, y) en luz lineal, con peso 'w'.
    void addBilinear(double x, double y, float w, float* acc) const
    {
        x -= 0.5;
        y -= 0.5;
        const int ix = int(std::floor(x)), iy = int(std::floor(y));
        const float tx = float(x - ix), ty = float(y - iy);
        const float* p00 = at(ix, iy);
        const float* p10 = at(ix + 1, iy);
        const float* p01 = at(ix, iy + 1);
        const float* p11 = at(ix + 1, iy + 1);
        for (int k = 0; k < 4; ++k)
            acc[k] += w * ((p00[k] * (1 - tx) + p10[k] * tx) * (1 - ty) + (p01[k] * (1 - tx) + p11[k] * tx) * ty);
    }

    QRgb bilinear(double x, double y) const
    {
        float c[4] = {0, 0, 0, 0};
        addBilinear(x, y, 1.f, c);
        return toPremul(c);
    }

    QRgb bicubic(double x, double y) const
    {
        x -= 0.5;
        y -= 0.5;
        const int ix = int(std::floor(x)), iy = int(std::floor(y));
        const double tx = x - ix, ty = y - iy;
        double wx[4], wy[4];
        for (int i = 0; i < 4; ++i) {
            wx[i] = kCatmullRom(tx - (i - 1));
            wy[i] = kCatmullRom(ty - (i - 1));
        }
        float c[4] = {0, 0, 0, 0};
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                const float* p = at(ix + i - 1, iy + j - 1);
                const float wgt = float(wx[i] * wy[j]);
                for (int k = 0; k < 4; ++k)
                    c[k] += p[k] * wgt;
            }
        c[3] = std::clamp(c[3], 0.f, 1.f);
        for (int k = 0; k < 3; ++k)
            c[k] = std::clamp(c[k], 0.f, c[3]);
        return toPremul(c);
    }
};

} // namespace

QImage prefiltered(const QImage& src, QTransform* transform, bool gammaCorrect)
{
    // Escala de cada eje de la imagen en el destino; solo se prefiltra lo que encoge.
    const double sx = std::hypot(transform->m11(), transform->m12());
    const double sy = std::hypot(transform->m21(), transform->m22());
    if (src.isNull() || (sx >= 0.85 && sy >= 0.85))
        return src;
    const QSize size(std::max(1, int(std::lround(src.width() * std::min(1.0, sx)))),
                     std::max(1, int(std::lround(src.height() * std::min(1.0, sy)))));
    struct Cache {
        qint64 key = 0;
        QSize size;
        bool gamma = false;
        QImage image;
    };
    static thread_local Cache cache;
    if (cache.key != src.cacheKey() || cache.size != size || cache.gamma != gammaCorrect) {
        cache.image = scale(src, size, Method::Supersampling, gammaCorrect);
        cache.key = src.cacheKey();
        cache.size = size;
        cache.gamma = gammaCorrect;
    }
    *transform = QTransform::fromScale(double(src.width()) / size.width(), double(src.height()) / size.height()) * *transform;
    return cache.image;
}

void drawTransformed(QImage& dst, const QImage& source, const QTransform& transform, Interpolation interpolation,
                     const QRect& clip, bool gammaCorrect)
{
    bool invertible = false;
    const QTransform inv = transform.inverted(&invertible);
    if (!invertible)
        return;
    const QRect area = clip.intersected(dst.rect())
                           .intersected(transform.mapRect(QRectF(source.rect())).toAlignedRect().adjusted(-2, -2, 2, 2));
    if (area.isEmpty())
        return;
    const QImage src = source.convertToFormat(kPremulFormat);
    const bool dstPremul = isPremultiplied(dst);

    // Multimuestra bilineal y anisotrópico: varias muestras bilineales repartidas por el píxel de
    // destino (2×2; en el anisotrópico, tantas en cada eje de la pantalla como píxeles de la imagen
    // abarque el píxel en ese eje, hasta 8) y su media.
    int nx = 1, ny = 1;
    if (interpolation == Interpolation::BilinearMultisample) {
        nx = ny = 2;
    } else if (interpolation == Interpolation::Anisotropic) {
        nx = std::clamp(int(std::ceil(std::hypot(inv.m11(), inv.m12()) - 1e-6)), 1, 8);
        ny = std::clamp(int(std::ceil(std::hypot(inv.m21(), inv.m22()) - 1e-6)), 1, 8);
    }
    const bool multi = interpolation == Interpolation::BilinearMultisample || interpolation == Interpolation::Anisotropic;

    if (gammaCorrect && interpolation != Interpolation::NearestNeighbor) {
        const LinearImage lin(src);
        const Pixels out(dst);
        const float weight = 1.f / float(nx * ny);
        parallelRows(area.top(), area.bottom() + 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                QRgb* d = out.row(y);
                for (int x = area.left(); x <= area.right(); ++x) {
                    QRgb s;
                    if (multi) {
                        float acc[4] = {0, 0, 0, 0};
                        for (int j = 0; j < ny; ++j)
                            for (int i = 0; i < nx; ++i) {
                                const QPointF p = inv.map(QPointF(x + (i + 0.5) / nx, y + (j + 0.5) / ny));
                                lin.addBilinear(p.x(), p.y(), weight, acc);
                            }
                        s = LinearImage::toPremul(acc);
                    } else {
                        const QPointF p = inv.map(QPointF(x + 0.5, y + 0.5));
                        s = interpolation == Interpolation::Bilinear ? lin.bilinear(p.x(), p.y()) : lin.bicubic(p.x(), p.y());
                    }
                    if (qAlpha(s))
                        d[x] = dstPremul ? overPixel(d[x], s) : overStraight(d[x], qUnpremultiply(s));
                }
            }
        });
        return;
    }
    const ConstPixels in(src);
    const Pixels out(dst);
    parallelRows(area.top(), area.bottom() + 1, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = out.row(y);
            for (int x = area.left(); x <= area.right(); ++x) {
                const QPointF p = inv.map(QPointF(x + 0.5, y + 0.5));
                QRgb s;
                switch (interpolation) {
                case Interpolation::NearestNeighbor:
                    s = pixelOrTransparent(in, int(std::floor(p.x())), int(std::floor(p.y())));
                    break;
                case Interpolation::Bilinear:
                    s = sampleBilinearTransparent(in, p.x(), p.y());
                    break;
                case Interpolation::BilinearMultisample:
                case Interpolation::Anisotropic: {
                    int acc[4] = {0, 0, 0, 0};
                    for (int j = 0; j < ny; ++j)
                        for (int i = 0; i < nx; ++i) {
                            const QPointF q = inv.map(QPointF(x + (i + 0.5) / nx, y + (j + 0.5) / ny));
                            const QRgb c = sampleBilinearTransparent(in, q.x(), q.y());
                            acc[0] += qRed(c), acc[1] += qGreen(c), acc[2] += qBlue(c), acc[3] += qAlpha(c);
                        }
                    const int n = nx * ny;
                    s = qRgba((acc[0] + n / 2) / n, (acc[1] + n / 2) / n, (acc[2] + n / 2) / n, (acc[3] + n / 2) / n);
                    break;
                }
                case Interpolation::Bicubic:
                default:
                    s = sampleBicubic(in, p.x(), p.y());
                    break;
                }
                if (qAlpha(s))
                    d[x] = dstPremul ? overPixel(d[x], s) : overStraight(d[x], qUnpremultiply(s));
            }
        }
    });
}

} // namespace Resample
