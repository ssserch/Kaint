// Retoque de PhotoCraft (Retouch.h): relleno de huecos para Relleno según el contenido y el Pincel
// corrector puntual. Algoritmos de PhotoCraft (licencia MIT).
//
// · completeMasked: el completado espacio-temporal multiescala de Wexler, Shechtman e Irani (EM:
//   para cada parche que toca el hueco, el más parecido de fuera, y después cada píxel del hueco es
//   el voto ponderado de los parches que lo cubren), con el campo de vecinos de PatchMatch (Barnes
//   et al.: inicio aleatorio, propagación y búsqueda aleatoria que se encoge). De lo grueso a lo
//   fino: el nivel más grueso empieza rellenando el hueco anillo a anillo desde el borde; cada
//   nivel más fino parte de las coincidencias del anterior. Con ajuste de color, cada parche
//   copiado puede escalarse y desplazarse por canal (Image Melding, Darabi et al.).
// · bestOffset: el desplazamiento cuyo anillo alrededor mejor coincide con el del hueco.
// · synthesize: síntesis de textura por bloques (Efros y Freeman) con cortes de error mínimo.

#include "core/retouch/Retouch.h"

#include "core/Parallel.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <climits>
#include <cmath>
#include <limits>

namespace retouch {

namespace {

constexpr size_t kParMin = 64 * 64;
constexpr size_t kParMinPatches = 16 * 16;
constexpr size_t kSourceMomentsMax = size_t(1) << 20;
constexpr int kMomCh = 8;
constexpr float kFlat = 0.02f;
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr int kNoMatch = INT_MIN;

struct Bounds {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Bounds grow(int r, int w, int h) const
    {
        return {std::max(x0 - r, 0), std::max(y0 - r, 0), std::min(x1 + r, w), std::min(y1 + r, h)};
    }
    Bounds half() const { return {x0 / 2, y0 / 2, (x1 + 1) / 2, (y1 + 1) / 2}; }
    int width() const { return std::max(x1 - x0, 0); }
    int height() const { return std::max(y1 - y0, 0); }
    size_t area() const { return size_t(width()) * height(); }
};

struct Level {
    int w = 0, h = 0;
    Floats img;
    std::vector<Px> px;
};

struct Pt {
    int x, y;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Pt& o) const { return !(*this == o); }
};

// En paralelo por bloques de índices [0, n): f(i0, i1).
template <typename F>
void parallelFor(size_t n, bool par, size_t grain, F f)
{
    if (!par || n <= grain) {
        f(size_t(0), n);
        return;
    }
    const int blocks = int((n + grain - 1) / grain);
    parallelRows(0, blocks, [&](int b0, int b1) { f(size_t(b0) * grain, std::min(n, size_t(b1) * grain)); }, 1);
}

Level downsample(const Level& l, int ch)
{
    Level out;
    out.w = (l.w + 1) / 2;
    out.h = (l.h + 1) / 2;
    out.img.assign(size_t(out.w) * out.h * ch, 0.f);
    out.px.assign(size_t(out.w) * out.h, Px::Void);
    parallelRows(0, out.h, [&](int r0, int r1) {
        for (int y = r0; y < r1; ++y)
            for (int x = 0; x < out.w; ++x) {
                float n = 0.f;
                float* o = out.img.data() + (size_t(y) * out.w + x) * ch;
                bool hole = false, allSource = true, any = false;
                static const int d[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
                for (const auto& dd : d) {
                    const int sx = x * 2 + dd[0], sy = y * 2 + dd[1];
                    if (sx >= l.w || sy >= l.h)
                        continue;
                    const size_t si = size_t(sy) * l.w + sx;
                    switch (l.px[si]) {
                    case Px::Hole: hole = true; break;
                    case Px::Void: allSource = false; break;
                    default:
                        any = true;
                        allSource = allSource && l.px[si] == Px::Source;
                        for (int c = 0; c < ch; ++c)
                            o[c] += l.img[si * ch + c];
                        n += 1.f;
                        break;
                    }
                }
                if (n > 0.f)
                    for (int c = 0; c < ch; ++c)
                        o[c] /= n;
                out.px[size_t(y) * out.w + x] = hole ? Px::Hole : !any ? Px::Void : allSource ? Px::Source : Px::Known;
            }
    });
    return out;
}

std::vector<uint32_t> integral(int w, int h, const Mask& m)
{
    std::vector<uint32_t> s(size_t(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        uint32_t row = 0;
        for (int x = 0; x < w; ++x) {
            row += m[size_t(y) * w + x];
            s[size_t(y + 1) * (w + 1) + x + 1] = s[size_t(y) * (w + 1) + x + 1] + row;
        }
    }
    return s;
}

uint32_t windowCount(const std::vector<uint32_t>& s, int gw, int gh, int x0, int y0, int x1, int y1)
{
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(std::max(x1, 0), gw);
    y1 = std::min(std::max(y1, 0), gh);
    if (x1 <= x0 || y1 <= y0)
        return 0;
    const size_t w1 = size_t(gw) + 1;
    return s[size_t(y1) * w1 + x1] + s[size_t(y0) * w1 + x0] - s[size_t(y0) * w1 + x1] - s[size_t(y1) * w1 + x0];
}

std::optional<Bounds> holeBBox(int w, int h, const Mask& hole)
{
    std::optional<Bounds> b;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (hole[size_t(y) * w + x]) {
                if (!b)
                    b = Bounds{x, y, x + 1, y + 1};
                else
                    b = Bounds{std::min(b->x0, x), std::min(b->y0, y), std::max(b->x1, x + 1), std::max(b->y1, y + 1)};
            }
    return b;
}

// Un hueco fino (un cable, un pelo: largo pero de pocos parches de grosor) se mide por su grosor.
constexpr int kThinRatio = 4;

int holeThickness(int w, Bounds hb, const std::vector<Px>& px)
{
    const int gw = hb.width() + 2, gh = hb.height() + 2;
    Mask outside(size_t(gw) * gh);
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            bool out;
            if (gx == 0 || gy == 0 || gx > hb.width() || gy > hb.height())
                out = true;
            else
                out = px[size_t(hb.y0 + gy - 1) * w + hb.x0 + gx - 1] != Px::Hole;
            outside[size_t(gy) * gw + gx] = out;
        }
    float deepest = 0.f;
    for (float d : distanceTransform(outside, gw, gh))
        deepest = std::max(deepest, d);
    return int(std::ceil(2.f * deepest));
}

constexpr size_t kPeelBudget = size_t(1) << 28;

bool peelAffordable(int w, const std::vector<Px>& px, Bounds hb, size_t sources)
{
    size_t holes = 0;
    for (int y = hb.y0; y < hb.y1; ++y)
        for (int x = hb.x0; x < hb.x1; ++x)
            holes += px[size_t(y) * w + x] == Px::Hole;
    return holes * sources <= kPeelBudget; // (desbordarse aquí exigiría huecos imposibles)
}

std::optional<Bounds> holeBounds(int w, int h, const std::vector<Px>& px)
{
    std::optional<Bounds> b;
    for (int y = 0; y < h; ++y) {
        const Px* row = px.data() + size_t(y) * w;
        int x0 = -1, x1 = -1;
        for (int x = 0; x < w; ++x)
            if (row[x] == Px::Hole) {
                if (x0 < 0)
                    x0 = x;
                x1 = x + 1;
            }
        if (x0 < 0)
            continue;
        if (!b)
            b = Bounds{x0, y, x1, y + 1};
        else
            b = Bounds{std::min(b->x0, x0), b->y0, std::max(b->x1, x1), y + 1};
    }
    return b;
}

// Los centros de parche que se pueden copiar: el parche entero son píxeles Source.
Mask validSources(int w, int h, int r, const std::vector<Px>& px, const Stop& stop)
{
    constexpr int kBand = 64;
    const int side = 2 * r + 1;
    const bool par = size_t(w) * h >= kParMin;
    Mask run(size_t(w) * h, 0);
    if (w < side || h < side)
        return run;
    auto runRows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            if (y % kBand == 0 && stop.requested())
                return;
            const Px* row = px.data() + size_t(y) * w;
            uint8_t* out = run.data() + size_t(y) * w;
            auto copyable = [&](int x) { return int(row[x] == Px::Source); };
            int count = 0;
            for (int x = 0; x < side; ++x)
                count += copyable(x);
            out[r] = count == side;
            for (int x = r + 1; x < w - r; ++x) {
                count = count + copyable(x + r) - copyable(x - r - 1);
                out[x] = count == side;
            }
        }
    };
    if (par)
        parallelRows(0, h, runRows, 16);
    else
        runRows(0, h);
    stop.check();
    Mask valid(size_t(w) * h, 0);
    const int bands = (h + kBand - 1) / kBand;
    auto bandRows = [&](int b0, int b1) {
        for (int band = b0; band < b1; ++band) {
            if (stop.requested())
                return;
            const int y0 = band * kBand, rows = std::min(kBand, h - y0);
            std::vector<int> stack(size_t(w), 0);
            for (int yy = std::max(y0 - r, 0); yy < std::min(y0 + rows + r, h); ++yy) {
                for (int x = 0; x < w; ++x)
                    stack[size_t(x)] = run[size_t(yy) * w + x] ? stack[size_t(x)] + 1 : 0;
                if (yy < y0 + r || yy < 2 * r)
                    continue;
                const int y = yy - r;
                for (int x = r; x < w - r; ++x)
                    valid[size_t(y) * w + x] = stack[size_t(x)] >= side;
            }
        }
    };
    if (par)
        parallelRows(0, bands, bandRows, 1);
    else
        bandRows(0, bands);
    stop.check();
    return valid;
}

