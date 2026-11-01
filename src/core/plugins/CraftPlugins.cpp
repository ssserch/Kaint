#include "core/plugins/CraftPlugins.h"

#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Pixels.h"
#include "core/plugins/Wasm.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <utility>

namespace CraftPlugins {

namespace {

QString category()
{
    return QObject::tr("Complementos");
}

// "keepLight" o "edge_width" → "Keep light", "Edge width": los parámetros de los filtros no traen
// etiqueta.
QString humanize(const QString& key)
{
    QString out;
    for (int i = 0; i < key.size(); ++i) {
        const QChar c = key[i];
        if (c == QLatin1Char('_') || c == QLatin1Char('-')) {
            out += QLatin1Char(' ');
            continue;
        }
        if (c.isUpper() && i > 0 && key[i - 1].isLower())
            out += QLatin1Char(' ');
        out += out.isEmpty() ? c.toUpper() : (c.isUpper() && i > 0 ? c.toLower() : c);
    }
    return out.simplified();
}

QString shortHash(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

float srgbToLinear(float v)
{
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float v)
{
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}

float clamp01(float v)
{
    return std::clamp(v, 0.f, 1.f);
}

int toByte(float v)
{
    return int(std::lround(clamp01(v) * 255.f));
}

// ===========================================================================================
// Filtros WebAssembly (interfaz de complementos v1 de PhotoCraft)
// ===========================================================================================

constexpr int kAbiVersion = 1;
// "format" de pc_filter para las capas de Kaint: 8 bits de origen, alfa directo y modo RGB (el 3
// de la numeración de PSD).
constexpr int kFormatRgba8 = 8 | (1 << 8) | (3 << 16);
constexpr int kMaxOverlap = 256;
// Hasta unos 16 millones de píxeles (4K y algo más) van de una vez: algunos filtros se colocan
// según el tamaño de lo que reciben. Más grandes, en cuadros con el margen que pidan.
constexpr qint64 kBandBytes = qint64(256) << 20;

// Las claves del objeto "params" en el orden del texto (QJsonObject las ordena alfabéticamente y
// el diálogo debe seguir el orden en que el complemento las declara).
QStringList paramOrder(const QByteArray& text)
{
    QStringList keys;
    std::vector<char> stack;
    QString str;
    bool haveString = false;
    bool nextIsParams = false;
    int paramsLevel = -1;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            QByteArray raw;
            for (++i; i < text.size() && text[i] != '"'; ++i) {
                if (text[i] == '\\' && i + 1 < text.size())
                    raw += text[i++];
                raw += text[i];
            }
            str = QJsonDocument::fromJson("[\"" + raw + "\"]").array().at(0).toString();
            haveString = true;
            continue;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == ':') {
            if (haveString && !stack.empty() && stack.back() == '{') {
                const int level = int(stack.size());
                if (level == paramsLevel)
                    keys << str;
                nextIsParams = level == 1 && str == QLatin1String("params");
            }
            haveString = false;
            continue;
        }
        if (c == '{' || c == '[') {
            stack.push_back(c);
            if (nextIsParams && c == '{')
                paramsLevel = int(stack.size());
            nextIsParams = false;
        } else if (c == '}' || c == ']') {
            if (int(stack.size()) == paramsLevel)
                paramsLevel = -2;
            if (!stack.empty())
                stack.pop_back();
        } else if (c != ',') {
            nextIsParams = false;
        }
        haveString = false;
    }
    return keys;
}

// Un parámetro declarado en el manifiesto.
struct ParamSpec {
    enum Type { Number, Int, Bool, Choice } type = Number;
    QString key;
    double min = 0;
    double max = 1;
    double def = 0;
    bool pixels = false;
    QStringList options;
};

struct Manifest {
    QString id;
    QString name;
    QString version;
    QString author;
    QString description;
    QList<ParamSpec> params;
    int overlap = 0;
    bool canvasArea = false;
};

bool validId(const QString& id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    return re.match(id).hasMatch();
}

bool parseManifest(const QByteArray& text, Manifest& m, QString* error)
{
    if (text.size() > 64 * 1024) {
        *error = QObject::tr("El manifiesto del complemento es demasiado grande.");
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(text, &pe);
    if (!doc.isObject()) {
        *error = QObject::tr("El manifiesto del complemento no es JSON válido: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    m.id = o.value(QStringLiteral("id")).toString();
    m.name = o.value(QStringLiteral("name")).toString().simplified();
    m.version = o.value(QStringLiteral("version")).toString().left(32);
    m.author = o.value(QStringLiteral("author")).toString().left(128);
    m.description = o.value(QStringLiteral("description")).toString().left(1024);
    m.overlap = o.value(QStringLiteral("overlap")).toInt(0);
    m.canvasArea = o.value(QStringLiteral("area")).toString() == QLatin1String("canvas");
    if (!validId(m.id)) {
        *error = QObject::tr("El identificador «%1» del complemento no vale: usa de 1 a 64 letras, números, puntos, "
                             "guiones o guiones bajos.").arg(m.id);
        return false;
    }
    if (m.name.isEmpty() || m.name.size() > 64) {
        *error = QObject::tr("El nombre del complemento debe tener de 1 a 64 caracteres.");
        return false;
    }
    if (o.value(QStringLiteral("kind")).toString() != QLatin1String("filter")) {
        *error = QObject::tr("Solo se admiten complementos de tipo «filter».");
        return false;
    }
    if (m.overlap < 0 || m.overlap > kMaxOverlap) {
        *error = QObject::tr("El margen («overlap») del complemento pasa de %1 píxeles.").arg(kMaxOverlap);
        return false;
    }
    const QJsonObject params = o.value(QStringLiteral("params")).toObject();
    QStringList order = paramOrder(text);
    for (const QString& key : params.keys())
        if (!order.contains(key))
            order << key;
    if (order.size() > 64) {
        *error = QObject::tr("El complemento declara demasiados parámetros.");
        return false;
    }
    for (const QString& key : std::as_const(order)) {
        const QJsonObject p = params.value(key).toObject();
        ParamSpec spec;
        spec.key = key;
        const QString type = p.value(QStringLiteral("type")).toString();
        spec.pixels = p.value(QStringLiteral("unit")).toString() == QLatin1String("px");
        if (type == QLatin1String("number") || type == QLatin1String("int")) {
            spec.type = type == QLatin1String("int") ? ParamSpec::Int : ParamSpec::Number;
            const double big = spec.type == ParamSpec::Int ? 1e6 : 0;
            spec.min = p.value(QStringLiteral("min")).toDouble(-big);
            spec.max = p.value(QStringLiteral("max")).toDouble(big);
            spec.def = p.value(QStringLiteral("default")).toDouble(0);
            if (!std::isfinite(spec.min) || !std::isfinite(spec.max) || spec.min > spec.max) {
                *error = QObject::tr("El parámetro «%1» del complemento tiene un rango que no vale.").arg(key);
                return false;
            }
            spec.def = std::clamp(spec.def, spec.min, spec.max);
        } else if (type == QLatin1String("bool")) {
            spec.type = ParamSpec::Bool;
            spec.def = p.value(QStringLiteral("default")).toBool() ? 1 : 0;
        } else if (type == QLatin1String("choice")) {
            spec.type = ParamSpec::Choice;
            for (const QJsonValue& v : p.value(QStringLiteral("options")).toArray())
                spec.options << v.toString();
            if (spec.options.isEmpty() || spec.options.size() > 64 || spec.options.contains(QString())) {
                *error = QObject::tr("El parámetro «%1» del complemento no tiene opciones válidas.").arg(key);
                return false;
            }
            spec.def = std::max<qsizetype>(0, spec.options.indexOf(p.value(QStringLiteral("default")).toString()));
        } else {
            *error = QObject::tr("El parámetro «%1» del complemento es de un tipo desconocido.").arg(key);
            return false;
        }
        m.params << spec;
    }
    return true;
}

class WasmFilterEffect : public Effect {
public:
    WasmFilterEffect(std::shared_ptr<const Wasm::Module> module, Manifest manifest)
        : m_module(std::move(module))
        , m_manifest(std::move(manifest))
    {
        for (const ParamSpec& p : m_manifest.params) {
            QString label = humanize(p.key);
            if (p.pixels)
                label += QStringLiteral(" (px)");
            switch (p.type) {
            case ParamSpec::Number: {
                const double span = p.max - p.min;
                const int decimals = span >= 100 ? 1 : span >= 10 ? 2 : 3;
                m_params << EffectParam::real(p.key, label, p.min, p.max, p.def, decimals);
                break;
            }
            case ParamSpec::Int:
                m_params << EffectParam::integer(p.key, label, int(p.min), int(p.max), int(p.def));
                break;
            case ParamSpec::Bool: m_params << EffectParam::boolean(p.key, label, p.def != 0); break;
            case ParamSpec::Choice: m_params << EffectParam::choice(p.key, label, p.options, int(p.def)); break;
            }
        }
    }

    QString id() const override { return QStringLiteral("craft.wasm.") + m_manifest.id; }
    QString name() const override { return m_manifest.name; }
    QString category() const override { return CraftPlugins::category(); }
    bool isAdjustment() const override { return false; }
    QString iconName() const override { return QStringLiteral("application-x-addon"); }
    QList<EffectParam> params() const override { return m_params; }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_manifest.name; }
    QString helpText() const override { return m_manifest.description; }

    QString takeError() const override
    {
        std::lock_guard lock(m_errorMutex);
        return std::exchange(m_error, QString());
    }

    void render(const RenderArgs& a) const override
    {
        const QJsonObject values = jsonValues(a.values);
        const QRect area = outputArea(a.src, a.roi);
        if (area.isEmpty())
            return;
        const int ov = m_manifest.overlap;
        // Cuadros cuyo contenido (con el margen) quepa en el presupuesto; con una imagen normal,
        // uno solo.
        const int side = std::max(16, int(std::sqrt(double(kBandBytes / 16))) - 2 * ov);
        QList<QRect> tiles;
        for (int y = area.top(); y <= area.bottom(); y += side)
            for (int x = area.left(); x <= area.right(); x += side)
                tiles << QRect(x, y, std::min(side, area.right() + 1 - x), std::min(side, area.bottom() + 1 - y));
        std::mutex mutex;
        QString firstError;
        parallelRows(0, int(tiles.size()), [&](int t0, int t1) {
            for (int t = t0; t < t1 && !a.cancel; ++t) {
                QString error;
                if (!runTile(a, tiles[t], values, &error)) {
                    std::lock_guard lock(mutex);
                    if (firstError.isEmpty())
                        firstError = error;
                }
                if (a.ctx.progress)
                    a.ctx.progress->done += qint64(tiles[t].height()) * tiles[t].width() / std::max(1, area.width());
            }
        }, 1);
        if (!firstError.isEmpty() && !a.cancel) {
            std::lock_guard lock(m_errorMutex);
            m_error = firstError;
        }
    }

private:
    // Lo que se escribe: el contenido de la capa (lo que no es transparente, con el margen) o, con
    // "area": "canvas", todo el lienzo; siempre dentro de la selección.
    QRect outputArea(const QImage& src, const QRect& roi) const
    {
        if (m_manifest.canvasArea)
            return roi;
        int x0 = src.width(), y0 = src.height(), x1 = -1, y1 = -1;
        for (int y = 0; y < src.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < src.width(); ++x)
                if (qAlpha(row[x])) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        }
        if (x1 < 0)
            return {};
        const int ov = m_manifest.overlap;
        return QRect(QPoint(x0, y0), QPoint(x1, y1)).adjusted(-ov, -ov, ov, ov).intersected(src.rect()).intersected(roi);
    }

    // Los valores del diálogo como los espera el complemento (las listas, por su texto).
    QJsonObject jsonValues(const ParamValues& values) const
    {
        QJsonObject o;
        for (const ParamSpec& p : m_manifest.params) {
            const QVariant v = values.value(p.key);
            switch (p.type) {
            case ParamSpec::Number: o.insert(p.key, std::clamp(v.isValid() ? v.toDouble() : p.def, p.min, p.max)); break;
            case ParamSpec::Int: o.insert(p.key, qint64(std::llround(std::clamp(v.isValid() ? v.toDouble() : p.def, p.min, p.max)))); break;
            case ParamSpec::Bool: o.insert(p.key, v.isValid() ? v.toBool() : p.def != 0); break;
            case ParamSpec::Choice: {
                const int i = std::clamp(v.isValid() ? v.toInt() : int(p.def), 0, int(p.options.size()) - 1);
                o.insert(p.key, p.options.at(i));
                break;
            }
            }
        }
        return o;
    }

    // Un cuadro: se le pasa con su margen (los píxeles de fuera de la imagen repiten el borde) en
    // una instancia nueva, y se recogen sus píxeles; un valor que no es un número deja el original.
    bool runTile(const RenderArgs& a, const QRect& tile, QJsonObject values, QString* error) const
    {
        const int ov = m_manifest.overlap;
        const QRect input = tile.adjusted(-ov, -ov, ov, ov);
        const int w = input.width(), h = input.height();
        std::vector<float> data(size_t(w) * size_t(h) * 4);
        const QImage& src = a.src;
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(std::clamp(input.top() + y, 0, src.height() - 1)));
            float* d = &data[size_t(y) * size_t(w) * 4];
            for (int x = 0; x < w; ++x, d += 4) {
                const QRgb p = row[std::clamp(input.left() + x, 0, src.width() - 1)];
                d[0] = qRed(p) / 255.f;
                d[1] = qGreen(p) / 255.f;
                d[2] = qBlue(p) / 255.f;
                d[3] = qAlpha(p) / 255.f;
            }
        }
        values.insert(QStringLiteral("_image"),
                      QJsonObject{{QStringLiteral("x"), input.left()}, {QStringLiteral("y"), input.top()},
                                  {QStringLiteral("width"), w}, {QStringLiteral("height"), h},
                                  {QStringLiteral("overlap"), ov}, {QStringLiteral("canvasWidth"), src.width()},
                                  {QStringLiteral("canvasHeight"), src.height()}, {QStringLiteral("mode"), QStringLiteral("rgb")},
                                  {QStringLiteral("depth"), 8}, {QStringLiteral("alpha"), true}, {QStringLiteral("channels"), 4}});
        const QByteArray params = QJsonDocument(values).toJson(QJsonDocument::Compact);
        const uint64_t bytes = uint64_t(data.size()) * 4;
        Wasm::Limits limits;
        limits.cancel = &a.cancel;
        limits.fuel = 200'000'000ULL + uint64_t(data.size()) * 4000ULL;
        auto instance = Wasm::Instance::create(m_module, limits, error);
        if (!instance)
            return false;
        // La memoria con la que empieza el módulo (su pila y sus datos) se reserva antes: algunos
        // complementos reparten desde dentro de ella y si no pisarían sus propios datos.
        const uint64_t reserve = uint64_t(instance->memoryPages()) * 65536;
        if (bytes + uint64_t(params.size()) + reserve > (uint64_t(1) << 30) || bytes > uint64_t(INT32_MAX)) {
            *error = QObject::tr("La imagen no cabe en la memoria del complemento.");
            return false;
        }
        std::vector<uint64_t> out;
        auto alloc = [&](uint64_t size, uint32_t* ptr) {
            if (!instance->call("pc_alloc", {uint64_t(uint32_t(size))}, &out, error) || out.empty())
                return false;
            *ptr = uint32_t(out[0]);
            return true;
        };
        uint32_t ignored = 0, ptr = 0, pptr = 0;
        if (reserve && !alloc(reserve, &ignored))
            return false;
        if (!alloc(bytes, &ptr) || !alloc(std::max<uint64_t>(1, uint64_t(params.size())), &pptr))
            return false;
        if (ptr == 0 || pptr == 0) {
            *error = QObject::tr("El complemento se quedó sin memoria (pc_alloc devolvió 0).");
            return false;
        }
        if (uint64_t(ptr) + bytes > instance->memorySize() || uint64_t(pptr) + uint64_t(params.size()) > instance->memorySize()) {
            *error = QObject::tr("pc_alloc devolvió un bloque fuera de la memoria del complemento.");
            return false;
        }
        std::memcpy(instance->memory() + ptr, data.data(), size_t(bytes));
        std::memcpy(instance->memory() + pptr, params.constData(), size_t(params.size()));
        const std::vector<uint64_t> args{ptr, uint64_t(uint32_t(bytes)), uint64_t(uint32_t(w)), uint64_t(uint32_t(h)), 4,
                                         uint64_t(uint32_t(kFormatRgba8)), pptr, uint64_t(uint32_t(params.size()))};
        if (!instance->call("pc_filter", args, &out, error))
            return false;
        if (!out.empty() && uint32_t(out[0]) != 0) {
            *error = QObject::tr("El complemento devolvió el código de error %1.").arg(int32_t(uint32_t(out[0])));
            return false;
        }
        if (uint64_t(ptr) + bytes > instance->memorySize()) {
            *error = QObject::tr("Los píxeles ya no están en la memoria del complemento.");
            return false;
        }
        const float* result = reinterpret_cast<const float*>(instance->memory() + ptr);
        const Pixels dst(a.dst);
        for (int y = 0; y < tile.height(); ++y) {
            QRgb* row = dst.row(tile.top() + y) + tile.left();
            const size_t at = (size_t(y + ov) * size_t(w) + size_t(ov)) * 4;
            for (int x = 0; x < tile.width(); ++x) {
                float c[4];
                std::memcpy(c, result + at + size_t(x) * 4, sizeof c);
                for (int k = 0; k < 4; ++k)
                    if (!std::isfinite(c[k]))
                        c[k] = data[at + size_t(x) * 4 + size_t(k)];
                row[x] = qRgba(toByte(c[0]), toByte(c[1]), toByte(c[2]), toByte(c[3]));
            }
        }
        return true;
    }

