#include "app/AppState.h"

#include "core/CustomShapes.h"
#include "core/FillPattern.h"

#include "core/Ora.h"
#include "core/Palette.h"
#include "core/Raster.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPixmap>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

AppState* AppState::instance()
{
    static AppState* state = new AppState;
    return state;
}

AppState::AppState()
    : m_palette(Palette::defaultColors())
{
}

void AppState::setPrimary(const QColor& c)
{
    if (c == m_primary)
        return;
    m_primary = c;
    emit colorsChanged();
}

void AppState::setSecondary(const QColor& c)
{
    if (c == m_secondary)
        return;
    m_secondary = c;
    emit colorsChanged();
}

void AppState::swapColors()
{
    std::swap(m_primary, m_secondary);
    emit colorsChanged();
}

void AppState::resetColors()
{
    m_primary = Qt::black;
    m_secondary = Qt::white;
    emit colorsChanged();
}

void AppState::setSecondaryActive(bool secondary)
{
    if (secondary == m_secondaryActive)
        return;
    m_secondaryActive = secondary;
    emit activeSlotChanged();
    emit colorsChanged();
}

void AppState::setPalette(const QList<QColor>& colors)
{
    m_palette = colors;
    while (m_palette.size() < Palette::kColorCount)
        m_palette.append(Qt::white);
    emit paletteChanged();
}

void AppState::setPaletteColor(int index, const QColor& c)
{
    if (index < 0 || index >= m_palette.size())
        return;
    m_palette[index] = c;
    emit paletteChanged();
}

void AppState::resetTools()
{
    m_tools = ToolSettings();
    emit toolSettingsChanged();
}

// Las opciones de "tools/" sin esta versión son de cuando se guardaba lo último usado.
constexpr int kToolDefaultsVersion = 2;

ToolSettings AppState::toolDefaults() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("tools"));
    ToolSettings t;
    auto num = [&](const char* key, auto def) { return s.value(QLatin1String(key), def).template value<decltype(def)>(); };
    t.brushSize = num("brushSize", t.brushSize);
    t.hardness = num("hardness", t.hardness);
    t.spacing = num("spacing", t.spacing);
    t.smoothing = num("smoothing", t.smoothing);
    t.antialias = num("antialias", t.antialias);
    t.selectionAntialias = num("selectionQuality", t.selectionAntialias);
    t.moveSelectionAntialias = num("moveSelectionAntialias", t.moveSelectionAntialias);
    t.pressure = num("pressure", t.pressure);
    t.blend = BlendMode(num("blend", int(t.blend)));
    t.tolerance = num("tolerance", t.tolerance);
    t.selectionMode = SelectionMode(num("selectionMode", int(t.selectionMode)));
    t.rectMode = RectSelectMode(num("rectMode", int(t.rectMode)));
    t.fixedWidth = num("fixedWidth", t.fixedWidth);
    t.fixedHeight = num("fixedHeight", t.fixedHeight);
    t.fontFamily = num("fontFamily", t.fontFamily);
    t.fontSize = num("fontSize", t.fontSize);
    t.fontMetric = FontMetric(num("fontMetric", int(t.fontMetric)));
    t.textRendering = TextRendering(num("textRendering", int(t.textRendering)));
    t.bold = num("bold", t.bold);
    t.italic = num("italic", t.italic);
    t.underline = num("underline", t.underline);
    t.strikeout = num("strikeout", t.strikeout);
    t.textAlign = TextAlign(num("textAlign", int(t.textAlign)));
    t.fill = std::clamp(num("fillPattern", t.fill), 0, FillPattern::count() - 1);
    t.dash = Qt::PenStyle(num("dash", int(t.dash)));
    t.cornerRadius = num("cornerRadius", t.cornerRadius);
    t.startCap = LineCap(num("startCap", int(t.startCap)));
    t.endCap = LineCap(num("endCap", int(t.endCap)));
    t.floodGlobal = num("floodGlobal", t.floodGlobal);
    t.toleranceStraight = num("toleranceStraight", t.toleranceStraight);
    t.gradientTransparency = num("gradientTransparency", t.gradientTransparency);
    t.gradientRepeat = GradientRepeat(num("gradientRepeat", int(t.gradientRepeat)));
    t.pickerSize = num("pickerSize", t.pickerSize);
    t.pickerAfter = PickerAfter(num("pickerAfter", int(t.pickerAfter)));
    t.sampleImage = num("floodSampleImage", t.sampleImage);
    t.fillAccumulate = num("fillAccumulate", t.fillAccumulate);
    t.pickerSampleImage = num("pickerSampleImage", t.pickerSampleImage);
    t.recolorSampling = RecolorSampling(num("recolorSampling", int(t.recolorSampling)));
    t.shape = ShapeType(num("shape", int(t.shape)));
    t.customShape = num("customShape", t.customShape);
    t.shapeStyle = ShapeStyle(num("shapeStyle", int(t.shapeStyle)));
    t.gradient = GradientType(num("gradient", int(t.gradient)));
    t.curveType = CurveType(num("curveType", int(t.curveType)));
    // Clave nueva al pasar a bicúbico por defecto (la anterior guardaba el vecino más próximo de
    // fábrica).
    t.moveInterpolation = Resample::Interpolation(std::clamp(num("moveResampling", int(t.moveInterpolation)), 0, 4));
    t.moveGamma = num("moveGamma", t.moveGamma);
    t.afterSelect = AfterSelect(std::clamp(num("afterSelect", int(t.afterSelect)), 0, 2));
    return t;
}

