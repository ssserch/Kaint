#include "core/Pdn.h"

#include "core/BlendMode.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Parallel.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QSaveFile>
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


// --- Escritura (MS-NRBF, como Paint.NET 5) --------------------------------------

// Versión de Paint.NET con la que se escriben los nombres de los ensamblados y la cabecera: la
// del formato que Kaint reproduce (Paint.NET 5.1).
constexpr const char* kPdnVersion = "5.112.9563.32325";
constexpr int kVersion[4] = {5, 112, 9563, 32325};
const char* const kKeyValue = "System.Collections.Generic.KeyValuePair`2[[System.String, mscorlib, Version=4.0.0.0, "
                              "Culture=neutral, PublicKeyToken=b77a5c561934e089],[System.String, mscorlib, "
                              "Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089]]";
// Tamaño de los bloques comprimidos de píxeles (el de Paint.NET).
constexpr quint32 kChunkSize = 256 * 1024;

class NrbfWriter {
public:
    // Tipos binarios y primitivos de MS-NRBF.
    enum Binary : quint8 { Primitive = 0, String = 1, SystemClass = 3, Class = 4, ObjectArray = 5 };
    enum Prim : quint8 { Boolean = 1, Byte = 2, Int32 = 8, Int64 = 9 };
    struct Member {
        const char* name;
        quint8 binary;
        quint8 prim = 0;
        QByteArray className = {};
        int library = 0;
    };

    QByteArray data;

    void u8(quint8 v) { data.append(char(v)); }
    void i32(qint32 v) { data.append(reinterpret_cast<const char*>(&v), 4); }
    void i64(qint64 v) { data.append(reinterpret_cast<const char*>(&v), 8); }
    void string(const QByteArray& utf8)
    {
        quint32 n = quint32(utf8.size());
        do {
            quint8 b = n & 0x7f;
            n >>= 7;
            if (n)
                b |= 0x80;
            u8(b);
        } while (n);
        data.append(utf8);
    }

    void header(int root)
    {
        u8(0);
        i32(root);
        i32(-1);
        i32(1);
        i32(0);
    }
    void library(int id, const QByteArray& name)
    {
        u8(12);
        i32(id);
        string(name);
    }
    // Definición de una clase (con sus miembros y tipos); 'library' 0: una clase del sistema.
    void classDefinition(int id, const QByteArray& name, const std::vector<Member>& members, int library)
    {
        u8(library ? 5 : 4);
        i32(id);
        string(name);
        i32(qint32(members.size()));
        for (const Member& m : members)
            string(m.name);
        for (const Member& m : members)
            u8(m.binary);
        for (const Member& m : members) {
            if (m.binary == Primitive)
                u8(m.prim);
            else if (m.binary == SystemClass)
                string(m.className);
            else if (m.binary == Class) {
                string(m.className);
                i32(m.library);
            }
        }
        if (library)
            i32(library);
    }
    void classWithId(int id, int metadata)
    {
        u8(1);
        i32(id);
        i32(metadata);
    }
    void objectString(int id, const QString& s)
    {
        u8(6);
        i32(id);
        string(s.toUtf8());
    }
    void reference(int id)
    {
        u8(9);
        i32(id);
    }
    void end() { u8(11); }
};

QByteArray gzip(const char* data, qsizetype size)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out;
    out.resize(qsizetype(deflateBound(&zs, uLong(size))) + 32);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
    zs.avail_in = uInt(size);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    const qsizetype produced = out.size() - zs.avail_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};
    out.resize(produced);
    return out;
}

void bigEndian32(QByteArray& d, quint32 v)
{
    const char b[4] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
    d.append(b, 4);
}

