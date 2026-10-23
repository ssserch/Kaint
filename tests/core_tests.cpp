// Pruebas del núcleo de Kaint (sin interfaz gráfica).

#include "core/Commands.h"
#include "core/CustomShapes.h"
#include "core/Document.h"
#include "core/FloodFill.h"
#include "core/Gif.h"
#include "core/ImageIO.h"
#include "core/Ora.h"
#include "core/Palette.h"
#include "core/Pdn.h"
#include "core/PngWriter.h"
#include "core/Raster.h"
#include "core/Resample.h"
#include "core/Stroke.h"
#include "core/effects/Effect.h"
#include "core/effects/EffectUtils.h"
#include "core/plugins/PdnBridge.h"
#include "core/plugins/PluginEffects.h"
#include "core/plugins/PluginManager.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImageReader>
#include <QRandomGenerator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <limits>

static int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

static bool sameImage(const QImage& a, const QImage& b)
{
    return a.convertToFormat(QImage::Format_ARGB32_Premultiplied) == b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

static void testStrokeAndUndo()
{
    std::unique_ptr<Document> doc(Document::createBlank(QSize(64, 64), Qt::white));
    const QImage original = doc->layer(0)->image.copy();

    StrokeEngine stroke;
    StrokeParams p;
    p.diameter = 9;
    p.color = QColor(255, 0, 0, 128);
    stroke.begin(doc.get(), doc->layerPtr(0), p);
    stroke.moveTo(QPointF(10, 10), 1.0);
    stroke.lineTo(QPointF(50, 10), 1.0);
    stroke.lineTo(QPointF(10, 10), 1.0);
    CHECK(stroke.end(QStringLiteral("Pincel")));

    const QRgb center = qUnpremultiply(doc->layer(0)->image.pixel(30, 10));
    CHECK(qRed(center) == 255);
    CHECK(std::abs(qGreen(center) - 127) <= 2);
    CHECK(doc->undoStack()->count() == 1);

    doc->undoStack()->undo();
    CHECK(sameImage(doc->layer(0)->image, original));
    doc->undoStack()->redo();
    CHECK(!sameImage(doc->layer(0)->image, original));
}

// Lápiz: tramos inclinados hacia la izquierda y hacia arriba sin huecos (antes se perdían los
// píxeles de la fila y la columna del final de cada tramo).
static void testPencilLines()
{
    std::unique_ptr<Document> doc(Document::createBlank(QSize(60, 60), Qt::white));
    StrokeEngine stroke;
    StrokeParams p;
    p.pixelExact = true;
    p.antialias = false;
    p.smoothing = false;
    p.diameter = 1;
    p.color = Qt::red;
    const QList<QPoint> pts{{50, 50}, {47, 48}, {40, 45}, {36, 40}, {35, 33}, {30, 31}, {22, 28}, {20, 20}, {28, 14}, {12, 9}, {5, 2}};
    stroke.begin(doc.get(), doc->layerPtr(0), p);
    stroke.moveTo(QPointF(pts.first()) + QPointF(0.5, 0.5), 1.0);
    for (const QPoint& pt : pts)
        stroke.lineTo(QPointF(pt) + QPointF(0.5, 0.5), 1.0);
    CHECK(stroke.end(QStringLiteral("Lápiz")));
    const QImage& img = doc->layer(0)->image;
    int gaps = 0;
    for (int i = 1; i < pts.size(); ++i) {
        // Bresenham, como el lápiz.
        int x0 = pts[i - 1].x(), y0 = pts[i - 1].y();
        const int x1 = pts[i].x(), y1 = pts[i].y();
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            gaps += QColor(img.pixel(x0, y0)) != QColor(Qt::red);
            if (x0 == x1 && y0 == y1)
                break;
            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
    CHECK(gaps == 0);
}

static void testSelection()
{
    const QSize canvas(100, 100);
    const Selection a = Selection::fromRect(QRect(0, 0, 50, 50), canvas);
    const Selection b = Selection::fromRect(QRect(25, 25, 50, 50), canvas);
    CHECK(a.isRectangular());
    const Selection u = a.combined(b, SelectionMode::Union);
    CHECK(u.coverage(10, 10) == 255 && u.coverage(70, 70) == 255 && u.coverage(90, 10) == 0);
    const Selection x = a.combined(b, SelectionMode::Exclude);
    CHECK(x.coverage(10, 10) == 255 && x.coverage(30, 30) == 0);
    const Selection i = a.combined(b, SelectionMode::Intersect);
    CHECK(i.bounds() == QRect(25, 25, 25, 25));
    const Selection inv = a.inverted(canvas);
    CHECK(inv.coverage(10, 10) == 0 && inv.coverage(80, 80) == 255);

    QImage mask(canvas, QImage::Format_Alpha8);
    mask.fill(0);
    mask.scanLine(5)[5] = 255;
    const Selection m = Selection::fromMask(mask);
    CHECK(m.bounds() == QRect(5, 5, 1, 1));
    CHECK(m.outline().size() == 4);
    CHECK(m.combined(a, SelectionMode::Union).coverage(40, 40) == 255);
}

static void testFloodFill()
{
    QImage img(20, 20, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter(&img).fillRect(10, 0, 1, 20, Qt::black);
    const QImage contiguous = FloodFill::mask(img, QPoint(2, 2), 0.5, true);
    CHECK(contiguous.scanLine(5)[5] == 255);
    CHECK(contiguous.scanLine(5)[10] == 0);
    CHECK(contiguous.scanLine(5)[15] == 0);
    const QImage global = FloodFill::mask(img, QPoint(2, 2), 0.5, false);
    CHECK(global.scanLine(5)[15] == 255);
}

static void testOraRoundTrip()
{
    QTemporaryDir dir;
    std::unique_ptr<Document> doc(Document::createBlank(QSize(32, 16), Qt::white));
    auto top = std::make_shared<Layer>(QStringLiteral("Arriba ñ"), QSize(32, 16));
    top->image.fill(QColor(0, 0, 255, 200));
    top->props.opacity = 128;
    top->props.blend = BlendMode::Multiply;
    top->props.visible = false;
    doc->insertLayer(1, top);

    const QString path = dir.filePath(QStringLiteral("prueba.ora"));
    QString error;
    CHECK(Ora::save(doc.get(), path, &error));
    std::unique_ptr<Document> loaded(ImageIO::load(path, &error));
    CHECK(loaded != nullptr);
    if (!loaded)
        return;
    CHECK(loaded->size() == QSize(32, 16));
    CHECK(loaded->layerCount() == 2);
    CHECK(loaded->layer(1)->props.name == QStringLiteral("Arriba ñ"));
    CHECK(loaded->layer(1)->props.blend == BlendMode::Multiply);
    CHECK(loaded->layer(1)->props.opacity == 128);
    CHECK(!loaded->layer(1)->props.visible);
    CHECK(sameImage(loaded->layer(1)->image, top->image));
    CHECK(!loaded->isModified());
}

static void testEffects()
{
    QImage src(48, 40, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            src.setPixel(x, y, qPremultiply(qRgba(x * 5, y * 6, 128, 255 - x)));
    const Selection sel = Selection::fromRect(QRect(5, 5, 30, 20), src.size());
    std::atomic_bool cancel{false};
    for (const Effect* e : Effects::all()) {
        const QImage whole = Effects::apply(*e, src, Selection(), Effects::defaults(*e), EffectContext(), cancel);
        const QImage part = Effects::apply(*e, src, sel, Effects::defaults(*e), EffectContext(), cancel);
        CHECK(whole.size() == src.size());
        CHECK(part.pixel(0, 0) == src.pixel(0, 0));
        if (whole.size() != src.size())
            std::fprintf(stderr, "  efecto: %s\n", qPrintable(e->id()));
    }
    const Effect* invert = Effects::find(QStringLiteral("invert"));
    CHECK(invert != nullptr);
    if (invert) {
        const QImage once = Effects::apply(*invert, src, Selection(), {}, EffectContext(), cancel);
        const QImage twice = Effects::apply(*invert, once, Selection(), {}, EffectContext(), cancel);
        CHECK(twice == src);
    }
}

// Las distorsiones (tan() en Reflejo en mosaico) dan coordenadas enormes o NaN: el muestreo de
// los bordes no debe quedarse en un bucle.
static void testEdgeSampling()
{
    QImage img(16, 8, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgba(x * 10, y * 20, 30, 255));
    const ConstPixels in(img);
    for (float v : {1e12f, -3e15f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(qAlpha(fx::sampleReflected(in, v, 4.f)) == 255);
        CHECK(qAlpha(fx::sampleWrapped(in, 3.f, v)) == 255);
        CHECK(qAlpha(sampleBilinear(in, v, v)) == 255);
    }
    CHECK(qRed(fx::sampleReflected(in, 18.5f, 0.5f)) == 130);
    CHECK(qRed(fx::sampleReflected(in, -2.5f, 0.5f)) == 20);
    CHECK(qRed(fx::sampleWrapped(in, 18.5f, 0.5f)) == 20);

    // Reflejo en mosaico con la curvatura máxima tiene que terminar.
    if (const Effect* e = Effects::find(QStringLiteral("tilereflection"))) {
        QImage src(300, 200, QImage::Format_ARGB32_Premultiplied);
        src.fill(qRgba(40, 80, 120, 255));
        ParamValues v = Effects::defaults(*e);
        v[QStringLiteral("curvature")] = 100.0;
        v[QStringLiteral("size")] = 3.0;
        std::atomic_bool cancel{false};
        QElapsedTimer t;
        t.start();
        const QImage out = Effects::apply(*e, src, Selection(), v, EffectContext(), cancel);
        CHECK(out.size() == src.size());
        CHECK(t.elapsed() < 20000);
    }

    // "Calidad" de Mediana y Contorno: bits de precisión del percentil.
    const QRgb c = qRgba(200, 10, 255, 128);
    CHECK(fx::quantizeBits(c, 8) == c && fx::quantizeBits(c, 9) == c && fx::quantizeBits(c, 0) == c);
    CHECK(fx::quantizeBits(c, 4) == qRgba(199, 8, 247, 135));
    CHECK(fx::quantizeBits(c, 1) == qRgba(191, 64, 191, 191));
}

// Formas personalizadas de Paint.NET (.xaml): sintaxis de trazados y modelo de objetos.
static void testCustomShapes()
{
    bool ok = false;
    QPainterPath house = CustomShapes::parsePathMarkup(QStringLiteral("F1 M 21,142 L 160,22 L 300,142 L 300,318 L 21,318 Z"), &ok);
    CHECK(ok && house.fillRule() == Qt::WindingFill);
    CHECK(house.boundingRect() == QRectF(21, 22, 279, 296));
    CHECK(house.contains(QPointF(160, 200)) && !house.contains(QPointF(30, 40)));
    const QPainterPath box = CustomShapes::parsePathMarkup(QStringLiteral("m10,10 h10 v10 h-10 z"), &ok);
    CHECK(ok && box.fillRule() == Qt::OddEvenFill && box.boundingRect() == QRectF(10, 10, 10, 10));
    const QPainterPath circle = CustomShapes::parsePathMarkup(QStringLiteral("M 0,50 A 50,50 0 1 1 100,50 A 50,50 0 1 1 0,50 Z"), &ok);
    const QRectF cb = circle.boundingRect();
    CHECK(ok && std::abs(cb.left()) < 0.5 && std::abs(cb.top()) < 0.5 && std::abs(cb.right() - 100) < 0.5
          && std::abs(cb.bottom() - 100) < 0.5);
    CHECK(circle.contains(QPointF(50, 50)) && circle.contains(QPointF(50, 3)) && !circle.contains(QPointF(5, 5)));
    CustomShapes::parsePathMarkup(QStringLiteral("M0 0C10 0 10 10 0 10S-10 20 0 20Q5 25 10 20T20 20L1e1-5z"), &ok);
    CHECK(ok);
    CustomShapes::parsePathMarkup(QStringLiteral("M 0,0 L x"), &ok);
    CHECK(!ok);
    CustomShapes::parsePathMarkup(QStringLiteral("M 0,0 L 5,5 Z 3 4"), &ok);
    CHECK(!ok);

    // Modelo de objetos: grupos, transformaciones, segmentos y combinaciones.
    const QByteArray xaml = R"(<ps:SimpleGeometryShape
    xmlns="clr-namespace:PaintDotNet.UI.Media;assembly=PaintDotNet.Framework"
    xmlns:ps="clr-namespace:PaintDotNet.Shapes;assembly=PaintDotNet.Framework" DisplayName="Prueba">
  <GeometryGroup FillRule="Nonzero">
    <EllipseGeometry Center="50,50" RadiusX="10" RadiusY="10" />
    <RectangleGeometry Rect="0,0,20,10">
      <RectangleGeometry.Transform>
        <TransformGroup>
          <ScaleTransform ScaleX="2" ScaleY="2" />
          <TranslateTransform X="100" Y="0" />
        </TransformGroup>
      </RectangleGeometry.Transform>
    </RectangleGeometry>
    <PathGeometry>
      <PathGeometry.Figures>
        <PathFigureCollection>
          <PathFigure StartPoint="0,100" IsClosed="True">
            <LineSegment Point="40,100" />
            <ArcSegment Point="40,140" Size="20,20" SweepDirection="Clockwise" />
            <PolyCurveSegment Points="20,150 0,140" />
          </PathFigure>
        </PathFigureCollection>
      </PathGeometry.Figures>
    </PathGeometry>
    <CombinedGeometry GeometryCombineMode="Exclude" Geometry1="M 200,0 L 260,0 L 260,60 L 200,60 Z">
      <CombinedGeometry.Geometry2>
        <EllipseGeometry Center="230,30" RadiusX="10" RadiusY="10" />
      </CombinedGeometry.Geometry2>
    </CombinedGeometry>
  </GeometryGroup>
</ps:SimpleGeometryShape>)";
    CustomShapes::Shape shape;
    QString error;
    CHECK(CustomShapes::parse(xaml, &shape, &error));
    CHECK(shape.name == QStringLiteral("Prueba"));
    CHECK(shape.path.contains(QPointF(50, 50)));
    CHECK(shape.path.contains(QPointF(139, 19)) && !shape.path.contains(QPointF(19, 5)));
    CHECK(shape.path.contains(QPointF(55, 120)));
    CHECK(shape.path.contains(QPointF(205, 5)) && !shape.path.contains(QPointF(230, 30)));
    CHECK(!CustomShapes::parse("<ps:Otra xmlns:ps=\"x\"/>", &shape, &error) && !error.isEmpty());
    CHECK(!CustomShapes::parse("<SimpleGeometryShape><Desconocida/></SimpleGeometryShape>", &shape, &error));

    // Ajustada al rectángulo de la herramienta.
    const QPainterPath fit = CustomShapes::fitted(house, QRectF(10, 20, 100, 50));
    const QRectF fb = fit.boundingRect();
    CHECK(std::abs(fb.left() - 10) < 1e-6 && std::abs(fb.top() - 20) < 1e-6 && std::abs(fb.width() - 100) < 1e-6
          && std::abs(fb.height() - 50) < 1e-6);

    // Búsqueda en carpetas: orden alfabético por nombre y los archivos rotos se omiten.
    QTemporaryDir dir;
    auto write = [&](const QString& name, const QByteArray& data) {
        QFile f(dir.filePath(name));
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
    };
    write(QStringLiteral("b.xaml"), "<SimpleGeometryShape DisplayName=\"Zeta\" Geometry=\"M0,0 L10,0 L5,8 Z\"/>");
    write(QStringLiteral("a.xaml"), "<SimpleGeometryShape Geometry=\"M0,0 L10,0 L10,10 Z\"/>");
    write(QStringLiteral("roto.xaml"), "<SimpleGeometryShape Geometry=\"M0,0 L\"/>");
    CustomShapes::setSearchPathsForTesting({dir.path()});
    const QList<CustomShapes::Shape>& all = CustomShapes::all();
    CHECK(all.size() == 2 && all.value(0).name == QStringLiteral("a") && all.value(1).name == QStringLiteral("Zeta"));
    CHECK(CustomShapes::find(dir.filePath(QStringLiteral("b.xaml"))) != nullptr);
    CustomShapes::setSearchPathsForTesting({});
}

static void testMovePixels()
{
    std::unique_ptr<Document> doc(Document::createBlank(QSize(40, 40), Qt::white));
    QPainter(&doc->layer(0)->image).fillRect(5, 5, 10, 10, Qt::red);
    const QImage original = doc->layer(0)->image.copy();
    doc->setSelection(Selection::fromRect(QRect(5, 5, 10, 10), doc->size()));

    auto f = std::make_shared<FloatingPixels>();
    f->layer = doc->layerPtr(0);
    f->original = f->layer->image;
    f->pixels = Raster::extractSelection(f->original, doc->selection());
    f->base = f->original.copy();
    Raster::eraseSelection(f->base, doc->selection());
    f->origin = doc->selection().bounds().topLeft();
    f->selectionBase = doc->selection();

    const QTransform t1 = QTransform::fromTranslate(20, 0);
    doc->undoStack()->push(new MovePixelsCommand(doc.get(), f, QTransform(), t1, true, false, doc->selection(),
                                                 QStringLiteral("Mover")));
    const QTransform t2 = QTransform::fromTranslate(20, 20);
    doc->undoStack()->push(new MovePixelsCommand(doc.get(), f, t1, t2, false, false, {}, QStringLiteral("Mover")));
    // Cada movimiento es un paso (como en Paint.NET): deshacer uno vuelve al anterior.
    CHECK(doc->undoStack()->count() == 2);
    CHECK(QColor(doc->layer(0)->image.pixel(30, 30)) == QColor(Qt::red));
    CHECK(qAlpha(doc->layer(0)->image.pixel(30, 10)) == 0 || QColor(doc->layer(0)->image.pixel(30, 10)) != QColor(Qt::red));
    CHECK(doc->selection().bounds() == QRect(25, 25, 10, 10));
    doc->undoStack()->undo();
    CHECK(QColor(doc->layer(0)->image.pixel(30, 10)) == QColor(Qt::red) && QColor(doc->layer(0)->image.pixel(30, 30)) != QColor(Qt::red));
    CHECK(doc->selection().bounds() == QRect(25, 5, 10, 10));
    doc->undoStack()->undo();
    CHECK(sameImage(doc->layer(0)->image, original));
    CHECK(doc->selection().bounds() == QRect(5, 5, 10, 10));
}

static void testBlendModes()
{
    // Fondo gris opaco, encima negro al 100 %: cada modo con su fórmula de Paint.NET.
    auto blendPixel = [](QRgb bottom, QRgb top, BlendMode mode) {
        QImage dst(1, 1, QImage::Format_ARGB32_Premultiplied);
        dst.setPixel(0, 0, qPremultiply(bottom));
        QImage src(1, 1, QImage::Format_ARGB32_Premultiplied);
        src.setPixel(0, 0, qPremultiply(top));
        blendImage(dst, QPoint(0, 0), src, mode);
        return qUnpremultiply(dst.pixel(0, 0));
    };
    const QRgb gray = qRgba(100, 150, 200, 255);
    CHECK(qRed(blendPixel(gray, qRgba(100, 100, 100, 255), BlendMode::Additive)) == 200);
    CHECK(qGreen(blendPixel(gray, qRgba(150, 150, 150, 255), BlendMode::Additive)) == 255);
    CHECK(qRed(blendPixel(gray, qRgba(100, 100, 100, 255), BlendMode::Xor)) == (100 ^ 100));
    CHECK(qBlue(blendPixel(gray, qRgba(55, 55, 55, 255), BlendMode::Negation)) == 255 - std::abs(255 - 200 - 55));
    CHECK(qRed(blendPixel(gray, qRgba(0, 0, 0, 255), BlendMode::Reflect)) == 100 * 100 / 255);
    CHECK(qAlpha(blendPixel(gray, qRgba(10, 10, 10, 128), BlendMode::Glow)) == 255);
    CHECK(blendPixel(gray, qRgba(0, 0, 0, 0), BlendMode::Overwrite) == 0);
    CHECK(layerBlendModes().size() == 14 && toolBlendModes().size() == 15);
}

// Reducir con la herramienta Mover: con el prefiltro, unas rayas de 1 píxel quedan grises (sin
// el, la bicúbica se salta píxeles y sale pixelado).
static void testShrinkTransform()
{
    QImage stripes(256, 256, QImage::Format_ARGB32);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            stripes.setPixel(x, y, (x + y / 3) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    QTransform t = QTransform::fromScale(0.125, 0.125) * QTransform().rotate(7);
    const QImage src = Resample::prefiltered(stripes, &t);
    CHECK(src.width() < 64 && src.height() < 64);
    QImage out(48, 48, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    Resample::drawTransformed(out, src, t, Resample::Interpolation::Bicubic, out.rect());
    int extreme = 0;
    for (int y = 10; y < 26; ++y)
        for (int x = 8; x < 24; ++x)
            if (qAlpha(out.pixel(x, y)) == 255 && (qGray(out.pixel(x, y)) < 90 || qGray(out.pixel(x, y)) > 165))
                ++extreme;
    CHECK(extreme == 0);
}

static void testResample()
{
    QImage src(40, 30, QImage::Format_ARGB32_Premultiplied);
    src.fill(QColor(200, 100, 50));
    for (Resample::Method m : Resample::all()) {
        const QImage up = Resample::scale(src, QSize(97, 61), m);
        const QImage down = Resample::scale(src, QSize(13, 7), m, true);
        CHECK(up.size() == QSize(97, 61) && down.size() == QSize(13, 7));
        CHECK(std::abs(QColor(up.pixel(50, 30)).red() - 200) <= 1);
        CHECK(std::abs(QColor(down.pixel(6, 3)).green() - 100) <= 1);
    }
}

static void testPaletteAndJson()
{
    const QList<QColor> pal = Palette::defaultColors();
    CHECK(pal.size() == 96 && pal[2] == QColor(255, 0, 0) && pal[64].alpha() == 128);
    const QList<QColor> parsed = Palette::parse(Palette::serialize(pal));
    CHECK(parsed == pal);
    CHECK(Palette::parse(QStringLiteral("; comentario\nFF00FF00\n")).first() == QColor(0, 255, 0));

    const Selection rect = Selection::fromRect(QRect(3, 4, 6, 15), QSize(50, 50));
    bool ok = false;
    const Selection back = Selection::fromJson(rect.toJson(), QSize(50, 50), &ok);
    CHECK(ok && back.bounds() == rect.bounds());
    QImage mask(50, 50, QImage::Format_Alpha8);
    mask.fill(0);
    for (int y = 10; y < 20; ++y)
        for (int x = 5; x < 9 + (y > 15 ? 6 : 0); ++x)
            mask.scanLine(y)[x] = 255;
    const Selection fromMask = Selection::fromMask(mask);
    const Selection maskBack = Selection::fromJson(fromMask.toJson(), QSize(50, 50), &ok);
    CHECK(ok && maskBack.bounds() == fromMask.bounds() && maskBack.coverage(13, 17) == 255 && maskBack.coverage(13, 12) == 0);
}

static void testSaveOptions()
{
    // Degradado con transparencia: PNG automático = 32 bits; 8 bits = paleta.
    std::unique_ptr<Document> doc(Document::createBlank(QSize(96, 64), Qt::transparent));
    QImage& img = doc->layer(0)->image;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 96; ++x)
            img.setPixel(x, y, qPremultiply(qRgba(x * 2, y * 4, 200, x < 8 ? 0 : 255)));
    QTemporaryDir dir;
    ImageIO::SaveOptions o;
    QString error;
    CHECK(ImageIO::automaticBitDepth(doc->flattened()) == 32);
    o.bitDepth = 8;
    const QString png8 = dir.filePath(QStringLiteral("a.png"));
    CHECK(ImageIO::save(doc.get(), png8, o, &error));
    const QImage back(png8);
    CHECK(back.format() == QImage::Format_Indexed8 || back.colorCount() <= 256);
    CHECK(qAlpha(back.pixel(2, 10)) == 0 && qAlpha(back.pixel(50, 10)) == 255);
    o.bitDepth = 24;
    const QImage opaque = ImageIO::prepare(doc->flattened(), png8, o);
    CHECK(!opaque.hasAlphaChannel() && QColor(opaque.pixel(2, 10)) == QColor(Qt::white));
    ImageIO::SaveOptions low, high;
    low.quality = 10;
    high.quality = 100;
    const QString jpg = dir.filePath(QStringLiteral("b.jpg"));
    const QImage prepared = ImageIO::prepare(doc->flattened(), jpg, low);
    const QByteArray small = ImageIO::encode(prepared, jpg, low, &error);
    const QByteArray big = ImageIO::encode(prepared, jpg, high, &error);
    CHECK(!small.isEmpty() && small.size() < big.size());
}

// Bote de pintura con suavizado: un círculo con contorno suavizado se rellena de un clic sin
// dejar halo de píxeles intermedios junto al borde.
static void testBucketHalo()
{
    // Blanco con un círculo negro suavizado (2,5 px de grosor, centro fraccionario).
    QImage drawn(120, 100, QImage::Format_ARGB32_Premultiplied);
    drawn.fill(Qt::white);
    {
        QPainter p(&drawn);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::black, 2.5));
        p.drawEllipse(QPointF(60.3, 49.7), 37.4, 33.1);
    }
    const QImage before = drawn.convertToFormat(kLayerFormat);

    // "Halo": restos del blanco de dentro. Con el relleno rojo puro y el contorno negro, el
    // blanco es lo único que aporta verde.
    auto halo = [&](const QImage& img) {
        int worst = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const double dx = (x + 0.5 - 60.3) / 37.4, dy = (y + 0.5 - 49.7) / 33.1;
                if (std::hypot(dx, dy) < 1.0)
                    worst = std::max(worst, qGreen(img.pixel(x, y)));
            }
        return worst;
    };

    FloodFill::BucketOptions o;
    o.tolerance = 0.5;
    o.primary = QColor(255, 0, 0);
    o.accumulate = false;
    QImage layer = before.copy();
    const QRect changed = FloodFill::paintBucket(layer, before, before, QPoint(60, 50), o, Selection());
    CHECK(!changed.isEmpty());
    CHECK(QColor(layer.pixel(60, 50)) == QColor(255, 0, 0));
    std::printf("  halo del bote de pintura: %d (sin suavizado: ", halo(layer));
    CHECK(halo(layer) <= 24);
    bool outsideSame = true;
    for (int y = 0; y < layer.height(); ++y)
        for (int x = 0; x < layer.width(); ++x) {
            const double dx = (x + 0.5 - 60.3) / 37.4, dy = (y + 0.5 - 49.7) / 33.1;
            if (std::hypot(dx, dy) > 1.1 && layer.pixel(x, y) != before.pixel(x, y))
                outsideSame = false;
        }
    CHECK(outsideSame);
    CHECK(qRed(layer.pixel(60, 49 - 33)) < 60 || qRed(layer.pixel(60, 50 - 33)) < 60);

    // Sin suavizado queda el halo que describía el problema (sirve de referencia).
    o.antialias = false;
    QImage hard = before.copy();
    FloodFill::paintBucket(hard, before, before, QPoint(60, 50), o, Selection());
    std::printf("%d)\n", halo(hard));
    CHECK(halo(hard) > halo(layer));

    // Con otro modo de fusión se cubre el borde en proporción (también reduce el halo).
    o.antialias = true;
    o.blend = BlendMode::Multiply;
    QImage multiplied = before.copy();
    FloodFill::paintBucket(multiplied, before, before, QPoint(60, 50), o, Selection());
    CHECK(halo(multiplied) < halo(hard));

    // Acumulando tampoco queda halo: el medio píxel de más se mezcla sobre lo ya desmezclado.
    o.blend = BlendMode::Normal;
    o.accumulate = true;
    QImage accumulated = before.copy();
    FloodFill::paintBucket(accumulated, before, before, QPoint(60, 50), o, Selection());
    std::printf("  halo acumulando: %d\n", halo(accumulated));
    CHECK(halo(accumulated) <= 24);
}

// Rellenos acumulados: como en Paint.NET, cada clic del cubo con suavizado sale medio píxel de la
// zona, así que repetirlo engrosa una silueta; sin acumular no crece. Acumular tampoco deja halo.
static void testBucketAccumulate()
{
    QImage silhouette(80, 80, kLayerFormat);
    silhouette.fill(kTransparent);
    {
        QImage pm(silhouette.size(), QImage::Format_ARGB32_Premultiplied);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawEllipse(QPointF(40, 40), 15.3, 15.3);
        p.end();
        silhouette = pm.convertToFormat(kLayerFormat);
    }
    auto opaque = [](const QImage& img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                n += qAlpha(img.pixel(x, y)) >= 128;
        return n;
    };
    auto clicks = [&](bool accumulate, int times) {
        FloodFill::BucketOptions o;
        o.primary = Qt::black;
        o.accumulate = accumulate;
        QImage layer = silhouette;
        for (int i = 0; i < times; ++i) {
            const QImage before = layer;
            FloodFill::paintBucket(layer, before, before, QPoint(40, 40), o, Selection());
        }
        return layer;
    };
    const int start = opaque(silhouette);
    const int once = opaque(clicks(true, 1)), many = opaque(clicks(true, 6));
    std::printf("  silueta acumulada: %d → %d → %d píxeles\n", start, once, many);
    CHECK(once >= start && many > once + 150);
    CHECK(opaque(clicks(false, 6)) == opaque(clicks(false, 1)));
    // Un píxel lejano no cambia: crece medio píxel por clic, no se desborda.
    CHECK(qAlpha(clicks(true, 6).pixel(2, 2)) == 0);
}

// Contorno suavizado de una máscara (varita mágica con antidentado): escalones como diagonales.
static void testSmoothMaskSelection()
{
    QImage mask(40, 40, QImage::Format_Alpha8);
    mask.fill(0);
    for (int y = 10; y < 30; ++y)
        for (int x = 10; x < 10 + (y - 10) + 1; ++x)
            mask.scanLine(y)[x] = 255;
    const Selection pixel = Selection::fromMask(mask);
    const Selection smooth = Selection::fromMaskSmooth(mask);
    CHECK(!pixel.isPathBased() && smooth.isPathBased());
    int partial = 0;
    long area = 0, smoothArea = 0;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            const int c = smooth.coverage(x, y);
            partial += c > 0 && c < 255;
            area += pixel.coverage(x, y);
            smoothArea += c;
        }
    CHECK(partial > 15);
    CHECK(std::abs(smoothArea - area) < area / 10);
    CHECK(smooth.coverage(12, 25) == 255 && smooth.coverage(25, 12) == 0);
}

// Capas con alfa directo (como ColorBgra de Paint.NET).
static void testStraightAlpha()
{
    // Imagen con colores en píxeles totalmente transparentes.
    QImage img(32, 24, kLayerFormat);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgba(x * 7, y * 9, (x * y) & 255, (x * 37 + y * 11) % 3 == 0 ? 0 : (x * 8) & 255));
    std::atomic_bool cancel{false};

    // Invertir alfa dos veces devuelve la imagen original exacta.
    const Effect* invAlpha = Effects::find(QStringLiteral("invertalpha"));
    CHECK(invAlpha != nullptr);
    if (invAlpha) {
        const QImage once = Effects::apply(*invAlpha, img, Selection(), {}, EffectContext(), cancel);
        CHECK(once.format() == kLayerFormat);
        CHECK(qAlpha(once.pixel(0, 0)) == 255 && qRed(once.pixel(3, 0)) == 21);
        const QImage twice = Effects::apply(*invAlpha, once, Selection(), {}, EffectContext(), cancel);
        CHECK(twice == img);
        const Selection sel = Selection::fromRect(QRect(4, 4, 10, 10), img.size());
        const QImage part = Effects::apply(*invAlpha, img, sel, {}, EffectContext(), cancel);
        CHECK(Effects::apply(*invAlpha, part, sel, {}, EffectContext(), cancel) == img);
    }

    // Un efecto espacial (desenfoque con selección) no toca los píxeles de fuera.
    if (const Effect* blur = Effects::find(QStringLiteral("gaussianblur"))) {
        const Selection sel = Selection::fromRect(QRect(8, 8, 8, 8), img.size());
        const QImage out = Effects::apply(*blur, img, sel, Effects::defaults(*blur), EffectContext(), cancel);
        CHECK(out.format() == kLayerFormat);
        bool outsideSame = true;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (!sel.bounds().contains(x, y) && out.pixel(x, y) != img.pixel(x, y))
                    outsideSame = false;
        CHECK(outsideSame);
    }

    // Capa nueva: blanco transparente; borrar la selección deja blanco transparente.
    const Layer layer(QStringLiteral("x"), QSize(4, 4));
    CHECK(layer.image.format() == kLayerFormat && layer.image.pixel(1, 1) == kTransparent);
    QImage erased = img.copy();
    Raster::eraseSelection(erased, Selection::fromRect(QRect(0, 0, 2, 2), img.size()));
    CHECK(erased.pixel(1, 1) == kTransparent && erased.pixel(5, 5) == img.pixel(5, 5));

    // Pintar con el pincel no altera el color de los píxeles transparentes que no toca.
    std::unique_ptr<Document> doc(Document::createBlank(img.size(), Qt::white));
    doc->layer(0)->image = img;
    StrokeEngine stroke;
    StrokeParams p;
    p.diameter = 4;
    p.color = QColor(10, 200, 30, 180);
    stroke.begin(doc.get(), doc->layerPtr(0), p);
    stroke.moveTo(QPointF(4, 4), 1.0);
    stroke.lineTo(QPointF(12, 4), 1.0);
    stroke.end(QStringLiteral("Pincel"));
    CHECK(doc->layer(0)->image.pixel(20, 20) == img.pixel(20, 20));
    CHECK(doc->layer(0)->image.pixel(8, 4) != img.pixel(8, 4));
    doc->undoStack()->undo();
    CHECK(doc->flattened() == img);

    // Los modos de fusión dan lo mismo sobre una capa (alfa directo) que sobre la caché de
    // pantalla (premultiplicada), salvo redondeo.
    QImage top(img.size(), kLayerFormat);
    for (int y = 0; y < top.height(); ++y)
        for (int x = 0; x < top.width(); ++x)
            top.setPixel(x, y, qRgba(255 - x * 7, (y * 13) & 255, 90, 60 + x * 6));
    QImage bottom(img.size(), kLayerFormat);
    bottom.fill(QColor(120, 60, 200, 230));
    for (BlendMode m : layerBlendModes()) {
        QImage straight = bottom.copy();
        blendImage(straight, QPoint(0, 0), top, m, 200);
        QImage premul = bottom.convertToFormat(kPremulFormat);
        blendImage(premul, QPoint(0, 0), top, m, 200);
        const QImage back = premul.convertToFormat(kLayerFormat);
        int worst = 0;
        for (int y = 0; y < top.height(); ++y)
            for (int x = 0; x < top.width(); ++x) {
                const QRgb a = straight.pixel(x, y), b = back.pixel(x, y);
                worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                                  std::abs(qBlue(a) - qBlue(b)), std::abs(qAlpha(a) - qAlpha(b))});
            }
        if (worst > 3)
            std::fprintf(stderr, "  modo %s: diferencia %d\n", qPrintable(blendModeName(m)), worst);
        CHECK(worst <= 3);
    }

    // OpenRaster guarda las capas con alfa directo: el color transparente vuelve igual.
    QTemporaryDir dir;
    std::unique_ptr<Document> two(Document::createBlank(img.size(), Qt::white));
    auto colored = std::make_shared<Layer>(QStringLiteral("c"), img);
    two->insertLayer(1, colored);
    const QString path = dir.filePath(QStringLiteral("alfa.ora"));
    QString error;
    CHECK(Ora::save(two.get(), path, &error));
    std::unique_ptr<Document> loaded(ImageIO::load(path, &error));
    CHECK(loaded && loaded->layerCount() == 2 && loaded->layer(1)->image == img);

    // Vecino más cercano: copia exacta de píxeles (con su color transparente).
    const QImage doubled = Resample::scale(img, img.size() * 2, Resample::Method::NearestNeighbor);
    CHECK(doubled.format() == kLayerFormat && doubled.pixel(6, 6) == img.pixel(3, 3));
}