// Las fuentes válidas, para sacar la k-ésima en orden de barrido sin guardarlas todas.
constexpr int kSourceBlock = 64;

struct Sources {
    std::vector<size_t> rowStart;
    std::vector<uint32_t> blockStart;
    int blocks = 0;

    Sources(const Mask& valid, int w)
    {
        const int h = w > 0 ? int(valid.size() / size_t(w)) : 0;
        blocks = (w + kSourceBlock - 1) / kSourceBlock;
        blockStart.assign(size_t(h) * blocks, 0);
        std::vector<size_t> counts(size_t(h), 0);
        for (int y = 0; y < h; ++y) {
            uint32_t n = 0;
            for (int b = 0; b < blocks; ++b) {
                blockStart[size_t(y) * blocks + b] = n;
                const int xe = std::min((b + 1) * kSourceBlock, w);
                for (int x = b * kSourceBlock; x < xe; ++x)
                    n += valid[size_t(y) * w + x];
            }
            counts[size_t(y)] = n;
        }
        rowStart.reserve(counts.size() + 1);
        size_t total = 0;
        rowStart.push_back(0);
        for (size_t c : counts) {
            total += c;
            rowStart.push_back(total);
        }
    }
    size_t size() const { return rowStart.empty() ? 0 : rowStart.back(); }
    Pt nth(size_t k, const Mask& valid, int w) const
    {
        // La fila: la última cuyo comienzo es ≤ k.
        const auto it = std::upper_bound(rowStart.begin(), rowStart.end(), k);
        const size_t y = it == rowStart.begin() ? 0 : size_t(it - rowStart.begin()) - 1;
        size_t left = k - std::min(k, y < rowStart.size() ? rowStart[y] : 0);
        const uint32_t* rb = blockStart.data() + y * blocks;
        const auto bt = std::upper_bound(rb, rb + blocks, uint32_t(left));
        const int b = bt == rb ? 0 : int(bt - rb) - 1;
        left -= std::min<size_t>(left, rb[b]);
        const int x0 = b * kSourceBlock;
        for (int x = x0; x < w; ++x)
            if (valid[y * w + x]) {
                if (left == 0)
                    return {x, int(y)};
                --left;
            }
        return {0, 0};
    }
    std::vector<Pt> all(const Mask& valid, int w) const
    {
        std::vector<Pt> out;
        out.reserve(size());
        for (size_t i = 0; i < valid.size(); ++i)
            if (valid[i])
                out.push_back({int(i % size_t(w)), int(i / size_t(w))});
        return out;
    }
};

// Los centros de parche (dentro de 'tb') cuyo parche toca el hueco.
Mask targetsAround(int w, int r, const std::vector<Px>& px, Bounds hb, Bounds tb)
{
    const int bw = hb.width(), bh = hb.height();
    std::vector<uint32_t> s(size_t(bw + 1) * (bh + 1), 0);
    for (int y = 0; y < bh; ++y) {
        uint32_t row = 0;
        for (int x = 0; x < bw; ++x) {
            row += px[size_t(hb.y0 + y) * w + hb.x0 + x] == Px::Hole;
            s[size_t(y + 1) * (bw + 1) + x + 1] = s[size_t(y) * (bw + 1) + x + 1] + row;
        }
    }
    Mask targets(tb.area(), 0);
    for (int y = tb.y0; y < tb.y1; ++y)
        for (int x = tb.x0; x < tb.x1; ++x) {
            const size_t k = size_t(y - tb.y0) * tb.width() + (x - tb.x0);
            targets[k] = windowCount(s, bw, bh, x - r - hb.x0, y - r - hb.y0, x + r + 1 - hb.x0, y + r + 1 - hb.y0) > 0;
        }
    return targets;
}

struct Coarser {
    int w = 0, h = 0;
    Floats img;
    std::vector<Pt> field;
    Bounds tbox;
    Pt fieldAt(int x, int y) const
    {
        const Bounds t = tbox;
        if (x < t.x0 || x >= t.x1 || y < t.y0 || y >= t.y1)
            return {kNoMatch, kNoMatch};
        const size_t i = size_t(y - t.y0) * t.width() + (x - t.x0);
        return i < field.size() ? field[i] : Pt{kNoMatch, kNoMatch};
    }
};

} // namespace

std::pair<float, float> fitGainBias(Adapt a, float n, float st, float stt, float ss, float sss)
{
    const float mt = st / n, ms = ss / n;
    const float vt = std::max(stt / n - mt * mt, 0.f);
    const float vs = std::max(sss / n - ms * ms, 0.f);
    const float g0 = vs > 1e-10f ? std::sqrt(vt / vs) : 1.f;
    float lo = a.gainLo, hi = a.gainHi;
    // Dos parches planos de nivel parecido: el que se copia puede igualarse con la ganancia.
    if (std::max(vt, vs) < kFlat * kFlat && std::fabs(mt - ms) <= a.bias && ms > 1e-6f) {
        const float glo = (mt - a.bias) / ms, ghi = (mt + a.bias) / ms;
        if (std::max(glo, lo) <= std::min(ghi, hi)) {
            lo = std::max(glo, lo);
            hi = std::min(ghi, hi);
        }
    }
    const float g = std::clamp(g0, lo, hi);
    const float b = std::clamp(mt - g * ms, -a.bias, a.bias);
    return {g, b};
}

