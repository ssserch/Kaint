#pragma once

// Utilidades internas compartidas por los efectos. Muchas funciones son adaptaciones a C++ de
// los algoritmos de Paint.NET 3.36 (licencia MIT).

#include "core/BlendMode.h"
#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Pixels.h"
#include "core/effects/Effect.h"

#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <vector>

namespace fx {

using RenderFn = std::function<void(const RenderArgs& a)>;
using PrepareFn = std::function<std::any(const QImage& src, const QRect& roi, const ParamValues& v, const EffectContext& ctx)>;

// Efecto definido por datos + funciones.
class SimpleEffect : public Effect {
public:
    struct Def {
        QString id;
        QString name;
        QString category;
        QString icon;
        QKeySequence shortcut;
        QList<EffectParam> params;
        RenderFn render;
        PrepareFn prepare = nullptr;
        bool hidden = false;
        bool straight = false;
        QString note = QString();
    };

    explicit SimpleEffect(Def def) : m(std::move(def)) {}

    QString id() const override { return m.id; }
    QString name() const override { return m.name; }
    QString category() const override { return m.category; }
    bool hidden() const override { return m.hidden; }
    bool straightAlpha() const override { return m.straight; }
    QKeySequence shortcut() const override { return m.shortcut; }
    QString iconName() const override { return m.icon; }
    QString dialogNote() const override { return m.note; }
    QList<EffectParam> params() const override { return m.params; }
    std::any prepare(const QImage& src, const QRect& roi, const ParamValues& v, const EffectContext& ctx) const override
    {
        return m.prepare ? m.prepare(src, roi, v, ctx) : std::any();
    }
    void render(const RenderArgs& a) const override { m.render(a); }

private:
    Def m;
};

// --- Valores --------------------------------------------------------------------

inline int ival(const ParamValues& v, const char* key) { return v.value(QLatin1String(key)).toInt(); }
inline double dval(const ParamValues& v, const char* key) { return v.value(QLatin1String(key)).toDouble(); }
inline bool bval(const ParamValues& v, const char* key) { return v.value(QLatin1String(key)).toBool(); }
inline QPointF pval(const ParamValues& v, const char* key) { return v.value(QLatin1String(key)).toPointF(); }
inline QColor cval(const ParamValues& v, const char* key) { return v.value(QLatin1String(key)).value<QColor>(); }

// --- Recorrido -----------------------------------------------------------------

// Recorre las filas [y0, y1) en paralelo, comprobando la cancelación y notificando el
// progreso. f(y) procesa una fila completa.
template <typename F>
void forRows(const RenderArgs& a, int y0, int y1, F f)
{
    parallelRows(y0, y1, [&](int r0, int r1) {
        for (int y = r0; y < r1 && !a.cancel; ++y)
            f(y);
        if (a.ctx.progress)
            a.ctx.progress->done += (r1 - r0);
    });
}

template <typename F>
void forRows(const RenderArgs& a, F f)
{
    forRows(a, a.roi.top(), a.roi.bottom() + 1, f);
}

// Píxel por píxel con colores sin premultiplicar. f(QRgb) -> QRgb. Sirve para imágenes con
// alfa directo (capas) y premultiplicadas.
template <typename F>
void mapStraight(const RenderArgs& a, F f)
{
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    const bool pm = isPremultiplied(a.src);
    forRows(a, [&](int y) {
        const QRgb* s = in.row(y);
        QRgb* d = out.row(y);
        if (pm) {
            for (int x = a.roi.left(); x <= a.roi.right(); ++x)
                d[x] = qPremultiply(f(qUnpremultiply(s[x])));
        } else {
            for (int x = a.roi.left(); x <= a.roi.right(); ++x)
                d[x] = f(s[x]);
        }
    });
}

// --- Color -----------------------------------------------------------------------

// Intensidad de Paint.NET (ColorBgra.GetIntensityByte).
inline int intensity(QRgb c) { return (7471 * qBlue(c) + 38470 * qGreen(c) + 19595 * qRed(c)) >> 16; }
inline double intensityF(QRgb c) { return (0.114 * qBlue(c) + 0.587 * qGreen(c) + 0.299 * qRed(c)) / 255.0; }

// Interpolación de Paint.NET (ColorBgra.Lerp), admite t fuera de 0..1.
inline QRgb lerpStraight(QRgb a, QRgb b, double t)
{
    auto l = [t](int x, int y) { return clamp255(int(x + (y - x) * t)); };
    return qRgba(l(qRed(a), qRed(b)), l(qGreen(a), qGreen(b)), l(qBlue(a), qBlue(b)), l(qAlpha(a), qAlpha(b)));
}

// Fórmula generalizada de mezcla de Paint.NET sobre colores directos: lhs = fondo, rhs = lo
// que se pone encima.
template <typename F>
inline QRgb blendStraight(QRgb lhs, QRgb rhs, F f)
{
    const int la = qAlpha(lhs), ra = qAlpha(rhs);
    const int y = (la * (255 - ra) + 127) / 255;
    const int total = y + ra;
    if (total == 0)
        return 0;
    const int x = (la * ra + 127) / 255;
    const int z = ra - x;
    auto ch = [&](int A, int B) { return clamp255((A * y + B * z + f(A, B) * x) / total); };
    return qRgba(ch(qRed(lhs), qRed(rhs)), ch(qGreen(lhs), qGreen(rhs)), ch(qBlue(lhs), qBlue(rhs)), total);
}

inline int opScreen(int a, int b) { return b + a - (a * b + 127) / 255; }
inline int opDarken(int a, int b) { return std::min(a, b); }
inline int opOverlay(int a, int b) { return a < 128 ? (2 * a * b + 127) / 255 : 255 - (2 * (255 - a) * (255 - b) + 127) / 255; }
inline int opColorDodge(int a, int b) { return b == 255 ? 255 : std::min(255, (a * 255) / (255 - b)); }

// Tabla de Brillo y contraste de Paint.NET (también la usan Resplandor, Bosquejo...).
class BrightnessContrast {
public:
    BrightnessContrast(int brightness, int contrast);
    QRgb apply(QRgb straight) const;

private:
    int m_divide = 1;
    std::vector<uint8_t> m_table;
};

// sRGB <-> lineal (Viñeta, Exposición...).
double toLinear(int c);
int toSrgb(double linear);

// --- Muestreo -------------------------------------------------------------------

enum class EdgeBehavior { Clamp, Wrap, Reflect, Primary, Secondary, Transparent, Original };
QStringList edgeBehaviorNames(const QList<EdgeBehavior>& list);

QRgb sampleWrapped(const ConstPixels& src, float x, float y);
// Muestra bilineal con el comportamiento de borde indicado fuera de la imagen.
QRgb sampleEdge(const ConstPixels& src, float x, float y, EdgeBehavior edge);
QRgb sampleReflected(const ConstPixels& src, float x, float y);
// Promedio de muestras premultiplicadas (ColorBgra.Blend de Paint.NET).
QRgb average(const QRgb* samples, int count);

// Desplazamientos de supermuestreo en rejilla girada (Utility.GetRgssOffsets).
std::vector<QPointF> rgssOffsets(int quality);

// Distorsión al estilo WarpEffectBase de Paint.NET: inverse(x, y) recibe coordenadas relativas
// al centro y devuelve las de origen (también relativas).
struct WarpSetup {
    QPointF offset;
    int quality = 2;
    EdgeBehavior edge = EdgeBehavior::Wrap;
};
void warp(const RenderArgs& a, const WarpSetup& setup, const std::function<QPointF(double, double)>& inverse);
inline double defaultRadius(const QRect& roi) { return std::min(roi.width(), roi.height()) * 0.5; }

// --- Desenfoques ------------------------------------------------------------------

// Desenfoque "gaussiano" de Paint.NET: núcleo triangular de radio r, que es exactamente dos
// pasadas de caja de ancho r + 1 en cada eje. Devuelve 'area'.
QImage tentBlur(const QImage& src, const QRect& area, int radius, const std::atomic_bool& cancel);
int tentMargin(int radius);
// Media de una caja cuadrada (Desenfoque cuadrado).
QImage boxBlur(const QImage& src, const QRect& area, int radius, const std::atomic_bool& cancel);

// --- Histograma local circular (LocalHistogramEffect de Paint.NET) --------------

struct LocalHistogram {
    int area = 0;
    int b[256], g[256], r[256], a[256];
    // Con radios pequeños, también las cuentas de 16 en 16 valores (cr[k] = r[16k] + ... +
    // r[16k + 15]): los percentiles se buscan saltando bloques enteros en vez de recorrer los 256
    // valores en cada píxel. Con radios grandes mantenerlas costaría más de lo que ahorran y
    // 'coarse' es falso (las cr... no valen).
    bool coarse = false;
    int cb[16], cg[16], cr[16], ca[16];