static int changedPixels(const QImage& a, const QImage& b)
{
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            n += a.pixel(x, y) != b.pixel(x, y);
    return n;
}

static QImage testPattern(int w, int h)
{
    QImage img(w, h, kLayerFormat);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgba(x * 4 & 255, y * 4 & 255, (x * y) & 255, 255));
    return img;
}

// GIF animado (capas = fotogramas, duración en el nombre) e iconos multitamaño.
static void testGifAndIco()
{
    CHECK(Gif::delayFromName(QStringLiteral("Fotograma 2 (250ms)"), 100) == 250);
    CHECK(Gif::delayFromName(QStringLiteral("Fondo"), 100) == 100);
    CHECK(Gif::nameWithDelay(QStringLiteral("Fotograma 2 (250ms)"), 80) == QLatin1String("Fotograma 2 (80ms)"));
    CHECK(Gif::nameWithDelay(QStringLiteral("Sol"), 40) == QLatin1String("Sol (40ms)"));

    QTemporaryDir dir;
    std::unique_ptr<Document> doc(Document::createBlank(QSize(40, 30), Qt::red));
    doc->layer(0)->props.name = QStringLiteral("Fotograma 1 (120ms)");
    auto second = std::make_shared<Layer>(QStringLiteral("Fotograma 2"), QSize(40, 30));
    second->image.fill(qRgba(0, 0, 255, 255));
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            second->image.setPixel(x, y, kTransparent);
    doc->insertLayer(1, second);
    auto hidden = std::make_shared<Layer>(QStringLiteral("Oculta"), QSize(40, 30), Qt::green);
    hidden->props.visible = false;
    doc->insertLayer(2, hidden);
    auto third = std::make_shared<Layer>(QStringLiteral("Fotograma 3 (300ms)"), QSize(40, 30), Qt::yellow);
    doc->insertLayer(3, third);

    const QString path = dir.filePath(QStringLiteral("anim.gif"));
    ImageIO::SaveOptions o;
    o.frameDelay = 70;
    o.loops = 3;
    QString error;
    CHECK(ImageIO::save(doc.get(), path, o, &error));
    std::unique_ptr<Document> back(ImageIO::load(path, &error));
    CHECK(back);
    if (back) {
        CHECK(back->layerCount() == 3);
        CHECK(back->gifLoops() == 3);
        CHECK(back->layerCount() == 3 && Gif::delayFromName(back->layer(0)->props.name, 0) == 120);
        CHECK(back->layerCount() == 3 && Gif::delayFromName(back->layer(1)->props.name, 0) == 70);
        CHECK(back->layerCount() == 3 && Gif::delayFromName(back->layer(2)->props.name, 0) == 300);
        CHECK(back->layer(0)->image.pixel(20, 20) == qRgba(255, 0, 0, 255));
        if (back->layerCount() == 3) {
            CHECK(back->layer(1)->image.pixel(20, 20) == qRgba(0, 0, 255, 255));
            CHECK(qAlpha(back->layer(1)->image.pixel(3, 3)) == 0);
            CHECK(back->layer(2)->image.pixel(5, 5) == qRgba(255, 255, 0, 255));
        }
    }
    o.loops = -1;
    CHECK(ImageIO::save(doc.get(), path, o, &error));
    back.reset(ImageIO::load(path, &error));
    CHECK(back && back->gifLoops() == -1);

    // Muchos colores e imagen grande: el LZW reinicia la tabla varias veces.
    QImage noise(320, 240, kLayerFormat);
    QRandomGenerator rng(7);
    for (int y = 0; y < noise.height(); ++y)
        for (int x = 0; x < noise.width(); ++x)
            noise.setPixel(x, y, qRgba(x & 255, (y * 3) & 255, int(rng.bounded(256)), 255));
    const QImage indexed = ImageIO::toIndexed8(noise, 0, 128);
    const QByteArray data = Gif::encode({Gif::Frame{noise, 100}}, Gif::Options{0, 0, 128}, &error);
    QImage decoded;
    CHECK(decoded.loadFromData(data, "GIF"));
    CHECK(decoded.size() == noise.size());
    bool same = !decoded.isNull();
    for (int y = 0; same && y < noise.height(); ++y)
        for (int x = 0; x < noise.width(); ++x)
            if ((decoded.pixel(x, y) | 0xff000000u) != (indexed.pixel(x, y) | 0xff000000u)) {
                same = false;
                break;
            }
    CHECK(same);

    // Icono con tres tamaños; al abrirlo se usa el más grande.
    QImage logo(40, 30, kLayerFormat);
    logo.fill(qRgba(0, 128, 255, 200));
    const QByteArray ico = Ico::encode(logo, {16, 32, 256}, &error);
    const QString icoPath = dir.filePath(QStringLiteral("logo.ico"));
    {
        QFile f(icoPath);
        CHECK(f.open(QIODevice::WriteOnly) && f.write(ico) == ico.size());
    }
    QImageReader reader(icoPath);
    CHECK(reader.imageCount() == 3);
    std::unique_ptr<Document> icon(ImageIO::load(icoPath, &error));
    CHECK(icon && icon->size() == QSize(256, 256));
    if (icon) {
        const QRgb c = icon->layer(0)->image.pixel(128, 128);
        CHECK(std::abs(qBlue(c) - 255) <= 2 && std::abs(qAlpha(c) - 200) <= 2);
        CHECK(qAlpha(icon->layer(0)->image.pixel(128, 5)) == 0);
    }
    ImageIO::SaveOptions io;
    io.icoSizes = {16, 48};
    CHECK(ImageIO::save(doc.get(), icoPath, io, &error));
    CHECK(QImageReader(icoPath).imageCount() == 2);
}

