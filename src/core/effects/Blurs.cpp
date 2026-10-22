// Efectos > Desenfoques

#include "core/Raster.h"
#include "core/effects/EffectUtils.h"

#include <functional>
#include <vector>

namespace fx {

namespace {

// Realce de gamma: los colores se elevan a 'gamma' antes de promediar y se devuelven después,
// así las luces dominan (aspecto "high key").
struct GammaTables {
    std::array<float, 256> forward{};
    double gamma = 1;
    explicit GammaTables(double g) : gamma(g)
    {
        for (int i = 0; i < 256; ++i)
            forward[size_t(i)] = float(std::pow(i / 255.0, gamma));
    }
    int back(float v) const { return clamp255f(float(std::pow(std::max(0.f, v), 1.0 / gamma) * 255.0)); }
};

void copyBlurred(const RenderArgs& a, const QImage& blurred)
{
    if (!blurred.isNull())
        Raster::blit(a.dst, blurred, a.roi.topLeft());
}

// Desenfoque con varias pasadas de caja (Calidad): 2 = el triangular clásico de Paint.NET; más
// pasadas se acercan a la campana de Gauss con la misma varianza.
QImage multiBox(const QImage& src, const QRect& area, double radius, int passes, const std::atomic_bool& cancel)
{
    const int r = std::max(0, int(std::lround(radius)));
    if (passes == 2 || r == 0)
        return tentBlur(src, area, r, cancel);
    const double variance = 2.0 * ((r + 1.0) * (r + 1.0) - 1.0) / 12.0;
    const int box = std::max(1, int(std::lround((std::sqrt(12.0 * variance / passes + 1.0) - 1.0) / 2.0)));
    const int margin = passes * box + 1;
    const QRect big = area.adjusted(-margin, -margin, margin, margin).intersected(src.rect());
    QImage work = src.copy(big);
    for (int i = 0; i < passes; ++i) {
        work = boxBlur(work, work.rect(), box, cancel);
        if (work.isNull())
            return {};
    }
    return work.copy(area.translated(-big.topLeft()));
}

// Aplica 'blur' con realce de gamma ("Potenciar Gamma" de Paint.NET 5: 0 = sin realce).
void blurWithGamma(const RenderArgs& a, double boost, int margin,
                   const std::function<QImage(const QImage&, const QRect&)>& blur)
{
    if (boost <= 0.0001) {
        copyBlurred(a, blur(a.src, a.roi));
        return;
    }
    const GammaTables gt(1.0 + boost);
    const QRect big = a.roi.adjusted(-margin, -margin, margin, margin).intersected(a.src.rect());
    QImage boosted = a.src.copy(big);
    for (int y = 0; y < boosted.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(boosted.scanLine(y));
        for (int x = 0; x < boosted.width(); ++x) {
            const QRgb u = qUnpremultiply(row[x]);
            auto f = [&](int c) { return clamp255f(gt.forward[size_t(c)] * 255.f); };
            row[x] = qPremultiply(qRgba(f(qRed(u)), f(qGreen(u)), f(qBlue(u)), qAlpha(u)));
        }
    }
    const QImage blurred = blur(boosted, QRect(a.roi.topLeft() - big.topLeft(), a.roi.size()));
    if (blurred.isNull())
        return;
    const Pixels out(a.dst);
    const ConstPixels bl(blurred);
    forRows(a, [&](int y) {
        const QRgb* b = bl.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const QRgb u = qUnpremultiply(b[x - a.roi.left()]);
            auto back = [&](int c) { return gt.back(c / 255.f); };
            d[x] = qPremultiply(qRgba(back(qRed(u)), back(qGreen(u)), back(qBlue(u)), qAlpha(u)));
        }
    });
}

void renderGaussian(const RenderArgs& a)
{
    const double radius = dval(a.values, "radius");
    const int passes = std::clamp(ival(a.values, "quality"), 1, 4);
    blurWithGamma(a, dval(a.values, "gamma"), tentMargin(int(std::ceil(radius))) + 4,
                  [&](const QImage& img, const QRect& area) { return multiBox(img, area, radius, passes, a.cancel); });
}

void renderBokeh(const RenderArgs& a)
{
    // Media de un disco (como Desenfocar/Unfocus de Paint.NET) con realce de gamma; con más
    // Calidad el borde del disco se suaviza.
    const int r = std::max(1, int(std::lround(dval(a.values, "radius"))));
    const GammaTables gt(1.0 + dval(a.values, "gamma"));
    const double edgeSoft = (std::clamp(ival(a.values, "quality"), 1, 8) - 1) / 7.0;
    const int left = std::max(0, a.roi.left() - r);
    const int right = std::min(a.src.width() - 1, a.roi.right() + r);
    const int W = right - left + 1;
    std::vector<int> half(static_cast<size_t>(r + 1));
    for (int v = 0; v <= r; ++v)
        half[size_t(v)] = int(std::floor(std::sqrt(double(r) * r - double(v) * v + r)));
    const ConstPixels in(a.src);
    const Pixels out(a.dst);

    // Por franjas para acotar la memoria de las sumas de prefijo por fila de (alfa,
    // alfa·color^gamma): el disco cuesta O(r) por píxel.
    constexpr int kBand = 256;
    std::vector<float> prefix;
    for (int band = a.roi.top(); band <= a.roi.bottom() && !a.cancel; band += kBand) {
        const int bandEnd = std::min(a.roi.bottom(), band + kBand - 1);
        const int top = std::max(0, band - r);
        const int bottom = std::min(a.src.height() - 1, bandEnd + r);
        const int H = bottom - top + 1;
        prefix.assign(size_t(W + 1) * size_t(H) * 4, 0.f);
        parallelRows(0, H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                float acc[4] = {0, 0, 0, 0};
                float* p = &prefix[size_t(y) * size_t(W + 1) * 4];
                const QRgb* s = in.row(top + y);
                for (int x = 0; x < W; ++x) {
                    const QRgb u = qUnpremultiply(s[left + x]);
                    const float al = qAlpha(u) / 255.f;
                    acc[0] += al;
                    acc[1] += al * gt.forward[size_t(qRed(u))];
                    acc[2] += al * gt.forward[size_t(qGreen(u))];
                    acc[3] += al * gt.forward[size_t(qBlue(u))];
                    for (int c = 0; c < 4; ++c)
                        p[size_t(x + 1) * 4 + size_t(c)] = acc[c];
                }
            }
        });
        forRows(a, band, bandEnd + 1, [&](int y) {
            QRgb* d = out.row(y);
            for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
                double sum[4] = {0, 0, 0, 0};
                int count = 0;
                double countF = 0;
                for (int v = -r; v <= r; ++v) {
                    const int yy = y + v - top;
                    if (yy < 0 || yy >= H)
                        continue;
                    const int w = half[size_t(std::abs(v))];
                    const int x0 = std::max(0, x - w - left);
                    const int x1 = std::min(W - 1, x + w - left);
                    if (x1 < x0)
                        continue;
                    const float* p = &prefix[size_t(yy) * size_t(W + 1) * 4];
                    for (int c = 0; c < 4; ++c)
                        sum[c] += double(p[size_t(x1 + 1) * 4 + size_t(c)]) - p[size_t(x0) * 4 + size_t(c)];
                    count += x1 - x0 + 1;
                    if (edgeSoft > 0) {
                        const double frac = (std::sqrt(std::max(0.0, double(r) * r - double(v) * v)) - w + 1) * edgeSoft * 0.5;
                        for (const int xe : {x - w - 1 - left, x + w + 1 - left}) {
                            if (xe < 0 || xe >= W || frac <= 0)
                                continue;
                            for (int c = 0; c < 4; ++c)
                                sum[c] += frac * (double(p[size_t(xe + 1) * 4 + size_t(c)]) - p[size_t(xe) * 4 + size_t(c)]);
                            countF += frac;
                        }
                    }
                }
                if (sum[0] <= 1e-6 || count == 0) {
                    d[x] = 0;
                    continue;
                }
                const int alpha = clamp255f(float(sum[0] / (count + countF) * 255.0));
                d[x] = qPremultiply(qRgba(gt.back(float(sum[1] / sum[0])), gt.back(float(sum[2] / sum[0])),
                                          gt.back(float(sum[3] / sum[0])), alpha));
            }
        });
    }
}

