// Funciones de PhotoCraft (ajustes, color, distorsiones, estilizado y objeto). Los algoritmos son
// los de PhotoCraft (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT;
// data/licenses/PhotoCraft-LICENSE.txt), escritos para Kaint sobre su imagen de trabajo (ExtraUtil.h).
// Están desactivadas mientras no se activen en Configuración > Complementos, y en los menús llevan
// la pieza de puzle de los complementos.

#include "core/effects/ExtraUtil.h"

#include <QColor>

#include <array>
#include <cmath>

namespace fx {
namespace extra {

FBuf toFloat(const QImage& img)
{
    FBuf b;
    b.w = img.width();
    b.h = img.height();
    b.d.resize(size_t(b.w) * b.h * 4);
    const ConstPixels in(img);
    const bool pm = isPremultiplied(img);
    parallelRows(0, b.h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = in.row(y);
            float* p = b.at(0, y);
            for (int x = 0; x < b.w; ++x, p += 4) {
                const QRgb c = pm ? qUnpremultiply(s[x]) : s[x];
                p[0] = qRed(c) / 255.f;
                p[1] = qGreen(c) / 255.f;
                p[2] = qBlue(c) / 255.f;
                p[3] = qAlpha(c) / 255.f;
            }
        }
    });
    return b;
}

void store(const FBuf& b, const RenderArgs& a)
{
    const Pixels out(a.dst);
    const bool pm = isPremultiplied(a.dst);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        const float* p = b.at(a.roi.left(), y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x, p += 4) {
            const int al = to8(p[3]);
            const QRgb c = al ? qRgba(to8(p[0]), to8(p[1]), to8(p[2]), al) : 0;
            d[x] = pm ? qPremultiply(c) : c;
        }
    });
}

void clipColor(float* c)
{
    const float l = lum(c);
    const float n = std::min({c[0], c[1], c[2]});
    const float x = std::max({c[0], c[1], c[2]});
    if (n < 0.f) {
        const float d = l - n;
        for (int i = 0; i < 3; ++i)
            c[i] = l > 0.f && d > 0.f ? l + (c[i] - l) * l / d : l;
    }
    if (x > 1.f) {
        const float d = x - l;
        for (int i = 0; i < 3; ++i)
            c[i] = l < 1.f && d > 0.f ? l + (c[i] - l) * (1.f - l) / d : l;
    }
}

void setLum(float* c, float l)
{
    const float d = l - lum(c);
    for (int i = 0; i < 3; ++i)
        c[i] += d;
    clipColor(c);
}

namespace {
inline int wrapMod(int v, int n)
{
    const int r = v % n;
    return r < 0 ? r + n : r;
}

inline float edgeValue(const FBuf& b, int x, int y, int c, Edge edge, const QRect& area)
{
    if (area.isEmpty())
        return 0.f;
    if (!area.contains(x, y)) {
        switch (edge) {
        case Edge::Transparent: return 0.f;
        case Edge::Repeat:
            x = std::clamp(x, area.left(), area.right());
            y = std::clamp(y, area.top(), area.bottom());
            break;
        case Edge::Wrap:
            x = area.left() + wrapMod(x - area.left(), area.width());
            y = area.top() + wrapMod(y - area.top(), area.height());
            break;
        }
    }
    return b.at(x, y)[c];
}
} // namespace

void sample(const FBuf& b, float x, float y, Edge edge, const QRect& area, float* out)
{
    const float fx = x - 0.5f, fy = y - 0.5f;
    const float x0f = std::floor(fx), y0f = std::floor(fy);
    const float ax = fx - x0f, ay = fy - y0f;
    const int x0 = int(x0f), y0 = int(y0f);
    out[0] = out[1] = out[2] = out[3] = 0.f;
    float accA = 0.f;
    const struct {
        int dx, dy;
        float w;
    } taps[4] = {{0, 0, (1 - ax) * (1 - ay)}, {1, 0, ax * (1 - ay)}, {0, 1, (1 - ax) * ay}, {1, 1, ax * ay}};
    for (const auto& t : taps) {
        if (t.w <= 0.f)
            continue;
        const float al = edgeValue(b, x0 + t.dx, y0 + t.dy, 3, edge, area);
        for (int c = 0; c < 3; ++c)
            out[c] += edgeValue(b, x0 + t.dx, y0 + t.dy, c, edge, area) * al * t.w;
        accA += al * t.w;
    }
    if (accA > 0.f)
        for (int c = 0; c < 3; ++c)
            out[c] /= accA;
    out[3] = accA;
}

FBuf remap(const FBuf& b, const QRect& area, Edge edge, const std::atomic_bool& cancel,
           const std::function<QPointF(float, float)>& f)
{
    FBuf res = b;
    parallelRows(area.top(), area.bottom() + 1, [&](int y0, int y1) {
        for (int y = y0; y < y1 && !cancel; ++y)
            for (int x = area.left(); x <= area.right(); ++x) {
                const QPointF s = f(x + 0.5f, y + 0.5f);
                sample(b, float(s.x()), float(s.y()), edge, area, res.at(x, y));
            }
    });
    return res;
}

namespace {
std::array<int, 3> gaussBoxRadii(float sigma)
{
    const float n = 3.f;
    const float ideal = std::sqrt(12.f * sigma * sigma / n + 1.f);
    int wl = int(std::floor(ideal));
    if (wl % 2 == 0)
        --wl;
    wl = std::max(1, wl);
    const int wu = wl + 2;
    const int m = int(std::clamp(std::round((12.f * sigma * sigma - n * wl * wl - 4.f * n * wl - 3.f * n) / (-4.f * wl - 4.f)), 0.f, n));
    std::array<int, 3> r;
    for (int i = 0; i < 3; ++i)
        r[i] = i < m ? (wl - 1) / 2 : (wu - 1) / 2;
    if (r[0] == 0 && r[1] == 0 && r[2] == 0)
        r[0] = 1;
    return r;
}

// Caja de radio r a lo largo de una línea de 'n' valores (de 4 canales), bordes repetidos.
void boxLine(const float* in, float* out, int n, int stride, int r)
{
    if (r <= 0 || n <= 0) {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 4; ++c)
                out[i * stride + c] = in[i * stride + c];
        return;
    }
    const float inv = 1.f / float(2 * r + 1);
    float sum[4] = {0, 0, 0, 0};
    auto px = [&](int i) { return in + std::clamp(i, 0, n - 1) * stride; };
    for (int i = -r; i <= r; ++i)
        for (int c = 0; c < 4; ++c)
            sum[c] += px(i)[c];
    for (int i = 0; i < n; ++i) {
        for (int c = 0; c < 4; ++c)
            out[i * stride + c] = sum[c] * inv;
        const float* add = px(i + r + 1);
        const float* sub = px(i - r);
        for (int c = 0; c < 4; ++c)
            sum[c] += add[c] - sub[c];
    }
}
} // namespace