namespace {

void patchMoments(const float* img, int w, int ch, int c8, int side, int x0, int y0, float (*out)[2])
{
    for (int c = 0; c < c8; ++c)
        out[c][0] = out[c][1] = 0.f;
    for (int y = y0; y < y0 + side; ++y) {
        const float* row = img + (size_t(y) * w + x0) * ch;
        for (int p = 0; p < side; ++p)
            for (int c = 0; c < c8; ++c) {
                const float a = row[size_t(p) * ch + c];
                out[c][0] += a;
                out[c][1] += a * a;
            }
    }
}

float adaptedCost(Adapt a, float nf, const float (*m)[5], int chn)
{
    float sum = 0.f;
    for (int c = 0; c < chn; ++c) {
        const auto [g, b] = fitGainBias(a, nf, m[c][0], m[c][1], m[c][2], m[c][3]);
        sum += m[c][1] + g * g * m[c][3] + nf * b * b - 2.f * g * m[c][4] - 2.f * b * m[c][0] + 2.f * g * b * m[c][2];
    }
    return std::max(sum, 0.f) / (nf * float(chn));
}

struct Solver {
    int w = 0, h = 0, ch = 0, r = 0;
    Floats* img = nullptr;
    const std::vector<Px>* px = nullptr;
    Mask valid;
    Bounds holeBox, tbox;
    Mask targets;
    Mask full;
    std::vector<float> tmom; // [tb.area() * c8][2]
    std::vector<float> smom; // [w * h * c8][2]
    Adapt adapt;
    Stop stop;

    bool isValid(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < w && y < h && valid[size_t(y) * w + x];
    }
    int c8() const { return std::min(ch, kMomCh); }

