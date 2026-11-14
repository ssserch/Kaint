// Retoque de PhotoCraft (Retouch.h): la Selección de objetos. Algoritmos de PhotoCraft (licencia
// MIT).
//
// · GrabCut (Rother, Kolmogorov y Blake, SIGGRAPH 2004): un trimapa (fondo y objeto seguros o
//   probables), mezclas de 5 gaussianas de covarianza completa por lado e iteraciones «asignar
//   componentes → aprender las mezclas → corte mínimo», con el término de suavidad
//   γ·e^(−β‖z_m − z_n‖²)/dist(m, n) (γ = 50, β de la diferencia media entre vecinos).
// · El corte mínimo: Boykov–Kolmogorov, o empujar/reetiquetar FIFO (Goldberg y Tarjan) en grafos
//   grandes con las dos terminales.
// · Buscador de objetos: crece la zona de color parecido bajo el puntero (CIELAB) y GrabCut la
//   afina.
// · Seleccionar sujeto: superpíxeles SLIC, prominencia por conectividad con el borde (Zhu et al.,
//   CVPR 2014) mezclada con la de frecuencias (Achanta et al., CVPR 2009), Otsu y dos GrabCut.
//   Funciona con un objeto destacado sobre un fondo más bien liso; no es un segmentador semántico.

#include "core/retouch/Retouch.h"

#include "core/Parallel.h"

#include <algorithm>
#include <tuple>
#include <cmath>
#include <deque>
#include <limits>
#include <queue>

namespace retouch {

namespace {

using Rgb = std::array<float, 3>;

struct RgbImage {
    int w = 0, h = 0;
    std::vector<Rgb> px;
    const Rgb& at(int x, int y) const { return px[size_t(y) * w + x]; }
};

float d2(const Rgb& a, const Rgb& b)
{
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

// RGBA directo a RGB sobre un gris medio (la transparencia es un color más para los modelos).
RgbImage fromRgba(const std::vector<std::array<float, 4>>& px, int w, int h)
{
    RgbImage out{w, h, std::vector<Rgb>(px.size())};
    for (size_t i = 0; i < px.size(); ++i) {
        const float a = std::clamp(px[i][3], 0.f, 1.f);
        for (int c = 0; c < 3; ++c)
            out.px[i][size_t(c)] = std::clamp(px[i][size_t(c)], 0.f, 1.f) * a + 0.5f * (1.f - a);
    }
    return out;
}

RgbImage downsampleRgb(const RgbImage& img, int step)
{
    if (step <= 1)
        return img;
    const int ow = (img.w + step - 1) / step, oh = (img.h + step - 1) / step;
    RgbImage out{ow, oh, std::vector<Rgb>(size_t(ow) * oh)};
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox) {
            Rgb acc{0, 0, 0};
            float n = 0.f;
            for (int y = oy * step; y < std::min((oy + 1) * step, img.h); ++y)
                for (int x = ox * step; x < std::min((ox + 1) * step, img.w); ++x) {
                    const Rgb& p = img.at(x, y);
                    for (int c = 0; c < 3; ++c)
                        acc[size_t(c)] += p[size_t(c)];
                    n += 1.f;
                }
            for (int c = 0; c < 3; ++c)
                acc[size_t(c)] /= n;
            out.px[size_t(oy) * ow + ox] = acc;
        }
    return out;
}

RgbImage readRgb(const Sampler& sampler, Box r)
{
    return fromRgba(sampler(r), r.width(), r.height());
}

// RGB sobre 'r' reducido por 'step', leído en franjas (en paralelo) para no tener nunca toda la
// zona a resolución completa en memoria.
RgbImage readRgbScaled(const Sampler& sampler, Box r, int step)
{
    if (step <= 1)
        return readRgb(sampler, r);
    const int w = r.width(), h = r.height();
    const int ow = (w + step - 1) / step, oh = (h + step - 1) / step;
    const int rowsPer = std::max((64 + step - 1) / step, 1);
    const int strips = (oh + rowsPer - 1) / rowsPer;
    std::vector<std::vector<Rgb>> parts(static_cast<size_t>(strips));
    parallelRows(0, strips, [&](int s0, int s1) {
        for (int si = s0; si < s1; ++si) {
            const int oy0 = si * rowsPer, oy1 = std::min(oy0 + rowsPer, oh);
            const int y0 = r.y0 + oy0 * step, y1 = std::min(r.y0 + oy1 * step, r.y1);
            parts[size_t(si)] = downsampleRgb(readRgb(sampler, {r.x0, y0, r.x1, y1}), step).px;
        }
    }, 1);
    RgbImage out{ow, oh, {}};
    out.px.reserve(size_t(ow) * oh);
    for (auto& p : parts)
        out.px.insert(out.px.end(), p.begin(), p.end());
    return out;
}

int scaleFor(Box area, int maxPx)
{
    const double n = double(area.width()) * double(area.height());
    return std::max(int(std::ceil(std::sqrt(n / double(std::max(maxPx, 1))))), 1);
}

template <typename T>
std::vector<T> subsample(const std::vector<T>& v, size_t max)
{
    if (v.size() <= max)
        return v;
    const size_t k = (v.size() + max - 1) / max;
    std::vector<T> out;
    for (size_t i = 0; i < v.size(); i += k)
        out.push_back(v[i]);
    return out;
}

// Números pseudoaleatorios (xorshift) para el muestreo.
struct XorRng {
    uint64_t s;
    explicit XorRng(uint64_t seed) : s((seed * 0x9E3779B97F4A7C15ull) | 1) {}
    uint64_t next()
    {
        uint64_t x = s;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        s = x;
        return x;
    }
    float unit() { return float(next() >> 40) / float(1ull << 24); }
};

// ---------------------------------------------------------------------------------------------
// Mezclas de gaussianas sobre RGB (0..1) con covarianzas completas.

constexpr float kDefaultReg = 2e-5f;

struct Component {
    float weight = 0.f;
    Rgb mean{};
    double inv[3][3] = {};
    double logNorm = 0.0;