    std::shared_ptr<const Wasm::Module> m_module;
    Manifest m_manifest;
    QList<EffectParam> m_params;
    mutable std::mutex m_errorMutex;
    mutable QString m_error;
};

Info loadWasm(const QString& path)
{
    Info info;
    info.kind = QStringLiteral("PhotoCraft");
    QFile file(path);
    if (file.size() > (qint64(32) << 20)) {
        info.errors << QObject::tr("El módulo pasa de 32 MB.");
        return info;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        info.errors << file.errorString();
        return info;
    }
    const QByteArray bytes = file.readAll();
    QString error;
    const auto module = Wasm::parse(bytes, &error);
    if (!module) {
        info.errors << error;
        return info;
    }
    if (!Wasm::imports(*module).isEmpty()) {
        info.errors << QObject::tr("El módulo importa %1, pero los complementos no tienen acceso a nada de fuera (ni "
                                   "archivos, ni red, ni reloj).").arg(Wasm::imports(*module).first());
        return info;
    }
    // Las cuatro funciones de la interfaz, con sus tipos.
    using V = Wasm::ValType;
    const std::vector<std::pair<std::string, Wasm::FuncType>> abi{
        {"pc_abi_version", {{}, {V::I32}}},
        {"pc_manifest", {{}, {V::I64}}},
        {"pc_alloc", {{V::I32}, {V::I32}}},
        {"pc_filter", {{V::I32, V::I32, V::I32, V::I32, V::I32, V::I32, V::I32, V::I32}, {V::I32}}},
    };
    for (const auto& [name, type] : abi) {
        Wasm::FuncType found;
        if (!Wasm::exportedFunction(*module, name, &found) || !(found == type)) {
            info.errors << QObject::tr("No es un complemento de filtros de PhotoCraft: falta la función %1.").arg(QString::fromStdString(name));
            return info;
        }
    }
    Wasm::Limits limits;
    limits.timeoutMs = 10'000;
    limits.fuel = 50'000'000;
    auto instance = Wasm::Instance::create(module, limits, &error);
    std::vector<uint64_t> out;
    if (!instance || !instance->call("pc_abi_version", {}, &out, &error)) {
        info.errors << error;
        return info;
    }
    if (int32_t(uint32_t(out.at(0))) != kAbiVersion) {
        info.errors << QObject::tr("Usa la versión %1 de la interfaz de complementos; Kaint admite la %2.")
                           .arg(int32_t(uint32_t(out.at(0))))
                           .arg(kAbiVersion);
        return info;
    }
    if (!instance->call("pc_manifest", {}, &out, &error)) {
        info.errors << error;
        return info;
    }
    const uint64_t packed = out.at(0);
    const uint64_t ptr = packed & 0xffffffffu, len = packed >> 32;
    if (len > 64 * 1024 || ptr + len > instance->memorySize()) {
        info.errors << QObject::tr("pc_manifest señala fuera de la memoria del complemento.");
        return info;
    }
    Manifest manifest;
    if (!parseManifest(QByteArray(reinterpret_cast<const char*>(instance->memory() + ptr), qsizetype(len)), manifest, &error)) {
        info.errors << error;
        return info;
    }
    info.name = manifest.name;
    info.version = manifest.version;
    info.author = manifest.author;
    info.description = manifest.description;
    info.effects << new WasmFilterEffect(module, manifest);
    return info;
}