    const int* coarseR() const { return coarse ? cr : nullptr; }
    const int* coarseG() const { return coarse ? cg : nullptr; }
    const int* coarseB() const { return coarse ? cb : nullptr; }
    const int* coarseA() const { return coarse ? ca : nullptr; }
};

// Hasta este radio el histograma lleva las cuentas de 16 en 16.
constexpr int kCoarseHistogramRadius = 32;

// Los bucles de Paint.NET sobre un canal del histograma, iguales pero saltando de 16 en 16 cuando
// hay cuentas por bloques ('coarse' no nulo). El primer valor con cuenta (255 si no hay):
inline int histogramFirst(const int* hist, const int* coarse)
{
    int v = 0;
    if (coarse)
        while (v < 240 && coarse[v >> 4] == 0)
            v += 16;
    while (v < 255 && hist[v] == 0)
        ++v;
    return v;
}

// Desde 'v', suma cuentas a 'count' mientras no llegue a 'target' y devuelve el valor siguiente
// al que la alcanzó (como mucho 255).
inline int histogramAdvance(const int* hist, const int* coarse, int v, int& count, int target)
{
    if (coarse) {
        while (v < 255 && count < target && (v & 15) != 0)
            count += hist[v++];
        while (v < 240 && count + coarse[v >> 4] < target) {
            count += coarse[v >> 4];
            v += 16;
        }
    }
    while (v < 255 && count < target)
        count += hist[v++];
    return v;
}

// Cuántos píxeles tienen un valor menor que 'v'.
inline int histogramCountBelow(const int* hist, const int* coarse, int v)
{
    int sum = 0, i = 0;
    if (coarse)
        for (; i + 16 <= v; i += 16)
            sum += coarse[i >> 4];
    for (; i < v; ++i)
        sum += hist[i];
    return sum;
}

namespace detail {

template <bool Coarse, typename Apply>
void localHistogramRows(const RenderArgs& a, int rad, Apply& apply)
{
    const QRect region = a.roi.adjusted(-(rad + 1), -(rad + 1), rad + 1, rad + 1).intersected(a.src.rect());
    const QImage straight = a.src.copy(region).convertToFormat(QImage::Format_ARGB32);
    const ConstPixels s(straight);
    const Pixels out(a.dst);
    const int ox = region.left(), oy = region.top();
    const int width = a.src.width(), height = a.src.height();

    // Semiancho horizontal del círculo en cada fila (aprox. (r + 0,5)^2).
    const int cutoff = ((rad * 2 + 1) * (rad * 2 + 1) + 2) / 4;
    std::vector<int> edge(size_t(rad + 1), 0);
    for (int v = 0; v <= rad; ++v)
        for (int u = 0; u <= rad; ++u)
            if (u * u + v * v <= cutoff)
                edge[size_t(v)] = u;

    forRows(a, [&](int y) {
        LocalHistogram h{};
        h.coarse = Coarse;
        auto add = [&h](QRgb p, int sign) {
            const int b = qBlue(p), g = qGreen(p), r = qRed(p), al = qAlpha(p);
            h.b[b] += sign;
            h.g[g] += sign;
            h.r[r] += sign;
            h.a[al] += sign;
            if constexpr (Coarse) {
                h.cb[b >> 4] += sign;
                h.cg[g >> 4] += sign;
                h.cr[r >> 4] += sign;
                h.ca[al >> 4] += sign;
            }
            h.area += sign;
        };
        const int top = -std::min(rad, y);
        const int bottom = std::min(rad, height - 1 - y);
        // Las filas del vecindario en la copia, y su semiancho.
        thread_local std::vector<const QRgb*> rows;
        thread_local std::vector<int> widths;
        rows.resize(size_t(bottom - top + 1));
        widths.resize(rows.size());
        for (int v = top; v <= bottom; ++v) {
            rows[size_t(v - top)] = s.row(y + v - oy) - ox;
            widths[size_t(v - top)] = edge[size_t(std::abs(v))];
        }
        const int x0 = a.roi.left();
        for (size_t i = 0; i < rows.size(); ++i) {
            const int w = widths[i];
            for (int u = std::max(-w, -x0); u <= std::min(w, width - 1 - x0); ++u)
                add(rows[i][x0 + u], +1);
        }
        const QRgb* center = s.row(y - oy) - ox;
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            d[x] = qPremultiply(apply(center[x], const_cast<const LocalHistogram&>(h)));
            if (x == a.roi.right())
                break;
            for (size_t i = 0; i < rows.size(); ++i) {
                const int w = widths[i];
                if (x - w >= 0)
                    add(rows[i][x - w], -1);
                if (x + 1 + w < width)
                    add(rows[i][x + 1 + w], +1);
            }
        }
    });
}

} // namespace detail