    static std::optional<Component> make(double weight, const double mean[3], const double cov[3][3], double reg)
    {
        double c[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                c[i][j] = cov[i][j] + (i == j ? reg : 0.0);
        const double det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0])
                         + c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
        if (det <= 1e-30 || weight <= 0.0)
            return std::nullopt;
        Component k;
        k.inv[0][0] = (c[1][1] * c[2][2] - c[1][2] * c[2][1]) / det;
        k.inv[0][1] = (c[0][2] * c[2][1] - c[0][1] * c[2][2]) / det;
        k.inv[0][2] = (c[0][1] * c[1][2] - c[0][2] * c[1][1]) / det;
        k.inv[1][0] = (c[1][2] * c[2][0] - c[1][0] * c[2][2]) / det;
        k.inv[1][1] = (c[0][0] * c[2][2] - c[0][2] * c[2][0]) / det;
        k.inv[1][2] = (c[0][2] * c[1][0] - c[0][0] * c[1][2]) / det;
        k.inv[2][0] = (c[1][0] * c[2][1] - c[1][1] * c[2][0]) / det;
        k.inv[2][1] = (c[0][1] * c[2][0] - c[0][0] * c[2][1]) / det;
        k.inv[2][2] = (c[0][0] * c[1][1] - c[0][1] * c[1][0]) / det;
        k.logNorm = std::log(weight) - 0.5 * std::log(det) - 1.5 * std::log(2.0 * 3.14159265358979323846);
        k.weight = float(weight);
        for (int i = 0; i < 3; ++i)
            k.mean[size_t(i)] = float(mean[i]);
        return k;
    }
    double logWeighted(const Rgb& z) const
    {
        const double d[3] = {double(z[0] - mean[0]), double(z[1] - mean[1]), double(z[2] - mean[2])};
        const auto& m = inv;
        const double q = d[0] * (m[0][0] * d[0] + m[0][1] * d[1] + m[0][2] * d[2])
                       + d[1] * (m[1][0] * d[0] + m[1][1] * d[1] + m[1][2] * d[2])
                       + d[2] * (m[2][0] * d[0] + m[2][1] * d[1] + m[2][2] * d[2]);
        return logNorm - 0.5 * q;
    }
};

struct Acc {
    double n = 0.0;
    double s[3] = {};
    double ss[3][3] = {};
    void add(const Rgb& z0, double w)
    {
        const double z[3] = {z0[0], z0[1], z0[2]};
        n += w;
        for (int i = 0; i < 3; ++i) {
            s[i] += w * z[i];
            for (int j = 0; j < 3; ++j)
                ss[i][j] += w * z[i] * z[j];
        }
    }
    std::optional<Component> component(double total, float reg) const
    {
        if (n <= 0.0)
            return std::nullopt;
        const double m[3] = {s[0] / n, s[1] / n, s[2] / n};
        double c[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                c[i][j] = ss[i][j] / n - m[i] * m[j];
        return Component::make(n / total, m, c, double(reg));
    }
};

size_t nearest(const std::vector<Rgb>& centers, const Rgb& z)
{
    size_t best = 0;
    float bd = std::numeric_limits<float>::max();
    for (size_t i = 0; i < centers.size(); ++i) {
        const float d = d2(centers[i], z);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// k-medias con la siembra de k-means++ (semilla fija). Quita los grupos vacíos.
std::vector<Rgb> kmeans(const std::vector<Rgb>& samples, size_t k, int iters)
{
    XorRng rng(0x5EED + uint64_t(samples.size()));
    std::vector<Rgb> centers{samples[size_t(rng.next() % samples.size())]};
    std::vector<float> dist(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        dist[i] = d2(samples[i], centers[0]);
    while (centers.size() < k) {
        double total = 0.0;
        for (float d : dist)
            total += double(d);
        if (total <= 1e-12)
            break;
        double t = double(rng.unit()) * total;
        size_t pick = samples.size() - 1;
        for (size_t i = 0; i < dist.size(); ++i) {
            t -= double(dist[i]);
            if (t <= 0.0) {
                pick = i;
                break;
            }
        }
        const Rgb c = samples[pick];
        centers.push_back(c);
        for (size_t i = 0; i < dist.size(); ++i)
            dist[i] = std::min(dist[i], d2(samples[i], c));
    }
    for (int it = 0; it < iters; ++it) {
        std::vector<std::pair<std::array<double, 3>, size_t>> sums(centers.size(), {{0, 0, 0}, 0});
        for (const Rgb& z : samples) {
            const size_t i = nearest(centers, z);
            for (int c = 0; c < 3; ++c)
                sums[i].first[size_t(c)] += double(z[size_t(c)]);
            ++sums[i].second;
        }
        std::vector<Rgb> fresh;
        for (const auto& [s, n] : sums)
            if (n > 0)
                fresh.push_back({float(s[0] / double(n)), float(s[1] / double(n)), float(s[2] / double(n))});
        const bool done = fresh == centers;
        centers = std::move(fresh);
        if (done)
            break;
    }
    return centers;
}

struct Gmm {
    std::vector<Component> comps;
    float reg = kDefaultReg;

    static std::optional<Gmm> fromLabels(const std::vector<Rgb>& samples, const std::vector<size_t>& labels, size_t k, float reg)
    {
        std::vector<Acc> acc(k);
        for (size_t i = 0; i < samples.size(); ++i)
            acc[labels[i]].add(samples[i], 1.0);
        const double total = double(samples.size());
        Gmm g;
        g.reg = reg;
        for (const Acc& a : acc)
            if (auto c = a.component(total, reg))
                g.comps.push_back(*c);
        if (g.comps.empty())
            return std::nullopt;
        return g;
    }
    static std::optional<Gmm> fit(const std::vector<Rgb>& samples, size_t k, float reg, int emIters = 3)
    {
        if (samples.empty())
            return std::nullopt;
        k = std::clamp<size_t>(k, 1, samples.size());
        const auto centers = kmeans(samples, k, 10);
        std::vector<size_t> labels(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            labels[i] = nearest(centers, samples[i]);
        auto g = fromLabels(samples, labels, centers.size(), reg);
        if (!g)
            return std::nullopt;
        for (int i = 0; i < emIters; ++i)
            if (auto next = g->emStep(samples))
                g = std::move(next);
        return g;
    }
    std::optional<Gmm> emStep(const std::vector<Rgb>& samples) const
    {
        const size_t k = comps.size();
        std::vector<Acc> acc(k);
        std::vector<double> lw(k);
        for (const Rgb& z : samples) {
            double mx = std::numeric_limits<double>::lowest();
            for (size_t c = 0; c < k; ++c) {
                lw[c] = comps[c].logWeighted(z);
                mx = std::max(mx, lw[c]);
            }
            double sum = 0.0;
            for (double& l : lw) {
                l = std::exp(l - mx);
                sum += l;
            }
            for (size_t c = 0; c < k; ++c) {
                const double r = lw[c] / sum;
                if (r > 1e-6)
                    acc[c].add(z, r);
            }
        }
        const double total = double(samples.size());
        Gmm g;
        g.reg = reg;
        for (const Acc& a : acc)
            if (auto c = a.component(total, reg))
                g.comps.push_back(*c);
        if (g.comps.empty())
            return std::nullopt;
        return g;
    }
    size_t componentOf(const Rgb& z) const
    {
        size_t best = 0;
        double bv = std::numeric_limits<double>::lowest();
        for (size_t i = 0; i < comps.size(); ++i) {
            const double v = comps[i].logWeighted(z);
            if (v > bv) {
                bv = v;
                best = i;
            }
        }
        return best;
    }
    // La actualización de GrabCut: cada muestra a su componente más probable y se reestima.
    std::optional<Gmm> refit(const std::vector<Rgb>& samples) const
    {
        std::vector<size_t> labels(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            labels[i] = componentOf(samples[i]);
        return fromLabels(samples, labels, comps.size(), reg);
    }
    double logProb(const Rgb& z) const
    {
        double mx = std::numeric_limits<double>::lowest();
        double vals[16] = {};
        const size_t n = std::min<size_t>(comps.size(), 16);
        for (size_t i = 0; i < n; ++i) {
            vals[i] = comps[i].logWeighted(z);
            mx = std::max(mx, vals[i]);
        }
        double s = 0.0;
        for (size_t i = 0; i < n; ++i)
            s += std::exp(vals[i] - mx);
        return mx + std::log(s);
    }
    float negLog(const Rgb& z) const { return float(std::clamp(-logProb(z), -50.0, 200.0)); }
};

// ---------------------------------------------------------------------------------------------
// Flujo máximo / corte mínimo.

constexpr uint32_t kNone = UINT32_MAX;
constexpr uint32_t kTerminal = UINT32_MAX - 1;
constexpr uint32_t kOrphan = UINT32_MAX - 2;
constexpr uint32_t kInfDist = UINT32_MAX;
// Capacidades residuales iguales o menores cuentan como saturadas.
constexpr float kEps = 1e-5f;
constexpr uint32_t kCancelEvery = 1024;

class Graph {
public:
    Graph(size_t nodes, size_t edges)
        : first(nodes, kNone), trCap(nodes, 0.f), parent(nodes, kNone), isSink(nodes, 0), ts(nodes, 0), dist(nodes, 0),
          active(nodes, 0)
    {
        head.reserve(edges * 2);
        next.reserve(edges * 2);
        rCap.reserve(edges * 2);
    }
    size_t nodeCount() const { return first.size(); }
    void addTWeights(size_t i, float capSource, float capSink)
    {
        const float delta = trCap[i];
        float s = capSource, t = capSink;
        if (delta > 0.f)
            s += delta;
        else
            t -= delta;
        flow += double(std::min(s, t));
        trCap[i] = s - t;
    }
    void addEdge(size_t i, size_t j, float cap, float revCap)
    {
        const uint32_t a = uint32_t(head.size());
        head.push_back(uint32_t(j));
        next.push_back(first[i]);
        rCap.push_back(cap);
        first[i] = a;
        head.push_back(uint32_t(i));
        next.push_back(first[j]);
        rCap.push_back(revCap);
        first[j] = a + 1;
    }
    bool inSource(size_t i) const { return parent[i] != kNone && !isSink[i]; }

    // Falso si se canceló.
    bool maxflow(const Stop& stop)
    {
        if (nodeCount() >= 4096) {
            bool source = false, sink = false;
            for (float c : trCap) {
                if (!std::isfinite(c))
                    return bk(stop);
                source = source || c > kEps;
                sink = sink || c < -kEps;
            }
            bool finite = true;
            for (float v : rCap)
                if (!std::isfinite(v) || v < 0.f) {
                    finite = false;
                    break;
                }
            if (source && sink && finite)
                return pushRelabel(stop);
        }
        return bk(stop);
    }

private:
    bool bk(const Stop& stop)
    {
        const size_t n = nodeCount();
        std::deque<uint32_t> queue, orphans;
        for (size_t i = 0; i < n; ++i) {
            active[i] = 0;
            ts[i] = 0;
            if (std::fabs(trCap[i]) > kEps) {
                isSink[i] = trCap[i] < 0.f;
                parent[i] = kTerminal;
                dist[i] = 1;
                active[i] = 1;
                queue.push_back(uint32_t(i));
            } else {
                parent[i] = kNone;
            }
        }
        uint32_t time = 0, steps = 0;
        while (!queue.empty()) {
            const size_t i = queue.front();
            queue.pop_front();
            ++steps;
            if (steps % kCancelEvery == 0 && stop.requested())
                return false;
            active[i] = 0;
            if (parent[i] == kNone)
                continue;
            uint32_t contact = kNone;
            const bool sink = isSink[i];
            for (uint32_t a = first[i]; a != kNone; a = next[a]) {
                const size_t j = head[a];
                const float cap = sink ? rCap[a ^ 1] : rCap[a];
                if (cap > kEps) {
                    if (parent[j] == kNone) {
                        isSink[j] = sink;
                        parent[j] = a ^ 1;
                        ts[j] = ts[i];
                        dist[j] = dist[i] + 1;
                        if (!active[j]) {
                            active[j] = 1;
                            queue.push_back(uint32_t(j));
                        }
                    } else if (bool(isSink[j]) != sink) {
                        contact = sink ? a ^ 1 : a;
                        break;
                    } else if (ts[j] <= ts[i] && dist[j] > dist[i]) {
                        parent[j] = a ^ 1;
                        ts[j] = ts[i];
                        dist[j] = dist[i] + 1;
                    }
                }
            }
            if (contact == kNone)
                continue;
            if (!active[i]) {
                active[i] = 1;
                queue.push_front(uint32_t(i));
            }
            ++time;
            augment(contact, orphans);
            while (!orphans.empty()) {
                const uint32_t o = orphans.front();
                orphans.pop_front();
                adopt(o, time, queue, orphans);
            }
        }
        return true;
    }

    void augment(uint32_t m, std::deque<uint32_t>& orphans)
    {
        const size_t u = head[m ^ 1], v = head[m];
        float b = rCap[m];
        size_t x = u;
        for (;;) {
            const uint32_t a = parent[x];
            if (a == kTerminal)
                break;
            b = std::min(b, rCap[a ^ 1]);
            x = head[a];
        }
        b = std::min(b, trCap[x]);
        x = v;
        for (;;) {
            const uint32_t a = parent[x];
            if (a == kTerminal)
                break;
            b = std::min(b, rCap[a]);
            x = head[a];
        }
        b = std::min(b, -trCap[x]);
        rCap[m ^ 1] += b;
        rCap[m] -= b;
        x = u;
        for (;;) {
            const uint32_t a = parent[x];
            if (a == kTerminal)
                break;
            rCap[a] += b;
            rCap[a ^ 1] -= b;
            const size_t up = head[a];
            if (rCap[a ^ 1] <= kEps) {
                parent[x] = kOrphan;
                orphans.push_back(uint32_t(x));
            }
            x = up;
        }
        trCap[x] -= b;
        if (trCap[x] <= kEps) {
            parent[x] = kOrphan;
            orphans.push_back(uint32_t(x));
        }
        x = v;
        for (;;) {
            const uint32_t a = parent[x];
            if (a == kTerminal)
                break;
            rCap[a ^ 1] += b;
            rCap[a] -= b;
            const size_t up = head[a];
            if (rCap[a] <= kEps) {
                parent[x] = kOrphan;
                orphans.push_back(uint32_t(x));
            }
            x = up;
        }
        trCap[x] += b;
        if (trCap[x] >= -kEps) {
            parent[x] = kOrphan;
            orphans.push_back(uint32_t(x));
        }
        flow += double(b);
    }

    void adopt(size_t i, uint32_t time, std::deque<uint32_t>& queue, std::deque<uint32_t>& orphans)
    {
        const bool sink = isSink[i];
        uint32_t best = kNone, dMin = kInfDist;
        for (uint32_t a = first[i]; a != kNone; a = next[a]) {
            const float cap = sink ? rCap[a] : rCap[a ^ 1];
            const size_t j = head[a];
            if (cap > kEps && bool(isSink[j]) == sink && parent[j] != kNone) {
                uint32_t d = 0;
                size_t k = j;
                for (;;) {
                    if (ts[k] == time) {
                        d = d > UINT32_MAX - dist[k] ? UINT32_MAX : d + dist[k];
                        break;
                    }
                    const uint32_t pa = parent[k];
                    ++d;
                    if (pa == kTerminal) {
                        ts[k] = time;
                        dist[k] = 1;
                        break;
                    }
                    if (pa == kOrphan) {
                        d = kInfDist;
                        break;
                    }
                    k = head[pa];
                }
                if (d < kInfDist) {
                    if (d < dMin) {
                        best = a;
                        dMin = d;
                    }
                    size_t kk = j;
                    uint32_t dd = d;
                    while (ts[kk] != time) {
                        ts[kk] = time;
                        dist[kk] = dd;
                        --dd;
                        kk = head[parent[kk]];
                    }
                }
            }
        }
        if (best != kNone) {
            parent[i] = best;
            ts[i] = time;
            dist[i] = dMin + 1;
            return;
        }
        parent[i] = kNone;
        for (uint32_t a = first[i]; a != kNone; a = next[a]) {
            const size_t j = head[a];
            const uint32_t pj = parent[j];
            if (bool(isSink[j]) == sink && pj != kNone) {
                const float cap = sink ? rCap[a] : rCap[a ^ 1];
                if (cap > kEps && !active[j]) {
                    active[j] = 1;
                    queue.push_back(uint32_t(j));
                }
                if (pj != kTerminal && pj != kOrphan && head[pj] == i) {
                    parent[j] = kOrphan;
                    orphans.push_back(uint32_t(j));
                }
            }
        }
    }

    bool pushRelabel(const Stop& stop)
    {
        const size_t n = nodeCount();
        if (n >= UINT32_MAX / 2)
            return bk(stop);
        const uint32_t sourceHeight = uint32_t(n) + 2;
        std::vector<float> back(n, 0.f);
        std::vector<double> excess(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            active[i] = 0;
            if (trCap[i] > kEps) {
                back[i] = trCap[i];
                excess[i] = double(trCap[i]);
                trCap[i] = 0.f;
            }
        }
        globalRelabel(sourceHeight, back);
        std::deque<size_t> queue;
        for (size_t i = 0; i < n; ++i)
            if (excess[i] > double(kEps)) {
                active[i] = 1;
                queue.push_back(i);
            }
        size_t work = 0;
        const size_t frequency = rCap.size() * 2 + n;
        uint32_t steps = 0;
        while (!queue.empty()) {
            const size_t i = queue.front();
            queue.pop_front();
            ++steps;
            if (steps % kCancelEvery == 0 && stop.requested())
                return false;
            active[i] = 0;
            while (excess[i] > double(kEps)) {
                if (dist[i] == 1 && trCap[i] < -kEps) {
                    const float delta = std::min(float(excess[i]), -trCap[i]);
                    trCap[i] += delta;
                    excess[i] -= double(delta);
                    flow += double(delta);
                    if (excess[i] <= double(kEps))
                        break;
                }
                if (dist[i] == sourceHeight + 1 && back[i] > kEps) {
                    const float delta = std::min(float(excess[i]), back[i]);
                    back[i] -= delta;
                    trCap[i] += delta;
                    excess[i] -= double(delta);
                    if (excess[i] <= double(kEps))
                        break;
                }
                uint32_t a = parent[i];
                while (a != kNone) {
                    const size_t j = head[a];
                    ++work;
                    if (rCap[a] > kEps && dist[j] != UINT32_MAX && dist[j] + 1 == dist[i]) {
                        const float delta = std::min(float(excess[i]), rCap[a]);
                        rCap[a] -= delta;
                        rCap[a ^ 1] += delta;
                        excess[i] -= double(delta);
                        excess[j] += double(delta);
                        if (!active[j] && excess[j] > double(kEps)) {
                            active[j] = 1;
                            queue.push_back(j);
                        }
                        if (excess[i] <= double(kEps))
                            break;
                    }
                    a = next[a];
                    parent[i] = a;
                }
                if (excess[i] <= double(kEps))
                    break;
                uint32_t height = trCap[i] < -kEps ? 0 : UINT32_MAX;
                if (back[i] > kEps)
                    height = std::min(height, sourceHeight);
                for (uint32_t b = first[i]; b != kNone; b = next[b]) {
                    ++work;
                    if (rCap[b] > kEps)
                        height = std::min(height, dist[head[b]]);
                }
                if (height == UINT32_MAX)
                    break;
                dist[i] = height == UINT32_MAX ? UINT32_MAX : height + 1;
                parent[i] = first[i];
            }
            if (work >= frequency) {
                globalRelabel(sourceHeight, back);
                work = 0;
            }
        }
        std::fill(parent.begin(), parent.end(), kNone);
        std::fill(isSink.begin(), isSink.end(), 1);
        std::deque<size_t> q;
        for (size_t i = 0; i < n; ++i)
            if (trCap[i] > kEps) {
                parent[i] = kTerminal;
                isSink[i] = 0;
                q.push_back(i);
            }
        while (!q.empty()) {
            const size_t i = q.front();
            q.pop_front();
            for (uint32_t a = first[i]; a != kNone; a = next[a]) {
                const size_t j = head[a];
                if (rCap[a] > kEps && parent[j] == kNone) {
                    parent[j] = kTerminal;
                    isSink[j] = 0;
                    q.push_back(j);
                }
            }
        }
        return true;
    }

    void globalRelabel(uint32_t sourceHeight, const std::vector<float>& back)
    {
        std::fill(dist.begin(), dist.end(), UINT32_MAX);
        parent = first;
        std::deque<size_t> queue;
        for (size_t i = 0; i < nodeCount(); ++i)
            if (trCap[i] < -kEps) {
                dist[i] = 1;
                queue.push_back(i);
            }
        auto bfs = [&] {
            while (!queue.empty()) {
                const size_t i = queue.front();
                queue.pop_front();
                for (uint32_t a = first[i]; a != kNone; a = next[a]) {
                    const size_t j = head[a];
                    if (rCap[a ^ 1] > kEps && dist[j] == UINT32_MAX) {
                        dist[j] = dist[i] == UINT32_MAX ? UINT32_MAX : dist[i] + 1;
                        queue.push_back(j);
                    }
                }
            }
        };
        bfs();
        for (size_t i = 0; i < back.size(); ++i)
            if (back[i] > kEps && dist[i] == UINT32_MAX) {
                dist[i] = sourceHeight + 1;
                queue.push_back(i);
            }
        bfs();
    }

    std::vector<uint32_t> first;
    std::vector<float> trCap;
    std::vector<uint32_t> parent;
    std::vector<uint8_t> isSink;
    std::vector<uint32_t> ts;
    std::vector<uint32_t> dist;
    std::vector<uint8_t> active;
    std::vector<uint32_t> head;
    std::vector<uint32_t> next;
    std::vector<float> rCap;
    double flow = 0.0;
};

// ---------------------------------------------------------------------------------------------
// Corte de una cuadrícula.

constexpr uint8_t kFree = 0;
constexpr uint8_t kHardFg = 1;
constexpr uint8_t kHardBg = 2;
struct Nb {
    int dx, dy;
    float len;
};
constexpr Nb kNb[4] = {{1, 0, 1.f}, {0, 1, 1.f}, {1, 1, 1.41421356237309504880f}, {-1, 1, 1.41421356237309504880f}};

float contrastBeta(const RgbImage& img)
{
    double sum = 0.0;
    uint64_t n = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const Rgb& a = img.px[size_t(y) * img.w + x];
            for (const Nb& nb : kNb) {
                const int nx = x + nb.dx, ny = y + nb.dy;
                if (nx >= 0 && nx < img.w && ny < img.h) {
                    sum += double(d2(a, img.px[size_t(ny) * img.w + nx]));
                    ++n;
                }
            }
        }
    if (n == 0 || sum <= 1e-12)
        return 0.f;
    return float(double(n) / (2.0 * sum));
}

// Etiquetado binario por un corte mínimo (Boykov y Jolly): verdadero para el objeto. Vacío si se
// canceló.
std::optional<std::vector<uint8_t>> gridCut(const RgbImage& img, const std::vector<float>& costFg,
                                            const std::vector<float>& costBg, const std::vector<uint8_t>& fixed,
                                            float gamma, float beta, const Stop& stop)
{
    const size_t n = size_t(img.w) * img.h;
    std::vector<uint32_t> idx(n, UINT32_MAX);
    uint32_t count = 0;
    for (size_t i = 0; i < n; ++i)
        if (fixed[i] == kFree)
            idx[i] = count++;
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = fixed[i] == kHardFg;
    if (count == 0)
        return out;
    if (stop.requested())
        return std::nullopt;
    Graph g(count, size_t(count) * 4);
    for (size_t i = 0; i < n; ++i)
        if (idx[i] != UINT32_MAX)
            g.addTWeights(idx[i], costBg[i], costFg[i]);
    auto fixedLink = [&](uint32_t node, uint8_t f, float wt) {
        if (f == kHardFg)
            g.addTWeights(node, wt, 0.f);
        else
            g.addTWeights(node, 0.f, wt);
    };
    // Los enlaces que tocan un píxel libre, en orden de barrido.
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const size_t i = size_t(y) * img.w + x;
            const Rgb& a = img.px[i];
            for (const Nb& nb : kNb) {
                const int nx = x + nb.dx, ny = y + nb.dy;
                if (nx < 0 || nx >= img.w || ny >= img.h)
                    continue;
                const size_t j = size_t(ny) * img.w + nx;
                if (idx[i] == UINT32_MAX && idx[j] == UINT32_MAX)
                    continue;
                const float wt = gamma * std::exp(-beta * d2(a, img.px[j])) / nb.len;
                const bool fi = idx[i] != UINT32_MAX, fj = idx[j] != UINT32_MAX;
                if (fi && fj)
                    g.addEdge(idx[i], idx[j], wt, wt);
                else if (fi)
                    fixedLink(idx[i], fixed[j], wt);
                else
                    fixedLink(idx[j], fixed[i], wt);
            }
        }
    if (stop.requested())
        return std::nullopt;
    if (!g.maxflow(stop))
        return std::nullopt;
    for (size_t i = 0; i < n; ++i)
        if (idx[i] != UINT32_MAX)
            out[i] = g.inSource(idx[i]);
    return out;
}

std::pair<std::vector<float>, std::vector<float>> dataCosts(const RgbImage& img, const Gmm& fg, const Gmm& bg)
{
    std::vector<float> cf(img.px.size()), cb(img.px.size());
    parallelRows(0, img.h, [&](int y0, int y1) {
        for (size_t i = size_t(y0) * img.w; i < size_t(y1) * img.w; ++i) {
            cf[i] = fg.negLog(img.px[i]);
            cb[i] = bg.negLog(img.px[i]);
        }
    });
    return {cf, cb};
}

// Componentes 8-conexas (0 = fondo) y sus tamaños.
std::pair<std::vector<uint32_t>, std::vector<size_t>> components8(const std::vector<uint8_t>& mask, int w, int h)
{
    std::vector<uint32_t> lab(size_t(w) * h, 0);
    std::vector<size_t> sizes{0};
    std::vector<size_t> stack;
    for (size_t s = 0; s < lab.size(); ++s) {
        if (!mask[s] || lab[s] != 0)
            continue;
        const uint32_t l = uint32_t(sizes.size());
        sizes.push_back(0);
        lab[s] = l;
        stack.push_back(s);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            ++sizes[l];
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const size_t j = size_t(ny) * w + nx;
                    if (mask[j] && lab[j] == 0) {
                        lab[j] = l;
                        stack.push_back(j);
                    }
                }
        }
    }
    return {lab, sizes};
}

std::pair<std::vector<uint32_t>, std::vector<size_t>> components4(const std::vector<uint8_t>& mask, int w, int h)
{
    std::vector<uint32_t> lab(size_t(w) * h, 0);
    std::vector<size_t> sizes{0};
    std::vector<size_t> stack;
    for (size_t s = 0; s < lab.size(); ++s) {
        if (!mask[s] || lab[s] != 0)
            continue;
        const uint32_t l = uint32_t(sizes.size());
        sizes.push_back(0);
        lab[s] = l;
        stack.push_back(s);
        while (!stack.empty()) {
            const size_t i = stack.back();
            stack.pop_back();
            ++sizes[l];
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const long long nbs[4] = {x > 0 ? (long long)i - 1 : -1, x + 1 < w ? (long long)i + 1 : -1,
                                      y > 0 ? (long long)i - w : -1, y + 1 < h ? (long long)i + w : -1};
            for (long long j : nbs)
                if (j >= 0 && mask[size_t(j)] && lab[size_t(j)] == 0) {
                    lab[size_t(j)] = l;
                    stack.push_back(static_cast<size_t>(j));
                }
        }
    }
    return {lab, sizes};
}

// Se queda con las componentes que tienen una semilla.
std::vector<uint8_t> keepSeeded(const std::vector<uint8_t>& mask, const std::vector<uint8_t>& seeds, int w, int h)
{
    const auto [lab, sizes] = components8(mask, w, h);
    std::vector<uint8_t> keep(sizes.size(), 0);
    for (size_t i = 0; i < lab.size(); ++i)
        if (seeds[i] && lab[i] != 0)
            keep[lab[i]] = 1;
    std::vector<uint8_t> out(lab.size());
    for (size_t i = 0; i < lab.size(); ++i)
        out[i] = lab[i] != 0 && keep[lab[i]];
    return out;
}

std::vector<uint8_t> borderTouches(const std::vector<uint32_t>& hl, size_t labels, int w, int h)
{
    std::vector<uint8_t> touches(labels, 0);
    for (int x = 0; x < w; ++x) {
        touches[hl[size_t(x)]] = 1;
        touches[hl[size_t(h - 1) * w + x]] = 1;
    }
    for (int y = 0; y < h; ++y) {
        touches[hl[size_t(y) * w]] = 1;
        touches[hl[size_t(y) * w + w - 1]] = 1;
    }
    return touches;
}

// Se queda con las componentes de al menos 'frac' de la mayor y tapa los agujeros (fondo
// 4-conexo que no toca el borde) de hasta 'holeFrac' del área.
std::vector<uint8_t> cleanMask(const std::vector<uint8_t>& mask, int w, int h, float frac, float holeFrac)
{
    const auto [lab, sizes] = components8(mask, w, h);
    size_t largest = 0;
    for (size_t i = 1; i < sizes.size(); ++i)
        largest = std::max(largest, sizes[i]);
    if (largest == 0)
        return std::vector<uint8_t>(size_t(w) * h, 0);
    const size_t min = size_t(std::ceil(float(largest) * frac));
    std::vector<uint8_t> out(lab.size());
    size_t area = 0;
    for (size_t i = 0; i < lab.size(); ++i) {
        out[i] = lab[i] != 0 && sizes[lab[i]] >= min;
        area += out[i];
    }
    const size_t maxHole = size_t(float(area) * holeFrac);
    std::vector<uint8_t> inv(out.size());
    for (size_t i = 0; i < out.size(); ++i)
        inv[i] = !out[i];
    const auto [hl, hs] = components4(inv, w, h);
    const auto touches = borderTouches(hl, hs.size(), w, h);
    for (size_t i = 0; i < out.size(); ++i)
        if (hl[i] != 0 && !touches[hl[i]] && hs[hl[i]] <= maxHole)
            out[i] = 1;
    return out;
}

// Tapa los agujeros según cómo son: hasta 'speckFrac' del área siempre; hasta 'holeFrac' solo si
// sus píxeles encajan mejor, de media, con el modelo del objeto que con el del fondo.
std::vector<uint8_t> fillHolesByModel(const std::vector<uint8_t>& mask, const RgbImage& img, const Gmm& fg, const Gmm& bg,
                                      float speckFrac, float holeFrac)
{
    const int w = img.w, h = img.h;
    std::vector<uint8_t> out = mask;
    size_t area = 0;
    for (uint8_t v : out)
        area += v;
    const size_t maxSpeck = size_t(float(area) * speckFrac);
    const size_t maxHole = size_t(float(area) * holeFrac);
    std::vector<uint8_t> inv(out.size());
    for (size_t i = 0; i < out.size(); ++i)
        inv[i] = !out[i];
    const auto [hl, hs] = components4(inv, w, h);
    const auto touches = borderTouches(hl, hs.size(), w, h);
    std::vector<std::pair<double, double>> cost(hs.size(), {0.0, 0.0});
    for (size_t i = 0; i < hl.size(); ++i) {
        const size_t l = hl[i];
        if (l == 0 || touches[l] || hs[l] > maxHole || hs[l] <= maxSpeck)
            continue;
        cost[l].first += double(fg.negLog(img.px[i]));
        cost[l].second += double(bg.negLog(img.px[i]));
    }
    for (size_t i = 0; i < out.size(); ++i) {
        const size_t l = hl[i];
        if (l == 0 || touches[l] || hs[l] > maxHole)
            continue;
        if (hs[l] <= maxSpeck || cost[l].first <= cost[l].second)
            out[i] = 1;
    }
    return out;
}

// Suaviza los bordes de una máscara 0/255 (la regla del suavizado de la varita mágica).
void antialiasMask(Bytes& mask, int w, int h)
{
    const Bytes src = mask;
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < w; ++x) {
                const uint8_t v = src[size_t(y) * w + x];
                uint32_t sum = 0;
                bool edge = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        const uint8_t nv = nx < 0 || ny < 0 || nx >= w || ny >= h ? v : src[size_t(ny) * w + nx];
                        edge = edge || nv != v;
                        sum += nv;
                    }
                if (edge) {
                    const float avg = float(sum) / (9.f * 255.f);
                    const float f = v > 127 ? 0.5f + 0.5f * avg : 0.5f * avg;
                    mask[size_t(y) * w + x] = uint8_t(std::lround(f * 255.f));
                }
            }
    });
}

