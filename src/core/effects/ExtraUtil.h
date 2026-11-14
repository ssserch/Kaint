#pragma once

// Utilidades de las funciones de PhotoCraft (ExtraEffects*.cpp): los algoritmos de PhotoCraft
// (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT; data/licenses/PhotoCraft-LICENSE.txt)
// trabajan con colores RGBA directos en coma flotante (0..1); aquí están esa imagen de trabajo, el
// muestreo con sus bordes y las conversiones de color que comparten.

#include "core/effects/EffectUtils.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace fx::extra {

// Imagen RGBA directa en coma flotante (la capa entera).
struct FBuf {
    int w = 0;
    int h = 0;
    std::vector<float> d;

    float* at(int x, int y) { return d.data() + (size_t(y) * w + x) * 4; }
    const float* at(int x, int y) const { return d.data() + (size_t(y) * w + x) * 4; }
};

inline int to8(float v)
{
    return v <= 0.f ? 0 : v >= 1.f ? 255 : int(v * 255.f + 0.5f);
}

FBuf toFloat(const QImage& img);
// Escribe las filas de la zona de 'a' en a.dst.
void store(const FBuf& b, const RenderArgs& a);

// Efecto que trabaja sobre la imagen entera: f(buf, a) cambia la zona a.roi de 'buf'.
template <typename F>
RenderFn whole(F f)
{
    return [f](const RenderArgs& a) {
        FBuf b = toFloat(a.src);
        f(b, a);
        if (!a.cancel)
            store(b, a);
    };
}

// Píxel a píxel sobre RGB (0..1) con el alfa intacto; los transparentes no se tocan.
template <typename F>
RenderFn perPixel(F f)
{
    return [f](const RenderArgs& a) {
        mapStraight(a, [&](QRgb c) {
            if (!qAlpha(c))
                return c;
            float v[3] = {qRed(c) / 255.f, qGreen(c) / 255.f, qBlue(c) / 255.f};
            f(v, a.values);
            return qRgba(to8(v[0]), to8(v[1]), to8(v[2]), qAlpha(c));
        });
    };
}

// --- Color ----------------------------------------------------------------------------

inline float srgbToLinear(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgb(float v)
{
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.f / 2.4f) - 0.055f;
}
// Luminosidad del W3C (la de los modos de fusión de Photoshop).
inline float lum(const float* c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }
// ClipColor y SetLum de los modos de fusión no separables.
void clipColor(float* c);
void setLum(float* c, float l);

// Ruido determinista en 0..1 (el mismo resultado en Kaint y en la web).
inline float hash01(int32_t x, int32_t y, uint32_t c, uint32_t seed)
{
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ c * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h >> 8) / float(1u << 24);
}

inline float smoothstep(float e0, float e1, float x)
{
    if (e1 <= e0)
        return x >= e1 ? 1.f : 0.f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// --- Muestreo -------------------------------------------------------------------------

enum class Edge { Transparent, Repeat, Wrap };

// Bilineal con los colores ponderados por el alfa; fuera de 'area', según 'edge'.
void sample(const FBuf& b, float x, float y, Edge edge, const QRect& area, float* out);

// Cada píxel de 'area' toma el color de la posición que da f(x + 0.5, y + 0.5).
FBuf remap(const FBuf& b, const QRect& area, Edge edge, const std::atomic_bool& cancel,
           const std::function<QPointF(float, float)>& f);

// Desenfoque gaussiano aproximado con tres pasadas de caja sobre los colores premultiplicados
// (de la zona 'area', con los bordes repetidos).
void gaussian(FBuf& b, const QRect& area, float sigma, const std::atomic_bool& cancel);

} // namespace fx::extra
