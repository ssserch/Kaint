#include "core/Ora.h"

#include "core/Document.h"
#include "core/Raster.h"

#include <QBuffer>
#include <QPainter>
#include <QFile>
#include <QHash>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>

#include <zlib.h>

namespace {

// ---------------------------------------------------------------------------
// ZIP mínimo: escritura sin compresión (los PNG ya van comprimidos) y lectura de entradas
// almacenadas o comprimidas con deflate.
// ---------------------------------------------------------------------------

void put16(QByteArray& out, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    out.append(b, 2);
}

void put32(QByteArray& out, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    out.append(b, 4);
}

// Lectura con comprobación de límites; fuera del archivo devuelve 0. data_ptr() en vez de
// constData() evita un falso aviso de -Warray-bounds.
quint32 getLE(const QByteArray& data, qsizetype pos, int bytes)
{
    if (pos < 0 || pos + bytes > data.size())
        return 0;
    const auto* p = reinterpret_cast<const uchar*>(data.data_ptr().data()) + pos;
    quint32 v = 0;
    for (int i = bytes - 1; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}
quint16 get16(const QByteArray& data, qsizetype pos) { return quint16(getLE(data, pos, 2)); }
quint32 get32(const QByteArray& data, qsizetype pos) { return getLE(data, pos, 4); }

class ZipWriter {
public:
    void add(const QString& name, const QByteArray& data)
    {
        const QByteArray fname = name.toUtf8();
        const quint32 crc = quint32(crc32(0L, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size())));
        Entry e{fname, crc, quint32(data.size()), quint32(m_out.size())};

        put32(m_out, 0x04034b50);
        put16(m_out, 20);
        put16(m_out, 0x0800);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0x21);
        put32(m_out, crc);
        put32(m_out, e.size);
        put32(m_out, e.size);
        put16(m_out, quint16(fname.size()));
        put16(m_out, 0);
        m_out.append(fname);
        m_out.append(data);
        m_entries.append(e);
    }

    QByteArray finish()
    {
        const quint32 cdStart = quint32(m_out.size());
        for (const Entry& e : m_entries) {
            put32(m_out, 0x02014b50);
            put16(m_out, 20);
            put16(m_out, 20);
            put16(m_out, 0x0800);
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0x21);
            put32(m_out, e.crc);
            put32(m_out, e.size);
            put32(m_out, e.size);
            put16(m_out, quint16(e.name.size()));
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0);
            put16(m_out, 0);
            put32(m_out, 0);
            put32(m_out, e.offset);
            m_out.append(e.name);
        }
        const quint32 cdSize = quint32(m_out.size()) - cdStart;
        put32(m_out, 0x06054b50);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, quint16(m_entries.size()));
        put16(m_out, quint16(m_entries.size()));
        put32(m_out, cdSize);
        put32(m_out, cdStart);
        put16(m_out, 0);
        return m_out;
    }

private:
    struct Entry {
        QByteArray name;
        quint32 crc;
        quint32 size;
        quint32 offset;
    };
    QByteArray m_out;
    QList<Entry> m_entries;
};

class ZipReader {
public:
    explicit ZipReader(QByteArray data)
        : m_data(std::move(data))
    {
        const qsizetype minPos = std::max<qsizetype>(0, m_data.size() - 22 - 65535);
        qsizetype eocd = -1;
        for (qsizetype pos = m_data.size() - 22; pos >= minPos; --pos) {
            if (get32(m_data, pos) == 0x06054b50) {
                eocd = pos;
                break;
            }
        }
        if (eocd < 0)
            return;
        const int count = get16(m_data, eocd + 10);
        qsizetype pos = get32(m_data, eocd + 16);
        for (int i = 0; i < count; ++i) {
            if (pos + 46 > m_data.size() || get32(m_data, pos) != 0x02014b50)
                return;
            Entry e;
            e.method = get16(m_data, pos + 10);
            e.compressed = get32(m_data, pos + 20);
            e.size = get32(m_data, pos + 24);
            const int nameLen = get16(m_data, pos + 28);
            const int extraLen = get16(m_data, pos + 30);
            const int commentLen = get16(m_data, pos + 32);
            e.localOffset = get32(m_data, pos + 42);
            const QString name = QString::fromUtf8(m_data.mid(pos + 46, nameLen));
            m_entries.insert(name, e);
            pos += 46 + nameLen + extraLen + commentLen;
        }
        m_valid = true;
    }

    bool isValid() const { return m_valid; }
    bool contains(const QString& name) const { return m_entries.contains(name); }

    QByteArray read(const QString& name) const
    {
        const auto it = m_entries.constFind(name);
        if (it == m_entries.cend())
            return {};
        const Entry& e = *it;
        const qsizetype lh = e.localOffset;
        if (lh + 30 > m_data.size() || get32(m_data, lh) != 0x04034b50)
            return {};
        const qsizetype start = lh + 30 + get16(m_data, lh + 26) + get16(m_data, lh + 28);
        if (start + qsizetype(e.compressed) > m_data.size())
            return {};
        const QByteArray raw = QByteArray::fromRawData(m_data.constData() + start, e.compressed);
        if (e.method == 0)
            return QByteArray(raw.constData(), raw.size());
        if (e.method != 8)
            return {};

        QByteArray out(qsizetype(e.size), Qt::Uninitialized);
        z_stream zs{};
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
            return {};
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
        zs.avail_in = uInt(raw.size());
        zs.next_out = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = uInt(out.size());
        const int ret = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (ret != Z_STREAM_END)
            return {};
        return out;
    }

private:
    struct Entry {
        int method = 0;
        quint32 compressed = 0;
        quint32 size = 0;
        quint32 localOffset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;
};

QByteArray encodePng(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    return bytes;
}

} // namespace