void AppState::setToolDefaults(const ToolSettings& t)
{
    QSettings s;
    s.beginGroup(QStringLiteral("tools"));
    s.setValue(QStringLiteral("defaultsVersion"), kToolDefaultsVersion);
    s.setValue(QStringLiteral("brushSize"), t.brushSize);
    s.setValue(QStringLiteral("hardness"), t.hardness);
    s.setValue(QStringLiteral("spacing"), t.spacing);
    s.setValue(QStringLiteral("smoothing"), t.smoothing);
    s.setValue(QStringLiteral("antialias"), t.antialias);
    s.setValue(QStringLiteral("selectionQuality"), t.selectionAntialias);
    s.remove(QStringLiteral("selectionAntialias"));
    s.setValue(QStringLiteral("moveSelectionAntialias"), t.moveSelectionAntialias);
    s.setValue(QStringLiteral("pressure"), t.pressure);
    s.setValue(QStringLiteral("blend"), int(t.blend));
    s.setValue(QStringLiteral("tolerance"), t.tolerance);
    s.setValue(QStringLiteral("selectionMode"), int(t.selectionMode));
    s.setValue(QStringLiteral("rectMode"), int(t.rectMode));
    s.setValue(QStringLiteral("fixedWidth"), t.fixedWidth);
    s.setValue(QStringLiteral("fixedHeight"), t.fixedHeight);
    s.setValue(QStringLiteral("fontFamily"), t.fontFamily);
    s.setValue(QStringLiteral("fontSize"), t.fontSize);
    s.setValue(QStringLiteral("fontMetric"), int(t.fontMetric));
    s.setValue(QStringLiteral("textRendering"), int(t.textRendering));
    s.setValue(QStringLiteral("bold"), t.bold);
    s.setValue(QStringLiteral("italic"), t.italic);
    s.setValue(QStringLiteral("underline"), t.underline);
    s.setValue(QStringLiteral("strikeout"), t.strikeout);
    s.setValue(QStringLiteral("textAlign"), int(t.textAlign));
    s.setValue(QStringLiteral("fillPattern"), t.fill);
    s.setValue(QStringLiteral("dash"), int(t.dash));
    s.setValue(QStringLiteral("cornerRadius"), t.cornerRadius);
    s.setValue(QStringLiteral("startCap"), int(t.startCap));
    s.setValue(QStringLiteral("endCap"), int(t.endCap));
    s.setValue(QStringLiteral("floodGlobal"), t.floodGlobal);
    s.setValue(QStringLiteral("toleranceStraight"), t.toleranceStraight);
    s.setValue(QStringLiteral("gradientTransparency"), t.gradientTransparency);
    s.setValue(QStringLiteral("gradientRepeat"), int(t.gradientRepeat));
    s.setValue(QStringLiteral("pickerSize"), t.pickerSize);
    s.setValue(QStringLiteral("pickerAfter"), int(t.pickerAfter));
    s.setValue(QStringLiteral("floodSampleImage"), t.sampleImage);
    s.setValue(QStringLiteral("fillAccumulate"), t.fillAccumulate);
    s.setValue(QStringLiteral("pickerSampleImage"), t.pickerSampleImage);
    s.remove(QStringLiteral("sampleImage"));
    s.setValue(QStringLiteral("recolorSampling"), int(t.recolorSampling));
    s.setValue(QStringLiteral("shape"), int(t.shape));
    s.setValue(QStringLiteral("customShape"), t.customShape);
    s.setValue(QStringLiteral("shapeStyle"), int(t.shapeStyle));
    s.setValue(QStringLiteral("gradient"), int(t.gradient));
    s.setValue(QStringLiteral("curveType"), int(t.curveType));
    s.setValue(QStringLiteral("moveResampling"), int(t.moveInterpolation));
    s.remove(QStringLiteral("moveSampling"));
    s.remove(QStringLiteral("moveInterpolation"));
    s.setValue(QStringLiteral("moveGamma"), t.moveGamma);
    s.setValue(QStringLiteral("afterSelect"), int(t.afterSelect));
}

