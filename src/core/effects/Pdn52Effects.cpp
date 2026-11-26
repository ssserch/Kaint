// Los efectos nuevos de Paint.NET 5.2: Linograbado, Mosaico, Puntillismo y Vidriera (Artístico),
// Tramado ordenado (Color), Esferizar y Ondas (Distorsionar) y Semitono (Estilizado). Paint.NET no
// publica su código: están hechos a partir de cómo se ven y de sus opciones (con los mismos valores
// por defecto), con algoritmos propios.

#include "core/effects/EffectUtils.h"

#include <QHash>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fx {

namespace {

// --- Utilidades ------------------------------------------------------------------

// El color directo (sin premultiplicar) en un punto, bilineal y con el borde recortado.
inline QRgb straightAt(const ConstPixels& in, double x, double y)
{
    return qUnpremultiply(sampleBilinear(in, float(x + 0.5), float(y + 0.5)));
}

// Media de cinco muestras alrededor de un punto (el centro de una celda de lado 'cell').
QRgb cellColor(const ConstPixels& in, double x, double y, double cell)
{
    const double d = cell * 0.25;
    QRgb s[5] = {sampleBilinear(in, float(x + 0.5), float(y + 0.5)), sampleBilinear(in, float(x - d + 0.5), float(y - d + 0.5)),
                 sampleBilinear(in, float(x + d + 0.5), float(y - d + 0.5)), sampleBilinear(in, float(x - d + 0.5), float(y + d + 0.5)),
                 sampleBilinear(in, float(x + d + 0.5), float(y + d + 0.5))};
    return qUnpremultiply(average(s, 5));
}

inline double lerpD(double a, double b, double t) { return a + (b - a) * t; }
inline double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Ruido de valor suave en [-1, 1] (una rejilla de valores al azar, interpolada).
double valueNoise(double x, double y, uint32_t seed)
{
    const double fx = std::floor(x), fy = std::floor(y);
    const int ix = int(fx), iy = int(fy);
    double tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    auto h = [seed](int i, int j) { return hashUnit(uint32_t(i), uint32_t(j), seed) * 2.0 - 1.0; };
    return lerpD(lerpD(h(ix, iy), h(ix + 1, iy), tx), lerpD(h(ix, iy + 1), h(ix + 1, iy + 1), tx), ty);
}

// Un color directo con 'shade' en [-1, 1]: hacia el blanco (positivo) o el negro (negativo).
inline QRgb shaded(QRgb c, double shade)
{
    auto ch = [shade](int v) {
        return clamp255(int(std::lround(shade >= 0 ? v + (255 - v) * shade : v * (1 + shade))));
    };
    return qRgba(ch(qRed(c)), ch(qGreen(c)), ch(qBlue(c)), qAlpha(c));
}

inline QRgb mixStraight(QRgb a, QRgb b, double t)
{
    auto l = [t](int x, int y) { return clamp255(int(std::lround(x + (y - x) * t))); };
    return qRgba(l(qRed(a), qRed(b)), l(qGreen(a), qGreen(b)), l(qBlue(a), qBlue(b)), l(qAlpha(a), qAlpha(b)));
}

// Transformada de distancia euclídea exacta (Felzenszwalb y Huttenlocher): la distancia de cada
// píxel al más cercano de los marcados en 'seeds' (sin marcados, muy grande).
std::vector<float> distanceTransform(const std::vector<uint8_t>& seeds, int w, int h)
{
    constexpr float kInf = 1e20f;
    std::vector<float> f(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < f.size(); ++i)
        f[i] = seeds[i] ? 0.f : kInf;
    auto pass = [](const float* in, float* out, int n, std::vector<int>& v, std::vector<float>& z) {
        v.resize(size_t(n));
        z.resize(size_t(n) + 1);
        int k = 0;
        v[0] = 0;
        z[0] = -kInf;
        z[1] = kInf;
        for (int q = 1; q < n; ++q) {
            float s;
            while (true) {
                const int p = v[size_t(k)];
                s = ((in[q] + float(q) * q) - (in[p] + float(p) * p)) / (2.f * float(q - p));
                if (s <= z[size_t(k)] && k > 0)
                    --k;
                else
                    break;
            }
            if (s <= z[size_t(k)]) {
                v[0] = q;
                z[0] = -kInf;
                z[1] = kInf;
                k = 0;
                continue;
            }
            ++k;
            v[size_t(k)] = q;
            z[size_t(k)] = s;
            z[size_t(k) + 1] = kInf;
        }
        k = 0;
        for (int q = 0; q < n; ++q) {
            while (z[size_t(k) + 1] < float(q))
                ++k;
            const int p = v[size_t(k)];
            out[q] = float(q - p) * float(q - p) + in[p];
        }
    };
    parallelRows(0, w, [&](int x0, int x1) {
        std::vector<int> v;
        std::vector<float> z;
        std::vector<float> col(static_cast<size_t>(h)), res(static_cast<size_t>(h));
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < h; ++y)
                col[size_t(y)] = f[size_t(y) * w + x];
            pass(col.data(), res.data(), h, v, z);
            for (int y = 0; y < h; ++y)
                f[size_t(y) * w + x] = res[size_t(y)];
        }
    });
    parallelRows(0, h, [&](int y0, int y1) {
        std::vector<int> v;
        std::vector<float> z;
        std::vector<float> res(static_cast<size_t>(w));
        for (int y = y0; y < y1; ++y) {
            float* row = f.data() + size_t(y) * w;
            pass(row, res.data(), w, v, z);
            for (int x = 0; x < w; ++x)
                row[x] = std::sqrt(res[size_t(x)]);
        }
    });
    return f;
}

// --- Distorsionar > Esferizar --------------------------------------------------------

void renderSpherize(const RenderArgs& a)
{
    // Como mirar a través de una bola de cristal: dentro del círculo (el radio, en % de la mitad
    // del lado menor) la imagen se curva sobre una esfera; con cantidad negativa, hacia dentro.
    const double amount = std::clamp(dval(a.values, "amount") / 100.0, -1.0, 1.0);
    const double radius = std::max(1e-3, dval(a.values, "radius") / 100.0 * defaultRadius(a.roi));
    WarpSetup setup;
    setup.offset = pval(a.values, "offset");
    setup.quality = ival(a.values, "quality");
    setup.edge = edgeValue(a.values, pdnEdges());
    const double k = std::abs(amount);
    warp(a, setup, [&](double u, double v) {
        const double r = std::hypot(u, v);
        if (r >= radius || r <= 1e-9 || k == 0)
            return QPointF(u, v);
        const double d = r / radius;
        const double bent = amount > 0 ? std::asin(d) / (M_PI / 2) : std::sin(d * M_PI / 2);
        const double s = (d * (1 - k) + bent * k) / d;
        return QPointF(u * s, v * s);
    });
}

// --- Distorsionar > Ondas --------------------------------------------------------------

// La forma de la onda en 't' (en vueltas), entre -1 y 1; todas empiezan en 0 subiendo, como el seno.
double waveShape(int form, double t)
{
    const double f = t - std::floor(t);
    switch (form) {
    case 1: return 1.0 - 4.0 * std::abs(std::fmod(f + 0.25, 1.0) - 0.5); // triangular
    case 2: return 2.0 * std::fmod(f + 0.5, 1.0) - 1.0;                  // diente de sierra
    case 3: return f < 0.5 ? 1.0 : -1.0;                                  // cuadrada
    case 4: {                                                             // semicírculo
        const double s = (f < 0.5 ? f : f - 0.5) * 4.0 - 1.0;
        const double arch = std::sqrt(std::max(0.0, 1.0 - s * s));
        return f < 0.5 ? arch : -arch;
    }
    case 5: {                                                             // armónicos
        const double x = 2 * M_PI * f;
        return 0.6 * std::sin(x) + 0.3 * std::sin(2 * x) + 0.1 * std::sin(3 * x);
    }
    case 6: return std::round(std::sin(2 * M_PI * f) * 3.0) / 3.0;       // senoidal escalonada
    default: return std::sin(2 * M_PI * f);
    }
}

// Los patrones que usan el centro, la amortiguación y los lóbulos (los demás los dejan en gris).
const QVariantList& wavesCentered() { static const QVariantList l{1, 3, 4, 5, 7, 8}; return l; }
const QVariantList& wavesDamped() { static const QVariantList l{1, 3, 4, 5, 7, 8, 9}; return l; }
const QVariantList& wavesLobed() { static const QVariantList l{7, 8}; return l; }

void renderWaves(const RenderArgs& a)
{
    // Amplitud y longitud de onda, en % del lado menor (como en Paint.NET); el ángulo gira el
    // patrón y la fase lo desplaza.
    const int pattern = ival(a.values, "pattern");
    const int form = ival(a.values, "form");
    const double minDim = std::max(1, std::min(a.roi.width(), a.roi.height()));
    const double amp = dval(a.values, "amplitude") / 100.0 * minDim;
    const double wl = std::max(1.0, dval(a.values, "wavelength") / 100.0 * minDim);
    const int lobes = std::max(1, ival(a.values, "lobes"));
    const double damping = dval(a.values, "damping") / 100.0;
    const double th = dval(a.values, "angle") * M_PI / 180.0;
    const double phase = dval(a.values, "phase") / 360.0;
    const double c = std::cos(th), s = std::sin(th);
    const double hw = a.roi.width() / 2.0, hh = a.roi.height() / 2.0;
    WarpSetup setup;
    setup.offset = wavesCentered().contains(pattern) ? pval(a.values, "offset") : QPointF();
    setup.quality = ival(a.values, "quality");
    setup.edge = edgeValue(a.values, pdnEdges());
    auto att = [&](double d) { return damping > 0 ? std::exp(-damping * 4.0 * d / (minDim / 2)) : 1.0; };
    auto sign = [](double v) { return v < 0 ? -1.0 : 1.0; };
    warp(a, setup, [&](double u, double v) -> QPointF {
        switch (pattern) {
        case 2: { // Compresión lineal: a lo largo de la dirección
            const double w = amp * waveShape(form, (u * c + v * s) / wl + phase);
            return {u + c * w, v + s * w};
        }
        case 1:
        case 3: { // Radial (hacia fuera) y tangencial (girando)
            const double r = std::hypot(u, v);
            if (r < 1e-9)
                return {u, v};
            const double w = amp * waveShape(form, r / wl + phase) * att(r) * std::min(1.0, r / wl);
            const double nx = u / r, ny = v / r;
            return pattern == 1 ? QPointF(u + nx * w, v + ny * w) : QPointF(u - ny * w, v + nx * w);
        }
        case 4:
        case 5: { // Anillos cuadrados y de rombo
            const double p = u * c + v * s, q = -u * s + v * c;
            double m, gp, gq;
            if (pattern == 4) {
                if (std::abs(p) >= std::abs(q)) {
                    m = std::abs(p), gp = sign(p), gq = 0;
                } else {
                    m = std::abs(q), gp = 0, gq = sign(q);
                }
            } else {
                m = std::abs(p) + std::abs(q), gp = sign(p) * M_SQRT1_2, gq = sign(q) * M_SQRT1_2;
            }
            const double w = amp * waveShape(form, m / wl + phase) * att(m) * std::min(1.0, m / wl);
            const double dp = gp * w, dq = gq * w;
            return {u + dp * c - dq * s, v + dp * s + dq * c};
        }
        case 6: { // Cruzado: una onda en cada eje
            const double p = u * c + v * s, q = -u * s + v * c;
            const double w1 = amp * waveShape(form, p / wl + phase);
            const double w2 = amp * waveShape(form, q / wl + phase);
            return {u + w2 * c - w1 * s, v + w2 * s + w1 * c};
        }
        case 7:
        case 8: { // Angular (lóbulos alrededor del centro) y espiral (brazos)
            const double r = std::hypot(u, v);
            if (r < 1e-9)
                return {u, v};
            const double ang = std::atan2(v, u) - th;
            const double t = lobes * ang / (2 * M_PI) + phase + (pattern == 8 ? r / wl : 0.0);
            const double w = amp * waveShape(form, t) * att(r) * std::min(1.0, r / wl);
            return {u + u / r * w, v + v / r * w};
        }
        case 9: { // Desde los bordes: paralelas a cada borde
            const double x = u + hw, y = v + hh;
            double m = x, nx = 1, ny = 0;
            if (2 * hw - x < m)
                m = 2 * hw - x, nx = -1, ny = 0;
            if (y < m)
                m = y, nx = 0, ny = 1;
            if (2 * hh - y < m)
                m = 2 * hh - y, nx = 0, ny = -1;
            const double w = amp * waveShape(form, std::max(0.0, m) / wl + phase) * att(std::max(0.0, m));
            return {u + nx * w, v + ny * w};
        }
        default: { // Lineal: la onda va a lo largo de la dirección y desplaza de lado
            const double w = amp * waveShape(form, (u * c + v * s) / wl + phase);
            return {u - s * w, v + c * w};
        }
        }
    });
}