// Lectura de .pdn con un archivo guardado con Paint.NET 5.1.
static void testPdn()
{
    const QString path = QStringLiteral(KAINT_SOURCE_DIR "/evidencia/nueva evidencia/pdn de prueba.pdn");
    if (!QFile::exists(path)) {
        std::printf("Se omite la prueba de .pdn: no está %s\n", qPrintable(path));
        return;
    }
    QString error;
    std::unique_ptr<Document> doc(ImageIO::load(path, &error));
    CHECK(doc);
    if (!doc) {
        std::fprintf(stderr, "  error: %s\n", qPrintable(error));
        return;
    }
    CHECK(doc->size() == QSize(1920, 1080));
    CHECK(doc->layerCount() == 6);
    if (doc->layerCount() != 6)
        return;
    CHECK(doc->layer(0)->props.name == QLatin1String("Fondo"));
    CHECK(doc->layer(5)->props.name == QLatin1String("Capa 6"));
    CHECK(doc->layer(3)->props.opacity == 96);
    CHECK(doc->layer(4)->props.blend == BlendMode::Overlay);
    CHECK(doc->layer(0)->image.pixel(100, 100) == 0xff202020u);
    CHECK(doc->layer(1)->image.pixel(960, 540) == 0xff47ae1bu);
    CHECK(doc->layer(2)->image.pixel(960, 1000) == 0xff00137fu);
    CHECK(qAlpha(doc->layer(3)->image.pixel(300, 600)) == 0);
    CHECK(doc->layer(4)->image.pixel(1500, 500) == 0xffef9907u);
    CHECK(!Pdn::thumbnail(path).isNull());
    CHECK(!ImageIO::isWritable(path));
    CHECK(ImageIO::isWritable(QStringLiteral("a.ora")) && ImageIO::isWritable(QStringLiteral("a.gif")));
    const QStringList open = ImageIO::openFilter().split(QStringLiteral(";;"));
    CHECK(open.first().startsWith(QStringLiteral("Todos los tipos de imagen (")) && open.first().contains(QStringLiteral("*.jfif")));
    CHECK(open.value(1) == QStringLiteral("Paint.NET (*.pdn)") && open.last() == QStringLiteral("Todos los archivos (*)"));
    CHECK(open.indexOf(QStringLiteral("PNG (*.png)")) < open.indexOf(QStringLiteral("JPEG (*.jpg *.jpeg *.jpe *.jfif *.exif)")));
    QString png;
    const QStringList save = ImageIO::saveFilter(&png).split(QStringLiteral(";;"));
    CHECK(save.first() == QStringLiteral("OpenRaster con capas (*.ora)") && png == QStringLiteral("PNG (*.png)"));
    CHECK(ImageIO::isWritable(QStringLiteral("a.jfif")) && ImageIO::isWritable(QStringLiteral("a.JPE")));
    QTemporaryDir dir;
    QFile broken(dir.filePath(QStringLiteral("roto.pdn")));
    QFile original(path);
    CHECK(original.open(QIODevice::ReadOnly) && broken.open(QIODevice::WriteOnly));
    broken.write(original.read(70000));
    broken.close();
    std::unique_ptr<Document> bad(ImageIO::load(broken.fileName(), &error));
    CHECK(!bad && !error.isEmpty());
}

