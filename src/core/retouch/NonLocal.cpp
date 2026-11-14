// Retoque de PhotoCraft (Retouch.h): el relleno de Quitar. Algoritmos de PhotoCraft (licencia MIT).
//
// Completado no local por parches (Newson, Almansa, Gousseau y Pérez, IPOL 2017): como el de
// Relleno según el contenido, alterna de lo grueso a lo fino una búsqueda PatchMatch y un voto
// ponderado, pero:
// · la distancia entre parches suma rasgos de textura (la media local de |∂x| y |∂y|), para no
//   confundir hierba con un fondo borroso del mismo color;
// · el nivel más grueso se inicia pelando el hueco capa a capa desde parches a medio conocer;
// · entre niveles se amplía el campo de coincidencias, no la imagen;
// · cada píxel final sale del mejor parche que lo cubre (no del voto), y las costuras se esconden
//   en el dominio del gradiente (una membrana para el borde del hueco y una corrección suave entre
//   zonas copiadas de sitios distintos);
// · el número de niveles sigue al grosor del hueco, no a su tamaño.

#include "core/retouch/Retouch.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace retouch {

namespace {

constexpr size_t kParMin = 128 * 128;
constexpr float kInf = std::numeric_limits<float>::infinity();

struct Pt {
    int x, y;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Pt& o) const { return !(*this == o); }
};

struct Level {
    int w = 0, h = 0;
    Floats img;
    Mask hole;
};

// Centros de parche enteramente conocidos (el parche dentro y fuera del hueco).
std::pair<Mask, std::vector<Pt>> validSources(int w, int h, const Mask& hole, int r)
{
    Mask valid(size_t(w) * h, 0);
    std::vector<Pt> list;
    if (w < 2 * r + 1 || h < 2 * r + 1)
        return {valid, list};
    const size_t w1 = size_t(w) + 1;
    std::vector<uint32_t> sat(w1 * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        uint32_t row = 0;
        for (int x = 0; x < w; ++x) {
            row += hole[size_t(y) * w + x];
            sat[size_t(y + 1) * w1 + x + 1] = sat[size_t(y) * w1 + x + 1] + row;
        }
    }
    for (int y = r; y < h - r; ++y)
        for (int x = r; x < w - r; ++x) {
            const size_t x0 = size_t(x - r), y0 = size_t(y - r), x1 = size_t(x + r + 1), y1 = size_t(y + r + 1);
            if (sat[y1 * w1 + x1] + sat[y0 * w1 + x0] == sat[y0 * w1 + x1] + sat[y1 * w1 + x0]) {
                valid[size_t(y) * w + x] = 1;
                list.push_back({x, y});
            }
        }
    return {valid, list};
}

// Los rasgos de textura de cada celda conocida: la media de |∂x| y de |∂y| de la media de los
// canales en una ventana de 'side', contando solo derivadas entre dos celdas conocidas.
std::pair<Floats, Floats> textureFeatures(int w, int h, int ch, const Floats& img, const Mask& hole, int side)
{
    const size_t n = size_t(w) * h;
    Floats g(n);
    for (size_t i = 0; i < n; ++i) {
        float s = 0.f;
        for (int c = 0; c < ch; ++c)
            s += img[i * ch + c];
        g[i] = s / float(ch);
    }
    const size_t w1 = size_t(w) + 1;
    auto table = [&](int dx, int dy) {
        std::vector<double> sum(w1 * (h + 1), 0.0);
        std::vector<uint32_t> cnt(w1 * (h + 1), 0);
        for (int y = 0; y < h; ++y) {
            double rs = 0.0;
            uint32_t rc = 0;
            for (int x = 0; x < w; ++x) {
                const size_t i = size_t(y) * w + x;
                const int nx = x + dx, ny = y + dy;
                if (nx < w && ny < h && !hole[i] && !hole[size_t(ny) * w + nx]) {
                    rs += double(std::fabs(g[size_t(ny) * w + nx] - g[i]));
                    ++rc;
                }
                sum[size_t(y + 1) * w1 + x + 1] = sum[size_t(y) * w1 + x + 1] + rs;
                cnt[size_t(y + 1) * w1 + x + 1] = cnt[size_t(y) * w1 + x + 1] + rc;
            }
        }
        const int half = side / 2;
        Floats out(n, 0.f);
        for (size_t i = 0; i < n; ++i) {
            if (hole[i])
                continue;
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const size_t x0 = size_t(std::max(x - half, 0)), y0 = size_t(std::max(y - half, 0));
            const size_t x1 = size_t(std::min(x + half + 1, w)), y1 = size_t(std::min(y + half + 1, h));
            const double s = sum[y1 * w1 + x1] + sum[y0 * w1 + x0] - sum[y0 * w1 + x1] - sum[y1 * w1 + x0];
            const uint32_t c = cnt[y1 * w1 + x1] + cnt[y0 * w1 + x0] - cnt[y0 * w1 + x1] - cnt[y1 * w1 + x0];
            out[i] = c == 0 ? 0.f : float(s / double(c));
        }
        return out;
    };
    return {table(1, 0), table(0, 1)};
}