// Metadatos EXIF de la resolución, como los guarda Paint.NET ($exif.tagN[0]).
QList<std::pair<QString, QString>> resolutionMetadata(double dpi)
{
    auto rational = [](double v) {
        quint32 num = quint32(std::lround(v)), den = 1;
        if (std::abs(v - std::round(v)) > 1e-6) {
            num = quint32(std::lround(v * 100));
            den = 100;
        }
        QByteArray b;
        b.append(reinterpret_cast<const char*>(&num), 4);
        b.append(reinterpret_cast<const char*>(&den), 4);
        return QString::fromLatin1(b.toBase64());
    };
    const QString unit = QString::fromLatin1(QByteArray("\x02\x00", 2).toBase64());
    return {
        {QStringLiteral("$exif.tag0[0]"), QStringLiteral("<exif id=\"296\" len=\"2\" type=\"3\" value=\"%1\" />").arg(unit)},
        {QStringLiteral("$exif.tag1[0]"), QStringLiteral("<exif id=\"282\" len=\"8\" type=\"5\" value=\"%1\" />").arg(rational(dpi))},
        {QStringLiteral("$exif.tag2[0]"), QStringLiteral("<exif id=\"283\" len=\"8\" type=\"5\" value=\"%1\" />").arg(rational(dpi))},
    };
}

const char* blendOpName(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Multiply: return "Multiply";
    case BlendMode::Additive: return "Additive";
    case BlendMode::ColorBurn: return "ColorBurn";
    case BlendMode::ColorDodge: return "ColorDodge";
    case BlendMode::Reflect: return "Reflect";
    case BlendMode::Glow: return "Glow";
    case BlendMode::Overlay: return "Overlay";
    case BlendMode::Difference: return "Difference";
    case BlendMode::Negation: return "Negation";
    case BlendMode::Lighten: return "Lighten";
    case BlendMode::Darken: return "Darken";
    case BlendMode::Screen: return "Screen";
    case BlendMode::Xor: return "Xor";
    default: return "Normal";
    }
}

// LayerBlendMode de Paint.NET 4/5 (mismo orden que BlendMode; Sobrescribir no existe allí).
int pdnBlendMode(BlendMode mode)
{
    return mode == BlendMode::Overwrite ? 0 : int(mode);
}

// Miniatura de la cabecera (como la de Paint.NET: hasta 256 px de lado).
QString thumbnailBase64(const QImage& flattened)
{
    QImage thumb = flattened;
    if (thumb.width() > 256 || thumb.height() > 256)
        thumb = thumb.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    thumb.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    return QString::fromLatin1(png.toBase64());
}

// Resolución guardada por Paint.NET en los metadatos EXIF del documento (puntos por pulgada).
double dpiFromMetadata(const Nrbf& n, const std::shared_ptr<Object>& doc)
{
    const Value items = member(n, doc, "userMetadataItems");
    const auto* arr = std::get_if<std::shared_ptr<Array>>(&items.v);
    if (!arr)
        return 0;
    static const QRegularExpression re(QStringLiteral("<exif id=\"(\\d+)\"[^>]*value=\"([^\"]*)\""));
    double dpi = 0;
    int unit = 2;
    for (const Value& item : **arr) {
        const auto kv = asObject(n, item);
        const auto m = re.match(text(member(n, kv, "value")));
        if (!m.hasMatch())
            continue;
        const QByteArray bytes = QByteArray::fromBase64(m.captured(2).toLatin1());
        const int tag = m.captured(1).toInt();
        if (tag == 282 && bytes.size() >= 8) {
            auto u32 = [&bytes](int at) {
                quint32 v = 0;
                for (int k = 3; k >= 0; --k)
                    v = (v << 8) | uchar(bytes.at(at + k));
                return v;
            };
            const quint32 num = u32(0);
            const quint32 den = u32(4);
            if (den)
                dpi = double(num) / den;
        } else if (tag == 296 && bytes.size() >= 2) {
            unit = uchar(bytes[0]) | (uchar(bytes[1]) << 8);
        }
    }
    return unit == 3 ? dpi * 2.54 : dpi;
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
        const double dpi = dpiFromMetadata(nrbf, doc);
        if (dpi >= 1 && dpi <= 100000)
            result->setDpi(dpi);
        result->setActiveIndex(result->layerCount() - 1);
        return result;
    } catch (const ParseError& e) {
        if (error)
            *error = e.message;
        return nullptr;
    }
}

