// La galería de filtros de PhotoCraft (Artístico, Trazos de pincel, Bosquejo, Textura y algunos
// de Distorsionar, Estilizado, Fotografía y Ruido). Los algoritmos son los de PhotoCraft
// (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT; data/licenses/PhotoCraft-LICENSE.txt)
// escritos para Kaint: cada filtro trabaja con el color sRGB directo de la capa (el alfa no
// cambia), y el ruido, los trazos y las texturas se anclan en la esquina de la zona (la selección).

#include "core/effects/ExtraUtil.h"

#include <QColor>
#include <QHash>
#include <QRegularExpression>

#include <array>
#include <cmath>
#include <map>
#include <mutex>

namespace fx {
namespace {

using namespace extra;
using Px = std::array<float, 3>;
constexpr float kPi = 3.14159265358979f;

inline float lumOf(const Px& p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }
inline Px mixPx(const Px& a, const Px& b, float t) { return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t}; }
template <typename F>
inline Px mapPx(const Px& p, F f) { return {f(p[0]), f(p[1]), f(p[2])}; }
inline Px grey(float v) { return {v, v, v}; }
inline float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
inline float posterize(float v, float levels)
{
    const float l = std::max(1.f, levels - 1.f);
    return std::round(clamp01(v) * l) / l;
}
inline Px saturatePx(const Px& p, float k)
{
    const float l = lumOf(p);
    return mapPx(p, [&](float v) { return l + (v - l) * k; });
}
inline float remEuclid(float a, float b)
{
    const float r = std::fmod(a, b);
    return r < 0.f ? r + b : r;
}
inline float smooth(float e0, float e1, float x)
{
    if (std::abs(e1 - e0) < 1e-9f)
        return x >= e1 ? 1.f : 0.f;
    const float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

// La ventana de trabajo: el color de toda la capa, con coordenadas relativas a la zona.
struct Win {
    int ox = 0, oy = 0; // coordenada del píxel (0, 0) de la capa
    int w = 0, h = 0;
    std::vector<Px> c;

    int xOf(size_t i) const { return ox + int(i % size_t(w)); }
    int yOf(size_t i) const { return oy + int(i / size_t(w)); }
    std::vector<float> lum() const
    {
        std::vector<float> l(c.size());
        for (size_t i = 0; i < c.size(); ++i)
            l[i] = lumOf(c[i]);
        return l;
    }
    Px sample(float x, float y) const
    {
        const float fx = std::clamp(x - 0.5f - ox, 0.f, float(w - 1));
        const float fy = std::clamp(y - 0.5f - oy, 0.f, float(h - 1));
        const int x0 = int(fx), y0 = int(fy);
        const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float ax = fx - x0, ay = fy - y0;
        auto g = [&](int xx, int yy) { return c[size_t(yy) * w + xx]; };
        return mixPx(mixPx(g(x0, y0), g(x1, y0), ax), mixPx(g(x0, y1), g(x1, y1), ax), ay);
    }
    Px at(int x, int y) const
    {
        const int xx = std::clamp(x - ox, 0, w - 1), yy = std::clamp(y - oy, 0, h - 1);
        return c[size_t(yy) * w + xx];
    }
};

std::array<int, 3> boxRadii(float sigma)
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

// Media móvil por filas o columnas de un búfer intercalado de 'n' canales (bordes repetidos).
void boxRows(float* buf, int w, int h, int n, int r)
{
    if (r <= 0 || w <= 0)
        return;
    const double norm = 1.0 / (2 * r + 1);
    parallelRows(0, h, [&](int y0, int y1) {
        std::vector<float> s(size_t(w) * n);
        for (int y = y0; y < y1; ++y) {
            float* row = buf + size_t(y) * w * n;
            std::copy_n(row, size_t(w) * n, s.data());
            auto at = [&](int x, int c) { return double(s[size_t(std::clamp(x, 0, w - 1)) * n + c]); };
            for (int c = 0; c < n; ++c) {
                double acc = 0;
                for (int x = -r; x <= r; ++x)
                    acc += at(x, c);
                for (int x = 0; x < w; ++x) {
                    row[size_t(x) * n + c] = float(acc * norm);
                    acc += at(x + r + 1, c) - at(x - r, c);
                }
            }
        }
    });
}

void boxCols(float* buf, int w, int h, int n, int r)
{
    if (r <= 0 || h <= 0)
        return;
    const double norm = 1.0 / (2 * r + 1);
    parallelRows(0, w, [&](int x0, int x1) {
        std::vector<float> s(size_t(h) * n);
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < h; ++y)
                std::copy_n(buf + (size_t(y) * w + x) * n, n, s.data() + size_t(y) * n);
            auto at = [&](int y, int c) { return double(s[size_t(std::clamp(y, 0, h - 1)) * n + c]); };
            for (int c = 0; c < n; ++c) {
                double acc = 0;
                for (int y = -r; y <= r; ++y)
                    acc += at(y, c);
                for (int y = 0; y < h; ++y) {
                    buf[(size_t(y) * w + x) * n + c] = float(acc * norm);
                    acc += at(y + r + 1, c) - at(y - r, c);
                }
            }
        }
    });
}

void boxBlurN(float* buf, int w, int h, int n, int r)
{
    boxRows(buf, w, h, n, r);
    boxCols(buf, w, h, n, r);
}

void gaussN(float* buf, int w, int h, int n, float sigma)
{
    if (sigma < 0.3f)
        return;
    for (int r : boxRadii(sigma))
        boxRows(buf, w, h, n, r);
    for (int r : boxRadii(sigma))
        boxCols(buf, w, h, n, r);
}

void blur1(std::vector<float>& p, int w, int h, float sigma) { gaussN(p.data(), w, h, 1, sigma); }
void blur3(std::vector<Px>& c, int w, int h, float sigma) { gaussN(c.data()->data(), w, h, 3, sigma); }

void sobel(const std::vector<float>& p, int w, int h, std::vector<float>& gx, std::vector<float>& gy)
{
    gx.assign(size_t(w) * h, 0.f);
    gy.assign(size_t(w) * h, 0.f);
    auto at = [&](int x, int y) { return p[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                gx[size_t(y) * w + x] = ((at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2 * at(x - 1, y) + at(x - 1, y + 1))) / 8.f;
                gy[size_t(y) * w + x] = ((at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2 * at(x, y - 1) + at(x + 1, y - 1))) / 8.f;
            }
    });
}

std::vector<float> gradMag(const std::vector<float>& p, int w, int h)
{
    std::vector<float> gx, gy;
    sobel(p, w, h, gx, gy);
    for (size_t i = 0; i < gx.size(); ++i)
        gx[i] = std::sqrt(gx[i] * gx[i] + gy[i] * gy[i]);
    return gx;
}

// Media móvil de radio r a lo largo de la dirección d ((1,0), (0,1), (1,1) o (1,-1)).
void lineBlur(float* buf, int n, int w, int h, int dx, int dy, int r)
{
    if (r <= 0 || w <= 0 || h <= 0)
        return;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
    const double norm = 1.0 / (2 * r + 1);
    std::vector<size_t> idx;
    std::vector<double> vals;
    for (int sy = 0; sy < h; ++sy)
        for (int sx = 0; sx < w; ++sx) {
            if (inside(sx - dx, sy - dy))
                continue;
            idx.clear();
            for (int x = sx, y = sy; inside(x, y); x += dx, y += dy)
                idx.push_back((size_t(y) * w + x) * n);
            const int m = int(idx.size());
            for (int c = 0; c < n; ++c) {
                vals.clear();
                for (size_t o : idx)
                    vals.push_back(buf[o + c]);
                auto at = [&](int k) { return vals[size_t(std::clamp(k, 0, m - 1))]; };
                double acc = 0;
                for (int k = -r; k <= r; ++k)
                    acc += at(k);
                for (int k = 0; k < m; ++k) {
                    buf[idx[size_t(k)] + c] = float(acc * norm);
                    acc += at(k + r + 1) - at(k - r);
                }
            }
        }
}

void lineBlur3(std::vector<Px>& c, int w, int h, int dx, int dy, int r) { lineBlur(c.data()->data(), 3, w, h, dx, dy, r); }

std::pair<int, int> direction(const QString& name)
{
    if (name == QLatin1String("horizontal"))
        return {1, 0};
    if (name == QLatin1String("vertical"))
        return {0, 1};
    if (name == QLatin1String("leftDiagonal"))
        return {1, 1};
    return {1, -1};
}

void kuwahara(Win& win, int r)
{
    const int h2 = std::max(1, (r + 1) / 2);
    const int w = win.w, h = win.h;
    std::vector<float> m(size_t(w) * h * 5);
    for (size_t i = 0; i < win.c.size(); ++i) {
        const float l = lumOf(win.c[i]);
        float* q = &m[i * 5];
        q[0] = win.c[i][0];
        q[1] = win.c[i][1];
        q[2] = win.c[i][2];
        q[3] = l;
        q[4] = l * l;
    }
    boxBlurN(m.data(), w, h, 5, h2);
    auto get = [&](int x, int y) { return &m[(size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)) * 5]; };
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                float best = std::numeric_limits<float>::max();
                Px col{};
                for (const auto& d : {std::pair{-h2, -h2}, std::pair{h2, -h2}, std::pair{-h2, h2}, std::pair{h2, h2}}) {
                    const float* q = get(x + d.first, y + d.second);
                    const float var = q[4] - q[3] * q[3];
                    if (var < best) {
                        best = var;
                        col = {q[0], q[1], q[2]};
                    }
                }
                win.c[size_t(y) * w + x] = col;
            }
    });
}

void unsharp(Win& win, float sigma, float amount)
{
    if (amount <= 0.f)
        return;
    std::vector<Px> b = win.c;
    blur3(b, win.w, win.h, sigma);
    for (size_t i = 0; i < win.c.size(); ++i)
        for (int k = 0; k < 3; ++k)
            win.c[i][k] += (win.c[i][k] - b[i][k]) * amount;
}

float vnoise(float x, float y, uint32_t seed)
{
    const float fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    auto hh = [&](int a, int b) { return hash01(a, b, 0, seed); };
    const float top = hh(ix, iy) + (hh(ix + 1, iy) - hh(ix, iy)) * sx;
    const float bot = hh(ix, iy + 1) + (hh(ix + 1, iy + 1) - hh(ix, iy + 1)) * sx;
    return top + (bot - top) * sy;
}

float fbm(float x, float y, uint32_t seed)
{
    return vnoise(x, y, seed) * 0.57f + vnoise(x * 2.03f, y * 2.03f, seed ^ 0x51u) * 0.29f + vnoise(x * 4.1f, y * 4.1f, seed ^ 0xa3u) * 0.14f;
}