// El nivel siguiente: el color de un gaussiano de 3×3 (σ = 1,5) sobre lo conocido, los rasgos de
// textura por vecino más cercano y una celda es hueco si lo es alguna de las cuatro que cubre.
Level downsample(const Level& l, int cha, int ch)
{
    static const float K[3] = {0.6412f, 0.8007f, 0.6412f};
    Level out;
    out.w = (l.w + 1) / 2;
    out.h = (l.h + 1) / 2;
    out.img.assign(size_t(out.w) * out.h * cha, 0.f);
    out.hole.assign(size_t(out.w) * out.h, 0);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const int fx = x * 2, fy = y * 2;
            const size_t o = (size_t(y) * out.w + x) * cha;
            bool hole = false;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    if (fx + dx < l.w && fy + dy < l.h && l.hole[size_t(fy + dy) * l.w + fx + dx])
                        hole = true;
            out.hole[size_t(y) * out.w + x] = hole;
            float wsum = 0.f;
            for (int ky = 0; ky < 3; ++ky)
                for (int kx = 0; kx < 3; ++kx) {
                    const int sx = fx + kx - 1, sy = fy + ky - 1;
                    if (sx < 0 || sy < 0 || sx >= l.w || sy >= l.h)
                        continue;
                    const size_t si = size_t(sy) * l.w + sx;
                    if (l.hole[si])
                        continue;
                    const float k = K[kx] * K[ky];
                    for (int c = 0; c < ch; ++c)
                        out.img[o + c] += l.img[si * cha + c] * k;
                    wsum += k;
                }
            if (wsum > 0.f)
                for (int c = 0; c < ch; ++c)
                    out.img[o + c] /= wsum;
            const size_t si = (size_t(fy) * l.w + fx) * cha;
            for (int c = ch; c < cha; ++c)
                out.img[o + c] = l.img[si + c];
        }
    return out;
}

// Cuántos niveles: ⌈log2(2·No/N)⌉ (al menos 1), No el grosor del hueco y N el lado del parche,
// mientras el nivel más grueso tenga al menos tres parches de ancho.
int levelCount(int w, int h, const Mask& hole, int r)
{
    const int n = 2 * r + 1;
    const uint32_t no = std::min<uint32_t>(holeRadius(w, h, hole), 1u << 20);
    const int want = int(std::max(std::ceil(std::log2(2.0 * double(no) / double(n))), 1.0));
    int lw = w, lh = h, levels = 1;
    while (levels < want && lw / 2 >= 3 * n && lh / 2 >= 3 * n) {
        lw = (lw + 1) / 2;
        lh = (lh + 1) / 2;
        ++levels;
    }
    return levels;
}

float kthValue(std::vector<float>& cs)
{
    if (cs.empty())
        return 1.f;
    const size_t k = std::min(cs.size() * 3 / 4, cs.size() - 1);
    std::nth_element(cs.begin(), cs.begin() + long(k), cs.end());
    return std::max(cs[k], 1e-6f);
}

struct Solver {
    int w = 0, h = 0, cha = 0, ch = 0, r = 0;
    Floats img;
    const Mask* hole = nullptr;
    Mask valid;
    std::vector<Pt> validList;
    Mask targets;
    Stop stop;

    bool isValid(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && valid[size_t(y) * w + x]; }

    Pt randomSource(MixRng& rng) const
    {
        const uint64_t n = std::max<uint64_t>(validList.size(), 1);
        const size_t k = size_t(rng.next() % n);
        return k < validList.size() ? validList[k] : Pt{0, 0};
    }

    float dist(Pt t, Pt s, float cutoff, const Mask* known) const
    {
        const float full = float((2 * r + 1) * (2 * r + 1)) * float(cha);
        float sum = 0.f;
        size_t n = 0;
        for (int dy = -r; dy <= r; ++dy) {
            const int ty = t.y + dy;
            if (ty < 0 || ty >= h)
                continue;
            for (int dx = -r; dx <= r; ++dx) {
                const int tx = t.x + dx;
                if (tx < 0 || tx >= w)
                    continue;
                const size_t tc = size_t(ty) * w + tx;
                if (known && !(*known)[tc])
                    continue;
                const size_t ti = tc * cha, si = (size_t(s.y + dy) * w + (s.x + dx)) * cha;
                for (int c = 0; c < cha; ++c) {
                    const float d = img[ti + c] - img[si + c];
                    sum += d * d;
                }
                n += size_t(cha);
            }
            if (!known && sum > cutoff * full)
                return kInf;
        }
        return n == 0 ? kInf : sum / float(n);
    }