namespace {

QString thumbnailCachePath(const QString& path)
{
    // Una miniatura por archivo y fecha de modificación.
    const QFileInfo info(path);
    const QByteArray key = (info.absoluteFilePath() + QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/thumbnails");
    return dir + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex())
         + QStringLiteral(".png");
}

constexpr QSize kRecentThumb(64, 48);

} // namespace

void AppState::addRecentFile(const QString& path, const QImage& thumbnail)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    m_recent.removeAll(abs);
    m_recent.prepend(abs);
    while (m_recent.size() > 10)
        m_recent.removeLast();
    if (!thumbnail.isNull()) {
        const QImage small = Raster::thumbnail(thumbnail, kRecentThumb);
        const QString cache = thumbnailCachePath(abs);
        QDir().mkpath(QFileInfo(cache).absolutePath());
        small.save(cache, "PNG");
        m_thumbs.insert(abs, QIcon(QPixmap::fromImage(small)));
    } else {
        m_thumbs.remove(abs);
    }
    emit recentFilesChanged();
}

QIcon AppState::recentThumbnail(const QString& path)
{
    const auto it = m_thumbs.constFind(path);
    if (it != m_thumbs.cend())
        return *it;
    QImage thumb(thumbnailCachePath(path));
    if (thumb.isNull() && QFileInfo::exists(path)) {
        QImage img;
        if (QFileInfo(path).suffix().compare(QLatin1String("ora"), Qt::CaseInsensitive) == 0) {
            img = Ora::readThumbnail(path);
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize size = reader.size();
            if (size.isValid() && (size.width() > 512 || size.height() > 512))
                reader.setScaledSize(size.scaled(QSize(256, 256), Qt::KeepAspectRatio));
            img = reader.read();
        }
        if (!img.isNull()) {
            thumb = Raster::thumbnail(img, kRecentThumb);
            const QString cache = thumbnailCachePath(path);
            QDir().mkpath(QFileInfo(cache).absolutePath());
            thumb.save(cache, "PNG");
        }
    }
    const QIcon icon = thumb.isNull() ? QIcon::fromTheme(QStringLiteral("image-x-generic")) : QIcon(QPixmap::fromImage(thumb));
    m_thumbs.insert(path, icon);
    return icon;
}

void AppState::clearRecentFiles()
{
    m_recent.clear();
    m_thumbs.clear();
    emit recentFilesChanged();
}

void AppState::load()
{
    QSettings s;
    // Como en Paint.NET, cada inicio empieza con negro y blanco y con las opciones
    // predeterminadas de las herramientas. Las versiones anteriores guardaban los colores y lo
    // último usado de cada herramienta como si fuera lo predeterminado: se descarta una vez.
    s.remove(QStringLiteral("colors/primary"));
    s.remove(QStringLiteral("colors/secondary"));
    if (s.value(QStringLiteral("tools/defaultsVersion")).toInt() < kToolDefaultsVersion)
        s.remove(QStringLiteral("tools"));
    m_primary = Qt::black;
    m_secondary = Qt::white;
    m_secondaryActive = false;
    m_recent = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString palette = s.value(QStringLiteral("colors/palette")).toString();
    if (!palette.isEmpty())
        m_palette = Palette::parse(palette);
    // Una vez: la paleta guardada de 32 colores con relleno blanco (la que quedaba de versiones
    // anteriores) pasa a la de fábrica, con las filas semitransparentes de Paint.NET.
    if (s.value(QStringLiteral("colors/paletteVersion")).toInt() < 2) {
        if (m_palette == Palette::smallColors()) {
            m_palette = Palette::defaultColors();
            s.setValue(QStringLiteral("colors/palette"), Palette::serialize(m_palette));
        }
        s.setValue(QStringLiteral("colors/paletteVersion"), 2);
    }

    m_tools = toolDefaults();
    emit colorsChanged();
    emit paletteChanged();
    emit toolSettingsChanged();
}