float strokes(int x, int y, int dxi, int dyi, float len, float wid, uint32_t seed)
{
    const float dx = float(dxi), dy = float(dyi);
    const float l = std::max(1e-6f, std::sqrt(dx * dx + dy * dy));
    const float ux = dx / l, uy = dy / l;
    const float u = x * ux + y * uy, v = -x * uy + y * ux;
    return vnoise(u / std::max(0.5f, len), v / std::max(0.3f, wid), seed);
}

std::pair<Px, float> paintStroke(const Win& src, int x, int y, int dxi, int dyi, float len, float wid, uint32_t seed)
{
    const float dx = float(dxi), dy = float(dyi);
    const float l = std::max(1e-6f, std::sqrt(dx * dx + dy * dy));
    const float ux = dx / l, uy = dy / l;
    len = std::max(1.f, len);
    wid = std::max(0.5f, wid);
    const float fx = x + 0.5f, fy = y + 0.5f;
    const float u = fx * ux + fy * uy, v = -fx * uy + fy * ux;
    const int lane = int(std::floor(v / wid));
    const float off = hash01(lane, 0, 0, seed) * len;
    const float t = remEuclid((u + off) / len, 1.f);
    const float du = (0.5f - t) * len;
    return {src.sample(fx + du * ux, fy + du * uy), t};
}

std::pair<float, float> lightDir(const QString& name)
{
    const float s = 0.70710678f;
    if (name == QLatin1String("bottom"))
        return {0, 1};
    if (name == QLatin1String("bottomLeft"))
        return {-s, s};
    if (name == QLatin1String("left"))
        return {-1, 0};
    if (name == QLatin1String("topLeft"))
        return {-s, -s};
    if (name == QLatin1String("topRight"))
        return {s, -s};
    if (name == QLatin1String("right"))
        return {1, 0};
    if (name == QLatin1String("bottomRight"))
        return {s, s};
    return {0, -1};
}

float shadeOf(float gx, float gy, std::pair<float, float> light, float k)
{
    const float nx = -k * gx, ny = -k * gy, nz = 1.f;
    const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float lx = light.first, ly = light.second, lz = 1.f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float d = (nx * lx + ny * ly + nz * lz) / (nl * ll);
    return d / (lz / ll);
}

float weave(float x, float y, float p, float noise)
{
    const int cx = int(std::floor(x / p)), cy = int(std::floor(y / p));
    const bool overH = ((cx + cy) % 2 + 2) % 2 == 0;
    const float u = remEuclid(x, p) / p, v = remEuclid(y, p) / p;
    const float ridge = overH ? std::sin(v * kPi) : std::sin(u * kPi);
    const float n = vnoise(x / (p * 0.7f), y / (p * 2.f), 97);
    return ridge * (1.f - noise) + n * noise;
}

float texture(const QString& kind, float x, float y, float s)
{
    x /= s;
    y /= s;
    if (kind == QLatin1String("brick")) {
        const float bw = 24.f, bh = 12.f;
        const float row = std::floor(y / bh);
        const float xo = x + (int(row) % 2 == 0 ? 0.f : bw / 2.f);
        const float u = remEuclid(xo, bw), v = remEuclid(y, bh);
        const float d = std::min({u, bw - u, v, bh - v});
        return smooth(0.6f, 2.2f, d) * (0.85f + 0.15f * vnoise(x / 3.f, y / 3.f, 91));
    }
    if (kind == QLatin1String("burlap"))
        return weave(x, y, 7.f, 0.35f);
    if (kind == QLatin1String("canvas"))
        return weave(x, y, 3.5f, 0.18f);
    if (kind == QLatin1String("sandstone"))
        return fbm(x / 7.f, y / 7.f, 93) * 0.7f + hash01(int(x), int(y), 0, 94) * 0.3f;
    if (kind == QLatin1String("blocks")) {
        const float b = 18.f;
        const float u = remEuclid(x, b) / b, v = remEuclid(y, b) / b;
        return std::min(1.f, std::min({u, 1.f - u, v, 1.f - v}) * 4.f);
    }
    if (kind == QLatin1String("frosted"))
        return fbm(x / 2.5f, y / 2.5f, 95);
    if (kind == QLatin1String("tinyLens")) {
        const float p = 12.f;
        const float u = (remEuclid(x, p) / p - 0.5f) * 2.f, v = (remEuclid(y, p) / p - 0.5f) * 2.f;
        return std::sqrt(std::max(0.f, 1.f - (u * u + v * v)));
    }
    return 0.5f;
}

template <typename Weight>
void texturizeBy(Win& win, const QString& kind, float scaling, float relief, const QString& light, bool invert, Weight weight)
{
    if (relief <= 0.f)
        return;
    const float s = std::clamp(scaling / 100.f, 0.5f, 2.f);
    const auto l = lightDir(light);
    const float k = relief / 50.f * 3.f;
    const float sign = invert ? -1.f : 1.f;
    parallelRows(0, win.h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * win.w; i < size_t(y1) * win.w; ++i) {
            const float fx = float(win.xOf(i)), fy = float(win.yOf(i));
            const float gx = (texture(kind, fx + 1, fy, s) - texture(kind, fx - 1, fy, s)) * 0.5f * sign;
            const float gy = (texture(kind, fx, fy + 1, s) - texture(kind, fx, fy - 1, s)) * 0.5f * sign;
            const float f = 1.f + (std::clamp(shadeOf(gx, gy, l, k), 0.f, 2.f) - 1.f) * weight(win.c[i]);
            win.c[i] = mapPx(win.c[i], [&](float v) { return v * f; });
        }
    });
}

void texturize(Win& win, const QString& kind, float scaling, float relief, const QString& light, bool invert)
{
    texturizeBy(win, kind, scaling, relief, light, invert, [](const Px&) { return 1.f; });
}

std::pair<float, float> cellPoint(int cx, int cy, float cell, uint32_t seed)
{
    return {(cx + 0.1f + 0.8f * hash01(cx, cy, 11, seed)) * cell, (cy + 0.1f + 0.8f * hash01(cx, cy, 12, seed)) * cell};
}

struct Voronoi {
    float d1, d2;
    std::pair<float, float> p1;
};

Voronoi voronoi(float x, float y, float cell, uint32_t seed)
{
    const int cx = int(std::floor(x / cell)), cy = int(std::floor(y / cell));
    Voronoi v{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), {x, y}};
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            const auto p = cellPoint(cx + i, cy + j, cell, seed);
            const float d = std::sqrt((p.first - x) * (p.first - x) + (p.second - y) * (p.second - y));
            if (d < v.d1) {
                v.d2 = v.d1;
                v.d1 = d;
                v.p1 = p;
            } else if (d < v.d2) {
                v.d2 = d;
            }
        }
    return v;
}

Px highlightsOf(const Px& p, float l, float area, float intensity)
{
    if (area <= 0.f)
        return p;
    const float m = smooth(1.f - area / 20.f * 0.75f, 1.02f, l) * (1.f - intensity / 10.f * 0.6f);
    return mapPx(p, [&](float v) { return v + (1.f - v) * m; });
}

const std::vector<float>& halftoneDotRanks()
{
    static const std::vector<float> ranks = [] {
        const int n = 128;
        std::vector<float> v(size_t(n) * n);
        for (int i = 0; i < n * n; ++i) {
            const float u = ((i % n) + 0.5f) / n, w = ((i / n) + 0.5f) / n;
            v[size_t(i)] = (std::cos(2 * kPi * u) + std::cos(2 * kPi * w)) / 2.f;
        }
        std::sort(v.begin(), v.end());
        return v;
    }();
    return ranks;
}

std::vector<float> halftoneOffsets(float gain)
{
    const float half = 0.5f / std::max(1e-3f, gain);
    auto cover = [&](float tau) {
        const float a = tau - half, z = tau + half;
        const float solid = clamp01(a);
        const float r0 = clamp01(a), r1 = clamp01(z);
        const float ramp = r1 > r0 ? (0.5f + tau * gain) * (r1 - r0) - 0.5f * gain * (r1 * r1 - r0 * r0) : 0.f;
        return solid + ramp;
    };
    std::vector<float> out(256);
    for (int i = 0; i < 256; ++i) {
        const float target = i / 255.f;
        float lo = -half - 1.f, hi = 1.f + half + 1.f;
        for (int k = 0; k < 40; ++k) {
            const float mid = (lo + hi) / 2.f;
            if (cover(mid) < target)
                lo = mid;
            else
                hi = mid;
        }
        out[size_t(i)] = i == 0 ? -half - 1.f : i == 255 ? 1.f + half + 1.f : (lo + hi) / 2.f;
    }
    return out;
}

