#include "core/plugins/PluginEffects.h"

#include "core/Parallel.h"
#include "core/effects/EffectUtils.h"
#include "core/plugins/PdnBridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

#include <cmath>

// ---------------------------------------------------------------------------
// Complementos de Paint.NET
// ---------------------------------------------------------------------------

namespace {

QVariant jsonToVariant(const QJsonValue& v)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble();
    return v.toString();
}

// Decimales que caben en el deslizador (rango entero de QSlider).
int fitDecimals(double min, double max, int decimals)
{
    decimals = std::clamp(decimals, 0, 4);
    while (decimals > 0 && (max - min) * std::pow(10.0, decimals) > 2.0e9)
        --decimals;
    return decimals;
}

} // namespace

QString PdnEffect::kaintCategory(const QString& subMenu)
{
    static const QHash<QString, QString> english{
        {QStringLiteral("Artistic"), fx::catArtistic()}, {QStringLiteral("Blurs"), fx::catBlurs()},
        {QStringLiteral("Color"), fx::catColor()},       {QStringLiteral("Distort"), fx::catDistort()},
        {QStringLiteral("Noise"), fx::catNoise()},       {QStringLiteral("Object"), fx::catObject()},
        {QStringLiteral("Photo"), fx::catPhoto()},       {QStringLiteral("Render"), fx::catRender()},
        {QStringLiteral("Stylize"), fx::catStylize()},
    };
    return english.value(subMenu, QCoreApplication::translate("QObject", subMenu.toUtf8().constData()));
}