    // Una pasada de PatchMatch por bandas de filas (la propagación no sale de la banda).
    void patchmatch(std::vector<Pt>& nnf, std::vector<float>& cost, size_t pass, uint64_t seed, int radius) const
    {
        constexpr int kBand = 16;
        const bool reverse = pass % 2 == 1;
        const int step = reverse ? -1 : 1;
        const int bands = (h + kBand - 1) / kBand;
        auto band = [&](int bi) {
            if (stop.requested())
                return;
            MixRng rng(seed ^ (uint64_t(bi) * 0x2545F4914F6CDD1Dull) ^ (uint64_t(pass) << 40));
            const int rows = std::min(kBand, h - bi * kBand);
            const size_t base = size_t(bi) * kBand * w;
            for (int ry = 0; ry < rows; ++ry) {
                const int ly = reverse ? rows - 1 - ry : ry;
                const int y = bi * kBand + ly;
                for (int rx = 0; rx < w; ++rx) {
                    const int x = reverse ? w - 1 - rx : rx;
                    if (!targets[size_t(y) * w + x])
                        continue;
                    const size_t li = base + size_t(ly) * w + x;
                    const Pt t{x, y};
                    Pt best = nnf[li];
                    float bc = cost[li];
                    auto tryCand = [&](Pt cand) {
                        if (cand != best && isValid(cand.x, cand.y)) {
                            const float d = dist(t, cand, bc, nullptr);
                            if (d < bc) {
                                best = cand;
                                bc = d;
                            }
                        }
                    };
                    const int nx = x - step;
                    if (nx >= 0 && nx < w) {
                        const Pt c = nnf[base + size_t(ly) * w + nx];
                        tryCand({c.x + step, c.y});
                    }
                    const int nly = ly - step;
                    if (nly >= 0 && nly < rows) {
                        const Pt c = nnf[base + size_t(nly) * w + x];
                        tryCand({c.x, c.y + step});
                    }
                    for (int rad = radius; rad >= 1; rad /= 2) {
                        const int cx = best.x + rng.range(-rad, rad);
                        const int cy = best.y + rng.range(-rad, rad);
                        tryCand({cx, cy});
                    }
                    nnf[li] = best;
                    cost[li] = bc;
                }
            }
        };
        if (size_t(w) * h >= kParMin)
            parallelRows(0, bands, [&](int b0, int b1) {
                for (int b = b0; b < b1; ++b)
                    band(b);
            }, 1);
        else
            for (int b = 0; b < bands; ++b)
                band(b);
    }

    void refreshCosts(const std::vector<Pt>& nnf, std::vector<float>& cost) const
    {
        auto one = [&](size_t i0, size_t i1) {
            for (size_t i = i0; i < i1; ++i)
                cost[i] = targets[i] ? dist({int(i % size_t(w)), int(i / size_t(w))}, nnf[i], kInf, nullptr) : kInf;
        };
        if (size_t(w) * h >= kParMin)
            parallelRows(0, h, [&](int y0, int y1) { one(size_t(y0) * w, size_t(y1) * w); });
        else
            one(0, cost.size());
    }

