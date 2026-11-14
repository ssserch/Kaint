#pragma once

// Retoque de PhotoCraft: Pincel corrector, Pincel corrector puntual, Quitar, Parche, Lazo magnético
// y Selección de objetos. Los algoritmos son los de PhotoCraft (ArtCraft Team y los colaboradores
// de PhotoCraft, licencia MIT; data/licenses/PhotoCraft-LICENSE.txt), escritos para Kaint.
//
// Trabajan con colores RGBA directos en coma flotante (0..1) entrelazados (w × h × ch) y máscaras
// de w × h (un byte por píxel, 0 o 1). Son deterministas: el resultado no depende de cuántos
// núcleos tenga el equipo.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace retouch {

using Mask = std::vector<uint8_t>;
using Floats = std::vector<float>;

// Se canceló (Esc, otra herramienta...): lo lanza Stop::check().
struct Cancelled {};

// Si hay que parar: los bucles largos lo consultan.
class Stop {
public:
    Stop() = default;
    explicit Stop(const std::atomic_bool* flag) : m_flag(flag) {}
    bool requested() const { return m_flag && m_flag->load(std::memory_order_relaxed); }
    void check() const
    {
        if (requested())
            throw Cancelled{};
    }

private:
    const std::atomic_bool* m_flag = nullptr;
};

// Rectángulo de píxeles [x0, x1) × [y0, y1).
struct Box {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    int width() const { return x1 > x0 ? x1 - x0 : 0; }
    int height() const { return y1 > y0 ? y1 - y0 : 0; }
    long long area() const { return (long long)width() * height(); }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    Box intersect(const Box& o) const;
    Box unite(const Box& o) const;
    Box inflate(int d) const;
    Box translate(int dx, int dy) const;
    bool operator==(const Box&) const = default;
};

// ---------------------------------------------------------------------------------------------
// Números pseudoaleatorios (SplitMix64), los mismos en cada equipo.

