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

// Descripción de un parámetro.
struct EffectParam {
    enum class Type {
        Int,
        Double,
        Bool,
        Choice,
        Seed,
        Angle,
        Offset,
        Color,
        Curves,
        Levels,
        RollBall,
        Text,
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
    QString linkKey;
    double minY = -1;
    double maxY = 1;
    QVariant defaultValue;
    QString description;
    bool multiline = false;
    struct ReadOnlyCondition {
        QString key;
        QVariantList values;
    };
    bool readOnly = false;
    QList<ReadOnlyCondition> readOnlyWhen;
    bool readOnlyInverse = false;
    QString enableKey;
    QString tab;
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
    static EffectParam rollBall(const QString& key, const QString& label, double maxTilt = 90);
    static EffectParam text(const QString& key, const QString& label, const QString& def, bool multiline = false);
    EffectParam linked(const QString& boolKey) const;
    bool isReadOnly(const ParamValues& values) const;
};

// Valor del parámetro de tipo Curvas.
struct CurvesValue {
    enum Mode { Luminosity = 0, Rgb = 1 };
    int mode = Luminosity;
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
    QList<QColor> palette;
    EffectProgress* progress = nullptr;
};

struct RenderArgs {
    const QImage& src;
    QImage& dst;
    QRect roi;
    const ParamValues& values;
    const EffectContext& ctx;
    const std::atomic_bool& cancel;
    const std::any& state;
};

class Effect {
public:
    virtual ~Effect() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString category() const { return {}; }
    virtual bool isAdjustment() const { return category().isEmpty(); }
    virtual bool hidden() const { return false; }
    virtual QKeySequence shortcut() const { return {}; }
    virtual QString iconName() const { return {}; }
    virtual QList<EffectParam> params() const { return {}; }
    virtual bool straightAlpha() const { return false; }
    virtual QString pluginName() const { return {}; }
    virtual QImage iconImage() const { return {}; }
    virtual QString helpText() const { return {}; }
    virtual QString dialogNote() const { return {}; }
    virtual bool hasCustomDialog() const { return false; }
    virtual bool showCustomDialog(const QImage& src, const QRect& roi, const EffectContext& ctx, QString* error) const;
    virtual QString takeError() const { return {}; }

    // Precálculo opcional sobre la imagen completa (histogramas, paletas...).
    virtual std::any prepare(const QImage& src, const QRect& roi, const ParamValues& values,
                             const EffectContext& ctx) const;
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

// Ejecuta el efecto respetando la selección (fuera de ella la imagen queda igual y en los
// bordes suavizados se mezcla).
QImage apply(const Effect& effect, const QImage& src, const Selection& selection, const ParamValues& values,
             const EffectContext& ctx, const std::atomic_bool& cancel);

} // namespace Effects