void renderFragment(const RenderArgs& a)
{
    const int fragments = ival(a.values, "fragments");
    const int distance = ival(a.values, "distance");
    const double rotation = dval(a.values, "rotation");
    std::vector<QPoint> offsets(static_cast<size_t>(fragments));
    const double step = 2 * M_PI / fragments;
    const double rot = (rotation - 90.0) * M_PI / 180.0;
    for (int i = 0; i < fragments; ++i) {
        const double ang = rot + step * i;
        offsets[size_t(i)] = QPoint(int(std::lround(distance * -std::sin(ang))), int(std::lround(distance * -std::cos(ang))));
    }
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(offsets.size());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int n = 0;
            for (const QPoint& o : offsets) {
                const int u = x - o.x(), v = y - o.y();
                if (u >= 0 && u < in.w && v >= 0 && v < in.h)
                    samples[size_t(n++)] = in.at(u, v);
            }
            d[x] = average(samples.data(), n);
        }
    });
}

void renderMedian(const RenderArgs& a)
{
    const int percentile = ival(a.values, "percentile");
    const int bits = ival(a.values, "quality");
    localHistogram(a, ival(a.values, "radius"), [&](QRgb, const LocalHistogram& h) {
        return quantizeBits(histogramPercentile(h, percentile), bits);
    });
}