    // Para cada celda del hueco, por filas: lo que devuelve pick (las que devuelven algo).
    template <typename T, typename Pick>
    std::vector<std::pair<size_t, T>> perHoleCell(Pick pick) const
    {
        std::vector<std::vector<std::pair<size_t, T>>> rows(static_cast<size_t>(h));
        auto row = [&](int y) {
            if (stop.requested())
                return;
            for (int x = 0; x < w; ++x)
                if ((*hole)[size_t(y) * w + x]) {
                    std::optional<T> v = pick(x, y);
                    if (v)
                        rows[size_t(y)].push_back({size_t(y) * w + x, std::move(*v)});
                }
        };
        if (size_t(w) * h >= kParMin)
            parallelRows(0, h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    row(y);
            });
        else
            for (int y = 0; y < h; ++y)
                row(y);
        std::vector<std::pair<size_t, T>> out;
        for (auto& rr : rows)
            for (auto& v : rr)
                out.push_back(std::move(v));
        return out;
    }

    // Las celdas de los parches que cubren la celda (x, y) del hueco: la fuente de cada una, con el
    // coste de su parche.
    template <typename F>
    void sources(int x, int y, const std::vector<Pt>& nnf, const std::vector<float>& cost, F f) const
    {
        for (int dy = -r; dy <= r; ++dy) {
            const int ty = y + dy;
            if (ty < 0 || ty >= h)
                continue;
            for (int dx = -r; dx <= r; ++dx) {
                const int tx = x + dx;
                if (tx < 0 || tx >= w)
                    continue;
                const size_t ti = size_t(ty) * w + tx;
                if (!targets[ti] || !std::isfinite(cost[ti]))
                    continue;
                const Pt s = nnf[ti];
                const int sx = s.x - dx, sy = s.y - dy;
                if (sx >= 0 && sy >= 0 && sx < w && sy < h)
                    f(size_t(sy) * w + sx, cost[ti]);
            }
        }
    }

    float vote(const std::vector<Pt>& nnf, const std::vector<float>& cost, bool uniform)
    {
        std::vector<float> cs;
        for (size_t i = 0; i < cost.size(); ++i)
            if (targets[i] && std::isfinite(cost[i]))
                cs.push_back(cost[i]);
        const float sigma2 = kthValue(cs);
        const auto fresh = perHoleCell<Floats>([&](int x, int y) -> std::optional<Floats> {
            Floats v(size_t(cha), 0.f);
            float wsum = 0.f;
            sources(x, y, nnf, cost, [&](size_t si, float c) {
                const float wt = uniform ? 1.f : std::max(std::exp(-c / (2.f * sigma2)), 1e-8f);
                for (int k = 0; k < cha; ++k)
                    v[size_t(k)] += img[si * cha + k] * wt;
                wsum += wt;
            });
            if (!(wsum > 0.f))
                return std::nullopt;
            for (float& o : v)
                o /= wsum;
            return v;
        });
        double change = 0.0;
        size_t n = 0;
        for (const auto& [i, v] : fresh) {
            for (int c = 0; c < ch; ++c)
                change += double(std::fabs(v[size_t(c)] - img[i * cha + c]));
            n += size_t(ch);
            std::copy(v.begin(), v.end(), img.begin() + long(i * cha));
        }
        return n == 0 ? 0.f : float(change / double(n));
    }

    struct Copied {
        size_t p, s;
        float c;
    };

    // La reconstrucción final: cada celda del hueco copia la fuente del parche de menor coste.
    std::vector<Copied> copyBest(const std::vector<Pt>& nnf, const std::vector<float>& cost)
    {
        const auto picked = perHoleCell<std::pair<size_t, float>>([&](int x, int y) {
            std::optional<std::pair<size_t, float>> best;
            sources(x, y, nnf, cost, [&](size_t si, float c) {
                if (!best || c < best->second)
                    best = std::pair<size_t, float>{si, c};
            });
            return best;
        });
        for (const auto& [i, b] : picked)
            std::copy(img.begin() + long(b.first * cha), img.begin() + long(b.first * cha + cha), img.begin() + long(i * cha));
        std::vector<Copied> out;
        out.reserve(picked.size());
        for (const auto& [i, b] : picked)
            out.push_back({i, b.first, b.second});
        return out;
    }

    // Esconde la costura de color del borde del hueco (Poisson): la diferencia entre cada celda
    // conocida junto al hueco y la que predice la fuente copiada (la mediana a lo largo del borde)
    // se reparte por el hueco como una membrana armónica.
    void blendSeams(const std::vector<Copied>& copied, float textureScale)
    {
        constexpr int kEdgeMedian = 8;
        constexpr float kMaxShift = 0.05f;
        if (copied.empty())
            return;
        const float unscale = textureScale > 0.f ? 0.5f / textureScale : 0.f;
        int x0 = w, y0 = h, x1 = 0, y1 = 0;
        for (const Copied& c : copied) {
            const int px = int(c.p % size_t(w)), py = int(c.p / size_t(w));
            x0 = std::min(x0, px);
            y0 = std::min(y0, py);
            x1 = std::max(x1, px + 1);
            y1 = std::max(y1, py + 1);
        }
        x0 = std::max(x0 - 1, 0);
        y0 = std::max(y0 - 1, 0);
        x1 = std::min(x1 + 1, w);
        y1 = std::min(y1 + 1, h);
        const int bw = std::max(x1 - x0, 0), bh = std::max(y1 - y0, 0);
        auto local = [&](size_t i) { return size_t(int(i / size_t(w)) - y0) * bw + size_t(int(i % size_t(w)) - x0); };
        const Mask& H = *hole;
        auto channel = [&](int c) {
            Floats v(size_t(bw) * bh, 0.f);
            std::vector<uint8_t> n(size_t(bw) * bh, 0);
            for (const Copied& cp : copied) {
                const int px = int(cp.p % size_t(w)), py = int(cp.p / size_t(w));
                const int sx = int(cp.s % size_t(w)), sy = int(cp.s / size_t(w));
                static const int d4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : d4) {
                    const int qx = px + d[0], qy = py + d[1], tx = sx + d[0], ty = sy + d[1];
                    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };
                    if (!inside(qx, qy) || !inside(tx, ty))
                        continue;
                    const size_t q = size_t(qy) * w + qx, t = size_t(ty) * w + tx;
                    if (H[q])
                        continue;
                    const size_t lq = local(q);
                    v[lq] += img[q * cha + c] - img[t * cha + c];
                    if (n[lq] < 255)
                        ++n[lq];
                }
            }
            for (size_t i = 0; i < v.size(); ++i)
                if (n[i] > 0)
                    v[i] /= float(n[i]);
            const Floats raw = v;
            std::vector<float> near;
            for (size_t lq = 0; lq < v.size(); ++lq) {
                if (n[lq] == 0)
                    continue;
                const int qx = int(lq % size_t(bw)), qy = int(lq / size_t(bw));
                near.clear();
                for (int y = std::max(qy - kEdgeMedian, 0); y < std::min(qy + kEdgeMedian + 1, bh); ++y)
                    for (int x = std::max(qx - kEdgeMedian, 0); x < std::min(qx + kEdgeMedian + 1, bw); ++x)
                        if (n[size_t(y) * bw + x] > 0)
                            near.push_back(raw[size_t(y) * bw + x]);
                const size_t mid = near.size() / 2;
                if (!near.empty()) {
                    std::nth_element(near.begin(), near.begin() + long(mid), near.end());
                    const float m = near[mid];
                    const size_t q = size_t(y0 + qy) * w + x0 + qx;
                    const float texture = (img[q * cha + ch] + img[q * cha + ch + 1]) * unscale;
                    const float cap = kMaxShift + 2.f * texture;
                    v[lq] = std::clamp(m, -cap, cap);
                }
            }
            std::deque<size_t> queue;
            Mask seen(v.size());
            for (size_t lq = 0; lq < v.size(); ++lq) {
                seen[lq] = n[lq] > 0;
                if (n[lq] > 0)
                    queue.push_back(lq);
            }
            auto inHole = [&](size_t lq) { return H[size_t(y0 + int(lq / size_t(bw))) * w + x0 + int(lq % size_t(bw))]; };
            while (!queue.empty()) {
                const size_t lq = queue.front();
                queue.pop_front();
                const int x = int(lq % size_t(bw)), y = int(lq / size_t(bw));
                const long long around[4] = {x > 0 ? (long long)lq - 1 : -1, x + 1 < bw ? (long long)lq + 1 : -1,
                                             y > 0 ? (long long)lq - bw : -1, y + 1 < bh ? (long long)lq + bw : -1};
                for (long long nb : around) {
                    if (nb < 0)
                        continue;
                    if (!seen[size_t(nb)] && !inHole(static_cast<size_t>(nb))) {
                        seen[size_t(nb)] = 1;
                        v[size_t(nb)] = v[lq];
                        queue.push_back(static_cast<size_t>(nb));
                    }
                }
            }
            Mask unknown(v.size());
            for (size_t i = 0; i < v.size(); ++i)
                unknown[i] = inHole(i);
            solveMembrane(bw, bh, unknown, v);
            return v;
        };
        std::vector<Floats> shifts(static_cast<size_t>(ch));
        parallelRows(0, ch, [&](int c0, int c1) {
            for (int c = c0; c < c1; ++c)
                shifts[size_t(c)] = channel(c);
        }, 1);
        for (const Copied& cp : copied) {
            const size_t lp = local(cp.p);
            for (int c = 0; c < ch; ++c)
                img[cp.p * cha + c] += shifts[size_t(c)][lp];
        }
    }

    // Quita las costuras visibles entre zonas copiadas de sitios distintos (fusión en el gradiente,
    // como Image Melding): donde el salto real se aparta del que dan las fuentes más de
    // kSeamContrast veces la textura local, se resuelve una corrección suave (SOR rojo-negro).
    void fuseSeams(const std::vector<Copied>& copied, float textureScale, int iters)
    {
        constexpr float kSeamContrast = 3.f;
        constexpr float kOmega = 1.8f;
        constexpr size_t kNone = SIZE_MAX;
        std::vector<std::pair<size_t, float>> src(size_t(w) * h, {kNone, kInf});
        for (const Copied& c : copied)
            src[c.p] = {c.s, c.c};
        auto cell = [&](int x, int y) -> std::optional<size_t> {
            if (x >= 0 && y >= 0 && x < w && y < h)
                return size_t(y) * w + x;
            return std::nullopt;
        };
        const float unscale = textureScale > 0.f ? 0.5f / textureScale : 0.f;
        Floats div(size_t(w) * h * ch, 0.f);
        bool any = false;
        std::vector<float> e(static_cast<size_t>(ch));
        for (const Copied& cp : copied) {
            const size_t p = cp.p, sp = cp.s;
            const float cpc = cp.c;
            const int px = int(p % size_t(w)), py = int(p / size_t(w));
            const float texture = (img[p * cha + ch] + img[p * cha + ch + 1]) * unscale;
            static const int d2[2][2] = {{1, 0}, {0, 1}};
            for (const auto& d : d2) {
                const auto q = cell(px + d[0], py + d[1]);
                if (!q)
                    continue;
                const auto [sq, cq] = src[*q];
                if (sq == kNone)
                    continue;
                const auto fromP = cell(int(sp % size_t(w)) + d[0], int(sp / size_t(w)) + d[1]);
                if (fromP && *fromP == sq)
                    continue;
                const auto fromQ = cell(int(sq % size_t(w)) - d[0], int(sq / size_t(w)) - d[1]);
                size_t a, b;
                if (fromP && fromQ) {
                    if (cpc <= cq) {
                        a = sp;
                        b = *fromP;
                    } else {
                        a = *fromQ;
                        b = sq;
                    }
                } else if (fromP) {
                    a = sp;
                    b = *fromP;
                } else if (fromQ) {
                    a = *fromQ;
                    b = sq;
                } else {
                    continue;
                }
                float sum = 0.f;
                for (int c = 0; c < ch; ++c) {
                    e[size_t(c)] = (img[b * cha + c] - img[a * cha + c]) - (img[*q * cha + c] - img[p * cha + c]);
                    sum += std::fabs(e[size_t(c)]);
                }
                if (sum / float(ch) <= kSeamContrast * texture)
                    continue;
                for (int c = 0; c < ch; ++c) {
                    div[p * ch + c] += e[size_t(c)];
                    div[*q * ch + c] -= e[size_t(c)];
                }
                any = true;
            }
        }
        if (!any)
            return;
        constexpr int kFuseBand = 16;
        Mask seams(size_t(w) * h, 0);
        for (size_t i = 0; i < seams.size(); ++i)
            for (int c = 0; c < ch; ++c)
                if (div[i * ch + c] != 0.f) {
                    seams[i] = 1;
                    break;
                }
        const Mask band = dilate(seams, w, h, kFuseBand);
        std::vector<size_t> halves[2];
        for (int colour = 0; colour < 2; ++colour)
            for (const Copied& c : copied)
                if (band[c.p] && int((c.p % size_t(w)) + (c.p / size_t(w))) % 2 == colour)
                    halves[colour].push_back(c.p);
        Floats delta(size_t(w) * h * ch, 0.f);
        Floats next;
        for (int it = 0; it < iters; ++it) {
            if (stop.requested())
                return;
            for (const auto& cells : halves) {
                next.assign(cells.size() * ch, 0.f);
                parallelRows(0, int(cells.size()), [&](int k0, int k1) {
                    for (int k = k0; k < k1; ++k) {
                        const size_t p = cells[size_t(k)];
                        const int x = int(p % size_t(w)), y = int(p / size_t(w));
                        float deg = 0.f;
                        float* out = next.data() + size_t(k) * ch;
                        static const int d4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                        for (const auto& d : d4) {
                            const auto q = cell(x + d[0], y + d[1]);
                            if (!q)
                                continue;
                            deg += 1.f;
                            for (int c = 0; c < ch; ++c)
                                out[c] += delta[*q * ch + c];
                        }
                        for (int c = 0; c < ch; ++c) {
                            const float cur = delta[p * ch + c];
                            out[c] = cur + kOmega * ((out[c] - div[p * ch + c]) / std::max(deg, 1.f) - cur);
                        }
                    }
                }, 256);
                for (size_t k = 0; k < cells.size(); ++k)
                    std::copy(next.begin() + long(k * ch), next.begin() + long(k * ch + ch), delta.begin() + long(cells[k] * ch));
            }
        }
        for (const Copied& c : copied)
            for (int k = 0; k < ch; ++k)
                img[c.p * cha + k] += delta[c.p * ch + k];
    }

    // Inicio pelando la cebolla: el hueco se rellena capa a capa desde el borde; cada centro
    // conocido junto a la capa busca su parche por sus celdas conocidas y cada celda de la capa es
    // el voto ponderado de esos parches.
    void onionPeel(std::vector<Pt>& nnf, Mask& has, uint64_t seed)
    {
        Mask known(hole->size());
        for (size_t i = 0; i < known.size(); ++i)
            known[i] = !(*hole)[i];
        std::vector<size_t> unknown;
        for (size_t i = 0; i < known.size(); ++i)
            if ((*hole)[i])
                unknown.push_back(i);
        Mask centre(size_t(w) * h, 0);
        std::vector<float> layerCost(size_t(w) * h, kInf);
        uint64_t layerNo = 0;
        for (;;) {
            stop.check();
            auto neighbourKnown = [&](size_t i) {
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                return (x > 0 && known[i - 1]) || (x + 1 < w && known[i + 1]) || (y > 0 && known[i - w])
                    || (y + 1 < h && known[i + w]);
            };
            std::vector<size_t> layer;
            for (size_t i : unknown)
                if (neighbourKnown(i))
                    layer.push_back(i);
            if (layer.empty())
                return;
            std::vector<size_t> centres;
            for (size_t i : layer) {
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                for (int cy = std::max(y - r, 0); cy <= std::min(y + r, h - 1); ++cy)
                    for (int cx = std::max(x - r, 0); cx <= std::min(x + r, w - 1); ++cx) {
                        const size_t ci = size_t(cy) * w + cx;
                        if (known[ci] && !centre[ci]) {
                            centre[ci] = 1;
                            centres.push_back(ci);
                        }
                    }
            }
            for (size_t ci : centres)
                centre[ci] = 0;
            struct Found {
                size_t ci;
                Pt s;
                float c;
            };
            std::vector<Found> found(centres.size());
            auto search = [&](size_t k) {
                const size_t ci = centres[k];
                const Pt t{int(ci % size_t(w)), int(ci / size_t(w))};
                MixRng rng(seed ^ (uint64_t(ci) * 0x9E3779B9ull) ^ (layerNo << 48));
                Pt best{0, 0};
                float bc = kInf;
                auto tryCand = [&](Pt cand) {
                    if (isValid(cand.x, cand.y)) {
                        const float d = dist(t, cand, bc, &known);
                        if (d < bc) {
                            best = cand;
                            bc = d;
                        }
                    }
                };
                if (has[ci])
                    tryCand(nnf[ci]);
                static const int d4[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : d4) {
                    const int nx = t.x + d[0], ny = t.y + d[1];
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h && has[size_t(ny) * w + nx]) {
                        const Pt s = nnf[size_t(ny) * w + nx];
                        tryCand({s.x - d[0], s.y - d[1]});
                    }
                }
                for (int i = 0; i < 8; ++i)
                    tryCand(randomSource(rng));
                for (int i = 0; i < 2; ++i)
                    for (int rad = std::max(w, h); rad >= 1; rad /= 2) {
                        const int cx = best.x + rng.range(-rad, rad);
                        const int cy = best.y + rng.range(-rad, rad);
                        tryCand({cx, cy});
                    }
                found[k] = {ci, best, bc};
            };
            parallelRows(0, int(centres.size()), [&](int k0, int k1) {
                for (int k = k0; k < k1; ++k)
                    search(static_cast<size_t>(k));
            }, 16);
            std::vector<float> cs;
            for (const Found& f : found)
                if (std::isfinite(f.c)) {
                    nnf[f.ci] = f.s;
                    has[f.ci] = 1;
                    layerCost[f.ci] = f.c;
                    cs.push_back(f.c);
                }
            const float sigma2 = kthValue(cs);
            std::vector<std::pair<size_t, Floats>> values;
            for (size_t i : layer) {
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                Floats v(size_t(cha), 0.f);
                float wsum = 0.f;
                for (int cy = std::max(y - r, 0); cy <= std::min(y + r, h - 1); ++cy)
                    for (int cx = std::max(x - r, 0); cx <= std::min(x + r, w - 1); ++cx) {
                        const size_t ci = size_t(cy) * w + cx;
                        if (!std::isfinite(layerCost[ci]))
                            continue;
                        const Pt s = nnf[ci];
                        const int sx = s.x + x - cx, sy = s.y + y - cy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                            continue;
                        const size_t si = size_t(sy) * w + sx;
                        const float wt = std::max(std::exp(-layerCost[ci] / (2.f * sigma2)), 1e-8f);
                        for (int c = 0; c < cha; ++c)
                            v[size_t(c)] += img[si * cha + c] * wt;
                        wsum += wt;
                    }
                if (wsum > 0.f) {
                    for (float& o : v)
                        o /= wsum;
                    values.push_back({i, std::move(v)});
                }
            }
            for (const Found& f : found)
                layerCost[f.ci] = kInf;
            if (values.empty())
                return;
            for (const auto& [i, v] : values) {
                std::copy(v.begin(), v.end(), img.begin() + long(i * cha));
                known[i] = 1;
            }
            unknown.erase(std::remove_if(unknown.begin(), unknown.end(), [&](size_t i) { return known[i] != 0; }),
                          unknown.end());
            ++layerNo;
        }
    }
};

} // namespace