void AppState::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("colors/palette"), Palette::serialize(m_palette));
    s.setValue(QStringLiteral("recentFiles"), m_recent);

    // Ni los colores ni las opciones de las herramientas se guardan al salir: al arrancar se usan
    // negro y blanco y los valores predeterminados (Configuración > Herramientas).
}

// ---------------------------------------------------------------------------
// Formas
// ---------------------------------------------------------------------------

const QList<ShapeType>& allShapes()
{
    static const QList<ShapeType> list = [] {
        QList<ShapeType> l;
        for (int i = 0; i <= int(ShapeType::Heart); ++i)
            l.append(ShapeType(i));
        return l;
    }();
    return list;
}

QList<std::pair<QString, QList<ShapeType>>> shapeGroups()
{
    using S = ShapeType;
    return {
        {QObject::tr("Formas básicas"),
         {S::Rectangle, S::RoundedRect, S::Ellipse, S::Diamond, S::Trapezoid, S::Parallelogram, S::Triangle, S::RightTriangle}},
        {QObject::tr("Polígonos y estrellas"),
         {S::Pentagon, S::Hexagon, S::Heptagon, S::Octagon, S::Star3, S::Star4, S::Star5, S::Star6}},
        {QObject::tr("Flechas"), {S::Arrow, S::NotchedArrow, S::PentagonArrow, S::ChevronArrow}},
        {QObject::tr("Bocadillos"), {S::RectCallout, S::RoundedRectCallout, S::EllipseCallout, S::CloudCallout}},
        {QObject::tr("Símbolos"), {S::LightningBolt, S::CheckMark, S::Multiply, S::Gear, S::Heart}},
    };
}

QString shapeName(ShapeType type)
{
    switch (type) {
    case ShapeType::Rectangle: return QObject::tr("Rectángulo");
    case ShapeType::RoundedRect: return QObject::tr("Rectángulo redondeado");
    case ShapeType::Ellipse: return QObject::tr("Elipse");
    case ShapeType::Diamond: return QObject::tr("Rombo");
    case ShapeType::Trapezoid: return QObject::tr("Trapecio");
    case ShapeType::Parallelogram: return QObject::tr("Paralelogramo");
    case ShapeType::Triangle: return QObject::tr("Triángulo");
    case ShapeType::RightTriangle: return QObject::tr("Triángulo rectángulo");
    case ShapeType::Pentagon: return QObject::tr("Pentágono");
    case ShapeType::Hexagon: return QObject::tr("Hexágono");
    case ShapeType::Heptagon: return QObject::tr("Heptágono");
    case ShapeType::Octagon: return QObject::tr("Octógono");
    case ShapeType::Star3: return QObject::tr("Estrella de 3 puntas");
    case ShapeType::Star4: return QObject::tr("Estrella de 4 puntas");
    case ShapeType::Star5: return QObject::tr("Estrella de 5 puntas");
    case ShapeType::Star6: return QObject::tr("Estrella de 6 puntas");
    case ShapeType::Arrow: return QObject::tr("Flecha");
    case ShapeType::NotchedArrow: return QObject::tr("Flecha con muesca");
    case ShapeType::PentagonArrow: return QObject::tr("Flecha pentagonal");
    case ShapeType::ChevronArrow: return QObject::tr("Galón");
    case ShapeType::RectCallout: return QObject::tr("Bocadillo rectangular");
    case ShapeType::RoundedRectCallout: return QObject::tr("Bocadillo redondeado");
    case ShapeType::EllipseCallout: return QObject::tr("Bocadillo elíptico");
    case ShapeType::CloudCallout: return QObject::tr("Bocadillo de nube");
    case ShapeType::LightningBolt: return QObject::tr("Rayo");
    case ShapeType::CheckMark: return QObject::tr("Marca de verificación");
    case ShapeType::Multiply: return QObject::tr("Multiplicar");
    case ShapeType::Gear: return QObject::tr("Engranaje");
    case ShapeType::Heart: return QObject::tr("Corazón");
    case ShapeType::Custom: return QObject::tr("Forma personalizada");
    }
    return {};
}