// Complementos: el nativo de ejemplo (API C) y, si hay mono, PolyGlitch a través del proceso
// auxiliar de Paint.NET.
static void testPlugins()
{
    QSettings().clear();
    QTemporaryDir dir;
    const QString user = dir.filePath(QStringLiteral("user"));

    // Instalar copia el archivo a la carpeta del usuario (que se crea si hace falta) y no
    // sustituye uno que ya exista salvo que se pida.
    PluginManager* pm = PluginManager::instance();
    pm->setPathsForTesting({user}, dir.filePath(QStringLiteral("cache.json")));
    const QString example = QStringLiteral(KAINT_TEST_PLUGIN);
    CHECK(pm->installedAlready({example}).isEmpty());
    CHECK(pm->install({example}, false) == QStringList{user + QLatin1Char('/') + QFileInfo(example).fileName()});
    CHECK(pm->installedAlready({example}) == QStringList{example});
    CHECK(pm->install({example}, false).isEmpty());
    CHECK(pm->install({example}, true).size() == 1);
    QStringList installErrors;
    CHECK(pm->install({dir.filePath(QStringLiteral("no-existe.so"))}, true, &installErrors).isEmpty()
          && installErrors.size() == 1);
    pm->scan();
    CHECK(pm->plugins().size() == 1);
    if (pm->plugins().size() == 1) {
        const PluginInfo& p = pm->plugins().first();
        CHECK(p.kind == QLatin1String("Kaint"));
        CHECK(p.enabled && p.loaded);
        CHECK(p.effects.size() == 2);
        CHECK(p.errors.isEmpty());
    }
    const Effect* tint = Effects::find(QStringLiteral("native:kaint.ejemplo.tinte"));
    const Effect* swap = Effects::find(QStringLiteral("native:kaint.ejemplo.intercambiar"));
    CHECK(tint && swap);
    if (tint && swap) {
        CHECK(swap->isAdjustment() && !tint->isAdjustment());
        QImage img(8, 8, kLayerFormat);
        img.fill(qRgba(10, 20, 30, 128));
        std::atomic_bool cancel{false};
        ParamValues v = Effects::defaults(*swap);
        const QImage swapped = Effects::apply(*swap, img, Selection(), v, EffectContext(), cancel);
        CHECK(swapped.pixel(3, 3) == qRgba(30, 20, 10, 128));
        v = Effects::defaults(*tint);
        CHECK(v.value(QStringLiteral("color")).value<QColor>() == QColor(255, 128, 0));
        v.insert(QStringLiteral("amount"), 100);
        const QImage tinted = Effects::apply(*tint, img, Selection(), v, EffectContext(), cancel);
        CHECK(tinted.pixel(0, 0) == qRgba(255, 128, 0, 128));
    }
    pm->setEnabled(pm->plugins().value(0).id, false);
    CHECK(!Effects::find(QStringLiteral("native:kaint.ejemplo.tinte")));

    QString why;
    if (!PdnBridge::available(&why)) {
        std::printf("Se omiten las pruebas de complementos de Paint.NET: %s\n", qPrintable(why));
        return;
    }
    const QImage src = testPattern(64, 48);
    std::atomic_bool cancel{false};
    const QString polyDir = QStringLiteral(KAINT_SOURCE_DIR "/PolyGlitch Plugin");
    auto polyGlitch = [&] {
        pm->setPathsForTesting({polyDir}, dir.filePath(QStringLiteral("cache.json")));
        pm->scan();
        pm->waitForInspection();
        const PluginInfo* poly = nullptr;
        for (const PluginInfo& p : pm->plugins())
            if (p.file == QLatin1String("PolyGlitch.Effects.dll"))
                poly = &p;
        CHECK(poly);
        if (!poly)
            return;
        CHECK(!poly->pending);
        CHECK(poly->effects.size() >= 19);
        CHECK(poly->errors.isEmpty());
        pm->setEnabled(poly->id, true);
        const Effect* flip = Effects::find(QStringLiteral("pdn:PolyGlitch.Effects.dll:PolyGlitch.PolyGlitchQuadFlipEffect"));
        const Effect* displace = Effects::find(QStringLiteral("pdn:PolyGlitch.Effects.dll:PolyGlitch.Effects.PolyGlitch3DisplacePlugin"));
        CHECK(flip && displace);
        if (!flip || !displace)
            return;
        CHECK(flip->category() == QLatin1String("PolyGlitch") && !displace->iconImage().isNull());

        const QImage flipped = Effects::apply(*flip, src, Selection(), Effects::defaults(*flip), EffectContext(), cancel);
        CHECK(flip->takeError().isEmpty());
        CHECK(changedPixels(src, flipped) > 1000);

        const QList<EffectParam> params = displace->params();
        CHECK(params.size() == 3 && params.value(0).type == EffectParam::Type::Offset);
        CHECK(params.value(0).min == -128 && params.value(0).maxY == 128);
        ParamValues v = Effects::defaults(*displace);
        CHECK(changedPixels(src, Effects::apply(*displace, src, Selection(), v, EffectContext(), cancel)) == 0);
        v.insert(QStringLiteral("OffsetRed"), QPointF(5, 0));
        const QImage shifted = Effects::apply(*displace, src, Selection(), v, EffectContext(), cancel);
        CHECK(qRed(shifted.pixel(20, 10)) != qRed(src.pixel(20, 10)));
        CHECK(qGreen(shifted.pixel(20, 10)) == qGreen(src.pixel(20, 10)) && qBlue(shifted.pixel(20, 10)) == qBlue(src.pixel(20, 10)));

        const Selection sel = Selection::fromRect(QRect(0, 0, 32, 48), src.size());
        const QImage half = Effects::apply(*flip, src, sel, Effects::defaults(*flip), EffectContext(), cancel);
        CHECK(half.pixel(50, 10) == src.pixel(50, 10));
        std::atomic_bool cancelled{true};
        CHECK(Effects::apply(*flip, src, Selection(), Effects::defaults(*flip), EffectContext(), cancelled).isNull());
        CHECK(changedPixels(src, Effects::apply(*flip, src, Selection(), Effects::defaults(*flip), EffectContext(), cancel)) > 1000);

        pm->setEnabled(poly->id, false);
        pm->scan();
        for (const PluginInfo& p : pm->plugins())
            CHECK(!p.pending);
    };
    if (!QDir(polyDir).exists())
        std::printf("Se omite la prueba de PolyGlitch: no está %s\n", qPrintable(polyDir));
    else
        polyGlitch();

#ifdef KAINT_TEST_PDN_PLUGIN
    // Complemento de prueba propio (tests/pdnplugin): parámetros, selección, cancelación y una
    // imagen más grande que obliga a crear otra memoria compartida.
    {
        QTemporaryDir pdnDir;
        QFile::copy(QStringLiteral(KAINT_TEST_PDN_PLUGIN), pdnDir.filePath(QStringLiteral("KaintTestEffect.dll")));
        pm->setPathsForTesting({pdnDir.path()}, dir.filePath(QStringLiteral("cache-prueba.json")));
        pm->scan();
        pm->waitForInspection();
        CHECK(pm->plugins().size() == 1 && pm->plugins().value(0).errors.isEmpty());
        pm->setEnabled(pm->plugins().value(0).id, true);
        const Effect* xorEffect = Effects::find(QStringLiteral("pdn:KaintTestEffect.dll:KaintTest.XorEffect"));
        CHECK(xorEffect);
        if (xorEffect) {
            const QImage inverted = Effects::apply(*xorEffect, src, Selection(), Effects::defaults(*xorEffect), EffectContext(), cancel);
            CHECK(xorEffect->takeError().isEmpty());
            CHECK(inverted.pixel(5, 7) == qRgba(255 - qRed(src.pixel(5, 7)), 255 - qGreen(src.pixel(5, 7)),
                                                255 - qBlue(src.pixel(5, 7)), qAlpha(src.pixel(5, 7))));
            ParamValues none = Effects::defaults(*xorEffect);
            none.insert(QStringLiteral("Amount"), 0);
            CHECK(changedPixels(src, Effects::apply(*xorEffect, src, Selection(), none, EffectContext(), cancel)) == 0);
            const Selection left = Selection::fromRect(QRect(0, 0, 32, 48), src.size());
            const QImage half = Effects::apply(*xorEffect, src, left, Effects::defaults(*xorEffect), EffectContext(), cancel);
            CHECK(half.pixel(50, 10) == src.pixel(50, 10) && half.pixel(10, 10) != src.pixel(10, 10));
            std::atomic_bool stopNow{true};
            CHECK(Effects::apply(*xorEffect, src, Selection(), Effects::defaults(*xorEffect), EffectContext(), stopNow).isNull());
            const QImage big = testPattern(300, 200);
            const QImage bigOut = Effects::apply(*xorEffect, big, Selection(), Effects::defaults(*xorEffect), EffectContext(), cancel);
            CHECK(changedPixels(big, bigOut) == 300 * 200);
            CHECK(changedPixels(src, Effects::apply(*xorEffect, src, Selection(), Effects::defaults(*xorEffect), EffectContext(), cancel))
                  == 64 * 48);
        }
        pm->setEnabled(pm->plugins().value(0).id, false);
    }
#endif

    // Complementos de Paint.NET 4.3/4.4 compilados para .NET moderno, si está la colección de
    // Windows en el proyecto.
    const QString windows = QStringLiteral(KAINT_SOURCE_DIR "/Efectos de mi version de windows paintnet");
    if (QFile::exists(windows + QStringLiteral("/OilPaintingPlus.dll"))) {
        QTemporaryDir modernDir;
        for (const char* name : {"OilPaintingPlus.dll", "Pastel.dll"})
            QFile::copy(windows + QLatin1Char('/') + QLatin1String(name), modernDir.filePath(QLatin1String(name)));
        pm->setPathsForTesting({modernDir.path()}, dir.filePath(QStringLiteral("cache-moderno.json")));
        pm->scan();
        pm->waitForInspection();
        int working = 0;
        for (const PluginInfo& p : pm->plugins()) {
            CHECK(p.errors.isEmpty() && p.effects.size() == 1);
            pm->setEnabled(p.id, true);
        }
        for (const Effect* e : Effects::all()) {
            if (!e->pluginName().contains(QLatin1String("OilPainting")) && !e->pluginName().contains(QLatin1String("Pastel")))
                continue;
            const QImage out = Effects::apply(*e, src, Selection(), Effects::defaults(*e), EffectContext(), cancel);
            CHECK(e->takeError().isEmpty());
            if (changedPixels(src, out) > 1000)
                ++working;
        }
        CHECK(working == 2);
    }
    pm->shutdown();
}