// --- Color > Tramado ordenado ----------------------------------------------------------

// Umbrales (0..1) de cada matriz a partir de su orden: el puesto de cada casilla, centrado.
std::vector<float> thresholdsFromRanks(const std::vector<int>& rank)
{
    std::vector<float> t(rank.size());
    for (size_t i = 0; i < rank.size(); ++i)
        t[i] = (float(rank[i]) + 0.5f) / float(rank.size());
    return t;
}

std::vector<int> bayerRanks(int n)
{
    std::vector<int> m{0};
    int s = 1;
    while (s < n) {
        std::vector<int> next(static_cast<size_t>(4 * s * s));
        for (int y = 0; y < 2 * s; ++y)
            for (int x = 0; x < 2 * s; ++x) {
                static const int quad[2][2] = {{0, 2}, {3, 1}};
                next[size_t(y * 2 * s + x)] = m[size_t((y % s) * s + x % s)] * 4 + quad[y / s][x / s];
            }
        m = std::move(next);
        s *= 2;
    }
    return m;
}

// Puntos agrupados que crecen en espiral desde uno ('centers' 1) o dos centros (en diagonal).
std::vector<int> spiralRanks(int n, int centers)
{
    struct Cell {
        int index;
        int cluster;
        double dist;
        double angle;
    };
    std::vector<Cell> cells;
    const double half = n / 2.0;
    const QPointF c1 = centers == 1 ? QPointF((n - 1) / 2.0, (n - 1) / 2.0) : QPointF(n / 4.0 - 0.5, n / 4.0 - 0.5);
    const QPointF c2(c1.x() + half, c1.y() + half);
    auto torus = [n](double d) {
        d = std::abs(d);
        return std::min(d, n - d);
    };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const double d1 = std::hypot(torus(x - c1.x()), torus(y - c1.y()));
            const double d2 = centers == 1 ? 1e9 : std::hypot(torus(x - c2.x()), torus(y - c2.y()));
            const QPointF c = d1 <= d2 ? c1 : c2;
            cells.push_back({y * n + x, d1 <= d2 ? 0 : 1, std::min(d1, d2), std::atan2(y - c.y(), x - c.x())});
        }
    auto less = [](const Cell& a, const Cell& b) {
        if (std::abs(a.dist - b.dist) > 1e-9)
            return a.dist < b.dist;
        return a.angle < b.angle;
    };
    std::vector<int> rank(static_cast<size_t>(n) * n);
    std::vector<Cell> lists[2];
    for (const Cell& c : cells)
        lists[c.cluster].push_back(c);
    for (auto& l : lists)
        std::sort(l.begin(), l.end(), less);
    // Alternando entre los dos grupos (con uno, en orden).
    size_t i0 = 0, i1 = 0;
    for (int r = 0; r < n * n; ++r) {
        const bool first = i1 >= lists[1].size() || (i0 < lists[0].size() && (r % 2 == 0));
        const Cell& c = first ? lists[0][i0++] : lists[1][i1++];
        rank[size_t(c.index)] = r;
    }
    return rank;
}

// Ruido azul (el método «void and cluster» de Ulichney) en un mosaico de n x n.
std::vector<int> blueNoiseRanks(int n)
{
    const int count = n * n;
    const double sigma = 1.5;
    std::vector<double> kernel(static_cast<size_t>(count));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const int dx = std::min(x, n - x), dy = std::min(y, n - y);
            kernel[size_t(y * n + x)] = std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma));
        }
    std::vector<uint8_t> bits(static_cast<size_t>(count), 0);
    std::vector<double> energy(static_cast<size_t>(count), 0.0);
    auto change = [&](int index, double sgn) {
        const int ix = index % n, iy = index / n;
        bits[size_t(index)] = sgn > 0;
        for (int y = 0; y < n; ++y) {
            const int ky = ((y - iy) % n + n) % n;
            for (int x = 0; x < n; ++x)
                energy[size_t(y * n + x)] += sgn * kernel[size_t(ky * n + ((x - ix) % n + n) % n)];
        }
    };
    auto tightest = [&] {
        int best = -1;
        for (int i = 0; i < count; ++i)
            if (bits[size_t(i)] && (best < 0 || energy[size_t(i)] > energy[size_t(best)]))
                best = i;
        return best;
    };
    auto largestVoid = [&] {
        int best = -1;
        for (int i = 0; i < count; ++i)
            if (!bits[size_t(i)] && (best < 0 || energy[size_t(i)] < energy[size_t(best)]))
                best = i;
        return best;
    };
    std::mt19937 rng(uint32_t(1234 + n));
    const int ones = std::max(1, count / 10);
    for (int placed = 0; placed < ones;) {
        const int i = int(rng() % uint32_t(count));
        if (!bits[size_t(i)]) {
            change(i, +1);
            ++placed;
        }
    }
    for (int iter = 0; iter < count; ++iter) {
        const int t = tightest();
        change(t, -1);
        const int v = largestVoid();
        change(v, +1);
        if (v == t)
            break;
    }
    const std::vector<uint8_t> startBits = bits;
    const std::vector<double> startEnergy = energy;
    std::vector<int> rank(static_cast<size_t>(count));
    for (int r = ones - 1; r >= 0; --r) {
        const int t = tightest();
        change(t, -1);
        rank[size_t(t)] = r;
    }
    bits = startBits;
    energy = startEnergy;
    for (int r = ones; r < count; ++r) {
        const int v = largestVoid();
        change(v, +1);
        rank[size_t(v)] = r;
    }
    return rank;
}

struct DitherMatrix {
    int n = 2;
    std::vector<float> t;
};

const QStringList& ditherMatrixNames()
{
    static const QStringList list{QObject::tr("Bayer 2x2"),           QObject::tr("Bayer 4x4"),
                                  QObject::tr("Bayer 8x8"),           QObject::tr("Bayer 16x16"),
                                  QObject::tr("Espiral 4x4"),         QObject::tr("Espiral 8x8"),
                                  QObject::tr("Espiral doble 4x4"),   QObject::tr("Espiral doble 8x8"),
                                  QObject::tr("Ruido azul 16x16"),    QObject::tr("Ruido azul 32x32"),
                                  QObject::tr("Ruido azul 64x64"),    QObject::tr("Ruido blanco 8x8"),
                                  QObject::tr("Ruido blanco 16x16"),  QObject::tr("Ruido blanco 32x32"),
                                  QObject::tr("Ruido blanco 64x64")};
    return list;
}

DitherMatrix ditherMatrix(int which, uint32_t seed)
{
    DitherMatrix m;
    if (which <= 3) {
        m.n = 2 << which;
        m.t = thresholdsFromRanks(bayerRanks(m.n));
    } else if (which <= 7) {
        m.n = (which % 2 == 0) ? 4 : 8;
        m.t = thresholdsFromRanks(spiralRanks(m.n, which <= 5 ? 1 : 2));
    } else if (which <= 10) {
        // El ruido azul se calcula una vez por tamaño; la semilla lo desplaza y lo voltea.
        m.n = 16 << (which - 8);
        static std::mutex mutex;
        static std::vector<float> cache[3];
        std::vector<float> base;
        {
            std::lock_guard lock(mutex);
            std::vector<float>& c = cache[which - 8];
            if (c.empty())
                c = thresholdsFromRanks(blueNoiseRanks(m.n));
            base = c;
        }
        const int n = m.n;
        const int sx = int(seed * 7u) % n, sy = int(seed * 13u) % n;
        const bool transpose = seed & 1u;
        m.t.resize(base.size());
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const int bx = (x + sx) % n, by = (y + sy) % n;
                m.t[size_t(y * n + x)] = transpose ? base[size_t(bx * n + by)] : base[size_t(by * n + bx)];
            }
    } else {
        // Ruido blanco: un orden al azar (según la semilla).
        m.n = 8 << (which - 11);
        const int count = m.n * m.n;
        std::vector<std::pair<uint32_t, int>> order(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            order[size_t(i)] = {hash3(uint32_t(i), 0x51u, seed), i};
        std::sort(order.begin(), order.end());
        std::vector<int> rank(static_cast<size_t>(count));
        for (int r = 0; r < count; ++r)
            rank[size_t(order[size_t(r)].second)] = r;
        m.t = thresholdsFromRanks(rank);
    }
    return m;
}

void renderOrderedDither(const RenderArgs& a)
{
    // Cada canal, a 'levels' niveles; el umbral de la matriz decide hacia cuál se redondea (con
    // intensidad 0, al más cercano: posterizar).
    const DitherMatrix m = ditherMatrix(std::clamp(ival(a.values, "matrix"), 0, int(ditherMatrixNames().size()) - 1),
                                        uint32_t(ival(a.values, "seed")));
    const int levels = std::max(2, ival(a.values, "levels"));
    const double intensity = dval(a.values, "intensity") / 100.0;
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        QRgb* d = out.row(y);
        const float* trow = m.t.data() + size_t(y % m.n) * m.n;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            if (!qAlpha(s[x])) {
                d[x] = s[x];
                continue;
            }
            const QRgb c = qUnpremultiply(s[x]);
            const double off = (trow[x % m.n] - 0.5) * intensity;
            auto ch = [&](int v) {
                const double q = std::floor(v / 255.0 * (levels - 1) + 0.5 + off);
                return clamp255(int(std::lround(std::clamp(q, 0.0, double(levels - 1)) * 255.0 / (levels - 1))));
            };
            d[x] = qPremultiply(qRgba(ch(qRed(c)), ch(qGreen(c)), ch(qBlue(c)), qAlpha(c)));
        }
    });
}

// --- Estilizado > Semitono ------------------------------------------------------------------