std::array<float, 3> normalize3(std::array<float, 3> v)
{
    const float l = std::max(1e-9f, std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    return {v[0] / l, v[1] / l, v[2] / l};
}

// --- Los filtros --------------------------------------------------------------------------

struct Filter {
    const char* key;
    const char* name; // traducible (QObject)
    int category;     // 0 Artístico, 1 Trazos de pincel, 2 Distorsionar, 3 Bosquejo, 4 Estilizado, 5 Textura, 6 Fotografía, 7 Ruido
    const char* params;
};

// Los parámetros de cada filtro (los de PhotoCraft): número min..max=def, "a|b|c" (lista; el
// primero por omisión) o bool. Los colores (primer plano, fondo, resplandor) van aparte.
const Filter kFilters[] = {
    {"coloredPencil", QT_TRANSLATE_NOOP("QObject", "Lápiz de color"), 0, "pencilWidth:1..24=4,strokePressure:0..15=8,paperBrightness:0..50=25"},
    {"cutout", QT_TRANSLATE_NOOP("QObject", "Cuarteado"), 0, "numberOfLevels:2..8=4,edgeSimplicity:0..10=4,edgeFidelity:1..3=2"},
    {"dryBrush", QT_TRANSLATE_NOOP("QObject", "Pincel seco"), 0, "brushSize:0..10=2,brushDetail:0..10=8,texture:1..3=1"},
    {"filmGrain", QT_TRANSLATE_NOOP("QObject", "Grano de película"), 7, "grain:0..20=4,highlightArea:0..20=0,intensity:0..10=10"},
    {"fresco", QT_TRANSLATE_NOOP("QObject", "Fresco"), 0, "brushSize:0..10=2,brushDetail:0..10=8,texture:1..3=1"},
    {"neonGlow", QT_TRANSLATE_NOOP("QObject", "Resplandor de neón"), 4, "glowSize:-24..24=5,glowBrightness:0..50=15"},
    {"paintDaubs", QT_TRANSLATE_NOOP("QObject", "Manchas de pintura"), 0, "brushSize:1..50=8,sharpness:0..40=7,brushType:simple|lightRough|darkRough|wideSharp|wideBlurry|sparkle"},
    {"paletteKnife", QT_TRANSLATE_NOOP("QObject", "Espátula"), 0, "strokeSize:1..50=25,strokeDetail:1..3=3,softness:0..10=0"},
    {"plasticWrap", QT_TRANSLATE_NOOP("QObject", "Envoltura plástica"), 4, "highlightStrength:0..20=15,detail:1..15=9,smoothness:1..15=7"},
    {"posterEdges", QT_TRANSLATE_NOOP("QObject", "Bordes de póster"), 0, "edgeThickness:0..10=2,edgeIntensity:0..10=1,posterization:0..6=2"},
    {"roughPastels", QT_TRANSLATE_NOOP("QObject", "Pastel grueso"), 0,
     "strokeLength:0..40=6,strokeDetail:1..20=4,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=20,light:bottom|bottomLeft|left|topLeft|top|topRight|right|bottomRight,invert:bool"},
    {"smudgeStick", QT_TRANSLATE_NOOP("QObject", "Barra difuminada"), 0, "strokeLength:0..10=2,highlightArea:0..20=0,intensity:0..10=10"},
    {"sponge", QT_TRANSLATE_NOOP("QObject", "Esponja"), 0, "brushSize:0..10=2,definition:0..25=12,smoothness:1..15=5"},
    {"underpainting", QT_TRANSLATE_NOOP("QObject", "Pintura subyacente"), 0,
     "brushSize:0..40=6,textureCoverage:0..40=16,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"},
    {"watercolor", QT_TRANSLATE_NOOP("QObject", "Acuarela"), 0, "brushDetail:1..14=9,shadowIntensity:0..10=1,texture:1..3=1"},
    {"accentedEdges", QT_TRANSLATE_NOOP("QObject", "Bordes acentuados"), 1, "edgeWidth:1..14=2,edgeBrightness:0..50=38,smoothness:1..15=5"},
    {"angledStrokes", QT_TRANSLATE_NOOP("QObject", "Trazos angulares"), 1, "directionBalance:0..100=50,strokeLength:3..50=15,sharpness:0..10=3"},
    {"crosshatch", QT_TRANSLATE_NOOP("QObject", "Sombreado cruzado"), 1, "strokeLength:3..50=9,sharpness:0..20=6,strength:1..3=1"},
    {"darkStrokes", QT_TRANSLATE_NOOP("QObject", "Trazos oscuros"), 1, "balance:0..10=5,blackIntensity:0..10=6,whiteIntensity:0..10=2"},
    {"inkOutlines", QT_TRANSLATE_NOOP("QObject", "Contornos con tinta"), 1, "strokeLength:1..50=4,darkIntensity:0..50=20,lightIntensity:0..50=10"},
    {"spatter", QT_TRANSLATE_NOOP("QObject", "Salpicaduras"), 1, "sprayRadius:0..25=10,smoothness:1..15=5"},
    {"sprayedStrokes", QT_TRANSLATE_NOOP("QObject", "Trazos pulverizados"), 1,
     "strokeLength:0..20=12,sprayRadius:0..25=7,strokeDirection:rightDiagonal|horizontal|leftDiagonal|vertical"},
    {"sumiE", QT_TRANSLATE_NOOP("QObject", "Sumi-e"), 1, "strokeWidth:3..15=10,strokePressure:0..15=2,contrast:0..40=16"},
    {"diffuseGlow", QT_TRANSLATE_NOOP("QObject", "Resplandor difuso"), 6, "graininess:0..10=6,glowAmount:0..20=10,clearAmount:0..20=15"},
    {"glass", QT_TRANSLATE_NOOP("QObject", "Vidrio"), 2, "distortion:0..20=5,smoothness:1..15=3,texture:frosted|blocks|canvas|tinyLens,scaling:50..200=100,invert:bool"},
    {"oceanRipple", QT_TRANSLATE_NOOP("QObject", "Rizo oceánico"), 2, "rippleSize:1..15=9,rippleMagnitude:0..20=9"},
    {"basRelief", QT_TRANSLATE_NOOP("QObject", "Bajorrelieve"), 3, "detail:1..15=13,smoothness:1..15=3,light:bottom|bottomLeft|left|topLeft|top|topRight|right|bottomRight"},
    {"chalkCharcoal", QT_TRANSLATE_NOOP("QObject", "Tiza y carboncillo"), 3, "charcoalArea:0..20=6,chalkArea:0..20=6,strokePressure:0..5=1"},
    {"charcoal", QT_TRANSLATE_NOOP("QObject", "Carboncillo"), 3, "charcoalThickness:1..7=1,detail:0..5=5,lightDarkBalance:0..100=50"},
    {"chrome", QT_TRANSLATE_NOOP("QObject", "Cromo"), 3, "detail:0..10=4,smoothness:0..10=7"},
    {"conteCrayon", QT_TRANSLATE_NOOP("QObject", "Crayón Conté"), 3,
     "foregroundLevel:1..15=11,backgroundLevel:1..15=7,texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"},
    {"graphicPen", QT_TRANSLATE_NOOP("QObject", "Pluma gráfica"), 3, "strokeLength:1..15=15,lightDarkBalance:0..100=50,strokeDirection:rightDiagonal|horizontal|leftDiagonal|vertical"},
    {"halftonePattern", QT_TRANSLATE_NOOP("QObject", "Trama de semitonos"), 2, "size:1..12=1,contrast:0..50=5,patternType:dot|circle|line"},
    {"notePaper", QT_TRANSLATE_NOOP("QObject", "Papel de cartas"), 3, "imageBalance:0..50=25,graininess:0..20=10,relief:0..25=11"},
    {"photocopy", QT_TRANSLATE_NOOP("QObject", "Fotocopia"), 3, "detail:1..24=7,darkness:1..50=8"},
    {"plaster", QT_TRANSLATE_NOOP("QObject", "Yeso"), 3, "imageBalance:0..50=20,smoothness:1..15=2,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft"},
    {"reticulation", QT_TRANSLATE_NOOP("QObject", "Reticulación"), 3, "density:0..50=12,foregroundLevel:0..50=40,backgroundLevel:0..50=5"},
    {"stamp", QT_TRANSLATE_NOOP("QObject", "Sello"), 3, "lightDarkBalance:0..50=25,smoothness:1..50=5"},
    {"tornEdges", QT_TRANSLATE_NOOP("QObject", "Bordes rasgados"), 3, "imageBalance:0..50=25,smoothness:1..15=11,contrast:1..25=17"},
    {"waterPaper", QT_TRANSLATE_NOOP("QObject", "Papel húmedo"), 3, "fiberLength:3..50=15,brightness:0..100=60,contrast:0..100=80"},
    {"glowingEdges", QT_TRANSLATE_NOOP("QObject", "Bordes brillantes"), 4, "edgeWidth:1..14=2,edgeBrightness:0..20=6,smoothness:1..15=5"},
    {"craquelure", QT_TRANSLATE_NOOP("QObject", "Craquelado"), 5, "crackSpacing:2..100=15,crackDepth:0..10=6,crackBrightness:0..10=9"},
    {"grain", QT_TRANSLATE_NOOP("QObject", "Granulado"), 5,
     "intensity:0..100=40,contrast:0..100=50,grainType:regular|soft|sprinkles|clumped|contrasty|enlarged|stippled|horizontal|vertical|speckle"},
    {"mosaicTiles", QT_TRANSLATE_NOOP("QObject", "Azulejos de mosaico"), 5, "tileSize:2..100=12,groutWidth:1..15=3,lightenGrout:0..10=9"},
    {"patchwork", QT_TRANSLATE_NOOP("QObject", "Patchwork"), 5, "squareSize:0..10=4,relief:0..25=8"},
    {"stainedGlass", QT_TRANSLATE_NOOP("QObject", "Vidriera"), 5, "cellSize:2..50=10,borderThickness:1..20=4,lightIntensity:0..10=3"},
    {"texturizer", QT_TRANSLATE_NOOP("QObject", "Texturizador"), 5,
     "texture:canvas|brick|burlap|sandstone,scaling:50..200=100,relief:0..50=4,light:top|topRight|right|bottomRight|bottom|bottomLeft|left|topLeft,invert:bool"},
};

// Qué colores usa cada filtro (primer plano y fondo, y el del resplandor de neón).
bool usesForeground(const QString& k)
{
    static const QStringList keys{QStringLiteral("neonGlow"), QStringLiteral("basRelief"), QStringLiteral("chalkCharcoal"), QStringLiteral("charcoal"),
                                  QStringLiteral("conteCrayon"), QStringLiteral("graphicPen"), QStringLiteral("halftonePattern"), QStringLiteral("notePaper"),
                                  QStringLiteral("photocopy"), QStringLiteral("plaster"), QStringLiteral("reticulation"), QStringLiteral("stamp"),
                                  QStringLiteral("tornEdges"), QStringLiteral("grain"), QStringLiteral("stainedGlass")};
    return keys.contains(k);
}
bool usesBackground(const QString& k)
{
    return k != QLatin1String("stainedGlass") && (usesForeground(k) || k == QLatin1String("coloredPencil") || k == QLatin1String("diffuseGlow"));
}

// Etiquetas de los parámetros y de las opciones de las listas.
QString paramLabel(const QString& key)
{
    static const QHash<QString, const char*> labels{
        {QStringLiteral("pencilWidth"), QT_TRANSLATE_NOOP("QObject", "Ancho del lápiz")},
        {QStringLiteral("strokePressure"), QT_TRANSLATE_NOOP("QObject", "Presión del trazo")},
        {QStringLiteral("paperBrightness"), QT_TRANSLATE_NOOP("QObject", "Brillo del papel")},
        {QStringLiteral("numberOfLevels"), QT_TRANSLATE_NOOP("QObject", "Número de niveles")},
        {QStringLiteral("edgeSimplicity"), QT_TRANSLATE_NOOP("QObject", "Simplicidad de los bordes")},
        {QStringLiteral("edgeFidelity"), QT_TRANSLATE_NOOP("QObject", "Fidelidad de los bordes")},
        {QStringLiteral("brushSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño del pincel")},
        {QStringLiteral("brushDetail"), QT_TRANSLATE_NOOP("QObject", "Detalle del pincel")},
        {QStringLiteral("texture"), QT_TRANSLATE_NOOP("QObject", "Textura")},
        {QStringLiteral("grain"), QT_TRANSLATE_NOOP("QObject", "Grano")},
        {QStringLiteral("highlightArea"), QT_TRANSLATE_NOOP("QObject", "Área de iluminación")},
        {QStringLiteral("intensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad")},
        {QStringLiteral("glowSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño del resplandor")},
        {QStringLiteral("glowBrightness"), QT_TRANSLATE_NOOP("QObject", "Brillo del resplandor")},
        {QStringLiteral("sharpness"), QT_TRANSLATE_NOOP("QObject", "Nitidez")},
        {QStringLiteral("brushType"), QT_TRANSLATE_NOOP("QObject", "Tipo de pincel")},
        {QStringLiteral("strokeSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño del trazo")},
        {QStringLiteral("strokeDetail"), QT_TRANSLATE_NOOP("QObject", "Detalle del trazo")},
        {QStringLiteral("softness"), QT_TRANSLATE_NOOP("QObject", "Suavidad")},
        {QStringLiteral("highlightStrength"), QT_TRANSLATE_NOOP("QObject", "Intensidad de las iluminaciones")},
        {QStringLiteral("detail"), QT_TRANSLATE_NOOP("QObject", "Detalle")},
        {QStringLiteral("smoothness"), QT_TRANSLATE_NOOP("QObject", "Suavizado")},
        {QStringLiteral("edgeThickness"), QT_TRANSLATE_NOOP("QObject", "Grosor de los bordes")},
        {QStringLiteral("edgeIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad de los bordes")},
        {QStringLiteral("posterization"), QT_TRANSLATE_NOOP("QObject", "Posterización")},
        {QStringLiteral("strokeLength"), QT_TRANSLATE_NOOP("QObject", "Longitud del trazo")},
        {QStringLiteral("scaling"), QT_TRANSLATE_NOOP("QObject", "Escala")},
        {QStringLiteral("relief"), QT_TRANSLATE_NOOP("QObject", "Relieve")},
        {QStringLiteral("light"), QT_TRANSLATE_NOOP("QObject", "Luz")},
        {QStringLiteral("invert"), QT_TRANSLATE_NOOP("QObject", "Invertir")},
        {QStringLiteral("definition"), QT_TRANSLATE_NOOP("QObject", "Definición")},
        {QStringLiteral("textureCoverage"), QT_TRANSLATE_NOOP("QObject", "Cobertura de la textura")},
        {QStringLiteral("shadowIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad de las sombras")},
        {QStringLiteral("edgeWidth"), QT_TRANSLATE_NOOP("QObject", "Ancho de los bordes")},
        {QStringLiteral("edgeBrightness"), QT_TRANSLATE_NOOP("QObject", "Brillo de los bordes")},
        {QStringLiteral("directionBalance"), QT_TRANSLATE_NOOP("QObject", "Equilibrio de la dirección")},
        {QStringLiteral("strength"), QT_TRANSLATE_NOOP("QObject", "Fuerza")},
        {QStringLiteral("balance"), QT_TRANSLATE_NOOP("QObject", "Equilibrio")},
        {QStringLiteral("blackIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad del negro")},
        {QStringLiteral("whiteIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad del blanco")},
        {QStringLiteral("darkIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad oscura")},
        {QStringLiteral("lightIntensity"), QT_TRANSLATE_NOOP("QObject", "Intensidad de la luz")},
        {QStringLiteral("sprayRadius"), QT_TRANSLATE_NOOP("QObject", "Radio de pulverización")},
        {QStringLiteral("strokeDirection"), QT_TRANSLATE_NOOP("QObject", "Dirección del trazo")},
        {QStringLiteral("strokeWidth"), QT_TRANSLATE_NOOP("QObject", "Ancho del trazo")},
        {QStringLiteral("contrast"), QT_TRANSLATE_NOOP("QObject", "Contraste")},
        {QStringLiteral("graininess"), QT_TRANSLATE_NOOP("QObject", "Granulado")},
        {QStringLiteral("glowAmount"), QT_TRANSLATE_NOOP("QObject", "Cantidad de resplandor")},
        {QStringLiteral("clearAmount"), QT_TRANSLATE_NOOP("QObject", "Cantidad de claridad")},
        {QStringLiteral("distortion"), QT_TRANSLATE_NOOP("QObject", "Distorsión")},
        {QStringLiteral("rippleSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño del rizo")},
        {QStringLiteral("rippleMagnitude"), QT_TRANSLATE_NOOP("QObject", "Magnitud del rizo")},
        {QStringLiteral("charcoalArea"), QT_TRANSLATE_NOOP("QObject", "Área de carboncillo")},
        {QStringLiteral("chalkArea"), QT_TRANSLATE_NOOP("QObject", "Área de tiza")},
        {QStringLiteral("charcoalThickness"), QT_TRANSLATE_NOOP("QObject", "Grosor del carboncillo")},
        {QStringLiteral("lightDarkBalance"), QT_TRANSLATE_NOOP("QObject", "Equilibrio claro/oscuro")},
        {QStringLiteral("foregroundLevel"), QT_TRANSLATE_NOOP("QObject", "Nivel del primer plano")},
        {QStringLiteral("backgroundLevel"), QT_TRANSLATE_NOOP("QObject", "Nivel del fondo")},
        {QStringLiteral("size"), QT_TRANSLATE_NOOP("QObject", "Tamaño")},
        {QStringLiteral("patternType"), QT_TRANSLATE_NOOP("QObject", "Tipo de trama")},
        {QStringLiteral("imageBalance"), QT_TRANSLATE_NOOP("QObject", "Equilibrio de la imagen")},
        {QStringLiteral("darkness"), QT_TRANSLATE_NOOP("QObject", "Oscuridad")},
        {QStringLiteral("density"), QT_TRANSLATE_NOOP("QObject", "Densidad")},
        {QStringLiteral("fiberLength"), QT_TRANSLATE_NOOP("QObject", "Longitud de las fibras")},
        {QStringLiteral("brightness"), QT_TRANSLATE_NOOP("QObject", "Brillo")},
        {QStringLiteral("crackSpacing"), QT_TRANSLATE_NOOP("QObject", "Separación de las grietas")},
        {QStringLiteral("crackDepth"), QT_TRANSLATE_NOOP("QObject", "Profundidad de las grietas")},
        {QStringLiteral("crackBrightness"), QT_TRANSLATE_NOOP("QObject", "Brillo de las grietas")},
        {QStringLiteral("grainType"), QT_TRANSLATE_NOOP("QObject", "Tipo de grano")},
        {QStringLiteral("tileSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño de los azulejos")},
        {QStringLiteral("groutWidth"), QT_TRANSLATE_NOOP("QObject", "Ancho de la lechada")},
        {QStringLiteral("lightenGrout"), QT_TRANSLATE_NOOP("QObject", "Aclarar la lechada")},
        {QStringLiteral("squareSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño de los cuadrados")},
        {QStringLiteral("cellSize"), QT_TRANSLATE_NOOP("QObject", "Tamaño de las celdas")},
        {QStringLiteral("borderThickness"), QT_TRANSLATE_NOOP("QObject", "Grosor del borde")},
    };
    const char* l = labels.value(key);
    return l ? QObject::tr(l) : key;
}

QString choiceLabel(const QString& v)
{
    static const QHash<QString, const char*> labels{
        {QStringLiteral("simple"), QT_TRANSLATE_NOOP("QObject", "Simple")},
        {QStringLiteral("lightRough"), QT_TRANSLATE_NOOP("QObject", "Claro y rugoso")},
        {QStringLiteral("darkRough"), QT_TRANSLATE_NOOP("QObject", "Oscuro y rugoso")},
        {QStringLiteral("wideSharp"), QT_TRANSLATE_NOOP("QObject", "Ancho y nítido")},
        {QStringLiteral("wideBlurry"), QT_TRANSLATE_NOOP("QObject", "Ancho y borroso")},
        {QStringLiteral("sparkle"), QT_TRANSLATE_NOOP("QObject", "Chispas")},
        {QStringLiteral("canvas"), QT_TRANSLATE_NOOP("QObject", "Lienzo")},
        {QStringLiteral("brick"), QT_TRANSLATE_NOOP("QObject", "Ladrillo")},
        {QStringLiteral("burlap"), QT_TRANSLATE_NOOP("QObject", "Arpillera")},
        {QStringLiteral("sandstone"), QT_TRANSLATE_NOOP("QObject", "Arenisca")},
        {QStringLiteral("frosted"), QT_TRANSLATE_NOOP("QObject", "Escarchado")},
        {QStringLiteral("blocks"), QT_TRANSLATE_NOOP("QObject", "Bloques")},
        {QStringLiteral("tinyLens"), QT_TRANSLATE_NOOP("QObject", "Lentes pequeñas")},
        {QStringLiteral("bottom"), QT_TRANSLATE_NOOP("QObject", "Abajo")},
        {QStringLiteral("bottomLeft"), QT_TRANSLATE_NOOP("QObject", "Abajo a la izquierda")},
        {QStringLiteral("left"), QT_TRANSLATE_NOOP("QObject", "Izquierda")},
        {QStringLiteral("topLeft"), QT_TRANSLATE_NOOP("QObject", "Arriba a la izquierda")},
        {QStringLiteral("top"), QT_TRANSLATE_NOOP("QObject", "Arriba")},
        {QStringLiteral("topRight"), QT_TRANSLATE_NOOP("QObject", "Arriba a la derecha")},
        {QStringLiteral("right"), QT_TRANSLATE_NOOP("QObject", "Derecha")},
        {QStringLiteral("bottomRight"), QT_TRANSLATE_NOOP("QObject", "Abajo a la derecha")},
        {QStringLiteral("rightDiagonal"), QT_TRANSLATE_NOOP("QObject", "Diagonal derecha")},
        {QStringLiteral("horizontal"), QT_TRANSLATE_NOOP("QObject", "Horizontal")},
        {QStringLiteral("leftDiagonal"), QT_TRANSLATE_NOOP("QObject", "Diagonal izquierda")},
        {QStringLiteral("vertical"), QT_TRANSLATE_NOOP("QObject", "Vertical")},
        {QStringLiteral("dot"), QT_TRANSLATE_NOOP("QObject", "Punto")},
        {QStringLiteral("circle"), QT_TRANSLATE_NOOP("QObject", "Círculo")},
        {QStringLiteral("line"), QT_TRANSLATE_NOOP("QObject", "Línea")},
        {QStringLiteral("regular"), QT_TRANSLATE_NOOP("QObject", "Normal")},
        {QStringLiteral("soft"), QT_TRANSLATE_NOOP("QObject", "Suave")},
        {QStringLiteral("sprinkles"), QT_TRANSLATE_NOOP("QObject", "Salpicado")},
        {QStringLiteral("clumped"), QT_TRANSLATE_NOOP("QObject", "Agrupado")},
        {QStringLiteral("contrasty"), QT_TRANSLATE_NOOP("QObject", "Contrastado")},
        {QStringLiteral("enlarged"), QT_TRANSLATE_NOOP("QObject", "Ampliado")},
        {QStringLiteral("stippled"), QT_TRANSLATE_NOOP("QObject", "Punteado")},
        {QStringLiteral("speckle"), QT_TRANSLATE_NOOP("QObject", "Moteado")},
    };
    const char* l = labels.value(v);
    return l ? QObject::tr(l) : v;
}

struct ParamSpec {
    QString key;
    enum Kind { Range, Choice, Bool } kind = Range;
    int min = 0, max = 1, def = 0;
    QStringList choices;
};

QList<ParamSpec> parseSpec(const char* text)
{
    QList<ParamSpec> out;
    for (const QString& part : QString::fromLatin1(text).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const int colon = int(part.indexOf(QLatin1Char(':')));
        ParamSpec p;
        p.key = part.left(colon);
        const QString v = part.mid(colon + 1);
        if (v == QLatin1String("bool")) {
            p.kind = ParamSpec::Bool;
        } else if (v.contains(QLatin1String(".."))) {
            static const QRegularExpression re(QStringLiteral("^(-?\\d+)\\.\\.(-?\\d+)=(-?\\d+)$"));
            const auto m = re.match(v);
            p.min = m.captured(1).toInt();
            p.max = m.captured(2).toInt();
            p.def = m.captured(3).toInt();
        } else {
            p.kind = ParamSpec::Choice;
            p.choices = v.split(QLatin1Char('|'));
        }
        out << p;
    }
    return out;
}

const QList<ParamSpec>& specOf(const char* key)
{
    static std::mutex lock;
    static std::map<std::string, QList<ParamSpec>> cache;
    std::lock_guard<std::mutex> guard(lock);
    auto it = cache.find(key);
    if (it == cache.end()) {
        for (const Filter& f : kFilters)
            if (std::string(f.key) == key)
                it = cache.emplace(key, parseSpec(f.params)).first;
    }
    return it->second;
}

struct Values {
    const ParamValues& v;
    const char* key;
    float operator()(const char* k) const { return float(dval(v, k)); }
    QString choice(const char* k) const
    {
        for (const ParamSpec& p : specOf(key))
            if (p.key == QLatin1String(k))
                return p.choices.value(ival(v, k), p.choices.value(0));
        return {};
    }
    bool flag(const char* k) const { return bval(v, k); }
};

Px colorOf(const ParamValues& v, const char* key, const QColor& fallback)
{
    QColor c = v.contains(QLatin1String(key)) ? cval(v, key) : fallback;
    if (!c.isValid())
        c = fallback;
    return {float(c.redF()), float(c.greenF()), float(c.blueF())};
}

void apply(const char* key, const ParamValues& values, Win& win, const QRect& bounds, const std::atomic_bool& cancel)
{
    const Values g{values, key};
    const int w = win.w, h = win.h;
    const Px fg = colorOf(values, "foreground", Qt::black), bg = colorOf(values, "background", Qt::white);
    const QLatin1String k(key);
    const size_t n = win.c.size();
    auto forEach = [&](auto f) {
        parallelRows(0, h, [&](int y0, int y1) {
            if (cancel)
                return;
            for (size_t i = size_t(y0) * w; i < size_t(y1) * w; ++i)
                f(i, win.xOf(i), win.yOf(i));
        });
    };
    if (k == QLatin1String("coloredPencil")) {
        const float pw = g("pencilWidth"), pressure = g("strokePressure") / 15.f;
        const Px paper = mapPx(bg, [&](float v) { return v * (0.7f + 0.3f * g("paperBrightness") / 50.f); });
        const auto l = win.lum();
        const auto e2 = gradMag(l, w, h);
        forEach([&](size_t i, int x, int y) {
            const float d = 1.f - l[i];
            const float h1 = strokes(x, y, 1, 1, 5 + pw * 3, 0.5f + pw * 0.45f, 11);
            const float h2 = strokes(x, y, 1, -1, 5 + pw * 3, 0.5f + pw * 0.45f, 12);
            const float hatch = std::max(h1, h2 * smooth(0.3f, 0.8f, d));
            const float cov = clamp01(d * (0.55f + pressure) + e2[i] * 3 - (1 - hatch) * 0.8f + 0.25f);
            win.c[i] = mixPx(paper, mapPx(win.c[i], [](float v) { return v * 0.88f; }), cov);
        });
    } else if (k == QLatin1String("cutout")) {
        blur3(win.c, w, h, 0.5f + g("edgeSimplicity") * (1.1f - 0.3f * g("edgeFidelity")));
        const float levels = std::round(g("numberOfLevels"));
        for (Px& p : win.c) {
            const float l = std::max(1e-4f, lumOf(p));
            const float kk = posterize(l, levels) / l;
            p = mapPx(p, [&](float v) { return posterize(clamp01(v * kk), levels * 2); });
        }
    } else if (k == QLatin1String("dryBrush") || k == QLatin1String("fresco")) {
        const bool fresco = k == QLatin1String("fresco");
        kuwahara(win, 1 + int(g("brushSize")));
        unsharp(win, 1.f, fresco ? 0.9f : 0.5f);
        const float levels = 3 + g("brushDetail");
        const float amp = 0.02f + (g("texture") - 1) * 0.05f;
        const std::vector<float> l = fresco ? gradMag(win.lum(), w, h) : std::vector<float>();
        forEach([&](size_t i, int x, int y) {
            const float nn = (hash01(x, y, 0, 21) - 0.5f) * amp;
            Px p = mapPx(win.c[i], [&](float v) { return posterize(v, levels) * 0.6f + v * 0.4f + nn; });
            if (fresco)
                p = mapPx(p, [&](float v) { return std::pow(clamp01((v - 0.5f) * 1.3f + 0.45f), 1.15f) * (1 - clamp01(l[i] * 2.5f) * 0.6f); });
            win.c[i] = p;
        });
    } else if (k == QLatin1String("filmGrain")) {
        const float grain = g("grain") / 20.f, area = g("highlightArea"), inten = g("intensity");
        forEach([&](size_t i, int x, int y) {
            const float l = lumOf(win.c[i]);
            const float nn = (hash01(x, y, 0, 31) - 0.5f) * grain * 0.5f * (0.3f + 2.8f * l * (1 - l));
            win.c[i] = highlightsOf(mapPx(win.c[i], [&](float v) { return v + nn; }), l, area, inten);
        });
    } else if (k == QLatin1String("neonGlow")) {
        const float size = g("glowSize"), br = g("glowBrightness") / 50.f;
        const Px glow = colorOf(values, "glowColor", QColor::fromRgbF(0, 0.62f, 1));
        const auto l = win.lum();
        std::vector<float> m = l;
        if (size < 0)
            for (float& v : m)
                v = 1 - v;
        blur1(m, w, h, std::abs(size) * 0.6f + 0.5f);
        forEach([&](size_t i, int, int) {
            const Px base = mixPx(fg, bg, l[i]);
            const float kk = clamp01(m[i] * 1.2f) * br * 2;
            for (int c = 0; c < 3; ++c)
                win.c[i][c] = 1 - (1 - base[c]) * (1 - clamp01(glow[c] * kk));
        });
    } else if (k == QLatin1String("paintDaubs")) {
        const QString ty = g.choice("brushType");
        const bool wide = ty == QLatin1String("wideSharp") || ty == QLatin1String("wideBlurry");
        kuwahara(win, std::max(1, int(g("brushSize") / 2 * (wide ? 1.5f : 1.f))));
        const float sharp = g("sharpness") / 40.f;
        if (ty == QLatin1String("wideBlurry"))
            blur3(win.c, w, h, 1.5f);
        else if (ty == QLatin1String("wideSharp"))
            unsharp(win, 1.5f, sharp * 4);
        else
            unsharp(win, 1.2f, sharp * 2.5f);
        if (ty == QLatin1String("lightRough") || ty == QLatin1String("darkRough") || ty == QLatin1String("sparkle")) {
            const std::vector<float> edges = ty == QLatin1String("sparkle") ? gradMag(win.lum(), w, h) : std::vector<float>();
            forEach([&](size_t i, int x, int y) {
                const float nn = fbm(x / 2.f, y / 2.f, 41) - 0.5f;
                if (ty == QLatin1String("lightRough"))
                    win.c[i] = mapPx(win.c[i], [&](float v) { return v + std::max(0.f, nn) * 0.35f; });
                else if (ty == QLatin1String("darkRough"))
                    win.c[i] = mapPx(win.c[i], [&](float v) { return v * (1 - std::max(0.f, nn) * 0.7f); });
                else
                    win.c[i] = mapPx(win.c[i], [&](float v) { return v + clamp01(edges[i] * 4) * smooth(0.75f, 0.95f, hash01(x, y, 0, 42)) * 0.8f; });
            });
        }
    } else if (k == QLatin1String("paletteKnife")) {
        kuwahara(win, std::max(1, int(g("strokeSize") / 3)));
        const float levels = 2 + g("strokeDetail") * 3;
        for (Px& p : win.c)
            p = mapPx(p, [&](float v) { return posterize(v, levels) * 0.5f + v * 0.5f; });
        blur3(win.c, w, h, g("softness") * 0.5f);
    } else if (k == QLatin1String("plasticWrap")) {
        const float hs = g("highlightStrength") / 20.f;
        auto l = win.lum();
        blur1(l, w, h, 0.5f + g("smoothness") * 0.5f + (15 - g("detail")) * 0.2f);
        std::vector<float> gx, gy;
        sobel(l, w, h, gx, gy);
        const auto lv = normalize3({-0.45f, -0.65f, 1.f});
        const auto hv = normalize3({lv[0], lv[1], lv[2] + 1});
        forEach([&](size_t i, int, int) {
            const float kk = 14;
            const auto nn = normalize3({-kk * gx[i], -kk * gy[i], 1});
            const float ndh = std::max(0.f, nn[0] * hv[0] + nn[1] * hv[1] + nn[2] * hv[2]);
            const float tilt = 1 - nn[2];
            const float spec = std::pow(ndh, 30.f) * smooth(0.01f, 0.12f, tilt) + smooth(0.25f, 0.6f, tilt) * 0.25f;
            win.c[i] = mapPx(win.c[i], [&](float v) { return v * 0.88f + spec * hs * 1.3f; });
        });
    } else if (k == QLatin1String("posterEdges")) {
        auto l = win.lum();
        blur1(l, w, h, 0.6f + g("edgeThickness") * 0.6f);
        const auto edge = gradMag(l, w, h);
        const float inten = g("edgeIntensity") / 10.f, levels = g("posterization") + 2;
        forEach([&](size_t i, int, int) {
            const float ink = smooth(0.015f, 0.06f, edge[i] * (1 + inten * 3)) * (0.55f + inten * 0.45f);
            win.c[i] = mapPx(win.c[i], [&](float v) { return posterize(v, levels) * (1 - ink); });
        });
    } else if (k == QLatin1String("roughPastels")) {
        const float len = g("strokeLength"), det = (g("strokeDetail") - 1) / 19.f;
        if (len >= 1) {
            Win src = win;
            blur3(src.c, w, h, 0.6f + (1 - det) * 1.2f);
            forEach([&](size_t i, int x, int y) {
                const auto [c, t] = paintStroke(src, x, y, 1, -1, 2 + len, 1.5f, 51);
                const float nn = (strokes(x, y, 1, -1, 2 + len * 0.5f, 0.8f, 52) - 0.5f) * 0.12f + (0.5f - std::abs(t - 0.5f)) * 0.06f;
                win.c[i] = mapPx(mixPx(c, win.c[i], det * 0.6f), [&](float v) { return v + nn; });
            });
        }
        texturizeBy(win, g.choice("texture"), g("scaling"), g("relief"), g.choice("light"), g.flag("invert"),
                    [](const Px& p) { return 0.15f + 0.85f * (1 - clamp01(lumOf(p))); });
    } else if (k == QLatin1String("smudgeStick")) {
        lineBlur3(win.c, w, h, 1, 1, int(g("strokeLength") * 1.5f));
        const float area = g("highlightArea"), inten = g("intensity");
        for (Px& p : win.c) {
            const float l = lumOf(p);
            p = highlightsOf(mapPx(p, [&](float v) { return v * (0.7f + 0.3f * l); }), l, area, inten);
        }
    } else if (k == QLatin1String("sponge")) {
        kuwahara(win, 1 + int(g("brushSize")));
        const float def = g("definition") / 25.f, sc = 1.5f + g("smoothness") * 0.5f;
        forEach([&](size_t i, int x, int y) {
            const float hole = smooth(0.58f, 0.75f, fbm(x / sc, y / sc, 61));
            win.c[i] = mapPx(win.c[i], [&](float v) { return clamp01((v - 0.5f) * (1 + def * 0.6f) + 0.5f) * (1 - hole * def * 0.4f); });
        });
    } else if (k == QLatin1String("underpainting")) {
        const std::vector<Px> orig = win.c;
        blur3(win.c, w, h, 0.5f + g("brushSize") * 0.4f);
        for (size_t i = 0; i < n; ++i)
            win.c[i] = saturatePx(mixPx(orig[i], win.c[i], 0.8f), 1.15f);
        const float cov = g("textureCoverage") / 40.f;
        texturize(win, g.choice("texture"), g("scaling"), g("relief") * (0.5f + cov * 1.5f), g.choice("light"), g.flag("invert"));
    } else if (k == QLatin1String("watercolor")) {
        kuwahara(win, 1 + int((15 - g("brushDetail")) / 2.5f));
        const float sh = g("shadowIntensity") / 10.f;
        const auto edge = gradMag(win.lum(), w, h);
        const float amp = (g("texture") - 1) * 0.04f;
        forEach([&](size_t i, int x, int y) {
            const float nn = (fbm(x / 3.f, y / 3.f, 71) - 0.5f) * amp;
            const float kk = 1 - clamp01(edge[i] * (3 + sh * 8)) * 0.6f;
            win.c[i] = mapPx(saturatePx(win.c[i], 1.2f), [&](float v) { return std::pow(clamp01(v * kk + nn), 1.1f + sh * 0.6f); });
        });
    } else if (k == QLatin1String("accentedEdges")) {
        auto l = win.lum();
        blur1(l, w, h, 0.4f + g("smoothness") * 0.3f);
        auto edge = gradMag(l, w, h);
        blur1(edge, w, h, g("edgeWidth") * 0.5f);
        const float eb = g("edgeBrightness") / 50.f;
        forEach([&](size_t i, int, int) {
            const float kk = clamp01(edge[i] * 6);
            if (eb >= 0.5f)
                win.c[i] = mapPx(win.c[i], [&](float v) { return v + (1 - v) * kk * (eb - 0.5f) * 2; });
            else
                win.c[i] = mapPx(win.c[i], [&](float v) { return v * (1 - kk * (0.5f - eb) * 2); });
        });
    } else if (k == QLatin1String("angledStrokes")) {
        const float len = std::max(3.f, g("strokeLength")), bal = g("directionBalance") / 100.f;
        Win src = win;
        blur3(src.c, w, h, 0.8f);
        const auto l = src.lum();
        forEach([&](size_t i, int x, int y) {
            const float kk = smooth(1 - bal - 0.05f, 1 - bal + 0.05f, l[i]);
            const Px down = paintStroke(src, x, y, 1, 1, len, 2, 83).first;
            const Px up = paintStroke(src, x, y, 1, -1, len, 2, 84).first;
            const float nn = (strokes(x, y, 1, 1, len * 0.5f, 0.7f, 85) - 0.5f) * (1 - kk) + (strokes(x, y, 1, -1, len * 0.5f, 0.7f, 86) - 0.5f) * kk;
            win.c[i] = mapPx(mixPx(down, up, kk), [&](float v) { return v + nn * 0.1f; });
        });
        unsharp(win, 1.f, 0.2f + g("sharpness") / 10.f * 2.3f);
    } else if (k == QLatin1String("crosshatch")) {
        const int r = int(g("strokeLength") / 3);
        const float st = g("strength");
        forEach([&](size_t i, int x, int y) {
            const float nn = (hash01(x, y, 0, 81) - 0.5f) * 0.12f * st;
            win.c[i] = mapPx(win.c[i], [&](float v) { return v + nn; });
        });
        std::vector<Px> a = win.c;
        lineBlur3(a, w, h, 1, 1, r);
        lineBlur3(win.c, w, h, 1, -1, r);
        for (size_t i = 0; i < n; ++i)
            win.c[i] = mixPx(a[i], win.c[i], 0.5f);
        unsharp(win, 1.f, g("sharpness") / 20.f * 3);
    } else if (k == QLatin1String("darkStrokes")) {
        const float bal = g("balance") / 10.f, bi = g("blackIntensity") / 10.f, wi = g("whiteIntensity") / 10.f;
        const auto l = win.lum();
        std::vector<Px> a = win.c;
        lineBlur3(a, w, h, 1, 1, 3);
        lineBlur3(win.c, w, h, 1, -1, 8);
        const float t = 0.25f + bal * 0.5f;
        forEach([&](size_t i, int, int) {
            const Px d = mapPx(a[i], [&](float v) { return v * (1 - bi * 0.85f); });
            const Px li = mapPx(win.c[i], [&](float v) { return v + (1 - v) * wi * 0.5f; });
            win.c[i] = mixPx(d, li, smooth(t - 0.1f, t + 0.1f, l[i]));
        });
    } else if (k == QLatin1String("inkOutlines")) {
        const float di = g("darkIntensity") / 50.f, li = g("lightIntensity") / 50.f;
        const auto l = win.lum();
        const auto edge = gradMag(l, w, h);
        lineBlur3(win.c, w, h, 1, 1, int(g("strokeLength") / 2));
        forEach([&](size_t i, int, int) {
            const float ink = clamp01(edge[i] * 5) * di * 1.5f;
            const float lt = smooth(0.5f, 1.f, l[i]) * li, dk = smooth(0.5f, 0.f, l[i]) * di * 0.8f;
            win.c[i] = mapPx(win.c[i], [&](float v) { return (v + (1 - v) * lt) * (1 - dk) * (1 - clamp01(ink)); });
        });
    } else if (k == QLatin1String("spatter")) {
        const float rad = g("sprayRadius"), feature = 1 + g("smoothness") * 0.35f;
        const float reach = std::min(rad * 1.2f / feature, rad);
        const Win src = win;
        forEach([&](size_t i, int x, int y) {
            const float fx = x / feature, fy = y / feature;
            const float dx = (fbm(fx, fy, 91) - 0.5f) * 3 * reach, dy = (fbm(fx, fy, 92) - 0.5f) * 3 * reach;
            win.c[i] = src.sample(x + 0.5f + dx, y + 0.5f + dy);
        });
    } else if (k == QLatin1String("sprayedStrokes")) {
        const float rad = g("sprayRadius"), sc = 1.2f;
        const Win src = win;
        forEach([&](size_t i, int x, int y) {
            const float fx = x / sc, fy = y / sc;
            const float dx = (vnoise(fx, fy, 91) - 0.5f) * 2 * rad, dy = (vnoise(fx, fy, 92) - 0.5f) * 2 * rad;
            win.c[i] = src.sample(x + 0.5f + dx, y + 0.5f + dy);
        });
        const auto d = direction(g.choice("strokeDirection"));
        lineBlur3(win.c, w, h, d.first, d.second, int(g("strokeLength") / 2));
    } else if (k == QLatin1String("sumiE")) {
        const float sw = g("strokeWidth"), pr = g("strokePressure") / 15.f, con = g("contrast") / 40.f;
        blur3(win.c, w, h, sw * 0.2f);
        lineBlur3(win.c, w, h, 1, 1, int(sw / 2));
        for (Px& p : win.c) {
            const float ink = std::pow(smooth(0.65f, 0.15f, lumOf(p)), 1 - pr * 0.5f);
            p = mapPx(p, [&](float v) { return clamp01((v - 0.5f) * (1 + con) + 0.5f) * (1 - ink * 0.9f * (0.5f + con)); });
        }
    } else if (k == QLatin1String("diffuseGlow")) {
        const float gr = g("graininess") / 10.f, ga = g("glowAmount") / 20.f, ca = g("clearAmount") / 20.f;
        const float thr = 0.2f + ca * 0.7f;
        forEach([&](size_t i, int x, int y) {
            const float l = lumOf(win.c[i]);
            float m = smooth(thr - 0.3f, thr + 0.1f, l) * ga * 1.4f;
            if (hash01(x, y, 0, 101) < gr * 0.3f * (m + 0.15f))
                m += 0.5f;
            win.c[i] = mixPx(win.c[i], bg, clamp01(m));
        });
    } else if (k == QLatin1String("glass")) {
        const float dist = g("distortion"), step = 0.5f + g("smoothness") * 0.5f;
        const QString kind = g.choice("texture");
        const float s = std::clamp(g("scaling") / 100.f, 0.5f, 2.f), sign = g.flag("invert") ? -1.f : 1.f, mx = dist * 2;
        const Win src = win;
        forEach([&](size_t i, int x, int y) {
            const float fx = float(x), fy = float(y);
            const float gx = texture(kind, fx + step, fy, s) - texture(kind, fx - step, fy, s);
            const float gy = texture(kind, fx, fy + step, s) - texture(kind, fx, fy - step, s);
            const float dx = std::clamp(gx * sign * dist * 3, -mx, mx), dy = std::clamp(gy * sign * dist * 3, -mx, mx);
            win.c[i] = src.sample(fx + 0.5f + dx, fy + 0.5f + dy);
        });
    } else if (k == QLatin1String("oceanRipple")) {
        const float s = 2 + g("rippleSize") * 1.2f, mag = g("rippleMagnitude") * 0.9f;
        const Win src = win;
        forEach([&](size_t i, int x, int y) {
            const float fx = x / s, fy = y / s;
            const float dx = (vnoise(fx, fy, 111) - 0.5f) * 2 * mag, dy = (vnoise(fx, fy, 112) - 0.5f) * 2 * mag;
            win.c[i] = src.sample(x + 0.5f + dx, y + 0.5f + dy);
        });
    } else if (k == QLatin1String("basRelief")) {
        auto l = win.lum();
        const auto orig = l;
        blur1(l, w, h, 0.3f + g("smoothness") * 0.5f + (15 - g("detail")) * 0.12f);
        std::vector<float> gx, gy;
        sobel(l, w, h, gx, gy);
        const auto lt = lightDir(g.choice("light"));
        const float kk = 3 + g("detail") * 0.4f;
        forEach([&](size_t i, int, int) {
            win.c[i] = mixPx(fg, bg, clamp01(orig[i] * 0.5f + 0.25f + kk * -(gx[i] * lt.first + gy[i] * lt.second)));
        });
    } else if (k == QLatin1String("chalkCharcoal")) {
        const float ca = g("charcoalArea") / 20.f, ch = g("chalkArea") / 20.f, pr = g("strokePressure") / 5.f;
        const Px mid = mixPx(fg, bg, 0.5f);
        forEach([&](size_t i, int x, int y) {
            const float l = lumOf(win.c[i]);
            const float cn = strokes(x, y, 1, 1, 9, 0.9f, 121) - 0.5f, kn = strokes(x, y, 1, -1, 9, 0.9f, 122) - 0.5f;
            const float cc = smooth(0.25f, 0.6f, (1 - l) * (0.4f + ca * 1.2f) + cn * (0.6f + pr * 0.4f) - 0.15f);
            const float kc = smooth(0.25f, 0.6f, l * (0.4f + ch * 1.2f) + kn * (0.6f + pr * 0.4f) - 0.15f);
            win.c[i] = mixPx(mixPx(mid, fg, cc), bg, kc * (1 - cc));
        });
    } else if (k == QLatin1String("charcoal")) {
        const float th = g("charcoalThickness"), det = g("detail") / 5.f, bal = g("lightDarkBalance") / 100.f;
        auto l = win.lum();
        blur1(l, w, h, 1.f);
        const auto edge = gradMag(l, w, h);
        forEach([&](size_t i, int x, int y) {
            const float sn = strokes(x, y, 1, 1, 8 + th * 3, 0.6f + th * 0.5f, 131) - 0.5f;
            const float cov = smooth(1 - bal - 0.2f, 1 - bal + 0.2f, 1 - l[i] + sn * 0.6f) + clamp01(edge[i] * det * 6);
            win.c[i] = mixPx(bg, fg, clamp01(cov));
        });
    } else if (k == QLatin1String("chrome")) {
        const float det = g("detail") / 10.f, sm = g("smoothness") / 10.f;
        auto l = win.lum();
        blur1(l, w, h, 1 + sm * 4);
        const auto edge = gradMag(l, w, h);
        forEach([&](size_t i, int, int) {
            const float v = 0.5f + 0.5f * std::sin((l[i] * (2.5f + det * 3) + edge[i] * 6) * 2 * kPi);
            win.c[i] = grey(smooth(0.05f, 0.95f, v));
        });
    } else if (k == QLatin1String("conteCrayon")) {
        const float fl = g("foregroundLevel") / 15.f, bl = g("backgroundLevel") / 15.f;
        const Px mid = mixPx(fg, bg, 0.5f);
        forEach([&](size_t i, int x, int y) {
            const float l = lumOf(win.c[i]);
            const float sn = strokes(x, y, 1, -1, 7, 1, 141) - 0.5f;
            const float cf = clamp01((0.6f - l) * 2 * fl * 1.4f + sn * 0.5f);
            const float cb = clamp01((l - 0.4f) * 2 * bl * 1.4f - sn * 0.5f);
            win.c[i] = mixPx(mixPx(mid, fg, cf), bg, cb * (1 - cf));
        });
        texturize(win, g.choice("texture"), g("scaling"), g("relief"), g.choice("light"), g.flag("invert"));
    } else if (k == QLatin1String("graphicPen")) {
        const int r = int(g("strokeLength") / 2);
        const float bal = g("lightDarkBalance") / 100.f;
        const auto l = win.lum();
        std::vector<float> nn(n);
        for (size_t i = 0; i < n; ++i)
            nn[i] = hash01(win.xOf(i), win.yOf(i), 0, 151);
        const auto d = direction(g.choice("strokeDirection"));
        lineBlur(nn.data(), 1, w, h, d.first, d.second, r);
        const float sd = 0.2887f / std::sqrt(float(2 * r + 1));
        forEach([&](size_t i, int, int) {
            const float t = l[i] + (nn[i] - 0.5f) / sd * 0.16f;
            win.c[i] = mixPx(bg, fg, smooth(bal + 0.04f, bal - 0.04f, t));
        });
    } else if (k == QLatin1String("halftonePattern")) {
        const float cell = 1.5f + 2 * std::clamp(g("size"), 1.f, 12.f);
        const float gain = 0.5f + g("contrast") * 0.6f;
        const auto offsets = halftoneOffsets(gain);
        auto l = win.lum();
        blur1(l, w, h, cell * 0.15f);
        const float cx = (bounds.left() + bounds.right() + 1) / 2.f, cy = (bounds.top() + bounds.bottom() + 1) / 2.f;
        const QString ty = g.choice("patternType");
        const auto& dots = halftoneDotRanks();
        forEach([&](size_t i, int x, int y) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            auto band = [&](float t) { return std::abs(remEuclid(t / cell, 1.f) - 0.5f) * 2; };
            float spot;
            if (ty == QLatin1String("circle")) {
                spot = band(std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)));
            } else if (ty == QLatin1String("line")) {
                spot = band(fy);
            } else {
                const float rx = (fx + fy) * 0.70710678f, ry = (fy - fx) * 0.70710678f;
                const float u = remEuclid(rx / cell, 1.f), v = remEuclid(ry / cell, 1.f);
                const float f = (std::cos(2 * kPi * u) + std::cos(2 * kPi * v)) / 2;
                spot = float(std::lower_bound(dots.begin(), dots.end(), f) - dots.begin()) / float(std::max<size_t>(1, dots.size()));
            }
            const float tone = clamp01(1 - l[i]) * float(offsets.size() - 1);
            const size_t kk = size_t(std::floor(tone));
            const float fr = tone - std::floor(tone);
            auto at = [&](size_t j) { return offsets[std::min(j, offsets.size() - 1)]; };
            const float level = at(kk) + (at(kk + 1) - at(kk)) * fr;
            win.c[i] = mixPx(bg, fg, clamp01(0.5f + (level - spot) * gain));
        });
    } else if (k == QLatin1String("notePaper")) {
        const float bal = g("imageBalance") / 50.f, gr = g("graininess") / 20.f, rel = g("relief") / 25.f;
        auto l = win.lum();
        blur1(l, w, h, 1.f);
        std::vector<float> m(n);
        for (size_t i = 0; i < n; ++i)
            m[i] = smooth(bal - 0.05f, bal + 0.05f, l[i]);
        blur1(m, w, h, 1.2f);
        std::vector<float> gx, gy;
        sobel(m, w, h, gx, gy);
        forEach([&](size_t i, int x, int y) {
            const float nn = (hash01(x, y, 0, 161) - 0.5f) * gr * 0.3f;
            const float f = std::clamp(shadeOf(gx[i], gy[i], lightDir(QStringLiteral("topLeft")), rel * 6), 0.f, 2.f);
            win.c[i] = mapPx(mixPx(fg, bg, m[i]), [&](float v) { return (v + nn) * f; });
        });
    } else if (k == QLatin1String("photocopy")) {
        const float det = g("detail"), dk = g("darkness") / 50.f;
        const auto l = win.lum();
        auto b = l;
        blur1(b, w, h, 1 + det * 0.5f);
        forEach([&](size_t i, int, int) {
            const float hp = l[i] - b[i];
            const float ink = smooth(0.f, 0.06f, -hp * (1 + dk * 8)) + smooth(0.35f, 0.05f, l[i]) * dk * 1.5f;
            win.c[i] = mixPx(bg, fg, clamp01(ink));
        });
    } else if (k == QLatin1String("plaster")) {
        const float bal = g("imageBalance") / 50.f;
        auto l = win.lum();
        blur1(l, w, h, 0.5f + g("smoothness") * 0.8f);
        std::vector<float> m(n);
        for (size_t i = 0; i < n; ++i)
            m[i] = smooth(bal + 0.1f, bal - 0.1f, l[i]);
        blur1(m, w, h, 1.5f);
        std::vector<float> gx, gy;
        sobel(m, w, h, gx, gy);
        const auto lt = lightDir(g.choice("light"));
        forEach([&](size_t i, int, int) {
            const float f = std::clamp(shadeOf(gx[i], gy[i], lt, 6), 0.f, 2.f);
            win.c[i] = mapPx(mixPx(bg, fg, m[i] * 0.6f), [&](float v) { return v * f; });
        });
    } else if (k == QLatin1String("reticulation")) {
        const float den = g("density") / 50.f, fl = g("foregroundLevel") / 50.f, bl = g("backgroundLevel") / 50.f;
        const Px ink = mixPx(bg, fg, 0.3f + fl * 0.7f), paper = mixPx(bg, fg, bl * 0.6f);
        forEach([&](size_t i, int x, int y) {
            const float nn = 0.5f * hash01(x, y, 0, 171) + 0.5f * vnoise(x / 1.8f, y / 1.8f, 172);
            const float t = (1 - lumOf(win.c[i])) + (nn - 0.5f) * (0.4f + den * 1.2f);
            win.c[i] = mixPx(paper, ink, smooth(0.45f, 0.55f, t));
        });
    } else if (k == QLatin1String("stamp")) {
        const float bal = g("lightDarkBalance") / 50.f;
        auto l = win.lum();
        blur1(l, w, h, 0.3f + g("smoothness") * 0.3f);
        forEach([&](size_t i, int, int) { win.c[i] = mixPx(bg, fg, smooth(bal + 0.02f, bal - 0.02f, l[i])); });
    } else if (k == QLatin1String("tornEdges")) {
        const float bal = g("imageBalance") / 50.f, sm = g("smoothness"), con = g("contrast");
        auto l = win.lum();
        blur1(l, w, h, 1.f);
        const float rough = (16 - sm) / 15.f * 0.35f, aa = 0.25f / con;
        forEach([&](size_t i, int x, int y) {
            const float t = l[i] + (fbm(x / 2.f, y / 2.f, 181) - 0.5f) * rough;
            win.c[i] = mixPx(bg, fg, smooth(bal + aa, bal - aa, t));
        });
    } else if (k == QLatin1String("waterPaper")) {
        const float fl = g("fiberLength"), br = g("brightness") / 100.f, co = g("contrast") / 100.f;
        const int r = int(fl / 6);
        lineBlur3(win.c, w, h, 1, 0, r);
        lineBlur3(win.c, w, h, 0, 1, r);
        forEach([&](size_t i, int x, int y) {
            const float fib = std::max(strokes(x, y, 1, 0, fl, 1, 191), strokes(x, y, 0, 1, fl, 1, 192));
            win.c[i] = mapPx(win.c[i], [&](float v) { return (v * (0.85f + 0.25f * fib) - 0.5f) * (co * 1.6f + 0.2f) + 0.5f + (br - 0.5f) * 0.8f; });
        });
    } else if (k == QLatin1String("glowingEdges")) {
        blur3(win.c, w, h, 0.3f + g("smoothness") * 0.25f);
        std::vector<Px> e3(n);
        for (int c = 0; c < 3; ++c) {
            std::vector<float> plane(n);
            for (size_t i = 0; i < n; ++i)
                plane[i] = win.c[i][size_t(c)];
            const auto gm = gradMag(plane, w, h);
            for (size_t i = 0; i < n; ++i)
                e3[i][size_t(c)] = gm[i];
        }
        blur3(e3, w, h, g("edgeWidth") * 0.4f);
        const float kk = 2 + g("edgeBrightness") * 1.2f;
        for (size_t i = 0; i < n; ++i)
            win.c[i] = mapPx(e3[i], [&](float v) { return clamp01(v * kk); });
    } else if (k == QLatin1String("craquelure")) {
        const float sp = g("crackSpacing"), dep = g("crackDepth") / 10.f, bri = g("crackBrightness") / 10.f;
        const float cell = sp * 1.3f + 1, width = 0.8f + dep * 0.8f;
        auto crack = [&](float x, float y) {
            const Voronoi v = voronoi(x, y, cell, 201);
            return 1 - smooth(0.f, width, v.d2 - v.d1);
        };
        forEach([&](size_t i, int x, int y) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float c0 = crack(fx, fy);
            const float gx = crack(fx + 1, fy) - c0, gy = crack(fx, fy + 1) - c0;
            const float f = std::clamp(shadeOf(-gx, -gy, lightDir(QStringLiteral("topLeft")), dep * 2), 0.f, 2.f);
            win.c[i] = mapPx(win.c[i], [&](float v) { return v * (0.55f + bri * 0.5f) * (1 - c0 * dep * 0.6f) * f; });
        });
    } else if (k == QLatin1String("grain")) {
        const float it = g("intensity") / 100.f, co = g("contrast") / 100.f;
        const QString ty = g.choice("grainType");
        forEach([&](size_t i, int x, int y) {
            auto hc = [&](uint32_t c) { return hash01(x, y, c, 211) - 0.5f; };
            const Px p = win.c[i];
            Px q;
            if (ty == QLatin1String("soft"))
                q = {p[0] + hc(0) * it * 0.4f, p[1] + hc(1) * it * 0.4f, p[2] + hc(2) * it * 0.4f};
            else if (ty == QLatin1String("sprinkles"))
                q = hash01(x, y, 3, 211) < it * 0.15f ? bg : p;
            else if (ty == QLatin1String("clumped"))
                q = mapPx(p, [&](float v) { return v + (vnoise(x / 2.f, y / 2.f, 212) - 0.5f) * it * 1.2f; });
            else if (ty == QLatin1String("contrasty"))
                q = mapPx(p, [&](float v) { return (v - 0.5f) * (1 + it * 2) + 0.5f + hc(4) * it * 0.5f; });
            else if (ty == QLatin1String("enlarged")) {
                const float fx = x / 3.f, fy = y / 3.f;
                q = {p[0] + (vnoise(fx, fy, 213) - 0.5f) * it, p[1] + (vnoise(fx, fy, 214) - 0.5f) * it, p[2] + (vnoise(fx, fy, 215) - 0.5f) * it};
            } else if (ty == QLatin1String("stippled"))
                q = mixPx(p, lumOf(p) > hash01(x, y, 5, 211) ? bg : fg, 0.3f + it * 0.7f);
            else if (ty == QLatin1String("horizontal"))
                q = mapPx(p, [&](float v) { return v + (vnoise(x / 25.f, float(y), 216) - 0.5f) * it; });
            else if (ty == QLatin1String("vertical"))
                q = mapPx(p, [&](float v) { return v + (vnoise(float(x), y / 25.f, 217) - 0.5f) * it; });
            else if (ty == QLatin1String("speckle"))
                q = hash01(x, y, 6, 211) < it * 0.1f ? mixPx(p, fg, 0.85f) : p;
            else
                q = {p[0] + hc(0) * it * 0.8f, p[1] + hc(1) * it * 0.8f, p[2] + hc(2) * it * 0.8f};
            win.c[i] = mapPx(q, [&](float v) { return (v - 0.5f) * (0.5f + co) + 0.5f; });
        });
    } else if (k == QLatin1String("mosaicTiles")) {
        const float ts = g("tileSize"), gw = g("groutWidth"), lg = g("lightenGrout") / 10.f;
        auto edge = [&](float x, float y) {
            const float jx = x + (vnoise(x / ts * 0.7f, y / ts * 0.7f, 221) - 0.5f) * ts * 0.3f;
            const float jy = y + (vnoise(x / ts * 0.7f, y / ts * 0.7f, 222) - 0.5f) * ts * 0.3f;
            const float u = remEuclid(jx, ts), v = remEuclid(jy, ts);
            return std::min({u, ts - u, v, ts - v});
        };
        forEach([&](size_t i, int x, int y) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float grout = smooth(gw * 0.5f + 0.7f, gw * 0.5f - 0.3f, edge(fx, fy));
            auto hgt = [&](float xx, float yy) { return smooth(gw * 0.5f, gw * 0.5f + 2.5f, edge(xx, yy)); };
            const float s = (hgt(fx - 1, fy - 1) - hgt(fx + 1, fy + 1)) * 0.35f;
            const Px gc = grey(clamp01(lumOf(win.c[i]) * 0.5f + lg * 0.45f));
            win.c[i] = mixPx(mapPx(win.c[i], [&](float v) { return v * (1 + s); }), gc, grout);
        });
    } else if (k == QLatin1String("patchwork")) {
        const float size = 3 + g("squareSize") * 1.5f, rel = g("relief") / 25.f;
        const Win src = win;
        auto hv = [&](float x, float y) {
            const int bx = int(std::floor(x / size)), by = int(std::floor(y / size));
            const float u = remEuclid(x, size), v = remEuclid(y, size);
            return hash01(bx, by, 0, 231) * smooth(0.f, 1.5f, std::min({u, size - u, v, size - v}));
        };
        forEach([&](size_t i, int x, int y) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float bx = std::floor(fx / size), by = std::floor(fy / size);
            const Px c = src.at(int((bx + 0.5f) * size), int((by + 0.5f) * size));
            const float s = (hv(fx - 1, fy - 1) - hv(fx + 1, fy + 1)) * rel * 1.5f;
            win.c[i] = mapPx(c, [&](float v) { return v * (1 + s); });
        });
    } else if (k == QLatin1String("stainedGlass")) {
        const float cell = g("cellSize") * 1.6f + 1, bt = g("borderThickness"), li = g("lightIntensity") / 10.f;
        const float cx = (bounds.left() + bounds.right() + 1) / 2.f, cy = (bounds.top() + bounds.bottom() + 1) / 2.f;
        const float half = std::max(1.f, std::max(bounds.width(), bounds.height()) / 2.f);
        const Win src = win;
        forEach([&](size_t i, int x, int y) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const Voronoi v = voronoi(fx, fy, cell, 241);
            const float border = smooth(bt * 0.5f + 0.6f, bt * 0.5f - 0.4f, (v.d2 - v.d1) * 0.5f);
            const float dn = std::min(1.f, std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) / half);
            const Px col = mapPx(src.at(int(v.p1.first), int(v.p1.second)), [&](float c) { return c * (1 + li * 0.6f * (1 - dn)); });
            win.c[i] = mixPx(col, fg, border);
        });
    } else if (k == QLatin1String("texturizer")) {
        texturize(win, g.choice("texture"), g("scaling"), g("relief"), g.choice("light"), g.flag("invert"));
    }
}

