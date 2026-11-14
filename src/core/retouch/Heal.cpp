// Retoque de PhotoCraft (Retouch.h): el Pincel corrector puntual y el Parche. Algoritmos de
// PhotoCraft (licencia MIT).

#include "core/retouch/Retouch.h"

#include <algorithm>
#include <cmath>

namespace retouch {

// ---------------------------------------------------------------------------------------------
// Pincel corrector puntual: el trazo (cobertura > 0) se rellena según el tipo sobre el hueco
// crecido 2 y el relleno se funde (clonado sin costuras) sobre el hueco crecido 1.

Box spotWindow(Box stroke, Box layer)
{
    Box win = samplingWindow(stroke, layer);
    if (win.area() > kMaxPixels)
        win = removeWindow(stroke, layer);
    return win;
}

namespace {

// Copia un parche desplazado de los píxeles de antes de curar (leer los ya escritos repetiría
// lo recién copiado y dejaría rayas).
std::optional<Floats> proximityPatch(const Floats& img, int w, int h, int ch, const Mask& mask, int dx, int dy)
{
    if (w == 0 || h == 0 || ch == 0 || mask.size() != size_t(w) * h || img.size() != mask.size() * ch)
        return std::nullopt;
    Floats out = img;
    for (size_t p = 0; p < mask.size(); ++p) {
        if (!mask[p])
            continue;
        const int x = int(p % size_t(w)) + dx, y = int(p / size_t(w)) + dy;
        if (x < 0 || y < 0 || x >= w || y >= h)
            return std::nullopt;
        const size_t src = (size_t(y) * w + x) * ch;
        std::copy(img.begin() + long(src), img.begin() + long(src + ch), out.begin() + long(p * ch));
    }
    return out;
}

} // namespace

Bytes spotHeal(int w, int h, const Bytes& imgBytes, const Bytes& cov, SpotKind kind, double brushSize, const Stop& stop)
{
    const Floats img = toFloats(imgBytes);
    Mask hole(cov.size());
    for (size_t i = 0; i < cov.size(); ++i)
        hole[i] = cov[i] > 0;
    // El relleno cubre más que la fusión: el borde de la solución lleva relleno que difiere del
    // alrededor, que es lo que corrige la membrana.
    const Mask domain = dilate(hole, w, h, 2);
    auto contentAware = [&]() {
        if (auto filled = complete(w, h, 4, img, domain, CompleteParams{}, stop))
            return std::move(*filled);
        return membraneFill(w, h, 4, img, domain, stop);
    };
    Floats filled;
    switch (kind) {
    case SpotKind::CreateTexture: {
        const int block = int(std::clamp(brushSize / 4.0, 6.0, 32.0));
        filled = synthesize(w, h, 4, img, domain, block, 0x5eed, stop);
        break;
    }
    case SpotKind::ProximityMatch: {
        // La fuente puede estar en cualquier sitio de la ventana.
        const int ring = int(std::clamp(brushSize / 8.0, 3.0, 16.0));
        const int margin = std::max(w, h) / 2;
        std::optional<Floats> patch;
        if (const auto off = bestOffset(w, h, 4, img, domain, ring, margin))
            patch = proximityPatch(img, w, h, 4, domain, off->first, off->second);
        filled = patch ? std::move(*patch) : contentAware();
        break;
    }
    default:
        filled = contentAware();
        break;
    }
    stop.check();
    Heal heal;
    heal.w = w;
    heal.h = h;
    heal.src = toBytes(filled);
    heal.dst = imgBytes;
    const Mask grown = dilate(hole, w, h, 1);
    heal.cov.resize(grown.size());
    for (size_t i = 0; i < grown.size(); ++i)
        heal.cov[i] = grown[i] ? 255 : 0;
    return heal.run(stop);
}

// ---------------------------------------------------------------------------------------------
// El Parche: la selección es el parche y el desplazamiento cuánto se arrastró. En Origen la zona
// seleccionada se repara con lo que hay bajo el contorno arrastrado; en Destino lo seleccionado se
// copia al contorno arrastrado y lo repara. En Según el contenido, la selección se rellena desde
// donde se soltó (el centro copiado, ajustado por Color, y una banda junto al borde rehecha según
// Estructura).

PatchRefusal planPatch(Box area, Box canvas, std::pair<int, int> offset, PatchMode mode, PatchPlan* out)
{
    area = area.intersect(canvas);
    if (area.empty())
        return PatchRefusal::Empty;
    const auto [dx, dy] = offset;
    if (dx == 0 && dy == 0)
        return PatchRefusal::NoOffset;
    // La textura y la zona reparada deben estar en el lienzo: fuera, los píxeles están vacíos.
    const Box moved = area.translate(dx, dy);
    if (!(moved.intersect(canvas) == moved))
        return PatchRefusal::OffCanvas;
    std::pair<int, int> dstShift{0, 0}, srcShift{dx, dy};
    if (mode == PatchMode::Destination) {
        dstShift = {dx, dy};
        srcShift = {-dx, -dy};
    }
    Box rect;
    if (mode == PatchMode::ContentAware) {
        rect = patchWindow(area, canvas);
    } else {
        // La zona reparada y un anillo de dos píxeles (el borde de la solución), recortados a
        // donde tanto lo reparado como lo que se toma están en el lienzo.
        rect = area.translate(dstShift.first, dstShift.second)
                   .inflate(2)
                   .intersect(canvas)
                   .intersect(canvas.translate(-srcShift.first, -srcShift.second));
    }
    if (rect.area() > kMaxPixels)
        return PatchRefusal::TooLarge;
    if (out)
        *out = {mode, rect, dstShift, srcShift};
    return PatchRefusal::None;
}

int patchPreviewStep(Box rect, long long maxCells)
{
    const double pixels = double(rect.area());
    const int need = int(std::ceil(std::sqrt(pixels / double(std::max(maxCells, 1ll)))));
    return std::clamp(need, 1, 64);
}

Box patchWindow(Box r, Box canvas)
{
    return samplingWindow(r, canvas).intersect(r.inflate(512));
}

namespace {

// Ancho de la banda junto al borde que se rehace, para Estructura 1..7 y un contenido de
// 'minSide' píxeles: de 3 a 7 es más o menos un parche; 2 y 1 la ensanchan.
int bandWidth(int structure, int minSide)
{
    const float side = float(minSide);
    float w;
    if (structure <= 1)
        w = std::max(side * 0.3f, 12.f);
    else if (structure == 2)
        w = std::max(side * 0.18f, 9.f);
    else
        w = 6.f;
    return std::min(int(std::lround(w)), 128);
}

Mask erode(int w, int h, const Mask& m, int r)
{
    Mask unset(m.size());
    for (size_t i = 0; i < m.size(); ++i)
        unset[i] = !m[i];
    const Mask reached = dilate(unset, w, h, r);
    Mask out(m.size());
    for (size_t i = 0; i < m.size(); ++i)
        out[i] = m[i] && !reached[i];
    return out;
}

Mask outerRing(int gw, int gh, const Mask& m, int width)
{
    const Mask grown = dilate(m, gw, gh, width);
    Mask out(m.size());
    for (size_t i = 0; i < m.size(); ++i)
        out[i] = grown[i] && !m[i];
    return out;
}

} // namespace

Floats patchLand(int w, int h, const Floats& here, const Floats& content, const Mask& selected, int structure, int color,
                 const Stop& stop)
{
    constexpr int N = 4;
    int sx0 = w, sy0 = h, sx1 = 0, sy1 = 0;
    for (size_t i = 0; i < selected.size(); ++i)
        if (selected[i]) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            sx0 = std::min(sx0, x);
            sy0 = std::min(sy0, y);
            sx1 = std::max(sx1, x + 1);
            sy1 = std::max(sy1, y + 1);
        }
    const int side = std::min(std::max(sx1 - sx0, 0), std::max(sy1 - sy0, 0));
    stop.check();
    const Mask core = erode(w, h, selected, bandWidth(structure, side));
    stop.check();
    // El color: el alrededor del contenido (de donde vino) contra el nuevo, justo fuera de la forma.
    const Mask ring = outerRing(w, h, selected, 6);
    auto pick = [&](const Floats& r) {
        Floats v;
        for (size_t i = 0; i < ring.size(); ++i)
            if (ring[i])
                v.insert(v.end(), r.begin() + long(i * N), r.begin() + long(i * N + N));
        return v;
    };
    const auto gb = levelTransform(N, pick(content), pick(here), colorAmount(color));
    stop.check();
    Floats buf = here;
    for (size_t i = 0; i < core.size(); ++i)
        if (core[i])
            for (int c = 0; c < N; ++c) {
                const float v = content[i * N + c];
                // Los canales de color se ajustan; el alfa se copia.
                buf[i * N + c] = c == N - 1 ? v : gb[size_t(c)].first * v + gb[size_t(c)].second;
            }
    for (float& v : buf)
        v = std::clamp(v, 0.f, 1.f);
    // La estructura: la banda entre el centro y el borde de la selección sale del alrededor y del
    // centro.
    Mask band(selected.size());
    bool any = false;
    for (size_t i = 0; i < band.size(); ++i) {
        band[i] = selected[i] && !core[i];
        any = any || band[i];
    }
    if (any) {
        const Mask allowed(size_t(w) * h, 1);
        const Adapt adapt = color == 0 ? colorLevel("default") : colorAmount(color);
        buf = contentAwareFill(w, h, N, buf, band, allowed, adapt, 0x00ca4e00ull, stop);
    }
    return buf;
}

} // namespace retouch
