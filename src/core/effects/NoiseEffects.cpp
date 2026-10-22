// Efectos > Ruido (algoritmos de Paint.NET 3.36)

#include "core/effects/EffectUtils.h"

#include <vector>

namespace fx {

namespace {

constexpr int kTableSize = 16384;

// Tabla de la curva normal de AddNoiseEffect: un índice aleatorio uniforme da un valor con
// distribución gaussiana.
const std::vector<int>& normalLookup()
{
    static const std::vector<int> lookup = [] {
        auto curve = [](double x, double scale) { return scale * std::exp(-x * x / 2); };
        double l = 5, r = 10, scale = 50, sum = 0;
        while (r - l > 0.0000001) {
            sum = 0;
            scale = (l + r) * 0.5;
            for (int i = 0; i < kTableSize; ++i) {
                sum += curve(16.0 * (double(i) - kTableSize / 2) / kTableSize, scale);
                if (sum > 1000000)
                    break;
            }
            if (sum > kTableSize)
                r = scale;
            else if (sum < kTableSize)
                l = scale;
            else
                break;
        }
        std::vector<int> t(kTableSize, 0);
        sum = 0;
        int rounded = 0;
        for (int i = 0; i < kTableSize; ++i) {
            sum += curve(16.0 * (double(i) - kTableSize / 2) / kTableSize, scale);
            const int last = rounded;
            rounded = int(sum);
            for (int j = last; j < rounded && j < kTableSize; ++j)
                t[size_t(j)] = (i - kTableSize / 2) * 65536 / kTableSize;
        }
        return t;
    }();
    return lookup;
}

void renderAddNoise(const RenderArgs& a)
{
    const int intensity = ival(a.values, "intensity");
    const int sat = ival(a.values, "saturation") * 4096 / 100;
    const double coverage = dval(a.values, "coverage") / 100.0;
    const uint32_t seed = uint32_t(ival(a.values, "seed")) * 2654435761u;
    const int dev = intensity * intensity / 4;
    const auto& lookup = normalLookup();
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const uint32_t ux = uint32_t(x), uy = uint32_t(y);
            if (hashUnit(ux, uy, seed) > coverage || qAlpha(s[x]) == 0) {
                d[x] = s[x];
                continue;
            }
            int r = lookup[hash3(ux, uy, seed + 1u) % kTableSize];
            int g = lookup[hash3(ux, uy, seed + 2u) % kTableSize];
            int b = lookup[hash3(ux, uy, seed + 3u) % kTableSize];
            const int i = (4899 * r + 9618 * g + 1867 * b) >> 14;
            r = i + (((r - i) * sat) >> 12);
            g = i + (((g - i) * sat) >> 12);
            b = i + (((b - i) * sat) >> 12);
            const QRgb u = qUnpremultiply(s[x]);
            d[x] = qPremultiply(qRgba(clamp255(qRed(u) + ((r * dev + 32768) >> 16)),
                                      clamp255(qGreen(u) + ((g * dev + 32768) >> 16)),
                                      clamp255(qBlue(u) + ((b * dev + 32768) >> 16)), qAlpha(u)));
        }
    });
}

void renderReduceNoise(const RenderArgs& a)
{
    const double strength = dval(a.values, "strength");
    localHistogram(a, ival(a.values, "radius"), [&](QRgb c, const LocalHistogram& h) {
        int rc = 0, gc = 0, bc = 0;
        for (int i = 0; i < qRed(c); ++i)
            rc += h.r[i];
        for (int i = 0; i < qGreen(c); ++i)
            gc += h.g[i];
        for (int i = 0; i < qBlue(c); ++i)
            bc += h.b[i];
        const int area = std::max(1, h.area);
        const QRgb normalized = qRgb(rc * 255 / area, gc * 255 / area, bc * 255 / area);
        const double t = strength * (1 - 0.75 * intensityF(c));
        const QRgb out = lerpStraight(c, normalized, t);
        return qRgba(qRed(out), qGreen(out), qBlue(out), qAlpha(c));
    });
}

} // namespace

void registerNoise(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"addnoise", QObject::tr("Agregar ruido"), catNoise(), {}, {},
         {P::integer("intensity", QObject::tr("Intensidad"), 0, 100, 64),
          P::integer("saturation", QObject::tr("Saturación de colores"), 0, 400, 100),
          P::real("coverage", QObject::tr("Cobertura"), 0, 100, 100, 2), P::seed()},
         renderAddNoise});
    add({"reducenoise", QObject::tr("Reducción de ruido"), catNoise(), {}, {},
         {P::integer("radius", QObject::tr("Radio"), 0, 200, 10), P::real("strength", QObject::tr("Intensidad"), 0, 1, 0.4, 2)},
         renderReduceNoise});
}

} // namespace fx