PdnEffect::PdnEffect(const QString& assembly, const QString& pluginName, const QJsonObject& d)
    : m_assembly(assembly)
    , m_typeId(d.value(QStringLiteral("id")).toString())
    , m_name(d.value(QStringLiteral("name")).toString().simplified())
    , m_adjustment(d.value(QStringLiteral("adjustment")).toBool())
    , m_hidden(d.value(QStringLiteral("hidden")).toBool())
    , m_customDialog(d.value(QStringLiteral("kind")).toString() == QLatin1String("dialog"))
    , m_pluginName(pluginName)
    , m_help(d.value(QStringLiteral("help")).toString())
{
    m_id = QStringLiteral("pdn:%1:%2").arg(QFileInfo(assembly).fileName(), m_typeId);
    if (!m_adjustment)
        m_category = kaintCategory(d.value(QStringLiteral("subMenu")).toString());
    const QByteArray icon = QByteArray::fromBase64(d.value(QStringLiteral("icon")).toString().toLatin1());
    if (!icon.isEmpty())
        m_icon.loadFromData(icon, "PNG");

    for (const QJsonValue& v : d.value(QStringLiteral("params")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("hidden")).toBool())
            continue;
        const QString key = o.value(QStringLiteral("name")).toString();
        const QString label = o.value(QStringLiteral("label")).toString();
        const QString type = o.value(QStringLiteral("type")).toString();
        const QString control = o.value(QStringLiteral("control")).toString();
        const double min = o.value(QStringLiteral("min")).toDouble();
        const double max = o.value(QStringLiteral("max")).toDouble();
        EffectParam p;
        if (type == QLatin1String("bool")) {
            p = EffectParam::boolean(key, label, o.value(QStringLiteral("default")).toBool());
        } else if (type == QLatin1String("int")) {
            const int def = o.value(QStringLiteral("default")).toInt();
            if (control == QLatin1String("IncrementButton")) {
                p = EffectParam::seed(label);
                p.key = key;
            } else if (control == QLatin1String("ColorWheel")) {
                const bool alpha = min < 0 || max > 0xffffff;
                m_colorAlpha.insert(key, alpha);
                p = EffectParam::color(key, label, EffectParam::ColorDefault::Primary);
                p.defaultValue = QColor::fromRgba(alpha ? QRgb(uint32_t(def)) : (QRgb(uint32_t(def)) | 0xff000000u));
            } else {
                p = EffectParam::integer(key, label, int(min), int(max), def);
            }
        } else if (type == QLatin1String("double")) {
            const double def = o.value(QStringLiteral("default")).toDouble();
            const int decimals = fitDecimals(min, max, o.value(QStringLiteral("decimals")).toInt(2));
            if (control == QLatin1String("AngleChooser"))
                p = EffectParam::angle(key, label, min, max, def);
            else
                p = EffectParam::real(key, label, min, max, def, decimals);
        } else if (type == QLatin1String("vector")) {
            const QJsonArray mn = o.value(QStringLiteral("min")).toArray();
            const QJsonArray mx = o.value(QStringLiteral("max")).toArray();
            const QJsonArray df = o.value(QStringLiteral("default")).toArray();
            p = EffectParam::offset(key, label);
            p.min = mn.at(0).toDouble(-1);
            p.max = mx.at(0).toDouble(1);
            p.minY = mn.at(1).toDouble(-1);
            p.maxY = mx.at(1).toDouble(1);
            p.decimals = fitDecimals(std::min(p.min, p.minY), std::max(p.max, p.maxY),
                                     o.value(QStringLiteral("decimals")).toInt(2));
            p.defaultValue = QPointF(df.at(0).toDouble(), df.at(1).toDouble());
        } else if (type == QLatin1String("choice")) {
            QStringList choices;
            for (const QJsonValue& c : o.value(QStringLiteral("choices")).toArray())
                choices << c.toString();
            p = EffectParam::choice(key, label, choices, o.value(QStringLiteral("default")).toInt());
        } else if (type == QLatin1String("string")) {
            p = EffectParam::text(key, label, o.value(QStringLiteral("default")).toString(),
                                  o.value(QStringLiteral("multiline")).toBool());
            p.max = o.value(QStringLiteral("maxLength")).toDouble();
        } else {
            continue;
        }
        p.description = o.value(QStringLiteral("description")).toString();
        const QString style = o.value(QStringLiteral("sliderStyle")).toString();
        if (style.startsWith(QLatin1String("Hue")) || style == QLatin1String("SaturationHue")) {
            const bool centered = style == QLatin1String("HueCentered");
            for (int i = 0; i <= 6; ++i)
                p.gradient << QColor::fromHsv(((centered ? 180 : 0) + i * 60) % 360, 255, 255);
        }
        p.readOnly = o.value(QStringLiteral("readOnly")).toBool();
        m_params.append(p);
    }

    // Reglas de IndirectUI: solo lectura según otros valores y valores vinculados.
    for (const QJsonValue& v : d.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString type = r.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("readOnly")) {
            const QString target = r.value(QStringLiteral("target")).toString();
            for (EffectParam& p : m_params) {
                if (p.key != target)
                    continue;
                for (const QJsonValue& c : r.value(QStringLiteral("conditions")).toArray()) {
                    EffectParam::ReadOnlyCondition cond;
                    cond.key = c.toObject().value(QStringLiteral("source")).toString();
                    for (const QJsonValue& value : c.toObject().value(QStringLiteral("values")).toArray())
                        cond.values << jsonToVariant(value);
                    p.readOnlyWhen << cond;
                }
                p.readOnlyInverse = r.value(QStringLiteral("inverse")).toBool();
            }
        } else if (type == QLatin1String("link") && !r.value(QStringLiteral("inverse")).toBool()) {
            const QString source = r.value(QStringLiteral("source")).toString();
            for (const QJsonValue& t : r.value(QStringLiteral("targets")).toArray())
                for (EffectParam& p : m_params)
                    if (p.key == t.toString())
                        p.linkKey = source;
        }
    }
}

QJsonObject PdnEffect::toHostValues(const ParamValues& values) const
{
    QJsonObject out;
    for (const EffectParam& p : m_params) {
        const QVariant v = values.value(p.key);
        if (!v.isValid())
            continue;
        switch (p.type) {
        case EffectParam::Type::Bool: out.insert(p.key, v.toBool()); break;
        case EffectParam::Type::Int:
        case EffectParam::Type::Seed:
        case EffectParam::Type::Choice: out.insert(p.key, v.toInt()); break;
        case EffectParam::Type::Double:
        case EffectParam::Type::Angle: out.insert(p.key, v.toDouble()); break;
        case EffectParam::Type::Offset: out.insert(p.key, QJsonArray{v.toPointF().x(), v.toPointF().y()}); break;
        case EffectParam::Type::Text: out.insert(p.key, v.toString()); break;
        case EffectParam::Type::Color: {
            const QRgb c = v.value<QColor>().rgba();
            out.insert(p.key, m_colorAlpha.value(p.key) ? qint64(int32_t(c)) : qint64(c & 0xffffffu));
            break;
        }
        default: break;
        }
    }
    return out;
}