void gaussian(FBuf& b, const QRect& area, float sigma, const std::atomic_bool& cancel)
{
    if (sigma <= 0.f || area.isEmpty())
        return;
    const int w = area.width(), h = area.height();
    // Premultiplicado (para que los transparentes no tiñan los bordes).
    std::vector<float> work(size_t(w) * h * 4), tmp(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* p = b.at(area.left() + x, area.top() + y);
            float* q = &work[(size_t(y) * w + x) * 4];
            q[0] = p[0] * p[3];
            q[1] = p[1] * p[3];
            q[2] = p[2] * p[3];
            q[3] = p[3];
        }
    for (int r : gaussBoxRadii(sigma)) {
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1 && !cancel; ++y)
                boxLine(&work[size_t(y) * w * 4], &tmp[size_t(y) * w * 4], w, 4, r);
        });
        parallelRows(0, w, [&](int x0, int x1) {
            std::vector<float> col(size_t(h) * 4), res(size_t(h) * 4);
            for (int x = x0; x < x1 && !cancel; ++x) {
                for (int y = 0; y < h; ++y)
                    std::copy_n(&tmp[(size_t(y) * w + x) * 4], 4, &col[size_t(y) * 4]);
                boxLine(col.data(), res.data(), h, 4, r);
                for (int y = 0; y < h; ++y)
                    std::copy_n(&res[size_t(y) * 4], 4, &work[(size_t(y) * w + x) * 4]);
            }
        });
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* q = &work[(size_t(y) * w + x) * 4];
            float* p = b.at(area.left() + x, area.top() + y);
            const float a = q[3];
            p[3] = a;
            for (int c = 0; c < 3; ++c)
                p[c] = a > 1e-6f ? q[c] / a : 0.f;
        }
}

} // namespace extra

namespace {

using namespace extra;
using P = EffectParam;

const QString kPhotoCraft = QStringLiteral("PhotoCraft");

// ---- Intensidad (Vibrance) ------------------------------------------------------------

constexpr float kVibranceGrey[2] = {0.288f, 0.712f};
constexpr float kVibranceBoost[7] = {0.5001f, 0.0887f, 0.4374f, 0.7573f, -0.8113f, 0.5154f, 2.8026f};

float vibranceSkin(float h)
{
    if (h <= 30.f)
        return 1.f;
    if (h < 45.f)
        return (45.f - h) / 15.f;
    if (h < 300.f)
        return 0.f;
    return (h - 300.f) / 60.f;
}

float hsvHue(const float* c, float mx, float mn)
{
    const float d = mx - mn;
    float h;
    if (mx == c[0])
        h = std::fmod(std::fmod((c[1] - c[2]) / d, 6.f) + 6.f, 6.f);
    else if (mx == c[1])
        h = (c[2] - c[0]) / d + 2.f;
    else
        h = (c[0] - c[1]) / d + 4.f;
    return h * 60.f;
}

void vibranceLinear(float* c, float v)
{
    const float mx = std::max({c[0], c[1], c[2]});
    const float mn = std::min({c[0], c[1], c[2]});
    if (mx <= 0.f || mx - mn <= 1e-9f)
        return;
    const float t = 1.f - mn / mx;
    const float shape = t * (1.f - t) * (2.f - t + t * t) * mx * std::max(0.f, 1.f - mx);
    float t2, v2;
    if (v < 0.f) {
        const float a = -v;
        t2 = t * (1.f - a / 4.f) * (1.f - a + a * t * (1.f + t) / 2.f);
        v2 = mx - a * shape;
    } else {
        const float* k = kVibranceBoost;
        const float w = vibranceSkin(hsvHue(c, mx, mn));
        const float a = v * (1.f - w) + 0.64f * std::pow(v, 1.6f) * w;
        const float boost = (k[0] * a + k[1] * a * a) * std::pow(t, k[2]) * std::pow(1.f - t, k[3])
                            * std::exp((k[4] + k[5] * a + k[6] * (1.f - t)) * std::max(0.f, 1.f - mx));
        t2 = std::min(1.f, t * (1.f + boost));
        v2 = mx + v / 4.f * shape;
    }
    const float m2 = v2 * (1.f - t2);
    const float kk = (v2 - m2) / (mx - mn);
    for (int i = 0; i < 3; ++i)
        c[i] = m2 + (c[i] - mn) * kk;
}

void vibrance(float* c, const ParamValues& v)
{
    const float vib = float(dval(v, "vibrance")) / 100.f, sat = float(dval(v, "saturation")) / 100.f;
    float l[3];
    for (int i = 0; i < 3; ++i)
        l[i] = srgbToLinear(std::max(0.f, c[i]));
    if (vib != 0.f)
        vibranceLinear(l, vib);
    if (sat != 0.f) {
        const float g = kVibranceGrey[0] * l[0] + kVibranceGrey[1] * l[1];
        for (float& x : l)
            x = std::clamp(g + (1.f + sat) * (x - g), 0.f, 1.f);
    }
    for (int i = 0; i < 3; ++i)
        c[i] = linearToSrgb(std::clamp(l[i], 0.f, 1.f));
}

// ---- Mapa de degradado ------------------------------------------------------------------

float bayer4(int x, int y)
{
    static const float m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (m[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.f - 0.5f;
}

void gradientMap(FBuf& b, const RenderArgs& a)
{
    const QColor from = cval(a.values, "from"), to = cval(a.values, "to");
    const float f[3] = {float(from.redF()), float(from.greenF()), float(from.blueF())};
    const float t3[3] = {float(to.redF()), float(to.greenF()), float(to.blueF())};
    const bool reverse = bval(a.values, "reverse"), dither = bval(a.values, "dither");
    parallelRows(a.roi.top(), a.roi.bottom() + 1, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
                float* p = b.at(x, y);
                if (p[3] <= 0.f)
                    continue;
                float t = std::clamp(lum(p), 0.f, 1.f);
                if (reverse)
                    t = 1.f - t;
                for (int i = 0; i < 3; ++i) {
                    float o = f[i] + (t3[i] - f[i]) * t;
                    if (dither)
                        o = std::clamp(o + bayer4(x, y) / 255.f, 0.f, 1.f);
                    p[i] = o;
                }
            }
    });
}

// ---- Filtro de fotografía --------------------------------------------------------------

struct PhotoFilterPreset {
    const char* id;
    const char* label;
    int r, g, b;
};
const PhotoFilterPreset kPhotoFilters[] = {
    {"warming85", QT_TRANSLATE_NOOP("QObject", "Filtro cálido (85)"), 236, 138, 0},
    {"warmingLBA", QT_TRANSLATE_NOOP("QObject", "Filtro cálido (LBA)"), 250, 150, 0},
    {"warming81", QT_TRANSLATE_NOOP("QObject", "Filtro cálido (81)"), 235, 177, 19},
    {"cooling80", QT_TRANSLATE_NOOP("QObject", "Filtro frío (80)"), 0, 109, 255},
    {"coolingLBB", QT_TRANSLATE_NOOP("QObject", "Filtro frío (LBB)"), 0, 93, 255},
    {"cooling82", QT_TRANSLATE_NOOP("QObject", "Filtro frío (82)"), 0, 181, 255},
    {"red", QT_TRANSLATE_NOOP("QObject", "Rojo"), 234, 26, 26},
    {"orange", QT_TRANSLATE_NOOP("QObject", "Naranja"), 243, 132, 23},
    {"yellow", QT_TRANSLATE_NOOP("QObject", "Amarillo"), 249, 227, 28},
    {"green", QT_TRANSLATE_NOOP("QObject", "Verde"), 25, 201, 25},
    {"cyan", QT_TRANSLATE_NOOP("QObject", "Cian"), 29, 203, 234},
    {"blue", QT_TRANSLATE_NOOP("QObject", "Azul"), 29, 53, 234},
    {"violet", QT_TRANSLATE_NOOP("QObject", "Violeta"), 155, 29, 234},
    {"magenta", QT_TRANSLATE_NOOP("QObject", "Magenta"), 227, 24, 227},
    {"sepia", QT_TRANSLATE_NOOP("QObject", "Sepia"), 172, 122, 51},
    {"deepRed", QT_TRANSLATE_NOOP("QObject", "Rojo intenso"), 255, 0, 0},
    {"deepBlue", QT_TRANSLATE_NOOP("QObject", "Azul intenso"), 0, 34, 205},
    {"deepEmerald", QT_TRANSLATE_NOOP("QObject", "Esmeralda intenso"), 0, 140, 0},
    {"deepYellow", QT_TRANSLATE_NOOP("QObject", "Amarillo intenso"), 255, 213, 0},
    {"underwater", QT_TRANSLATE_NOOP("QObject", "Bajo el agua"), 0, 194, 177},
};
constexpr int kPhotoFilterCount = int(sizeof(kPhotoFilters) / sizeof(kPhotoFilters[0]));

constexpr float kD50[3] = {0.96422f, 1.f, 0.82521f};
constexpr float kSrgbToXyz[3][3] = {{0.436074f, 0.385064f, 0.143080f}, {0.222504f, 0.716878f, 0.060618f}, {0.013932f, 0.097104f, 0.714173f}};
constexpr float kXyzToSrgb[3][3] = {{3.133856f, -1.616867f, -0.490615f}, {-0.978768f, 1.916142f, 0.033454f}, {0.071945f, -0.228991f, 1.405243f}};

struct Mat3 {
    float m[3][3];
};

Mat3 photoFilterMatrix(const float* color, float density)
{
    const float d = std::isfinite(density) ? std::clamp(density, 0.f, 1.f) : 0.f;
    float lin[3], xyz[3], s[3];
    for (int i = 0; i < 3; ++i)
        lin[i] = srgbToLinear(std::clamp(color[i], 0.f, 1.f));
    for (int r = 0; r < 3; ++r)
        xyz[r] = kSrgbToXyz[r][0] * lin[0] + kSrgbToXyz[r][1] * lin[1] + kSrgbToXyz[r][2] * lin[2];
    for (int i = 0; i < 3; ++i)
        s[i] = 1.f - d + d * xyz[i] / kD50[i];
    Mat3 out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            float sum = 0.f;
            for (int k = 0; k < 3; ++k)
                sum += kXyzToSrgb[r][k] * s[k] * kSrgbToXyz[k][c];
            out.m[r][c] = sum;
        }
    return out;
}