    void prepare(bool keepSourceMoments)
    {
        const Bounds tb = tbox;
        const int side = 2 * r + 1;
        full.assign(tb.area(), 0);
        const std::vector<Px>& P = *px;
        auto rows = [&](int t0, int t1) {
            for (int ty = t0; ty < t1; ++ty) {
                const int y = tb.y0 + ty;
                if (y < r || y + r >= h)
                    continue;
                for (int tx = 0; tx < tb.width(); ++tx) {
                    const int x = tb.x0 + tx;
                    if (x < r || x + r >= w)
                        continue;
                    bool all = true;
                    for (int yy = y - r; yy <= y + r && all; ++yy)
                        for (int k = 0; k < side; ++k)
                            if (P[size_t(yy) * w + x - r + k] == Px::Void) {
                                all = false;
                                break;
                            }
                    full[size_t(ty) * tb.width() + tx] = all;
                }
            }
        };
        if (tb.area() >= kParMinPatches)
            parallelRows(0, tb.height(), rows);
        else
            rows(0, tb.height());
        if (adapt.none())
            return;
        const int c = c8();
        tmom.assign(tb.area() * c * 2, 0.f);
        if (!keepSourceMoments)
            return;
        smom.assign(size_t(w) * h * c * 2, 0.f);
        const float* I = img->data();
        auto srows = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                if (stop.requested())
                    return;
                for (int x = 0; x < w; ++x)
                    if (valid[size_t(y) * w + x])
                        patchMoments(I, w, ch, c, side, x - r, y - r,
                                     reinterpret_cast<float(*)[2]>(smom.data() + (size_t(y) * w + x) * c * 2));
            }
        };
        if (size_t(w) * h >= kParMinPatches)
            parallelRows(0, h, srows);
        else
            srows(0, h);
    }

    float dist(Pt t, size_t k, Pt s, float cutoff) const
    {
        const bool f = full[k];
        if (!adapt.none())
            return f ? distAdaptedFull(k, t, s) : distAdapted(t, s);
        return f ? distFull(t, s, cutoff) : distClipped(t, s, cutoff);
    }

    float distClipped(Pt t, Pt s, float cutoff) const
    {
        const float fullN = float((2 * r + 1) * (2 * r + 1)) * float(ch);
        const float* I = img->data();
        float sum = 0.f;
        size_t n = 0;
        for (int dy = -r; dy <= r; ++dy) {
            const int ty = t.y + dy;
            if (ty < 0 || ty >= h)
                continue;
            const int sy = s.y + dy;
            for (int dx = -r; dx <= r; ++dx) {
                const int tx = t.x + dx;
                if (tx < 0 || tx >= w || (*px)[size_t(ty) * w + tx] == Px::Void)
                    continue;
                const size_t ti = (size_t(ty) * w + tx) * ch;
                const size_t si = (size_t(sy) * w + (s.x + dx)) * ch;
                for (int c = 0; c < ch; ++c) {
                    const float d = I[ti + c] - I[si + c];
                    sum += d * d;
                }
                n += size_t(ch);
            }
            if (sum > cutoff * fullN)
                return kInf;
        }
        return n == 0 ? kInf : sum / float(n);
    }

    float distFull(Pt t, Pt s, float cutoff) const
    {
        const int side = 2 * r + 1;
        const float fullN = float(side * side) * float(ch);
        const float limit = cutoff * fullN;
        const float* I = img->data();
        float sum = 0.f;
        for (int dy = -r; dy <= r; ++dy) {
            const float* tp = I + (size_t(t.y + dy) * w + (t.x - r)) * ch;
            const float* sp = I + (size_t(s.y + dy) * w + (s.x - r)) * ch;
            for (int p = 0; p < side; ++p) {
                for (int c = 0; c < ch; ++c) {
                    const float d = tp[size_t(p) * ch + c] - sp[size_t(p) * ch + c];
                    sum += d * d;
                }
                if (sum > limit)
                    return kInf;
            }
        }
        return sum / float(size_t(side) * side * ch);
    }

    size_t moments(Pt t, Pt s, float (*m)[5], int chn) const
    {
        for (int c = 0; c < chn; ++c)
            for (int k = 0; k < 5; ++k)
                m[c][k] = 0.f;
        const float* I = img->data();
        size_t n = 0;
        for (int dy = -r; dy <= r; ++dy) {
            const int ty = t.y + dy;
            if (ty < 0 || ty >= h)
                continue;
            const int sy = s.y + dy;
            for (int dx = -r; dx <= r; ++dx) {
                const int tx = t.x + dx;
                if (tx < 0 || tx >= w || (*px)[size_t(ty) * w + tx] == Px::Void)
                    continue;
                const size_t ti = (size_t(ty) * w + tx) * ch;
                const size_t si = (size_t(sy) * w + (s.x + dx)) * ch;
                for (int c = 0; c < std::min(chn, ch); ++c) {
                    const float a = I[ti + c], b = I[si + c];
                    m[c][0] += a;
                    m[c][1] += a * a;
                    m[c][2] += b;
                    m[c][3] += b * b;
                    m[c][4] += a * b;
                }
                ++n;
            }
        }
        return n;
    }

    void momentsFull(size_t k, Pt t, Pt s, float (*m)[5], int chn) const
    {
        const int side = 2 * r + 1;
        const float* tm = tmom.data() + k * chn * 2;
        const bool haveS = !smom.empty();
        const float* sm = haveS ? smom.data() + (size_t(s.y) * w + s.x) * chn * 2 : nullptr;
        float acc[kMomCh][3] = {};
        const float* I = img->data();
        for (int dy = -r; dy <= r; ++dy) {
            const float* trow = I + (size_t(t.y + dy) * w + (t.x - r)) * ch;
            const float* srow = I + (size_t(s.y + dy) * w + (s.x - r)) * ch;
            for (int p = 0; p < side; ++p) {
                const float* tp = trow + size_t(p) * ch;
                const float* sp = srow + size_t(p) * ch;
                if (haveS) {
                    for (int c = 0; c < chn; ++c)
                        acc[c][2] += tp[c] * sp[c];
                } else {
                    for (int c = 0; c < chn; ++c) {
                        const float a = tp[c], b = sp[c];
                        acc[c][0] += b;
                        acc[c][1] += b * b;
                        acc[c][2] += a * b;
                    }
                }
            }
        }
        for (int c = 0; c < chn; ++c) {
            const float ss = haveS ? sm[c * 2] : acc[c][0];
            const float sss = haveS ? sm[c * 2 + 1] : acc[c][1];
            m[c][0] = tm[c * 2];
            m[c][1] = tm[c * 2 + 1];
            m[c][2] = ss;
            m[c][3] = sss;
            m[c][4] = acc[c][2];
        }
    }

    float distAdapted(Pt t, Pt s) const
    {
        float m[kMomCh][5];
        const int c = c8();
        const size_t n = moments(t, s, m, c);
        if (n == 0)
            return kInf;
        return adaptedCost(adapt, float(n), m, c);
    }

    float distAdaptedFull(size_t k, Pt t, Pt s) const
    {
        float m[kMomCh][5];
        const int c = c8();
        momentsFull(k, t, s, m, c);
        const int side = 2 * r + 1;
        return adaptedCost(adapt, float(side * side), m, c);
    }

    void transform(Pt t, size_t k, Pt s, std::pair<float, float>* out) const
    {
        if (adapt.none()) {
            for (int c = 0; c < ch; ++c)
                out[c] = {1.f, 0.f};
            return;
        }
        float m[kMomCh][5];
        const int c8n = c8();
        size_t n;
        if (full[k]) {
            momentsFull(k, t, s, m, c8n);
            const int side = 2 * r + 1;
            n = size_t(side) * side;
        } else {
            n = moments(t, s, m, c8n);
        }
        for (int c = 0; c < ch; ++c)
            out[c] = n == 0 || c >= c8n ? std::pair<float, float>{1.f, 0.f}
                                         : fitGainBias(adapt, float(n), m[c][0], m[c][1], m[c][2], m[c][3]);
    }

    std::pair<Pt, float> pmStep(Pt t, size_t k, std::pair<Pt, float> cur, const Pt* left, const Pt* up, int step,
                                MixRng& rng, int searchRadius) const
    {
        Pt best = cur.first;
        float bc = cur.second;
        if (left) {
            const Pt cand{left->x + step, left->y};
            if (isValid(cand.x, cand.y) && cand != best) {
                const float d = dist(t, k, cand, bc);
                if (d < bc) {
                    best = cand;
                    bc = d;
                }
            }
        }
        if (up) {
            const Pt cand{up->x, up->y + step};
            if (isValid(cand.x, cand.y) && cand != best) {
                const float d = dist(t, k, cand, bc);
                if (d < bc) {
                    best = cand;
                    bc = d;
                }
            }
        }
        for (int rad = searchRadius; rad >= 1; rad /= 2) {
            const int cx = best.x + rng.range(-rad, rad);
            const int cy = best.y + rng.range(-rad, rad);
            const Pt cand{cx, cy};
            if (isValid(cand.x, cand.y) && cand != best) {
                const float d = dist(t, k, cand, bc);
                if (d < bc) {
                    best = cand;
                    bc = d;
                }
            }
        }
        return {best, bc};
    }

    // Una pasada de PatchMatch por bandas de 16 filas (la propagación no sale de la banda, así el
    // resultado es el mismo en paralelo).
    void patchmatch(std::vector<Pt>& nnf, std::vector<float>& cost, size_t pass, uint64_t seed, int searchRadius) const
    {
        constexpr int kBand = 16;
        const Bounds tb = tbox;
        const int tw = tb.width();
        if (tw == 0 || tb.height() == 0)
            return;
        const bool reverse = pass % 2 == 1;
        const int step = reverse ? -1 : 1;
        auto bandSeed = [&](int bi) { return seed ^ (uint64_t(bi) * 0x2545F4914F6CDD1Dull) ^ (uint64_t(pass) << 40); };
        struct Band {
            int bi, ys, rows;
        };
        std::vector<Band> bands;
        for (int y = tb.y0; y < tb.y1;) {
            const int bi = y / kBand;
            const int end = std::min((bi + 1) * kBand, tb.y1);
            bands.push_back({bi, y, end - y});
            y = end;
        }
        auto run = [&](const Band& b) {
            if (stop.requested())
                return;
            MixRng rng(bandSeed(b.bi));
            const size_t base = size_t(b.ys - tb.y0) * tw;
            for (int ry = 0; ry < b.rows; ++ry) {
                if (stop.requested())
                    return;
                const int ly = reverse ? b.rows - 1 - ry : ry;
                const int y = b.ys + ly;
                for (int rx = 0; rx < tw; ++rx) {
                    const int lx = reverse ? tw - 1 - rx : rx;
                    const size_t li = base + size_t(ly) * tw + lx;
                    if (!targets[li])
                        continue;
                    const int nx = lx - step;
                    Pt leftV, upV;
                    const Pt* left = nullptr;
                    const Pt* up = nullptr;
                    if (nx >= 0 && nx < tw) {
                        leftV = nnf[base + size_t(ly) * tw + nx];
                        left = &leftV;
                    }
                    const int nly = ly - step;
                    if (nly >= 0 && nly < b.rows) {
                        upV = nnf[base + size_t(nly) * tw + lx];
                        up = &upV;
                    }
                    const Pt t{tb.x0 + lx, y};
                    const auto res = pmStep(t, li, {nnf[li], cost[li]}, left, up, step, rng, searchRadius);
                    nnf[li] = res.first;
                    cost[li] = res.second;
                }
            }
        };
        const bool par = tb.area() >= kParMinPatches && bands.size() > 1;
        if (par)
            parallelRows(0, int(bands.size()), [&](int b0, int b1) {
                for (int b = b0; b < b1; ++b)
                    run(bands[size_t(b)]);
            }, 1);
        else
            for (const Band& b : bands)
                run(b);
    }

    void costs(const std::vector<Pt>& nnf, std::vector<float>& cost)
    {
        const Bounds tb = tbox;
        const bool par = tb.area() >= kParMinPatches;
        stop.check();
        if (!adapt.none()) {
            const int c = c8(), side = 2 * r + 1;
            const float* I = img->data();
            auto rows = [&](int t0, int t1) {
                for (int ty = t0; ty < t1; ++ty) {
                    if (stop.requested())
                        return;
                    for (int tx = 0; tx < tb.width(); ++tx)
                        if (full[size_t(ty) * tb.width() + tx])
                            patchMoments(I, w, ch, c, side, tb.x0 + tx - r, tb.y0 + ty - r,
                                         reinterpret_cast<float(*)[2]>(tmom.data() + (size_t(ty) * tb.width() + tx) * c * 2));
                }
            };
            if (par)
                parallelRows(0, tb.height(), rows);
            else
                rows(0, tb.height());
        }
        auto rows = [&](int t0, int t1) {
            for (int ty = t0; ty < t1; ++ty) {
                if (stop.requested())
                    return;
                const int y = tb.y0 + ty;
                for (int tx = 0; tx < tb.width(); ++tx) {
                    const size_t k = size_t(ty) * tb.width() + tx;
                    if (targets[k])
                        cost[k] = dist({tb.x0 + tx, y}, k, nnf[k], kInf);
                }
            }
        };
        if (par)
            parallelRows(0, tb.height(), rows);
        else
            rows(0, tb.height());
        stop.check();
    }

    void vote(const std::vector<Pt>& nnf, const std::vector<float>& cost)
    {
        const Bounds tb = tbox, hb = holeBox;
        if (stop.requested())
            return;
        auto used = [&](size_t k) { return targets[k] && std::isfinite(cost[k]); };
        // σ² del percentil 75 de los costes.
        std::vector<float> cs;
        for (size_t k = 0; k < tb.area(); ++k)
            if (used(k))
                cs.push_back(cost[k]);
        float sigma2 = 1.f;
        if (!cs.empty()) {
            const size_t kk = std::min(cs.size() * 3 / 4, cs.size() - 1);
            std::nth_element(cs.begin(), cs.begin() + kk, cs.end());
            sigma2 = std::max(cs[kk], 1e-6f);
        }
        cs = {};
        const bool adapted = !adapt.none();
        std::vector<float> wt(tb.area(), 0.f);
        std::vector<long long> src(tb.area(), 0);
        for (size_t k = 0; k < tb.area(); ++k)
            if (used(k)) {
                wt[k] = std::max(std::exp(-cost[k] / (2.f * sigma2)), 1e-8f);
                src[k] = (long long)nnf[k].y * w + nnf[k].x;
            }
        std::vector<std::pair<float, float>> gb;
        if (adapted) {
            gb.assign(tb.area() * ch, {1.f, 0.f});
            auto rows = [&](int t0, int t1) {
                for (int ty = t0; ty < t1; ++ty) {
                    if (stop.requested())
                        return;
                    const int y = tb.y0 + ty;
                    for (int tx = 0; tx < tb.width(); ++tx) {
                        const size_t k = size_t(ty) * tb.width() + tx;
                        if (used(k))
                            transform({tb.x0 + tx, y}, k, nnf[k], gb.data() + k * ch);
                    }
                }
            };
            if (tb.area() >= kParMinPatches)
                parallelRows(0, tb.height(), rows);
            else
                rows(0, tb.height());
        }
        // El voto, en un búfer sobre el hueco (cada píxel lee los píxeles anteriores).
        const float* I = img->data();
        std::vector<float> out(hb.area() * ch, 0.f);
        Mask got(hb.area(), 0);
        auto rows = [&](int r0, int r1) {
            std::vector<float> acc(static_cast<size_t>(ch));
            for (int yy = r0; yy < r1; ++yy) {
                if (stop.requested())
                    return;
                const int y = hb.y0 + yy;
                for (int x = hb.x0; x < hb.x1; ++x) {
                    if ((*px)[size_t(y) * w + x] != Px::Hole)
                        continue;
                    std::fill(acc.begin(), acc.end(), 0.f);
                    float wsum = 0.f;
                    for (int ty = std::max(y - r, 0); ty <= std::min(y + r, h - 1); ++ty) {
                        const long long dy = ty - y;
                        const size_t krow = size_t(ty - tb.y0) * tb.width();
                        for (int tx = std::max(x - r, 0); tx <= std::min(x + r, w - 1); ++tx) {
                            const size_t k = krow + size_t(tx - tb.x0);
                            const float wk = wt[k];
                            if (wk == 0.f)
                                continue;
                            const size_t si = size_t(src[k] - dy * w - (tx - x)) * ch;
                            for (int c = 0; c < ch; ++c) {
                                float v = I[si + c];
                                if (adapted) {
                                    const auto& g = gb[k * ch + c];
                                    v = g.first * v + g.second;
                                }
                                acc[size_t(c)] += v * wk;
                            }
                            wsum += wk;
                        }
                    }
                    if (wsum > 0.f) {
                        float* o = out.data() + (size_t(yy) * hb.width() + (x - hb.x0)) * ch;
                        for (int c = 0; c < ch; ++c)
                            o[c] = acc[size_t(c)] / wsum;
                        got[size_t(yy) * hb.width() + (x - hb.x0)] = 1;
                    }
                }
            }
        };
        if (hb.area() >= kParMinPatches)
            parallelRows(0, hb.height(), rows);
        else
            rows(0, hb.height());
        for (int yy = 0; yy < hb.height(); ++yy)
            for (int xx = 0; xx < hb.width(); ++xx)
                if (got[size_t(yy) * hb.width() + xx]) {
                    const size_t d = (size_t(hb.y0 + yy) * w + hb.x0 + xx) * ch;
                    const size_t s = (size_t(yy) * hb.width() + xx) * ch;
                    std::copy(out.begin() + long(s), out.begin() + long(s + ch), img->begin() + long(d));
                }
    }
};