namespace Ora {

bool save(const Document* doc, const QString& path, QString* error)
{
    ZipWriter zip;
    zip.add(QStringLiteral("mimetype"), QByteArrayLiteral("image/openraster"));

    QByteArray stackXml;
    QXmlStreamWriter xml(&stackXml);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("image"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("0.0.5"));
    xml.writeAttribute(QStringLiteral("w"), QString::number(doc->size().width()));
    xml.writeAttribute(QStringLiteral("h"), QString::number(doc->size().height()));
    xml.writeAttribute(QStringLiteral("xres"), QString::number(doc->dpi(), 'f', 2));
    xml.writeAttribute(QStringLiteral("yres"), QString::number(doc->dpi(), 'f', 2));
    xml.writeStartElement(QStringLiteral("stack"));
    for (int i = doc->layerCount() - 1; i >= 0; --i) {
        const Layer* l = doc->layer(i);
        const QString src = QStringLiteral("data/layer%1.png").arg(i);
        xml.writeStartElement(QStringLiteral("layer"));
        xml.writeAttribute(QStringLiteral("name"), l->props.name);
        xml.writeAttribute(QStringLiteral("src"), src);
        xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("opacity"), QString::number(l->props.opacity / 255.0, 'f', 3));
        xml.writeAttribute(QStringLiteral("visibility"), l->props.visible ? QStringLiteral("visible") : QStringLiteral("hidden"));
        xml.writeAttribute(QStringLiteral("composite-op"), blendModeOraName(l->props.blend));
        xml.writeEndElement();
        zip.add(src, encodePng(l->image));
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    zip.add(QStringLiteral("stack.xml"), stackXml);

    const QImage merged = doc->flattened();
    zip.add(QStringLiteral("mergedimage.png"), encodePng(merged));
    zip.add(QStringLiteral("Thumbnails/thumbnail.png"), encodePng(Raster::thumbnail(merged, QSize(256, 256), false)));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(zip.finish());
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

Document* load(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return nullptr;
    }
    ZipReader zip(file.readAll());
    if (!zip.isValid() || !zip.contains(QStringLiteral("stack.xml"))) {
        if (error)
            *error = QObject::tr("El archivo no es un OpenRaster válido.");
        return nullptr;
    }

    struct LayerInfo {
        LayerProps props;
        QString src;
        QPoint pos;
    };
    QList<LayerInfo> infos;
    QSize size;
    double dpi = 96;

    QXmlStreamReader xml(zip.read(QStringLiteral("stack.xml")));
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto attrs = xml.attributes();
        if (xml.name() == QLatin1String("image")) {
            size = QSize(attrs.value(QLatin1String("w")).toInt(), attrs.value(QLatin1String("h")).toInt());
            const double xres = attrs.value(QLatin1String("xres")).toDouble();
            if (xres > 0)
                dpi = xres;
        } else if (xml.name() == QLatin1String("layer")) {
            LayerInfo info;
            info.props.name = attrs.value(QLatin1String("name")).toString();
            info.src = attrs.value(QLatin1String("src")).toString();
            info.pos = QPoint(attrs.value(QLatin1String("x")).toInt(), attrs.value(QLatin1String("y")).toInt());
            const auto opacity = attrs.value(QLatin1String("opacity"));
            info.props.opacity = opacity.isEmpty() ? 255 : std::clamp(int(opacity.toDouble() * 255 + 0.5), 0, 255);
            info.props.visible = attrs.value(QLatin1String("visibility")) != QLatin1String("hidden");
            info.props.blend = blendModeFromOra(attrs.value(QLatin1String("composite-op")).toString());
            infos.append(info);
        }
    }
    if (xml.hasError() || size.isEmpty()) {
        if (error)
            *error = QObject::tr("stack.xml no es válido: %1").arg(xml.errorString());
        return nullptr;
    }

    auto* doc = new Document(size);
    QList<LayerPtr> layers;
    for (auto it = infos.crbegin(); it != infos.crend(); ++it) {
        auto layer = std::make_shared<Layer>(it->props.name, size);
        layer->props = it->props;
        if (layer->props.name.isEmpty())
            layer->props.name = QObject::tr("Capa %1").arg(layers.size() + 1);
        QImage img;
        img.loadFromData(zip.read(it->src), "PNG");
        if (!img.isNull())
            Raster::blit(layer->image, img.convertToFormat(kLayerFormat), it->pos);
        layers.append(layer);
    }
    if (layers.isEmpty())
        layers.append(std::make_shared<Layer>(QObject::tr("Fondo"), size, Qt::white));
    doc->replaceState(size, layers, int(layers.size()) - 1);
    doc->setDpi(dpi);
    doc->setHistoryRootLabel(QObject::tr("Abrir imagen"));
    return doc;
}

QImage readThumbnail(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    ZipReader zip(file.readAll());
    if (!zip.isValid())
        return {};
    for (const QString& name : {QStringLiteral("Thumbnails/thumbnail.png"), QStringLiteral("mergedimage.png")}) {
        QImage img;
        if (zip.contains(name) && img.loadFromData(zip.read(name), "PNG"))
            return img;
    }
    return {};
}

} // namespace Ora