// La forma del punto de cada patrón, como en Paint.NET: el punto crece desde el centro de la celda
// con la raíz de la tinta hasta llegar a las esquinas con toda la tinta (los puntos redondos ya se
// tocan a media tinta: más densos que si el área fuese la tinta). 'spotMetric' da 0 en el centro y
// 1 en la esquina (u, v de -0,5 a 0,5) y 'spotLimit', hasta dónde llega el punto con esa tinta.
double spotMetric(int pattern, double u, double v)
{
    switch (pattern) {
    case 1: return std::abs(u) + std::abs(v);                                                // rombo
    case 2: return std::hypot(u, v / 0.65) / std::hypot(0.5, 0.5 / 0.65);                    // elipse
    case 3: return std::max(std::abs(u), std::abs(v)) * 2;                                   // cuadrado
    case 4: return std::min(std::abs(u), std::abs(v)) * 2;                                   // trama cruzada
    case 5: return std::abs(v) * 2;                                                          // líneas
    default: return std::hypot(u, v) * M_SQRT2;                                              // puntos
    }
}

double spotLimit(int pattern, double ink)
{
    ink = clamp01(ink);
    switch (pattern) {
    case 4: return 1 - std::sqrt(1 - ink); // dos barras que se cruzan: su unión cubre la tinta
    case 5: return ink;                    // líneas: el ancho es la tinta
    default: return std::sqrt(ink) * 0.88;
    }
}

void renderHalftone(const RenderArgs& a)
{
    // Semitono en cuatricromía: cian, magenta, amarillo y negro, cada uno en su trama girada
    // (15°, 75°, 0° y 45° más el ángulo), con puntos del tamaño de la tinta de su celda; o solo
    // negro (monocromo). Generación de negro: cuánto del gris común pasa a la tinta negra.
    const int pattern = ival(a.values, "pattern");
    const bool mono = bval(a.values, "mono");
    const double cell = std::max(1.0, dval(a.values, "cell"));
    const double base = dval(a.values, "angle");
    const double gcr = std::clamp(dval(a.values, "black") / 100.0, 0.0, 1.0);
    const double soft = dval(a.values, "softness") / 100.0;
    const int q = std::clamp(ival(a.values, "quality"), 1, 8);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);

    struct Ink {
        int channel; // 0 C, 1 M, 2 Y, 3 K
        double c, s;
        int i0, j0, ni, nj;
        std::vector<float> value;
    };
    std::vector<Ink> inks;
    const int channels[4] = {0, 1, 2, 3};
    const double angles[4] = {15, 75, 0, 45};
    for (int k = 0; k < 4; ++k) {
        if (mono && k != 3)
            continue;
        const double th = (base + angles[k]) * M_PI / 180.0;
        inks.push_back({channels[k], std::cos(th), std::sin(th), 0, 0, 0, 0, {}});
    }
    // La tinta de cada celda (en su trama girada), con el color de su centro.
    auto inkOf = [&](QRgb c, int channel) {
        const double r = qRed(c) / 255.0, g = qGreen(c) / 255.0, b = qBlue(c) / 255.0;
        if (mono)
            return 1.0 - intensityF(c);
        double cc = 1 - r, mm = 1 - g, yy = 1 - b;
        const double k = std::min({cc, mm, yy}) * gcr;
        if (channel == 3)
            return k;
        const double rest = 1 - k;
        const double v = channel == 0 ? cc : channel == 1 ? mm : yy;
        return rest > 1e-6 ? clamp01((v - k) / rest) : 0.0;
    };
    for (Ink& ink : inks) {
        double smin = 1e18, smax = -1e18, tmin = 1e18, tmax = -1e18;
        for (const QPointF p : {QPointF(a.roi.left(), a.roi.top()), QPointF(a.roi.right() + 1, a.roi.top()),
                                QPointF(a.roi.left(), a.roi.bottom() + 1), QPointF(a.roi.right() + 1, a.roi.bottom() + 1)}) {
            const double s = (p.x() * ink.c + p.y() * ink.s) / cell, t = (-p.x() * ink.s + p.y() * ink.c) / cell;
            smin = std::min(smin, s), smax = std::max(smax, s), tmin = std::min(tmin, t), tmax = std::max(tmax, t);
        }
        ink.i0 = int(std::floor(smin)) - 1;
        ink.j0 = int(std::floor(tmin)) - 1;
        ink.ni = int(std::floor(smax)) - ink.i0 + 2;
        ink.nj = int(std::floor(tmax)) - ink.j0 + 2;
        ink.value.assign(size_t(ink.ni) * ink.nj, 0.f);
        parallelRows(0, ink.nj, [&](int j0, int j1) {
            for (int j = j0; j < j1 && !a.cancel; ++j)
                for (int i = 0; i < ink.ni; ++i) {
                    const double s = (ink.i0 + i + 0.5) * cell, t = (ink.j0 + j + 0.5) * cell;
                    const double x = s * ink.c - t * ink.s, y = s * ink.s + t * ink.c;
                    ink.value[size_t(j) * ink.ni + i] = float(inkOf(cellColor(in, x, y, cell), ink.channel));
                }
        });
    }
    forRows(a, [&](int y) {
        const QRgb* src = in.row(y);
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            if (!qAlpha(src[x])) {
                d[x] = src[x];
                continue;
            }
            double sr = 0, sg = 0, sb = 0;
            for (int sj = 0; sj < q; ++sj)
                for (int si = 0; si < q; ++si) {
                    const double px = x + (si + 0.5) / q, py = y + (sj + 0.5) / q;
                    double cov[4] = {0, 0, 0, 0};
                    for (const Ink& ink : inks) {
                        const double s = (px * ink.c + py * ink.s) / cell, t = (-px * ink.s + py * ink.c) / cell;
                        const double fs = std::floor(s), ft = std::floor(t);
                        const int i = std::clamp(int(fs) - ink.i0, 0, ink.ni - 1), j = std::clamp(int(ft) - ink.j0, 0, ink.nj - 1);
                        const double value = ink.value[size_t(j) * ink.ni + i];
                        const double m = spotMetric(pattern, s - fs - 0.5, t - ft - 0.5);
                        const double limit = value > 1e-4 ? spotLimit(pattern, value) : -1.0;
                        cov[ink.channel] = soft > 0 ? clamp01((limit - m) / soft + 0.5) : (m < limit ? 1.0 : 0.0);
                    }
                    const double k = 1 - cov[3];
                    sr += (1 - cov[0]) * k;
                    sg += (1 - cov[1]) * k;
                    sb += (1 - cov[2]) * k;
                }
            const double n = q * q;
            const QRgb o = qRgba(clamp255(int(std::lround(sr / n * 255))), clamp255(int(std::lround(sg / n * 255))),
                                 clamp255(int(std::lround(sb / n * 255))), qAlpha(src[x]));
            d[x] = qPremultiply(o);
        }
    });
}

// --- Artístico > Linograbado -----------------------------------------------------------------

void renderLinocut(const RenderArgs& a)
{
    // Líneas de tinta sobre papel, más gruesas cuanto más oscuro: paralelas (al ángulo), siguiendo
    // los contornos de la imagen o cruzadas. La aspereza hace temblar y rasga los bordes.
    const int mode = ival(a.values, "mode");
    const double th = dval(a.values, "angle") * M_PI / 180.0;
    const double spacing = std::max(1.0, dval(a.values, "spacing"));
    const double contrast = dval(a.values, "contrast");
    const double gamma = std::max(0.05, dval(a.values, "gamma"));
    const double rough = dval(a.values, "roughness") / 100.0;
    const uint32_t seed = uint32_t(ival(a.values, "seed"));
    const QRgb ink = cval(a.values, "ink").rgba();
    const QRgb paper = cval(a.values, "paper").rgba();
    const QRect roi = a.roi;
    const int w = roi.width(), h = roi.height();

    // El tono (0 papel, 1 tinta) de cada píxel, algo suavizado para que las líneas no se rompan.
    const QImage blurred = tentBlur(a.src, roi, std::max(1, int(std::lround(spacing / 4))), a.cancel);
    if (a.cancel)
        return;
    std::vector<float> dark(static_cast<size_t>(w) * h);
    {
        const ConstPixels b(blurred);
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const QRgb* row = b.row(y);
                for (int x = 0; x < w; ++x) {
                    const QRgb c = qUnpremultiply(row[x]);
                    double l = clamp01((intensityF(c) - 0.5) * contrast + 0.5);
                    l = std::pow(l, 1.0 / gamma);
                    dark[size_t(y) * w + x] = float(1.0 - l);
                }
            }
        });
    }
    // Contorno: las líneas son las curvas a igual distancia de los bordes de las zonas de tono.
    std::vector<float> contour;
    if (mode == 1) {
        const QImage soft = tentBlur(a.src, roi, std::max(2, int(std::lround(spacing))), a.cancel);
        if (a.cancel)
            return;
        const ConstPixels b(soft);
        std::vector<uint8_t> level(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                level[size_t(y) * w + x] = uint8_t(std::min(5, int(intensityF(qUnpremultiply(b.row(y)[x])) * 6)));
        std::vector<uint8_t> edge(static_cast<size_t>(w) * h, 0);
        bool any = false;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const uint8_t l = level[size_t(y) * w + x];
                if ((x + 1 < w && level[size_t(y) * w + x + 1] != l) || (y + 1 < h && level[size_t(y + 1) * w + x] != l))
                    edge[size_t(y) * w + x] = 1, any = true;
            }
        if (any)
            contour = distanceTransform(edge, w, h);
    }
    const double c = std::cos(th), s = std::sin(th);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    // Cobertura de una familia de líneas en la coordenada 'u' (a lo ancho de las líneas) con la
    // fracción de tinta 'frac' (1: todo tinta).
    auto lines = [&](double u, double frac) {
        const double t = u / spacing - std::floor(u / spacing);
        const double dist = std::abs(t - 0.5) * spacing;
        // La parte del píxel (de ancho 1) que cae en la línea (de medio ancho 'half'): sin rastro
        // de línea donde no hay tinta.
        const double half = frac * spacing / 2;
        return clamp01(std::min(half, dist + 0.5) - std::max(-half, dist - 0.5));
    };
    forRows(a, [&](int y) {
        const QRgb* src = in.row(y);
        QRgb* d = out.row(y);
        const int ly = y - roi.top();
        for (int x = roi.left(); x <= roi.right(); ++x) {
            const int lx = x - roi.left();
            double dk = dark[size_t(ly) * w + lx];
            const double px = x + 0.5, py = y + 0.5;
            double wobble = 0;
            if (rough > 0) {
                wobble = rough * spacing * 0.15 * valueNoise(px / (spacing * 1.5), py / (spacing * 1.5), seed);
                dk = clamp01(dk + rough * 0.2 * valueNoise(px / (spacing * 0.3), py / (spacing * 0.3), seed + 101u));
            }
            // Las líneas siguen el ángulo (45°: de abajo a la izquierda a arriba a la derecha).
            const double across = px * s + py * c;
            double cov;
            if (mode == 2) {
                const double frac = 1.0 - std::sqrt(1.0 - dk);
                const double along = px * c - py * s;
                const double c1 = lines(across + wobble, frac), c2 = lines(along + wobble, frac);
                cov = 1 - (1 - c1) * (1 - c2);
            } else if (mode == 1 && !contour.empty()) {
                cov = lines(contour[size_t(ly) * w + lx] + wobble, dk);
            } else {
                cov = lines(across + wobble, dk);
            }
            QRgb o = mixStraight(paper, ink, cov);
            o = qRgba(qRed(o), qGreen(o), qBlue(o), qAlpha(o) * qAlpha(src[x]) / 255);
            d[x] = qPremultiply(o);
        }
    });
}

