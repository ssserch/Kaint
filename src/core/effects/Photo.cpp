// Efectos > Foto y Efectos > Objeto

#include "core/Raster.h"
#include "core/Resample.h"
#include "core/effects/EffectUtils.h"

#include <cstring>

namespace fx {

namespace {

void renderGlow(const RenderArgs& a)
{
    const QImage blurred = tentBlur(a.src, a.roi, std::max(1, int(std::lround(dval(a.values, "radius")))), a.cancel);
    if (blurred.isNull())
        return;
    const BrightnessContrast bc(ival(a.values, "brightness"), ival(a.values, "contrast"));
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

void renderRedEye(const RenderArgs& a)
{
    // UnaryPixelOps.RedEyeRemove de Paint.NET. Paint.NET 5 tiene un solo control, "Fuerza": 3
    // equivale a la tolerancia 70 y la saturación 90 %.
    const double force = dval(a.values, "force");
    const int tolerance = int(std::lround(100 - 10 * force));
    const double strength = std::clamp(1.2 - 0.1 * force, 0.0, 1.0);
    mapStraight(a, [&](QRgb c) {
        const int r = qRed(c), g = qGreen(c), b = qBlue(c);
        const int mx = std::max({r, g, b}), mn = std::min({r, g, b});
        const int saturation = (mx == 0 || mx == mn) ? 0 : int((mx - mn) * 255.0 / mx);
        const int difference = r - std::max(b, g);
        if (difference > tolerance && saturation > 100) {
            const double i = 255.0 * intensityF(c);
            return qRgba(int(i * strength), g, b, qAlpha(c));
        }
        return c;
    });
}

void renderSharpen(const RenderArgs& a)
{
    // SharpenEffect de Paint.NET: se aleja de la mediana local. Con Umbral solo se refuerzan
    // las diferencias mayores (no el ruido de zonas lisas).
    const int threshold = int(std::lround(dval(a.values, "threshold") * 2.55));
    localHistogram(a, std::max(1, int(std::lround(dval(a.values, "amount")))), [threshold](QRgb c, const LocalHistogram& h) {
        const QRgb median = histogramPercentile(h, 50);
        if (threshold > 0) {
            const int diff = std::max({std::abs(qRed(c) - qRed(median)), std::abs(qGreen(c) - qGreen(median)),
                                       std::abs(qBlue(c) - qBlue(median))});
            if (diff < threshold)
                return c;
        }
        return lerpStraight(c, median, -0.5);
    });
}

void renderSoftenPortrait(const RenderArgs& a)
{
    const double softness = dval(a.values, "softness");
    const int lighting = ival(a.values, "lighting");
    const int warmth = ival(a.values, "warmth");
    const QImage blurred = tentBlur(a.src, a.roi, int(std::lround(softness * 3)), a.cancel);
    if (blurred.isNull())
        return;
    const BrightnessContrast bc(lighting, -lighting / 2);
    const double redAdjust = 1.0 + warmth / 100.0, blueAdjust = 1.0 - warmth / 100.0;
    const ConstPixels in(a.src), bl(blurred);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        const QRgb* b = bl.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const QRgb su = qUnpremultiply(s[x]);
            const int i = intensity(su);
            const QRgb grey = qRgba(clamp255(int(i * redAdjust)), i, clamp255(int(i * blueAdjust)), qAlpha(su));
            const QRgb top = bc.apply(qUnpremultiply(b[x - a.roi.left()]));
            d[x] = qPremultiply(blendStraight(grey, top, opOverlay));
        }
    });
}

