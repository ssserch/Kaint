// Efectos > Color > Cuantificar: reduce la imagen a una paleta de N colores.

#include "core/effects/EffectUtils.h"

#include <memory>
#include <vector>

namespace fx {

namespace {

struct Rgb {
    int r, g, b;
};

std::vector<Rgb> collect(const QImage& src, const QRect& roi)
{
    // Con imágenes enormes basta con una muestra uniforme de ~1 M de píxeles.
    const qint64 total = qint64(roi.width()) * roi.height();
    const int step = std::max(1, int(std::sqrt(double(total) / 1e6)));
    std::vector<Rgb> colors;
    colors.reserve(size_t(total / (qint64(step) * step) + 1));
    const ConstPixels in(src);
    for (int y = roi.top(); y <= roi.bottom(); y += step) {
        const QRgb* row = in.row(y);
        for (int x = roi.left(); x <= roi.right(); x += step) {
            if (qAlpha(row[x]) < 16)
                continue;
            const QRgb u = qUnpremultiply(row[x]);
            colors.push_back({qRed(u), qGreen(u), qBlue(u)});
        }
    }
    return colors;
}

std::vector<Rgb> medianCut(std::vector<Rgb> colors, int count)
{
    struct Box {
        size_t begin, end;
        int channel = 0, range = 0;
    };
    auto measure = [&](Box& b) {
        int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        for (size_t i = b.begin; i < b.end; ++i) {
            const int c[3] = {colors[i].r, colors[i].g, colors[i].b};
            for (int k = 0; k < 3; ++k)
                lo[k] = std::min(lo[k], c[k]), hi[k] = std::max(hi[k], c[k]);
        }
        b.channel = 0;
        for (int k = 1; k < 3; ++k)
            if (hi[k] - lo[k] > hi[b.channel] - lo[b.channel])
                b.channel = k;
        b.range = b.end - b.begin < 2 ? 0 : hi[b.channel] - lo[b.channel];
    };
    std::vector<Box> boxes{{0, colors.size()}};
    measure(boxes[0]);
    while (int(boxes.size()) < count) {
        auto it = std::max_element(boxes.begin(), boxes.end(), [](const Box& x, const Box& y) { return x.range < y.range; });
        if (it == boxes.end() || it->range == 0)
            break;
        Box b = *it;
        const size_t mid = b.begin + (b.end - b.begin) / 2;
        const int ch = b.channel;
        auto key = [ch](const Rgb& c) { return ch == 0 ? c.r : ch == 1 ? c.g : c.b; };
        std::nth_element(colors.begin() + long(b.begin), colors.begin() + long(mid), colors.begin() + long(b.end),
                         [&](const Rgb& x, const Rgb& y) { return key(x) < key(y); });
        Box lo{b.begin, mid}, hi{mid, b.end};
        measure(lo);
        measure(hi);
        *it = lo;
        boxes.push_back(hi);
    }
    std::vector<Rgb> palette;
    for (const Box& b : boxes) {
        if (b.end == b.begin)
            continue;
        long r = 0, g = 0, bl = 0;
        for (size_t i = b.begin; i < b.end; ++i)
            r += colors[i].r, g += colors[i].g, bl += colors[i].b;
        const long n = long(b.end - b.begin);
        palette.push_back({int(r / n), int(g / n), int(bl / n)});
    }
    return palette;
}

// Cuantificador por octree: los colores se agrupan en un árbol de 8 niveles y se fusionan las
// hojas menos usadas hasta quedar con 'count' colores.
class Octree {
public:
    void add(const Rgb& c) { insert(m_root, c, 0); }

    std::vector<Rgb> palette(int count)
    {
        while (m_leaves > count) {
            int level = 7;
            while (level >= 0 && m_reducible[size_t(level)].empty())
                --level;
            if (level < 0)
                break;
            auto& list = m_reducible[size_t(level)];
            if (!m_sorted[size_t(level)]) {
                std::sort(list.begin(), list.end(), [](const Node* a, const Node* b) { return a->count > b->count; });
                m_sorted[size_t(level)] = true;
            }
            Node* node = list.back();
            list.pop_back();
            reduce(node);
        }
        std::vector<Rgb> out;
        collectLeaves(m_root.get(), out);
        return out;
    }

private:
    struct Node {
        long r = 0, g = 0, b = 0, count = 0;
        bool leaf = false;
        std::unique_ptr<Node> child[8];
    };

    void insert(std::unique_ptr<Node>& node, const Rgb& c, int level)
    {
        if (!node) {
            node = std::make_unique<Node>();
            if (level == 8) {
                node->leaf = true;
                ++m_leaves;
            } else {
                m_reducible[size_t(level)].push_back(node.get());
            }
        }
        if (node->leaf) {
            node->r += c.r, node->g += c.g, node->b += c.b, ++node->count;
            return;
        }
        node->count++;
        const int shift = 7 - level;
        const int idx = ((c.r >> shift) & 1) << 2 | ((c.g >> shift) & 1) << 1 | ((c.b >> shift) & 1);
        insert(node->child[idx], c, level + 1);
    }