void photoFilter(float* c, const ParamValues& v)
{
    const int preset = ival(v, "filter");
    float color[3];
    if (preset >= 0 && preset < kPhotoFilterCount) {
        color[0] = kPhotoFilters[preset].r / 255.f;
        color[1] = kPhotoFilters[preset].g / 255.f;
        color[2] = kPhotoFilters[preset].b / 255.f;
    } else {
        const QColor q = cval(v, "color");
        color[0] = float(q.redF());
        color[1] = float(q.greenF());
        color[2] = float(q.blueF());
    }
    const Mat3 m = photoFilterMatrix(color, float(dval(v, "density")) / 100.f);
    float lin[3], f[3];
    for (int i = 0; i < 3; ++i)
        lin[i] = srgbToLinear(std::max(0.f, c[i]));
    for (int r = 0; r < 3; ++r)
        f[r] = linearToSrgb(std::max(0.f, m.m[r][0] * lin[0] + m.m[r][1] * lin[1] + m.m[r][2] * lin[2]));
    if (bval(v, "preserveLuminosity"))
        setLum(f, lum(c));
    for (int i = 0; i < 3; ++i)
        c[i] = std::clamp(f[i], 0.f, 1.f);
}

// ---- Corrección selectiva --------------------------------------------------------------

void selectiveColor(float* c, const ParamValues& v)
{
    const float mx = std::max({c[0], c[1], c[2]});
    const float mn = std::min({c[0], c[1], c[2]});
    const float mid = c[0] + c[1] + c[2] - mx - mn;
    auto top = [&](int i) { return c[i] >= mx ? mx - mid : 0.f; };
    auto bottom = [&](int i) { return c[i] <= mn ? mid - mn : 0.f; };
    const float weights[9] = {top(0), bottom(2), top(1), bottom(0), top(2), bottom(1), std::max(0.f, (mn - 0.5f) * 2.f),
                              std::clamp(1.f - std::abs(mx - 0.5f) - std::abs(mn - 0.5f), 0.f, 1.f), std::max(0.f, (0.5f - mx) * 2.f)};
    const int family = ival(v, "colors");
    const float w = family >= 0 && family < 9 ? weights[family] : 0.f;
    if (w <= 0.f)
        return;
    const bool relative = ival(v, "method") == 0;
    const float adj[4] = {float(dval(v, "cyan")) / 100.f, float(dval(v, "magenta")) / 100.f, float(dval(v, "yellow")) / 100.f,
                          float(dval(v, "black")) / 100.f};
    for (int i = 0; i < 3; ++i) {
        const float ink = 1.f - c[i];
        float d = adj[i] + adj[3];
        if (relative)
            d *= ink;
        else if (d > 0.f)
            d *= std::min(1.f, 2.f * (1.f - ink));
        else
            d *= std::min(1.f, 2.f * ink);
        c[i] = std::clamp(c[i] - d * w, 0.f, 1.f);
    }
}

