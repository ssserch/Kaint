// Retoque de PhotoCraft (Retouch.h): el trazado del Lazo magnético. Algoritmos de PhotoCraft
// (licencia MIT).
//
// Entre dos puntos de anclaje el borde es el camino de píxeles (8-conexo) más barato, donde los
// píxeles sobre un borde fuerte y bien localizado son baratos: el «cable vivo» de Mortensen y
// Barrett (Intelligent Scissors, SIGGRAPH 1995). Un enlace cuesta una suma ponderada de la fuerza
// del borde, de estar en su centro (supresión de no máximos) y de la dirección del gradiente. La
// búsqueda se queda en un pasillo (los píxeles a la anchura de detección del recorrido del
// puntero) y un pequeño tirón hacia ese recorrido mantiene el borde en el puntero donde no hay
// bordes. Los puntos sobre un borde van a su centro con precisión de subpíxel.

#include "core/retouch/Retouch.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <queue>

namespace retouch {

namespace {

constexpr int kBlock = 128;
constexpr size_t kMaxBlocks = 1024;
constexpr double kPiece = 256.0;
constexpr double kMaxGuideLength = 262144.0;
constexpr float kWCentre = 0.43f;
constexpr float kWStrength = 0.43f;
constexpr float kWDirection = 0.14f;
constexpr float kWLength = 0.02f;
constexpr float kWGuide = 0.4f;
constexpr float kNeutralDirection = 2.f / 3.f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrt2 = 1.41421356237309504880f;
constexpr int kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

double dist(PointF a, PointF b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

double dist2Segment(PointF p, PointF a, PointF b)
{
    const double vx = b.x - a.x, vy = b.y - a.y;
    const double l2 = vx * vx + vy * vy;
    const double t = l2 > 0.0 ? std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / l2, 0.0, 1.0) : 0.0;
    const double dx = a.x + vx * t - p.x, dy = a.y + vy * t - p.y;
    return dx * dx + dy * dy;
}

// El trazo cortado en tramos de 'piece' de largo como mucho; cada uno empieza donde acabó el otro.
std::vector<std::vector<PointF>> split(const std::vector<PointF>& g, double piece)
{
    if (g.empty())
        return {};
    std::vector<std::vector<PointF>> out;
    std::vector<PointF> cur{g.front()};
    double acc = 0.0;
    for (size_t i = 0; i + 1 < g.size(); ++i) {
        PointF a = g[i];
        const PointF b = g[i + 1];
        double seg = dist(a, b);
        while (acc + seg > piece && seg > 0.0) {
            const double t = std::clamp((piece - acc) / seg, 0.0, 1.0);
            const PointF m{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
            cur.push_back(m);
            out.push_back(std::move(cur));
            cur = {m};
            a = m;
            seg = dist(a, b);
            acc = 0.0;
        }
        cur.push_back(b);
        acc += seg;
    }
    out.push_back(std::move(cur));
    return out;
}

float directionCost(const float* tp, const float* tq, int dx, int dy, float len)
{
    if ((tp[0] == 0.f && tp[1] == 0.f) || (tq[0] == 0.f && tq[1] == 0.f))
        return kNeutralDirection;
    float l[2] = {float(dx) / len, float(dy) / len};
    if (tp[0] * l[0] + tp[1] * l[1] < 0.f) {
        l[0] = -l[0];
        l[1] = -l[1];
    }
    const float dp = std::clamp(tp[0] * l[0] + tp[1] * l[1], -1.f, 1.f);
    const float dq = std::clamp(l[0] * tq[0] + l[1] * tq[1], -1.f, 1.f);
    return 2.f / (3.f * kPi) * (std::acos(dp) + std::acos(dq));
}

} // namespace

MagneticSettings MagneticSettings::make(double width, float contrast)
{
    MagneticSettings s;
    s.width = std::isfinite(width) ? std::clamp(width, 1.0, 256.0) : 10.0;
    s.contrast = std::isfinite(contrast) ? std::clamp(contrast, 0.01f, 1.f) : 0.1f;
    return s;
}

double polylineLength(const std::vector<PointF>& p)
{
    double s = 0.0;
    for (size_t i = 0; i + 1 < p.size(); ++i)
        s += dist(p[i], p[i + 1]);
    return s;
}

std::vector<PointF> simplifyPolyline(const std::vector<PointF>& p, double tol)
{
    const size_t n = p.size();
    if (n < 3)
        return p;
    std::vector<uint8_t> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    const double tol2 = tol * tol;
    std::vector<std::pair<size_t, size_t>> stack{{0, n - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1)
            continue;
        double far = 0.0;
        size_t farK = a;
        for (size_t k = a + 1; k < b; ++k) {
            const double d2 = dist2Segment(p[k], p[a], p[b]);
            if (d2 > far) {
                far = d2;
                farK = k;
            }
        }
        if (far > tol2) {
            keep[farK] = 1;
            stack.push_back({a, farK});
            stack.push_back({farK, b});
        }
    }
    std::vector<PointF> out;
    for (size_t i = 0; i < n; ++i)
        if (keep[i])
            out.push_back(p[i]);
    return out;
}

// Rasgos de borde de los píxeles de una zona.
struct MagneticTracer::Field {
    Box area;
    int w = 0;
    std::vector<float> strength;
    std::vector<float> grad; // 2 por píxel
    std::vector<uint8_t> ridge;
    std::vector<float> offset; // 2 por píxel