std::optional<Region> trimRegion(Region r)
{
    const int w = r.box.width();
    if (w == 0)
        return std::nullopt;
    const int h = int(r.mask.size() / size_t(w));
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = 0, y1 = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = r.mask.data() + size_t(y) * w;
        int a = -1, b = -1;
        for (int x = 0; x < w; ++x)
            if (row[x]) {
                if (a < 0)
                    a = x;
                b = x;
            }
        if (a >= 0) {
            x0 = std::min(x0, a);
            x1 = std::max(x1, b + 1);
            y0 = std::min(y0, y);
            y1 = y + 1;
        }
    }
    if (x0 == INT32_MAX)
        return std::nullopt;
    if (x0 == 0 && y0 == 0 && x1 == w && y1 == h)
        return r;
    const int nw = x1 - x0;
    Bytes mask;
    mask.reserve(size_t(nw) * (y1 - y0));
    for (int y = y0; y < y1; ++y)
        mask.insert(mask.end(), r.mask.begin() + long(size_t(y) * w + x0), r.mask.begin() + long(size_t(y) * w + x1));
    const Box b = r.box;
    return Region{{b.x0 + x0, b.y0 + y0, b.x0 + x1, b.y0 + y1}, std::move(mask)};
}

// El corte a resolución completa en la banda del borde (por mosaicos).
void refineBand(const Sampler& sampler, Box window, int step, const std::vector<uint8_t>& low,
                const std::vector<uint8_t>& band, int lw, int lh, const Gmm& fg, const Gmm& bg, Bytes& mask,
                const Stop& stop)
{
    constexpr int T = 192;
    constexpr int kHalo = 12;
    const int w = window.width();
    auto block = [&](int x, int y) {
        return size_t(std::min((y - window.y0) / step, lh - 1)) * lw + size_t(std::min((x - window.x0) / step, lw - 1));
    };
    std::vector<Box> tiles;
    for (int ty = window.y0; ty < window.y1; ty += T)
        for (int tx = window.x0; tx < window.x1; tx += T) {
            const Box t{tx, ty, std::min(tx + T, window.x1), std::min(ty + T, window.y1)};
            const int bx0 = (t.x0 - window.x0) / step, by0 = (t.y0 - window.y0) / step;
            const int bx1 = std::min((t.x1 - 1 - window.x0) / step, lw - 1);
            const int by1 = std::min((t.y1 - 1 - window.y0) / step, lh - 1);
            bool any = false;
            for (int by = by0; by <= by1 && !any; ++by)
                for (int bx = bx0; bx <= bx1; ++bx)
                    if (band[size_t(by) * lw + bx]) {
                        any = true;
                        break;
                    }
            if (any)
                tiles.push_back(t);
        }
    std::vector<std::vector<uint8_t>> cores(tiles.size());
    std::atomic_bool cancelled{false};
    parallelRows(0, int(tiles.size()), [&](int k0, int k1) {
        for (int k = k0; k < k1; ++k) {
            if (stop.requested()) {
                cancelled = true;
                return;
            }
            const Box t = tiles[size_t(k)];
            const Box r = t.inflate(kHalo).intersect(window);
            const RgbImage img = readRgb(sampler, r);
            const int rw = r.width(), rh = r.height();
            std::vector<uint8_t> fixed(size_t(rw) * rh, kFree);
            std::vector<float> cfg(fixed.size(), 0.f), cbg(fixed.size(), 0.f);
            for (int yy = 0; yy < rh; ++yy)
                for (int xx = 0; xx < rw; ++xx) {
                    const size_t i = size_t(yy) * rw + xx;
                    const size_t b = block(r.x0 + xx, r.y0 + yy);
                    const bool lab = low[b];
                    if (!band[b]) {
                        fixed[i] = lab ? kHardFg : kHardBg;
                        continue;
                    }
                    const Rgb& p = img.px[i];
                    cfg[i] = fg.negLog(p) + (lab ? 0.f : 0.4f);
                    cbg[i] = bg.negLog(p) + (lab ? 0.4f : 0.f);
                }
            const float beta = contrastBeta(img);
            const auto cut = gridCut(img, cfg, cbg, fixed, 50.f, beta, stop);
            if (!cut) {
                cancelled = true;
                return;
            }
            std::vector<uint8_t> core;
            core.reserve(size_t(t.width()) * t.height());
            for (int yy = 0; yy < t.height(); ++yy) {
                const size_t sy = size_t(t.y0 - r.y0 + yy), sx = size_t(t.x0 - r.x0);
                core.insert(core.end(), cut->begin() + long(sy * rw + sx), cut->begin() + long(sy * rw + sx + t.width()));
            }
            cores[size_t(k)] = std::move(core);
        }
    }, 1);
    if (cancelled)
        throw Cancelled{};
    for (size_t k = 0; k < tiles.size(); ++k) {
        const Box t = tiles[k];
        const int cw = t.width();
        for (int yy = 0; yy < t.height(); ++yy) {
            const size_t o = size_t(t.y0 - window.y0 + yy) * w + (t.x0 - window.x0);
            for (int xx = 0; xx < cw; ++xx)
                mask[o + xx] = cores[k][size_t(yy) * cw + xx] ? 255 : 0;
        }
    }
}