// ===========================================================================================
// Looks: tablas de color .cube y .3dl
// ===========================================================================================

struct Lut {
    int size = 0;
    bool is3d = true;
    // 3D: el rojo cambia más rápido (como en .cube); 1D: una fila por entrada.
    std::vector<float> data;
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    QString title;

    const float* at(int r, int g, int b) const { return &data[(size_t(b) * size_t(size) * size_t(size) + size_t(g) * size_t(size) + size_t(r)) * 3]; }

    void apply(const float in[3], float out[3]) const
    {
        float t[3];
        for (int c = 0; c < 3; ++c) {
            const float span = domainMax[size_t(c)] - domainMin[size_t(c)];
            t[c] = clamp01(span > 0 ? (in[c] - domainMin[size_t(c)]) / span : in[c]) * float(size - 1);
        }
        if (!is3d) {
            for (int c = 0; c < 3; ++c) {
                const int i = std::min(int(t[c]), size - 2);
                const float f = t[c] - float(i);
                out[c] = data[size_t(i) * 3 + size_t(c)] * (1 - f) + data[size_t(i + 1) * 3 + size_t(c)] * f;
            }
            return;
        }
        // Interpolación tetraédrica entre los 8 vecinos (la habitual en las tablas de color).
        int i[3];
        float f[3];
        for (int c = 0; c < 3; ++c) {
            i[c] = std::min(int(t[c]), size - 2);
            f[c] = t[c] - float(i[c]);
        }
        const float* c000 = at(i[0], i[1], i[2]);
        const float* c111 = at(i[0] + 1, i[1] + 1, i[2] + 1);
        const float fx = f[0], fy = f[1], fz = f[2];
        const float* a = nullptr;
        const float* b = nullptr;
        float w0, wa, wb, w1;
        if (fx >= fy) {
            if (fy >= fz) {
                a = at(i[0] + 1, i[1], i[2]), b = at(i[0] + 1, i[1] + 1, i[2]);
                w0 = 1 - fx, wa = fx - fy, wb = fy - fz, w1 = fz;
            } else if (fx >= fz) {
                a = at(i[0] + 1, i[1], i[2]), b = at(i[0] + 1, i[1], i[2] + 1);
                w0 = 1 - fx, wa = fx - fz, wb = fz - fy, w1 = fy;
            } else {
                a = at(i[0], i[1], i[2] + 1), b = at(i[0] + 1, i[1], i[2] + 1);
                w0 = 1 - fz, wa = fz - fx, wb = fx - fy, w1 = fy;
            }
        } else {
            if (fz >= fy) {
                a = at(i[0], i[1], i[2] + 1), b = at(i[0], i[1] + 1, i[2] + 1);
                w0 = 1 - fz, wa = fz - fy, wb = fy - fx, w1 = fx;
            } else if (fz >= fx) {
                a = at(i[0], i[1] + 1, i[2]), b = at(i[0], i[1] + 1, i[2] + 1);
                w0 = 1 - fy, wa = fy - fz, wb = fz - fx, w1 = fx;
            } else {
                a = at(i[0], i[1] + 1, i[2]), b = at(i[0] + 1, i[1] + 1, i[2]);
                w0 = 1 - fy, wa = fy - fx, wb = fx - fz, w1 = fz;
            }
        }
        for (int k = 0; k < 3; ++k)
            out[k] = w0 * c000[k] + wa * a[k] + wb * b[k] + w1 * c111[k];
    }
};