void PdnEffect::render(const RenderArgs& a) const
{
    QString error;
    const bool ok = PdnBridge::instance().render(m_assembly, m_typeId, toHostValues(a.values), a.src, a.dst, a.roi,
                                                 a.ctx, a.cancel, &error);
    if (!ok && !error.isEmpty()) {
        std::lock_guard lock(m_errorMutex);
        m_error = error;
    }
}

QString PdnEffect::takeError() const
{
    std::lock_guard lock(m_errorMutex);
    QString e = m_error;
    m_error.clear();
    return e;
}

bool PdnEffect::showCustomDialog(const QImage& src, const QRect& roi, const EffectContext& ctx, QString* error) const
{
    bool accepted = false;
    if (!PdnBridge::instance().showDialog(m_assembly, m_typeId, m_name, src, roi, ctx, &accepted, error))
        return false;
    return accepted;
}

// ---------------------------------------------------------------------------
// Complementos nativos
// ---------------------------------------------------------------------------

NativeEffect::NativeEffect(const KaintEffectInfo& info, const QString& pluginName)
    : m_id(QStringLiteral("native:") + QString::fromUtf8(info.id ? info.id : ""))
    , m_name(QString::fromUtf8(info.name ? info.name : info.id))
    , m_category(info.category ? PdnEffect::kaintCategory(QString::fromUtf8(info.category)) : QString())
    , m_pluginName(pluginName)
    , m_render(info.render)
{
    for (int i = 0; i < info.param_count && info.params; ++i) {
        const KaintParam& kp = info.params[i];
        const QString key = QString::fromUtf8(kp.key);
        const QString label = QString::fromUtf8(kp.label ? kp.label : kp.key);
        EffectParam p;
        switch (kp.type) {
        case KAINT_PARAM_DOUBLE: p = EffectParam::real(key, label, kp.min, kp.max, kp.def, kp.decimals); break;
        case KAINT_PARAM_BOOL: p = EffectParam::boolean(key, label, kp.def != 0); break;
        case KAINT_PARAM_CHOICE:
            p = EffectParam::choice(key, label, QString::fromUtf8(kp.choices ? kp.choices : "").split(QLatin1Char('\n')),
                                    int(kp.def));
            break;
        case KAINT_PARAM_ANGLE: p = EffectParam::angle(key, label, kp.min, kp.max, kp.def); break;
        case KAINT_PARAM_COLOR:
            p = EffectParam::color(key, label, EffectParam::ColorDefault::Primary);
            p.defaultValue = QColor::fromRgba(QRgb(uint32_t(kp.def)));
            break;
        case KAINT_PARAM_SEED:
            p = EffectParam::seed(label);
            p.key = key;
            break;
        default: p = EffectParam::integer(key, label, int(kp.min), int(kp.max), int(kp.def)); break;
        }
        m_params.append(p);
    }
}

void NativeEffect::render(const RenderArgs& a) const
{
    if (!m_render)
        return;
    std::vector<double> values;
    values.reserve(size_t(m_params.size()));
    for (const EffectParam& p : m_params) {
        const QVariant v = a.values.value(p.key);
        values.push_back(p.type == EffectParam::Type::Color ? double(v.value<QColor>().rgba()) : v.toDouble());
    }
    const KaintImage src{a.src.width(), a.src.height(), int(a.src.bytesPerLine()), const_cast<uchar*>(a.src.constBits())};
    KaintImage dst{a.dst.width(), a.dst.height(), int(a.dst.bytesPerLine()), a.dst.bits()};
    volatile int cancelled = 0;
    std::atomic<qint64> done{0};
    parallelRows(a.roi.top(), a.roi.bottom() + 1, [&](int y0, int y1) {
        if (a.cancel) {
            cancelled = 1;
            return;
        }
        const KaintRenderArgs args{&src, &dst, a.roi.left(), y0, a.roi.width(), y1 - y0, values.data(),
                                   uint32_t(a.ctx.primary.rgba()), uint32_t(a.ctx.secondary.rgba()), &cancelled};
        m_render(&args);
        if (a.ctx.progress)
            a.ctx.progress->done = done += (y1 - y0);
    });
}
