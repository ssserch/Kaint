// Menú Ajustes: correcciones de color que no mueven píxeles.

#include "core/effects/EffectUtils.h"

namespace fx {

namespace {

using Lut = std::array<uint8_t, 256>;

void applyLuts(const RenderArgs& a, const Lut& lr, const Lut& lg, const Lut& lb)
{
    mapStraight(a, [&](QRgb c) { return qRgba(lr[qRed(c)], lg[qGreen(c)], lb[qBlue(c)], qAlpha(c)); });
}

// --- HSV de Paint.NET en coma flotante (h 0..360, s y v 0..1) ---------------

inline void rgbToHsv(int r, int g, int b, double& h, double& s, double& v)
{
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf});
    const double delta = mx - mn;
    v = mx;
    s = mx == 0 ? 0 : delta / mx;
    if (delta == 0) {
        h = 0;
        return;
    }
    if (rf == mx)
        h = (gf - bf) / delta;
    else if (gf == mx)
        h = 2 + (bf - rf) / delta;
    else
        h = 4 + (rf - gf) / delta;
    h *= 60;
    if (h < 0)
        h += 360;
}

inline QRgb hsvToRgb(double h, double s, double v, int alpha)
{
    if (s == 0) {
        const int c = clamp255f(float(v * 255));
        return qRgba(c, c, c, alpha);
    }
    h = std::fmod(h, 360.0) / 60.0;
    const int i = int(std::floor(h));
    const double f = h - i;
    const double p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    double r, g, b;
    switch (i) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return qRgba(clamp255f(float(r * 255)), clamp255f(float(g * 255)), clamp255f(float(b * 255)), alpha);
}

// --- Ajustes --------------------------------------------------------------------

void renderBlackWhite(const RenderArgs& a)
{
    mapStraight(a, [](QRgb c) {
        const int i = intensity(c);
        return qRgba(i, i, i, qAlpha(c));
    });
}

void renderBrightnessContrast(const RenderArgs& a)
{
    const BrightnessContrast bc(ival(a.values, "brightness"), ival(a.values, "contrast"));
    mapStraight(a, [&](QRgb c) { return bc.apply(c); });
}

void renderCurves(const RenderArgs& a)
{
    const CurvesValue cv = a.values.value(QStringLiteral("curves")).value<CurvesValue>();
    if (cv.mode == CurvesValue::Luminosity) {
        const Lut lum = cv.lut(0);
        mapStraight(a, [&](QRgb c) {
            const int i = intensity(c);
            const int diff = lum[size_t(i)] - i;
            return qRgba(clamp255(qRed(c) + diff), clamp255(qGreen(c) + diff), clamp255(qBlue(c) + diff), qAlpha(c));
        });
        return;
    }
    applyLuts(a, cv.lut(1), cv.lut(2), cv.lut(3));
}

void renderExposure(const RenderArgs& a)
{
    const double factor = std::pow(2.0, dval(a.values, "exposure"));
    Lut lut;
    for (int i = 0; i < 256; ++i)
        lut[size_t(i)] = uint8_t(toSrgb(toLinear(i) * factor));
    applyLuts(a, lut, lut, lut);
}

std::any prepareLocalLuminance(const QImage& src, const QRect& roi, const ParamValues& v, const EffectContext&)
{
    // Luminosidad media del entorno de cada píxel. El radio va en % del lado mayor de la zona
    // (1,25 por defecto, como Paint.NET).
    std::atomic_bool never{false};
    const int px = std::max(1, int(std::lround(dval(v, "radius") / 100.0 * std::max(roi.width(), roi.height()))));
    return tentBlur(src, roi, px, never);
}