QList<QByteArray> tokens(const QByteArray& line)
{
    return line.simplified().split(' ');
}

bool parseCube(const QByteArray& text, Lut& lut, QString* error)
{
    std::vector<float> values;
    for (QByteArray line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> t = tokens(line);
        const QByteArray key = t.first().toUpper();
        if (key == "TITLE") {
            lut.title = QString::fromUtf8(line.mid(5).trimmed()).remove(QLatin1Char('"'));
        } else if (key == "LUT_3D_SIZE" || key == "LUT_1D_SIZE") {
            lut.is3d = key == "LUT_3D_SIZE";
            lut.size = t.value(1).toInt();
        } else if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX") {
            auto& d = key == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
            for (int c = 0; c < 3; ++c)
                d[size_t(c)] = t.value(c + 1).toFloat();
        } else if (key == "LUT_1D_INPUT_RANGE" || key == "LUT_3D_INPUT_RANGE") {
            for (int c = 0; c < 3; ++c) {
                lut.domainMin[size_t(c)] = t.value(1).toFloat();
                lut.domainMax[size_t(c)] = t.value(2).toFloat();
            }
        } else if (t.size() >= 3) {
            bool ok = true;
            for (int c = 0; c < 3 && ok; ++c)
                values.push_back(t[c].toFloat(&ok));
            if (!ok) {
                *error = QObject::tr("Línea de la tabla que no se entiende: %1").arg(QString::fromUtf8(line.left(60)));
                return false;
            }
        }
    }
    const size_t expected = lut.is3d ? size_t(lut.size) * size_t(lut.size) * size_t(lut.size) * 3 : size_t(lut.size) * 3;
    if (lut.size < 2 || lut.size > 256 || values.size() != expected) {
        *error = QObject::tr("La tabla de color no tiene el tamaño que dice (%1).").arg(lut.size);
        return false;
    }
    lut.data = std::move(values);
    return true;
}

bool parse3dl(const QByteArray& text, Lut& lut, QString* error)
{
    // Una línea con las entradas de la malla (la cuadrícula) y luego R G B enteros, con el azul
    // cambiando más rápido.
    std::vector<int> mesh;
    std::vector<float> values;
    for (QByteArray line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> t = tokens(line);
        bool numeric = true;
        std::vector<float> row;
        for (const QByteArray& tok : t) {
            bool ok = false;
            row.push_back(tok.toFloat(&ok));
            numeric = numeric && ok;
        }
        if (!numeric)
            continue;
        if (row.size() > 3 && mesh.empty() && values.empty()) {
            for (const float v : row)
                mesh.push_back(int(v));
            continue;
        }
        if (row.size() == 3)
            values.insert(values.end(), row.begin(), row.end());
    }
    const size_t count = values.size() / 3;
    int n = mesh.empty() ? int(std::lround(std::cbrt(double(count)))) : int(mesh.size());
    if (n < 2 || n > 256 || size_t(n) * size_t(n) * size_t(n) != count) {
        *error = QObject::tr("La tabla .3dl no tiene el tamaño de su malla.");
        return false;
    }
    float peak = 0;
    for (const float v : values)
        peak = std::max(peak, v);
    const float scale = peak <= 1.f ? 1.f : peak <= 1023.f ? 1023.f : peak <= 4095.f ? 4095.f : 65535.f;
    lut.size = n;
    lut.is3d = true;
    lut.data.assign(values.size(), 0.f);
    for (int r = 0; r < n; ++r)
        for (int g = 0; g < n; ++g)
            for (int b = 0; b < n; ++b) {
                const size_t from = ((size_t(r) * size_t(n) + size_t(g)) * size_t(n) + size_t(b)) * 3;
                float* to = &lut.data[((size_t(b) * size_t(n) + size_t(g)) * size_t(n) + size_t(r)) * 3];
                for (int c = 0; c < 3; ++c)
                    to[c] = values[from + size_t(c)] / scale;
            }
    return true;
}