    int h() const { return w > 0 ? int(strength.size() / size_t(w)) : 0; }
    std::optional<size_t> index(int x, int y) const
    {
        if (!area.contains(x, y))
            return std::nullopt;
        return size_t(y - area.y0) * w + (x - area.x0);
    }
    PointF point(size_t i, int x, int y, float contrast) const
    {
        const bool onEdge = ridge[i] && strength[i] >= contrast;
        const float ox = onEdge ? offset[i * 2] : 0.f, oy = onEdge ? offset[i * 2 + 1] : 0.f;
        return {double(x) + 0.5 + double(ox), double(y) + 0.5 + double(oy)};
    }
    PointF pointAt(size_t i, float contrast) const
    {
        const int ww = std::max(w, 1);
        return point(i, int(i % size_t(ww)) + area.x0, int(i / size_t(ww)) + area.y0, contrast);
    }

    // Rasgos sobre 'area' desde los píxeles RGBA de 8 bits 'px' de 'ext' (la zona y hasta dos
    // píxeles alrededor, dentro de la imagen; fuera, se repiten los del borde).
    static Field make(Box area, Box ext, const std::vector<uint32_t>& px)
    {
        const int ew = ext.width(), eh = ext.height();
        std::vector<std::array<float, 4>> chn(px.size());
        for (size_t i = 0; i < px.size(); ++i) {
            const uint32_t p = px[i];
            const float a = float((p >> 24) & 0xff) / 255.f;
            chn[i] = {float(p & 0xff) / 255.f * a, float((p >> 8) & 0xff) / 255.f * a, float((p >> 16) & 0xff) / 255.f * a, a};
        }
        auto at = [&](int x, int y) -> std::array<float, 4> {
            x = std::clamp(x - ext.x0, 0, std::max(ew - 1, 0));
            y = std::clamp(y - ext.y0, 0, std::max(eh - 1, 0));
            const size_t i = size_t(y) * std::max(ew, 0) + x;
            return i < chn.size() ? chn[i] : std::array<float, 4>{0, 0, 0, 0};
        };
        const Box g1 = area.inflate(1).intersect(ext);
        const int gw = g1.width();
        std::vector<float> st(size_t(gw) * g1.height(), 0.f);
        std::vector<float> gd(st.size() * 2, 0.f);
        for (int y = g1.y0; y < g1.y1; ++y)
            for (int x = g1.x0; x < g1.x1; ++x) {
                const auto nw = at(x - 1, y - 1), n = at(x, y - 1), ne = at(x + 1, y - 1);
                const auto wv = at(x - 1, y), e = at(x + 1, y);
                const auto sw = at(x - 1, y + 1), s = at(x, y + 1), se = at(x + 1, y + 1);
                float bestM = 0.f, bx = 0.f, by = 0.f;
                for (int c = 0; c < 4; ++c) {
                    const float gx = (ne[c] + 2.f * e[c] + se[c]) - (nw[c] + 2.f * wv[c] + sw[c]);
                    const float gy = (sw[c] + 2.f * s[c] + se[c]) - (nw[c] + 2.f * n[c] + ne[c]);
                    const float m2 = gx * gx + gy * gy;
                    if (m2 > bestM) {
                        bestM = m2;
                        bx = gx;
                        by = gy;
                    }
                }
                const float mag = std::sqrt(bestM);
                const size_t i = size_t(y - g1.y0) * gw + (x - g1.x0);
                st[i] = std::min(mag / 4.f, 1.f);
                if (mag > 1e-6f) {
                    gd[i * 2] = bx / mag;
                    gd[i * 2 + 1] = by / mag;
                }
            }
        auto sAt = [&](int x, int y) -> float {
            if (!g1.contains(x, y))
                return 0.f;
            return st[size_t(y - g1.y0) * gw + (x - g1.x0)];
        };
        Field f;
        f.area = area;
        f.w = area.width();
        const size_t n = size_t(f.w) * area.height();
        f.strength.assign(n, 0.f);
        f.grad.assign(n * 2, 0.f);
        f.ridge.assign(n, 0);
        f.offset.assign(n * 2, 0.f);
        for (int y = area.y0; y < area.y1; ++y)
            for (int x = area.x0; x < area.x1; ++x) {
                const float s0 = sAt(x, y);
                float g[2] = {0.f, 0.f};
                if (g1.contains(x, y)) {
                    const size_t gi = size_t(y - g1.y0) * gw + (x - g1.x0);
                    g[0] = gd[gi * 2];
                    g[1] = gd[gi * 2 + 1];
                }
                const size_t i = size_t(y - area.y0) * f.w + (x - area.x0);
                f.strength[i] = s0;
                f.grad[i * 2] = g[0];
                f.grad[i * 2 + 1] = g[1];
                if (g[0] == 0.f && g[1] == 0.f)
                    continue;
                auto q = [](float v, float o) { return std::fabs(v) >= 0.4142f * std::fabs(o) ? (v > 0 ? 1 : v < 0 ? -1 : 0) : 0; };
                const int dx = q(g[0], g[1]), dy = q(g[1], g[0]);
                const float sm = sAt(x - dx, y - dy), sp = sAt(x + dx, y + dy);
                if (s0 > 0.f && s0 >= sm && s0 >= sp) {
                    f.ridge[i] = 1;
                    const float denom = sm - 2.f * s0 + sp;
                    const float t = denom < -1e-6f ? std::clamp(0.5f * (sm - sp) / denom, -0.5f, 0.5f) : 0.f;
                    f.offset[i * 2] = t * float(dx);
                    f.offset[i * 2 + 1] = t * float(dy);
                }
            }
        return f;
    }
};

std::optional<PointF> MagneticTracer::clampPoint(PointF p) const
{
    const Box b = m_bounds;
    if (b.empty() || !std::isfinite(p.x) || !std::isfinite(p.y))
        return std::nullopt;
    return PointF{std::clamp(p.x, double(b.x0), double(b.x1)), std::clamp(p.y, double(b.y0), double(b.y1))};
}

std::pair<int, int> MagneticTracer::pixel(PointF p) const
{
    const Box b = m_bounds;
    auto c = [](double v, int lo, int hi) {
        if (!std::isfinite(v))
            return lo;
        return std::clamp(int(std::floor(v)), lo, std::max(hi - 1, lo));
    };
    return {c(p.x, b.x0, b.x1), c(p.y, b.y0, b.y1)};
}

std::vector<uint32_t> MagneticTracer::pixels(const Fetch& fetch, Box r)
{
    const int w = r.width();
    std::vector<uint32_t> out(size_t(w) * r.height(), 0);
    if (r.empty())
        return out;
    if (m_blocks.size() > kMaxBlocks)
        m_blocks.clear();
    auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    const int bx0 = floorDiv(r.x0, kBlock), by0 = floorDiv(r.y0, kBlock);
    const int bx1 = floorDiv(r.x1 - 1, kBlock), by1 = floorDiv(r.y1 - 1, kBlock);
    for (int by = by0; by <= by1; ++by)
        for (int bx = bx0; bx <= bx1; ++bx) {
            const Box br = Box{bx * kBlock, by * kBlock, (bx + 1) * kBlock, (by + 1) * kBlock}.intersect(m_bounds);
            if (br.empty())
                continue;
            const size_t n = size_t(br.width()) * br.height();
            const uint64_t key = (uint64_t(uint32_t(bx)) << 32) | uint32_t(by);
            auto it = m_blocks.find(key);
            if (it == m_blocks.end()) {
                std::vector<uint32_t> v = fetch(br);
                if (v.size() != n)
                    v.assign(n, 0);
                it = m_blocks.emplace(key, std::move(v)).first;
            }
            const std::vector<uint32_t>& block = it->second;
            const Box ov = br.intersect(r);
            const int len = ov.width();
            for (int y = ov.y0; y < ov.y1; ++y) {
                const size_t src = size_t(y - br.y0) * br.width() + (ov.x0 - br.x0);
                const size_t dst = size_t(y - r.y0) * w + (ov.x0 - r.x0);
                std::copy(block.begin() + long(src), block.begin() + long(src + len), out.begin() + long(dst));
            }
        }
    return out;
}

MagneticTracer::Field MagneticTracer::field(const Fetch& fetch, Box area)
{
    const Box ext = area.inflate(2).intersect(m_bounds);
    const std::vector<uint32_t> px = pixels(fetch, ext);
    return Field::make(area, ext, px);
}

std::optional<PointF> MagneticTracer::snap(const Fetch& fetch, PointF p0, MagneticSettings s)
{
    s = MagneticSettings::make(s.width, s.contrast);
    const auto p = clampPoint(p0);
    if (!p)
        return std::nullopt;
    const double r = s.width;
    const auto [px, py] = pixel(*p);
    const int ri = int(std::ceil(r)) + 1;
    const Box area = Box{px - ri, py - ri, px + ri + 1, py + ri + 1}.intersect(m_bounds);
    const Field f = field(fetch, area);
    std::optional<std::pair<float, PointF>> best;
    for (int y = area.y0; y < area.y1; ++y)
        for (int x = area.x0; x < area.x1; ++x) {
            const auto i = f.index(x, y);
            if (!i)
                continue;
            const float strength = f.strength[*i];
            if (strength < s.contrast || !f.ridge[*i])
                continue;
            const double d = dist({double(x) + 0.5, double(y) + 0.5}, *p);
            if (d > r)
                continue;
            const float score = strength * (1.f - 0.25f * float(d / r));
            if (!best || score > best->first)
                best = std::pair<float, PointF>{score, f.point(*i, x, y, s.contrast)};
        }
    return best ? best->second : *p;
}

namespace {

// Distancia al cuadrado de cada centro de píxel de 'area' al trazo, infinita más allá de 'rad'.
std::vector<float> corridor(Box area, const std::vector<PointF>& guide, double rad)
{
    const int w = area.width();
    std::vector<float> near(size_t(w) * area.height(), std::numeric_limits<float>::infinity());
    const double r2 = rad * rad;
    std::vector<PointF> segs = guide;
    if (segs.size() == 1)
        segs.push_back(segs[0]);
    for (size_t k = 0; k + 1 < segs.size(); ++k) {
        const PointF a = segs[k], b = segs[k + 1];
        auto lo = [rad](double u, double v) { return int(std::floor(std::min(u, v) - rad)); };
        auto hi = [rad](double u, double v) { return int(std::ceil(std::max(u, v) + rad)) + 1; };
        const Box r = Box{lo(a.x, b.x), lo(a.y, b.y), hi(a.x, b.x), hi(a.y, b.y)}.intersect(area);
        for (int y = r.y0; y < r.y1; ++y)
            for (int x = r.x0; x < r.x1; ++x) {
                const double d2 = dist2Segment({double(x) + 0.5, double(y) + 0.5}, a, b);
                if (d2 <= r2) {
                    float& v = near[size_t(y - area.y0) * w + (x - area.x0)];
                    v = std::min(v, float(d2));
                }
            }
    }
    return near;
}

} // namespace

std::vector<PointF> MagneticTracer::tracePiece(const Fetch& fetch, PointF a, PointF b, const std::vector<PointF>& guide,
                                               MagneticSettings s)
{
    const double rad = s.width + 1.5;
    const auto [ax, ay] = pixel(a);
    const auto [bx, by] = pixel(b);
    Box bbox{std::min(ax, bx), std::min(ay, by), std::max(ax, bx) + 1, std::max(ay, by) + 1};
    for (const PointF& q : guide) {
        const auto [x, y] = pixel(q);
        bbox = bbox.unite({x, y, x + 1, y + 1});
    }
    const Box area = bbox.inflate(int(std::ceil(rad)) + 1).intersect(m_bounds);
    const Field f = field(fetch, area);
    std::vector<float> near = corridor(area, guide, rad);
    const auto start = f.index(ax, ay), goal = f.index(bx, by);
    if (!start || !goal)
        return {a, b};
    near[*start] = 0.f;
    near[*goal] = 0.f;
    // La búsqueda: el camino 8-conexo más barato por el pasillo.
    const int w = f.w, h = f.h();
    const size_t n = size_t(w) * h;
    const float rad2 = float(rad * rad);
    const float contrast = s.contrast;
    std::vector<float> node(n, std::numeric_limits<float>::infinity());
    std::vector<float> tangent(n * 2, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const float d2 = near[i];
        if (!std::isfinite(d2))
            continue;
        const float st = f.strength[i];
        const bool onEdge = st >= contrast && f.ridge[i];
        const float good = std::clamp((st - contrast) / std::max(contrast, 0.02f), 0.f, 1.f) * (0.6f + 0.4f * std::min(st, 1.f));
        const float pull = std::min(std::sqrt(d2 / std::max(rad2, 1.f)), 1.f);
        const float centre = onEdge ? 0.f : 1.f;
        node[i] = kWCentre * centre + kWStrength * (1.f - good) + kWLength + kWGuide * pull;
        if (st >= 0.5f * contrast) {
            tangent[i * 2] = f.grad[i * 2 + 1];
            tangent[i * 2 + 1] = -f.grad[i * 2];
        }
    }
    std::vector<float> distv(n, std::numeric_limits<float>::infinity());
    std::vector<uint8_t> from(n, 255);
    distv[*start] = 0.f;
    using Item = std::pair<uint32_t, size_t>; // (bits del coste, índice): mínimo primero
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    auto bits = [](float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        return u;
    };
    heap.push({bits(0.f), *start});
    while (!heap.empty()) {
        const auto [db, i] = heap.top();
        heap.pop();
        float d;
        std::memcpy(&d, &db, 4);
        if (d > distv[i])
            continue;
        if (i == *goal)
            break;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const float* tp = tangent.data() + i * 2;
        for (int k = 0; k < 8; ++k) {
            const int dx = kDirs[k][0], dy = kDirs[k][1];
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            const size_t j = size_t(ny) * w + nx;
            const float cost = node[j];
            if (!std::isfinite(cost))
                continue;
            const float len = dx != 0 && dy != 0 ? kSqrt2 : 1.f;
            const float nd = d + cost * len + kWDirection * directionCost(tp, tangent.data() + j * 2, dx, dy, len);
            if (nd < distv[j]) {
                distv[j] = nd;
                from[j] = uint8_t(k);
                heap.push({bits(nd), j});
            }
        }
    }
    if (!std::isfinite(distv[*goal]))
        return {a, b};
    std::vector<size_t> cells{*goal};
    size_t cur = *goal;
    while (cur != *start && cells.size() <= n) {
        if (from[cur] >= 8)
            return {a, b};
        const int x = int(cur % size_t(w)) - kDirs[from[cur]][0], y = int(cur / size_t(w)) - kDirs[from[cur]][1];
        if (x < 0 || y < 0)
            return {a, b};
        cur = size_t(y) * w + x;
        cells.push_back(cur);
    }
    std::reverse(cells.begin(), cells.end());
    std::vector<PointF> pts;
    pts.reserve(cells.size());
    for (size_t c : cells)
        pts.push_back(f.pointAt(c, s.contrast));
    if (pts.size() <= 1)
        return {a, b};
    pts.front() = a;
    pts.back() = b;
    return pts;
}

std::vector<PointF> MagneticTracer::trace(const Fetch& fetch, PointF from0, PointF to0, const std::vector<PointF>& guide,
                                          MagneticSettings s)
{
    s = MagneticSettings::make(s.width, s.contrast);
    const auto from = clampPoint(from0), to = clampPoint(to0);
    if (!from || !to)
        return {};
    std::vector<PointF> g;
    g.reserve(guide.size() + 2);
    g.push_back(*from);
    for (const PointF& q : guide)
        if (const auto c = clampPoint(q))
            g.push_back(*c);
    g.push_back(*to);
    g = simplifyPolyline(g, 0.5);
    if (polylineLength(g) > kMaxGuideLength)
        return g;
    const auto pieces = split(g, kPiece);
    std::vector<PointF> out{*from};
    PointF cur = *from;
    const size_t last = pieces.empty() ? 0 : pieces.size() - 1;
    for (size_t k = 0; k < pieces.size(); ++k) {
        PointF target = *to;
        if (k != last && !pieces[k].empty())
            if (const auto sp = snap(fetch, pieces[k].back(), s))
                target = *sp;
        const auto part = tracePiece(fetch, cur, target, pieces[k], s);
        out.insert(out.end(), part.begin() + std::min<long>(1, long(part.size())), part.end());
        cur = target;
    }
    return simplifyPolyline(out, 0.5);
}

} // namespace retouch