void renderHighlightsShadows(const RenderArgs& a)
{
    const QImage blurred = std::any_cast<QImage>(a.state);
    const double shadows = ival(a.values, "shadows") / 100.0;
    const double highlights = ival(a.values, "highlights") / 100.0;
    const double clarity = ival(a.values, "clarity") / 100.0;
    const ConstPixels in(a.src), bl(blurred);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        const QRgb* b = bl.row(y - a.roi.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            if (qAlpha(s[x]) == 0) {
                d[x] = 0;
                continue;
            }
            const QRgb u = qUnpremultiply(s[x]);
            const double local = intensityF(qUnpremultiply(b[x - a.roi.left()]));
            const double localLin = toLinear(int(std::lround(local * 255)));
            const double wShadow = std::pow(1.0 - local, 2.0);
            const double wHighlight = std::pow(local, 2.0);
            const double wMid = 4.0 * local * (1.0 - local);
            auto ch = [&](int c) {
                double l = toLinear(c);
                if (shadows > 0)
                    l += (1 - l) * shadows * 1.5 * wShadow * (1 - l);
                else
                    l *= 1.0 + shadows * wShadow;
                if (highlights > 0)
                    l += (1 - l) * highlights * wHighlight;
                else
                    l *= 1.0 + highlights * 0.75 * wHighlight;
                l += clarity * (l - localLin) * wMid;
                return toSrgb(std::clamp(l, 0.0, 1.0));
            };
            d[x] = qPremultiply(qRgba(ch(qRed(u)), ch(qGreen(u)), ch(qBlue(u)), qAlpha(u)));
        }
    });
}

void renderHueSaturation(const RenderArgs& a)
{
    // HueSaturationLightness de Paint.NET.
    const int hueDelta = ival(a.values, "hue");
    const int satFactor = ival(a.values, "saturation") * 1024 / 100;
    const int lightness = ival(a.values, "lightness");
    const int blendAlpha = std::abs(lightness) * 255 / 100;
    const int blendTo = lightness > 0 ? 255 : 0;
    mapStraight(a, [&](QRgb c) {
        const int i = intensity(c);
        const int r = clamp255((i * 1024 + (qRed(c) - i) * satFactor) >> 10);
        const int g = clamp255((i * 1024 + (qGreen(c) - i) * satFactor) >> 10);
        const int b = clamp255((i * 1024 + (qBlue(c) - i) * satFactor) >> 10);
        double h, s, v;
        rgbToHsv(r, g, b, h, s, v);
        h = std::fmod(h + hueDelta + 720.0, 360.0);
        QRgb out = hsvToRgb(h, s, v, qAlpha(c));
        if (lightness != 0) {
            auto mix = [&](int x) { return x + ((blendTo - x) * blendAlpha + 127) / 255; };
            out = qRgba(mix(qRed(out)), mix(qGreen(out)), mix(qBlue(out)), qAlpha(c));
        }
        return out;
    });
}

void renderInvertAlpha(const RenderArgs& a)
{
    mapStraight(a, [](QRgb c) { return qRgba(qRed(c), qGreen(c), qBlue(c), 255 - qAlpha(c)); });
}

void renderInvert(const RenderArgs& a)
{
    mapStraight(a, [](QRgb c) { return qRgba(255 - qRed(c), 255 - qGreen(c), 255 - qBlue(c), qAlpha(c)); });
}

void renderLevels(const RenderArgs& a)
{
    const LevelsValue lv = a.values.value(QStringLiteral("levels")).value<LevelsValue>();
    applyLuts(a, lv.lut(0), lv.lut(1), lv.lut(2));
}

std::any prepareAutoLevel(const QImage& src, const QRect& roi, const ParamValues&, const EffectContext&)
{
    return LevelsValue::automatic(src, roi);
}

void renderAutoLevel(const RenderArgs& a)
{
    const LevelsValue lv = std::any_cast<LevelsValue>(a.state);
    applyLuts(a, lv.lut(0), lv.lut(1), lv.lut(2));
}