// --- Artístico > Puntillismo -----------------------------------------------------------------

void renderPointillism(const RenderArgs& a)
{
    // Un punto redondo por celda (descolocado al azar), del color de la imagen allí y con algo de
    // variación; con la respuesta tonal, más pequeño en lo claro (deja ver el fondo).
    const double cell = std::max(2, ival(a.values, "cell"));
    const double fill = ival(a.values, "fill") / 100.0;
    const double tonal = ival(a.values, "tonal") / 100.0;
    const double place = ival(a.values, "placement") / 100.0;
    const double cvar = ival(a.values, "colorvar") / 100.0;
    const QRgb bg = cval(a.values, "background").rgba();
    const uint32_t seed = uint32_t(ival(a.values, "seed"));
    const ConstPixels in(a.src);
    const Pixels out(a.dst);

    const int i0 = int(std::floor(a.roi.left() / cell)) - 2, j0 = int(std::floor(a.roi.top() / cell)) - 2;
    const int ni = int(std::floor((a.roi.right() + 1) / cell)) - i0 + 3, nj = int(std::floor((a.roi.bottom() + 1) / cell)) - j0 + 3;
    struct Dot {
        float x, y, r, z;
        QRgb color;
    };
    std::vector<Dot> dots(static_cast<size_t>(ni) * nj);
    parallelRows(0, nj, [&](int jj0, int jj1) {
        for (int jj = jj0; jj < jj1 && !a.cancel; ++jj)
            for (int ii = 0; ii < ni; ++ii) {
                const uint32_t i = uint32_t(i0 + ii), j = uint32_t(j0 + jj);
                const double cx = (int(i) + 0.5 + (hashUnit(i, j, seed) - 0.5) * place) * cell;
                const double cy = (int(j) + 0.5 + (hashUnit(i, j, seed + 1u) - 0.5) * place) * cell;
                const QRgb c = cellColor(in, cx, cy, cell * 0.5);
                const double l = intensityF(c);
                // Sobre todo de brillo (igual en los tres canales) y algo de tono.
                const double common = (hashUnit(i, j, seed + 6u) - 0.5) * 2 * cvar * 55;
                auto vary = [&](int v, uint32_t k) {
                    return clamp255(int(std::lround(v + common + (hashUnit(i, j, seed + k) - 0.5) * 2 * cvar * 18)));
                };
                Dot& dot = dots[size_t(jj) * ni + ii];
                dot.x = float(cx);
                dot.y = float(cy);
                dot.r = float(std::max(0.35, cell * fill / 2 * (1 - tonal * 0.5 * l * l)));
                dot.z = hashUnit(i, j, seed + 5u);
                dot.color = qRgba(vary(qRed(c), 2), vary(qGreen(c), 3), vary(qBlue(c), 4), qAlpha(c));
            }
    });
    if (a.cancel)
        return;
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        struct Hit {
            float z;
            float cov;
            QRgb color;
        };
        Hit hits[25];
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const int ci = int(std::floor(px / cell)) - i0, cj = int(std::floor(py / cell)) - j0;
            int n = 0;
            for (int jj = std::max(0, cj - 2); jj <= std::min(nj - 1, cj + 2); ++jj)
                for (int ii = std::max(0, ci - 2); ii <= std::min(ni - 1, ci + 2); ++ii) {
                    const Dot& dot = dots[size_t(jj) * ni + ii];
                    const double cov = clamp01(dot.r - std::hypot(px - dot.x, py - dot.y) + 0.5);
                    if (cov <= 0)
                        continue;
                    // En orden de profundidad (los de z mayor, encima).
                    int at = n++;
                    while (at > 0 && hits[at - 1].z > dot.z) {
                        hits[at] = hits[at - 1];
                        --at;
                    }
                    hits[at] = {dot.z, float(cov), dot.color};
                }
            QRgb o = bg;
            for (int k = 0; k < n; ++k) {
                const QRgb c = hits[k].color;
                o = overStraight(o, qRgba(qRed(c), qGreen(c), qBlue(c), int(std::lround(qAlpha(c) * hits[k].cov))));
            }
            d[x] = qPremultiply(o);
        }
    });
}

// --- Teselas: Mosaico y Vidriera --------------------------------------------------------------

// Lo que hay en un punto de un mosaico: su tesela (una clave), el centro de la tesela, la
// distancia al borde de la tesela, la dirección hacia ese borde (hacia fuera) y un sombreado
// propio (las caras de los cubos).
struct TileHit {
    int64_t key = -1;
    double cx = 0, cy = 0;
    double dist = 0;
    double nx = 0, ny = -1;
    double shade = 0;
};

class Tiling {
public:
    virtual ~Tiling() = default;
    virtual bool hit(double x, double y, TileHit* h) const = 0;
};

// Teselas poligonales convexas en una zona, con una rejilla de cubos para encontrarlas.
class PolygonTiling : public Tiling {
public:
    struct Poly {
        std::vector<QPointF> pts;
        double shade = 0;
    };

    PolygonTiling(const QRectF& area, double bucket)
        : m_area(area)
        , m_bucket(std::max(2.0, bucket))
    {
        m_nx = std::max(1, int(std::ceil(area.width() / m_bucket)) + 1);
        m_ny = std::max(1, int(std::ceil(area.height() / m_bucket)) + 1);
        m_cells.resize(size_t(m_nx) * m_ny);
    }

    void add(const std::vector<QPointF>& pts, double shade = 0)
    {
        if (pts.size() < 3)
            return;
        double minx = pts[0].x(), maxx = minx, miny = pts[0].y(), maxy = miny;
        for (const QPointF& p : pts)
            minx = std::min(minx, p.x()), maxx = std::max(maxx, p.x()), miny = std::min(miny, p.y()), maxy = std::max(maxy, p.y());
        if (maxx < m_area.left() || minx > m_area.right() || maxy < m_area.top() || miny > m_area.bottom())
            return;
        Item item;
        item.shade = shade;
        // Orden antihorario en la pantalla (y hacia abajo): el área con signo, positiva.
        double area2 = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            const QPointF& p = pts[i];
            const QPointF& q = pts[(i + 1) % pts.size()];
            area2 += p.x() * q.y() - q.x() * p.y();
        }
        item.pts = pts;
        if (area2 < 0)
            std::reverse(item.pts.begin(), item.pts.end());
        double cx = 0, cy = 0;
        for (const QPointF& p : item.pts) {
            cx += p.x();
            cy += p.y();
            item.edges.push_back({});
        }
        item.cx = cx / double(item.pts.size());
        item.cy = cy / double(item.pts.size());
        for (size_t i = 0; i < item.pts.size(); ++i) {
            const QPointF& p = item.pts[i];
            const QPointF& q = item.pts[(i + 1) % item.pts.size()];
            const double ex = q.x() - p.x(), ey = q.y() - p.y();
            const double len = std::max(1e-12, std::hypot(ex, ey));
            // Normal hacia fuera (el polígono queda a la izquierda al recorrerlo en ese sentido).
            item.edges[i] = {ey / len, -ex / len, -(ey / len * p.x() - ex / len * p.y())};
        }
        const int index = int(m_items.size());
        m_items.push_back(std::move(item));
        const int bx0 = std::clamp(int((minx - m_area.left()) / m_bucket), 0, m_nx - 1);
        const int bx1 = std::clamp(int((maxx - m_area.left()) / m_bucket), 0, m_nx - 1);
        const int by0 = std::clamp(int((miny - m_area.top()) / m_bucket), 0, m_ny - 1);
        const int by1 = std::clamp(int((maxy - m_area.top()) / m_bucket), 0, m_ny - 1);
        for (int by = by0; by <= by1; ++by)
            for (int bx = bx0; bx <= bx1; ++bx)
                m_cells[size_t(by) * m_nx + bx].push_back(index);
    }

    bool hit(double x, double y, TileHit* h) const override
    {
        const int bx = std::clamp(int((x - m_area.left()) / m_bucket), 0, m_nx - 1);
        const int by = std::clamp(int((y - m_area.top()) / m_bucket), 0, m_ny - 1);
        double bestDist = -1e18;
        int best = -1;
        const Edge* bestEdge = nullptr;
        for (int index : m_cells[size_t(by) * m_nx + bx]) {
            const Item& it = m_items[size_t(index)];
            // Dentro si está detrás de todos los lados; la distancia al borde, la del lado más cercano.
            double inside = 1e18;
            const Edge* nearest = nullptr;
            for (const Edge& e : it.edges) {
                const double dd = -(e.nx * x + e.ny * y + e.c);
                if (dd < inside)
                    inside = dd, nearest = &e;
            }
            if (inside > bestDist) {
                bestDist = inside;
                best = index;
                bestEdge = nearest;
            }
            if (inside >= 0)
                break;
        }
        if (best < 0)
            return false;
        const Item& it = m_items[size_t(best)];
        h->key = best;
        h->cx = it.cx;
        h->cy = it.cy;
        h->dist = std::max(0.0, bestDist);
        h->nx = bestEdge->nx;
        h->ny = bestEdge->ny;
        h->shade = it.shade;
        return true;
    }

private:
    struct Edge {
        double nx, ny, c;
    };
    struct Item {
        std::vector<QPointF> pts;
        std::vector<Edge> edges;
        double cx = 0, cy = 0;
        double shade = 0;
    };
    QRectF m_area;
    double m_bucket;
    int m_nx = 1, m_ny = 1;
    std::vector<std::vector<int>> m_cells;
    std::vector<Item> m_items;
};

// Celdas de Voronoi de puntos descolocados en una rejilla (Vidriera, Adoquines). 'round' redondea
// las esquinas (la distancia al borde mezcla los dos vecinos más cercanos).
class VoronoiTiling : public Tiling {
public:
    VoronoiTiling(double cell, double jitter, uint32_t seed, double round, QPointF origin)
        : m_cell(std::max(2.0, cell))
        , m_jitter(jitter)
        , m_seed(seed)
        , m_round(round)
        , m_origin(origin)
    {
    }

    QPointF site(int i, int j) const
    {
        const double jx = (hashUnit(uint32_t(i), uint32_t(j), m_seed) - 0.5) * m_jitter;
        const double jy = (hashUnit(uint32_t(i), uint32_t(j), m_seed + 7u) - 0.5) * m_jitter;
        return {m_origin.x() + (i + 0.5 + jx) * m_cell, m_origin.y() + (j + 0.5 + jy) * m_cell};
    }