QString categoryName(int c)
{
    switch (c) {
    case 0: return catArtistic();
    case 1: return catBrushStrokes();
    case 2: return catDistort();
    case 3: return catSketch();
    case 4: return catStylize();
    case 5: return catTexture();
    case 6: return catPhoto();
    default: return catNoise();
    }
}

} // namespace

void registerPhotoCraft2(QList<const Effect*>& list)
{
    using P = EffectParam;
    for (const Filter& f : kFilters) {
        const QString key = QString::fromLatin1(f.key);
        QList<P> params;
        for (const ParamSpec& s : specOf(f.key)) {
            if (s.kind == ParamSpec::Range) {
                params << P::integer(s.key, paramLabel(s.key), s.min, s.max, s.def);
            } else if (s.kind == ParamSpec::Bool) {
                params << P::boolean(s.key, paramLabel(s.key), false);
            } else {
                QStringList labels;
                for (const QString& c : s.choices)
                    labels << choiceLabel(c);
                params << P::choice(s.key, paramLabel(s.key), labels, 0);
            }
        }
        if (usesForeground(key))
            params << P::color(QStringLiteral("foreground"), QObject::tr("Primer plano"), P::ColorDefault::Primary);
        if (usesBackground(key))
            params << P::color(QStringLiteral("background"), key == QLatin1String("coloredPencil") ? QObject::tr("Papel") : QObject::tr("Fondo"),
                               P::ColorDefault::Secondary);
        if (key == QLatin1String("neonGlow")) {
            P glow = P::color(QStringLiteral("glowColor"), QObject::tr("Color del resplandor"), P::ColorDefault::Primary);
            glow.defaultValue = QColor(0, 158, 255);
            params << glow;
        }
        const char* fkey = f.key;
        SimpleEffect::Def def{QStringLiteral("pc") + key.left(1).toUpper() + key.mid(1), QObject::tr(f.name), categoryName(f.category),
                              "draw-brush", {}, params,
                              whole([fkey](FBuf& b, const RenderArgs& a) {
                                  // La capa entera como ventana; la zona marca dónde se anclan las
                                  // texturas (y lo que se escribe).
                                  Win win;
                                  win.w = b.w;
                                  win.h = b.h;
                                  win.ox = -a.roi.left();
                                  win.oy = -a.roi.top();
                                  win.c.resize(size_t(b.w) * b.h);
                                  for (size_t i = 0; i < win.c.size(); ++i)
                                      win.c[i] = {b.d[i * 4], b.d[i * 4 + 1], b.d[i * 4 + 2]};
                                  const QRect bounds(0, 0, a.roi.width(), a.roi.height());
                                  apply(fkey, a.values, win, bounds, a.cancel);
                                  for (size_t i = 0; i < win.c.size(); ++i)
                                      for (int c = 0; c < 3; ++c)
                                          b.d[i * 4 + c] = clamp01(win.c[i][size_t(c)]);
                              })};
        def.origin = QStringLiteral("PhotoCraft");
        def.straight = true;
        list.append(new SimpleEffect(std::move(def)));
    }
}

} // namespace fx