void renderStraighten(const RenderArgs& a)
{
    // Gira alrededor del centro y amplía lo justo para cubrir toda la selección.
    const double angle = dval(a.values, "angle");
    const double th = std::abs(angle) * M_PI / 180.0;
    const double w = a.roi.width(), h = a.roi.height();
    const double s = std::max((w * std::cos(th) + h * std::sin(th)) / w, (w * std::sin(th) + h * std::cos(th)) / h);
    const QPointF c = QRectF(a.roi).center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(angle);
    t.scale(s, s);
    t.translate(-c.x(), -c.y());
    const Resample::Interpolation interp = ival(a.values, "sampling") == 0   ? Resample::Interpolation::Bicubic
                                         : ival(a.values, "sampling") == 1 ? Resample::Interpolation::Bilinear
                                                                            : Resample::Interpolation::NearestNeighbor;
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        std::memset(a.dst.scanLine(y) + a.roi.left() * 4, 0, size_t(a.roi.width()) * 4);
    const QImage region = a.src.copy(a.roi);
    Resample::drawTransformed(a.dst, region, QTransform::fromTranslate(a.roi.left(), a.roi.top()) * t, interp, a.roi);
    if (a.ctx.progress)
        a.ctx.progress->done = a.ctx.progress->total.load();
}

void renderVignette(const RenderArgs& a)
{
    // VignetteEffect de Paint.NET (en luz lineal).
    const QPointF off = pval(a.values, "offset");
    const double amount = dval(a.values, "amount");
    const double amount1 = 1.0 - amount;
    const double xc = a.roi.left() + a.roi.width() * (1 + off.x()) * 0.5;
    const double yc = a.roi.top() + a.roi.height() * (1 + off.y()) * 0.5;
    double radius = std::max(a.roi.width(), a.roi.height()) * 0.5 * dval(a.values, "radius");
    radius *= radius;
    const double radiusR = M_PI / (8 * radius);
    std::array<double, 256> lin;
    for (int i = 0; i < 256; ++i)
        lin[size_t(i)] = toLinear(i);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const double iy2 = (y - yc) * (y - yc);
        const QRgb* s = in.row(y);
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double ix = x - xc;
            const double dd = (iy2 + ix * ix) * radiusR;
            double factor = std::cos(dd);
            if (factor <= 0 || dd > M_PI) {
                factor = amount1;
            } else {
                factor *= factor;
                factor *= factor;
                factor = amount1 + amount * factor;
            }
            const QRgb u = qUnpremultiply(s[x]);
            d[x] = qPremultiply(qRgba(toSrgb(lin[size_t(qRed(u))] * factor), toSrgb(lin[size_t(qGreen(u))] * factor),
                                      toSrgb(lin[size_t(qBlue(u))] * factor), qAlpha(u)));
        }
    });
}

void renderDropShadow(const RenderArgs& a)
{
    // Sombra: el alfa del objeto desplazado, desenfocado y teñido, debajo del objeto.
    const int blur = int(std::lround(dval(a.values, "radius")));
    const double distanceF = dval(a.values, "distance");
    const int distance = int(std::ceil(distanceF));
    const double ang = -dval(a.values, "angle") * M_PI / 180.0;
    const QPoint offset(int(std::lround(std::cos(ang) * distanceF)), int(std::lround(std::sin(ang) * distanceF)));
    const QColor color = cval(a.values, "color");
    const int opacity = int(std::lround(dval(a.values, "opacity") * 255));
    const bool onlyShadow = bval(a.values, "only");

    // Silueta (alfa) desplazada sobre la región necesaria.
    const int margin = tentMargin(blur) + distance + 1;
    const QRect big = a.roi.adjusted(-margin, -margin, margin, margin).intersected(a.src.rect());
    QImage silhouette(big.size(), QImage::Format_ARGB32_Premultiplied);
    silhouette.fill(Qt::transparent);
    const ConstPixels in(a.src);
    for (int y = 0; y < big.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(silhouette.scanLine(y));
        const int sy = big.top() + y - offset.y();
        if (sy < 0 || sy >= in.h)
            continue;
        for (int x = 0; x < big.width(); ++x) {
            const int sx = big.left() + x - offset.x();
            if (sx >= 0 && sx < in.w) {
                const int al = qAlpha(in.at(sx, sy));
                row[x] = qRgba(al, al, al, al);
            }
        }
    }
    const QImage blurred = tentBlur(silhouette, QRect(a.roi.topLeft() - big.topLeft(), a.roi.size()), blur, a.cancel);
    if (blurred.isNull())
        return;
    const ConstPixels sh(blurred);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        const QRgb* b = sh.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int al = qAlpha(b[x - a.roi.left()]) * opacity / 255 * color.alpha() / 255;
            const QRgb shadow = qPremultiply(qRgba(color.red(), color.green(), color.blue(), al));
            d[x] = onlyShadow ? shadow : overPixel(shadow, s[x]);
        }
    });
}

} // namespace

