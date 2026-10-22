// Efectos > Renderizar (algoritmos de Paint.NET 3.36 y Turbulencia de 4.1)

#include "core/Layer.h"
#include "core/Raster.h"
#include "core/effects/EffectUtils.h"
#include "core/effects/RenderBlend.h"

namespace fx {

namespace {

// Modo de fusión de los efectos de Representación de Paint.NET 5 (la lista larga).
EffectParam renderBlendParam(int def)
{
    return EffectParam::choice(QStringLiteral("blend"), QObject::tr("Modo de fusión"), RenderBlend::names(), def);
}

// Compone 'rendered' (región roi) sobre la capa con el modo elegido.
void blendResult(const RenderArgs& a, const QImage& rendered)
{
    QImage base = a.src.copy(a.roi);
    RenderBlend::compose(base, rendered, ival(a.values, "blend"));
    Raster::blit(a.dst, base, a.roi.topLeft());
}

// --- Nubes -----------------------------------------------------------------------

const int kPerm[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142,
    8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165,
    71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,
    55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,
    18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250,
    124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189,
    28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,
    129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,
    242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,
    181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114,
    67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180,
};

inline int perm(int i) { return kPerm[i & 255]; }
inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline double grad(int hash, double x, double y)
{
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14) ? x : 0;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double cloudNoise(uint8_t ix, uint8_t iy, double x, double y, uint8_t seed)
{
    const double u = fade(x), v = fade(y);
    const int a = perm(ix + seed) + iy;
    const int aa = perm(a), ab = perm(a + 1);
    const int b = perm(ix + 1 + seed) + iy;
    const int ba = perm(b), bb = perm(b + 1);
    const double e1 = grad(perm(aa), x, y) + u * (grad(perm(ba), x - 1, y) - grad(perm(aa), x, y));
    const double e2 = grad(perm(ab), x, y - 1) + u * (grad(perm(bb), x - 1, y - 1) - grad(perm(ab), x, y - 1));
    return e1 + v * (e2 - e1);
}

void renderClouds(const RenderArgs& a)
{
    const int scale = ival(a.values, "scale");
    const double power = dval(a.values, "power");
    const uint8_t seed = uint8_t(ival(a.values, "seed"));
    const QRgb from = cval(a.values, "color1").rgba();
    const QRgb to = cval(a.values, "color2").rgba();
    const int w = a.src.width(), h = a.src.height();
    QImage clouds(a.roi.size(), kPremulFormat);
    const Pixels out(clouds);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y - a.roi.top());
        const int dy = 2 * y - h;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int dx = 2 * x - w;
            double val = 0, mult = 1;
            int div = scale;
            for (int i = 0; i < 12 && mult > 0.03 && div > 0; ++i) {
                double dxr = 65536 + double(dx) / div;
                double dyr = 65536 + double(dy) / div;
                const int dxd = int(dxr), dyd = int(dyr);
                dxr -= dxd;
                dyr -= dyd;
                val += cloudNoise(uint8_t(dxd), uint8_t(dyd), dxr, dyr, uint8_t(seed ^ i)) * mult;
                div /= 2;
                mult *= power;
            }
            d[x - a.roi.left()] = qPremultiply(lerpStraight(from, to, (val + 1) / 2));
        }
    });
    if (!a.cancel)
        blendResult(a, clouds);
}

// --- Fractales -----------------------------------------------------------------

void renderJulia(const RenderArgs& a)
{
    const double zoom = dval(a.values, "zoom");
    const int quality = ival(a.values, "quality");
    const double angleTheta = dval(a.values, "angle") * M_PI * 2 / 360.0;
    const double factor = dval(a.values, "factor");
    const double jr = 0.3125, ji = 0.03;
    const double log10000 = std::log(10000.0);
    const int w = a.src.width(), h = a.src.height();
    const double invH = 1.0 / h, invZoom = 1.0 / zoom, invQuality = 1.0 / quality;
    const double aspect = double(h) / w;
    const int count = quality * quality + 1;
    const double invCount = 1.0 / count;
    QImage fractal(a.roi.size(), kPremulFormat);
    const Pixels out(fractal);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y - a.roi.top());
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int r = 0, g = 0, b = 0, al = 0;
            for (int i = 0; i < count; ++i) {
                const double u = (2.0 * x - w + i * invCount) * invH;
                const double v = (2.0 * y - h + std::fmod(i * invQuality, 1.0)) * invH;
                const double radius = std::sqrt(u * u + v * v);
                const double theta = std::atan2(v, u) + angleTheta;
                const double up = radius * std::cos(theta), vp = radius * std::sin(theta);
                double jx = (up - vp * aspect) * invZoom, jy = (vp + up * aspect) * invZoom;
                double c = 0;
                while (c < 256 && jx * jx + jy * jy < 10000) {
                    const double t = jx;
                    jx = jx * jx - jy * jy + jr;
                    jy = 2 * t * jy + ji;
                    ++c;
                }
                c -= 2 - 2 * log10000 / std::log(jx * jx + jy * jy);
                c *= factor;
                b += clamp255(int(c - 768)), g += clamp255(int(c - 512)), r += clamp255(int(c - 256)), al += clamp255(int(c));
            }
            d[x - a.roi.left()] = qPremultiply(qRgba(r / count, g / count, b / count, al / count));
        }
    });
    if (!a.cancel)
        blendResult(a, fractal);
}

