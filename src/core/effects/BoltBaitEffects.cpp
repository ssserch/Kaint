// Efectos que siguen complementos de BoltBait para Paint.NET 5 que no pueden funcionar en Kaint
// (usan la API de efectos con Direct2D de Paint.NET 5): Kaint los hace a su manera, con el mismo
// nombre y controles parecidos. Se activan uno a uno desde Explorar complementos (sección
// Paint.NET) y llevan la pieza de puzle de los complementos. Los "clásicos" son los de Paint.NET
// 3.36 que Kaint ya trae (Detección de bordes y Nítido), como los de BoltBait.

#include "core/effects/ExtraUtil.h"

#include <QColor>

#include <cmath>

namespace fx {
namespace {

using namespace extra;
using P = EffectParam;

const QString kBoltBait = QStringLiteral("BoltBait");

// Otro efecto de Kaint con otro nombre (y en otro menú).
class AliasEffect : public Effect {
public:
    AliasEffect(QString id, QString name, QString category, QString base)
        : m_id(std::move(id)), m_name(std::move(name)), m_category(std::move(category)), m_base(std::move(base)) {}

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return m_category; }
    bool isAdjustment() const override { return m_category.isEmpty(); }
    bool hidden() const override { return !Effects::boltBaitEnabled(m_id) || !base(); }
    QString origin() const override { return kBoltBait; }
    QString iconName() const override { return base() ? base()->iconName() : QString(); }
    QList<EffectParam> params() const override { return base() ? base()->params() : QList<EffectParam>(); }
    bool straightAlpha() const override { return base() && base()->straightAlpha(); }
    std::any prepare(const QImage& src, const QRect& roi, const ParamValues& values, const EffectContext& ctx) const override
    {
        return base() ? base()->prepare(src, roi, values, ctx) : std::any();
    }
    void render(const RenderArgs& a) const override
    {
        if (base())
            base()->render(a);
    }

private:
    const Effect* base() const { return Effects::find(m_base); }
    QString m_id, m_name, m_category, m_base;
};

// Colores complementarios: el matiz opuesto con la misma luminosidad y saturación (HSL).
void complementary(float* c, const ParamValues&)
{
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]});
    for (int i = 0; i < 3; ++i)
        c[i] = mx + mn - c[i];
}

// Blanco y negro+: varias maneras de pasar a gris, con brillo y contraste.
void blackWhitePlus(FBuf& b, const RenderArgs& a)
{
    const int type = ival(a.values, "type");
    const BrightnessContrast bc(ival(a.values, "brightness"), ival(a.values, "contrast"));
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            float g;
            switch (type) {
            case 1: g = (p[0] + p[1] + p[2]) / 3.f; break;
            case 2: g = (std::max({p[0], p[1], p[2]}) + std::min({p[0], p[1], p[2]})) / 2.f; break;
            case 3: g = p[0]; break;
            case 4: g = p[1]; break;
            case 5: g = p[2]; break;
            case 6: g = std::max({p[0], p[1], p[2]}); break;
            case 7: g = std::min({p[0], p[1], p[2]}); break;
            default: g = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; break;
            }
            const int v = to8(g);
            const QRgb out = bc.apply(qRgba(v, v, v, 255));
            p[0] = qRed(out) / 255.f;
            p[1] = qGreen(out) / 255.f;
            p[2] = qBlue(out) / 255.f;
        }
}

// Ajustes combinados: brillo, contraste, saturación y temperatura de una vez.
void combined(FBuf& b, const RenderArgs& a)
{
    const BrightnessContrast bc(ival(a.values, "brightness"), ival(a.values, "contrast"));
    const float sat = 1.f + ival(a.values, "saturation") / 100.f;
    const float warm = ival(a.values, "temperature") / 100.f * 0.12f;
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            const QRgb t = bc.apply(qRgba(to8(p[0]), to8(p[1]), to8(p[2]), 255));
            float c[3] = {qRed(t) / 255.f, qGreen(t) / 255.f, qBlue(t) / 255.f};
            const float l = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            for (float& v : c)
                v = l + (v - l) * sat;
            c[0] += warm;
            c[2] -= warm;
            for (int i = 0; i < 3; ++i)
                p[i] = std::clamp(c[i], 0.f, 1.f);
        }
}