    bool hit(double x, double y, TileHit* h) const override
    {
        const int ci = int(std::floor((x - m_origin.x()) / m_cell)), cj = int(std::floor((y - m_origin.y()) / m_cell));
        QPointF s[25];
        int ids[25][2];
        double d2[25];
        int n = 0, best = 0;
        for (int j = cj - 2; j <= cj + 2; ++j)
            for (int i = ci - 2; i <= ci + 2; ++i) {
                s[n] = site(i, j);
                ids[n][0] = i;
                ids[n][1] = j;
                d2[n] = (s[n].x() - x) * (s[n].x() - x) + (s[n].y() - y) * (s[n].y() - y);
                if (d2[n] < d2[best])
                    best = n;
                ++n;
            }
        // Distancia a la mediatriz con cada vecino; las dos menores.
        double b1 = 1e18, b2 = 1e18;
        QPointF n1(0, -1), n2(0, -1);
        for (int k = 0; k < n; ++k) {
            if (k == best)
                continue;
            const double dx = s[k].x() - s[best].x(), dy = s[k].y() - s[best].y();
            const double len = std::hypot(dx, dy);
            if (len < 1e-9)
                continue;
            const double dist = (d2[k] - d2[best]) / (2 * len);
            if (dist < b1) {
                b2 = b1, n2 = n1;
                b1 = dist, n1 = QPointF(dx / len, dy / len);
            } else if (dist < b2) {
                b2 = dist, n2 = QPointF(dx / len, dy / len);
            }
        }
        double dist = b1;
        QPointF normal = n1;
        if (m_round > 0 && b2 < 1e17) {
            // Mínimo suave: cerca de una esquina, la distancia y la normal se redondean.
            const double e1 = std::exp(-b1 / m_round), e2 = std::exp(-b2 / m_round);
            dist = -m_round * std::log(e1 + e2);
            const double w1 = e1 / (e1 + e2), w2 = 1 - w1;
            normal = n1 * w1 + n2 * w2;
            const double len = std::hypot(normal.x(), normal.y());
            normal = len > 1e-9 ? normal / len : n1;
        }
        h->key = (int64_t(ids[best][1]) << 32) ^ int64_t(uint32_t(ids[best][0]));
        h->cx = s[best].x();
        h->cy = s[best].y();
        h->dist = std::max(0.0, dist);
        h->nx = normal.x();
        h->ny = normal.y();
        h->shade = 0;
        return true;
    }

private:
    double m_cell, m_jitter;
    uint32_t m_seed;
    double m_round;
    QPointF m_origin;
};

// Escamas de pez: círculos en filas al tresbolillo; cada fila tapa la de debajo (se ve el arco de
// abajo de cada escama).
class FishScaleTiling : public Tiling {
public:
    FishScaleTiling(double r, QPointF origin)
        : m_r(std::max(2.0, r))
        , m_origin(origin)
    {
    }

    QPointF center(int i, int j) const { return {m_origin.x() + 2 * m_r * i + (j & 1 ? m_r : 0), m_origin.y() + m_r * j}; }

    bool hit(double x, double y, TileHit* h) const override
    {
        const int jc = int(std::floor((y - m_origin.y()) / m_r));
        // La fila de más arriba que lo contiene (las de arriba se ven encima).
        for (int j = jc - 1; j <= jc + 2; ++j) {
            const double off = j & 1 ? m_r : 0;
            const int ic = int(std::floor((x - m_origin.x() - off) / (2 * m_r)));
            for (int i = ic - 1; i <= ic + 1; ++i) {
                const QPointF c = center(i, j);
                const double dc = std::hypot(x - c.x(), y - c.y());
                if (dc > m_r)
                    continue;
                // El borde: el propio círculo y los dos de la fila de encima que lo muerden.
                double dist = m_r - dc;
                QPointF n = dc > 1e-9 ? QPointF((x - c.x()) / dc, (y - c.y()) / dc) : QPointF(0, 1);
                for (const QPointF& o : {center(j & 1 ? i : i - 1, j - 1), center(j & 1 ? i + 1 : i, j - 1)}) {
                    const double d = std::hypot(x - o.x(), y - o.y()) - m_r;
                    if (d < dist && d > -1e-9) {
                        dist = d;
                        const double len = std::max(1e-9, std::hypot(o.x() - x, o.y() - y));
                        n = QPointF((o.x() - x) / len, (o.y() - y) / len);
                    }
                }
                h->key = (int64_t(j) << 32) ^ int64_t(uint32_t(i));
                h->cx = c.x();
                h->cy = c.y() + m_r * 0.4;
                h->dist = std::max(0.0, dist);
                h->nx = n.x();
                h->ny = n.y();
                h->shade = 0;
                return true;
            }
        }
        return false;
    }

private:
    double m_r;
    QPointF m_origin;
};

// Anfiteatro: anillos alrededor del ancla, partidos en piezas de un largo parecido al ancho.
class AmphitheaterTiling : public Tiling {
public:
    AmphitheaterTiling(double size, QPointF center, uint32_t seed)
        : m_size(std::max(2.0, size))
        , m_center(center)
        , m_seed(seed)
    {
    }

    bool hit(double x, double y, TileHit* h) const override
    {
        const double dx = x - m_center.x(), dy = y - m_center.y();
        const double r = std::hypot(dx, dy);
        const int ring = int(std::floor(r / m_size));
        const double nxr = r > 1e-9 ? dx / r : 0, nyr = r > 1e-9 ? dy / r : -1;
        double dist = (ring + 1) * m_size - r;
        double nx = nxr, ny = nyr;
        if (ring > 0 && r - ring * m_size < dist)
            dist = r - ring * m_size, nx = -nxr, ny = -nyr;
        int seg = 0;
        double mid = 0;
        if (ring > 0) {
            const int n = std::max(3, int(std::lround(2 * M_PI * (ring + 0.5))));
            const double step = 2 * M_PI / n;
            const double off = hashUnit(uint32_t(ring), 3u, m_seed) * step;
            double ang = std::atan2(dy, dx) - off;
            ang -= std::floor(ang / (2 * M_PI)) * 2 * M_PI;
            seg = int(ang / step) % n;
            const double a0 = ang - seg * step, a1 = (seg + 1) * step - ang;
            // Los cortes entre piezas, como lados rectos (radiales).
            const double ta = std::min(a0, a1);
            const double dseg = r * std::sin(std::min(ta, M_PI / 2));
            if (dseg < dist) {
                dist = dseg;
                const double edgeAng = (a0 < a1 ? seg * step : (seg + 1) * step) + off;
                // La normal del corte, hacia fuera de la pieza.
                const double tx = -std::sin(edgeAng), ty = std::cos(edgeAng);
                nx = a0 < a1 ? -tx : tx;
                ny = a0 < a1 ? -ty : ty;
            }
            mid = (seg + 0.5) * step + off;
        }
        const double rm = ring > 0 ? (ring + 0.5) * m_size : 0;
        h->key = (int64_t(ring) << 32) ^ int64_t(seg);
        h->cx = m_center.x() + std::cos(mid) * rm;
        h->cy = m_center.y() + std::sin(mid) * rm;
        h->dist = std::max(0.0, dist);
        h->nx = nx;
        h->ny = ny;
        h->shade = 0;
        return true;
    }

private:
    double m_size;
    QPointF m_center;
    uint32_t m_seed;
};

// Piezas hechas de casillas (tetrominós, pentominós): un número de pieza por casilla.
class CellTiling : public Tiling {
public:
    CellTiling(const QRectF& area, double cell, int pieceSize, uint32_t seed)
        : m_cell(std::max(2.0, cell))
    {
        m_x0 = int(std::floor(area.left() / m_cell)) - 1;
        m_y0 = int(std::floor(area.top() / m_cell)) - 1;
        m_nx = int(std::ceil(area.right() / m_cell)) - m_x0 + 2;
        m_ny = int(std::ceil(area.bottom() / m_cell)) - m_y0 + 2;
        m_ids.assign(size_t(m_nx) * m_ny, -1);
        pack(pieceSize, seed);
    }

    bool hit(double x, double y, TileHit* h) const override
    {
        const double gx = x / m_cell - m_x0, gy = y / m_cell - m_y0;
        const int cx = std::clamp(int(std::floor(gx)), 0, m_nx - 1), cy = std::clamp(int(std::floor(gy)), 0, m_ny - 1);
        const int id = m_ids[size_t(cy) * m_nx + cx];
        auto other = [&](int ix, int iy) {
            return ix < 0 || iy < 0 || ix >= m_nx || iy >= m_ny || m_ids[size_t(iy) * m_nx + ix] != id;
        };
        const double fx = (gx - cx) * m_cell, fy = (gy - cy) * m_cell;
        double dist = 1e18, nx = 0, ny = -1;
        auto take = [&](double d, double ax, double ay) {
            if (d < dist)
                dist = d, nx = ax, ny = ay;
        };
        if (other(cx - 1, cy))
            take(fx, -1, 0);
        if (other(cx + 1, cy))
            take(m_cell - fx, 1, 0);
        if (other(cx, cy - 1))
            take(fy, 0, -1);
        if (other(cx, cy + 1))
            take(m_cell - fy, 0, 1);
        // Esquinas hacia dentro: una casilla en diagonal de otra pieza.
        for (const auto& [ox, oy] : {std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}, std::pair{1, 1}}) {
            if (!other(cx + ox, cy + oy) || other(cx + ox, cy) || other(cx, cy + oy))
                continue;
            const double px = ox < 0 ? 0 : m_cell, py = oy < 0 ? 0 : m_cell;
            const double d = std::hypot(fx - px, fy - py);
            if (d < dist && d > 1e-9)
                dist = d, nx = (px - fx) / d, ny = (py - fy) / d;
        }
        const QPointF c = m_centers.value(id);
        h->key = id;
        h->cx = c.x();
        h->cy = c.y();
        h->dist = std::min(dist, m_cell);
        h->nx = nx;
        h->ny = ny;
        h->shade = 0;
        return true;
    }

private:
    // Todas las orientaciones de las piezas de 'n' casillas (n = 4 o 5), normalizadas.
    static std::vector<std::vector<QPoint>> shapes(int n)
    {
        std::vector<std::vector<QPoint>> base;
        if (n == 4) {
            base = {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{0, 0}, {1, 0}, {2, 0}, {1, 1}},
                    {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{0, 0}, {0, 1}, {0, 2}, {1, 2}}};
        } else {
            base = {{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}}, {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}},
                    {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0, 2}}, {{0, 0}, {1, 0}, {2, 0}, {1, 1}, {1, 2}},
                    {{0, 0}, {0, 1}, {1, 1}, {2, 1}, {2, 2}}, {{0, 0}, {1, 0}, {1, 1}, {1, 2}, {2, 2}},
                    {{1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}}, {{0, 0}, {0, 1}, {1, 1}, {1, 2}, {2, 2}},
                    {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}}, {{0, 0}, {1, 0}, {1, 1}, {2, 1}, {3, 1}},
                    {{1, 0}, {0, 1}, {1, 1}, {1, 2}, {2, 2}}, {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {1, 1}}};
        }
        std::vector<std::vector<QPoint>> out;
        for (const auto& b : base)
            for (int t = 0; t < 8; ++t) {
                std::vector<QPoint> s;
                for (QPoint p : b) {
                    if (t & 4)
                        p = QPoint(-p.x(), p.y());
                    for (int r = 0; r < (t & 3); ++r)
                        p = QPoint(-p.y(), p.x());
                    s.push_back(p);
                }
                // Primero la casilla de más arriba a la izquierda (la que se coloca).
                std::sort(s.begin(), s.end(), [](QPoint a, QPoint b) { return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x(); });
                const QPoint o = s.front();
                for (QPoint& p : s)
                    p -= o;
                if (std::find(out.begin(), out.end(), s) == out.end())
                    out.push_back(s);
            }
        return out;
    }

    void pack(int n, uint32_t seed)
    {
        const auto all = shapes(n);
        int next = 0;
        auto free = [&](int x, int y) { return x >= 0 && y >= 0 && x < m_nx && y < m_ny && m_ids[size_t(y) * m_nx + x] < 0; };
        for (int y = 0; y < m_ny; ++y)
            for (int x = 0; x < m_nx; ++x) {
                if (!free(x, y))
                    continue;
                // Una pieza al azar que quepa empezando aquí (si no cabe ninguna, la casilla sola o
                // con su vecina).
                const int start = int(hash3(uint32_t(x + m_x0), uint32_t(y + m_y0), seed) % uint32_t(all.size()));
                const std::vector<QPoint>* chosen = nullptr;
                for (size_t k = 0; k < all.size() && !chosen; ++k) {
                    const auto& s = all[(size_t(start) + k) % all.size()];
                    bool fits = true;
                    for (const QPoint& p : s)
                        fits = fits && free(x + p.x(), y + p.y());
                    if (fits)
                        chosen = &s;
                }
                std::vector<QPoint> piece = chosen ? *chosen : std::vector<QPoint>{{0, 0}};
                if (!chosen && free(x + 1, y))
                    piece.push_back({1, 0});
                double sx = 0, sy = 0;
                for (const QPoint& p : piece) {
                    m_ids[size_t(y + p.y()) * m_nx + x + p.x()] = next;
                    sx += (x + p.x() + m_x0 + 0.5) * m_cell;
                    sy += (y + p.y() + m_y0 + 0.5) * m_cell;
                }
                m_centers.insert(next, QPointF(sx / double(piece.size()), sy / double(piece.size())));
                ++next;
            }
    }

    double m_cell;
    int m_x0 = 0, m_y0 = 0, m_nx = 1, m_ny = 1;
    std::vector<int> m_ids;
    QHash<int, QPointF> m_centers;
};