class LookEffect : public Effect {
public:
    LookEffect(Lut lut, QString name, QString id)
        : m_lut(std::move(lut))
        , m_name(std::move(name))
        , m_id(std::move(id))
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return CraftPlugins::category(); }
    bool isAdjustment() const override { return false; }
    QString iconName() const override { return QStringLiteral("color-management"); }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_name; }
    QList<EffectParam> params() const override
    {
        return {EffectParam::integer(QStringLiteral("amount"), QObject::tr("Cantidad"), 0, 100, 100)};
    }

    void render(const RenderArgs& a) const override
    {
        const float k = std::clamp(a.values.value(QStringLiteral("amount"), 100).toInt(), 0, 100) / 100.f;
        const ConstPixels in(a.src);
        const Pixels out(a.dst);
        parallelRows(a.roi.top(), a.roi.bottom() + 1, [&](int y0, int y1) {
            for (int y = y0; y < y1 && !a.cancel; ++y) {
                const QRgb* s = in.row(y);
                QRgb* d = out.row(y);
                for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
                    const float c[3] = {qRed(s[x]) / 255.f, qGreen(s[x]) / 255.f, qBlue(s[x]) / 255.f};
                    float o[3];
                    m_lut.apply(c, o);
                    d[x] = qRgba(toByte(c[0] + (o[0] - c[0]) * k), toByte(c[1] + (o[1] - c[1]) * k), toByte(c[2] + (o[2] - c[2]) * k),
                                 qAlpha(s[x]));
                }
            }
        });
    }

private:
    Lut m_lut;
    QString m_name;
    QString m_id;
};

Info loadLut(const QString& path)
{
    Info info;
    info.kind = QStringLiteral("LightCraft (look)");
    QFile file(path);
    if (file.size() > (qint64(64) << 20) || !file.open(QIODevice::ReadOnly)) {
        info.errors << QObject::tr("No se pudo leer la tabla de color.");
        return info;
    }
    Lut lut;
    QString error;
    const bool threeDl = path.endsWith(QLatin1String(".3dl"), Qt::CaseInsensitive);
    if (!(threeDl ? parse3dl(file.readAll(), lut, &error) : parseCube(file.readAll(), lut, &error))) {
        info.errors << error;
        return info;
    }
    info.name = lut.title.isEmpty() ? QFileInfo(path).completeBaseName() : lut.title.left(64);
    info.description = QObject::tr("Tabla de color %1 de %2 puntos por canal.").arg(lut.is3d ? QStringLiteral("3D") : QStringLiteral("1D")).arg(lut.size);
    info.effects << new LookEffect(std::move(lut), info.name, QStringLiteral("craft.look.") + shortHash(QFileInfo(path).fileName()));
    return info;
}

// ===========================================================================================
// Ajustes de revelado .xmp (Camera Raw / Lightroom)
// ===========================================================================================

const char* const kHueBands[8] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};
constexpr float kHueCenters[8] = {0, 30, 60, 120, 180, 240, 270, 300};

struct Develop {
    QString name;
    std::map<QString, double> values;
    // Curvas de puntos (0-255): la general y las de rojo, verde y azul.
    std::array<QList<QPointF>, 4> curves;

    double get(const char* key, double def = 0) const
    {
        const auto it = values.find(QLatin1String(key));
        return it == values.end() ? def : it->second;
    }
    bool has(const char* key) const { return values.count(QLatin1String(key)) > 0; }
};

// Interpolación cúbica monótona (Fritsch-Carlson) de los puntos de una curva, en 256 valores.
std::array<float, 256> curveLut(QList<QPointF> pts)
{
    std::array<float, 256> lut{};
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    if (pts.size() < 2) {
        for (int i = 0; i < 256; ++i)
            lut[size_t(i)] = i / 255.f;
        return lut;
    }
    const int n = int(pts.size());
    std::vector<double> m(pts.size()), d(pts.size() - 1);
    for (int i = 0; i < n - 1; ++i) {
        const double dx = std::max(1e-6, pts[i + 1].x() - pts[i].x());
        d[size_t(i)] = (pts[i + 1].y() - pts[i].y()) / dx;
    }
    m[0] = d[0];
    m[size_t(n - 1)] = d[size_t(n - 2)];
    for (int i = 1; i < n - 1; ++i)
        m[size_t(i)] = d[size_t(i - 1)] * d[size_t(i)] <= 0 ? 0 : (d[size_t(i - 1)] + d[size_t(i)]) / 2;
    for (int i = 0; i < n - 1; ++i) {
        if (d[size_t(i)] == 0) {
            m[size_t(i)] = m[size_t(i + 1)] = 0;
            continue;
        }
        const double a = m[size_t(i)] / d[size_t(i)], b = m[size_t(i + 1)] / d[size_t(i)], s = a * a + b * b;
        if (s > 9) {
            const double t = 3 / std::sqrt(s);
            m[size_t(i)] = t * a * d[size_t(i)];
            m[size_t(i + 1)] = t * b * d[size_t(i)];
        }
    }
    for (int x = 0; x < 256; ++x) {
        double y;
        if (x <= pts.first().x()) {
            y = pts.first().y();
        } else if (x >= pts.last().x()) {
            y = pts.last().y();
        } else {
            int i = 0;
            while (i < n - 2 && x > pts[i + 1].x())
                ++i;
            const double h = pts[i + 1].x() - pts[i].x(), t = (x - pts[i].x()) / h;
            const double t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * pts[i].y() + (t3 - 2 * t2 + t) * h * m[size_t(i)] + (-2 * t3 + 3 * t2) * pts[i + 1].y()
                + (t3 - t2) * h * m[size_t(i + 1)];
        }
        lut[size_t(x)] = float(std::clamp(y / 255.0, 0.0, 1.0));
    }
    return lut;
}

bool isIdentity(const QList<QPointF>& pts)
{
    for (const QPointF& p : pts)
        if (std::abs(p.x() - p.y()) > 0.5)
            return false;
    return true;
}