// Del etiquetado de trabajo sobre 'window' (muestreado con 'step') a una región a resolución
// completa: con step > 1, el borde se vuelve a cortar en una banda de unos 'step' píxeles.
std::optional<Region> finishRegion(const Sampler& sampler, Box window, int step, const std::vector<uint8_t>& low, int lw,
                                   int lh, const Gmm* fg, const Gmm* bg, const Stop& stop)
{
    const int w = window.width(), h = window.height();
    Bytes mask(size_t(w) * h, 0);
    if (step <= 1) {
        for (size_t i = 0; i < mask.size(); ++i)
            mask[i] = low[i] ? 255 : 0;
    } else {
        std::vector<uint8_t> band(size_t(lw) * lh, 0);
        for (int y = 0; y < lh; ++y)
            for (int x = 0; x < lw; ++x) {
                const uint8_t v = low[size_t(y) * lw + x];
                bool diff = false;
                for (int dy = -1; dy <= 1 && !diff; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < lw && ny < lh && low[size_t(ny) * lw + nx] != v) {
                            diff = true;
                            break;
                        }
                    }
                band[size_t(y) * lw + x] = diff;
            }
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const int ly = std::min(y / step, lh - 1);
                for (int x = 0; x < w; ++x)
                    mask[size_t(y) * w + x] = low[size_t(ly) * lw + std::min(x / step, lw - 1)] ? 255 : 0;
            }
        });
        if (fg && bg)
            refineBand(sampler, window, step, low, band, lw, lh, *fg, *bg, mask, stop);
    }
    antialiasMask(mask, w, h);
    return trimRegion(Region{window, std::move(mask)});
}