void renderMandelbrot(const RenderArgs& a)
{
    const double zoom = 1 + 20.0 * dval(a.values, "zoom");
    const double factor = dval(a.values, "factor");
    const int quality = ival(a.values, "quality");
    const double angleTheta = dval(a.values, "angle") * 2 * M_PI / 360.0;
    const bool invert = bval(a.values, "invert");
    const double maxV = 100000, invLogMax = 1.0 / std::log(maxV);
    const double xOffset = -0.7, yOffset = -0.29;
    const int w = a.src.width(), h = a.src.height();
    const double invH = 1.0 / h, invZoom = 1.0 / zoom, invQuality = 1.0 / quality;
    const int count = quality * quality + 1;
    const double invCount = 1.0 / count;
    QImage fractal(a.roi.size(), kPremulFormat);
    const Pixels out(fractal);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y - a.roi.top());
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int r = 0, g = 0, b = 0, al = 0;
            for (int i = 0; i < count; ++i) {
                const double u = (2.0 * x - w + i * invCount) * invH;
                const double v = (2.0 * y - h + std::fmod(i * invQuality, 1.0)) * invH;
                const double radius = std::sqrt(u * u + v * v);
                const double theta = std::atan2(v, u) + angleTheta;
                const double cr = radius * std::cos(theta) * invZoom + xOffset;
                const double ci = radius * std::sin(theta) * invZoom + yOffset;
                int it = 0;
                double zx = 0, zy = 0;
                while (it * factor < 1024 && zx * zx + zy * zy < maxV) {
                    const double t = zx;
                    zx = zx * zx - zy * zy + cr;
                    zy = 2 * t * zy + ci;
                    ++it;
                }
                const double m = it - std::log(zy * zy + zx * zx) * invLogMax;
                const double c = 64 + factor * m;
                r += clamp255(int(c - 768)), g += clamp255(int(c - 512)), b += clamp255(int(c - 256)), al += clamp255(int(c));
            }
            int rr = r / count, gg = g / count, bb = b / count;
            if (invert)
                rr = 255 - rr, gg = 255 - gg, bb = 255 - bb;
            d[x - a.roi.left()] = qPremultiply(qRgba(rr, gg, bb, al / count));
        }
    });
    if (!a.cancel)
        blendResult(a, fractal);
}

// --- Turbulencia -----------------------------------------------------------------

void renderTurbulence(const RenderArgs& a)
{
    const int octaves = ival(a.values, "octaves");
    const double period = dval(a.values, "period");
    const double roughness = 0.5;
    const double size = std::max(1, ival(a.values, "size"));
    const bool fractalSum = ival(a.values, "noise") == 0;
    const uint8_t seed = uint8_t(ival(a.values, "seed"));
    QImage result(a.roi.size(), kPremulFormat);
    const Pixels out(result);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y - a.roi.top());
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            int ch[3];
            for (int c = 0; c < 3; ++c) {
                double sum = 0, amp = 1, freq = 1 / period, norm = 0;
                for (int o = 0; o < octaves; ++o) {
                    const double n = pdnPerlin(std::fmod(x, size) * freq + c * 137.1, std::fmod(y, size) * freq - c * 71.7, 1, 1,
                                               uint8_t(seed + c * 31));
                    sum += (fractalSum ? n : std::abs(n)) * amp;
                    norm += amp;
                    amp *= roughness;
                    freq *= 2;
                }
                sum /= std::max(1e-9, norm);
                const double v = fractalSum ? (sum + 1) / 2 : sum * 1.8;
                ch[c] = clamp255f(float(v * 255));
            }
            d[x - a.roi.left()] = qRgb(ch[0], ch[1], ch[2]);
        }
    });
    if (!a.cancel)
        blendResult(a, result);
}

} // namespace

void registerRender(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    {
        QList<P> params{P::integer("scale", QObject::tr("Escala"), 2, 1000, 250),
                        P::real("power", QObject::tr("Aspereza"), 0, 1, 0.5, 2), renderBlendParam(RenderBlend::Over),
                        P::seed(), P::color("color1", QObject::tr("Color 1"), P::ColorDefault::Primary),
                        P::color("color2", QObject::tr("Color 2"), P::ColorDefault::Secondary)};
        for (int i = 0; i < params.size(); ++i)
            params[i].tab = i < 4 ? QObject::tr("Nubes") : QObject::tr("Colores");
        add({"clouds", QObject::tr("Nubes"), catRender(), "weather-many-clouds", {}, params, renderClouds});
    }
    add({"julia", QObject::tr("Fractal Julia"), catRender(), {}, {},
         {P::real("factor", QObject::tr("Factor"), 1, 10, 4, 2), P::real("zoom", QObject::tr("Zum"), 0.1, 50, 1, 2),
          P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0), qualityParam(2), renderBlendParam(RenderBlend::Overwrite)},
         renderJulia});
    add({"mandelbrot", QObject::tr("Fractal Mandelbrot"), catRender(), {}, {},
         {P::real("factor", QObject::tr("Factor"), 1, 10, 1, 2), P::real("zoom", QObject::tr("Zum"), 0, 100, 10, 2),
          P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0), qualityParam(2),
          P::boolean("invert", QObject::tr("Invertir colores"), false), renderBlendParam(RenderBlend::Overwrite)},
         renderMandelbrot});
    add({"turbulence", QObject::tr("Turbulencia"), catRender(), {}, {},
         {P::integer("octaves", QObject::tr("Octavas"), 1, 12, 4), P::real("period", QObject::tr("Período"), 2, 1000, 100, 2),
          P::integer("size", QObject::tr("Tamaño"), 16, 4096, 4096),
          P::choice("noise", QObject::tr("Ruido"), {QObject::tr("Suma Fractal"), QObject::tr("Turbulencia")}, 1),
          P::seed(), renderBlendParam(RenderBlend::Over)},
         renderTurbulence});
}

} // namespace fx