bool parseXmp(const QByteArray& text, Develop& dev, QString* error)
{
    // Los ajustes van como atributos crs:Nombre="valor" o como elementos <crs:Nombre>valor</...>;
    // las curvas, como una lista de "x, y".
    static const QString crs = QStringLiteral("http://ns.adobe.com/camera-raw-settings/1.0/");
    QXmlStreamReader xml(text);
    QString element;
    int curve = -1;
    bool inName = false;
    auto store = [&](const QString& key, const QString& value) {
        if (key == QLatin1String("ConvertToGrayscale")) {
            dev.values[key] = value.trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ? 1 : 0;
            return;
        }
        if (key == QLatin1String("Name") || key == QLatin1String("PresetName")) {
            if (dev.name.isEmpty() && !value.trimmed().isEmpty())
                dev.name = value.trimmed();
            return;
        }
        bool ok = false;
        const double v = QString(value).remove(QLatin1Char('+')).toDouble(&ok);
        if (ok && std::isfinite(v))
            dev.values[key] = v;
    };
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            for (const QXmlStreamAttribute& at : xml.attributes())
                if (at.namespaceUri() == crs)
                    store(at.name().toString(), at.value().toString());
            if (xml.namespaceUri() == crs) {
                element = xml.name().toString();
                static const QStringList curveNames{QStringLiteral("ToneCurvePV2012"), QStringLiteral("ToneCurvePV2012Red"),
                                                    QStringLiteral("ToneCurvePV2012Green"), QStringLiteral("ToneCurvePV2012Blue")};
                const qsizetype c = curveNames.indexOf(element);
                if (c >= 0) {
                    curve = int(c);
                    dev.curves[size_t(curve)].clear();
                }
                inName = element == QLatin1String("Name");
            }
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            const QString t = xml.text().toString().trimmed();
            if (curve >= 0) {
                const QStringList xy = t.split(QLatin1Char(','));
                if (xy.size() == 2)
                    dev.curves[size_t(curve)] << QPointF(xy[0].trimmed().toDouble(), xy[1].trimmed().toDouble());
            } else if (inName) {
                store(QStringLiteral("Name"), t);
            } else if (!element.isEmpty()) {
                store(element, t);
            }
        } else if (xml.isEndElement() && xml.namespaceUri() == crs) {
            if (xml.name() == element || curve >= 0)
                curve = -1;
            element.clear();
            inName = false;
        }
    }
    if (xml.hasError()) {
        *error = QObject::tr("El archivo .xmp no es XML válido: %1").arg(xml.errorString());
        return false;
    }
    if (dev.values.empty() && std::all_of(dev.curves.begin(), dev.curves.end(), [](const auto& c) { return c.isEmpty(); })) {
        *error = QObject::tr("El archivo .xmp no tiene ajustes de revelado (crs:).");
        return false;
    }
    return true;
}

// Peso de cada banda de color de HSL para un tono (las dos más cercanas, en proporción).
void hueWeights(float hue, float w[8])
{
    std::fill(w, w + 8, 0.f);
    hue = std::fmod(hue + 360.f, 360.f);
    for (int i = 0; i < 8; ++i) {
        const float a = kHueCenters[i], b = i == 7 ? 360.f : kHueCenters[i + 1];
        if (hue >= a && hue < b) {
            const float t = (hue - a) / (b - a);
            w[i] = 1 - t;
            w[(i + 1) % 8] += t;
            return;
        }
    }
}

void rgbToHsv(const float c[3], float& h, float& s, float& v)
{
    const float mx = std::max({c[0], c[1], c[2]}), mn = std::min({c[0], c[1], c[2]}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 1e-6f) {
        h = 0;
        return;
    }
    if (mx == c[0])
        h = 60 * std::fmod((c[1] - c[2]) / d + 6, 6.f);
    else if (mx == c[1])
        h = 60 * ((c[2] - c[0]) / d + 2);
    else
        h = 60 * ((c[0] - c[1]) / d + 4);
}