// La menor suma e ≥ 0 con e/n > v (estricto) o e/n ≥ v en coma flotante.
float leastSum(float v, float n, bool strict)
{
    auto holds = [&](float e) { return strict ? e / n > v : e / n >= v; };
    if (!std::isfinite(v) || !std::isfinite(n) || n <= 0.f)
        return kInf;
    float e = std::max(v * n, 0.f);
    while (e > 0.f && holds(std::nextafter(e, -kInf)))
        e = std::nextafter(e, -kInf);
    while (!holds(e) && e < kInf)
        e = std::nextafter(e, kInf);
    return e;
}

// Rellena el hueco anillo a anillo desde el borde: cada píxel toma el centro del parche fuente que
// mejor coincide con lo ya conocido a su alrededor (el primero en 'sources' de los que empatan).
void onionPeel(int w, int h, int ch, int r, Floats& img, const std::vector<Px>& px, Bounds hb,
               const std::vector<Pt>& sources, const Stop& stop)
{
    constexpr uint32_t kNone = UINT32_MAX;
    Mask known(px.size());
    for (size_t i = 0; i < px.size(); ++i)
        known[i] = px[i] == Px::Known || px[i] == Px::Source;
    std::vector<uint32_t> index(size_t(w) * h, kNone);
    for (size_t j = 0; j < sources.size(); ++j)
        index[size_t(sources[j].y) * w + sources[j].x] = uint32_t(j);
    std::vector<uint32_t> took(size_t(w) * h, kNone);
    static const int nb[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    for (;;) {
        stop.check();
        std::vector<size_t> front;
        for (int y = hb.y0; y < hb.y1; ++y)
            for (int x = hb.x0; x < hb.x1; ++x) {
                const size_t i = size_t(y) * w + x;
                if (known[i] || px[i] != Px::Hole)
                    continue;
                bool edge = false;
                for (int dy = -1; dy <= 1 && !edge; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && known[size_t(ny) * w + nx]) {
                            edge = true;
                            break;
                        }
                    }
                if (edge)
                    front.push_back(i);
            }
        if (front.empty())
            break;
        const float* image = img.data();
        std::vector<float> vals(front.size() * ch);
        std::vector<uint32_t> picks(front.size(), kNone);
        auto best = [&](size_t f) {
            const size_t i = front[f];
            const int x = int(i % w), y = int(i / w);
            const int side = 2 * r + 1;
            std::vector<long long> offs;
            std::vector<float> tv;
            offs.reserve(size_t(side) * side);
            tv.reserve(size_t(side) * side * ch);
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const int tx = x + dx, ty = y + dy;
                    if (tx >= 0 && ty >= 0 && tx < w && ty < h && known[size_t(ty) * w + tx]) {
                        const size_t ti = (size_t(ty) * w + tx) * ch;
                        offs.push_back((long long)dy * w + dx);
                        tv.insert(tv.end(), image + ti, image + ti + ch);
                    }
                }
            struct Best {
                float v = kInf;
                size_t j = SIZE_MAX;
                float worse = kInf;
                float equal = kInf;
            } bst;
            if (!offs.empty()) {
                const float nf = float(offs.size());
                auto baseOf = [&](size_t j) {
                    return j < sources.size() ? (long long)sources[j].y * w + sources[j].x : 0ll;
                };
                auto consider = [&](size_t j) {
                    if (j >= sources.size())
                        return;
                    const long long base = baseOf(j);
                    const float limit = j > bst.j ? bst.equal : bst.worse;
                    float e = 0.f;
                    for (size_t o = 0; o < offs.size(); ++o) {
                        const size_t si = size_t(base + offs[o]) * ch;
                        for (int c = 0; c < ch; ++c) {
                            const float d = tv[o * ch + c] - image[si + c];
                            e += d * d;
                        }
                        if (e >= limit)
                            return;
                    }
                    const float v = e / nf;
                    if (v < kInf && (v < bst.v || (v == bst.v && j < bst.j))) {
                        bst.v = v;
                        bst.j = j;
                        bst.worse = leastSum(v, nf, true);
                        bst.equal = leastSum(v, nf, false);
                    }
                };
                // Primero donde tomaron sus vecinos ya rellenos; si no, una de cada 16 fuentes.
                for (const auto& d : nb) {
                    const int qx = x + d[0], qy = y + d[1];
                    if (qx < 0 || qy < 0 || qx >= w || qy >= h)
                        continue;
                    const uint32_t t = took[size_t(qy) * w + qx];
                    if (t == kNone || t >= sources.size())
                        continue;
                    const int cx = sources[t].x - d[0], cy = sources[t].y - d[1];
                    if (cx >= 0 && cy >= 0 && cx < w && cy < h && index[size_t(cy) * w + cx] != kNone)
                        consider(index[size_t(cy) * w + cx]);
                }
                if (bst.j == SIZE_MAX)
                    for (size_t j = 0; j < sources.size(); j += 16)
                        consider(j);
                for (size_t j = 0; j < sources.size(); ++j) {
                    if (j % 4096 == 0 && j > 0 && stop.requested())
                        break;
                    consider(j);
                }
            }
            const Pt s = bst.j < sources.size() ? sources[bst.j] : Pt{0, 0};
            const size_t si = (size_t(s.y) * w + s.x) * ch;
            std::copy(image + si, image + si + ch, vals.begin() + long(f * ch));
            picks[f] = bst.j == SIZE_MAX ? kNone : uint32_t(bst.j);
        };
        const bool par = front.size() * sources.size() >= 4096;
        parallelFor(front.size(), par, 4, [&](size_t a, size_t b) {
            for (size_t f = a; f < b; ++f)
                best(f);
        });
        stop.check();
        for (size_t f = 0; f < front.size(); ++f) {
            const size_t i = front[f];
            std::copy(vals.begin() + long(f * ch), vals.begin() + long(f * ch + ch), img.begin() + long(i * ch));
            known[i] = 1;
            took[i] = picks[f];
        }
    }
}

} // namespace