QPainterPath shapePath(ShapeType type, const QRectF& rect, double cornerRadius)
{
    const QRectF r = rect.normalized();
    auto at = [&](qreal fx, qreal fy) { return QPointF(r.left() + fx * r.width(), r.top() + fy * r.height()); };
    auto polygon = [&](std::initializer_list<std::pair<qreal, qreal>> pts) {
        QPolygonF poly;
        for (const auto& [x, y] : pts)
            poly << at(x, y);
        QPainterPath p;
        p.addPolygon(poly);
        p.closeSubpath();
        return p;
    };
    auto regular = [&](int n, double startDeg, double innerRatio) {
        QPolygonF poly;
        const int count = innerRatio > 0 ? n * 2 : n;
        for (int i = 0; i < count; ++i) {
            const double a = (startDeg + i * 360.0 / count) * M_PI / 180.0;
            const double k = (innerRatio > 0 && i % 2 == 1) ? innerRatio : 1.0;
            poly << at(0.5 + 0.5 * k * std::cos(a), 0.5 + 0.5 * k * std::sin(a));
        }
        QPainterPath p;
        p.addPolygon(poly);
        p.closeSubpath();
        return p;
    };
    auto tail = [&](qreal top) { return polygon({{0.25, top - 0.05}, {0.45, top - 0.05}, {0.2, 1.0}}); };

    QPainterPath p;
    switch (type) {
    case ShapeType::Rectangle:
        p.addRect(r);
        break;
    case ShapeType::RoundedRect: {
        const qreal radius = std::min(cornerRadius, std::min(r.width(), r.height()) / 2.0);
        p.addRoundedRect(r, radius, radius);
        break;
    }
    case ShapeType::Ellipse: p.addEllipse(r); break;
    case ShapeType::Diamond: p = polygon({{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}); break;
    case ShapeType::Trapezoid: p = polygon({{0.2, 0}, {0.8, 0}, {1, 1}, {0, 1}}); break;
    case ShapeType::Parallelogram: p = polygon({{0.25, 0}, {1, 0}, {0.75, 1}, {0, 1}}); break;
    case ShapeType::Triangle: p = polygon({{0.5, 0}, {1, 1}, {0, 1}}); break;
    case ShapeType::RightTriangle: p = polygon({{0, 0}, {1, 1}, {0, 1}}); break;
    case ShapeType::Pentagon: p = regular(5, -90, 0); break;
    case ShapeType::Hexagon: p = regular(6, 0, 0); break;
    case ShapeType::Heptagon: p = regular(7, -90, 0); break;
    case ShapeType::Octagon: p = regular(8, 22.5, 0); break;
    case ShapeType::Star3: p = regular(3, -90, 0.3); break;
    case ShapeType::Star4: p = regular(4, -90, 0.4); break;
    case ShapeType::Star5: p = regular(5, -90, 0.382); break;
    case ShapeType::Star6: p = regular(6, -90, 0.55); break;
    case ShapeType::Arrow:
        p = polygon({{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}});
        break;
    case ShapeType::NotchedArrow:
        p = polygon({{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}, {0.15, 0.5}});
        break;
    case ShapeType::PentagonArrow: p = polygon({{0, 0}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0, 1}}); break;
    case ShapeType::ChevronArrow: p = polygon({{0, 0}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0, 1}, {0.25, 0.5}}); break;
    case ShapeType::RectCallout:
        p = polygon({{0, 0}, {1, 0}, {1, 0.75}, {0.45, 0.75}, {0.2, 1}, {0.25, 0.75}, {0, 0.75}});
        break;
    case ShapeType::RoundedRectCallout: {
        QPainterPath body;
        const QRectF b(at(0, 0), at(1, 0.75));
        const qreal radius = std::min(b.width(), b.height()) * 0.2;
        body.addRoundedRect(b, radius, radius);
        p = body.united(tail(0.75));
        break;
    }
    case ShapeType::EllipseCallout: {
        QPainterPath body;
        body.addEllipse(QRectF(at(0, 0), at(1, 0.8)));
        p = body.united(tail(0.72));
        break;
    }
    case ShapeType::CloudCallout: {
        QPainterPath body;
        const std::pair<qreal, qreal> puffs[] = {{0.25, 0.32}, {0.45, 0.22}, {0.68, 0.25}, {0.8, 0.45},
                                                 {0.65, 0.6},  {0.4, 0.62}, {0.2, 0.52}};
        for (const auto& [x, y] : puffs) {
            QPainterPath c;
            c.addEllipse(QRectF(at(x - 0.2, y - 0.2), at(x + 0.2, y + 0.2)));
            body = body.united(c);
        }
        body.addEllipse(QRectF(at(0.12, 0.8), at(0.2, 0.88)));
        body.addEllipse(QRectF(at(0.02, 0.92), at(0.07, 0.98)));
        p = body;
        break;
    }
    case ShapeType::LightningBolt:
        p = polygon({{0.55, 0}, {0.12, 0.56}, {0.42, 0.56}, {0.28, 1}, {0.88, 0.38}, {0.56, 0.38}, {0.76, 0}});
        break;
    case ShapeType::CheckMark: p = polygon({{0, 0.55}, {0.15, 0.4}, {0.38, 0.62}, {0.85, 0.1}, {1, 0.25}, {0.38, 0.92}}); break;
    case ShapeType::Multiply:
        p = polygon({{0.15, 0}, {0.5, 0.35}, {0.85, 0}, {1, 0.15}, {0.65, 0.5}, {1, 0.85}, {0.85, 1}, {0.5, 0.65},
                     {0.15, 1}, {0, 0.85}, {0.35, 0.5}, {0, 0.15}});
        break;
    case ShapeType::Gear: {
        QPolygonF poly;
        const int teeth = 8;
        for (int i = 0; i < teeth * 4; ++i) {
            const double a = (i * 360.0 / (teeth * 4) - 90) * M_PI / 180.0;
            const double k = (i % 4 < 2) ? 0.5 : 0.38;
            poly << at(0.5 + k * std::cos(a), 0.5 + k * std::sin(a));
        }
        p.addPolygon(poly);
        p.closeSubpath();
        p.addEllipse(QRectF(at(0.35, 0.35), at(0.65, 0.65)));
        p.setFillRule(Qt::OddEvenFill);
        break;
    }
    case ShapeType::Heart:
        p.moveTo(at(0.5, 0.25));
        p.cubicTo(at(0.5, 0.0), at(0.0, 0.0), at(0.0, 0.3));
        p.cubicTo(at(0.0, 0.6), at(0.35, 0.75), at(0.5, 1.0));
        p.cubicTo(at(0.65, 0.75), at(1.0, 0.6), at(1.0, 0.3));
        p.cubicTo(at(1.0, 0.0), at(0.5, 0.0), at(0.5, 0.25));
        p.closeSubpath();
        break;
    case ShapeType::Custom:
        p.addRect(r);
        break;
    }
    return p;
}