Lut posterizeLevels(int count)
{
    // CalcLevels de Paint.NET.
    count = std::clamp(count, 2, 64);
    std::vector<uint8_t> t1(size_t(count), 0);
    for (int i = 1; i < count; ++i)
        t1[size_t(i)] = uint8_t((255 * i) / (count - 1));
    Lut levels{};
    int j = 0, k = 0;
    for (int i = 0; i < 256; ++i) {
        levels[size_t(i)] = t1[size_t(std::min(j, count - 1))];
        k += count;
        if (k > 255) {
            k -= 255;
            ++j;
        }
    }
    return levels;
}

void renderPosterize(const RenderArgs& a)
{
    // Cada canal solo si su casilla está marcada (también el alfa, como Paint.NET 5).
    auto lut = [&](const char* key, const char* on) {
        if (!a.values.value(QLatin1String(on), true).toBool()) {
            Lut identity;
            for (int i = 0; i < 256; ++i)
                identity[size_t(i)] = uint8_t(i);
            return identity;
        }
        return posterizeLevels(ival(a.values, key));
    };
    const Lut r = lut("red", "redOn"), g = lut("green", "greenOn"), b = lut("blue", "blueOn"), al = lut("alpha", "alphaOn");
    mapStraight(a, [&](QRgb c) {
        return qRgba(r[size_t(qRed(c))], g[size_t(qGreen(c))], b[size_t(qBlue(c))], al[size_t(qAlpha(c))]);
    });
}

void renderSepia(const RenderArgs& a)
{
    // Desaturar + Niveles con gamma (R 0,8 / G 1,0 / B 1,2), como Paint.NET.
    const double k = ival(a.values, "intensity") / 50.0;
    LevelsValue lv;
    lv.gamma = {std::pow(0.8, std::max(1.0, k)), 1.0, std::pow(1.2, std::max(1.0, k))};
    const Lut lr = lv.lut(0), lg = lv.lut(1), lb = lv.lut(2);
    mapStraight(a, [&](QRgb c) {
        const int i = intensity(c);
        const QRgb sepia = qRgba(lr[size_t(i)], lg[size_t(i)], lb[size_t(i)], qAlpha(c));
        return k >= 1 ? sepia : lerpStraight(c, sepia, k);
    });
}

void renderTemperatureTint(const RenderArgs& a)
{
    const double t = ival(a.values, "temperature") / 100.0;
    const double tint = ival(a.values, "tint") / 100.0;
    const double gr = 1.0 + 0.35 * t + 0.10 * tint;
    const double gg = 1.0 - 0.25 * tint;
    const double gb = 1.0 - 0.35 * t + 0.10 * tint;
    Lut lr, lg, lb;
    for (int i = 0; i < 256; ++i) {
        const double l = toLinear(i);
        lr[size_t(i)] = uint8_t(toSrgb(l * gr));
        lg[size_t(i)] = uint8_t(toSrgb(l * gg));
        lb[size_t(i)] = uint8_t(toSrgb(l * gb));
    }
    applyLuts(a, lr, lg, lb);
}

} // namespace