QByteArray encode(const Document* doc, QString* error)
{
    const int count = doc->layerCount();
    const QSize size = doc->size();
    if (count <= 0 || size.isEmpty()) {
        if (error)
            *error = QObject::tr("La imagen no tiene capas.");
        return {};
    }
    using W = NrbfWriter;
    W w;
    const QByteArray version(kPdnVersion);
    const QByteArray dataLib = "PaintDotNet.Data, Version=" + version + ", Culture=neutral, PublicKeyToken=null";
    const QByteArray coreLib = "PaintDotNet.Core, Version=" + version + ", Culture=neutral, PublicKeyToken=null";
    const QByteArray keyValueArray = QByteArray(kKeyValue) + "[]";
    int next = 1;
    auto id = [&next] { return next++; };
    int nextStruct = -1;
    auto structId = [&nextStruct] { return nextStruct--; };

    // Identificadores: el documento es la raíz (1).
    const int docId = id(), dataId = id(), listId = id(), versionId = id(), metaId = id(), itemsId = id(), coreId = id();
    struct LayerIds {
        int layer, bitmapProps, surface, props, blendOp, block;
    };
    std::vector<LayerIds> ids(static_cast<size_t>(count));
    for (LayerIds& l : ids)
        l = {id(), id(), id(), id(), id(), id()};
    const int emptyMetaId = id();

    w.header(docId);
    w.library(dataId, dataLib);
    // Documento
    w.classDefinition(docId, "PaintDotNet.Document",
                      {{"isDisposed", W::Primitive, W::Boolean},
                       {"layers", W::Class, 0, "PaintDotNet.LayerList", dataId},
                       {"width", W::Primitive, W::Int32},
                       {"height", W::Primitive, W::Int32},
                       {"savedWith", W::SystemClass, 0, "System.Version"},
                       {"userMetadataItems", W::SystemClass, 0, keyValueArray}},
                      dataId);
    w.u8(0);
    w.reference(listId);
    w.i32(size.width());
    w.i32(size.height());
    w.reference(versionId);
    w.reference(metaId);
    // Lista de capas (un ArrayList)
    w.classDefinition(listId, "PaintDotNet.LayerList",
                      {{"parent", W::Class, 0, "PaintDotNet.Document", dataId},
                       {"ArrayList+_items", W::ObjectArray},
                       {"ArrayList+_size", W::Primitive, W::Int32},
                       {"ArrayList+_version", W::Primitive, W::Int32}},
                      dataId);
    w.reference(docId);
    w.reference(itemsId);
    w.i32(count);
    w.i32(count);
    // Versión
    w.classDefinition(versionId, "System.Version",
                      {{"_Major", W::Primitive, W::Int32},
                       {"_Minor", W::Primitive, W::Int32},
                       {"_Build", W::Primitive, W::Int32},
                       {"_Revision", W::Primitive, W::Int32}},
                      0);
    for (int v : kVersion)
        w.i32(v);
    // Metadatos del documento: la resolución.
    const auto metadata = resolutionMetadata(doc->dpi());
    w.u8(7);
    w.i32(metaId);
    w.u8(0);
    w.i32(1);
    w.i32(qint32(metadata.size()));
    w.u8(W::SystemClass);
    w.string(kKeyValue);
    int keyValueMeta = 0;
    for (const auto& [key, value] : metadata) {
        const int sid = structId();
        if (!keyValueMeta) {
            w.classDefinition(sid, kKeyValue, {{"key", W::String}, {"value", W::String}}, 0);
            keyValueMeta = sid;
        } else {
            w.classWithId(sid, keyValueMeta);
        }
        w.objectString(id(), key);
        w.objectString(id(), value);
    }
    // Capas, de abajo arriba.
    w.u8(16);
    w.i32(itemsId);
    w.i32(count);
    for (const LayerIds& l : ids)
        w.reference(l.layer);
    w.library(coreId, coreLib);
    for (int i = 0; i < count; ++i) {
        if (i == 0)
            w.classDefinition(ids[0].layer, "PaintDotNet.BitmapLayer",
                              {{"properties", W::Class, 0, "PaintDotNet.BitmapLayer+BitmapLayerProperties", dataId},
                               {"surface", W::Class, 0, "PaintDotNet.Surface", coreId},
                               {"Layer+isDisposed", W::Primitive, W::Boolean},
                               {"Layer+width", W::Primitive, W::Int32},
                               {"Layer+height", W::Primitive, W::Int32},
                               {"Layer+properties", W::Class, 0, "PaintDotNet.Layer+LayerProperties", dataId}},
                              dataId);
        else
            w.classWithId(ids[size_t(i)].layer, ids[0].layer);
        w.reference(ids[size_t(i)].bitmapProps);
        w.reference(ids[size_t(i)].surface);
        w.u8(0);
        w.i32(size.width());
        w.i32(size.height());
        w.reference(ids[size_t(i)].props);
    }
    // Propiedades de cada capa: la operación de fusión (antigua), la superficie y las propiedades.
    QHash<QByteArray, int> blendOpMeta, bitmapPropsMeta;
    int layerBlendMeta = 0;
    for (int i = 0; i < count; ++i) {
        const Layer* layer = doc->layer(i);
        const LayerIds& l = ids[size_t(i)];
        const QByteArray blendClass = QByteArray("PaintDotNet.UserBlendOps+") + blendOpName(layer->props.blend) + "BlendOp";
        // Una definición por cada clase de fusión distinta (el tipo del miembro va en ella).
        if (const int meta = bitmapPropsMeta.value(blendClass)) {
            w.classWithId(l.bitmapProps, meta);
        } else {
            w.classDefinition(l.bitmapProps, "PaintDotNet.BitmapLayer+BitmapLayerProperties",
                              {{"blendOp", W::Class, 0, blendClass, dataId}}, dataId);
            bitmapPropsMeta.insert(blendClass, l.bitmapProps);
        }
        w.reference(l.blendOp);
        if (i == 0)
            w.classDefinition(l.surface, "PaintDotNet.Surface",
                              {{"width", W::Primitive, W::Int32},
                               {"height", W::Primitive, W::Int32},
                               {"stride", W::Primitive, W::Int32},
                               {"scan0", W::Class, 0, "PaintDotNet.MemoryBlock", coreId}},
                              coreId);
        else
            w.classWithId(l.surface, ids[0].surface);
        w.i32(size.width());
        w.i32(size.height());
        w.i32(size.width() * 4);
        w.reference(l.block);
        if (i == 0)
            w.classDefinition(l.props, "PaintDotNet.Layer+LayerProperties",
                              {{"name", W::String},
                               {"userMetadataItems", W::SystemClass, 0, keyValueArray},
                               {"visible", W::Primitive, W::Boolean},
                               {"isBackground", W::Primitive, W::Boolean},
                               {"opacity", W::Primitive, W::Byte},
                               {"blendMode", W::Class, 0, "PaintDotNet.LayerBlendMode", dataId}},
                              dataId);
        else
            w.classWithId(l.props, ids[0].props);
        w.objectString(id(), layer->props.name);
        w.reference(emptyMetaId);
        w.u8(layer->props.visible ? 1 : 0);
        w.u8(i == 0 ? 1 : 0);
        w.u8(quint8(std::clamp(layer->props.opacity, 0, 255)));
        const int blendId = structId();
        if (!layerBlendMeta) {
            w.classDefinition(blendId, "PaintDotNet.LayerBlendMode", {{"value__", W::Primitive, W::Int32}}, dataId);
            layerBlendMeta = blendId;
        } else {
            w.classWithId(blendId, layerBlendMeta);
        }
        w.i32(pdnBlendMode(layer->props.blend));
    }
    // Las operaciones de fusión, los bloques de memoria (con los píxeles al final, diferidos) y la
    // lista vacía de metadatos de las capas.
    int memoryMeta = 0;
    for (int i = 0; i < count; ++i) {
        const LayerIds& l = ids[size_t(i)];
        const QByteArray blendClass = QByteArray("PaintDotNet.UserBlendOps+") + blendOpName(doc->layer(i)->props.blend) + "BlendOp";
        if (const int meta = blendOpMeta.value(blendClass)) {
            w.classWithId(l.blendOp, meta);
        } else {
            w.classDefinition(l.blendOp, blendClass, {}, dataId);
            blendOpMeta.insert(blendClass, l.blendOp);
        }
        if (!memoryMeta) {
            w.classDefinition(l.block, "PaintDotNet.MemoryBlock",
                              {{"length64", W::Primitive, W::Int64},
                               {"hasParent", W::Primitive, W::Boolean},
                               {"deferred", W::Primitive, W::Boolean}},
                              coreId);
            memoryMeta = l.block;
        } else {
            w.classWithId(l.block, memoryMeta);
        }
        w.i64(qint64(size.width()) * 4 * size.height());
        w.u8(0);
        w.u8(1);
        if (i == 0) {
            w.u8(7);
            w.i32(emptyMetaId);
            w.u8(0);
            w.i32(1);
            w.i32(0);
            w.u8(W::SystemClass);
            w.string(kKeyValue);
        }
    }
    w.end();

    // Píxeles de cada capa (BGRA con alfa directo, como las de Kaint): bloques de 256 KB
    // comprimidos con gzip, numerados.
    QByteArray body = std::move(w.data);
    const qint64 length = qint64(size.width()) * 4 * size.height();
    const int chunks = int((length + kChunkSize - 1) / kChunkSize);
    for (int i = 0; i < count; ++i) {
        const QImage image = doc->layer(i)->image.convertToFormat(kLayerFormat);
        QByteArray raw(length, Qt::Uninitialized);
        for (int y = 0; y < size.height(); ++y)
            std::memcpy(raw.data() + qsizetype(y) * size.width() * 4, image.constScanLine(y), size_t(size.width()) * 4);
        std::vector<QByteArray> packed(static_cast<size_t>(chunks));
        std::atomic_bool failed{false};
        parallelRows(0, chunks, [&](int c0, int c1) {
            for (int c = c0; c < c1; ++c) {
                const qint64 offset = qint64(c) * kChunkSize;
                packed[size_t(c)] = gzip(raw.constData() + offset, qsizetype(std::min<qint64>(kChunkSize, length - offset)));
                if (packed[size_t(c)].isEmpty())
                    failed = true;
            }
        }, 1);
        if (failed) {
            if (error)
                *error = QObject::tr("No se pudieron comprimir los píxeles.");
            return {};
        }
        body.append(char(0));
        bigEndian32(body, kChunkSize);
        for (int c = 0; c < chunks; ++c) {
            bigEndian32(body, quint32(c));
            bigEndian32(body, quint32(packed[size_t(c)].size()));
            body.append(packed[size_t(c)]);
        }
    }

    // Cabecera: "PDN3", el largo del XML (24 bits) y el XML con la miniatura.
    const QByteArray xml = QStringLiteral("<pdnImage width=\"%1\" height=\"%2\" layers=\"%3\" savedWithVersion=\"%4\">"
                                          "<custom><thumb png=\"%5\" /></custom></pdnImage>")
                               .arg(size.width())
                               .arg(size.height())
                               .arg(count)
                               .arg(QLatin1String(kPdnVersion), thumbnailBase64(doc->flattened()))
                               .toUtf8();
    if (xml.size() >= (1 << 24)) {
        if (error)
            *error = QObject::tr("La cabecera del archivo es demasiado grande.");
        return {};
    }
    QByteArray out;
    out.reserve(7 + xml.size() + 2 + body.size());
    out.append("PDN3", 4);
    out.append(char(xml.size() & 0xff));
    out.append(char((xml.size() >> 8) & 0xff));
    out.append(char((xml.size() >> 16) & 0xff));
    out.append(xml);
    out.append(char(0x00));
    out.append(char(0x01));
    out.append(body);
    return out;
}

bool save(const Document* doc, const QString& path, QString* error)
{
    const QByteArray data = encode(doc, error);
    if (data.isEmpty())
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace Pdn
