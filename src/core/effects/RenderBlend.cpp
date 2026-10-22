#include "core/effects/RenderBlend.h"

#include "core/Parallel.h"

#include <QObject>

#include <algorithm>
#include <cmath>

namespace RenderBlend {

QStringList names()
{
    // Tal cual los muestra Paint.NET 5.1 en español (incluido el segundo
    // "Superponer", que es Diferencia).
    return {QObject::tr("Sobre (Normal)"), QObject::tr("Por debajo"), QObject::tr("Sobrescribir"),
            QObject::tr("Multiplicar"), QObject::tr("Pantalla"), QObject::tr("Oscurecer"), QObject::tr("Aclarar"),
            QObject::tr("Disolver"), QObject::tr("Sobreexposición de color"), QObject::tr("Subexposición lineal"),
            QObject::tr("Color más oscuro"), QObject::tr("Color más claro"), QObject::tr("Subexposición de color"),
            QObject::tr("Sobreexposición lineal"), QObject::tr("Superponer"), QObject::tr("Luz tenue"),
            QObject::tr("Luz dura"), QObject::tr("Luz intensa"), QObject::tr("Luz lineal"), QObject::tr("Luz focal"),
            QObject::tr("Mezcla dura"), QObject::tr("Superponer"), QObject::tr("Exclusión"), QObject::tr("Matiz"),
            QObject::tr("Saturación"), QObject::tr("Color"), QObject::tr("Luminosidad"), QObject::tr("Restar"),
            QObject::tr("División"), QObject::tr("Adición"), QObject::tr("Reflejo"), QObject::tr("Brillo"),
            QObject::tr("Negativo"), QObject::tr("Mínimo"), QObject::tr("Máximo"), QObject::tr("En"),
            QObject::tr("Destino en"), QObject::tr("Afuera"), QObject::tr("Salida de destino"), QObject::tr("Encima"),
            QObject::tr("Destino en la cima"), QObject::tr("Más"), QObject::tr("Invertir máscara"),
            QObject::tr("Xor (8 bits)"), QObject::tr("Xor (16 bits)"), QObject::tr("Xor (compuesto)")};
}

namespace {

struct C3 {
    float r, g, b;
};

float lum(const C3& c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }

C3 clipColor(C3 c)
{
    const float l = lum(c);
    const float n = std::min({c.r, c.g, c.b});
    const float x = std::max({c.r, c.g, c.b});
    auto fix = [&](float v) {
        if (n < 0)
            v = l + (v - l) * l / std::max(1e-6f, l - n);
        if (x > 1)
            v = l + (v - l) * (1 - l) / std::max(1e-6f, x - l);
        return v;
    };
    return {fix(c.r), fix(c.g), fix(c.b)};
}

C3 setLum(C3 c, float l)
{
    const float d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}

float sat(const C3& c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }

C3 setSat(C3 c, float s)
{
    float* v[3] = {&c.r, &c.g, &c.b};
    std::sort(v, v + 3, [](float* a, float* b) { return *a < *b; });
    if (*v[2] > *v[0]) {
        *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]);
        *v[2] = s;
    } else {
        *v[1] = *v[2] = 0;
    }
    *v[0] = 0;
    return c;
}

float separable(int mode, float b, float s)
{
    switch (mode) {
    case Multiply: return b * s;
    case Screen: return b + s - b * s;
    case Darken:
    case Minimum: return std::min(b, s);
    case Lighten:
    case Maximum: return std::max(b, s);
    case ColorBurn: return b >= 1 ? 1 : (s <= 0 ? 0 : 1 - std::min(1.f, (1 - b) / s));
    case LinearBurn: return std::max(0.f, b + s - 1);
    case ColorDodge: return b <= 0 ? 0 : (s >= 1 ? 1 : std::min(1.f, b / (1 - s)));
    case LinearDodge:
    case Additive: return std::min(1.f, b + s);
    case Overlay: return b <= 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case SoftLight: {
        const float d = b <= 0.25f ? ((16 * b - 12) * b + 4) * b : std::sqrt(b);
        return s <= 0.5f ? b - (1 - 2 * s) * b * (1 - b) : b + (2 * s - 1) * (d - b);
    }
    case HardLight: return s <= 0.5f ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
    case VividLight:
        return s <= 0.5f ? (s <= 0 ? 0 : 1 - std::min(1.f, (1 - b) / (2 * s)))
                         : (s >= 1 ? 1 : std::min(1.f, b / (2 * (1 - s))));
    case LinearLight: return std::clamp(b + 2 * s - 1, 0.f, 1.f);
    case PinLight: return s <= 0.5f ? std::min(b, 2 * s) : std::max(b, 2 * s - 1);
    case HardMix: return b + s >= 1 ? 1 : 0;
    case Difference: return std::abs(b - s);
    case Exclusion: return b + s - 2 * b * s;
    case Subtract: return std::max(0.f, b - s);
    case Division: return s <= 0 ? (b > 0 ? 1 : 0) : std::min(1.f, b / s);
    case Reflect: return s >= 1 ? 1 : std::min(1.f, b * b / (1 - s));
    case Glow: return b >= 1 ? 1 : std::min(1.f, s * s / (1 - b));
    case Negation: return 1 - std::abs(1 - b - s);
    default: return s;
    }
}

