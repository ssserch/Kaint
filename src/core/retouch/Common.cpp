// Retoque de PhotoCraft (Retouch.h): rectángulos, máscaras, distancias, la fusión de Poisson y la
// curación de los pinceles correctores. Algoritmos de PhotoCraft (licencia MIT).

#include "core/retouch/Retouch.h"

#include "core/Parallel.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace retouch {

namespace {

int sat(long long v)
{
    return int(std::clamp<long long>(v, INT_MIN, INT_MAX));
}

} // namespace

Box Box::intersect(const Box& o) const
{
    return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
}

Box Box::unite(const Box& o) const
{
    if (empty())
        return o;
    if (o.empty())
        return *this;
    return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
}

Box Box::inflate(int d) const
{
    return {sat((long long)x0 - d), sat((long long)y0 - d), sat((long long)x1 + d), sat((long long)y1 + d)};
}

Box Box::translate(int dx, int dy) const
{
    return {sat((long long)x0 + dx), sat((long long)y0 + dy), sat((long long)x1 + dx), sat((long long)y1 + dy)};
}

// ---------------------------------------------------------------------------------------------

Mask dilate(const Mask& m, int w, int h, int r)
{
    if (r <= 0 || m.size() != size_t(w) * h)
        return m;
    // Cada pasada marca la celda si alguna a 'r' o menos en un eje lo está (con un contador).
    auto pass = [&](const Mask& src, bool horizontal) {
        const int lines = horizontal ? h : w;
        const int len = horizontal ? w : h;
        auto at = [&](int line, int k) { return horizontal ? size_t(line) * w + k : size_t(k) * w + line; };
        Mask out(src.size(), 0);
        for (int line = 0; line < lines; ++line) {
            int count = 0;
            for (int k = 0; k < std::min(len, r); ++k)
                count += src[at(line, k)];
            for (int k = 0; k < len; ++k) {
                if (k + r < len)
                    count += src[at(line, k + r)];
                if (k > r)
                    count -= src[at(line, k - r - 1)];
                out[at(line, k)] = count > 0;
            }
        }
        return out;
    };
    return pass(pass(m, true), false);
}

void closeLoops(Mask& m, int w, int h)
{
    if (w <= 0 || h <= 0 || m.size() != size_t(w) * h)
        return;
    Mask outside(m.size(), 0);
    std::vector<size_t> stack;
    auto visit = [&](size_t i) {
        if (!m[i] && !outside[i]) {
            outside[i] = 1;
            stack.push_back(i);
        }
    };
    for (int x = 0; x < w; ++x) {
        visit(static_cast<size_t>(x));
        visit(size_t(h - 1) * w + x);
    }
    for (int y = 0; y < h; ++y) {
        visit(size_t(y) * w);
        visit(size_t(y) * w + w - 1);
    }
    while (!stack.empty()) {
        const size_t i = stack.back();
        stack.pop_back();
        const int x = int(i % w), y = int(i / w);
        if (x > 0)
            visit(i - 1);
        if (x + 1 < w)
            visit(i + 1);
        if (y > 0)
            visit(i - w);
        if (y + 1 < h)
            visit(i + w);
    }
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = !outside[i];
}

uint32_t holeRadius(int w, int h, const Mask& hole)
{
    if (hole.size() != size_t(w) * h)
        return 0;
    std::vector<uint32_t> d(hole.size());
    for (size_t i = 0; i < hole.size(); ++i)
        d[i] = hole[i] ? UINT32_MAX : 0;
    auto relax = [&](size_t i, size_t j) {
        const uint32_t v = d[j] == UINT32_MAX ? UINT32_MAX : d[j] + 1;
        if (v < d[i])
            d[i] = v;
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            if (x > 0)
                relax(i, i - 1);
            if (y > 0) {
                relax(i, i - w);
                if (x > 0)
                    relax(i, i - w - 1);
                if (x + 1 < w)
                    relax(i, i - w + 1);
            }
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            const size_t i = size_t(y) * w + x;
            if (x + 1 < w)
                relax(i, i + 1);
            if (y + 1 < h) {
                relax(i, i + w);
                if (x + 1 < w)
                    relax(i, i + w + 1);
                if (x > 0)
                    relax(i, i + w - 1);
            }
        }
    uint32_t best = 0;
    for (size_t i = 0; i < hole.size(); ++i)
        if (hole[i])
            best = std::max(best, d[i]);
    return best;
}