void hsvToRgb(float h, float s, float v, float c[3])
{
    h = std::fmod(h + 360.f, 360.f) / 60.f;
    const int i = int(h) % 6;
    const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    const float table[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    c[0] = table[i][0];
    c[1] = table[i][1];
    c[2] = table[i][2];
}

float luma(const float c[3])
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

float smoothstep(float a, float b, float x)
{
    if (b <= a)
        return x >= b ? 1.f : 0.f;
    const float t = clamp01((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}

// Desenfoque de caja (tres pasadas, casi gaussiano) de un canal de coma flotante.
void blurPlane(std::vector<float>& p, int w, int h, int radius)
{
    if (radius < 1)
        return;
    std::vector<float> tmp(p.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (const bool horizontal : {true, false}) {
            const int n = horizontal ? w : h, lines = horizontal ? h : w;
            parallelRows(0, lines, [&](int l0, int l1) {
                for (int line = l0; line < l1; ++line) {
                    auto at = [&](int i) -> float& { return horizontal ? p[size_t(line) * size_t(w) + size_t(i)] : p[size_t(i) * size_t(w) + size_t(line)]; };
                    auto out = [&](int i) -> float& { return horizontal ? tmp[size_t(line) * size_t(w) + size_t(i)] : tmp[size_t(i) * size_t(w) + size_t(line)]; };
                    double sum = 0;
                    int count = 0;
                    for (int i = 0; i <= std::min(n - 1, radius); ++i, ++count)
                        sum += at(i);
                    for (int i = 0; i < n; ++i) {
                        out(i) = float(sum / std::max(1, count));
                        const int leave = i - radius, enter = i + radius + 1;
                        if (leave >= 0) {
                            sum -= at(leave);
                            --count;
                        }
                        if (enter < n) {
                            sum += at(enter);
                            ++count;
                        }
                    }
                }
            });
            std::swap(p, tmp);
        }
    }
}

class DevelopEffect : public Effect {
public:
    DevelopEffect(Develop dev, QString name, QString id)
        : m_dev(std::move(dev))
        , m_name(std::move(name))
        , m_id(std::move(id))
    {
        for (int i = 0; i < 4; ++i)
            if (!m_dev.curves[size_t(i)].isEmpty() && !isIdentity(m_dev.curves[size_t(i)])) {
                m_curve[size_t(i)] = curveLut(m_dev.curves[size_t(i)]);
                m_hasCurve[size_t(i)] = true;
            }
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_name; }
    QString category() const override { return CraftPlugins::category(); }
    bool isAdjustment() const override { return false; }
    QString iconName() const override { return QStringLiteral("color-management"); }
    bool straightAlpha() const override { return true; }
    QString pluginName() const override { return m_name; }
    QList<EffectParam> params() const override
    {
        return {EffectParam::integer(QStringLiteral("amount"), QObject::tr("Cantidad"), 0, 100, 100)};
    }

    void render(const RenderArgs& a) const override
    {
        const Develop& d = m_dev;
        const float amount = std::clamp(a.values.value(QStringLiteral("amount"), 100).toInt(), 0, 100) / 100.f;
        const QRect r = a.roi;
        const int w = r.width(), h = r.height();
        // Balance de blancos: en las fotos ya reveladas, el relativo (-100 a 100); si solo está la
        // temperatura en kelvin, la diferencia con la luz de día.
        float temp = float(d.get("IncrementalTemperature") / 100);
        float tint = float(d.get("IncrementalTint") / 100);
        if (!d.has("IncrementalTemperature") && d.has("Temperature"))
            temp = std::clamp(float((d.get("Temperature") - 5500) / 4500), -1.f, 1.f) * 0.6f;
        if (!d.has("IncrementalTint") && d.has("Tint"))
            tint = std::clamp(float(d.get("Tint") / 150), -1.f, 1.f);
        const float wb[3] = {1 + 0.25f * temp + 0.08f * tint, 1 - 0.18f * tint, 1 - 0.25f * temp + 0.08f * tint};
        const float exposure = std::exp2(float(d.get("Exposure2012", d.get("Exposure"))));
        const float dehaze = float(d.get("Dehaze") / 100);
        const float contrast = float(d.get("Contrast2012", d.get("Contrast")) / 100) + 0.3f * dehaze;
        const float highlights = float(d.get("Highlights2012", -d.get("HighlightRecovery")) / 100);
        const float shadows = float(d.get("Shadows2012", d.get("FillLight")) / 100);
        const float whites = float(d.get("Whites2012") / 100), blacks = float(d.get("Blacks2012", -d.get("Shadows") / 2) / 100);
        const float clarity = float(d.get("Clarity2012", d.get("Clarity")) / 100), texture = float(d.get("Texture") / 100);
        const float vibrance = float(d.get("Vibrance") / 100), saturation = float(d.get("Saturation") / 100) + 0.2f * dehaze;
        const float split[4] = {float(d.get("ParametricShadowSplit", 25) / 100), float(d.get("ParametricMidtoneSplit", 50) / 100),
                                float(d.get("ParametricHighlightSplit", 75) / 100), 1.f};
        const float parametric[4] = {float(d.get("ParametricShadows") / 100), float(d.get("ParametricDarks") / 100),
                                     float(d.get("ParametricLights") / 100), float(d.get("ParametricHighlights") / 100)};
        float hueAdj[8], satAdj[8], lumAdj[8], grayAdj[8];
        for (int i = 0; i < 8; ++i) {
            const QByteArray band(kHueBands[i]);
            hueAdj[i] = float(d.get(("HueAdjustment" + band).constData()) / 100);
            satAdj[i] = float(d.get(("SaturationAdjustment" + band).constData()) / 100);
            lumAdj[i] = float(d.get(("LuminanceAdjustment" + band).constData()) / 100);
            grayAdj[i] = float(d.get(("GrayMixer" + band).constData()) / 100);
        }
        const bool gray = d.get("ConvertToGrayscale") > 0.5;
        // Virado (zonas de sombras, medios tonos y luces) y gradación de color.
        struct Tone {
            float tint[3];
            float strength;
            float lum;
        };
        auto tone = [](double hue, double sat, double lum) {
            Tone t{};
            hsvToRgb(float(hue), 1, 1, t.tint);
            const float y = luma(t.tint);
            for (float& c : t.tint)
                c /= std::max(0.05f, y);
            t.strength = float(sat / 100) * 0.35f;
            t.lum = float(lum / 100) * 0.2f;
            return t;
        };
        const Tone shadowTone = tone(d.get("ColorGradeShadowHue", d.get("SplitToningShadowHue")),
                                     std::max(d.get("ColorGradeShadowSat"), d.get("SplitToningShadowSaturation")), d.get("ColorGradeShadowLum"));
        const Tone highlightTone = tone(d.get("ColorGradeHighlightHue", d.get("SplitToningHighlightHue")),
                                        std::max(d.get("ColorGradeHighlightSat"), d.get("SplitToningHighlightSaturation")),
                                        d.get("ColorGradeHighlightLum"));
        const Tone midTone = tone(d.get("ColorGradeMidtoneHue"), d.get("ColorGradeMidtoneSat"), d.get("ColorGradeMidtoneLum"));
        const Tone globalTone = tone(d.get("ColorGradeGlobalHue"), d.get("ColorGradeGlobalSat"), d.get("ColorGradeGlobalLum"));
        const float balance = float(std::max(-100.0, std::min(100.0, d.get("ColorGradeBalance", d.get("SplitToningBalance"))))) / 100;
        const float vignette = float(d.get("PostCropVignetteAmount") / 100);
        const float vMid = 0.4f + 0.8f * float(d.get("PostCropVignetteMidpoint", 50) / 100);
        const float vFeather = 0.05f + 0.9f * float(d.get("PostCropVignetteFeather", 50) / 100);
        const float grain = float(d.get("GrainAmount") / 100);
        const int grainCell = 1 + int(d.get("GrainSize", 25) / 25);

        // Curva de tonos de las luminancias: contraste, luces y sombras, blancos y negros y la
        // curva paramétrica (las mías, a la manera de Camera Raw).
        auto toneCurve = [&](float v) {
            v = clamp01(v);
            if (dehaze > 0)
                v = clamp01((v - 0.04f * dehaze) / (1 - 0.04f * dehaze));
            if (contrast != 0) {
                const float e = contrast > 0 ? 1 + 1.5f * contrast : 1 / (1 - 1.5f * std::max(contrast, -0.66f));
                v = v < 0.5f ? 0.5f * std::pow(2 * v, e) : 1 - 0.5f * std::pow(2 * (1 - v), e);
            }
            const float ws = 6.75f * v * (1 - v) * (1 - v), wh = 6.75f * v * v * (1 - v);
            v += 0.25f * (shadows * ws + highlights * wh);
            v += 0.25f * (whites * v * v * v + blacks * (1 - v) * (1 - v) * (1 - v));
            float lo = 0;
            for (int z = 0; z < 4; ++z) {
                const float hi = split[z], c = (lo + hi) / 2, width = std::max(0.12f, hi - lo);
                const float b = std::max(0.f, 1 - std::abs(v - c) / width);
                v += 0.2f * parametric[z] * b * b;
                lo = hi;
            }
            return clamp01(v);
        };
        std::array<float, 1024> toneLut{};
        for (int i = 0; i < 1024; ++i)
            toneLut[size_t(i)] = toneCurve(i / 1023.f);
        auto toneAt = [&](float v) {
            const float f = clamp01(v) * 1023.f;
            const int i = std::min(int(f), 1022);
            return toneLut[size_t(i)] + (toneLut[size_t(i) + 1] - toneLut[size_t(i)]) * (f - float(i));
        };

        // Primera pasada: balance, exposición y tonos (y la luminancia, para la claridad).
        std::vector<float> rgb(size_t(w) * size_t(h) * 3), lum(size_t(w) * size_t(h));
        std::array<float, 256> toLinear{};
        for (int i = 0; i < 256; ++i)
            toLinear[size_t(i)] = srgbToLinear(i / 255.f);
        const ConstPixels in(a.src);
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const QRgb* s = in.row(r.top() + y) + r.left();
                for (int x = 0; x < w; ++x) {
                    float c[3] = {toLinear[size_t(qRed(s[x]))], toLinear[size_t(qGreen(s[x]))], toLinear[size_t(qBlue(s[x]))]};
                    for (int k = 0; k < 3; ++k)
                        c[k] = linearToSrgb(std::min(4.f, c[k] * wb[k] * exposure));
                    const float l = luma(c), l2 = toneAt(l);
                    for (float& v : c)
                        v = l > 1e-5f ? v * l2 / l : l2;
                    float* o = &rgb[(size_t(y) * size_t(w) + size_t(x)) * 3];
                    std::copy(c, c + 3, o);
                    lum[size_t(y) * size_t(w) + size_t(x)] = l2;
                }
            }
        });
        if (a.cancel)
            return;
        std::vector<float> wide, fine;
        if (clarity != 0) {
            wide = lum;
            blurPlane(wide, w, h, std::max(2, std::min(w, h) / 60));
        }
        if (texture != 0) {
            fine = lum;
            blurPlane(fine, w, h, 2);
        }
        const float cx = (a.src.width() - 1) / 2.f, cy = (a.src.height() - 1) / 2.f;
        const Pixels out(a.dst);
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1 && !a.cancel; ++y) {
                const QRgb* s = in.row(r.top() + y) + r.left();
                QRgb* dst = out.row(r.top() + y) + r.left();
                for (int x = 0; x < w; ++x) {
                    const size_t i = size_t(y) * size_t(w) + size_t(x);
                    float c[3];
                    std::copy(&rgb[i * 3], &rgb[i * 3] + 3, c);
                    float l = lum[i];
                    // Claridad (contraste local amplio, más en los medios tonos) y textura (fino).
                    float l2 = l;
                    if (clarity != 0)
                        l2 += clarity * 0.8f * (l - wide[i]) * (1 - std::abs(2 * l - 1));
                    if (texture != 0)
                        l2 += texture * 0.6f * (l - fine[i]);
                    if (l2 != l) {
                        l2 = clamp01(l2);
                        for (float& v : c)
                            v = l > 1e-5f ? v * l2 / l : l2;
                        l = l2;
                    }
                    // Curvas de puntos: la general en los tres canales y luego la de cada uno.
                    for (int k = 0; k < 3; ++k) {
                        auto apply = [&](const std::array<float, 256>& lut, float v) {
                            const float f = clamp01(v) * 255.f;
                            const int j = std::min(int(f), 254);
                            return lut[size_t(j)] + (lut[size_t(j) + 1] - lut[size_t(j)]) * (f - float(j));
                        };
                        if (m_hasCurve[0])
                            c[k] = apply(m_curve[0], c[k]);
                        if (m_hasCurve[size_t(k) + 1])
                            c[k] = apply(m_curve[size_t(k) + 1], c[k]);
                    }
                    // HSL por bandas de color, intensidad y saturación.
                    float hh, ss, vv;
                    rgbToHsv(c, hh, ss, vv);
                    float bw[8];
                    hueWeights(hh, bw);
                    float dh = 0, ds = 0, dl = 0, dg = 0;
                    for (int b = 0; b < 8; ++b) {
                        dh += bw[b] * hueAdj[b];
                        ds += bw[b] * satAdj[b];
                        dl += bw[b] * lumAdj[b];
                        dg += bw[b] * grayAdj[b];
                    }
                    if (gray) {
                        float yv = luma(c) * (1 + 0.7f * dg * ss);
                        yv = clamp01(yv);
                        c[0] = c[1] = c[2] = yv;
                    } else {
                        hh += dh * 30;
                        ss *= 1 + ds;
                        vv *= 1 + 0.5f * dl * ss;
                        ss *= 1 + vibrance * (1 - ss);
                        ss *= 1 + saturation;
                        hsvToRgb(hh, clamp01(ss), clamp01(vv), c);
                    }
                    // Gradación: sombras, medios, luces y global.
                    const float yv = clamp01(luma(c) - 0.25f * balance);
                    const float zs = (1 - yv) * (1 - yv), zh = yv * yv, zm = std::max(0.f, 1 - zs - zh);
                    for (const auto& [t, zone] : {std::pair{&shadowTone, zs}, std::pair{&highlightTone, zh}, std::pair{&midTone, zm},
                                                  std::pair{&globalTone, 1.f}}) {
                        const float k = t->strength * zone;
                        for (int ch = 0; ch < 3; ++ch)
                            c[ch] = c[ch] + (c[ch] * t->tint[ch] - c[ch]) * k + t->lum * zone;
                    }
                    // Viñeta tras el recorte (sobre la imagen entera).
                    if (vignette != 0) {
                        const float dx = (r.left() + x - cx) / std::max(1.f, cx), dy = (r.top() + y - cy) / std::max(1.f, cy);
                        const float f = smoothstep(vMid - vFeather / 2, vMid + vFeather / 2, std::sqrt(dx * dx + dy * dy));
                        for (float& v : c)
                            v = vignette < 0 ? v * (1 + vignette * f) : v + (1 - v) * vignette * f;
                    }
                    if (grain > 0) {
                        uint32_t hsh = uint32_t((r.left() + x) / grainCell) * 73856093u ^ uint32_t((r.top() + y) / grainCell) * 19349663u;
                        hsh ^= hsh >> 13;
                        hsh *= 0x5bd1e995u;
                        hsh ^= hsh >> 15;
                        const float n = (float(hsh & 0xffff) / 65535.f - 0.5f) * 0.12f * grain;
                        for (float& v : c)
                            v += n;
                    }
                    const float o[3] = {qRed(s[x]) / 255.f, qGreen(s[x]) / 255.f, qBlue(s[x]) / 255.f};
                    dst[x] = qRgba(toByte(o[0] + (c[0] - o[0]) * amount), toByte(o[1] + (c[1] - o[1]) * amount),
                                   toByte(o[2] + (c[2] - o[2]) * amount), qAlpha(s[x]));
                }
            }
        });
    }