void renderMotion(const RenderArgs& a)
{
    const double angle = dval(a.values, "angle");
    const double distance = dval(a.values, "distance");
    const bool centered = bval(a.values, "centered");
    const EdgeBehavior edge = edgeValue(a.values, pdnEdges());
    const double theta = (angle + 180) * 2 * M_PI / 360.0;
    QPointF start(0, 0);
    QPointF end(distance * std::cos(theta), -distance * std::sin(theta));
    if (centered) {
        start = -end / 2.0;
        end /= 2.0;
    }
    const int count = std::max(1, int(((1 + distance) * 3) / 2));
    std::vector<QPointF> points(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        points[size_t(i)] = count == 1 ? QPointF(0, 0) : start + (end - start) * (double(i) / (count - 1));
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(points.size());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int n = 0;
            for (const QPointF& p : points) {
                const double px = p.x() + x, py = p.y() + y;
                samples[size_t(n++)] = sampleEdge(in, float(px + 0.5), float(py + 0.5), edge);
            }
            d[x] = average(samples.data(), n);
        }
    });
}

void renderRadial(const RenderArgs& a)
{
    const double angle = dval(a.values, "angle") * M_PI / 180.0;
    const QPointF off = pval(a.values, "offset");
    const double q = std::clamp(dval(a.values, "quality"), 1.0, 5.0);
    const double cx = a.roi.left() + a.roi.width() * (1 + off.x()) / 2.0;
    const double cy = a.roi.top() + a.roi.height() * (1 + off.y()) / 2.0;
    const int n = int(std::lround((q * q) * (30 + q * q)));
    const double step = angle / n;
    const double cs = std::cos(step), sn = std::sin(step);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const QRgb c0 = in.at(x, y);
            long sr = qRed(c0), sg = qGreen(c0), sb = qBlue(c0), sa = qAlpha(c0);
            int sc = 1;
            double x1 = x - cx, y1 = y - cy, x2 = x1, y2 = y1;
            for (int i = 0; i < n; ++i) {
                const double nx1 = x1 * cs - y1 * sn, ny1 = x1 * sn + y1 * cs;
                const double nx2 = x2 * cs + y2 * sn, ny2 = -x2 * sn + y2 * cs;
                x1 = nx1, y1 = ny1, x2 = nx2, y2 = ny2;
                for (const auto& [px, py] : {std::pair{x1, y1}, std::pair{x2, y2}}) {
                    const int u = int(std::lround(px + cx)), v = int(std::lround(py + cy));
                    if (u > 0 && v > 0 && u < in.w && v < in.h) {
                        const QRgb s = in.at(u, v);
                        sr += qRed(s), sg += qGreen(s), sb += qBlue(s), sa += qAlpha(s);
                        ++sc;
                    }
                }
            }
            d[x] = qRgba(int(sr / sc), int(sg / sc), int(sb / sc), int(sa / sc));
        }
    });
}