bool completeMasked(int w, int h, int ch, Floats& image, const std::vector<Px>& px, const CompleteParams& p,
                    const Stop& stop)
{
    if (image.size() != size_t(w) * h * ch || px.size() != size_t(w) * h)
        return false;
    const auto hb0 = holeBounds(w, h, px);
    if (!hb0)
        return true;
    if (ch == 0)
        return true;
    const int r = std::max(p.patchRadius, 1);
    const int psz = 2 * r + 1;
    std::vector<Level> levels;
    levels.push_back({w, h, image, px});
    std::vector<Bounds> boxes{*hb0};
    int ext = std::max(hb0->width(), hb0->height());
    const int thickness = holeThickness(w, *hb0, px);
    const bool thin = (long long)thickness * kThinRatio <= ext;
    if (thin)
        ext = thickness;
    std::vector<int> exts{ext};
    for (;;) {
        stop.check();
        const Level& l = levels.back();
        if (ext <= (thin ? 2 * psz : 4 * psz) || l.w / 2 < 3 * psz || l.h / 2 < 3 * psz)
            break;
        Level d = downsample(l, ch);
        levels.push_back(std::move(d));
        boxes.push_back(boxes.back().half());
        ext = (ext + 1) / 2;
        exts.push_back(ext);
    }
    const int nlev = int(levels.size());
    std::optional<Coarser> prev;
    for (int li = nlev - 1; li >= 0; --li) {
        stop.check();
        Level lv = std::move(levels[size_t(li)]);
        const int lw = lv.w, lh = lv.h;
        const std::vector<Px>& lpx = lv.px;
        const Bounds hb = boxes[size_t(li)];
        const Bounds tb = hb.grow(r, lw, lh);
        Mask valid = validSources(lw, lh, r, lpx, stop);
        const Sources sources(valid, lw);
        if (li == 0 && sources.size() == 0)
            return false;
        Floats limg = std::move(lv.img);
        Mask targets = targetsAround(lw, r, lpx, hb, tb);
        stop.check();
        std::optional<Coarser> coarser = std::move(prev);
        prev.reset();
        if (coarser) {
            const int pw = coarser->w, ph = coarser->h;
            for (int y = hb.y0; y < hb.y1; ++y)
                for (int x = hb.x0; x < hb.x1; ++x)
                    if (lpx[size_t(y) * lw + x] == Px::Hole) {
                        const int sx = std::min(x / 2, pw - 1), sy = std::min(y / 2, ph - 1);
                        const size_t d = (size_t(y) * lw + x) * ch, s = (size_t(sy) * pw + sx) * ch;
                        std::copy(coarser->img.begin() + long(s), coarser->img.begin() + long(s + ch), limg.begin() + long(d));
                    }
        } else if (sources.size() > 0 && (!thin || peelAffordable(lw, lpx, hb, sources.size()))) {
            onionPeel(lw, lh, ch, r, limg, lpx, hb, sources.all(valid, lw), stop);
        } else {
            Mask unknown(lpx.size());
            for (size_t i = 0; i < lpx.size(); ++i)
                unknown[i] = lpx[i] == Px::Hole || lpx[i] == Px::Void;
            if (thin) {
                Mask hole(lpx.size());
                for (size_t i = 0; i < lpx.size(); ++i)
                    hole[i] = lpx[i] == Px::Hole;
                const Floats d = distanceTransform(hole, lw, lh);
                for (size_t i = 0; i < unknown.size(); ++i)
                    unknown[i] = unknown[i] || d[i] <= float(r);
            }
            const Floats m = membraneFill(lw, lh, ch, limg, unknown);
            for (size_t i = 0; i < lpx.size(); ++i)
                if (lpx[i] == Px::Hole)
                    std::copy(m.begin() + long(i * ch), m.begin() + long(i * ch + ch), limg.begin() + long(i * ch));
        }
        if (sources.size() == 0) {
            prev = Coarser{lw, lh, std::move(limg), {}, tb};
            continue;
        }
        const bool refineOnly = li + 1 < nlev && exts[size_t(li)] > p.refineExtent;
        size_t nTargets = 0;
        for (uint8_t t : targets)
            nTargets += t;
        const bool keepSourceMoments = size_t(lw) * lh <= kSourceMomentsMax
                                    && sources.size() <= nTargets * (refineOnly ? 4 : 64);
        Solver solver;
        solver.w = lw;
        solver.h = lh;
        solver.ch = ch;
        solver.r = r;
        solver.img = &limg;
        solver.px = &lpx;
        solver.valid = std::move(valid);
        solver.targets = std::move(targets);
        solver.holeBox = hb;
        solver.tbox = tb;
        solver.adapt = p.adapt;
        solver.stop = stop;
        solver.prepare(keepSourceMoments);
        stop.check();
        MixRng rng(p.seed ^ (uint64_t(li) << 20));
        const int tw = tb.width();
        std::vector<Pt> nnf(tb.area(), Pt{0, 0});
        bool upsampled = false;
        for (int y = tb.y0; y < tb.y1; ++y) {
            if (y % 32 == 0)
                stop.check();
            for (int x = tb.x0; x < tb.x1; ++x) {
                const size_t k = size_t(y - tb.y0) * tw + (x - tb.x0);
                if (!solver.targets[k])
                    continue;
                std::optional<Pt> cand;
                if (coarser && !coarser->field.empty()) {
                    const int cx = std::min(x / 2, coarser->w - 1), cy = std::min(y / 2, coarser->h - 1);
                    const Pt s = coarser->fieldAt(cx, cy);
                    if (s.x != kNoMatch) {
                        const Pt c{x + 2 * (s.x - cx), y + 2 * (s.y - cy)};
                        if (solver.isValid(c.x, c.y))
                            cand = c;
                    }
                }
                upsampled = upsampled || cand.has_value();
                nnf[k] = cand ? *cand : sources.nth(size_t(rng.next() % sources.size()), solver.valid, lw);
            }
        }
        coarser.reset();
        std::vector<float> cost(tb.area(), kInf);
        if (upsampled) {
            solver.costs(nnf, cost);
            solver.vote(nnf, cost);
        }
        const int em = li + 1 == nlev ? p.emIters
                       : refineOnly   ? 1
                                      : std::max(p.emIters >> (nlev - 1 - li), p.minEmIters);
        const int pmIters = refineOnly ? 1 : p.pmIters;
        const int fullSearch = std::max(lw, lh);
        for (int it = 0; it < em; ++it) {
            solver.costs(nnf, cost);
            const int radius = refineOnly ? 2 : (li + 1 == nlev || it == 0) ? fullSearch : std::min(4 * psz, fullSearch);
            for (int pass = 0; pass < pmIters; ++pass) {
                stop.check();
                solver.patchmatch(nnf, cost, size_t(pass + it * pmIters), p.seed + uint64_t(li * 1000 + it), radius);
            }
            stop.check();
            solver.vote(nnf, cost);
            stop.check();
        }
        stop.check();
        std::vector<Pt> field;
        if (li != 0) {
            field.resize(nnf.size());
            for (size_t k = 0; k < nnf.size(); ++k)
                field[k] = solver.targets[k] ? nnf[k] : Pt{kNoMatch, kNoMatch};
        }
        prev = Coarser{lw, lh, std::move(limg), std::move(field), tb};
    }
    if (prev)
        image = std::move(prev->img);
    return true;
}