// ---------------------------------------------------------------------------------------------
// GrabCut.

constexpr uint8_t BG = 0, FG = 1, PR_BG = 2, PR_FG = 3;
constexpr float kGamma = 50.f;
constexpr size_t K = 5;
constexpr size_t kMaxSamples = 40000;

bool isFg(uint8_t t)
{
    return t == FG || t == PR_FG;
}

std::optional<std::pair<Gmm, Gmm>> grabcut(const RgbImage& img, std::vector<uint8_t>& trimap, int iters, const Stop& stop)
{
    const float beta = contrastBeta(img);
    std::vector<uint8_t> fixed(trimap.size());
    for (size_t i = 0; i < trimap.size(); ++i)
        fixed[i] = trimap[i] == FG ? kHardFg : trimap[i] == BG ? kHardBg : kFree;
    std::optional<std::pair<Gmm, Gmm>> models;
    for (int it = 0; it < std::max(iters, 1); ++it) {
        stop.check();
        std::vector<Rgb> fs, bs;
        for (size_t i = 0; i < img.px.size(); ++i)
            (isFg(trimap[i]) ? fs : bs).push_back(img.px[i]);
        fs = subsample(fs, kMaxSamples);
        bs = subsample(bs, kMaxSamples);
        Gmm fg, bg;
        if (!models) {
            auto f = Gmm::fit(fs, K, kDefaultReg), b = Gmm::fit(bs, K, kDefaultReg);
            if (!f || !b)
                return std::nullopt;
            fg = std::move(*f);
            bg = std::move(*b);
        } else {
            auto f = models->first.refit(fs), b = models->second.refit(bs);
            fg = f ? std::move(*f) : models->first;
            bg = b ? std::move(*b) : models->second;
        }
        const auto [cf, cb] = dataCosts(img, fg, bg);
        const auto cut = gridCut(img, cf, cb, fixed, kGamma, beta, stop);
        if (!cut)
            throw Cancelled{};
        size_t changed = 0;
        for (size_t i = 0; i < trimap.size(); ++i)
            if (trimap[i] == PR_BG || trimap[i] == PR_FG) {
                const uint8_t nt = (*cut)[i] ? PR_FG : PR_BG;
                changed += nt != trimap[i];
                trimap[i] = nt;
            }
        models = std::pair<Gmm, Gmm>{std::move(fg), std::move(bg)};
        if (changed * 2000 < trimap.size())
            break;
    }
    return models;
}