// Ruido estable por píxel para Disolver.
float hashNoise(int x, int y)
{
    quint32 h = quint32(x) * 374761393u + quint32(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xffff) / 65535.f;
}

} // namespace

void compose(QImage& base, const QImage& top, int mode)
{
    const bool basePremul = base.format() == QImage::Format_ARGB32_Premultiplied;
    const int w = std::min(base.width(), top.width());
    const int h = std::min(base.height(), top.height());
    parallelRows(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            QRgb* d = reinterpret_cast<QRgb*>(base.scanLine(y));
            const QRgb* t = reinterpret_cast<const QRgb*>(top.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb bs = basePremul ? qUnpremultiply(d[x]) : d[x];
                const QRgb ts = qUnpremultiply(t[x]);
                const float ab = qAlpha(bs) / 255.f, as = qAlpha(ts) / 255.f;
                const C3 cb{qRed(bs) / 255.f, qGreen(bs) / 255.f, qBlue(bs) / 255.f};
                const C3 cs{qRed(ts) / 255.f, qGreen(ts) / 255.f, qBlue(ts) / 255.f};
                // Resultado premultiplicado (pr, pg, pb, ao).
                float ao = as + ab - as * ab;
                C3 p{0, 0, 0};
                auto composite = [&](const C3& mixed) {
                    // W3C: cs·(1−ab) + cb·(1−as) + as·ab·B(cb, cs).
                    p = {cs.r * as * (1 - ab) + cb.r * ab * (1 - as) + as * ab * mixed.r,
                         cs.g * as * (1 - ab) + cb.g * ab * (1 - as) + as * ab * mixed.g,
                         cs.b * as * (1 - ab) + cb.b * ab * (1 - as) + as * ab * mixed.b};
                };
                auto porterDuff = [&](float fs, float fb) {
                    ao = std::min(1.f, as * fs + ab * fb);
                    p = {cs.r * as * fs + cb.r * ab * fb, cs.g * as * fs + cb.g * ab * fb, cs.b * as * fs + cb.b * ab * fb};
                };
                switch (mode) {
                case Over: porterDuff(1, 1 - as); break;
                case Behind: porterDuff(1 - ab, 1); break;
                case Overwrite: porterDuff(1, 0); break;
                case Dissolve:
                    if (hashNoise(x, y) < as)
                        porterDuff(1 / std::max(as, 1e-6f), 0), ao = 1;
                    else
                        porterDuff(0, 1);
                    break;
                case DarkerColor: composite(lum(cs) < lum(cb) ? cs : cb); break;
                case LighterColor: composite(lum(cs) > lum(cb) ? cs : cb); break;
                case Hue: composite(setLum(setSat(cs, sat(cb)), lum(cb))); break;
                case Saturation: composite(setLum(setSat(cb, sat(cs)), lum(cb))); break;
                case Color: composite(setLum(cs, lum(cb))); break;
                case Luminosity: composite(setLum(cb, lum(cs))); break;
                case In: porterDuff(ab, 0); break;
                case DestinationIn: porterDuff(0, as); break;
                case Out: porterDuff(1 - ab, 0); break;
                case DestinationOut: porterDuff(0, 1 - as); break;
                case Atop: porterDuff(ab, 1 - as); break;
                case DestinationAtop: porterDuff(1 - ab, as); break;
                case Plus:
                    ao = std::min(1.f, as + ab);
                    p = {std::min(1.f, cs.r * as + cb.r * ab), std::min(1.f, cs.g * as + cb.g * ab),
                         std::min(1.f, cs.b * as + cb.b * ab)};
                    break;
                case MaskInvert:
                    // Invierte el fondo donde hay fuente.
                    ao = ab;
                    p = {(ab - cb.r * ab) * as + cb.r * ab * (1 - as), (ab - cb.g * ab) * as + cb.g * ab * (1 - as),
                         (ab - cb.b * ab) * as + cb.b * ab * (1 - as)};
                    break;
                case XorComposite: porterDuff(1 - ab, 1 - as), ao = as * (1 - ab) + ab * (1 - as); break;
                case Xor8:
                case Xor16: {
                    // O exclusivo bit a bit de los colores (8 o 16 bits por canal).
                    auto bits = [&](float b, float s) {
                        if (mode == Xor8)
                            return float(int(std::lround(b * 255)) ^ int(std::lround(s * 255))) / 255.f;
                        return float(int(std::lround(b * 65535)) ^ int(std::lround(s * 65535))) / 65535.f;
                    };
                    composite({bits(cb.r, cs.r), bits(cb.g, cs.g), bits(cb.b, cs.b)});
                    break;
                }
                default:
                    composite({separable(mode, cb.r, cs.r), separable(mode, cb.g, cs.g), separable(mode, cb.b, cs.b)});
                    break;
                }
                ao = std::clamp(ao, 0.f, 1.f);
                const int a8 = int(std::lround(ao * 255));
                if (a8 == 0) {
                    d[x] = 0;
                    continue;
                }
                auto ch = [&](float v) { return std::clamp(int(std::lround(v / ao * 255)), 0, 255); };
                const QRgb straight = qRgba(ch(p.r), ch(p.g), ch(p.b), a8);
                d[x] = basePremul ? qPremultiply(straight) : straight;
            }
        }
    });
}

} // namespace RenderBlend