void registerPhoto(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"glow", QObject::tr("Resplandor"), catPhoto(), {}, {},
         {P::real("radius", QObject::tr("Radio"), 1, 20, 6, 1),
          P::integer("brightness", QObject::tr("Brillo"), -100, 100, 10),
          P::integer("contrast", QObject::tr("Contraste"), -100, 100, 10)},
         renderGlow});
    {
        SimpleEffect::Def redeye{"redeye", QObject::tr("Eliminar ojos rojos"), catPhoto(), {}, {},
                                 {P::integer("force", QObject::tr("Fuerza"), 1, 5, 3)}, renderRedEye};
        redeye.note = QObject::tr("Para obtener mejores resultados, utilice primero las herramientas de selección para "
                                  "seleccionar cada ojo.");
        add(redeye);
    }
    add({"sharpen", QObject::tr("Nítido"), catPhoto(), {}, {},
         {P::real("amount", QObject::tr("Cantidad"), 1, 20, 2, 2), P::real("threshold", QObject::tr("Umbral"), 0, 100, 0, 2)},
         renderSharpen});
    add({"softenportrait", QObject::tr("Suavizado de retrato"), catPhoto(), {}, {},
         {P::real("softness", QObject::tr("Suavizado"), 0, 10, 5, 1), P::integer("lighting", QObject::tr("Iluminación"), -20, 20, 0),
          P::integer("warmth", QObject::tr("Temperatura"), 0, 20, 10)},
         renderSoftenPortrait});
    add({"straighten", QObject::tr("Enderezar"), catPhoto(), "transform-rotate", {},
         {P::angle("angle", QObject::tr("Ángulo"), -45, 45, 0),
          P::choice("sampling", QObject::tr("Muestreo"),
                    {QObject::tr("Bicúbico"), QObject::tr("Bilineal"), QObject::tr("Vecino más próximo")}, 0)},
         renderStraighten});
    add({"vignette", QObject::tr("Viñeta"), catPhoto(), {}, {},
         {P::offset("offset", QObject::tr("Centrar")), P::real("radius", QObject::tr("Radio"), 0.1, 4, 0.5, 2),
          P::real("amount", QObject::tr("Intensidad"), 0, 1, 1, 2)},
         renderVignette});
}

void registerObject(QList<const Effect*>& list)
{
    using P = EffectParam;
    SimpleEffect::Def def{"dropshadow", QObject::tr("Sombra paralela"), catObject(), {}, {},
                          {P::real("radius", QObject::tr("Radio de sombra"), 0, 100, 10, 1),
                           P::real("distance", QObject::tr("Distancia"), 0, 100, 10, 1),
                           P::angle("angle", QObject::tr("Ángulo"), -180, 180, -45),
                           P::real("opacity", QObject::tr("Opacidad"), 0, 1, 0.75, 2),
                           P::color("color", QObject::tr("Color"), P::ColorDefault::Black),
                           P::boolean("only", QObject::tr("Solo proyectar sombra"), false)},
                          renderDropShadow};
    def.note = QObject::tr("Este efecto requiere una capa con transparencia. Asegúrese de que el objeto para el que desea "
                           "una sombra paralela esté en su propia capa.");
    list.append(new SimpleEffect(std::move(def)));
}

} // namespace fx