// ---------------------------------------------------------------------------------------------
// CIELAB y SLIC.

Rgb rgbToLab(const Rgb& c)
{
    auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    const float r = lin(c[0]), g = lin(c[1]), b = lin(c[2]);
    const float x = (0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f;
    const float y = 0.2126729f * r + 0.7151522f * g + 0.072175f * b;
    const float z = (0.0193339f * r + 0.119192f * g + 0.9503041f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.f / 116.f; };
    const float fx = f(x), fy = f(y), fz = f(z);
    return {116.f * fy - 16.f, 500.f * (fx - fy), 200.f * (fy - fz)};
}

std::vector<Rgb> box3(const std::vector<Rgb>& px, int w, int h)
{
    std::vector<Rgb> out(px.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Rgb acc{0, 0, 0};
            float n = 0.f;
            for (int yy = std::max(y - 1, 0); yy < std::min(y + 2, h); ++yy)
                for (int xx = std::max(x - 1, 0); xx < std::min(x + 2, w); ++xx) {
                    const Rgb& p = px[size_t(yy) * w + xx];
                    acc = {acc[0] + p[0], acc[1] + p[1], acc[2] + p[2]};
                    n += 1.f;
                }
            const float d = std::max(n, 1.f);
            out[size_t(y) * w + x] = {acc[0] / d, acc[1] / d, acc[2] / d};
        }
    return out;
}

struct Superpixels {
    std::vector<uint32_t> labels;
    size_t count = 0;
};

std::pair<std::vector<uint32_t>, size_t> enforceConnectivity(const std::vector<uint32_t>& labels, int w, int h, size_t minSize)
{
    const size_t n = size_t(w) * h;
    std::vector<uint32_t> out(n, UINT32_MAX);
    uint32_t next = 0;
    std::vector<size_t> comp;
    for (size_t s = 0; s < n; ++s) {
        if (out[s] != UINT32_MAX)
            continue;
        const int sx = int(s % size_t(w)), sy = int(s / size_t(w));
        std::optional<uint32_t> adj;
        if (sx > 0 && out[s - 1] != UINT32_MAX)
            adj = out[s - 1];
        if (sy > 0 && out[s - w] != UINT32_MAX)
            adj = out[s - w];
        comp.clear();
        comp.push_back(s);
        out[s] = next;
        size_t k = 0;
        while (k < comp.size()) {
            const size_t i = comp[k++];
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const long long nbs[4] = {x > 0 ? (long long)i - 1 : -1, x + 1 < w ? (long long)i + 1 : -1,
                                      y > 0 ? (long long)i - w : -1, y + 1 < h ? (long long)i + w : -1};
            for (long long j : nbs)
                if (j >= 0 && out[size_t(j)] == UINT32_MAX && labels[size_t(j)] == labels[s]) {
                    out[size_t(j)] = next;
                    comp.push_back(static_cast<size_t>(j));
                }
        }
        if (adj && comp.size() < minSize) {
            for (size_t i : comp)
                out[i] = *adj;
        } else {
            ++next;
        }
    }
    return {out, size_t(next)};
}

Superpixels slicLab(const std::vector<Rgb>& lab, int w, int h, size_t target, float compactness, int iters)
{
    const size_t n = size_t(w) * h;
    if (n == 0)
        return {};
    const float s = std::max(std::sqrt(float(n) / float(std::max<size_t>(target, 1))), 1.f);
    auto grad = [&](int x, int y) {
        auto at = [&](int xx, int yy) -> const Rgb& { return lab[size_t(std::min(yy, h - 1)) * w + std::min(xx, w - 1)]; };
        const Rgb& l = at(std::max(x - 1, 0), y);
        const Rgb& r = at(x + 1, y);
        const Rgb& u = at(x, std::max(y - 1, 0));
        const Rgb& d = at(x, y + 1);
        return d2(l, r) + d2(u, d);
    };
    std::vector<std::array<float, 5>> centers;
    for (float y = s / 2.f; y < float(h); y += s)
        for (float x = s / 2.f; x < float(w); x += s) {
            const int cx = int(x), cy = int(y);
            int bx = cx, by = cy;
            float bg = std::numeric_limits<float>::max();
            for (int yy = std::max(cy - 1, 0); yy <= std::min(cy + 1, h - 1); ++yy)
                for (int xx = std::max(cx - 1, 0); xx <= std::min(cx + 1, w - 1); ++xx) {
                    const float g = grad(xx, yy);
                    if (g < bg) {
                        bx = xx;
                        by = yy;
                        bg = g;
                    }
                }
            const Rgb& c = lab[size_t(by) * w + bx];
            centers.push_back({c[0], c[1], c[2], float(bx), float(by)});
        }
    std::vector<uint32_t> labels(n, UINT32_MAX);
    std::vector<float> dist(n);
    const float wxy = (compactness / s) * (compactness / s);
    const long long r = (long long)std::ceil(s);
    for (int it = 0; it < std::max(iters, 1); ++it) {
        std::fill(dist.begin(), dist.end(), std::numeric_limits<float>::max());
        for (size_t k = 0; k < centers.size(); ++k) {
            const auto& c = centers[k];
            const long long cx = std::llround(c[3]), cy = std::llround(c[4]);
            for (long long yy = std::max(cy - r, 0ll); yy < std::min(cy + r + 1, (long long)h); ++yy)
                for (long long xx = std::max(cx - r, 0ll); xx < std::min(cx + r + 1, (long long)w); ++xx) {
                    const size_t i = size_t(yy) * w + size_t(xx);
                    const Rgb& p = lab[i];
                    const float dc = d2(p, {c[0], c[1], c[2]});
                    const float dx = float(xx) - c[3], dy = float(yy) - c[4];
                    const float d = dc + (dx * dx + dy * dy) * wxy;
                    if (d < dist[i]) {
                        dist[i] = d;
                        labels[i] = uint32_t(k);
                    }
                }
        }
        std::vector<std::array<double, 6>> sums(centers.size(), {0, 0, 0, 0, 0, 0});
        for (size_t i = 0; i < n; ++i) {
            if (labels[i] == UINT32_MAX)
                continue;
            const Rgb& p = lab[i];
            auto& sm = sums[labels[i]];
            sm[0] += p[0];
            sm[1] += p[1];
            sm[2] += p[2];
            sm[3] += double(i % size_t(w));
            sm[4] += double(i / size_t(w));
            sm[5] += 1.0;
        }
        for (size_t k = 0; k < centers.size(); ++k)
            if (sums[k][5] > 0.0)
                for (int j = 0; j < 5; ++j)
                    centers[k][size_t(j)] = float(sums[k][size_t(j)] / sums[k][5]);
    }
    for (size_t i = 0; i < n; ++i)
        if (labels[i] == UINT32_MAX)
            labels[i] = i > 0 ? labels[i - 1] : 0;
    auto [lbl, count] = enforceConnectivity(labels, w, h, size_t((s * s) / 4.f));
    return {std::move(lbl), count};
}

// ---------------------------------------------------------------------------------------------
// Prominencia (para Seleccionar sujeto).

constexpr float kSigmaClr = 10.f;
constexpr float kSigmaSpa = 0.25f;

void dijkstra(size_t src, const std::vector<std::vector<size_t>>& adj, const std::vector<Rgb>& mean, std::vector<float>& dist)
{
    std::fill(dist.begin(), dist.end(), std::numeric_limits<float>::infinity());
    dist[src] = 0.f;
    using Item = std::pair<float, size_t>;
    auto cmp = [](const Item& a, const Item& b) { return a.first > b.first; };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> heap(cmp);
    heap.push({0.f, src});
    while (!heap.empty()) {
        const auto [d, u] = heap.top();
        heap.pop();
        if (d > dist[u])
            continue;
        for (size_t v : adj[u]) {
            const float nd = d + std::sqrt(d2(mean[u], mean[v]));
            if (nd < dist[v]) {
                dist[v] = nd;
                heap.push({nd, v});
            }
        }
    }
}

float otsu(const std::vector<float>& v)
{
    uint64_t hist[256] = {};
    for (float x : v)
        ++hist[size_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f))];
    const double total = double(v.size());
    double sum = 0.0;
    for (int i = 0; i < 256; ++i)
        sum += double(i) * double(hist[i]);
    double wb = 0.0, sb = 0.0, best = -1.0;
    int bt = 0;
    for (int t = 0; t < 256; ++t) {
        wb += double(hist[t]);
        if (wb == 0.0)
            continue;
        const double wf = total - wb;
        if (wf == 0.0)
            break;
        sb += double(t) * double(hist[t]);
        const double mb = sb / wb, mf = (sum - sb) / wf;
        const double between = wb * wf * (mb - mf) * (mb - mf);
        if (between > best) {
            best = between;
            bt = t;
        }
    }
    return (float(bt) + 0.5f) / 255.f;
}

std::vector<Rgb> blurLab(const std::vector<Rgb>& img, int w, int h)
{
    std::vector<Rgb> cur = img;
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<Rgb> tmp = cur;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Rgb& l = cur[size_t(y) * w + std::max(x - 1, 0)];
                const Rgb& c = cur[size_t(y) * w + x];
                const Rgb& r = cur[size_t(y) * w + std::min(x + 1, w - 1)];
                for (int k = 0; k < 3; ++k)
                    tmp[size_t(y) * w + x][size_t(k)] = (l[size_t(k)] + 2.f * c[size_t(k)] + r[size_t(k)]) / 4.f;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Rgb& u = tmp[size_t(std::max(y - 1, 0)) * w + x];
                const Rgb& c = tmp[size_t(y) * w + x];
                const Rgb& d = tmp[size_t(std::min(y + 1, h - 1)) * w + x];
                for (int k = 0; k < 3; ++k)
                    cur[size_t(y) * w + x][size_t(k)] = (u[size_t(k)] + 2.f * c[size_t(k)] + d[size_t(k)]) / 4.f;
            }
    }
    return cur;
}