// ---- Automáticos ----------------------------------------------------------------------------

using Hist = std::array<std::array<uint64_t, 256>, 3>;

inline int bin(float v) { return int(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }

Hist histograms(const FBuf& b, const RenderArgs& a)
{
    Hist h{};
    const ConstMask mask = a.selection && !a.selection->isEmpty() ? ConstMask(a.selection->mask()) : ConstMask();
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            if (!mask.isNull() && y < mask.h && x < mask.w && !mask.row(y)[x])
                continue;
            const float* p = b.at(x, y);
            if (p[3] <= 0.f)
                continue;
            for (int c = 0; c < 3; ++c)
                ++h[c][bin(p[c])];
        }
    return h;
}

std::pair<float, float> clipPoints(const std::array<uint64_t, 256>& h, double clip)
{
    uint64_t total = 0;
    for (uint64_t v : h)
        total += v;
    if (!total)
        return {0.f, 1.f};
    const uint64_t lim = uint64_t(double(total) * clip);
    uint64_t acc = 0;
    int lo = 0;
    while (lo < 254 && acc + h[lo] <= lim)
        acc += h[lo++];
    acc = 0;
    int hi = 255;
    while (hi > lo + 1 && acc + h[hi] <= lim)
        acc += h[hi--];
    return {lo / 255.f, hi / 255.f};
}

float medianOf(const std::array<uint64_t, 256>& h)
{
    uint64_t total = 0, acc = 0;
    for (uint64_t v : h)
        total += v;
    for (int i = 0; i < 256; ++i) {
        acc += h[i];
        if (acc * 2 >= total)
            return i / 255.f;
    }
    return 0.5f;
}

struct LevelsF {
    float black = 0.f, white = 1.f, gamma = 1.f;
};

void applyLevels(FBuf& b, const QRect& r, const std::array<LevelsF, 3>& lv)
{
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x) {
            float* p = b.at(x, y);
            if (p[3] <= 0.f)
                continue;
            for (int c = 0; c < 3; ++c) {
                const float t = std::clamp((p[c] - lv[c].black) / std::max(1e-6f, lv[c].white - lv[c].black), 0.f, 1.f);
                p[c] = std::pow(t, 1.f / lv[c].gamma);
            }
        }
}

constexpr double kClip = 0.001;

void autoContrast(FBuf& b, const RenderArgs& a)
{
    const Hist h = histograms(b, a);
    std::array<uint64_t, 256> all{};
    for (const auto& c : h)
        for (int i = 0; i < 256; ++i)
            all[i] += c[i];
    const auto [black, white] = clipPoints(all, kClip);
    applyLevels(b, a.roi, {LevelsF{black, white, 1.f}, LevelsF{black, white, 1.f}, LevelsF{black, white, 1.f}});
}

void autoColor(FBuf& b, const RenderArgs& a)
{
    const Hist h = histograms(b, a);
    std::array<std::pair<float, float>, 3> pts;
    std::array<float, 3> ts;
    for (int c = 0; c < 3; ++c) {
        pts[c] = clipPoints(h[c], kClip);
        ts[c] = std::clamp((medianOf(h[c]) - pts[c].first) / std::max(1e-3f, pts[c].second - pts[c].first), 0.02f, 0.98f);
    }
    const float want = (ts[0] + ts[1] + ts[2]) / 3.f;
    std::array<LevelsF, 3> lv;
    for (int c = 0; c < 3; ++c)
        lv[c] = LevelsF{pts[c].first, pts[c].second, std::clamp(std::log(ts[c]) / std::log(want), 0.1f, 9.99f)};
    applyLevels(b, a.roi, lv);
}

void equalize(FBuf& b, const RenderArgs& a)
{
    const Hist h = histograms(b, a);
    std::array<double, 256> all{};
    for (const auto& c : h)
        for (int i = 0; i < 256; ++i)
            all[i] += double(c[i]);
    double total = 0, first = 0;
    for (double v : all)
        total += v;
    if (total == 0.0)
        return;
    for (double v : all)
        if (v > 0) {
            first = v;
            break;
        }
    std::array<float, 256> lut;
    double acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += all[i];
        lut[i] = total > first ? float(std::clamp((acc - first) / (total - first), 0.0, 1.0)) : i / 255.f;
    }
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            if (p[3] > 0.f)
                for (int c = 0; c < 3; ++c)
                    p[c] = lut[bin(p[c])];
        }
}

// ---- Mezclador de blanco y negro ----------------------------------------------------------

const char* const kBwKeys[6] = {"reds", "yellows", "greens", "cyans", "blues", "magentas"};

void blackWhite(float* c, const ParamValues& v)
{
    float weights[6];
    for (int i = 0; i < 6; ++i)
        weights[i] = float(dval(v, kBwKeys[i]));
    const float r = c[0], g = c[1], bl = c[2];
    const float mx = std::max({r, g, bl}), mn = std::min({r, g, bl});
    const float mid = r + g + bl - mx - mn;
    const int primary = r >= g && r >= bl ? 0 : g >= bl ? 2 : 4;
    const int secondary = bl <= r && bl <= g ? 1 : r <= g ? 3 : 5;
    const float gray = std::clamp(mn + (mid - mn) * weights[secondary] / 100.f + (mx - mid) * weights[primary] / 100.f, 0.f, 1.f);
    if (!bval(v, "tint")) {
        c[0] = c[1] = c[2] = gray;
        return;
    }
    const QColor t = cval(v, "tintColor");
    float out[3] = {float(t.redF()), float(t.greenF()), float(t.blueF())};
    setLum(out, gray);
    for (int i = 0; i < 3; ++i)
        c[i] = std::clamp(out[i], 0.f, 1.f);
}

// ---- HSB/HSL ---------------------------------------------------------------------------------