inline uint64_t splitMix(uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct MixRng {
    uint64_t state;
    explicit MixRng(uint64_t seed) : state(seed ^ 0x9E3779B97F4A7C15ull) {}
    uint64_t next() { return splitMix(state); }
    void skip(uint64_t n) { state += n * 0x9E3779B97F4A7C15ull; }
    int range(int lo, int hi)
    {
        if (hi <= lo)
            return lo;
        return lo + int(next() % uint64_t(int64_t(hi) - lo + 1));
    }
};

// ---------------------------------------------------------------------------------------------
// Máscaras y ventanas (Quitar, Pincel corrector puntual).

// El trazo más grande (su rectángulo), en píxeles: 16 Mi.
constexpr long long kMaxPixels = 16ll << 20;

// La máscara crecida 'r' celdas (elemento cuadrado, en dos pasadas).
Mask dilate(const Mask& m, int w, int h, int r);
// Añade a la máscara lo que encierra: las celdas desde las que no se llega al borde sin cruzarla.
void closeLoops(Mask& m, int w, int h);
// La mayor distancia de tablero de ajedrez de una celda del hueco a la más cercana de fuera.
uint32_t holeRadius(int w, int h, const Mask& hole);
// La ventana de la que toma Quitar: el trazo y un margen de 3/4 de su lado mayor (32 a 512).
Box removeWindow(Box stroke, Box canvas);
// La ventana de Relleno según el contenido (la de Photoshop): un cuadrado de 4·√(área).
Box samplingWindow(Box hole, Box canvas);
// Distancia euclídea a la celda marcada más cercana (1e10 sin ninguna).
Floats distanceTransform(const Mask& inside, int w, int h);

// ---------------------------------------------------------------------------------------------
// Fusión en el dominio del gradiente (Poisson).

// Resuelve la ecuación de Laplace en las celdas desconocidas de 'v' (el resto es el borde).
void solveMembrane(int w, int h, const Mask& unknown, Floats& v, const Stop& stop = {});
// Clonado sin costuras: 'src' dentro de la máscara con los colores de 'dst' alrededor.
Floats seamlessClone(int w, int h, int ch, const Floats& src, const Floats& dst, const Mask& mask,
                     const Stop& stop = {});
// Rellena el hueco con la membrana armónica de su borde.
Floats membraneFill(int w, int h, int ch, const Floats& img, const Mask& hole, const Stop& stop = {});

// ---------------------------------------------------------------------------------------------
// Relleno según el contenido (completado por parches, coincidencia por proximidad, textura).

// Ajuste de color de cada parche copiado (ganancia y desplazamiento por canal).
struct Adapt {
    float gainLo = 1.f;
    float gainHi = 1.f;
    float bias = 0.f;
    bool none() const { return gainLo == 1.f && gainHi == 1.f && bias == 0.f; }
};

struct CompleteParams {
    int patchRadius = 3;
    int emIters = 10;
    int minEmIters = 3;
    int pmIters = 2;
    int refineExtent = 192;
    Adapt adapt;
    uint64_t seed = 1;
};

// Papel de cada píxel en el completado.
enum class Px : uint8_t { Void, Known, Source, Hole };

// Completa el hueco de 'img' (los píxeles Hole). Falso, sin tocar 'img', si no hay de dónde copiar.
bool completeMasked(int w, int h, int ch, Floats& img, const std::vector<Px>& px, const CompleteParams& p,
                    const Stop& stop = {});
// Con todo lo que no es hueco como fuente.
std::optional<Floats> complete(int w, int h, int ch, const Floats& img, const Mask& hole, const CompleteParams& p,
                               const Stop& stop = {});
// Coincidencia por proximidad: el desplazamiento cuyo anillo mejor coincide con el del hueco.
std::optional<std::pair<int, int>> bestOffset(int w, int h, int ch, const Floats& img, const Mask& hole, int ring,
                                              int maxRadius);
// Crear textura: bloques de la textura de alrededor unidos por cortes de error mínimo.
Floats synthesize(int w, int h, int ch, const Floats& img, const Mask& hole, int block, uint64_t seed,
                  const Stop& stop = {});

// Relleno según el contenido de Edición (con su ajuste de color).
Adapt colorLevel(const char* level);
Adapt colorAmount(int color);
std::pair<float, float> fitGainBias(Adapt a, float n, float st, float stt, float ss, float sss);
std::vector<std::pair<float, float>> levelTransform(int ch, const Floats& from, const Floats& to, Adapt a);
Floats contentAwareFill(int w, int h, int ch, const Floats& img, const Mask& hole, const Mask& source, Adapt adapt,
                        uint64_t seed, const Stop& stop = {});

// ---------------------------------------------------------------------------------------------
// Quitar: completado no local (Newson et al.) con rasgos de textura.

struct NonLocalParams {
    int patchRadius = 3;
    float lambda = 50.f;
    int maxIters = 10;
    int pmPasses = 2;
    float tolerance = 0.1f / 255.f;
    uint64_t seed = 1;
};

std::optional<Floats> completeNonLocal(int w, int h, int ch, const Floats& img, const Mask& hole,
                                       const NonLocalParams& p, const Stop& stop = {});
// Rellena el hueco desde su alrededor (por mosaicos si es disperso).
Floats removeFill(int w, int h, int ch, const Floats& img, const Mask& hole, uint64_t seed, const Stop& stop = {});

// ---------------------------------------------------------------------------------------------
// Curar (Pincel corrector, Pincel corrector puntual, Parche): píxeles RGBA de 8 bits directos.

using Bytes = std::vector<uint8_t>;

Floats toFloats(const Bytes& rgba);
Bytes toBytes(const Floats& v);

// Una curación sobre un rectángulo w × h: el clon 'src' encajado en 'dst' donde la cobertura
// 'cov' (0..255) no es 0.
struct Heal {
    int w = 0;
    int h = 0;
    Bytes src;
    Bytes dst;
    Bytes cov;
    Bytes run(const Stop& stop = {}) const;
    // Para la vista previa del Parche: la membrana 'step' veces más gruesa.
    Bytes runCoarse(int step, const Stop& stop = {}) const;
};

// Margen del Pincel corrector alrededor del trazo (el borde de la solución).
constexpr int kHealPad = 2;

// Pincel corrector puntual: el tipo de relleno.
enum class SpotKind { ContentAware, CreateTexture, ProximityMatch };
Box spotWindow(Box stroke, Box layer);
// Rellena la ventana 'img' (w × h) donde la cobertura 'cov' no es 0 y lo funde con el alrededor.
Bytes spotHeal(int w, int h, const Bytes& img, const Bytes& cov, SpotKind kind, double brushSize,
               const Stop& stop = {});

// Parche.
enum class PatchMode { Source, Destination, ContentAware };
enum class PatchRefusal { None, Empty, TooLarge, NoOffset, OffCanvas };
struct PatchPlan {
    PatchMode mode = PatchMode::Source;
    Box rect;
    std::pair<int, int> dstShift;
    std::pair<int, int> srcShift;
};
PatchRefusal planPatch(Box area, Box canvas, std::pair<int, int> offset, PatchMode mode, PatchPlan* out);
int patchPreviewStep(Box rect, long long maxCells);
Box patchWindow(Box r, Box canvas);
Floats patchLand(int w, int h, const Floats& here, const Floats& content, const Mask& selected, int structure, int color,
                 const Stop& stop = {});

// ---------------------------------------------------------------------------------------------
// Lazo magnético: el borde más barato por los bordes de la imagen (Mortensen y Barrett).

struct PointF {
    double x = 0;
    double y = 0;
    bool operator==(const PointF&) const = default;
};

struct MagneticSettings {
    double width = 10;     // anchura de detección (px)
    float contrast = 0.1f; // 0..1
    static MagneticSettings make(double width, float contrast);
};

class MagneticTracer {
public:
    // Lee los píxeles RGBA de 8 bits directos de un rectángulo (dentro de la imagen), por filas.
    using Fetch = std::function<std::vector<uint32_t>(Box)>;

    MagneticTracer() = default;
    explicit MagneticTracer(Box bounds) : m_bounds(bounds) {}
    Box bounds() const { return m_bounds; }
    std::optional<PointF> snap(const Fetch& fetch, PointF p, MagneticSettings s);
    std::vector<PointF> trace(const Fetch& fetch, PointF from, PointF to, const std::vector<PointF>& guide,
                              MagneticSettings s);

private:
    struct Field;
    std::vector<PointF> tracePiece(const Fetch& fetch, PointF a, PointF b, const std::vector<PointF>& guide,
                                   MagneticSettings s);
    std::optional<PointF> clampPoint(PointF p) const;
    std::pair<int, int> pixel(PointF p) const;
    Field field(const Fetch& fetch, Box area);
    std::vector<uint32_t> pixels(const Fetch& fetch, Box r);

    Box m_bounds;
    std::unordered_map<uint64_t, std::vector<uint32_t>> m_blocks;
};

std::vector<PointF> simplifyPolyline(const std::vector<PointF>& p, double tol);
double polylineLength(const std::vector<PointF>& p);

// ---------------------------------------------------------------------------------------------
// Selección de objetos (GrabCut, Buscador de objetos y Seleccionar sujeto).

// Una región: cobertura de 8 bits sobre 'box'.
struct Region {
    Box box;
    Bytes mask;
};

// De dónde se leen los colores: RGBA directos (0..1) de un rectángulo, por filas.
using Sampler = std::function<std::vector<std::array<float, 4>>(Box)>;

std::optional<Region> objectInRect(const Sampler& sampler, Box rect, Box canvas, int workPx, const Stop& stop = {});
constexpr int kObjectAtWorkPx = 60000;
std::optional<Region> objectAt(const Sampler& sampler, Box canvas, float x, float y, int workPx, const Stop& stop = {});
constexpr int kSubjectWorkPx = 110000;
std::optional<Region> selectSubject(const Sampler& sampler, Box canvas, int workPx, const Stop& stop = {});

} // namespace retouch
