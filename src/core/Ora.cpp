#include "core/Ora.h"

#include "core/Document.h"
#include "core/Raster.h"
#include "core/Zip.h"

#include <QBuffer>
#include <QPainter>
#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace {

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