private:
    Develop m_dev;
    QString m_name;
    QString m_id;
    std::array<std::array<float, 256>, 4> m_curve{};
    std::array<bool, 4> m_hasCurve{};
};

Info loadXmp(const QString& path)
{
    Info info;
    info.kind = QStringLiteral("LightCraft (preset)");
    QFile file(path);
    if (file.size() > (qint64(8) << 20) || !file.open(QIODevice::ReadOnly)) {
        info.errors << QObject::tr("No se pudo leer el ajuste de revelado.");
        return info;
    }
    Develop dev;
    QString error;
    if (!parseXmp(file.readAll(), dev, &error)) {
        info.errors << error;
        return info;
    }
    info.name = dev.name.isEmpty() ? QFileInfo(path).completeBaseName() : dev.name.left(64);
    info.description = QObject::tr("Ajuste de revelado con %n valor(es).", nullptr, int(dev.values.size()));
    info.effects << new DevelopEffect(std::move(dev), info.name, QStringLiteral("craft.preset.") + shortHash(QFileInfo(path).fileName()));
    return info;
}

} // namespace

QStringList fileFilters()
{
    return {QStringLiteral("*.wasm"), QStringLiteral("*.cube"), QStringLiteral("*.3dl"), QStringLiteral("*.xmp")};
}

bool isCraftFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("wasm") || suffix == QLatin1String("cube") || suffix == QLatin1String("3dl")
           || suffix == QLatin1String("xmp");
}

Info load(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("wasm"))
        return loadWasm(path);
    if (suffix == QLatin1String("xmp"))
        return loadXmp(path);
    return loadLut(path);
}

bool parseLut(const QByteArray& data, bool threeDl, QString* error, int* size)
{
    Lut lut;
    QString why;
    const bool ok = threeDl ? parse3dl(data, lut, &why) : parseCube(data, lut, &why);
    if (error)
        *error = why;
    if (size)
        *size = lut.size;
    return ok;
}

} // namespace CraftPlugins