std::vector<float> saliency(const RgbImage& img)
{
    const int w = img.w, h = img.h;
    std::vector<Rgb> lab(img.px.size());
    for (size_t i = 0; i < lab.size(); ++i)
        lab[i] = rgbToLab(img.px[i]);
    const size_t target = std::clamp<size_t>(size_t(w) * h / 150, 16, 400);
    const Superpixels sp = slicLab(lab, w, h, target, 10.f, 8);
    const size_t n = sp.count;
    std::vector<std::array<double, 3>> meanD(n, {0, 0, 0});
    std::vector<std::array<double, 2>> posD(n, {0, 0});
    std::vector<double> cnt(n, 0.0);
    std::vector<uint8_t> onBorder(n, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t l = sp.labels[size_t(y) * w + x];
            const Rgb& c = lab[size_t(y) * w + x];
            for (int k = 0; k < 3; ++k)
                meanD[l][size_t(k)] += double(c[size_t(k)]);
            posD[l][0] += double(x) / double(w);
            posD[l][1] += double(y) / double(h);
            cnt[l] += 1.0;
            if (x == 0 || y == 0 || x + 1 == w || y + 1 == h)
                onBorder[l] = 1;
        }
    std::vector<Rgb> mean(n);
    std::vector<std::array<float, 2>> pos(n);
    for (size_t l = 0; l < n; ++l) {
        const double c = std::max(cnt[l], 1.0);
        mean[l] = {float(meanD[l][0] / c), float(meanD[l][1] / c), float(meanD[l][2] / c)};
        pos[l] = {float(posD[l][0] / c), float(posD[l][1] / c)};
    }
    auto dapp = [&](size_t a, size_t b) { return std::sqrt(d2(mean[a], mean[b])); };
    std::vector<std::vector<size_t>> adj(n);
    auto link = [&](size_t a, size_t b) {
        if (a != b && std::find(adj[a].begin(), adj[a].end(), b) == adj[a].end()) {
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t l = sp.labels[size_t(y) * w + x];
            if (x + 1 < w)
                link(l, sp.labels[size_t(y) * w + x + 1]);
            if (y + 1 < h)
                link(l, sp.labels[size_t(y + 1) * w + x]);
        }
    std::vector<float> wbg(n, 0.f), geo(n);
    for (size_t p = 0; p < n; ++p) {
        dijkstra(p, adj, mean, geo);
        float area = 0.f, len = 0.f;
        for (size_t q = 0; q < n; ++q) {
            const float s = std::exp(-geo[q] * geo[q] / (2.f * kSigmaClr * kSigmaClr));
            area += s;
            if (onBorder[q])
                len += s;
        }
        const float bnd = len / std::sqrt(std::max(area, 1e-6f));
        wbg[p] = 1.f - std::exp(-bnd * bnd / 2.f);
    }
    std::vector<float> wctr(n, 0.f);
    for (size_t p = 0; p < n; ++p) {
        float s = 0.f;
        for (size_t q = 0; q < n; ++q) {
            const float dx = pos[p][0] - pos[q][0], dy = pos[p][1] - pos[q][1];
            const float wspa = std::exp(-(dx * dx + dy * dy) / (2.f * kSigmaSpa * kSigmaSpa));
            s += dapp(p, q) * wspa * wbg[q];
        }
        wctr[p] = s;
    }
    float mx = 0.f;
    for (float v : wctr)
        mx = std::max(mx, v);
    mx = std::max(mx, 1e-6f);
    std::vector<float> wfg(n);
    for (size_t p = 0; p < n; ++p)
        wfg[p] = wctr[p] / mx;
    std::vector<std::vector<float>> wij(n);
    for (size_t p = 0; p < n; ++p)
        for (size_t q : adj[p]) {
            const float d = dapp(p, q);
            wij[p].push_back(std::exp(-(d * d) / (2.f * kSigmaClr * kSigmaClr)) + 0.1f);
        }
    std::vector<float> s = wfg;
    for (int it = 0; it < 200; ++it)
        for (size_t p = 0; p < n; ++p) {
            float num = wfg[p], den = wbg[p] + wfg[p];
            for (size_t k = 0; k < adj[p].size(); ++k) {
                num += wij[p][k] * s[adj[p][k]];
                den += wij[p][k];
            }
            s[p] = num / std::max(den, 1e-6f);
        }
    std::array<double, 3> meanAll{0, 0, 0};
    for (const Rgb& c : lab)
        for (int k = 0; k < 3; ++k)
            meanAll[size_t(k)] += double(c[size_t(k)]);
    const Rgb meanAllF{float(meanAll[0] / double(lab.size())), float(meanAll[1] / double(lab.size())),
                       float(meanAll[2] / double(lab.size()))};
    const std::vector<Rgb> blurred = blurLab(lab, w, h);
    std::vector<float> ft(blurred.size());
    float ftmax = 0.f;
    for (size_t i = 0; i < blurred.size(); ++i) {
        ft[i] = std::sqrt(d2(blurred[i], meanAllF));
        ftmax = std::max(ftmax, ft[i]);
    }
    ftmax = std::max(ftmax, 1e-6f);
    float smin = std::numeric_limits<float>::max(), smax = std::numeric_limits<float>::lowest();
    for (float v : s) {
        smin = std::min(smin, v);
        smax = std::max(smax, v);
    }
    const float srange = std::max(smax - smin, 1e-6f);
    std::vector<float> out(sp.labels.size());
    float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = 0.75f * (s[sp.labels[i]] - smin) / srange + 0.25f * ft[i] / ftmax;
        lo = std::min(lo, out[i]);
        hi = std::max(hi, out[i]);
    }
    const float rr = std::max(hi - lo, 1e-6f);
    for (float& v : out)
        v = (v - lo) / rr;
    return out;
}

// El segundo GrabCut de Seleccionar sujeto, con un fondo que solo conoce el telón (el marco de la
// imagen): así las partes del sujeto de color parecido al fondo empiezan como objeto.
std::optional<std::tuple<std::vector<uint8_t>, Gmm, Gmm>> backdropPass(const RgbImage& img, const std::vector<uint8_t>& blob,
                                                                       const Stop& stop)
{
    const int w = img.w, h = img.h;
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    bool any = false;
    for (size_t i = 0; i < blob.size(); ++i)
        if (blob[i]) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
            any = true;
        }
    if (!any)
        return std::nullopt;
    const int margin = std::max(int(std::ceil(float(std::max(x1 - x0 + 1, y1 - y0 + 1)) * 0.15f)), 4);
    const int bx0 = std::max(x0 - margin, 0), by0 = std::max(y0 - margin, 0);
    const int bx1 = std::min(x1 + margin, w - 1), by1 = std::min(y1 + margin, h - 1);
    const int frame = std::max(int(std::lround(float(std::min(w, h)) * 0.06f)), 2);
    auto inFrame = [&](int x, int y) { return x < frame || y < frame || x + frame >= w || y + frame >= h; };
    auto nearBlob = [&](int x, int y) { return x >= bx0 && x <= bx1 && y >= by0 && y <= by1; };
    std::vector<Rgb> samples;
    for (size_t i = 0; i < img.px.size(); ++i) {
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        if (inFrame(x, y) && !nearBlob(x, y))
            samples.push_back(img.px[i]);
    }
    const auto backdrop = Gmm::fit(subsample(samples, 40000), K, kDefaultReg);
    if (!backdrop)
        return std::nullopt;
    std::vector<float> frameCosts(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
        frameCosts[i] = backdrop->negLog(samples[i]);
    std::sort(frameCosts.begin(), frameCosts.end());
    const size_t at = frameCosts.size() * 99 / 100;
    if (at >= frameCosts.size())
        return std::nullopt;
    const float limit = frameCosts[at] + 4.f;
    std::vector<uint8_t> trimap(img.px.size());
    for (size_t i = 0; i < trimap.size(); ++i) {
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        if (inFrame(x, y) && !nearBlob(x, y))
            trimap[i] = BG;
        else if (blob[i] || backdrop->negLog(img.px[i]) > limit)
            trimap[i] = PR_FG;
        else
            trimap[i] = PR_BG;
    }
    auto models = grabcut(img, trimap, 4, stop);
    if (!models)
        return std::nullopt;
    std::vector<uint8_t> cut(trimap.size());
    for (size_t i = 0; i < trimap.size(); ++i)
        cut[i] = trimap[i] == PR_FG || trimap[i] == FG;
    std::vector<uint8_t> low = keepSeeded(cut, blob, w, h);
    if (std::none_of(low.begin(), low.end(), [](uint8_t v) { return v; }))
        return std::nullopt;
    return std::tuple<std::vector<uint8_t>, Gmm, Gmm>{std::move(low), std::move(models->first), std::move(models->second)};
}