void renderSketchBlur(const RenderArgs& a)
{
    // Aproximación del desenfoque de bosquejo de Paint.NET 5: percentil de muestras aleatorias
    // dentro del disco, como pinceladas gruesas.
    const int radius = std::max(1, int(std::lround(dval(a.values, "radius"))));
    const int percentile = ival(a.values, "percentile");
    const int samples = 8 + ival(a.values, "smoothness") * 8;
    const uint32_t seed = 0;
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<int> ch[4];
        for (auto& c : ch)
            c.resize(size_t(samples));
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int n = 0;
            for (int i = 0; i < samples; ++i) {
                const float ang = hashUnit(uint32_t(x), uint32_t(y * samples + i), seed) * float(2 * M_PI);
                const float dist = std::sqrt(hashUnit(uint32_t(x * 7 + i), uint32_t(y), seed + 99u)) * radius;
                const int u = std::clamp(int(x + std::cos(ang) * dist), 0, in.w - 1);
                const int v = std::clamp(int(y + std::sin(ang) * dist), 0, in.h - 1);
                const QRgb s = qUnpremultiply(in.at(u, v));
                ch[0][size_t(n)] = qRed(s), ch[1][size_t(n)] = qGreen(s), ch[2][size_t(n)] = qBlue(s), ch[3][size_t(n)] = qAlpha(s);
                ++n;
            }
            const size_t k = size_t(std::clamp(percentile * (n - 1) / 100, 0, n - 1));
            int value[4];
            for (int c = 0; c < 4; ++c) {
                std::nth_element(ch[c].begin(), ch[c].begin() + long(k), ch[c].begin() + n);
                value[c] = ch[c][k];
            }
            d[x] = qPremultiply(qRgba(value[0], value[1], value[2], value[3]));
        }
    });
}

void renderSquare(const RenderArgs& a)
{
    const int radius = std::max(0, int(std::lround(dval(a.values, "radius"))));
    blurWithGamma(a, dval(a.values, "gamma"), radius + 2,
                  [&](const QImage& img, const QRect& area) { return boxBlur(img, area, radius, a.cancel); });
}

void renderSurface(const RenderArgs& a)
{
    // Desenfoque de superficie de Paint.NET: media ponderada por parecido de color.
    const int threshold = std::max(1, ival(a.values, "threshold"));
    std::array<int, 256> factor{};
    const double slope = 96.0 / threshold;
    for (int i = 0; i < 256; ++i)
        factor[size_t(i)] = std::max(0, int(std::lround(255 - i * slope)));
    auto blurChannel = [&](int current, const int* hist) {
        int sum = 0, divisor = 0;
        for (int bin = 0; bin < 256; ++bin) {
            if (hist[bin] == 0)
                continue;
            const int w = factor[size_t(std::abs(bin - current))];
            if (w > 0) {
                const int t = hist[bin] * w;
                sum += t * bin;
                divisor += t;
            }
        }
        return divisor > 0 ? (sum + (divisor >> 1)) / divisor : current;
    };
    localHistogram(a, ival(a.values, "radius"), [&](QRgb c, const LocalHistogram& h) {
        return qRgba(blurChannel(qRed(c), h.r), blurChannel(qGreen(c), h.g), blurChannel(qBlue(c), h.b), qAlpha(c));
    });
}

void renderZoom(const RenderArgs& a)
{
    // Desenfoque de zum de Paint.NET 5: cada píxel se promedia a lo largo del radio hacia el
    // centro.
    const double distance = dval(a.values, "distance") / 10.0;
    const double focus = std::max(0.1, dval(a.values, "focus"));
    const double quality = std::clamp(dval(a.values, "quality"), 1.0, 5.0);
    const QPointF off = pval(a.values, "offset");
    const double cx = a.roi.left() + a.roi.width() * (1 + off.x()) / 2.0;
    const double cy = a.roi.top() + a.roi.height() * (1 + off.y()) / 2.0;
    const double maxR = std::max(1.0, std::hypot(a.roi.width(), a.roi.height()) / 2.0);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double dx = x - cx, dy = y - cy;
            const double r = std::hypot(dx, dy);
            const double shrink = std::min(0.95, distance * std::pow(std::min(1.0, r / maxR), focus - 1.0));
            const double length = r * shrink;
            const int n = std::clamp(int(length * quality / 2.0), 1, 256);
            double acc[4] = {0, 0, 0, 0};
            for (int i = 0; i < n; ++i) {
                const double t = 1.0 - shrink * (n == 1 ? 0.0 : double(i) / (n - 1));
                const QRgb s = sampleBilinear(in, float(cx + dx * t + 0.5), float(cy + dy * t + 0.5));
                acc[0] += qRed(s), acc[1] += qGreen(s), acc[2] += qBlue(s), acc[3] += qAlpha(s);
            }
            d[x] = qRgba(int(acc[0] / n), int(acc[1] / n), int(acc[2] / n), int(acc[3] / n));
        }
    });
}

} // namespace