std::optional<Floats> completeNonLocal(int w, int h, int ch, const Floats& img, const Mask& hole, const NonLocalParams& p,
                                       const Stop& stop)
{
    const size_t n = size_t(w) * h;
    if (ch == 0 || hole.size() != n || img.size() != n * ch
        || std::none_of(hole.begin(), hole.end(), [](uint8_t v) { return v; }))
        return img;
    const int r = std::max(p.patchRadius, 1);
    const int levels = levelCount(w, h, hole, r);
    stop.check();
    const auto [tx, ty] = textureFeatures(w, h, ch, img, hole, 1 << std::min(levels, 8));
    stop.check();
    const int cha = ch + 2;
    const float k = std::sqrt(std::max(p.lambda, 0.f));
    Floats data;
    data.reserve(n * cha);
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < ch; ++c)
            data.push_back(img[i * ch + c]);
        data.push_back(tx[i] * k);
        data.push_back(ty[i] * k);
    }
    stop.check();
    std::vector<Level> pyramid;
    pyramid.push_back({w, h, std::move(data), hole});
    while (int(pyramid.size()) < levels) {
        stop.check();
        Level next = downsample(pyramid.back(), cha, ch);
        if (validSources(next.w, next.h, next.hole, r).second.empty())
            break;
        pyramid.push_back(std::move(next));
    }
    std::optional<std::pair<int, std::vector<Pt>>> prev;
    std::optional<Floats> result;
    for (int li = int(pyramid.size()) - 1; li >= 0; --li) {
        stop.check();
        const Level& level = pyramid[size_t(li)];
        const int lw = level.w, lh = level.h;
        auto [valid, validList] = validSources(lw, lh, level.hole, r);
        if (validList.empty())
            return std::nullopt;
        Mask targets(size_t(lw) * lh, 0);
        for (size_t i = 0; i < level.hole.size(); ++i)
            if (level.hole[i]) {
                const int x = int(i % size_t(lw)), y = int(i / size_t(lw));
                for (int yy = std::max(y - r, 0); yy <= std::min(y + r, lh - 1); ++yy)
                    for (int xx = std::max(x - r, 0); xx <= std::min(x + r, lw - 1); ++xx)
                        targets[size_t(yy) * lw + xx] = 1;
            }
        Solver s;
        s.w = lw;
        s.h = lh;
        s.cha = cha;
        s.ch = ch;
        s.r = r;
        s.img = level.img;
        s.hole = &level.hole;
        s.valid = std::move(valid);
        s.validList = std::move(validList);
        s.targets = std::move(targets);
        s.stop = stop;
        std::vector<Pt> nnf(size_t(lw) * lh, Pt{0, 0});
        std::vector<float> cost(size_t(lw) * lh, kInf);
        MixRng rng(p.seed ^ (uint64_t(li) << 20));
        if (!prev) {
            Mask has(size_t(lw) * lh, 0);
            s.onionPeel(nnf, has, p.seed ^ 0x0c104e00ull);
            for (size_t i = 0; i < nnf.size(); ++i)
                if (s.targets[i] && !has[i])
                    nnf[i] = s.randomSource(rng);
        } else {
            const int pw = prev->first;
            const std::vector<Pt>& pnnf = prev->second;
            for (int y = 0; y < lh; ++y)
                for (int x = 0; x < lw; ++x) {
                    const size_t i = size_t(y) * lw + x;
                    if (!s.targets[i])
                        continue;
                    const size_t pi = size_t(y / 2) * pw + x / 2;
                    const Pt c = pi < pnnf.size() ? pnnf[pi] : Pt{-1, -1};
                    const Pt cand{c.x * 2 + (x % 2), c.y * 2 + (y % 2)};
                    if (s.isValid(cand.x, cand.y))
                        nnf[i] = cand;
                    else if (s.isValid(c.x * 2, c.y * 2))
                        nnf[i] = {c.x * 2, c.y * 2};
                    else
                        nnf[i] = s.randomSource(rng);
                }
            for (size_t i = 0; i < cost.size(); ++i)
                if (s.targets[i])
                    cost[i] = 0.f;
            s.vote(nnf, cost, true);
        }
        const bool coarsest = li + 1 == int(pyramid.size());
        const int full = std::max(lw, lh);
        for (int it = 0; it < std::max(p.maxIters, 1); ++it) {
            stop.check();
            s.refreshCosts(nnf, cost);
            const int radius = coarsest || it == 0 ? full : std::min(8 * (2 * r + 1), full);
            for (int pass = 0; pass < std::max(p.pmPasses, 1); ++pass) {
                stop.check();
                s.patchmatch(nnf, cost, size_t(pass + it * p.pmPasses), p.seed + uint64_t(li * 1000 + it), radius);
            }
            stop.check();
            const float change = s.vote(nnf, cost, false);
            stop.check();
            if (change < p.tolerance)
                break;
        }
        if (li == 0) {
            s.refreshCosts(nnf, cost);
            const auto copied = s.copyBest(nnf, cost);
            stop.check();
            s.blendSeams(copied, k);
            stop.check();
            s.fuseSeams(copied, k, 100);
            stop.check();
            result = std::move(s.img);
        }
        prev = std::pair<int, std::vector<Pt>>{lw, std::move(nnf)};
    }
    if (!result)
        return std::nullopt;
    Floats out = img;
    for (size_t i = 0; i < n; ++i)
        if (hole[i])
            for (int c = 0; c < ch; ++c)
                out[i * ch + c] = (*result)[i * cha + c];
    return out;
}