void registerAdjustments(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def, bool straight = true) {
        def.straight = straight;
        list.append(new SimpleEffect(std::move(def)));
    };
    auto key = [](const char* s) { return QKeySequence(QString::fromLatin1(s)); };

    add({"autolevel", QObject::tr("Nivel automático"), {}, "adjustlevels", key("Ctrl+Shift+L"), {}, renderAutoLevel,
         prepareAutoLevel});
    add({"blackwhite", QObject::tr("Blanco y negro"), {}, "color-mode-black-white", key("Ctrl+Shift+G"), {},
         renderBlackWhite});
    add({"brightness", QObject::tr("Brillo y contraste"), {}, "adjustrgb", key("Ctrl+Shift+T"),
         {P::integer("brightness", QObject::tr("Brillo"), -100, 100, 0),
          P::integer("contrast", QObject::tr("Contraste"), -100, 100, 0)},
         renderBrightnessContrast});
    add({"curves", QObject::tr("Curvas"), {}, "adjustcurves", key("Ctrl+Shift+M"), {P::curves("curves")}, renderCurves});
    add({"exposure", QObject::tr("Exposición"), {}, "brightness-high", {},
         {P::real("exposure", QString(), -5, 5, 0, 2)}, renderExposure});
    add({"highlightsshadows", QObject::tr("Iluminaciones y sombras"), {}, "contrast", {},
         {P::integer("highlights", QObject::tr("Iluminaciones"), -100, 100, 0),
          P::integer("shadows", QObject::tr("Sombras"), -100, 100, 0),
          P::integer("clarity", QObject::tr("Claridad"), -100, 100, 0),
          P::real("radius", QObject::tr("Radio"), 0.25, 20, 1.25, 2)},
         renderHighlightsShadows, prepareLocalLuminance},
        false);
    {
        P hue = P::integer("hue", QObject::tr("Matiz"), -180, 180, 0);
        hue.gradient = {QColor(0, 255, 255), QColor(0, 0, 255), QColor(255, 0, 255), QColor(255, 0, 0),
                        QColor(255, 255, 0), QColor(0, 255, 0), QColor(0, 255, 255)};
        P sat = P::integer("saturation", QObject::tr("Saturación"), 0, 200, 100);
        sat.gradient = {QColor(128, 128, 128), QColor(255, 40, 40), QColor(255, 0, 0)};
        P light = P::integer("lightness", QObject::tr("Claridad"), -100, 100, 0);
        light.gradient = {QColor(0, 0, 0), QColor(128, 128, 128), QColor(255, 255, 255)};
        add({"huesat", QObject::tr("Matiz y saturación"), {}, "adjusthsl", key("Ctrl+Shift+U"), {hue, sat, light},
             renderHueSaturation});
    }
    add({"invertalpha", QObject::tr("Invertir alfa"), {}, "edit-select-invert", key("Ctrl+Alt+I"), {},
         renderInvertAlpha});
    add({"invert", QObject::tr("Invertir colores"), {}, "color-mode-invert-image", key("Ctrl+Shift+I"), {},
         renderInvert});
    add({"levels", QObject::tr("Niveles"), {}, "adjustlevels", key("Ctrl+L"), {P::levels("levels")}, renderLevels});
    {
        auto channel = [](const char* key, const char* on, const QString& label) {
            P p = P::integer(key, label, 2, 64, 16).linked("linked");
            p.enableKey = QString::fromLatin1(on);
            return p;
        };
        add({"posterize", QObject::tr("Posterización"), {}, "colorfx", key("Ctrl+Shift+P"),
             {channel("red", "redOn", QObject::tr("Rojo")), channel("green", "greenOn", QObject::tr("Verde")),
              channel("blue", "blueOn", QObject::tr("Azul")), channel("alpha", "alphaOn", QObject::tr("Alfa")),
              P::boolean("redOn", QObject::tr("Rojo"), true), P::boolean("greenOn", QObject::tr("Verde"), true),
              P::boolean("blueOn", QObject::tr("Azul"), true), P::boolean("alphaOn", QObject::tr("Alfa"), true),
              P::boolean("linked", QObject::tr("Vinculado"), true)},
             renderPosterize});
    }
    add({"sepia", QObject::tr("Sepia"), {}, "colorfx", key("Ctrl+Shift+E"),
         {P::integer("intensity", QObject::tr("Intensidad"), 0, 100, 50)}, renderSepia});
    {
        P temperature = P::integer("temperature", QObject::tr("Temperatura"), -100, 100, 0);
        temperature.gradient = {QColor(60, 130, 255), QColor(235, 235, 235), QColor(255, 140, 30)};
        P tint = P::integer("tint", QObject::tr("Tinte"), -100, 100, 0);
        tint.gradient = {QColor(240, 110, 220), QColor(235, 235, 235), QColor(100, 210, 100)};
        add({"temperature", QObject::tr("Temperatura y tinte"), {}, "color-management", {}, {temperature, tint},
             renderTemperatureTint});
    }
}

} // namespace fx
