#include "core/Pdn.h"

#include "core/BlendMode.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Parallel.h"

#include <QFile>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QVariant>

#include <zlib.h>

#include <cstring>
#include <iterator>
#include <memory>
#include <variant>
#include <vector>

namespace {

// --- MS-NRBF -------------------------------------------------------------------

struct Object;
struct Ref {
    int id;
};
using Array = std::vector<struct Value>;
struct Value {
    std::variant<std::monostate, bool, qint64, double, QString, QByteArray, Ref, std::shared_ptr<Object>,
                 std::shared_ptr<Array>>
        v;
};
struct Object {
    int id = 0;
    QString cls;
    QHash<QString, Value> members;
};

struct ClassInfo {
    QString name;
    QStringList members;
    std::vector<int> binaryTypes;
    std::vector<QVariant> extra;
};

class ParseError {
public:
    explicit ParseError(QString m) : message(std::move(m)) {}
    QString message;
};

class Nrbf {
public:
    Nrbf(const QByteArray& data, qsizetype pos)
        : m_data(data)
        , m_pos(pos)
    {
    }

    // Lee hasta MessageEnd y devuelve el objeto raíz.
    Value parse()
    {
        while (true) {
            const Value v = record();
            if (m_end)
                break;
            Q_UNUSED(v);
        }
        return deref(Value{Ref{m_root}});
    }

    qsizetype position() const { return m_pos; }
    const std::vector<std::shared_ptr<Object>>& memoryBlocks() const { return m_blocks; }