// Sombra paralela+: la silueta de la capa, desplazada, desenfocada y del color elegido, debajo de
// la capa (o sola).
void dropShadow(FBuf& b, const RenderArgs& a)
{
    const int dx = ival(a.values, "offsetX"), dy = ival(a.values, "offsetY");
    const float radius = float(ival(a.values, "blur"));
    const float opacity = ival(a.values, "opacity") / 100.f;
    const QColor color = cval(a.values, "color");
    const bool only = bval(a.values, "shadowOnly");
    FBuf shadow = b;
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const int sx = x - dx, sy = y - dy;
            float* s = shadow.at(x, y);
            const float al = a.roi.contains(sx, sy) ? b.at(sx, sy)[3] : 0.f;
            s[0] = float(color.redF());
            s[1] = float(color.greenF());
            s[2] = float(color.blueF());
            s[3] = al * opacity;
        }
    gaussian(shadow, a.roi, radius / 2.f, a.cancel);
    for (int y = a.roi.top(); y <= a.roi.bottom(); ++y)
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            float* p = b.at(x, y);
            const float* s = shadow.at(x, y);
            if (only) {
                std::copy_n(s, 4, p);
                continue;
            }
            // La capa encima de la sombra.
            const float ta = p[3], sa = s[3] * (1.f - ta), outA = ta + sa;
            for (int c = 0; c < 3; ++c)
                p[c] = outA > 1e-6f ? (p[c] * ta + s[c] * sa) / outA : 0.f;
            p[3] = outA;
        }
}

// Nivelar el horizonte: gira la zona alrededor de su centro y, si se pide, la amplía para que no
// queden esquinas vacías.
void levelHorizon(FBuf& b, const RenderArgs& a)
{
    const double angle = dval(a.values, "angle") * M_PI / 180.0;
    const QRect r = a.roi;
    const double cx = r.left() + r.width() / 2.0, cy = r.top() + r.height() / 2.0;
    double zoom = 1.0;
    if (bval(a.values, "fill")) {
        const double w = r.width(), h = r.height(), c = std::abs(std::cos(angle)), s = std::abs(std::sin(angle));
        zoom = std::max((w * c + h * s) / w, (w * s + h * c) / h);
    }
    const double ca = std::cos(-angle) / zoom, sa = std::sin(-angle) / zoom;
    b = remap(b, r, Edge::Transparent, a.cancel, [&](float x, float y) {
        const double dx = x - cx, dy = y - cy;
        return QPointF(cx + dx * ca - dy * sa, cy + dx * sa + dy * ca);
    });
}

} // namespace

void registerBoltBait(QList<const Effect*>& list)
{
    list.append(new AliasEffect(QStringLiteral("bbEdgeDetectClassic"), QObject::tr("Detección de bordes clásica"), catStylize(),
                                QStringLiteral("edgedetect")));
    list.append(new AliasEffect(QStringLiteral("bbSharpenClassic"), QObject::tr("Nitidez clásica"), catPhoto(), QStringLiteral("sharpen")));
    auto add = [&](SimpleEffect::Def def) {
        def.origin = kBoltBait;
        def.straight = true;
        list.append(new SimpleEffect(std::move(def)));
    };
    add({"bbComplementaryColors", QObject::tr("Colores complementarios"), catColor(), "color-management", {}, {}, perPixel(complementary)});
    add({"bbBlackAndWhitePlus", QObject::tr("Blanco y negro+"), {}, "color-mode-black-white", {},
         {P::choice("type", QObject::tr("Tipo"),
                    {QObject::tr("Luminosidad"), QObject::tr("Promedio"), QObject::tr("Desaturar"), QObject::tr("Canal rojo"), QObject::tr("Canal verde"),
                     QObject::tr("Canal azul"), QObject::tr("Máximo"), QObject::tr("Mínimo")},
                    0),
          P::integer("brightness", QObject::tr("Brillo"), -100, 100, 0), P::integer("contrast", QObject::tr("Contraste"), -100, 100, 0)},
         whole(blackWhitePlus)});
    add({"bbCombinedAdjustments", QObject::tr("Ajustes combinados"), {}, "adjustrgb", {},
         {P::integer("brightness", QObject::tr("Brillo"), -100, 100, 0), P::integer("contrast", QObject::tr("Contraste"), -100, 100, 0),
          P::integer("saturation", QObject::tr("Saturación"), -100, 100, 0), P::integer("temperature", QObject::tr("Temperatura"), -100, 100, 0)},
         whole(combined)});
    add({"bbDropShadowPlus", QObject::tr("Sombra paralela+"), catObject(), "draw-rectangle", {},
         {P::integer("offsetX", QObject::tr("Desplazamiento horizontal"), -200, 200, 6), P::integer("offsetY", QObject::tr("Desplazamiento vertical"), -200, 200, 6),
          P::integer("blur", QObject::tr("Desenfoque"), 0, 100, 10), P::integer("opacity", QObject::tr("Opacidad"), 0, 100, 60),
          P::color("color", QObject::tr("Color"), P::ColorDefault::Black), P::boolean("shadowOnly", QObject::tr("Solo la sombra"), false)},
         whole(dropShadow)});
    add({"bbLevelHorizon", QObject::tr("Nivelar el horizonte"), catPhoto(), "transform-rotate", {},
         {P::real("angle", QObject::tr("Ángulo"), -45, 45, 0, 2), P::boolean("fill", QObject::tr("Ampliar para rellenar"), true)}, whole(levelHorizon)});
}

} // namespace fx