namespace {

// Huecos que cubren menos de esta parte de su rectángulo (un trazo largo, marcas sueltas) se
// rellenan por mosaicos, así el trabajo sigue al hueco y no a su rectángulo.
constexpr double kSparse = 0.25;
constexpr int kMinTile = 256;

Floats fillOne(int w, int h, int ch, const Floats& img, const Mask& hole, uint64_t seed, const Stop& stop)
{
    NonLocalParams p;
    p.seed = seed;
    if (auto out = completeNonLocal(w, h, ch, img, hole, p, stop))
        return std::move(*out);
    return membraneFill(w, h, ch, img, hole, stop);
}

} // namespace

Floats removeFill(int w, int h, int ch, const Floats& img, const Mask& hole, uint64_t seed, const Stop& stop)
{
    const size_t n = size_t(w) * h;
    if (ch == 0 || hole.size() != n || img.size() != n * ch)
        return img;
    int bx0 = w, by0 = h, bx1 = 0, by1 = 0;
    size_t count = 0;
    for (size_t i = 0; i < n; ++i)
        if (hole[i]) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            bx0 = std::min(bx0, x);
            by0 = std::min(by0, y);
            bx1 = std::max(bx1, x + 1);
            by1 = std::max(by1, y + 1);
            ++count;
        }
    if (count == 0)
        return img;
    const int thickness = int(std::min<uint32_t>(holeRadius(w, h, hole), 1u << 20));
    stop.check();
    const int tile = std::max(8 * thickness, kMinTile);
    const double area = double(bx1 - bx0) * double(by1 - by0);
    if (double(count) >= kSparse * area || std::max(bx1 - bx0, by1 - by0) <= 2 * tile)
        return fillOne(w, h, ch, img, hole, seed, stop);
    const int margin = std::max(6 * thickness, 48);
    Floats out = img;
    Mask left = hole;
    uint64_t k = 0;
    for (int ty0 = by0; ty0 < by1; ty0 += tile)
        for (int tx0 = bx0; tx0 < bx1; tx0 += tile, ++k) {
            stop.check();
            const int tx1 = std::min(tx0 + tile, bx1), ty1 = std::min(ty0 + tile, by1);
            bool any = false;
            for (int y = ty0; y < ty1 && !any; ++y)
                for (int x = tx0; x < tx1; ++x)
                    if (left[size_t(y) * w + x]) {
                        any = true;
                        break;
                    }
            if (!any)
                continue;
            const int x0 = std::max(tx0 - margin, 0), y0 = std::max(ty0 - margin, 0);
            const int x1 = std::min(tx1 + margin, w), y1 = std::min(ty1 + margin, h);
            const int ww = x1 - x0, hh = y1 - y0;
            Floats subImg(size_t(ww) * hh * ch);
            Mask subHole(size_t(ww) * hh);
            for (int y = y0; y < y1; ++y) {
                std::copy(out.begin() + long((size_t(y) * w + x0) * ch), out.begin() + long((size_t(y) * w + x1) * ch),
                          subImg.begin() + long(size_t(y - y0) * ww * ch));
                std::copy(left.begin() + long(size_t(y) * w + x0), left.begin() + long(size_t(y) * w + x1),
                          subHole.begin() + long(size_t(y - y0) * ww));
            }
            const Floats filled = fillOne(ww, hh, ch, subImg, subHole, seed + k, stop);
            for (int y = ty0; y < ty1; ++y)
                for (int x = tx0; x < tx1; ++x) {
                    const size_t i = size_t(y) * w + x;
                    if (left[i]) {
                        const size_t j = size_t(y - y0) * ww + (x - x0);
                        std::copy(filled.begin() + long(j * ch), filled.begin() + long(j * ch + ch), out.begin() + long(i * ch));
                        left[i] = 0;
                    }
                }
        }
    return out;
}

} // namespace retouch