Box removeWindow(Box stroke, Box canvas)
{
    stroke = stroke.intersect(canvas);
    if (stroke.empty())
        return {};
    const long long ext = std::max(stroke.width(), stroke.height());
    const int margin = std::max(int(std::min<long long>(ext * 3 / 4, INT_MAX)), 32);
    return stroke.inflate(std::min(margin, 512)).intersect(canvas);
}

Box samplingWindow(Box hole, Box canvas)
{
    hole = hole.intersect(canvas);
    if (hole.empty() || canvas.empty())
        return {};
    const double w = std::max(hole.width(), 50), h = std::max(hole.height(), 50);
    const long long side = (long long)std::min(std::round(4.0 * std::sqrt(w * h)), double(INT_MAX));
    // El doble del centro, para que los tamaños impares sean exactos.
    auto span = [side](long long c2, long long extent, long long lo, long long hi) -> std::pair<int, int> {
        const long long s = std::max(side, extent);
        if (s >= hi - lo)
            return {int(lo), int(hi)};
        const long long a = std::clamp((c2 - s) / 2, lo, hi - s);
        return {int(a), int(a + s)};
    };
    const auto [x0, x1] = span((long long)hole.x0 + hole.x1, hole.width(), canvas.x0, canvas.x1);
    const auto [y0, y1] = span((long long)hole.y0 + hole.y1, hole.height(), canvas.y0, canvas.y1);
    return {x0, y0, x1, y1};
}

namespace {

constexpr uint32_t kNoSeed = UINT32_MAX;
constexpr float kNoDistance = 1e10f;

// Envolvente inferior de parábolas de una fila (Felzenszwalb y Huttenlocher).
void rowDistance(const uint32_t* costs, float* out, int w, std::vector<int>& sites, std::vector<double>& starts)
{
    int count = 0;
    for (int q = 0; q < w; ++q) {
        if (costs[q] == kNoSeed)
            continue;
        const double fq = double(costs[q]) * costs[q];
        double start = -INFINITY;
        while (count > 0) {
            const int p = sites[size_t(count - 1)];
            const double dp = costs[p];
            start = (fq - dp * dp) / (2.0 * (q - p)) + (double(q) + p) * 0.5;
            if (start > starts[size_t(count - 1)])
                break;
            --count;
        }
        if (count == 0)
            start = -INFINITY;
        sites[size_t(count)] = q;
        starts[size_t(count)] = start;
        ++count;
    }
    if (count == 0) {
        std::fill(out, out + w, kNoDistance);
        return;
    }
    int k = 0;
    for (int x = 0; x < w; ++x) {
        while (k + 1 < count && starts[size_t(k + 1)] < double(x))
            ++k;
        const int site = sites[size_t(k)];
        const double dx = double(x) - site;
        const double v = costs[site];
        out[x] = float(std::sqrt(dx * dx + v * v));
    }
}

} // namespace