std::optional<Floats> complete(int w, int h, int ch, const Floats& img, const Mask& hole, const CompleteParams& p,
                               const Stop& stop)
{
    std::vector<Px> px(hole.size());
    for (size_t i = 0; i < hole.size(); ++i)
        px[i] = hole[i] ? Px::Hole : Px::Source;
    Floats out = img;
    if (!completeMasked(w, h, ch, out, px, p, stop))
        return std::nullopt;
    return out;
}

// ---------------------------------------------------------------------------------------------

std::optional<std::pair<int, int>> bestOffset(int w, int h, int ch, const Floats& img, const Mask& hole, int ring,
                                              int maxRadius)
{
    const auto bb = holeBBox(w, h, hole);
    if (!bb)
        return std::nullopt;
    const int extent = std::min(std::max(w, h), INT_MAX / 4);
    maxRadius = std::min(std::max(maxRadius, 0), extent);
    if (maxRadius == 0)
        return std::nullopt;
    ring = std::min(std::max(ring, 1), extent);
    // Tabla de áreas del hueco sobre su rectángulo.
    const int hw = bb->width(), hh = bb->height();
    std::vector<uint32_t> hs(size_t(hw + 1) * (hh + 1), 0);
    for (int y = 0; y < hh; ++y) {
        uint32_t count = 0;
        for (int x = 0; x < hw; ++x) {
            count += hole[size_t(bb->y0 + y) * w + bb->x0 + x];
            hs[size_t(y + 1) * (hw + 1) + x + 1] = hs[size_t(y) * (hw + 1) + x + 1] + count;
        }
    }
    auto holeCount = [&](int x0, int y0, int x1, int y1) {
        return windowCount(hs, hw, hh, x0 - bb->x0, y0 - bb->y0, x1 - bb->x0, y1 - bb->y0);
    };
    std::vector<Pt> pts;
    const int x0 = bb->x0 - ring, y0 = bb->y0 - ring, x1 = bb->x1 + ring, y1 = bb->y1 + ring;
    for (int y = std::max(y0, 0); y < std::min(y1, h); ++y)
        for (int x = std::max(x0, 0); x < std::min(x1, w); ++x) {
            if (hole[size_t(y) * w + x])
                continue;
            if (holeCount(x - ring, y - ring, x + ring + 1, y + ring + 1) > 0)
                pts.push_back({x, y});
        }
    const size_t stride = std::max<size_t>(pts.size() / 2000, 1);
    std::vector<Pt> sampled;
    for (size_t i = 0; i < pts.size(); i += stride)
        sampled.push_back(pts[i]);
    pts = std::move(sampled);
    auto eval = [&](int dx, int dy, float cutoff) -> std::optional<float> {
        const int hx0 = x0 + dx, hy0 = y0 + dy, hx1 = x1 + dx, hy1 = y1 + dy;
        const bool inside = hx0 >= 0 && hy0 >= 0 && hx1 <= w && hy1 <= h;
        if ((dx == 0 && dy == 0) || !inside || holeCount(hx0, hy0, hx1, hy1) != 0)
            return std::nullopt;
        float e = 0.f;
        const float n = float(std::max<size_t>(pts.size(), 1));
        const float penalty = 1.f + 1e-3f * (std::hypot(float(dx), float(dy)) / float(maxRadius));
        for (size_t i = 0; i < pts.size(); ++i) {
            const size_t a = (size_t(pts[i].y) * w + pts[i].x) * ch;
            const size_t b = (size_t(pts[i].y + dy) * w + (pts[i].x + dx)) * ch;
            for (int c = 0; c < ch; ++c) {
                const float d = img[a + c] - img[b + c];
                e += d * d;
            }
            if (i % 32 == 31 && std::isfinite(cutoff) && e / n * penalty >= cutoff)
                return std::nullopt;
        }
        return e / n * penalty;
    };
    const int step = std::max(maxRadius / 24, 1);
    std::optional<std::pair<Pt, float>> best;
    auto consider = [&](int dx, int dy) {
        const auto e = eval(dx, dy, best ? best->second : kInf);
        if (e && (!best || *e < best->second))
            best = std::pair<Pt, float>{Pt{dx, dy}, *e};
    };
    for (int dy = -maxRadius; dy <= maxRadius; dy += step)
        for (int dx = -maxRadius; dx <= maxRadius; dx += step)
            consider(dx, dy);
    if (step > 1 && best) {
        const Pt b = best->first;
        for (int dy = b.y - step; dy <= b.y + step; ++dy)
            for (int dx = b.x - step; dx <= b.x + step; ++dx)
                consider(dx, dy);
    }
    if (!best)
        return std::nullopt;
    return std::pair<int, int>{best->first.x, best->first.y};
}