// Repite unas teselas (en unidades de 'unit', relativas a la casilla) en una red de vectores 'u'
// y 'v' (también en unidades) que cubre la zona.
void addPeriodic(PolygonTiling& t, const QRectF& area, QPointF origin, double unit, QPointF u, QPointF v,
                 const std::vector<std::pair<std::vector<QPointF>, double>>& protos)
{
    const double det = u.x() * v.y() - u.y() * v.x();
    if (std::abs(det) < 1e-12)
        return;
    double imin = 1e18, imax = -1e18, jmin = 1e18, jmax = -1e18;
    for (const QPointF& c : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()}) {
        const double px = (c.x() - origin.x()) / unit, py = (c.y() - origin.y()) / unit;
        const double i = (px * v.y() - py * v.x()) / det, j = (u.x() * py - u.y() * px) / det;
        imin = std::min(imin, i), imax = std::max(imax, i), jmin = std::min(jmin, j), jmax = std::max(jmax, j);
    }
    for (int j = int(std::floor(jmin)) - 2; j <= int(std::ceil(jmax)) + 2; ++j)
        for (int i = int(std::floor(imin)) - 2; i <= int(std::ceil(imax)) + 2; ++i) {
            const QPointF base = origin + (u * i + v * j) * unit;
            for (const auto& [proto, shade] : protos) {
                std::vector<QPointF> pts;
                pts.reserve(proto.size());
                for (const QPointF& p : proto)
                    pts.push_back(base + p * unit);
                t.add(pts, shade);
            }
        }
}

std::vector<QPointF> rect(double x, double y, double w, double h) { return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}; }

// Mondrian: bloques grandes partidos al azar en rectángulos (cortes horizontales o verticales).
void addMondrian(PolygonTiling& t, const QRectF& area, QPointF origin, double size, uint32_t seed)
{
    const double block = size * 4;
    const int i0 = int(std::floor((area.left() - origin.x()) / block)) - 1, i1 = int(std::ceil((area.right() - origin.x()) / block));
    const int j0 = int(std::floor((area.top() - origin.y()) / block)) - 1, j1 = int(std::ceil((area.bottom() - origin.y()) / block));
    std::function<void(QRectF, int, uint32_t)> split = [&](QRectF r, int depth, uint32_t key) {
        const double w = r.width(), h = r.height();
        const double stop = hashUnit(key, uint32_t(depth), seed + 11u);
        if ((w < size * 1.6 && h < size * 1.6) || (depth > 1 && stop < 0.18) || depth > 8) {
            t.add(rect(r.x(), r.y(), w, h));
            return;
        }
        const bool vertical = w > h * 1.3 || (h <= w * 1.3 && hashUnit(key, uint32_t(depth), seed + 12u) < 0.5);
        const double pos = 0.3 + 0.4 * hashUnit(key, uint32_t(depth), seed + 13u);
        if (vertical && w >= size * 1.6) {
            const double cut = std::round(w * pos);
            split(QRectF(r.x(), r.y(), cut, h), depth + 1, hash3(key, 1u, seed));
            split(QRectF(r.x() + cut, r.y(), w - cut, h), depth + 1, hash3(key, 2u, seed));
        } else if (h >= size * 1.6) {
            const double cut = std::round(h * pos);
            split(QRectF(r.x(), r.y(), w, cut), depth + 1, hash3(key, 3u, seed));
            split(QRectF(r.x(), r.y() + cut, w, h - cut), depth + 1, hash3(key, 4u, seed));
        } else {
            t.add(rect(r.x(), r.y(), w, h));
        }
    };
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            split(QRectF(origin.x() + i * block, origin.y() + j * block, block, block), 0, hash3(uint32_t(i), uint32_t(j), seed));
}

// Sillería: hiladas de alto variable con piedras de largo variable.
void addAshlar(PolygonTiling& t, const QRectF& area, QPointF origin, double size, uint32_t seed)
{
    // Las hiladas, desde el origen hacia arriba y hacia abajo hasta cubrir la zona.
    auto rowHeight = [&](int j) { return size * (0.6 + 0.8 * hashUnit(uint32_t(j), 7u, seed)); };
    std::vector<std::pair<double, double>> rows;
    double y = origin.y();
    int j = 0;
    while (y > area.top() - size * 2)
        y -= rowHeight(--j);
    for (; y < area.bottom() + size * 2; ++j) {
        const double hgt = rowHeight(j);
        rows.push_back({y, hgt});
        double x = origin.x() - size * (1 + 3 * hashUnit(uint32_t(j), 9u, seed));
        while (x > area.left() - size * 3)
            x -= size * 3;
        for (int k = 0; x < area.right() + size * 3; ++k) {
            const double w = size * (0.8 + 1.7 * hashUnit(uint32_t(j), uint32_t(k), seed + 21u));
            t.add(rect(x, y, w, hgt));
            x += w;
        }
        y += hgt;
    }
}

// Penrose (rombos P3) por subdivisión de triángulos de Robinson desde una «rueda» de diez.
void addPenrose(PolygonTiling& t, const QRectF& area, QPointF center, double edge)
{
    const double phi = (1 + std::sqrt(5.0)) / 2;
    struct Tri {
        int kind; // 0 delgado, 1 grueso
        QPointF a, b, c;
    };
    const double reach = std::hypot(std::max(std::abs(area.left() - center.x()), std::abs(area.right() - center.x())),
                                    std::max(std::abs(area.top() - center.y()), std::abs(area.bottom() - center.y())));
    // El radio de partida: el lado deseado multiplicado por phi hasta cubrir la zona.
    double radius = edge;
    int steps = 0;
    while (radius < reach * 1.2 && steps < 40) {
        radius *= phi;
        ++steps;
    }
    std::vector<Tri> tris;
    for (int i = 0; i < 10; ++i) {
        QPointF b = center + QPointF(std::cos((2 * i - 1) * M_PI / 10), std::sin((2 * i - 1) * M_PI / 10)) * radius;
        QPointF c = center + QPointF(std::cos((2 * i + 1) * M_PI / 10), std::sin((2 * i + 1) * M_PI / 10)) * radius;
        if (i % 2 == 0)
            std::swap(b, c);
        tris.push_back({0, center, b, c});
    }
    auto touches = [&](const Tri& tr) {
        const double minx = std::min({tr.a.x(), tr.b.x(), tr.c.x()}), maxx = std::max({tr.a.x(), tr.b.x(), tr.c.x()});
        const double miny = std::min({tr.a.y(), tr.b.y(), tr.c.y()}), maxy = std::max({tr.a.y(), tr.b.y(), tr.c.y()});
        return maxx >= area.left() && minx <= area.right() && maxy >= area.top() && miny <= area.bottom();
    };
    for (int s = 0; s < steps; ++s) {
        std::vector<Tri> next;
        next.reserve(tris.size() * 3);
        for (const Tri& tr : tris) {
            if (!touches(tr))
                continue;
            if (tr.kind == 0) {
                const QPointF p = tr.a + (tr.b - tr.a) / phi;
                next.push_back({0, tr.c, p, tr.b});
                next.push_back({1, p, tr.c, tr.a});
            } else {
                const QPointF q = tr.b + (tr.a - tr.b) / phi;
                const QPointF r = tr.b + (tr.c - tr.b) / phi;
                next.push_back({1, r, tr.c, tr.a});
                next.push_back({1, q, r, tr.b});
                next.push_back({0, r, q, tr.a});
            }
        }
        tris = std::move(next);
    }
    // Cada rombo son dos triángulos con la misma base (b, c).
    auto keyOf = [](QPointF p, QPointF q) {
        auto k = [](QPointF v) { return std::pair<int64_t, int64_t>(std::llround(v.x() * 64), std::llround(v.y() * 64)); };
        auto a = k(p), b = k(q);
        if (b < a)
            std::swap(a, b);
        return std::tuple<int64_t, int64_t, int64_t, int64_t>(a.first, a.second, b.first, b.second);
    };
    std::map<std::tuple<int64_t, int64_t, int64_t, int64_t>, const Tri*> open;
    for (const Tri& tr : tris) {
        if (!touches(tr))
            continue;
        const auto k = keyOf(tr.b, tr.c);
        const auto it = open.find(k);
        if (it == open.end()) {
            open.emplace(k, &tr);
            continue;
        }
        const Tri* o = it->second;
        t.add({tr.a, tr.b, o->a, tr.c}, tr.kind == 0 ? -0.06 : 0.04);
        open.erase(it);
    }
    // Los que se quedaron sin pareja (en el borde de la zona), como triángulos.
    for (const auto& [k, tr] : open)
        t.add({tr->a, tr->b, tr->c}, tr->kind == 0 ? -0.06 : 0.04);
}

QStringList mosaicShapeNames()
{
    return {QObject::tr("Cuadrado"),        QObject::tr("Rombo"),          QObject::tr("Triángulo"),
            QObject::tr("Hexágono"),        QObject::tr("Octágono y cuadrado"), QObject::tr("El Cairo"),
            QObject::tr("Cuadrado romo"),   QObject::tr("Ladrillo"),       QObject::tr("Espiga"),
            QObject::tr("Trenzado"),        QObject::tr("Escamas de pez"), QObject::tr("Anfiteatro"),
            QObject::tr("Sillería"),        QObject::tr("Pitagórico"),     QObject::tr("Versalles"),
            QObject::tr("Mondrian"),        QObject::tr("Tetrominó"),      QObject::tr("Pentominó"),
            QObject::tr("Cubos en perspectiva"), QObject::tr("Adoquines"), QObject::tr("Penrose")};
}