Floats distanceTransform(const Mask& inside, int w, int h)
{
    const size_t n = size_t(w) * h;
    if (n == 0 || inside.size() != n)
        return {};
    if (std::none_of(inside.begin(), inside.end(), [](uint8_t v) { return v; }))
        return Floats(n, kNoDistance);
    if (std::all_of(inside.begin(), inside.end(), [](uint8_t v) { return v; }))
        return Floats(n, 0.f);
    // Semilla más cercana por columnas.
    std::vector<uint32_t> vertical(n, kNoSeed); // por columnas: x * h + y
    parallelRows(0, w, [&](int c0, int c1) {
        for (int x = c0; x < c1; ++x) {
            uint32_t* col = vertical.data() + size_t(x) * h;
            int nearest = -1;
            for (int y = 0; y < h; ++y) {
                if (inside[size_t(y) * w + x])
                    nearest = y;
                if (nearest >= 0)
                    col[y] = uint32_t(y - nearest);
            }
            nearest = -1;
            for (int y = h - 1; y >= 0; --y) {
                if (col[y] == 0)
                    nearest = y;
                if (nearest >= 0)
                    col[y] = std::min(col[y], uint32_t(nearest - y));
            }
        }
    }, 16);
    Floats out(n, 0.f);
    parallelRows(0, h, [&](int y0, int y1) {
        std::vector<uint32_t> costs(static_cast<size_t>(w));
        std::vector<int> sites(static_cast<size_t>(w));
        std::vector<double> starts(static_cast<size_t>(w));
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < w; ++x)
                costs[size_t(x)] = vertical[size_t(x) * h + y];
            rowDistance(costs.data(), out.data() + size_t(y) * w, w, sites, starts);
        }
    }, 32);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Poisson (Pérez, Gangnet y Blake): la membrana armónica, resuelta en cascada (a mitad de
// resolución, interpolada como punto de partida y unas pocas pasadas de SOR por nivel).

namespace {

constexpr float kTol = 2e-5f;
constexpr int kFineIters = 30;
constexpr float kFineOmega = 1.7f;

void sor(int w, int h, const Mask& unknown, Floats& v, int maxIter, float omega, const Stop& stop)
{
    struct Cell {
        uint32_t i;
        uint8_t m;
    };
    std::vector<Cell> cells;
    for (size_t i = 0; i < size_t(w) * h; ++i)
        if (unknown[i]) {
            const int x = int(i % w), y = int(i / w);
            const uint8_t m = uint8_t((x > 0) | ((x + 1 < w) << 1) | ((y > 0) << 2) | ((y + 1 < h) << 3));
            cells.push_back({uint32_t(i), m});
        }
    for (int it = 0; it < maxIter; ++it) {
        if (stop.requested())
            return;
        float maxD = 0.f;
        for (const Cell& c : cells) {
            const size_t i = c.i;
            float s, n;
            if (c.m == 0b1111) {
                s = v[i - 1] + v[i + 1] + v[i - w] + v[i + w];
                n = 4.f;
            } else {
                s = 0.f;
                n = 0.f;
                if (c.m & 1) {
                    s += v[i - 1];
                    n += 1.f;
                }
                if (c.m & 2) {
                    s += v[i + 1];
                    n += 1.f;
                }
                if (c.m & 4) {
                    s += v[i - w];
                    n += 1.f;
                }
                if (c.m & 8) {
                    s += v[i + w];
                    n += 1.f;
                }
                if (n == 0.f)
                    continue;
            }
            const float d = omega * (s / n - v[i]);
            v[i] += d;
            maxD = std::max(maxD, std::fabs(d));
        }
        if (maxD < kTol)
            break;
    }
}

void solveRec(int w, int h, const Mask& unknown, Floats& v, int depth, const Stop& stop)
{
    if (stop.requested())
        return;
    size_t nUnknown = 0;
    for (uint8_t u : unknown)
        nUnknown += u;
    if (nUnknown == 0)
        return;
    std::vector<float> known;
    known.reserve(v.size() - nUnknown);
    for (size_t i = 0; i < v.size(); ++i)
        if (!unknown[i])
            known.push_back(v[i]);
    if (known.empty())
        return; // nada desde lo que interpolar
    if (w >= 8 && h >= 8 && nUnknown > 64 && depth < 16) {
        const int cw = (w + 1) / 2, chh = (h + 1) / 2;
        Mask cu(size_t(cw) * chh, 1);
        Floats cv(size_t(cw) * chh, 0.f);
        for (int cy = 0; cy < chh; ++cy)
            for (int cx = 0; cx < cw; ++cx) {
                float s = 0.f;
                unsigned n = 0;
                static const int d[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
                for (const auto& o : d) {
                    const int x = cx * 2 + o[0], y = cy * 2 + o[1];
                    if (x < w && y < h && !unknown[size_t(y) * w + x]) {
                        s += v[size_t(y) * w + x];
                        ++n;
                    }
                }
                if (n > 0) {
                    cu[size_t(cy) * cw + cx] = 0;
                    cv[size_t(cy) * cw + cx] = s / float(n);
                }
            }
        solveRec(cw, chh, cu, cv, depth + 1, stop);
        for (int y = 0; y < h; ++y) {
            const float fy = std::clamp((float(y) + 0.5f) / 2.f - 0.5f, 0.f, float(chh - 1));
            const int y0 = int(std::floor(fy));
            const int y1 = std::min(y0 + 1, chh - 1);
            const float ty = fy - float(y0);
            for (int x = 0; x < w; ++x) {
                if (!unknown[size_t(y) * w + x])
                    continue;
                const float fx = std::clamp((float(x) + 0.5f) / 2.f - 0.5f, 0.f, float(cw - 1));
                const int x0 = int(std::floor(fx));
                const int x1 = std::min(x0 + 1, cw - 1);
                const float tx = fx - float(x0);
                const float a = cv[size_t(y0) * cw + x0] + (cv[size_t(y0) * cw + x1] - cv[size_t(y0) * cw + x0]) * tx;
                const float b = cv[size_t(y1) * cw + x0] + (cv[size_t(y1) * cw + x1] - cv[size_t(y1) * cw + x0]) * tx;
                v[size_t(y) * w + x] = a + (b - a) * ty;
            }
        }
        sor(w, h, unknown, v, kFineIters, kFineOmega, stop);
    } else {
        float sum = 0.f;
        for (float k : known)
            sum += k;
        const float mean = sum / float(known.size());
        for (size_t i = 0; i < v.size(); ++i)
            if (unknown[i])
                v[i] = mean;
        sor(w, h, unknown, v, 2000, 1.85f, stop);
    }
}

// f(c) por canal (en paralelo con imágenes grandes), entrelazado al final.
template <typename F>
Floats perChannel(int w, int h, int ch, F f)
{
    std::vector<Floats> planes(static_cast<size_t>(ch));
    if (size_t(w) * h >= 64 * 64)
        parallelRows(0, ch, [&](int c0, int c1) {
            for (int c = c0; c < c1; ++c)
                planes[size_t(c)] = f(c);
        }, 1);
    else
        for (int c = 0; c < ch; ++c)
            planes[size_t(c)] = f(c);
    Floats out(size_t(w) * h * ch, 0.f);
    for (int c = 0; c < ch; ++c)
        for (size_t i = 0; i < size_t(w) * h; ++i)
            out[i * ch + c] = planes[size_t(c)][i];
    return out;
}

} // namespace

void solveMembrane(int w, int h, const Mask& unknown, Floats& v, const Stop& stop)
{
    if (unknown.size() != size_t(w) * h || v.size() != size_t(w) * h)
        return;
    solveRec(w, h, unknown, v, 0, stop);
}

Floats seamlessClone(int w, int h, int ch, const Floats& src, const Floats& dst, const Mask& mask, const Stop& stop)
{
    stop.check();
    const size_t n = size_t(w) * h;
    Floats out = perChannel(w, h, ch, [&](int c) {
        Floats hv(n);
        for (size_t i = 0; i < n; ++i)
            hv[i] = mask[i] ? 0.f : dst[i * ch + c] - src[i * ch + c];
        solveRec(w, h, mask, hv, 0, stop);
        Floats r(n);
        for (size_t i = 0; i < n; ++i)
            r[i] = mask[i] ? src[i * ch + c] + hv[i] : dst[i * ch + c];
        return r;
    });
    stop.check();
    return out;
}

Floats membraneFill(int w, int h, int ch, const Floats& img, const Mask& hole, const Stop& stop)
{
    stop.check();
    const size_t n = size_t(w) * h;
    Floats out = perChannel(w, h, ch, [&](int c) {
        Floats v(n);
        for (size_t i = 0; i < n; ++i)
            v[i] = hole[i] ? 0.f : img[i * ch + c];
        if (hole.size() == n)
            solveRec(w, h, hole, v, 0, stop);
        return v;
    });
    stop.check();
    return out;
}

// ---------------------------------------------------------------------------------------------
// La curación.

Floats toFloats(const Bytes& rgba)
{
    Floats f(rgba.size());
    for (size_t i = 0; i < rgba.size(); ++i)
        f[i] = float(rgba[i]) / 255.f;
    return f;
}

static inline uint8_t byteOf(float v)
{
    // ×255 y redondeado hacia arriba en la mitad; negativo (o NaN) 0, de más 255.
    const float t = v * 255.f + 0.5f;
    return t >= 255.f ? 255 : t > 0.f ? uint8_t(t) : 0;
}

Bytes toBytes(const Floats& v)
{
    Bytes b(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        b[i] = byteOf(v[i]);
    return b;
}

Bytes Heal::run(const Stop& stop) const
{
    Bytes out = dst;
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (cov[size_t(y) * w + x] > 0) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1);
                y1 = std::max(y1, y + 1);
            }
    if (x0 >= x1)
        return out;
    // La solución solo sobre la cobertura y un píxel más (el resto es 'dst').
    x0 = std::max(x0 - 1, 0);
    y0 = std::max(y0 - 1, 0);
    x1 = std::min(x1 + 1, w);
    y1 = std::min(y1 + 1, h);
    const int cw = x1 - x0, chh = y1 - y0;
    Floats s(size_t(cw) * chh * 4), d(size_t(cw) * chh * 4);
    Mask m(size_t(cw) * chh);
    for (int y = 0; y < chh; ++y)
        for (int x = 0; x < cw; ++x) {
            const size_t from = size_t(y + y0) * w + x + x0, to = size_t(y) * cw + x;
            m[to] = cov[from] > 0;
            for (int k = 0; k < 4; ++k) {
                s[to * 4 + k] = float(src[from * 4 + k]) / 255.f;
                d[to * 4 + k] = float(dst[from * 4 + k]) / 255.f;
            }
        }
    const Floats healed = seamlessClone(cw, chh, 4, s, d, m, stop);
    for (int y = 0; y < chh; ++y)
        for (int x = 0; x < cw; ++x) {
            const size_t from = size_t(y) * cw + x, to = size_t(y + y0) * w + x + x0;
            for (int k = 0; k < 4; ++k)
                out[to * 4 + k] = byteOf(healed[from * 4 + k]);
        }
    return out;
}

Bytes Heal::runCoarse(int step, const Stop& stop) const
{
    if (step <= 1)
        return run(stop);
    constexpr int N = 4;
    const int cw = (w + step - 1) / step, chh = (h + step - 1) / step;
    auto f = [](uint8_t v) { return float(v) / 255.f; };
    // Una celda es desconocida si todos sus píxeles se parchean; si no, guarda la diferencia media
    // dst - src de los que no (los datos de Dirichlet del problema grueso).
    Floats diff(size_t(cw) * chh * N, 0.f);
    Mask hole(size_t(cw) * chh, 1);
    std::vector<uint32_t> count(size_t(cw) * chh, 0);
    for (int y = 0; y < h; ++y) {
        stop.check();
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            if (cov[i] > 0)
                continue;
            const size_t c = size_t(y / step) * cw + x / step;
            hole[c] = 0;
            ++count[c];
            for (int k = 0; k < N; ++k)
                diff[c * N + k] += f(dst[i * N + k]) - f(src[i * N + k]);
        }
    }
    for (size_t c = 0; c < count.size(); ++c)
        if (count[c] > 0)
            for (int k = 0; k < N; ++k)
                diff[c * N + k] /= float(count[c]);
    const Floats membrane = membraneFill(cw, chh, N, diff, hole, stop);
    struct Lerp {
        int c0, c1;
        float t;
    };
    auto lerpAt = [step](int p, int cells) {
        const float fv = std::clamp((float(p) + 0.5f) / float(step) - 0.5f, 0.f, float(cells - 1));
        const int c0 = int(std::floor(fv));
        return Lerp{c0, std::min(c0 + 1, cells - 1), fv - float(c0)};
    };
    std::vector<Lerp> xs(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x)
        xs[size_t(x)] = lerpAt(x, cw);
    Bytes out = dst;
    parallelRows(0, h, [&](int r0, int r1) {
        for (int y = r0; y < r1; ++y) {
            if (stop.requested())
                return;
            const Lerp ly = lerpAt(y, chh);
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                if (cov[i] == 0)
                    continue;
                const Lerp lx = xs[size_t(x)];
                for (int k = 0; k < N; ++k) {
                    auto v = [&](int cx, int cy) { return membrane[(size_t(cy) * cw + cx) * N + k]; };
                    const float m = (v(lx.c0, ly.c0) * (1.f - lx.t) + v(lx.c1, ly.c0) * lx.t) * (1.f - ly.t)
                                  + (v(lx.c0, ly.c1) * (1.f - lx.t) + v(lx.c1, ly.c1) * lx.t) * ly.t;
                    out[i * N + k] = byteOf(f(src[i * N + k]) + m);
                }
            }
        }
    });
    stop.check();
    return out;
}

} // namespace retouch
