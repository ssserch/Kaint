// Efectos > Artístico (algoritmos de Paint.NET 3.36)

#include "core/effects/EffectUtils.h"

#include <vector>

namespace fx {

namespace {

// Resplandor (Foto > Resplandor) aplicado sobre dst: desenfoque + brillo/contraste + trama.
void glowInto(const RenderArgs& a, int radius, int brightness, int contrast)
{
    const QImage blurred = tentBlur(a.src, a.roi, radius, a.cancel);
    if (blurred.isNull())
        return;
    const BrightnessContrast bc(brightness, contrast);
    const ConstPixels in(a.src), bl(blurred);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        const QRgb* b = bl.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const QRgb top = bc.apply(qUnpremultiply(b[x - a.roi.left()]));
            d[x] = qPremultiply(blendStraight(qUnpremultiply(s[x]), top, opScreen));
        }
    });
}

void renderInkSketch(const RenderArgs& a)
{
    static const int conv[5][5] = {
        {-1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1}, {-1, -1, 30, -1, -1}, {-1, -1, -1, -1, -1}, {-1, -1, -5, -1, -1},
    };
    const int inkOutline = ival(a.values, "ink");
    const int coloring = ival(a.values, "coloring");
    glowInto(a, 6, -(coloring - 50) * 2, -(coloring - 50) * 2);
    if (a.cancel)
        return;
    const QImage straight = a.src.convertToFormat(QImage::Format_ARGB32);
    const ConstPixels in(straight);
    const Pixels out(a.dst);
    const int threshold = inkOutline * 255 / 100;
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int r = 0, g = 0, b = 0;
            for (int v = std::max(0, y - 2); v < std::min(in.h, y + 3); ++v) {
                const QRgb* row = in.row(v);
                for (int u = std::max(0, x - 2); u < std::min(in.w, x + 3); ++u) {
                    const int w = conv[v - y + 2][u - x + 2];
                    r += qRed(row[u]) * w, g += qGreen(row[u]) * w, b += qBlue(row[u]) * w;
                }
            }
            const int i = intensity(qRgb(clamp255(r), clamp255(g), clamp255(b)));
            const QRgb top = i > threshold ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
            d[x] = qPremultiply(blendStraight(top, qUnpremultiply(d[x]), opDarken));
        }
    });
}

void renderOilPainting(const RenderArgs& a)
{
    // Paint.NET 5: tamaño del pincel (diámetro), granularidad (0..1) y forma del núcleo.
    const int brush = std::max(1, int(std::lround(ival(a.values, "brush") / 2.0)));
    const int levels = std::clamp(int(std::lround(3 + dval(a.values, "granularity") * 235)), 3, 255);
    const bool circle = ival(a.values, "kernel") == 0;
    const QImage straight = a.src.convertToFormat(QImage::Format_ARGB32);
    const ConstPixels in(straight);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<int> count(static_cast<size_t>(levels + 1));
        std::vector<unsigned> sr(static_cast<size_t>(levels + 1)), sg(static_cast<size_t>(levels + 1)), sb(static_cast<size_t>(levels + 1)), sa(static_cast<size_t>(levels + 1));
        const int top = std::max(0, y - brush), bottom = std::min(in.h, y + brush + 1);
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            std::fill(count.begin(), count.end(), 0);
            std::fill(sr.begin(), sr.end(), 0u);
            std::fill(sg.begin(), sg.end(), 0u);
            std::fill(sb.begin(), sb.end(), 0u);
            std::fill(sa.begin(), sa.end(), 0u);
            const int left = std::max(0, x - brush), right = std::min(in.w, x + brush + 1);
            for (int j = top; j < bottom; ++j) {
                const QRgb* row = in.row(j);
                for (int i = left; i < right; ++i) {
                    if (circle && (i - x) * (i - x) + (j - y) * (j - y) > brush * brush + brush)
                        continue;
                    const QRgb p = row[i];
                    const size_t k = size_t(intensity(p) * levels / 255);
                    ++count[k];
                    sr[k] += qRed(p), sg[k] += qGreen(p), sb[k] += qBlue(p), sa[k] += qAlpha(p);
                }
            }
            size_t best = 0;
            for (size_t k = 0; k < count.size(); ++k)
                if (count[k] > count[best])
                    best = k;
            const unsigned n = unsigned(std::max(1, count[best]));
            d[x] = qPremultiply(qRgba(int(sr[best] / n), int(sg[best] / n), int(sb[best] / n), int(sa[best] / n)));
        }
    });
}

void renderPencilSketch(const RenderArgs& a)
{
    const int tip = std::max(1, int(std::lround(dval(a.values, "tip"))));
    const int range = int(std::lround(dval(a.values, "range")));
    const QImage blurred = tentBlur(a.src, a.roi, tip, a.cancel);
    if (blurred.isNull())
        return;
    const BrightnessContrast bc(range, -range);
    const ConstPixels in(a.src), bl(blurred);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        const QRgb* b = bl.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            QRgb t = bc.apply(qUnpremultiply(b[x - a.roi.left()]));
            t = qRgba(255 - qRed(t), 255 - qGreen(t), 255 - qBlue(t), qAlpha(t));
            const int ti = intensity(t);
            t = qRgba(ti, ti, ti, qAlpha(t));
            const QRgb su = qUnpremultiply(s[x]);
            const int si = intensity(su);
            d[x] = qPremultiply(blendStraight(qRgba(si, si, si, qAlpha(su)), t, opColorDodge));
        }
    });
}

} // namespace

void registerArtistic(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"inksketch", QObject::tr("Boceto a tinta"), catArtistic(), "draw-calligraphic", {},
         {P::integer("ink", QObject::tr("Contorno en tinta"), 0, 99, 50),
          P::integer("coloring", QObject::tr("Color"), 0, 100, 50)},
         renderInkSketch});
    add({"oilpainting", QObject::tr("Pintura al óleo"), catArtistic(), "draw-brush", {},
         {P::integer("brush", QObject::tr("Tamaño del pincel"), 1, 40, 10),
          P::real("granularity", QObject::tr("Granularidad"), 0, 1, 0.2, 2),
          P::choice("kernel", QObject::tr("Forma del Kernel"), {QObject::tr("Círculo"), QObject::tr("Cuadrado")}, 0)},
         renderOilPainting});
    add({"pencilsketch", QObject::tr("Boceto a lápiz"), catArtistic(), "draw-freehand", {},
         {P::real("tip", QObject::tr("Espesor del lápiz"), 1, 20, 2, 2),
          P::real("range", QObject::tr("Intervalo"), -20, 20, 0, 1)},
         renderPencilSketch});
}

} // namespace fx
