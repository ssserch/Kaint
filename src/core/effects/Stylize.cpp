// Efectos > Estilizar (ColorDifferenceEffect y OutlineEffect de Paint.NET 3.36)

#include "core/effects/EffectUtils.h"

namespace fx {

namespace {

using Weights = std::array<std::array<double, 3>, 3>;

Weights directionalWeights(double angleDeg, double center)
{
    const double r = angleDeg * 2.0 * M_PI / 360.0;
    const double dr = M_PI / 4.0;
    Weights w{};
    w[0] = {std::cos(r + dr), std::cos(r + 2 * dr), std::cos(r + 3 * dr)};
    w[1] = {std::cos(r), center, std::cos(r + 4 * dr)};
    w[2] = {std::cos(r - dr), std::cos(r - 2 * dr), std::cos(r - 3 * dr)};
    return w;
}

// Convolución 3x3 sobre colores directos; en los bordes se omiten las filas y columnas que
// quedan fuera, como en Paint.NET.
void colorDifference(const RenderArgs& a, const Weights& w)
{
    const QImage straight = a.src.convertToFormat(QImage::Format_ARGB32);
    const ConstPixels in(straight);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const int fy0 = y == 0 ? 1 : 0, fy1 = y == in.h - 1 ? 2 : 3;
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int fx0 = x == 0 ? 1 : 0, fx1 = x == in.w - 1 ? 2 : 3;
            double r = 0, g = 0, b = 0;
            for (int fy = fy0; fy < fy1; ++fy)
                for (int fx = fx0; fx < fx1; ++fx) {
                    const QRgb c = in.at(x - 1 + fx, y - 1 + fy);
                    const double k = w[size_t(fy)][size_t(fx)];
                    r += k * qRed(c), g += k * qGreen(c), b += k * qBlue(c);
                }
            d[x] = qPremultiply(qRgba(clamp255(int(r)), clamp255(int(g)), clamp255(int(b)), qAlpha(in.at(x, y))));
        }
    });
}

// Detección de bordes de Paint.NET 5: magnitud del gradiente (Sobel, Prewitt o Scharr) de cada
// canal, opcionalmente sobre un desenfoque previo, en negro o sobre la imagen ("Bordes
// superpuestos").
void renderEdgeDetect(const RenderArgs& a)
{
    const double strength = dval(a.values, "intensity");
    const double blur = dval(a.values, "blur");
    const int algorithm = ival(a.values, "algorithm");
    const bool overlay = bval(a.values, "overlay");
    const int margin = 2 + int(std::ceil(blur * 3));
    const QRect big = a.roi.adjusted(-margin, -margin, margin, margin).intersected(a.src.rect());
    QImage work = a.src.copy(big);
    if (blur > 0) {
        std::atomic_bool never{false};
        work = tentBlur(work, work.rect(), std::max(1, int(std::lround(blur * 2))), never);
    }
    const QImage straight = work.convertToFormat(QImage::Format_ARGB32);
    const ConstPixels in(straight);
    const ConstPixels orig(a.src);
    const Pixels out(a.dst);
    const double center = algorithm == 2 ? 10 : (algorithm == 1 ? 1 : 2);
    const double side = algorithm == 2 ? 3 : 1;
    const double norm = 1.0 / ((center + 2 * side) * 255.0);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        const int ly = y - big.top();
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int lx = x - big.left();
            auto px = [&](int dx, int dy) { return in.clamped(lx + dx, ly + dy); };
            double mag[3];
            for (int c = 0; c < 3; ++c) {
                auto ch = [&](QRgb p) { return double((p >> (16 - 8 * c)) & 0xff); };
                const double gx = side * (ch(px(1, -1)) - ch(px(-1, -1))) + center * (ch(px(1, 0)) - ch(px(-1, 0)))
                                + side * (ch(px(1, 1)) - ch(px(-1, 1)));
                const double gy = side * (ch(px(-1, 1)) - ch(px(-1, -1))) + center * (ch(px(0, 1)) - ch(px(0, -1)))
                                + side * (ch(px(1, 1)) - ch(px(1, -1)));
                mag[c] = std::min(1.0, std::sqrt(gx * gx + gy * gy) * norm * strength * 4);
            }
            const QRgb o = qUnpremultiply(orig.at(x, y));
            QRgb result;
            if (overlay) {
                auto screen = [&](int base, double e) { return clamp255(int(std::lround(255 - (255 - base) * (1 - e)))); };
                result = qRgba(screen(qRed(o), mag[0]), screen(qGreen(o), mag[1]), screen(qBlue(o), mag[2]), qAlpha(o));
            } else {
                result = qRgba(int(mag[0] * 255), int(mag[1] * 255), int(mag[2] * 255), qAlpha(o));
            }
            d[x] = qPremultiply(result);
        }
    });
}