std::unique_ptr<Tiling> makeMosaicTiling(int shape, const QRectF& area, QPointF origin, double size, uint32_t seed)
{
    const double r3 = std::sqrt(3.0);
    auto poly = [&](double unit, QPointF u, QPointF v, const std::vector<std::pair<std::vector<QPointF>, double>>& protos) {
        auto t = std::make_unique<PolygonTiling>(area, size);
        addPeriodic(*t, area, origin, unit, u, v, protos);
        return t;
    };
    switch (shape) {
    case 1: // Rombo: cuadrados girados 45°
        return poly(size, {1, 0}, {0.5, 0.5}, {{{{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}, 0}});
    case 2: { // Triángulo equilátero
        const double hgt = r3 / 2;
        return poly(size * 1.4, {1, 0}, {0.5, hgt}, {{{{0, 0}, {1, 0}, {0.5, hgt}}, 0}, {{{1, 0}, {1.5, hgt}, {0.5, hgt}}, 0}});
    }
    case 3: { // Hexágono
        std::vector<QPointF> hex;
        for (int k = 0; k < 6; ++k)
            hex.push_back(QPointF(std::cos((30 + 60 * k) * M_PI / 180), std::sin((30 + 60 * k) * M_PI / 180)) / r3);
        return poly(size, {1, 0}, {0.5, r3 / 2}, {{hex, 0}});
    }
    case 4: { // Octágono y cuadrado
        const double c = 1 / (2 + std::sqrt(2.0));
        return poly(size * 1.2, {1, 0}, {0, 1},
                    {{{{c, 0}, {1 - c, 0}, {1, c}, {1, 1 - c}, {1 - c, 1}, {c, 1}, {0, 1 - c}, {0, c}}, 0},
                     {{{1, 1 - c}, {1 + c, 1}, {1, 1 + c}, {1 - c, 1}}, 0}});
    }
    case 5: { // El Cairo: pentágonos alrededor de barras alternas (horizontales y verticales)
        const double k = 0.3;
        std::vector<std::pair<std::vector<QPointF>, double>> p;
        for (int cj = 0; cj < 2; ++cj)
            for (int ci = 0; ci < 2; ++ci) {
                const double i = ci, j = cj;
                if ((ci + cj) % 2 == 0) {
                    p.push_back({{{i, j + 1}, {i + 0.5 - k, j + 0.5}, {i + 0.5 + k, j + 0.5}, {i + 1, j + 1}, {i + 0.5, j + 1.5 - k}}, 0});
                    p.push_back({{{i, j}, {i + 0.5, j - 0.5 + k}, {i + 1, j}, {i + 0.5 + k, j + 0.5}, {i + 0.5 - k, j + 0.5}}, 0});
                } else {
                    p.push_back({{{i, j}, {i + 0.5, j + 0.5 - k}, {i + 0.5, j + 0.5 + k}, {i, j + 1}, {i - 0.5 + k, j + 0.5}}, 0});
                    p.push_back({{{i + 1, j}, {i + 1.5 - k, j + 0.5}, {i + 1, j + 1}, {i + 0.5, j + 0.5 + k}, {i + 0.5, j + 0.5 - k}}, 0});
                }
            }
        return poly(size * 1.4, {2, 0}, {0, 2}, p);
    }
    case 6: { // Cuadrado romo (3.3.4.3.4): dos cuadrados girados ±15° y cuatro triángulos
        const double s = 2 * std::cos(M_PI / 12), rho = M_SQRT1_2;
        auto square = [&](QPointF c, double startDeg) {
            std::vector<QPointF> pts;
            for (int k = 0; k < 4; ++k)
                pts.push_back(c + QPointF(std::cos((startDeg + 90 * k) * M_PI / 180), std::sin((startDeg + 90 * k) * M_PI / 180)) * rho);
            return pts;
        };
        // Los vértices compartidos (lado 1): los de los dos cuadrados.
        const double a = rho * std::cos(M_PI / 3), b = rho * std::sin(M_PI / 3), cc = s / 2 + a, d = s / 2 + b;
        return poly(size * 0.75, {s, 0}, {0, s},
                    {{square({0, 0}, 60), 0},
                     {square({s / 2, s / 2}, 30), 0},
                     {{{a, b}, {cc, a}, {b, -a}}, 0},
                     {{{cc, a}, {b, -a}, {d, -b}}, 0},
                     {{{a, b}, {b, d}, {-a, cc}}, 0},
                     {{{a, b}, {-a, cc}, {-b, a}}, 0}});
    }
    case 7: // Ladrillo (aparejo a soga)
        return poly(size, {2, 0}, {1, 1}, {{rect(0, 0, 2, 1), 0}});
    case 8: // Espiga
        return poly(size * 0.6, {1, 1}, {2, -2}, {{rect(0, 0, 2, 1), 0}, {rect(0, 1, 1, 2), 0}});
    case 9: // Trenzado (cesta): pares de ladrillos alternando la dirección
        return poly(size * 0.5, {4, 0}, {0, 4},
                    {{rect(0, 0, 2, 1), 0}, {rect(0, 1, 2, 1), 0}, {rect(2, 0, 1, 2), 0}, {rect(3, 0, 1, 2), 0},
                     {rect(0, 2, 1, 2), 0}, {rect(1, 2, 1, 2), 0}, {rect(2, 2, 2, 1), 0}, {rect(2, 3, 2, 1), 0}});
    case 10:
        return std::make_unique<FishScaleTiling>(size * 0.6, origin);
    case 11:
        return std::make_unique<AmphitheaterTiling>(size, origin, seed);
    case 12: {
        auto t = std::make_unique<PolygonTiling>(area, size);
        addAshlar(*t, area, origin, size, seed);
        return t;
    }
    case 13: { // Pitagórico: cuadrados de dos tamaños
        const double big = 1, small = 0.5;
        return poly(size, {big, small}, {-small, big}, {{rect(0, 0, big, big), 0}, {rect(big, 0, small, small), 0}});
    }
    case 14: // Versalles: un módulo de cuadrados y rectángulos, con las filas desplazadas
        return poly(size * 0.5, {6, 0}, {2, 6},
                    {{rect(0, 0, 4, 4), 0}, {rect(4, 0, 2, 4), 0}, {rect(2, 4, 4, 2), 0}, {rect(0, 4, 2, 2), 0}});
    case 15: {
        auto t = std::make_unique<PolygonTiling>(area, size * 2);
        addMondrian(*t, area, origin, size, seed);
        return t;
    }
    case 16:
    case 17:
        return std::make_unique<CellTiling>(area, size * 0.5, shape == 16 ? 4 : 5, seed);
    case 18: { // Cubos en perspectiva: cada hexágono, tres rombos con su luz (arriba, izquierda, derecha)
        const double x = r3 / 2;
        const QPointF c(0, 0), top(0, -1), ur(x, -0.5), lr(x, 0.5), bottom(0, 1), ll(-x, 0.5), ul(-x, -0.5);
        return poly(size * 0.7, {r3, 0}, {r3 / 2, 1.5},
                    {{{c, ul, top, ur}, 0.18}, {{c, bottom, ll, ul}, 0}, {{c, ur, lr, bottom}, -0.22}});
    }
    case 19:
        return std::make_unique<VoronoiTiling>(size, 0.55, seed, size * 0.12, origin);
    case 20: {
        auto t = std::make_unique<PolygonTiling>(area, size);
        addPenrose(*t, area, origin, size * 0.9);
        return t;
    }
    default:
        return poly(size, {1, 0}, {0, 1}, {{rect(0, 0, 1, 1), 0}});
    }
}

struct TileStyle {
    double size = 24;
    double grout = 4;     // ancho de la junta (px)
    QRgb groutColor = 0;  // directo
    double bevel = 4;     // ancho del bisel (px)
    double strength = 0.5; // luz (hacia el blanco)
    double shadow = 0.5;   // sombra (hacia el negro)
    bool pillow = false;  // bisel redondeado (Vidriera) o en chaflán (Mosaico)
    double light = 135;   // grados
    bool centerColor = false;
    double brightness = 0; // variación de luminosidad (0..1)
    double position = 0;   // variación de posición (0..1)
    uint32_t seed = 0;
};

// Pinta las teselas: el color de cada una (media de sus píxeles o el de su centro), las juntas y
// el bisel con la luz.
void renderTiles(const RenderArgs& a, const Tiling& tiling, const TileStyle& st)
{
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    struct Acc {
        double r = 0, g = 0, b = 0, al = 0;
        int64_t n = 0;
        double cx = 0, cy = 0;
    };
    std::unordered_map<int64_t, QRgb> colors;
    if (!st.centerColor) {
        // La media (premultiplicada) de cada tesela, por trozos de filas y luego juntas.
        std::mutex mutex;
        std::unordered_map<int64_t, Acc> total;
        parallelRows(a.roi.top(), a.roi.bottom() + 1, [&](int y0, int y1) {
            std::unordered_map<int64_t, Acc> local;
            for (int y = y0; y < y1 && !a.cancel; ++y) {
                const QRgb* s = in.row(y);
                Acc* last = nullptr;
                int64_t lastKey = std::numeric_limits<int64_t>::min();
                for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
                    TileHit h;
                    if (!tiling.hit(x + 0.5, y + 0.5, &h))
                        continue;
                    if (h.key != lastKey || !last) {
                        last = &local[h.key];
                        lastKey = h.key;
                    }
                    last->r += qRed(s[x]);
                    last->g += qGreen(s[x]);
                    last->b += qBlue(s[x]);
                    last->al += qAlpha(s[x]);
                    ++last->n;
                }
            }
            std::lock_guard lock(mutex);
            for (const auto& [k, v] : local) {
                Acc& t = total[k];
                t.r += v.r, t.g += v.g, t.b += v.b, t.al += v.al, t.n += v.n;
            }
        });
        if (a.cancel)
            return;
        colors.reserve(total.size());
        for (const auto& [k, v] : total) {
            const double n = double(std::max<int64_t>(1, v.n));
            colors.emplace(k, qUnpremultiply(qRgba(int(std::lround(v.r / n)), int(std::lround(v.g / n)), int(std::lround(v.b / n)),
                                                   int(std::lround(v.al / n)))));
        }
    }
    const double lx = std::cos(st.light * M_PI / 180.0), ly = -std::sin(st.light * M_PI / 180.0);
    const double halfGrout = st.grout / 2;
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        int64_t lastKey = std::numeric_limits<int64_t>::min();
        QRgb lastColor = 0;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            TileHit h;
            if (!tiling.hit(x + 0.5, y + 0.5, &h)) {
                d[x] = qPremultiply(st.groutColor);
                continue;
            }
            if (h.key != lastKey) {
                lastKey = h.key;
                if (st.centerColor) {
                    lastColor = straightAt(in, h.cx, h.cy);
                } else {
                    const auto it = colors.find(h.key);
                    lastColor = it != colors.end() ? it->second : straightAt(in, h.cx, h.cy);
                }
                const uint32_t kh = uint32_t(h.key) ^ uint32_t(h.key >> 32) * 0x9e3779b9u;
                if (st.brightness > 0)
                    lastColor = shaded(lastColor, (hashUnit(kh, 17u, st.seed) - 0.5) * st.brightness);
                if (h.shade != 0)
                    lastColor = shaded(lastColor, h.shade);
            }
            double dist = h.dist;
            if (st.position > 0) {
                const uint32_t kh = uint32_t(h.key) ^ uint32_t(h.key >> 32) * 0x9e3779b9u;
                const double ox = (hashUnit(kh, 31u, st.seed) - 0.5) * 2 * st.position * st.size * 0.12;
                const double oy = (hashUnit(kh, 37u, st.seed) - 0.5) * 2 * st.position * st.size * 0.12;
                dist = std::max(0.0, dist + ox * h.nx + oy * h.ny);
            }
            const double inner = dist - halfGrout;
            QRgb c = lastColor;
            if (st.bevel > 0 && inner < st.bevel + 0.5) {
                const double facing = h.nx * lx + h.ny * ly;
                // Más fuerte hacia el borde: en curva (Vidriera) o en línea recta (Mosaico); los lados
                // a la luz se aclaran más de lo que se oscurecen los de la sombra (como en Paint.NET).
                // En chaflán (como en Paint.NET): la franja entera con el mismo tono, suavizada en su
                // borde de dentro; abombado, cada vez más fuerte hacia el borde.
                const double t = 1 - clamp01(inner / st.bevel);
                const double profile = st.pillow ? t * std::sqrt(t) : clamp01(st.bevel - inner + 0.5);
                c = shaded(c, facing * profile * (facing > 0 ? st.strength : st.shadow));
            }
            const double tileCov = st.grout > 0 ? clamp01(inner + 0.5) : 1.0;
            d[x] = qPremultiply(tileCov >= 1 ? c : mixStraight(st.groutColor, c, tileCov));
        }
    });
}