// PNG propio (entrelazado Adam7 y sin entrelazar): RGBA, RGB e indexado se leen igual que el
// original, y la profundidad de 8 bits admite los dos cuantificadores.
static void testPngWriter()
{
    QImage rgba(37, 23, QImage::Format_ARGB32);
    for (int y = 0; y < rgba.height(); ++y)
        for (int x = 0; x < rgba.width(); ++x)
            rgba.setPixel(x, y, qRgba(x * 7, y * 11, (x + y) * 3, 255 - x * 4));
    QImage rgb = rgba.convertToFormat(QImage::Format_RGB32);
    QImage indexed = rgba.convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither);
    for (bool interlaced : {true, false}) {
        for (const QImage& src : {rgba, rgb, indexed}) {
            const QByteArray data = Png::encode(src, interlaced);
            QImage back;
            CHECK(back.loadFromData(data, "png"));
            CHECK(back.convertToFormat(QImage::Format_ARGB32) == src.convertToFormat(QImage::Format_ARGB32));
        }
    }
    CHECK(Png::encode(rgba, true).at(28) == 1);
    ImageIO::SaveOptions o;
    o.bitDepth = 8;
    o.interlaced = true;
    for (int algorithm : {0, 1}) {
        o.quantization = algorithm;
        const QImage prepared = ImageIO::prepare(rgba, QStringLiteral("a.png"), o);
        CHECK(prepared.format() == QImage::Format_Indexed8 && prepared.colorCount() <= 256);
        QString error;
        QImage back;
        CHECK(back.loadFromData(ImageIO::encode(prepared, QStringLiteral("a.png"), o, &error), "png"));
        CHECK(back.size() == rgba.size());
    }
}