float hueOf(const float* c, float mx, float d)
{
    if (d <= 0.f)
        return 0.f;
    float h;
    if (mx == c[0])
        h = std::fmod(std::fmod((c[1] - c[2]) / d, 6.f) + 6.f, 6.f);
    else if (mx == c[1])
        h = (c[2] - c[0]) / d + 2.f;
    else
        h = (c[0] - c[1]) / d + 4.f;
    return h / 6.f;
}

void fromHcm(float h, float chroma, float m, float* out)
{
    const float hp = (h - std::floor(h)) * 6.f;
    const float x = chroma * (1.f - std::abs(std::fmod(hp, 2.f) - 1.f));
    float r, g, b;
    switch (int(hp)) {
    case 0: r = chroma, g = x, b = 0; break;
    case 1: r = x, g = chroma, b = 0; break;
    case 2: r = 0, g = chroma, b = x; break;
    case 3: r = 0, g = x, b = chroma; break;
    case 4: r = x, g = 0, b = chroma; break;
    default: r = chroma, g = 0, b = x; break;
    }
    out[0] = r + m;
    out[1] = g + m;
    out[2] = b + m;
}

void hsbHsl(float* c, const ParamValues& v)
{
    const int input = ival(v, "input"), output = ival(v, "output"); // 0 RGB, 1 HSB, 2 HSL
    if (input == output)
        return;
    float rgb[3] = {std::clamp(c[0], 0.f, 1.f), std::clamp(c[1], 0.f, 1.f), std::clamp(c[2], 0.f, 1.f)};
    if (input == 1)
        fromHcm(rgb[0], rgb[2] * rgb[1], rgb[2] - rgb[2] * rgb[1], rgb);
    else if (input == 2) {
        const float chroma = (1.f - std::abs(2.f * rgb[2] - 1.f)) * rgb[1];
        fromHcm(rgb[0], chroma, rgb[2] - chroma / 2.f, rgb);
    }
    const float mx = std::max({rgb[0], rgb[1], rgb[2]}), mn = std::min({rgb[0], rgb[1], rgb[2]}), d = mx - mn;
    if (output == 0) {
        std::copy_n(rgb, 3, c);
    } else if (output == 1) {
        c[0] = hueOf(rgb, mx, d);
        c[1] = mx > 0.f ? d / mx : 0.f;
        c[2] = mx;
    } else {
        const float l = (mx + mn) / 2.f;
        c[0] = hueOf(rgb, mx, d);
        c[1] = d <= 0.f ? 0.f : std::clamp(d / std::max(1e-7f, 1.f - std::abs(2.f * l - 1.f)), 0.f, 1.f);
        c[2] = l;
    }
}

// ---- Distorsiones ----------------------------------------------------------------------------

float waveShape(int type, float phase)
{
    const float tau = float(2 * M_PI);
    const float p = (phase - tau * std::floor(phase / tau)) / tau;
    if (type == 1)
        return 1.f - 4.f * std::abs(p - 0.5f);
    if (type == 2)
        return p < 0.5f ? 1.f : -1.f;
    return std::sin(phase);
}

void wave(FBuf& b, const RenderArgs& a)
{
    const auto& v = a.values;
    const int n = std::clamp(ival(v, "generators"), 1, 64);
    const float wl0 = float(dval(v, "wavelengthMin")), wl1 = float(dval(v, "wavelengthMax"));
    const float am0 = float(dval(v, "amplitudeMin")), am1 = float(dval(v, "amplitudeMax"));
    const int type = ival(v, "type");
    const uint32_t seed = uint32_t(ival(v, "seed"));
    const Edge edge = ival(v, "undefined") == 1 ? Edge::Repeat : Edge::Wrap;
    auto lerp = [](float lo, float hi, float t) { return lo + std::max(0.f, hi - lo) * t; };
    struct Gen {
        float len, amp, p1, p2;
    };
    std::vector<Gen> gens;
    const float tau = float(2 * M_PI);
    for (int i = 0; i < n; ++i) {
        auto r = [&](uint32_t k) { return hash01(i, int32_t(k), 7, seed); };
        gens.push_back({std::max(1.f, lerp(wl0, wl1, r(1))), lerp(am0, am1, r(2)), r(3) * tau, r(4) * tau});
    }
    const QRect area = a.roi;
    b = remap(b, area, edge, a.cancel, [&](float x, float y) {
        const float lx = x - area.left(), ly = y - area.top();
        float dx = 0.f, dy = 0.f;
        for (const Gen& g : gens) {
            dx += g.amp * waveShape(type, tau * ly / g.len + g.p1);
            dy += g.amp * waveShape(type, tau * lx / g.len + g.p2);
        }
        return QPointF(x + dx / n, y + dy / n);
    });
}

void ripple(FBuf& b, const RenderArgs& a)
{
    const int size = ival(a.values, "size");
    const float len = size == 0 ? 8.f : size == 1 ? 16.f : 32.f;
    const float amp = float(dval(a.values, "amount")) / 100.f * len / 8.f;
    const float tau = float(2 * M_PI);
    const QRect area = a.roi;
    b = remap(b, area, Edge::Repeat, a.cancel, [&](float x, float y) {
        const float lx = x - area.left(), ly = y - area.top();
        return QPointF(x + amp * std::sin(tau * ly / len), y + amp * std::sin(tau * lx / len));
    });
}

void polarCoordinates(FBuf& b, const RenderArgs& a)
{
    const QRect area = a.roi;
    const float bw = float(std::max(1, area.width())), bh = float(std::max(1, area.height()));
    const float cx = area.left() + bw / 2.f, cy = area.top() + bh / 2.f;
    const float rmax = std::min(bw, bh) / 2.f;
    const bool toPolar = ival(a.values, "mode") == 0;
    const float tau = float(2 * M_PI);
    b = remap(b, area, Edge::Repeat, a.cancel, [&](float px, float py) {
        const float x = px - 0.5f, y = py - 0.5f;
        float sx, sy;
        if (toPolar) {
            const float dx = x - cx, dy = y - cy;
            float theta = std::atan2(-dx, -dy);
            if (theta <= 0.f)
                theta += tau;
            const float r = std::sqrt(dx * dx + dy * dy);
            sx = area.left() + theta / tau * bw;
            sy = area.top() + r / rmax * bh;
        } else {
            const float theta = (x + 1.f - area.left()) / bw * tau;
            const float r = (y - area.top()) / bh * rmax;
            sx = cx - r * std::sin(theta);
            sy = cy - r * std::cos(theta);
        }
        return QPointF(sx + 0.5f, sy + 0.5f);
    });
}