    Value deref(const Value& v) const
    {
        if (const Ref* r = std::get_if<Ref>(&v.v)) {
            const auto it = m_objects.find(r->id);
            return it == m_objects.end() ? Value{} : it.value();
        }
        return v;
    }

private:
    void need(qsizetype n) const
    {
        if (m_pos + n > m_data.size())
            throw ParseError(QObject::tr("El archivo está incompleto."));
    }
    quint8 u8()
    {
        need(1);
        return quint8(m_data[m_pos++]);
    }
    template <typename T>
    T read()
    {
        need(sizeof(T));
        T v;
        std::memcpy(&v, m_data.constData() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return v;
    }
    QString string()
    {
        int length = 0;
        for (int shift = 0; shift < 35; shift += 7) {
            const quint8 b = u8();
            length |= (b & 0x7f) << shift;
            if (!(b & 0x80))
                break;
        }
        need(length);
        const QString s = QString::fromUtf8(m_data.constData() + m_pos, length);
        m_pos += length;
        return s;
    }

    Value primitive(int type)
    {
        switch (type) {
        case 1: return {bool(u8())};
        case 2: return {qint64(u8())};
        case 3: return {QString(QChar(u8()))};
        case 5: return {string()};
        case 6: return {read<double>()};
        case 7: return {qint64(read<qint16>())};
        case 8: return {qint64(read<qint32>())};
        case 9: return {qint64(read<qint64>())};
        case 10: return {qint64(read<qint8>())};
        case 11: return {double(read<float>())};
        case 12:
        case 13: return {qint64(read<qint64>())};
        case 14: return {qint64(read<quint16>())};
        case 15: return {qint64(read<quint32>())};
        case 16: return {qint64(read<quint64>())};
        case 17: return {};
        case 18: return {string()};
        default: throw ParseError(QObject::tr("Tipo primitivo desconocido (%1).").arg(type));
        }
    }

    void memberTypes(ClassInfo& c, int count)
    {
        c.binaryTypes.resize(size_t(count));
        c.extra.resize(size_t(count));
        for (int i = 0; i < count; ++i)
            c.binaryTypes[size_t(i)] = u8();
        for (int i = 0; i < count; ++i) {
            switch (c.binaryTypes[size_t(i)]) {
            case 0:
            case 7: c.extra[size_t(i)] = int(u8()); break;
            case 3: string(); break;
            case 4:
                string();
                read<qint32>();
                break;
            default: break;
            }
        }
    }

    std::shared_ptr<Object> objectFrom(int id, const ClassInfo& c)
    {
        auto obj = std::make_shared<Object>();
        obj->id = id;
        obj->cls = c.name;
        m_objects.insert(id, Value{obj});
        if (c.name == QLatin1String("PaintDotNet.MemoryBlock"))
            m_blocks.push_back(obj);
        for (int i = 0; i < c.members.size(); ++i) {
            const int bt = c.binaryTypes[size_t(i)];
            obj->members.insert(c.members[i], bt == 0 ? primitive(c.extra[size_t(i)].toInt()) : record());
        }
        return obj;
    }

    // Elementos de una matriz de objetos (con las rachas de nulos).
    std::shared_ptr<Array> objectArray(int id, qint64 count)
    {
        auto arr = std::make_shared<Array>();
        arr->reserve(size_t(std::min<qint64>(count, 1 << 20)));
        m_objects.insert(id, Value{arr});
        while (qint64(arr->size()) < count) {
            m_nulls = 0;
            const Value v = record();
            if (m_nulls > 0) {
                for (int i = 0; i < m_nulls && qint64(arr->size()) < count; ++i)
                    arr->push_back({});
                m_nulls = 0;
            } else {
                arr->push_back(v);
            }
        }
        return arr;
    }

    Value record()
    {
        const int type = u8();
        switch (type) {
        case 0:
            m_root = read<qint32>();
            read<qint32>();
            read<qint32>();
            read<qint32>();
            return {};
        case 12:
            read<qint32>();
            string();
            return record();
        case 1: {
            const int id = read<qint32>();
            const int meta = read<qint32>();
            const auto it = m_classes.find(meta);
            if (it == m_classes.end())
                throw ParseError(QObject::tr("Referencia a una clase desconocida."));
            const ClassInfo c = it.value();
            m_classes.insert(id, c);
            return {objectFrom(id, c)};
        }
        case 4:
        case 5: {
            const int id = read<qint32>();
            ClassInfo c;
            c.name = string();
            const int count = read<qint32>();
            for (int i = 0; i < count; ++i)
                c.members << string();
            memberTypes(c, count);
            if (type == 5)
                read<qint32>();
            m_classes.insert(id, c);
            return {objectFrom(id, c)};
        }
        case 6: {
            const int id = read<qint32>();
            const Value v{string()};
            m_objects.insert(id, v);
            return v;
        }
        case 7: {
            const int id = read<qint32>();
            const int arrayType = u8();
            const int rank = read<qint32>();
            qint64 count = 1;
            for (int i = 0; i < rank; ++i)
                count *= read<qint32>();
            if (arrayType >= 3 && arrayType <= 5)
                for (int i = 0; i < rank; ++i)
                    read<qint32>();
            const int bt = u8();
            int prim = 0;
            if (bt == 0 || bt == 7)
                prim = u8();
            else if (bt == 3)
                string();
            else if (bt == 4) {
                string();
                read<qint32>();
            }
            if (bt == 0) {
                auto arr = std::make_shared<Array>();
                for (qint64 i = 0; i < count; ++i)
                    arr->push_back(primitive(prim));
                m_objects.insert(id, Value{arr});
                return {arr};
            }
            return {objectArray(id, count)};
        }
        case 8: {
            const int t = u8();
            return primitive(t);
        }
        case 9: return {Ref{read<qint32>()}};
        case 10: return {};
        case 11: m_end = true; return {};
        case 13: m_nulls = u8(); return {};
        case 14: m_nulls = read<qint32>(); return {};
        case 15: {
            const int id = read<qint32>();
            const int count = read<qint32>();
            const int t = u8();
            if (t == 2) {
                need(count);
                const Value v{m_data.mid(m_pos, count)};
                m_pos += count;
                m_objects.insert(id, v);
                return v;
            }
            auto arr = std::make_shared<Array>();
            for (int i = 0; i < count; ++i)
                arr->push_back(primitive(t));
            m_objects.insert(id, Value{arr});
            return {arr};
        }
        case 16:
        case 17: {
            const int id = read<qint32>();
            const int count = read<qint32>();
            return {objectArray(id, count)};
        }
        default:
            throw ParseError(QObject::tr("Registro desconocido (%1) en la serialización.").arg(type));
        }
    }

    const QByteArray& m_data;
    qsizetype m_pos;
    int m_root = 1;
    bool m_end = false;
    int m_nulls = 0;
    QHash<int, ClassInfo> m_classes;
    QHash<int, Value> m_objects;
    std::vector<std::shared_ptr<Object>> m_blocks;
};

// --- Ayudas ------------------------------------------------------------------

std::shared_ptr<Object> asObject(const Nrbf& n, const Value& v)
{
    const Value d = n.deref(v);
    if (auto* o = std::get_if<std::shared_ptr<Object>>(&d.v))
        return *o;
    return nullptr;
}

Value member(const Nrbf& n, const std::shared_ptr<Object>& o, const char* name)
{
    if (!o)
        return {};
    return n.deref(o->members.value(QLatin1String(name)));
}

qint64 integer(const Value& v, qint64 def = 0)
{
    if (auto* i = std::get_if<qint64>(&v.v))
        return *i;
    if (auto* b = std::get_if<bool>(&v.v))
        return *b;
    if (auto* d = std::get_if<double>(&v.v))
        return qint64(*d);
    return def;
}

QString text(const Value& v)
{
    if (auto* s = std::get_if<QString>(&v.v))
        return *s;
    return {};
}

QByteArray gunzip(const char* data, qsizetype size, qsizetype expected)
{
    QByteArray out;
    out.resize(expected > 0 ? expected : size * 4);
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
        return {};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    zs.avail_in = uInt(size);
    qsizetype produced = 0;
    int rc = Z_OK;
    while (rc == Z_OK) {
        if (produced == out.size())
            out.resize(out.size() * 2);
        zs.next_out = reinterpret_cast<Bytef*>(out.data() + produced);
        zs.avail_out = uInt(out.size() - produced);
        rc = inflate(&zs, Z_NO_FLUSH);
        produced = out.size() - zs.avail_out;
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};
    out.resize(produced);
    return out;
}

quint32 bigEndian32(const QByteArray& d, qsizetype pos)
{
    const auto* p = reinterpret_cast<const uchar*>(d.constData() + pos);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

// Datos diferidos de un MemoryBlock: versión (0 = gzip, 1 = sin comprimir), tamaño de bloque y
// los bloques numerados, en cualquier orden.
QByteArray readDeferred(const QByteArray& d, qsizetype& pos, qint64 length)
{
    if (pos + 5 > d.size())
        throw ParseError(QObject::tr("Faltan los píxeles de una capa."));
    const int format = uchar(d[pos]);
    const quint32 chunkSize = bigEndian32(d, pos + 1);
    pos += 5;
    if ((format != 0 && format != 1) || chunkSize == 0)
        throw ParseError(QObject::tr("Formato de píxeles desconocido."));
    const qint64 count = (length + chunkSize - 1) / chunkSize;
    struct Chunk {
        quint32 number;
        qsizetype offset;
        quint32 size;
    };
    std::vector<Chunk> chunks;
    for (qint64 i = 0; i < count; ++i) {
        if (pos + 8 > d.size())
            throw ParseError(QObject::tr("Faltan los píxeles de una capa."));
        const quint32 number = bigEndian32(d, pos);
        const quint32 size = bigEndian32(d, pos + 4);
        pos += 8;
        if (number >= count || pos + size > d.size())
            throw ParseError(QObject::tr("Los píxeles de una capa están dañados."));
        chunks.push_back({number, pos, size});
        pos += size;
    }
    QByteArray out(length, Qt::Uninitialized);
    std::atomic_bool failed{false};
    parallelRows(0, int(chunks.size()), [&](int c0, int c1) {
        for (int c = c0; c < c1; ++c) {
            const Chunk& ch = chunks[size_t(c)];
            const qint64 offset = qint64(ch.number) * chunkSize;
            const qint64 expected = std::min<qint64>(chunkSize, length - offset);
            if (format == 1) {
                std::memcpy(out.data() + offset, d.constData() + ch.offset, size_t(std::min<qint64>(expected, ch.size)));
                continue;
            }
            const QByteArray raw = gunzip(d.constData() + ch.offset, ch.size, expected);
            if (raw.size() != expected) {
                failed = true;
                continue;
            }
            std::memcpy(out.data() + offset, raw.constData(), size_t(expected));
        }
    }, 1);
    if (failed)
        throw ParseError(QObject::tr("No se pudieron descomprimir los píxeles de una capa."));
    return out;
}

BlendMode blendFromPdn(const Nrbf& n, const std::shared_ptr<Object>& layer, const std::shared_ptr<Object>& props)
{
    // Paint.NET 4/5: LayerProperties.blendMode (enum LayerBlendMode).
    static const BlendMode modes[] = {BlendMode::Normal,   BlendMode::Multiply,   BlendMode::Additive,
                                      BlendMode::ColorBurn, BlendMode::ColorDodge, BlendMode::Reflect,
                                      BlendMode::Glow,     BlendMode::Overlay,    BlendMode::Difference,
                                      BlendMode::Negation, BlendMode::Lighten,    BlendMode::Darken,
                                      BlendMode::Screen,   BlendMode::Xor};
    const Value mode = member(n, props, "blendMode");
    if (auto e = asObject(n, mode)) {
        const qint64 i = integer(member(n, e, "value__"));
        if (i >= 0 && i < qint64(std::size(modes)))
            return modes[i];
    }
    const auto bitmapProps = asObject(n, member(n, layer, "properties"));
    if (const auto op = asObject(n, member(n, bitmapProps, "blendOp"))) {
        static const QHash<QString, BlendMode> names{
            {QStringLiteral("Normal"), BlendMode::Normal},       {QStringLiteral("Multiply"), BlendMode::Multiply},
            {QStringLiteral("Additive"), BlendMode::Additive},   {QStringLiteral("ColorBurn"), BlendMode::ColorBurn},
            {QStringLiteral("ColorDodge"), BlendMode::ColorDodge}, {QStringLiteral("Reflect"), BlendMode::Reflect},
            {QStringLiteral("Glow"), BlendMode::Glow},           {QStringLiteral("Overlay"), BlendMode::Overlay},
            {QStringLiteral("Difference"), BlendMode::Difference}, {QStringLiteral("Negation"), BlendMode::Negation},
            {QStringLiteral("Lighten"), BlendMode::Lighten},     {QStringLiteral("Darken"), BlendMode::Darken},
            {QStringLiteral("Screen"), BlendMode::Screen},       {QStringLiteral("Xor"), BlendMode::Xor}};
        static const QRegularExpression re(QStringLiteral("\\+(\\w+)BlendOp$"));
        const auto m = re.match(op->cls);
        if (m.hasMatch())
            return names.value(m.captured(1), BlendMode::Normal);
    }
    return BlendMode::Normal;
}

} // namespace

namespace Pdn {

QImage thumbnail(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray head = f.read(7);
    if (head.size() < 7 || !head.startsWith("PDN3"))
        return {};
    const int n = uchar(head[4]) | (uchar(head[5]) << 8) | (uchar(head[6]) << 16);
    const QString xml = QString::fromUtf8(f.read(n));
    static const QRegularExpression re(QStringLiteral("<thumb png=\"([^\"]+)\""));
    const auto m = re.match(xml);
    QImage img;
    if (m.hasMatch())
        img.loadFromData(QByteArray::fromBase64(m.captured(1).toLatin1()), "PNG");
    return img;
}

Document* load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return nullptr;
    }
    QByteArray data = f.readAll();
    try {
        qsizetype pos = 0;
        if (data.startsWith("PDN3")) {
            if (data.size() < 7)
                throw ParseError(QObject::tr("El archivo está incompleto."));
            pos = 7 + (uchar(data[4]) | (uchar(data[5]) << 8) | (uchar(data[6]) << 16));
        }
        if (pos + 2 > data.size())
            throw ParseError(QObject::tr("El archivo está incompleto."));
        if (uchar(data[pos]) == 0x00 && uchar(data[pos + 1]) == 0x01) {
            pos += 2;
        } else if (uchar(data[pos]) == 0x1f && uchar(data[pos + 1]) == 0x8b) {
            data = gunzip(data.constData() + pos, data.size() - pos, 0);
            if (data.isEmpty())
                throw ParseError(QObject::tr("No se pudo descomprimir el archivo."));
            pos = 0;
        } else {
            throw ParseError(QObject::tr("No es una imagen de Paint.NET válida."));
        }

        Nrbf nrbf(data, pos);
        const auto doc = asObject(nrbf, nrbf.parse());
        if (!doc || !doc->cls.endsWith(QLatin1String("Document")))
            throw ParseError(QObject::tr("No es una imagen de Paint.NET válida."));
        const int width = int(integer(member(nrbf, doc, "width")));
        const int height = int(integer(member(nrbf, doc, "height")));
        if (width <= 0 || height <= 0 || qint64(width) * height > (qint64(1) << 31))
            throw ParseError(QObject::tr("Tamaño de imagen no válido."));

        qsizetype deferredPos = nrbf.position();
        QHash<int, QByteArray> pixels;
        for (const auto& block : nrbf.memoryBlocks()) {
            const qint64 length = integer(member(nrbf, block, "length64"));
            if (integer(member(nrbf, block, "hasParent")))
                continue;
            if (integer(member(nrbf, block, "deferred"))) {
                pixels.insert(block->id, readDeferred(data, deferredPos, length));
            } else {
                const Value inline_ = member(nrbf, block, "pointerData");
                if (auto* bytes = std::get_if<QByteArray>(&inline_.v))
                    pixels.insert(block->id, *bytes);
            }
        }

        const auto list = asObject(nrbf, member(nrbf, doc, "layers"));
        const Value itemsValue = member(nrbf, list, "ArrayList+_items");
        const auto* items = std::get_if<std::shared_ptr<Array>>(&itemsValue.v);
        const qint64 count = integer(member(nrbf, list, "ArrayList+_size"));
        if (!items || count <= 0)
            throw ParseError(QObject::tr("La imagen no tiene capas."));

        Document* result = nullptr;
        for (qint64 i = 0; i < count && i < qint64((*items)->size()); ++i) {
            const auto layer = asObject(nrbf, (**items)[size_t(i)]);
            const auto props = asObject(nrbf, member(nrbf, layer, "Layer+properties"));
            const auto surface = asObject(nrbf, member(nrbf, layer, "surface"));
            const auto block = asObject(nrbf, member(nrbf, surface, "scan0"));
            if (!layer || !surface || !block || !pixels.contains(block->id))
                throw ParseError(QObject::tr("Una capa no tiene píxeles (¿capa de un tipo desconocido?)."));
            const int w = int(integer(member(nrbf, surface, "width")));
            const int h = int(integer(member(nrbf, surface, "height")));
            const int stride = int(integer(member(nrbf, surface, "stride")));
            const QByteArray& px = pixels[block->id];
            if (w != width || h != height || stride < w * 4 || qint64(stride) * h > px.size())
                throw ParseError(QObject::tr("Las dimensiones de una capa no coinciden con las de la imagen."));
            QImage image(w, h, kLayerFormat);
            for (int y = 0; y < h; ++y)
                std::memcpy(image.scanLine(y), px.constData() + qsizetype(y) * stride, size_t(w) * 4);
            QString name = text(member(nrbf, props, "name"));
            if (name.isEmpty())
                name = QObject::tr("Capa %1").arg(i + 1);
            LayerProps lp;
            lp.name = name;
            lp.visible = integer(member(nrbf, props, "visible"), 1) != 0;
            lp.opacity = int(std::clamp<qint64>(integer(member(nrbf, props, "opacity"), 255), 0, 255));
            lp.blend = blendFromPdn(nrbf, layer, props);
            if (!result) {
                result = Document::createFromImage(image, name);
                result->setLayerProps(0, lp);
            } else {
                auto l = std::make_shared<Layer>(name, image);
                l->props = lp;
                result->insertLayer(result->layerCount(), l);
            }
        }
        if (!result)
            throw ParseError(QObject::tr("La imagen no tiene capas."));
        result->setActiveIndex(result->layerCount() - 1);
        return result;
    } catch (const ParseError& e) {
        if (error)
            *error = e.message;
        return nullptr;
    }
}

} // namespace Pdn