void renderRelief(const RenderArgs& a)
{
    colorDifference(a, directionalWeights(dval(a.values, "angle"), 1));
}

void renderEmboss(const RenderArgs& a)
{
    const Weights w = directionalWeights(dval(a.values, "angle"), 0);
    const QImage straight = a.src.convertToFormat(QImage::Format_ARGB32);
    const ConstPixels in(straight);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const int fy0 = y == 0 ? 1 : 0, fy1 = y == in.h - 1 ? 2 : 3;
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int fx0 = x == 0 ? 1 : 0, fx1 = x == in.w - 1 ? 2 : 3;
            double sum = 0;
            for (int fy = fy0; fy < fy1; ++fy)
                for (int fx = fx0; fx < fx1; ++fx)
                    sum += w[size_t(fy)][size_t(fx)] * intensity(in.at(x - 1 + fx, y - 1 + fy));
            const int v = clamp255(int(sum) + 128);
            d[x] = qPremultiply(qRgba(v, v, v, qAlpha(in.at(x, y))));
        }
    });
}

void renderOutline(const RenderArgs& a)
{
    const int intensityParam = ival(a.values, "intensity");
    const int bits = ival(a.values, "quality");
    localHistogram(a, ival(a.values, "thickness"), [&](QRgb, const LocalHistogram& h) {
        const int min1 = h.area * (100 - intensityParam) / 200;
        const int min2 = h.area * (100 + intensityParam) / 200;
        auto span = [&](const int* hist, int& v1, int& v2) {
            int count = 0;
            v1 = 0;
            while (v1 < 255 && hist[v1] == 0)
                ++v1;
            while (v1 < 255 && count < min1) {
                count += hist[v1];
                ++v1;
            }
            v2 = v1;
            while (v2 < 255 && count < min2) {
                count += hist[v2];
                ++v2;
            }
        };
        int r1, r2, g1, g2, b1, b2, a1, a2;
        span(h.r, r1, r2);
        span(h.g, g1, g2);
        span(h.b, b1, b2);
        span(h.a, a1, a2);
        return quantizeBits(qRgba(255 - (r2 - r1), 255 - (g2 - g1), 255 - (b2 - b1), a2), bits);
    });
}

} // namespace

void registerStylize(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"edgedetect", QObject::tr("Detección de bordes"), catStylize(), {}, {},
         {P::real("intensity", QObject::tr("Intensidad"), 0, 1, 0.5, 2), P::real("blur", QObject::tr("Desenfoque"), 0, 10, 0, 2),
          P::choice("algorithm", QObject::tr("Algoritmo"), {QObject::tr("Sobel"), QObject::tr("Prewitt"), QObject::tr("Scharr")},
                    0),
          P::boolean("overlay", QObject::tr("Bordes superpuestos"), false)},
         renderEdgeDetect});
    add({"emboss", QObject::tr("Relieve"), catStylize(), {}, {}, {P::angle("angle", QObject::tr("Ángulo"), -180, 180, 45)},
         renderEmboss});
    add({"outline", QObject::tr("Contorno"), catStylize(), {}, {},
         {P::integer("thickness", QObject::tr("Grosor"), 1, 200, 1),
          P::integer("intensity", QObject::tr("Intensidad"), 0, 100, 50),
          P::integer("quality", QObject::tr("Calidad"), 1, 9, 8)},
         renderOutline});
    add({"relief", QObject::tr("Realce"), catStylize(), {}, {}, {P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0)},
         renderRelief});
}

} // namespace fx