    void reduce(Node* node)
    {
        long r = 0, g = 0, b = 0, count = 0;
        int removed = 0;
        for (auto& ch : node->child) {
            if (!ch)
                continue;
            if (!ch->leaf)
                reduce(ch.get());
            r += ch->r, g += ch->g, b += ch->b, count += ch->count;
            ++removed;
            ch.reset();
        }
        node->r = r, node->g = g, node->b = b, node->count = count;
        node->leaf = true;
        m_leaves -= removed - 1;
    }

    void collectLeaves(const Node* node, std::vector<Rgb>& out) const
    {
        if (!node)
            return;
        if (node->leaf) {
            const long n = std::max(1L, node->count);
            out.push_back({int(node->r / n), int(node->g / n), int(node->b / n)});
            return;
        }
        for (const auto& ch : node->child)
            collectLeaves(ch.get(), out);
    }

    std::unique_ptr<Node> m_root;
    std::array<std::vector<Node*>, 8> m_reducible;
    std::array<bool, 8> m_sorted{};
    int m_leaves = 0;
};

std::any prepareQuantize(const QImage& src, const QRect& roi, const ParamValues& v, const EffectContext&)
{
    const int count = std::clamp(ival(v, "colors"), 2, 256);
    std::vector<Rgb> colors = collect(src, roi);
    if (colors.empty())
        return std::vector<Rgb>{{0, 0, 0}};
    if (ival(v, "algorithm") == 1) {
        Octree tree;
        for (const Rgb& c : colors)
            tree.add(c);
        return tree.palette(count);
    }
    return medianCut(std::move(colors), count);
}

void renderQuantize(const RenderArgs& a)
{
    const auto palette = std::any_cast<std::vector<Rgb>>(a.state);
    const double dither = ival(a.values, "dither") / 8.0;
    const int threshold = a.values.value(QStringLiteral("threshold"), 0).toInt();
    std::vector<int16_t> cache(32768, -1);
    auto nearest = [&](int r, int g, int b) {
        const int key = (r >> 3) << 10 | (g >> 3) << 5 | (b >> 3);
        int16_t& slot = cache[size_t(key)];
        if (slot >= 0)
            return int(slot);
        int best = 0, bestD = INT32_MAX;
        for (size_t i = 0; i < palette.size(); ++i) {
            const int dr = palette[i].r - r, dg = palette[i].g - g, db = palette[i].b - b;
            const int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bestD)
                bestD = d, best = int(i);
        }
        slot = int16_t(best);
        return best;
    };
    const int w = a.roi.width();
    std::vector<float> err(size_t(w + 2) * 3 * 2, 0.f);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    for (int y = a.roi.top(); y <= a.roi.bottom() && !a.cancel; ++y) {
        float* cur = &err[size_t((y - a.roi.top()) % 2) * size_t(w + 2) * 3];
        float* next = &err[size_t((y - a.roi.top() + 1) % 2) * size_t(w + 2) * 3];
        std::fill(next, next + size_t(w + 2) * 3, 0.f);
        const QRgb* s = in.row(y);
        QRgb* d = out.row(y);
        for (int i = 0; i < w; ++i) {
            const int x = a.roi.left() + i;
            if (qAlpha(s[x]) == 0 || qAlpha(s[x]) < threshold) {
                d[x] = 0;
                continue;
            }
            const QRgb u = qUnpremultiply(s[x]);
            float* e = &cur[size_t(i + 1) * 3];
            const float want[3] = {qRed(u) + e[0], qGreen(u) + e[1], qBlue(u) + e[2]};
            const Rgb& c = palette[size_t(nearest(clamp255f(want[0]), clamp255f(want[1]), clamp255f(want[2])))];
            d[x] = qPremultiply(qRgba(c.r, c.g, c.b, qAlpha(u)));
            if (dither <= 0)
                continue;
            const float diff[3] = {float((want[0] - c.r) * dither), float((want[1] - c.g) * dither),
                                   float((want[2] - c.b) * dither)};
            for (int k = 0; k < 3; ++k) {
                cur[size_t(i + 2) * 3 + size_t(k)] += diff[k] * 7 / 16;
                next[size_t(i) * 3 + size_t(k)] += diff[k] * 3 / 16;
                next[size_t(i + 1) * 3 + size_t(k)] += diff[k] * 5 / 16;
                next[size_t(i + 2) * 3 + size_t(k)] += diff[k] * 1 / 16;
            }
        }
        if (a.ctx.progress)
            ++a.ctx.progress->done;
    }
}

} // namespace

void registerColor(QList<const Effect*>& list)
{
    using P = EffectParam;
    SimpleEffect::Def def{"quantize", QObject::tr("Cuantizar"), catColor(), "colorfx", {},
                          {P::choice("algorithm", QObject::tr("Algoritmo"),
                                     {QObject::tr("Corte a la mediana"), QObject::tr("Octree")}, 1),
                           P::integer("colors", QObject::tr("Colores"), 2, 256, 256),
                           P::integer("dither", QObject::tr("Nivel de tramado"), 0, 8, 7),
                           P::integer("threshold", QObject::tr("Umbral de transparencia"), 0, 255, 128)},
                          renderQuantize, prepareQuantize};
    def.note = QObject::tr("Los píxeles que tengan un valor del canal alfa menor que el umbral serán totalmente "
                           "transparentes.");
    list.append(new SimpleEffect(std::move(def)));
}

} // namespace fx