void registerBlurs(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"bokeh", QObject::tr("Bokeh"), catBlurs(), "blurfx", {},
         {P::real("radius", QObject::tr("Radio"), 1, 200, 25, 1), P::real("gamma", QObject::tr("Potenciar Gamma"), 0, 7, 0, 2),
          P::integer("quality", QObject::tr("Calidad"), 1, 8, 3)},
         renderBokeh});
    add({"fragment", QObject::tr("Fragmento"), catBlurs(), "blurfx", {},
         {P::integer("fragments", QObject::tr("Cantidad de fragmentos"), 2, 50, 4),
          P::integer("distance", QObject::tr("Distancia"), 0, 100, 8),
          P::angle("rotation", QObject::tr("Rotación"), 0, 360, 0)},
         renderFragment});
    add({"gaussianblur", QObject::tr("Desenfoque gaussiano"), catBlurs(), "blurimage", {},
         {P::real("radius", QObject::tr("Radio"), 0, 200, 2, 1), P::real("gamma", QObject::tr("Potenciar Gamma"), 0, 7, 0, 2),
          P::integer("quality", QObject::tr("Calidad"), 1, 4, 4)},
         renderGaussian});
    add({"median", QObject::tr("Desenfoque por mediana"), catBlurs(), "blurfx", {},
         {P::integer("radius", QObject::tr("Radio"), 1, 200, 10),
          P::integer("percentile", QObject::tr("Percentil"), 0, 100, 50),
          P::integer("quality", QObject::tr("Calidad"), 1, 9, 8)},
         renderMedian});
    add({"motionblur", QObject::tr("Desenfoque de movimiento"), catBlurs(), "blurfx", {},
         {P::angle("angle", QObject::tr("Ángulo"), -180, 180, 25),
          P::real("distance", QObject::tr("Distancia"), 1, 200, 10, 2),
          P::boolean("centered", QObject::tr("Centrado"), true), edgeParam(pdnEdges(), EdgeBehavior::Clamp)},
         renderMotion});
    add({"radialblur", QObject::tr("Desenfoque radial"), catBlurs(), "blurfx", {},
         {P::angle("angle", QObject::tr("Ángulo"), 0, 360, 4), P::offset("offset", QObject::tr("Centrar")),
          P::real("quality", QObject::tr("Calidad"), 1, 5, 2, 1)},
         renderRadial});
    add({"sketchblur", QObject::tr("Boceto borroso"), catBlurs(), "blurfx", {},
         {P::real("radius", QObject::tr("Radio"), 1, 100, 25, 2),
          P::integer("percentile", QObject::tr("Percentil"), 0, 100, 50),
          P::integer("smoothness", QObject::tr("Suavizado"), 1, 10, 3)},
         renderSketchBlur});
    add({"squareblur", QObject::tr("Desenfoque cuadrado"), catBlurs(), "blurfx", {},
         {P::real("radius", QObject::tr("Radio"), 1, 200, 6, 1), P::real("gamma", QObject::tr("Potenciar Gamma"), 0, 7, 0, 2)},
         renderSquare});
    add({"surfaceblur", QObject::tr("Desenfoque de superficie"), catBlurs(), "blurfx", {},
         {P::integer("radius", QObject::tr("Radio"), 1, 100, 6),
          P::integer("threshold", QObject::tr("Umbral"), 1, 100, 15)},
         renderSurface});
    add({"zoomblur", QObject::tr("Desenfoque de zum"), catBlurs(), "zoom-in", {},
         {P::real("distance", QObject::tr("Distancia"), 0, 9.5, 1.25, 2), P::real("focus", QObject::tr("Enfoque"), 0.1, 10, 2, 2),
          P::offset("offset", QObject::tr("Centrar")), P::real("quality", QObject::tr("Calidad"), 1, 5, 2, 1)},
         renderZoom});
}

} // namespace fx