// Ampliar, reducir y difuminar la selección: esquinas redondeadas y borde suave.
static void testSelectionReshape()
{
    const QSize canvas(80, 60);
    const Selection box = Selection::fromRect(QRect(20, 20, 30, 20), canvas);
    const Selection grown = box.grown(5);
    CHECK(grown.bounds() == QRect(15, 15, 40, 30));
    CHECK(grown.coverage(15, 30) > 200 && grown.coverage(15, 15) < 128);
    const Selection shrunk = box.grown(-5);
    CHECK(shrunk.bounds() == QRect(25, 25, 20, 10));
    CHECK(box.grown(-20).isEmpty());
    const Selection soft = box.feathered(6);
    CHECK(soft.coverage(35, 30) > 250);
    CHECK(soft.coverage(20, 30) > 60 && soft.coverage(20, 30) < 200);
    CHECK(soft.bounds().contains(box.bounds()));
}

// Balance de color: cada rango actúa en su zona de luminosidad.
static void testColorBalance()
{
    const Effect* balance = Effects::find(QStringLiteral("colorbalance"));
    CHECK(balance != nullptr);
    if (!balance)
        return;
    QImage src(2, 1, QImage::Format_ARGB32_Premultiplied);
    src.setPixel(0, 0, qRgba(40, 40, 40, 255));
    src.setPixel(1, 0, qRgba(220, 220, 220, 255));
    ParamValues v = Effects::defaults(*balance);
    v.insert(QStringLiteral("shadowsCyanRed"), 100);
    v.insert(QStringLiteral("preserve"), false);
    std::atomic_bool cancel{false};
    const QImage out = Effects::apply(*balance, src, Selection(), v, EffectContext(), cancel);
    CHECK(qRed(out.pixel(0, 0)) > 100 && qGreen(out.pixel(0, 0)) == 40);
    CHECK(std::abs(qRed(out.pixel(1, 0)) - 220) <= 2);
    CHECK(Effects::apply(*balance, src, Selection(), Effects::defaults(*balance), EffectContext(), cancel) == src);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("KaintCoreTest"));
    QGuiApplication::setApplicationName(QStringLiteral("KaintCoreTest"));
    testStrokeAndUndo();
    testPencilLines();
    testSelection();
    testFloodFill();
    testOraRoundTrip();
    testEffects();
    testEdgeSampling();
    testCustomShapes();
    testMovePixels();
    testBlendModes();
    testResample();
    testShrinkTransform();
    testPaletteAndJson();
    testSaveOptions();
    testStraightAlpha();
    testBucketHalo();
    testBucketAccumulate();
    testSmoothMaskSelection();
    testGifAndIco();
    testPdn();
    testPlugins();
    testPngWriter();
    testSelectionReshape();
    testColorBalance();
    if (g_failures == 0)
        std::printf("Todas las pruebas del núcleo pasaron.\n");
    return g_failures == 0 ? 0 : 1;
}
