#pragma once

#include "core/Selection.h"

#include <QColor>
#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QMetaType>
#include <QPointF>
#include <QStringList>
#include <QVariantMap>

#include <any>
#include <array>
#include <atomic>
#include <cstdint>

using ParamValues = QVariantMap;

// Descripción de un parámetro. El diálogo de efectos construye los controles
// a partir de esta lista (como el IndirectUI de Paint.NET), así que cada
// efecto solo declara qué necesita.
struct EffectParam {
    enum class Type {
        Int,     // deslizador + caja numérica + restablecer
        Double,
        Bool,    // casilla
        Choice,  // lista desplegable
        Seed,    // botón "Nueva semilla" (0..255 como en Paint.NET)
        Angle,   // selector circular de ángulo
        Offset,  // centro/desplazamiento -1..1 sobre una miniatura
        Color,   // botón de color
        Curves,  // editor de curvas
        Levels,  // editor de niveles con histogramas
        RollBall, // globo de "Rotación y giro" (ángulo, dirección e inclinación; valor QVector3D)
        Text,     // caja de texto (una línea o varias)
    };
    enum class ColorDefault { Primary, Secondary, Black, White };

    Type type = Type::Int;
    QString key;
    QString label;
    double min = 0;
    double max = 100;
    double def = 0;
    int decimals = 0;
    QStringList choices;
    ColorDefault colorDefault = ColorDefault::Primary;
    QString linkKey; // parámetro Bool que, activado, iguala los parámetros con el mismo linkKey
    double minY = -1; // Offset: rango de Y (el de X es min..max)
    double maxY = 1;
    QVariant defaultValue; // si es válido, sustituye a def/colorDefault (complementos)
    QString description;   // texto bajo el título (en las casillas, el texto de la casilla)
    bool multiline = false; // Text
    // Solo lectura (control deshabilitado): siempre, o mientras algún
    // parámetro 'key' tenga uno de 'values' (al revés si readOnlyInverse).
    struct ReadOnlyCondition {
        QString key;
        QVariantList values;
    };
    bool readOnly = false;
    QList<ReadOnlyCondition> readOnlyWhen;
    bool readOnlyInverse = false;
    // Casilla en el título (parámetro Bool): el control solo se usa si está
    // marcada, como los canales de Posterización de Paint.NET.
    QString enableKey;
    // Pestaña del diálogo (vacío = sin pestañas), como Nubes/Colores.
    QString tab;
    // Degradado bajo el deslizador (Matiz, Temperatura...).
    QList<QColor> gradient;

    static EffectParam integer(const QString& key, const QString& label, int min, int max, int def);
    static EffectParam real(const QString& key, const QString& label, double min, double max, double def, int decimals = 2);
    static EffectParam boolean(const QString& key, const QString& label, bool def);
    static EffectParam choice(const QString& key, const QString& label, const QStringList& choices, int def = 0);
    static EffectParam seed(const QString& label = QString());
    static EffectParam angle(const QString& key, const QString& label, double min, double max, double def);
    static EffectParam offset(const QString& key, const QString& label);
    static EffectParam color(const QString& key, const QString& label, ColorDefault def);
    static EffectParam curves(const QString& key);
    static EffectParam levels(const QString& key);
    // x = ángulo (-180..180), y = dirección de la inclinación (-180..180), z = inclinación (-max..max).
    static EffectParam rollBall(const QString& key, const QString& label, double maxTilt = 90);
    static EffectParam text(const QString& key, const QString& label, const QString& def, bool multiline = false);
    EffectParam linked(const QString& boolKey) const;
    bool isReadOnly(const ParamValues& values) const;
};

// Valor del parámetro de tipo Curvas.
struct CurvesValue {
    enum Mode { Luminosity = 0, Rgb = 1 };
    int mode = Luminosity;
    // 0 = luminosidad, 1 = rojo, 2 = verde, 3 = azul. Puntos en 0..255.
    std::array<QList<QPointF>, 4> points;

    CurvesValue();
    std::array<uint8_t, 256> lut(int channel) const;
    bool operator==(const CurvesValue& o) const { return mode == o.mode && points == o.points; }
};
Q_DECLARE_METATYPE(CurvesValue)

// Valor del parámetro de tipo Niveles (por canal: 0 = rojo, 1 = verde, 2 = azul).
struct LevelsValue {
    std::array<int, 3> inLow{0, 0, 0};
    std::array<int, 3> inHigh{255, 255, 255};
    std::array<int, 3> outLow{0, 0, 0};
    std::array<int, 3> outHigh{255, 255, 255};
    std::array<double, 3> gamma{1, 1, 1};