void zigzag(FBuf& b, const RenderArgs& a)
{
    const QRect area = a.roi;
    const float cx = area.left() + area.width() / 2.f, cy = area.top() + area.height() / 2.f;
    const float rr = std::min(area.width(), area.height()) / 2.f;
    const float am = std::clamp(float(dval(a.values, "amount")) / 100.f, -1.f, 1.f);
    const float k = std::max(0.5f, std::clamp(float(dval(a.values, "ridges")), 0.f, 20.f));
    const int style = ival(a.values, "style"); // 0 ondas de estanque, 1 desde el centro, 2 alrededor del centro
    const float pi = float(M_PI), tau = float(2 * M_PI);
    b = remap(b, area, Edge::Repeat, a.cancel, [&](float x, float y) {
        const float dx = x - cx, dy = y - cy;
        const float r = std::sqrt(dx * dx + dy * dy);
        if (r >= rr || r <= 0.f || am == 0.f)
            return QPointF(x, y);
        const float rn = r / rr;
        const float w = std::sin(tau * k * rn) * (1.f - rn);
        float radial = 0.f, angular = 0.f;
        if (style == 1)
            radial = am * w * rr / (4.f * k);
        else if (style == 2)
            angular = am * w * pi / (2.f * k);
        else {
            radial = am * w * rr / (8.f * k);
            angular = am * w * pi / (4.f * k);
        }
        const float th = std::atan2(dy, dx) + angular;
        const float rs = std::max(0.f, r + radial);
        return QPointF(cx + rs * std::cos(th), cy + rs * std::sin(th));
    });
}

void offsetEffect(FBuf& b, const RenderArgs& a)
{
    const QRect area = a.roi;
    const int dx = ival(a.values, "horizontal"), dy = ival(a.values, "vertical");
    const int mode = ival(a.values, "undefined"); // 0 ajustar, 1 repetir, 2 transparente
    auto coord = [&](int pos, int dist, int start, int end, int& out) {
        if (start >= end)
            return false;
        const long long p = (long long)pos - dist;
        const long long len = end - start;
        if (mode == 0) {
            long long m = (p - start) % len;
            if (m < 0)
                m += len;
            out = int(start + m);
        } else if (mode == 1) {
            out = int(std::clamp<long long>(p, start, end - 1));
        } else {
            if (p < start || p >= end)
                return false;
            out = int(p);
        }
        return true;
    };
    const FBuf src = b;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        int sy = 0;
        const bool okY = coord(y, dy, area.top(), area.bottom() + 1, sy);
        for (int x = area.left(); x <= area.right(); ++x) {
            int sx = 0;
            float* o = b.at(x, y);
            if (okY && coord(x, dx, area.left(), area.right() + 1, sx))
                std::copy_n(src.at(sx, sy), 4, o);
            else
                o[0] = o[1] = o[2] = o[3] = 0.f;
        }
    }
}

// Inclinar (Shear): cada fila se desplaza según una curva que pasa por los puntos (aquí, dos
// valores: arriba y abajo, con la curva de un arco).
void shear(FBuf& b, const RenderArgs& a)
{
    const QRect area = a.roi;
    const float top = float(dval(a.values, "top")), bottom = float(dval(a.values, "bottom")), middle = float(dval(a.values, "middle"));
    const Edge edge = ival(a.values, "undefined") == 1 ? Edge::Repeat : Edge::Wrap;
    const float h = float(std::max(1, area.height()));
    b = remap(b, area, edge, a.cancel, [&](float x, float y) {
        const float t = (y - area.top()) / h;
        // Cuadrática de Lagrange por arriba (0), el centro (0.5) y abajo (1), en % del ancho.
        const float l0 = 2.f * (t - 0.5f) * (t - 1.f), l1 = -4.f * t * (t - 1.f), l2 = 2.f * t * (t - 0.5f);
        const float shift = (top * l0 + middle * l1 + bottom * l2) / 100.f * area.width() / 2.f;
        return QPointF(x - shift, y);
    });
}

// ---- Estilizado ------------------------------------------------------------------------------

void solarize(float* c, const ParamValues&)
{
    for (int i = 0; i < 3; ++i)
        if (c[i] > 0.5f)
            c[i] = 1.f - c[i];
}

void threshold(float* c, const ParamValues& v)
{
    const float t = std::round(float(dval(v, "level")));
    const float r = std::round(std::clamp(c[0], 0.f, 1.f) * 255.f), g = std::round(std::clamp(c[1], 0.f, 1.f) * 255.f),
                bl = std::round(std::clamp(c[2], 0.f, 1.f) * 255.f);
    const float white = 30.f * r + 59.f * g + 11.f * bl > 100.f * t - 50.f ? 1.f : 0.f;
    c[0] = c[1] = c[2] = white;
}

// Paso alto: la imagen menos su desenfoque, sobre un gris medio.
void highPass(FBuf& b, const RenderArgs& a)
{
    FBuf blur = b;
    gaussian(blur, a.roi, float(dval(a.values, "radius")), a.cancel);
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            const float* q = blur.at(x, y);
            for (int c = 0; c < 3; ++c)
                p[c] = std::clamp(p[c] - q[c] + 0.5f, 0.f, 1.f);
        }
}

// ---- Objeto ---------------------------------------------------------------------------------

// Color a alfa (el de GIMP): cada canal se separa del color clave lo justo para que el color
// visible no cambie, y el alfa es lo que hace falta para llegar a él.
void colorToAlpha(FBuf& b, const RenderArgs& a)
{
    const QColor key = cval(a.values, "color");
    const float k[3] = {float(key.redF()), float(key.greenF()), float(key.blueF())};
    const float transparency = float(dval(a.values, "transparency")) / 100.f;
    const float opacity = std::max(transparency + 1e-4f, float(dval(a.values, "opacity")) / 100.f);
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            float alpha = 0.f;
            for (int c = 0; c < 3; ++c) {
                const float d = p[c] - k[c];
                const float lim = d > 0.f ? 1.f - k[c] : k[c];
                if (lim > 1e-6f)
                    alpha = std::max(alpha, std::abs(d) / lim);
                else if (std::abs(d) > 1e-6f)
                    alpha = 1.f;
            }
            // Los umbrales: hasta 'transparency' se vuelve transparente; desde 'opacity', opaco.
            float na = std::clamp((alpha - transparency) / (opacity - transparency), 0.f, 1.f);
            if (na > 0.f) {
                for (int c = 0; c < 3; ++c)
                    p[c] = std::clamp(k[c] + (p[c] - k[c]) / na, 0.f, 1.f);
            }
            p[3] *= na;
        }
}