void renderMosaic(const RenderArgs& a)
{
    const int shape = ival(a.values, "shape");
    TileStyle st;
    st.size = std::max(2, ival(a.values, "size"));
    // La lechada y el bisel, en % del tamaño de la tesela (4 y 20: 1 y 5 px con teselas de 24).
    st.grout = st.size * ival(a.values, "grout") / 100.0;
    st.groutColor = cval(a.values, "groutcolor").rgba();
    st.bevel = st.size * 0.7 * ival(a.values, "bevel") / 100.0;
    st.strength = 0.45;
    st.shadow = 0.3;
    st.light = dval(a.values, "light");
    st.centerColor = ival(a.values, "tilecolor") == 1;
    st.brightness = ival(a.values, "brightness") / 100.0;
    st.position = ival(a.values, "position") / 100.0;
    st.seed = uint32_t(ival(a.values, "seed"));
    // El ancla mueve el origen de las teselas (en el centro de la imagen sin moverla).
    const QPointF off = pval(a.values, "anchor");
    const QPointF origin(a.roi.left() + a.roi.width() * (1 + off.x()) / 2.0, a.roi.top() + a.roi.height() * (1 + off.y()) / 2.0);
    const QRectF area = QRectF(a.roi).adjusted(-st.size * 3, -st.size * 3, st.size * 3, st.size * 3);
    const std::unique_ptr<Tiling> tiling = makeMosaicTiling(shape, area, origin, st.size, st.seed);
    renderTiles(a, *tiling, st);
}

void renderStainedGlass(const RenderArgs& a)
{
    TileStyle st;
    st.size = std::max(2, ival(a.values, "cell"));
    // El emplomado, algo más fino que el % del tamaño (como en Paint.NET); el cristal, con un
    // chaflán: casi blanco hacia la luz y gris al otro lado.
    st.grout = st.size * ival(a.values, "border") / 100.0 * 0.75;
    st.groutColor = cval(a.values, "lead").rgba();
    st.bevel = st.size * 0.38 * ival(a.values, "bevel") / 100.0;
    st.strength = 1.0;
    st.shadow = 0.4;
    st.pillow = false;
    st.light = dval(a.values, "light");
    st.centerColor = ival(a.values, "cellcolor") == 1;
    st.seed = uint32_t(ival(a.values, "seed"));
    const VoronoiTiling tiling(st.size, 1.0, st.seed, st.size * 0.03, QPointF(0, 0));
    renderTiles(a, tiling, st);
}

} // namespace

void registerPdn52(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };

    // Distorsionar
    add({"spherize", QObject::tr("Esferizar"), catDistort(), "effect-spherize", {},
         {P::real("amount", QObject::tr("Cantidad"), -100, 100, 50, 2), P::real("radius", QObject::tr("Radio"), 0, 100, 100, 2),
          P::offset("offset", QObject::tr("Centrar")), edgeParam(pdnEdges(), EdgeBehavior::Clamp), qualityParam(2)},
         renderSpherize});
    {
        P lobes = P::integer("lobes", QObject::tr("Lóbulos"), 1, 50, 6);
        lobes.readOnlyWhen = {{QStringLiteral("pattern"), wavesLobed()}};
        lobes.readOnlyInverse = true;
        P damping = P::real("damping", QObject::tr("Amortiguación"), 0, 100, 0, 2);
        damping.readOnlyWhen = {{QStringLiteral("pattern"), wavesDamped()}};
        damping.readOnlyInverse = true;
        P center = P::offset("offset", QObject::tr("Centrar"));
        center.readOnlyWhen = {{QStringLiteral("pattern"), wavesCentered()}};
        center.readOnlyInverse = true;
        add({"waves", QObject::tr("Ondas"), catDistort(), "effect-waves", {},
             {P::choice("pattern", QObject::tr("Patrón"),
                        {QObject::tr("Lineal"), QObject::tr("Radial"), QObject::tr("Compresión lineal"), QObject::tr("Tangencial"),
                         QObject::tr("Anillos cuadrados"), QObject::tr("Anillos de rombo"), QObject::tr("Cruzado"),
                         QObject::tr("Angular"), QObject::tr("Espiral"), QObject::tr("Desde los bordes")},
                        0),
              P::choice("form", QObject::tr("Forma de onda"),
                        {QObject::tr("Senoidal"), QObject::tr("Triangular"), QObject::tr("Diente de sierra"), QObject::tr("Cuadrada"),
                         QObject::tr("Semicírculo"), QObject::tr("Armónicos"), QObject::tr("Senoidal escalonada")},
                        0),
              P::real("amplitude", QObject::tr("Amplitud"), -50, 50, 10, 2), P::real("wavelength", QObject::tr("Longitud de onda"), 1, 40, 10, 2),
              lobes, damping, P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0), P::angle("phase", QObject::tr("Fase"), -180, 180, 0),
              center, edgeParam(pdnEdges(), EdgeBehavior::Reflect), qualityParam(2)},
             renderWaves});
    }

    // Color
    {
        P seed = P::seed();
        QVariantList ordered;
        for (int i = 0; i <= 7; ++i)
            ordered << i;
        seed.readOnlyWhen = {{QStringLiteral("matrix"), ordered}};
        add({"ordereddither", QObject::tr("Tramado ordenado"), catColor(), "effect-ordereddither", {},
             {P::choice("matrix", QObject::tr("Matriz"), ditherMatrixNames(), 2), P::integer("levels", QObject::tr("Niveles"), 2, 32, 4),
              P::integer("intensity", QObject::tr("Intensidad"), 0, 100, 100), seed},
             renderOrderedDither});
    }

    // Estilizado
    add({"halftone", QObject::tr("Semitono"), catStylize(), "effect-halftone", {},
         {P::choice("pattern", QObject::tr("Patrón"),
                    {QObject::tr("Puntos"), QObject::tr("Rombo"), QObject::tr("Elipse"), QObject::tr("Cuadrado"),
                     QObject::tr("Trama cruzada"), QObject::tr("Líneas")},
                    0),
          P::boolean("mono", QObject::tr("Monocromo"), false), P::real("cell", QObject::tr("Tamaño de celda"), 2, 200, 8, 2),
          P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0), P::integer("black", QObject::tr("Generación de negro"), 0, 100, 75),
          P::integer("softness", QObject::tr("Suavizado"), 0, 100, 0), P::integer("quality", QObject::tr("Calidad"), 1, 8, 4)},
         renderHalftone});

    // Artístico
    {
        const QString main = QObject::tr("Linograbado"), colors = QObject::tr("Colores");
        QList<P> params{P::choice("mode", QObject::tr("Modo de línea"),
                                  {QObject::tr("Paralelas"), QObject::tr("Contorno"), QObject::tr("Sombreado cruzado")}, 0),
                        P::angle("angle", QObject::tr("Ángulo"), -180, 180, 45),
                        P::real("spacing", QObject::tr("Espaciado de líneas"), 2, 20, 8, 1),
                        P::real("contrast", QObject::tr("Contraste"), 0.1, 2.1, 1, 2),
                        P::real("gamma", QObject::tr("Gamma"), 0.1, 2.1, 1, 2),
                        P::integer("roughness", QObject::tr("Aspereza"), 0, 100, 20), P::seed()};
        for (P& p : params)
            p.tab = main;
        P ink = P::color("ink", QObject::tr("Color de tinta"), P::ColorDefault::Black).inlineColor(false);
        ink.tab = colors;
        P paper = P::color("paper", QObject::tr("Color de papel"), P::ColorDefault::White).inlineColor(false);
        paper.defaultValue = QColor(245, 242, 233);
        paper.tab = colors;
        params << ink << paper;
        add({"linocut", main, catArtistic(), "effect-linocut", {}, params, renderLinocut});
    }
    {
        const QString main = QObject::tr("Mosaico"), more = QObject::tr("Ajustes");
        P grout = P::color("groutcolor", QObject::tr("Color de lechada"), P::ColorDefault::Black).inlineColor(false);
        grout.defaultValue = QColor(128, 128, 128);
        QList<P> first{P::choice("shape", QObject::tr("Forma de tesela"), mosaicShapeNames(), 0),
                       P::integer("size", QObject::tr("Tamaño de tesela"), 4, 200, 24),
                       P::integer("bevel", QObject::tr("Profundidad de bisel"), 0, 100, 20),
                       P::integer("grout", QObject::tr("Ancho de lechada"), 0, 40, 4), grout};
        for (P& p : first)
            p.tab = main;
        QList<P> second{P::choice("tilecolor", QObject::tr("Color de tesela"), {QObject::tr("Promedio"), QObject::tr("Píxel central")}, 0),
                        P::angle("light", QObject::tr("Ángulo de luz"), -180, 180, 135),
                        P::integer("brightness", QObject::tr("Variación de luminosidad"), 0, 100, 15),
                        P::integer("position", QObject::tr("Variación de posición"), 0, 50, 10),
                        P::offset("anchor", QObject::tr("Ancla"))};
        for (P& p : second)
            p.tab = more;
        add({"mosaic", main, catArtistic(), "effect-mosaic", {}, first + second + QList<P>{P::seed()}, renderMosaic});
    }
    {
        P bg = P::color("background", QObject::tr("Color de fondo"), P::ColorDefault::White).inlineColor(true);
        add({"pointillism", QObject::tr("Puntillismo"), catArtistic(), "effect-pointillism", {},
             {P::integer("cell", QObject::tr("Tamaño de celda"), 2, 64, 12), P::integer("fill", QObject::tr("% de relleno de celda"), 10, 200, 141),
              P::integer("tonal", QObject::tr("Respuesta tonal"), 0, 100, 100),
              P::integer("placement", QObject::tr("Variación de colocación"), 0, 100, 100),
              P::integer("colorvar", QObject::tr("Variación de color"), 0, 100, 25), bg, P::seed()},
             renderPointillism});
    }
    add({"stainedglass", QObject::tr("Vidriera"), catArtistic(), "effect-stainedglass", {},
         {P::integer("cell", QObject::tr("Tamaño de celda"), 2, 200, 20),
          P::choice("cellcolor", QObject::tr("Color de celda"), {QObject::tr("Promedio"), QObject::tr("Píxel central")}, 0),
          P::integer("border", QObject::tr("% de ancho de borde"), 0, 100, 15),
          P::integer("bevel", QObject::tr("Profundidad de bisel"), 0, 100, 50), P::angle("light", QObject::tr("Ángulo de luz"), -180, 180, 135),
          P::color("lead", QObject::tr("Color del emplomado"), P::ColorDefault::Black).inlineColor(false), P::seed()},
         renderStainedGlass});
}

} // namespace fx