// Crece desde (sx, sy) por los píxeles (4-conexos) de color CIELAB cercano a la media de la zona y
// con poco salto desde el vecino por el que se llegó: se para en los bordes.
std::vector<uint8_t> growSimilar(const std::vector<Rgb>& lab, int w, int h, int sx, int sy)
{
    constexpr float kMeanDist = 32.f;
    constexpr float kEdgeStep = 10.f;
    auto d = [](const Rgb& a, const Rgb& b) { return std::sqrt(d2(a, b)); };
    std::vector<uint8_t> out(size_t(w) * h, 0);
    const Rgb seed = lab[size_t(sy) * w + sx];
    double sum[3] = {seed[0], seed[1], seed[2]};
    double n = 1.0;
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    out[size_t(sy) * w + sx] = 1;
    while (!stack.empty()) {
        const auto [x, y] = stack.back();
        stack.pop_back();
        const Rgb c = lab[size_t(y) * w + x];
        const Rgb mean{float(sum[0] / n), float(sum[1] / n), float(sum[2] / n)};
        const int nbs[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
        for (const auto& nb : nbs) {
            const int nx = nb[0], ny = nb[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            const size_t i = size_t(ny) * w + nx;
            if (out[i])
                continue;
            const Rgb& q = lab[i];
            if (d(q, c) <= kEdgeStep && d(q, mean) <= kMeanDist) {
                out[i] = 1;
                sum[0] += q[0];
                sum[1] += q[1];
                sum[2] += q[2];
                n += 1.0;
                stack.push_back({nx, ny});
            }
        }
    }
    return out;
}

// Una pasada del Buscador de objetos sobre 'canvas' con el paso 'step'.
std::optional<Region> objectIn(const Sampler& sampler, Box canvas, int px, int py, int step, const Stop& stop)
{
    const RgbImage img = readRgbScaled(sampler, canvas, step);
    const int w = img.w, h = img.h;
    if (w < 2 || h < 2)
        return std::nullopt;
    const std::vector<Rgb> blurred = box3(box3(img.px, w, h), w, h);
    std::vector<Rgb> lab(blurred.size());
    for (size_t i = 0; i < lab.size(); ++i)
        lab[i] = rgbToLab(blurred[i]);
    const int sx = std::min((px - canvas.x0) / step, w - 1);
    const int sy = std::min((py - canvas.y0) / step, h - 1);
    const std::vector<uint8_t> grown = growSimilar(lab, w, h, sx, sy);
    size_t count = 0, boundary = 0;
    for (size_t i = 0; i < grown.size(); ++i)
        if (grown[i]) {
            ++count;
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            if (x == 0 || x + 1 == w || y == 0 || y + 1 == h)
                ++boundary;
        }
    if (boundary * 2 >= size_t(2 * w + 2 * h - 4) || count < 4)
        return std::nullopt;
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (size_t i = 0; i < grown.size(); ++i)
        if (grown[i]) {
            const int gx = int(i % size_t(w)), gy = int(i / size_t(w));
            x0 = std::min(x0, gx);
            y0 = std::min(y0, gy);
            x1 = std::max(x1, gx + 1);
            y1 = std::max(y1, gy + 1);
        }
    const int pad = std::max(std::max(x1 - x0, y1 - y0) / 6, 2);
    const int bx0 = std::max(x0 - pad, 0), by0 = std::max(y0 - pad, 0), bx1 = std::min(x1 + pad, w), by1 = std::min(y1 + pad, h);
    std::vector<uint8_t> trimap(size_t(w) * h, BG);
    bool anyBg = false;
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) {
            const size_t i = size_t(yy) * w + xx;
            const bool inside = xx >= bx0 && xx < bx1 && yy >= by0 && yy < by1;
            if (grown[i]) {
                trimap[i] = PR_FG;
            } else if (inside) {
                trimap[i] = PR_BG;
            } else {
                anyBg = true;
                trimap[i] = BG;
            }
        }
    if (!anyBg)
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
                if ((xx == 0 || yy == 0 || xx + 1 == w || yy + 1 == h) && !(xx == sx && yy == sy))
                    trimap[size_t(yy) * w + xx] = BG;
    trimap[size_t(sy) * w + sx] = FG;
    const auto models = grabcut(img, trimap, 5, stop);
    if (!models)
        return std::nullopt;
    std::vector<uint8_t> low(trimap.size());
    for (size_t i = 0; i < trimap.size(); ++i)
        low[i] = isFg(trimap[i]);
    std::vector<uint8_t> seeds(trimap.size(), 0);
    seeds[size_t(sy) * w + sx] = 1;
    low = keepSeeded(low, seeds, w, h);
    low = cleanMask(low, w, h, 0.1f, 0.f);
    low = fillHolesByModel(low, img, models->first, models->second, 0.f, 0.05f);
    if (std::none_of(low.begin(), low.end(), [](uint8_t v) { return v; }))
        return std::nullopt;
    return finishRegion(sampler, canvas, step, low, w, h, &models->first, &models->second, stop);
}

} // namespace

// GrabCut iniciado con un rectángulo (fuera, fondo; dentro, objeto probable) sobre una copia de
// unos 'workPx' píxeles; después el borde se vuelve a cortar a resolución completa.
std::optional<Region> objectInRect(const Sampler& sampler, Box rect, Box canvas, int workPx, const Stop& stop)
{
    rect = rect.intersect(canvas);
    if (rect.width() < 2 || rect.height() < 2)
        return std::nullopt;
    const int margin = std::max(int(std::ceil(float(std::max(rect.width(), rect.height())) * 0.15f)), 8);
    const Box window = rect.inflate(margin).intersect(canvas);
    const int step = scaleFor(window, workPx);
    const RgbImage img = readRgbScaled(sampler, window, step);
    const int lw = img.w, lh = img.h;
    std::vector<uint8_t> trimap(size_t(lw) * lh, BG);
    bool anyBg = false;
    for (int y = 0; y < lh; ++y)
        for (int x = 0; x < lw; ++x) {
            const int cx = window.x0 + x * step + step / 2, cy = window.y0 + y * step + step / 2;
            if (rect.contains(cx, cy))
                trimap[size_t(y) * lw + x] = PR_FG;
            else
                anyBg = true;
        }
    if (!anyBg) {
        const int ring = std::max(std::min(lw, lh) / 50, 1);
        for (int y = 0; y < lh; ++y)
            for (int x = 0; x < lw; ++x)
                if (x < ring || y < ring || x + ring >= lw || y + ring >= lh)
                    trimap[size_t(y) * lw + x] = BG;
    }
    const auto models = grabcut(img, trimap, 8, stop);
    if (!models)
        return std::nullopt;
    std::vector<uint8_t> low(trimap.size());
    for (size_t i = 0; i < trimap.size(); ++i)
        low[i] = isFg(trimap[i]);
    low = cleanMask(low, lw, lh, 0.1f, 0.f);
    low = fillHolesByModel(low, img, models->first, models->second, 0.f, 0.05f);
    return finishRegion(sampler, window, step, low, lw, lh, &models->first, &models->second, stop);
}

std::optional<Region> objectAt(const Sampler& sampler, Box canvas, float x, float y, int workPx, const Stop& stop)
{
    if (canvas.width() < 4 || canvas.height() < 4 || !std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    const int px = int(std::floor(x)), py = int(std::floor(y));
    if (!canvas.contains(px, py))
        return std::nullopt;
    workPx = std::max(workPx, 1024);
    int step = scaleFor(canvas, workPx);
    if (auto r = objectIn(sampler, canvas, px, py, step, stop))
        return r;
    // Un objeto de pocos píxeles de trabajo se pierde en lo de alrededor: se busca otra vez con
    // pasos más finos en ventanas del mismo tamaño alrededor del punto.
    const int side = int(std::sqrt(double(workPx)));
    while (step > 1) {
        step = std::max(step / 4, 1);
        const int half = int(std::min<long long>((long long)side * step / 2, INT32_MAX / 4));
        const Box window = Box{px - half, py - half, px + half, py + half}.intersect(canvas);
        if (window.width() < 4 || window.height() < 4)
            continue;
        if (auto r = objectIn(sampler, window, px, py, step, stop))
            return r;
    }
    return std::nullopt;
}

std::optional<Region> selectSubject(const Sampler& sampler, Box canvas, int workPx, const Stop& stop)
{
    if (canvas.width() < 4 || canvas.height() < 4)
        return std::nullopt;
    const int step = scaleFor(canvas, std::clamp(workPx, 1024, 4000000));
    const RgbImage img = readRgbScaled(sampler, canvas, step);
    const int w = img.w, h = img.h;
    const std::vector<float> sal = saliency(img);
    stop.check();
    const float t = std::clamp(otsu(sal), 0.15f, 0.85f);
    std::vector<uint8_t> trimap(size_t(w) * h, PR_BG);
    bool anyFg = false;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * w + x;
            const bool border = x == 0 || y == 0 || x + 1 == w || y + 1 == h;
            if (sal[i] >= t) {
                anyFg = true;
                trimap[i] = PR_FG;
            } else if (border || sal[i] < 0.4f * t) {
                trimap[i] = BG;
            } else {
                trimap[i] = PR_BG;
            }
        }
    if (!anyFg)
        return std::nullopt;
    auto models = grabcut(img, trimap, 6, stop);
    if (!models)
        return std::nullopt;
    std::vector<uint8_t> low(trimap.size());
    for (size_t i = 0; i < trimap.size(); ++i)
        low[i] = trimap[i] == PR_FG || trimap[i] == FG;
    low = cleanMask(low, w, h, 0.2f, 0.f);
    if (std::none_of(low.begin(), low.end(), [](uint8_t v) { return v; }))
        return std::nullopt;
    Gmm fg = models->first, bg = models->second;
    if (auto back = backdropPass(img, low, stop)) {
        low = std::move(std::get<0>(*back));
        fg = std::move(std::get<1>(*back));
        bg = std::move(std::get<2>(*back));
    }
    stop.check();
    low = fillHolesByModel(low, img, fg, bg, 0.002f, 0.05f);
    return finishRegion(sampler, canvas, step, low, w, h, &fg, &bg, stop);
}

} // namespace retouch