// Quitar halo negro o blanco: deshace la mezcla con el fondo de los bordes semitransparentes.
void removeMatte(FBuf& b, const RenderArgs& a, bool white)
{
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            const float al = p[3];
            if (al <= 0.f || al >= 1.f)
                continue;
            for (int c = 0; c < 3; ++c)
                p[c] = white ? std::clamp((p[c] - (1.f - al)) / al, 0.f, 1.f) : std::clamp(p[c] / al, 0.f, 1.f);
        }
}

// Media: toda la zona de un solo color (el promedio de lo visible).
void averageBlur(FBuf& b, const RenderArgs& a)
{
    double sum[3] = {0, 0, 0}, wsum = 0;
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const float* p = b.at(x, y);
            for (int c = 0; c < 3; ++c)
                sum[c] += double(p[c]) * p[3];
            wsum += p[3];
        }
    if (wsum <= 0)
        return;
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            for (int c = 0; c < 3; ++c)
                p[c] = float(sum[c] / wsum);
        }
}

QStringList trList(std::initializer_list<const char*> items)
{
    QStringList out;
    for (const char* s : items)
        out << QObject::tr(s);
    return out;
}

} // namespace

void registerPhotoCraft(QList<const Effect*>& list)
{
    auto add = [&](SimpleEffect::Def def) {
        def.origin = kPhotoCraft;
        def.straight = true;
        list.append(new SimpleEffect(std::move(def)));
    };
    // Ajustes.
    add({"pcVibrance", QObject::tr("Intensidad"), {}, "color-management", {},
         {P::integer("vibrance", QObject::tr("Intensidad"), -100, 100, 0), P::integer("saturation", QObject::tr("Saturación"), -100, 100, 0)},
         perPixel(vibrance)});
    add({"pcThreshold", QObject::tr("Umbral"), {}, "contrast", {}, {P::integer("level", QObject::tr("Nivel"), 1, 255, 128)},
         perPixel(threshold)});
    {
        P from = P::color("from", QObject::tr("Color inicial"), P::ColorDefault::Primary);
        P to = P::color("to", QObject::tr("Color final"), P::ColorDefault::Secondary);
        add({"pcGradientMap", QObject::tr("Mapa de degradado"), {}, "color-gradient", {},
             {from, to, P::boolean("reverse", QObject::tr("Invertir"), false), P::boolean("dither", QObject::tr("Tramado"), false)},
             whole(gradientMap)});
    }
    {
        QList<P> mixer;
        const char* keys[3][4] = {{"rr", "rg", "rb", "rc"}, {"gr", "gg", "gb", "gc"}, {"br", "bg", "bb", "bc"}};
        const QString outs[3] = {QObject::tr("Rojo"), QObject::tr("Verde"), QObject::tr("Azul")};
        const QString ins[4] = {QObject::tr("del rojo"), QObject::tr("del verde"), QObject::tr("del azul"), QObject::tr("constante")};
        for (int o = 0; o < 3; ++o)
            for (int i = 0; i < 4; ++i) {
                P p = P::integer(keys[o][i], QStringLiteral("%1 %2").arg(outs[o], ins[i]), -200, 200, i == o ? 100 : 0);
                p.tab = outs[o];
                mixer << p;
            }
        mixer << P::boolean("monochrome", QObject::tr("Monocromo"), false);
        add({"pcChannelMixer", QObject::tr("Mezclador de canales"), {}, "color-management", {}, mixer,
             perPixel([](float* c, const ParamValues& v) {
                 const char* keys[3][4] = {{"rr", "rg", "rb", "rc"}, {"gr", "gg", "gb", "gc"}, {"br", "bg", "bb", "bc"}};
                 auto mix = [&](int o) {
                     return std::clamp(float(dval(v, keys[o][0]) * c[0] + dval(v, keys[o][1]) * c[1] + dval(v, keys[o][2]) * c[2]
                                             + dval(v, keys[o][3]))
                                           / 100.f,
                                       0.f, 1.f);
                 };
                 if (bval(v, "monochrome")) {
                     const float g = mix(0);
                     c[0] = c[1] = c[2] = g;
                 } else {
                     const float r = mix(0), g = mix(1), b = mix(2);
                     c[0] = r, c[1] = g, c[2] = b;
                 }
             })});
    }
    {
        QStringList presets;
        for (const PhotoFilterPreset& f : kPhotoFilters)
            presets << QObject::tr(f.label);
        presets << QObject::tr("Color");
        P color = P::color("color", QObject::tr("Color"), P::ColorDefault::Primary);
        color.defaultValue = QColor(0xEC, 0x8A, 0x00);
        color.readOnlyWhen = {{QStringLiteral("filter"), {}}};
        for (int i = 0; i < kPhotoFilterCount; ++i)
            color.readOnlyWhen.first().values << i;
        add({"pcPhotoFilter", QObject::tr("Filtro de fotografía"), {}, "camera-photo", {},
             {P::choice("filter", QObject::tr("Filtro"), presets, 0), color, P::integer("density", QObject::tr("Densidad"), 1, 100, 25),
              P::boolean("preserveLuminosity", QObject::tr("Conservar la luminosidad"), true)},
             perPixel(photoFilter)});
    }
    add({"pcSelectiveColor", QObject::tr("Corrección selectiva"), {}, "color-management", {},
         {P::choice("colors", QObject::tr("Colores"),
                    trList({QT_TRANSLATE_NOOP("QObject", "Rojos"), QT_TRANSLATE_NOOP("QObject", "Amarillos"), QT_TRANSLATE_NOOP("QObject", "Verdes"),
                            QT_TRANSLATE_NOOP("QObject", "Cianes"), QT_TRANSLATE_NOOP("QObject", "Azules"), QT_TRANSLATE_NOOP("QObject", "Magentas"),
                            QT_TRANSLATE_NOOP("QObject", "Blancos"), QT_TRANSLATE_NOOP("QObject", "Neutros"), QT_TRANSLATE_NOOP("QObject", "Negros")}),
                    0),
          P::integer("cyan", QObject::tr("Cian"), -100, 100, 0), P::integer("magenta", QObject::tr("Magenta"), -100, 100, 0),
          P::integer("yellow", QObject::tr("Amarillo"), -100, 100, 0), P::integer("black", QObject::tr("Negro"), -100, 100, 0),
          P::choice("method", QObject::tr("Método"), {QObject::tr("Relativo"), QObject::tr("Absoluto")}, 0)},
         perPixel(selectiveColor)});
    add({"pcEqualize", QObject::tr("Ecualizar"), {}, "histogram", {}, {}, whole(equalize)});
    add({"pcAutoContrast", QObject::tr("Contraste automático"), {}, "contrast", {}, {}, whole(autoContrast)});
    add({"pcAutoColor", QObject::tr("Color automático"), {}, "color-management", {}, {}, whole(autoColor)});
    {
        QList<P> bw;
        const QString labels[6] = {QObject::tr("Rojos"), QObject::tr("Amarillos"), QObject::tr("Verdes"),
                                   QObject::tr("Cianes"), QObject::tr("Azules"), QObject::tr("Magentas")};
        const int defs[6] = {40, 60, 40, 60, 20, 80};
        for (int i = 0; i < 6; ++i)
            bw << P::integer(kBwKeys[i], labels[i], -200, 300, defs[i]);
        bw << P::boolean("tint", QObject::tr("Matiz"), false);
        P tint = P::color("tintColor", QObject::tr("Color del matiz"), P::ColorDefault::Primary);
        tint.defaultValue = QColor(0xE1, 0xD3, 0xB3);
        tint.enableKey = QStringLiteral("tint");
        bw << tint;
        add({"pcBlackAndWhiteMixer", QObject::tr("Mezclador de blanco y negro"), {}, "color-mode-black-white", {}, bw, perPixel(blackWhite)});
    }

    // Color.
    add({"pcHsbHsl", QObject::tr("HSB/HSL"), catColor(), "color-management", {},
         {P::choice("input", QObject::tr("Modo de entrada"), {QObject::tr("RGB"), QObject::tr("HSB"), QObject::tr("HSL")}, 0),
          P::choice("output", QObject::tr("Modo de salida"), {QObject::tr("RGB"), QObject::tr("HSB"), QObject::tr("HSL")}, 1)},
         perPixel(hsbHsl)});

    // Distorsionar.
    const QStringList wrapRepeat{QObject::tr("Ajustar"), QObject::tr("Repetir los píxeles del borde")};
    add({"pcOffset", QObject::tr("Desplazamiento"), catDistort(), "transform-move", {},
         {P::integer("horizontal", QObject::tr("Horizontal"), -65535, 65535, 0), P::integer("vertical", QObject::tr("Vertical"), -65535, 65535, 0),
          P::choice("undefined", QObject::tr("Zonas sin definir"), wrapRepeat + QStringList{QObject::tr("Transparente")}, 0)},
         whole(offsetEffect)});
    add({"pcPolarCoordinates", QObject::tr("Coordenadas polares"), catDistort(), "transform-rotate", {},
         {P::choice("mode", QObject::tr("Modo"), {QObject::tr("De rectangulares a polares"), QObject::tr("De polares a rectangulares")}, 0)},
         whole(polarCoordinates)});
    add({"pcWave", QObject::tr("Onda"), catDistort(), "wave", {},
         {P::integer("generators", QObject::tr("Número de generadores"), 1, 999, 5),
          P::integer("wavelengthMin", QObject::tr("Longitud de onda mínima"), 1, 998, 10),
          P::integer("wavelengthMax", QObject::tr("Longitud de onda máxima"), 2, 999, 120),
          P::integer("amplitudeMin", QObject::tr("Amplitud mínima"), 1, 998, 5),
          P::integer("amplitudeMax", QObject::tr("Amplitud máxima"), 1, 999, 35),
          P::choice("type", QObject::tr("Tipo"), {QObject::tr("Seno"), QObject::tr("Triángulo"), QObject::tr("Cuadrado")}, 0),
          P::choice("undefined", QObject::tr("Zonas sin definir"), wrapRepeat, 0), P::seed()},
         whole(wave)});
    add({"pcRipple", QObject::tr("Rizo"), catDistort(), "wave", {},
         {P::integer("amount", QObject::tr("Cantidad"), -999, 999, 100),
          P::choice("size", QObject::tr("Tamaño"), {QObject::tr("Pequeño"), QObject::tr("Mediano"), QObject::tr("Grande")}, 1)},
         whole(ripple)});
    add({"pcZigZag", QObject::tr("Zigzag"), catDistort(), "wave", {},
         {P::integer("amount", QObject::tr("Cantidad"), -100, 100, 10), P::integer("ridges", QObject::tr("Crestas"), 0, 20, 5),
          P::choice("style", QObject::tr("Estilo"),
                    {QObject::tr("Ondas de estanque"), QObject::tr("Desde el centro"), QObject::tr("Alrededor del centro")}, 0)},
         whole(zigzag)});
    add({"pcShear", QObject::tr("Inclinar"), catDistort(), "transform-shear-left", {},
         {P::integer("top", QObject::tr("Arriba"), -100, 100, 30), P::integer("middle", QObject::tr("Centro"), -100, 100, 0),
          P::integer("bottom", QObject::tr("Abajo"), -100, 100, -30), P::choice("undefined", QObject::tr("Zonas sin definir"), wrapRepeat, 0)},
         whole(shear)});

    // Estilizado.
    add({"pcSolarize", QObject::tr("Solarizar"), catStylize(), "weather-clear", {}, {}, perPixel(solarize)});
    add({"pcHighPass", QObject::tr("Paso alto"), catStylize(), "edit-select-all", {},
         {P::real("radius", QObject::tr("Radio"), 0.1, 250, 10, 1)}, whole(highPass)});

    // Objeto.
    {
        P color = P::color("color", QObject::tr("Color"), P::ColorDefault::White);
        add({"pcColorToAlpha", QObject::tr("Color a alfa"), catObject(), "edit-select-all", {},
             {color, P::integer("transparency", QObject::tr("Umbral de transparencia"), 0, 100, 0),
              P::integer("opacity", QObject::tr("Umbral de opacidad"), 0, 100, 100)},
             whole(colorToAlpha)});
    }
    add({"pcRemoveBlackMatte", QObject::tr("Quitar halo negro"), catObject(), "edit-clear", {}, {},
         whole([](FBuf& b, const RenderArgs& a) { removeMatte(b, a, false); })});
    add({"pcRemoveWhiteMatte", QObject::tr("Quitar halo blanco"), catObject(), "edit-clear", {}, {},
         whole([](FBuf& b, const RenderArgs& a) { removeMatte(b, a, true); })});

    // Desenfoques.
    add({"pcAverage", QObject::tr("Media"), catBlurs(), "blur", {}, {}, whole(averageBlur)});

    registerPhotoCraft2(list);
}

} // namespace fx