QPainterPath toolShapePath(const ToolSettings& s, const QRectF& r)
{
    if (s.shape == ShapeType::Custom)
        if (const CustomShapes::Shape* c = CustomShapes::find(s.customShape))
            return CustomShapes::fitted(c->path, r);
    return shapePath(s.shape, r, s.cornerRadius);
}

QString toolShapeName(const ToolSettings& s)
{
    if (s.shape == ShapeType::Custom)
        if (const CustomShapes::Shape* c = CustomShapes::find(s.customShape))
            return c->name;
    return shapeName(s.shape);
}

void stepShape(ToolSettings& s, int dir)
{
    // Primero las integradas (en el orden de la lista) y después las personalizadas.
    const QList<CustomShapes::Shape>& custom = CustomShapes::all();
    const int builtIn = int(allShapes().size());
    const int n = builtIn + int(custom.size());
    int i = int(s.shape);
    if (s.shape == ShapeType::Custom) {
        i = builtIn;
        for (int k = 0; k < int(custom.size()); ++k)
            if (custom[k].file == s.customShape)
                i = builtIn + k;
    }
    i = ((i + dir) % n + n) % n;
    if (i < builtIn) {
        s.shape = ShapeType(i);
    } else {
        s.shape = ShapeType::Custom;
        s.customShape = custom[i - builtIn].file;
    }
}