namespace {

// El corte de error mínimo de una banda de 'across' de ancho y 'along' de largo (programación
// dinámica): para cada paso a lo largo, dónde va el corte. Vacío si no cuesta nada en ningún sitio.
template <typename Err>
std::vector<int> minCut(int across, int along, Err err)
{
    if (across <= 0 || along <= 0)
        return {};
    std::vector<float> acc(size_t(across) * along);
    bool any = false;
    for (int v = 0; v < along; ++v)
        for (int u = 0; u < across; ++u) {
            acc[size_t(v) * across + u] = err(u, v);
            any = any || acc[size_t(v) * across + u] != 0.f;
        }
    if (!any)
        return {};
    for (int v = 1; v < along; ++v)
        for (int u = 0; u < across; ++u) {
            const int lo = std::max(u - 1, 0), hi = std::min(u + 1, across - 1);
            float m = kInf;
            for (int k = lo; k <= hi; ++k)
                m = std::min(m, acc[size_t(v - 1) * across + k]);
            acc[size_t(v) * across + u] += m;
        }
    auto argmin = [&](int v, int lo, int hi) {
        int best = lo;
        for (int k = lo + 1; k <= hi; ++k)
            if (acc[size_t(v) * across + k] < acc[size_t(v) * across + best])
                best = k;
        return best;
    };
    std::vector<int> cut(size_t(along), 0);
    int u = argmin(along - 1, 0, across - 1);
    for (int v = along - 1; v >= 0; --v) {
        if (v + 1 < along)
            u = argmin(v, std::max(u - 1, 0), std::min(u + 1, across - 1));
        cut[size_t(v)] = u;
    }
    return cut;
}

} // namespace

Floats synthesize(int w, int h, int ch, const Floats& img, const Mask& hole, int block, uint64_t seed, const Stop& stop)
{
    Floats out = img;
    const auto bb = holeBBox(w, h, hole);
    if (!bb)
        return out;
    const int b = std::max(block, 4);
    const int ov = std::max(b / 3, 1);
    const std::vector<uint32_t> hs = integral(w, h, hole);
    std::vector<Pt> cands;
    for (int y = ov; y < h - b - ov; ++y) {
        stop.check();
        for (int x = ov; x < w - b - ov; ++x)
            if (windowCount(hs, w, h, x - ov, y - ov, x + b + ov, y + b + ov) == 0)
                cands.push_back({x, y});
    }
    if (cands.empty())
        return membraneFill(w, h, ch, img, hole, stop);
    Mask filled(hole.size());
    for (size_t i = 0; i < hole.size(); ++i)
        filled[i] = !hole[i];
    MixRng rng(seed);
    for (int by = bb->y0; by < bb->y1; by += b) {
        stop.check();
        for (int bx = bb->x0; bx < bb->x1; bx += b) {
            if (windowCount(hs, w, h, bx, by, bx + b, by + b) == 0)
                continue;
            std::pair<Pt, float> best{cands[0], kInf};
            for (int t = 0; t < 48; ++t) {
                const Pt c = cands[size_t(rng.next() % cands.size())];
                float e = 0.f;
                unsigned n = 0;
                for (int y = by - ov; y < by + b + ov; ++y)
                    for (int x = bx - ov; x < bx + b + ov; ++x) {
                        if (x < 0 || y < 0 || x >= w || y >= h || !filled[size_t(y) * w + x])
                            continue;
                        const size_t a = (size_t(y) * w + x) * ch;
                        const size_t s = (size_t(y - by + c.y) * w + (x - bx + c.x)) * ch;
                        for (int k = 0; k < ch; ++k) {
                            const float d = out[a + k] - img[s + k];
                            e += d * d;
                        }
                        ++n;
                    }
                const float ev = n == 0 ? 0.f : e / float(n);
                if (ev < best.second)
                    best = {c, ev};
            }
            const Pt c = best.first;
            const int x0 = std::max(bx - ov, 0), y0 = std::max(by - ov, 0);
            const int x1 = std::min(bx + b + ov, w), y1 = std::min(by + b + ov, h);
            auto src = [&](int x, int y) { return (size_t(y - by + c.y) * w + (x - bx + c.x)) * ch; };
            auto err = [&](int x, int y) {
                const size_t i = size_t(y) * w + x;
                if (!(hole[i] && filled[i]))
                    return 0.f;
                const size_t a = i * ch, s = src(x, y);
                float sum = 0.f;
                for (int q = 0; q < ch; ++q)
                    sum += (out[a + q] - img[s + q]) * (out[a + q] - img[s + q]);
                return sum;
            };
            const std::vector<int> left = minCut(std::max(std::min(bx + ov, x1) - x0, 0), y1 - y0,
                                                 [&](int u, int v) { return err(x0 + u, y0 + v); });
            const std::vector<int> top = minCut(std::max(std::min(by + ov, y1) - y0, 0), x1 - x0,
                                                [&](int u, int v) { return err(x0 + v, y0 + u); });
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const size_t i = size_t(y) * w + x;
                    if (!hole[i])
                        continue;
                    const int u = x - x0, v = y - y0;
                    const bool keep = filled[i]
                                   && ((size_t(v) < left.size() && u < left[size_t(v)])
                                       || (size_t(u) < top.size() && v < top[size_t(u)]));
                    if (!keep) {
                        const size_t a = i * ch, s = src(x, y);
                        std::copy(img.begin() + long(s), img.begin() + long(s + ch), out.begin() + long(a));
                    }
                    filled[i] = 1;
                }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Relleno según el contenido de Edición (y el del Parche).

Adapt colorLevel(const char* level)
{
    const std::string_view l(level ? level : "");
    if (l == "none")
        return {};
    if (l == "high")
        return {0.8f, 1.25f, 0.08f};
    if (l == "veryHigh")
        return {0.67f, 1.5f, 0.14f};
    return {0.9f, 1.1f, 0.04f};
}

Adapt colorAmount(int color)
{
    if (color <= 0)
        return {};
    const float c = float(std::min(color, 10));
    return {1.f - 0.033f * c, 1.f + 0.05f * c, 0.04f * c};
}

std::vector<std::pair<float, float>> levelTransform(int ch, const Floats& from, const Floats& to, Adapt a)
{
    auto moments = [ch](const Floats& v, int c) {
        double s = 0, s2 = 0, n = 0;
        for (size_t i = 0; i + ch <= v.size(); i += size_t(std::max(ch, 1))) {
            s += double(v[i + c]);
            s2 += double(v[i + c]) * double(v[i + c]);
            n += 1.0;
        }
        return std::array<double, 3>{s, s2, n};
    };
    std::vector<std::pair<float, float>> out;
    for (int c = 0; c < ch; ++c) {
        const auto f = moments(from, c);
        const auto t = moments(to, c);
        if (a.none() || f[2] == 0.0 || t[2] == 0.0) {
            out.push_back({1.f, 0.f});
            continue;
        }
        const double k = f[2] / t[2];
        out.push_back(fitGainBias(a, float(f[2]), float(t[0] * k), float(t[1] * k), float(f[0]), float(f[1])));
    }
    return out;
}

Floats contentAwareFill(int w, int h, int ch, const Floats& img, const Mask& hole, const Mask& source, Adapt adapt,
                        uint64_t seed, const Stop& stop)
{
    if (std::none_of(hole.begin(), hole.end(), [](uint8_t v) { return v; }))
        return img;
    CompleteParams params;
    params.seed = seed;
    params.adapt = adapt;
    std::vector<Px> roles(size_t(w) * h);
    for (size_t s = 0; s < roles.size(); ++s)
        roles[s] = hole[s] ? Px::Hole : source[s] ? Px::Source : Px::Known;
    stop.check();
    Floats out = img;
    if (!completeMasked(w, h, ch, out, roles, params, stop))
        out = membraneFill(w, h, ch, img, hole, stop);
    stop.check();
    return out;
}

} // namespace retouch