    std::array<uint8_t, 256> lut(int channel) const;
    bool isIdentity() const;
    // Igual que el botón "Automático" y el ajuste Nivel automático de Paint.NET.
    static LevelsValue automatic(const QImage& image, const QRect& rect);
    bool operator==(const LevelsValue& o) const = default;
};
Q_DECLARE_METATYPE(LevelsValue)

// Avance del renderizado (lo consulta la barra de estado desde la interfaz).
struct EffectProgress {
    std::atomic<qint64> done{0};
    std::atomic<qint64> total{1};
    int permille() const
    {
        const qint64 t = std::max<qint64>(1, total.load());
        return int(std::min<qint64>(999, done.load() * 1000 / t));
    }
};

struct EffectContext {
    QColor primary = Qt::black;
    QColor secondary = Qt::white;
    QList<QColor> palette; // paleta actual (complementos de Paint.NET que la piden)
    EffectProgress* progress = nullptr;
};

struct RenderArgs {
    const QImage& src;
    QImage& dst;
    QRect roi; // límites de la selección: región a calcular y base de centros y radios
    const ParamValues& values;
    const EffectContext& ctx;
    const std::atomic_bool& cancel;
    const std::any& state; // lo que devolvió prepare()
};

class Effect {
public:
    virtual ~Effect() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString category() const { return {}; }   // vacío = menú Ajustes
    virtual bool isAdjustment() const { return category().isEmpty(); }
    virtual bool hidden() const { return false; }      // p. ej. Rotar/Zoom (menú Capas)
    virtual QKeySequence shortcut() const { return {}; }
    virtual QString iconName() const { return {}; }
    virtual QList<EffectParam> params() const { return {}; }
    // true: render() acepta píxeles con alfa directo (ajustes píxel a píxel,
    // exactos aunque el píxel sea transparente). false: Effects::apply le pasa
    // la capa convertida a premultiplicado (desenfoques, distorsiones...).
    virtual bool straightAlpha() const { return false; }
    // Efectos de complementos: nombre del complemento e icono propio.
    virtual QString pluginName() const { return {}; }
    virtual QImage iconImage() const { return {}; }
    // Texto de ayuda del complemento (botón "?" del diálogo).
    virtual QString helpText() const { return {}; }
    // Nota al pie del diálogo (p. ej. "Este efecto requiere una capa con transparencia...").
    virtual QString dialogNote() const { return {}; }
    // Complementos con diálogo propio, no descrito con parámetros: lo muestra
    // el propio complemento (bloquea hasta que se cierra). false si el
    // usuario cancela o falla (con el motivo en 'error').
    virtual bool hasCustomDialog() const { return false; }
    virtual bool showCustomDialog(const QImage& src, const QRect& roi, const EffectContext& ctx, QString* error) const;
    // Error del último render() (complementos que fallan); lo vacía al leerlo.
    virtual QString takeError() const { return {}; }

    // Precálculo opcional sobre la imagen completa (histogramas, paletas...).
    virtual std::any prepare(const QImage& src, const QRect& roi, const ParamValues& values,
                             const EffectContext& ctx) const;
    // Escribe en dst (copia de src) el resultado dentro de roi. Puede leer
    // src fuera de roi (desenfoques, distorsiones). Debe ser reentrante.
    virtual void render(const RenderArgs& args) const = 0;
};

namespace Effects {

// Efectos integrados y de los complementos cargados.
const QList<const Effect*>& all();
const Effect* find(const QString& id);
// Los complementos añaden y quitan sus efectos (el llamante conserva la propiedad).
void addExternal(const Effect* effect);
void removeExternal(const Effect* effect);
QStringList categories();
ParamValues defaults(const Effect& effect, const EffectContext& ctx = EffectContext());

// Ejecuta el efecto respetando la selección (fuera de ella la imagen queda
// igual y en los bordes suavizados se mezcla). Devuelve una imagen nula si
// se canceló. El resultado tiene el formato de 'src' (alfa directo para las
// capas); los píxeles que el efecto no cambia se conservan exactos.
QImage apply(const Effect& effect, const QImage& src, const Selection& selection, const ParamValues& values,
             const EffectContext& ctx, const std::atomic_bool& cancel);

} // namespace Effects