// Recorre la selección con el histograma circular de radio 'radius' alrededor de cada píxel:
// apply(píxel directo, histograma) -> píxel directo.
template <typename Apply>
void localHistogram(const RenderArgs& a, int radius, Apply&& apply)
{
    const int rad = std::max(0, radius);
    if (rad <= kCoarseHistogramRadius)
        detail::localHistogramRows<true>(a, rad, apply);
    else
        detail::localHistogramRows<false>(a, rad, apply);
}

QRgb histogramPercentile(const LocalHistogram& h, int percentile);
// "Calidad" (1–9) de Mediana y Contorno en Paint.NET 5: bits de precisión de la búsqueda del
// percentil; con menos de 8 cada canal cae en el centro de su intervalo.
QRgb quantizeBits(QRgb c, int bits);

// --- Ruido de Perlin de Paint.NET (PerlinNoise2D) --------------------------------

double pdnPerlin(double x, double y, double detail, double roughness, uint8_t seed);

// Registro de cada familia de efectos.
void registerAdjustments(QList<const Effect*>& list);
void registerArtistic(QList<const Effect*>& list);
void registerBlurs(QList<const Effect*>& list);
void registerColor(QList<const Effect*>& list);
void registerDistort(QList<const Effect*>& list);
void registerNoise(QList<const Effect*>& list);
void registerObject(QList<const Effect*>& list);
void registerPhoto(QList<const Effect*>& list);
void registerRender(QList<const Effect*>& list);
void registerStylize(QList<const Effect*>& list);
void registerLayerEffects(QList<const Effect*>& list);

// Nombres de categoría (también ordenan el menú Efectos).
inline QString catArtistic() { return QObject::tr("Artístico"); }
inline QString catBlurs() { return QObject::tr("Desenfoques"); }
inline QString catColor() { return QObject::tr("Color"); }
inline QString catDistort() { return QObject::tr("Distorsionar"); }
inline QString catNoise() { return QObject::tr("Ruido"); }
inline QString catObject() { return QObject::tr("Objeto"); }
inline QString catPhoto() { return QObject::tr("Fotografía"); }
inline QString catRender() { return QObject::tr("Representación"); }
inline QString catStylize() { return QObject::tr("Estilizado"); }
inline QString catLayer() { return QObject::tr("Capa"); }

// Ayudas para declarar parámetros habituales.
EffectParam qualityParam(int def = 2);
EffectParam blendParam(const char* key = "blend");
BlendMode blendValue(const ParamValues& v, const char* key = "blend");
EffectParam edgeParam(const QList<EdgeBehavior>& list, EdgeBehavior def);
// Los cuatro comportamientos de borde de Paint.NET 5.
const QList<EdgeBehavior>& pdnEdges();
EdgeBehavior edgeValue(const ParamValues& v, const QList<EdgeBehavior>& list);

} // namespace fx
