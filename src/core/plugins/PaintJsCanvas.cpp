#include "core/plugins/PaintJsCanvas.h"

#include <QBuffer>
#include <QConicalGradient>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace PaintJs::detail {

namespace {

JSClassID g_canvasClass = 0;
std::mutex g_classMutex;

constexpr int kMaxSide = 32767;
constexpr qint64 kMaxPixels = 16384LL * 16384;

// --- Valores de JavaScript ------------------------------------------------------------------

JSValueConst arg(int argc, JSValueConst* argv, int i)
{
    return i < argc ? argv[i] : JS_UNDEFINED;
}

double num(JSContext* ctx, JSValueConst v, double fallback = 0)
{
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return fallback;
    double d = 0;
    if (JS_ToFloat64(ctx, &d, v) < 0) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return fallback;
    }
    return std::isfinite(d) ? d : fallback;
}

QString str(JSContext* ctx, JSValueConst v)
{
    if (JS_IsUndefined(v) || JS_IsNull(v))
        return {};
    size_t len = 0;
    const char* s = JS_ToCStringLen(ctx, &len, v);
    if (!s) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    const QString out = QString::fromUtf8(s, qsizetype(len));
    JS_FreeCString(ctx, s);
    return out;
}

// Un valor que se libera solo.
struct Owned {
    JSContext* ctx;
    JSValue v;
    Owned(JSContext* c, JSValue value) : ctx(c), v(value) {}
    ~Owned() { JS_FreeValue(ctx, v); }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
};

// Bytes de un TypedArray (o de un ArrayBuffer); null si no lo es.
uint8_t* bytesOf(JSContext* ctx, JSValueConst v, size_t* size)
{
    size_t offset = 0, length = 0, element = 0;
    JSValue buffer = JS_GetTypedArrayBuffer(ctx, v, &offset, &length, &element);
    if (JS_IsException(buffer)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        uint8_t* direct = JS_GetArrayBuffer(ctx, size, v);
        if (!direct)
            JS_FreeValue(ctx, JS_GetException(ctx));
        return direct;
    }
    size_t total = 0;
    uint8_t* data = JS_GetArrayBuffer(ctx, &total, buffer);
    JS_FreeValue(ctx, buffer);
    if (!data)
        return nullptr;
    *size = length;
    return data + offset;
}

JSValue newClampedArray(JSContext* ctx, const uint8_t* data, size_t size)
{
    JSValue buffer = JS_NewArrayBufferCopy(ctx, data, size);
    if (JS_IsException(buffer))
        return buffer;
    JSValue array = JS_NewTypedArray(ctx, 1, &buffer, JS_TYPED_ARRAY_UINT8C);
    JS_FreeValue(ctx, buffer);
    return array;
}

// --- El lienzo ------------------------------------------------------------------------------

struct Canvas {
    struct State {
        QTransform transform;
        QPainterPath clip;
        bool clipped = false;
    };
    QImage image;
    QPainterPath path;
    State s;
    QList<State> stack;

    void resize(int w, int h)
    {
        w = std::clamp(w, 0, kMaxSide);
        h = std::clamp(h, 0, kMaxSide);
        if (qint64(w) * h > kMaxPixels)
            h = int(kMaxPixels / std::max(1, w));
        image = w > 0 && h > 0 ? QImage(w, h, QImage::Format_RGBA8888_Premultiplied) : QImage();
        if (!image.isNull())
            image.fill(Qt::transparent);
        path = QPainterPath();
        s = State();
        stack.clear();
    }
};

Canvas* canvasOf(JSContext* ctx, JSValueConst v)
{
    auto* c = static_cast<Canvas*>(JS_GetOpaque(v, g_canvasClass));
    if (!c)
        JS_ThrowTypeError(ctx, "not a canvas");
    return c;
}

void finalizeCanvas(JSRuntime*, JSValueConst value)
{
    delete static_cast<Canvas*>(JS_GetOpaque(value, g_canvasClass));
}

// --- Colores, fuentes y estilos de CSS --------------------------------------------------------

bool parseColor(const QString& text, QColor* out)
{
    const QString s = text.trimmed().toLower();
    if (s.isEmpty())
        return false;
    if (s == QLatin1String("transparent")) {
        *out = QColor(0, 0, 0, 0);
        return true;
    }
    if (s.startsWith(QLatin1Char('#'))) {
        const QString h = s.mid(1);
        bool ok = false;
        const quint64 v = h.toULongLong(&ok, 16);
        if (!ok)
            return false;
        auto nib = [&](int i) { return QStringView(h).mid(i, 1).toInt(nullptr, 16) * 17; };
        switch (h.size()) {
        case 3: *out = QColor(nib(0), nib(1), nib(2)); return true;
        case 4: *out = QColor(nib(0), nib(1), nib(2), nib(3)); return true;
        case 6: *out = QColor(int((v >> 16) & 255), int((v >> 8) & 255), int(v & 255)); return true;
        case 8: *out = QColor(int((v >> 24) & 255), int((v >> 16) & 255), int((v >> 8) & 255), int(v & 255)); return true;
        default: return false;
        }
    }
    static const QRegularExpression function(QStringLiteral(R"(^(rgba?|hsla?)\s*\((.*)\)$)"));
    const QRegularExpressionMatch m = function.match(s);
    if (m.hasMatch()) {
        QString body = m.captured(2);
        body.replace(QLatin1Char(','), QLatin1Char(' ')).replace(QLatin1Char('/'), QLatin1Char(' '));
        const QStringList parts = body.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 3 || parts.size() > 4)
            return false;
        bool ok = true;
        auto part = [&ok](QString p, double percentScale) {
            bool good = false;
            double v = 0;
            if (p.endsWith(QLatin1Char('%'))) {
                p.chop(1);
                v = p.toDouble(&good) / 100.0 * percentScale;
            } else {
                v = p.toDouble(&good);
            }
            ok = ok && good;
            return v;
        };
        const double alpha = parts.size() == 4 ? part(parts[3], 1) : 1;
        auto unit = [](double v) { return std::clamp(v, 0.0, 1.0); };
        if (m.captured(1).startsWith(QLatin1String("rgb"))) {
            const double r = part(parts[0], 255), g = part(parts[1], 255), b = part(parts[2], 255);
            if (!ok)
                return false;
            *out = QColor::fromRgbF(float(unit(r / 255)), float(unit(g / 255)), float(unit(b / 255)), float(unit(alpha)));
            return true;
        }
        QString hueText = parts[0];
        double hueScale = 1;
        if (hueText.endsWith(QLatin1String("deg")))
            hueText.chop(3);
        else if (hueText.endsWith(QLatin1String("turn")))
            hueText.chop(4), hueScale = 360;
        else if (hueText.endsWith(QLatin1String("rad")))
            hueText.chop(3), hueScale = 180 / M_PI;
        bool hueOk = false;
        double hue = hueText.toDouble(&hueOk) * hueScale;
        const double sat = part(parts[1], 1), light = part(parts[2], 1);
        if (!ok || !hueOk)
            return false;
        hue = std::fmod(hue, 360.0);
        if (hue < 0)
            hue += 360;
        *out = QColor::fromHslF(float(hue / 360), float(unit(sat)), float(unit(light)), float(unit(alpha)));
        return true;
    }
    const QColor named = QColor::fromString(s);
    if (!named.isValid())
        return false;
    *out = named;
    return true;
}

// Como lo devuelve el navegador: #rrggbb si es opaco y rgba(...) si no.
QString cssText(const QColor& c)
{
    if (c.alpha() == 255)
        return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(QString::number(c.alphaF(), 'g', 4));
}

// "italic bold 12px Arial, sans-serif" (el formato de la propiedad font de CSS).
QFont parseFont(const QString& css)
{
    QFont font(QStringLiteral("Sans Serif"));
    font.setPixelSize(10);
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*((?:(?:italic|oblique|normal|bold|bolder|lighter|small-caps|[1-9]00)\s+)*)(\d*\.?\d+)(px|pt|em|rem|%)?(?:\s*/\s*\S+)?\s+(.+?)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(css);
    if (!m.hasMatch())
        return font;
    for (const QString& word : m.captured(1).toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        if (word == QLatin1String("italic") || word == QLatin1String("oblique"))
            font.setItalic(true);
        else if (word == QLatin1String("bold") || word == QLatin1String("bolder"))
            font.setWeight(QFont::Bold);
        else if (word == QLatin1String("lighter"))
            font.setWeight(QFont::Light);
        else if (word.size() == 3 && word.endsWith(QLatin1String("00")))
            font.setWeight(QFont::Weight(std::clamp(word.toInt(), 100, 900)));
    }
    double size = m.captured(2).toDouble();
    const QString unit = m.captured(3).toLower();
    if (unit == QLatin1String("pt"))
        size *= 4.0 / 3.0;
    else if (unit == QLatin1String("em") || unit == QLatin1String("rem"))
        size *= 16;
    else if (unit == QLatin1String("%"))
        size *= 0.16;
    font.setPixelSize(std::clamp(int(std::lround(size)), 1, 4096));
    QStringList families;
    for (QString f : m.captured(4).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        f = f.trimmed();
        if (f.size() > 1 && (f.startsWith(QLatin1Char('"')) || f.startsWith(QLatin1Char('\''))))
            f = f.mid(1, f.size() - 2);
        const QString lower = f.toLower();
        if (lower == QLatin1String("serif"))
            f = QStringLiteral("Serif");
        else if (lower == QLatin1String("sans-serif") || lower == QLatin1String("system-ui"))
            f = QStringLiteral("Sans Serif");
        else if (lower == QLatin1String("monospace"))
            f = QStringLiteral("Monospace");
        if (!f.isEmpty())
            families << f;
    }
    if (!families.isEmpty())
        font.setFamilies(families);
    return font;
}

QPainter::CompositionMode compositionOf(const QString& name)
{
    static const QHash<QString, QPainter::CompositionMode> modes{
        {QStringLiteral("source-over"), QPainter::CompositionMode_SourceOver},
        {QStringLiteral("source-in"), QPainter::CompositionMode_SourceIn},
        {QStringLiteral("source-out"), QPainter::CompositionMode_SourceOut},
        {QStringLiteral("source-atop"), QPainter::CompositionMode_SourceAtop},
        {QStringLiteral("destination-over"), QPainter::CompositionMode_DestinationOver},
        {QStringLiteral("destination-in"), QPainter::CompositionMode_DestinationIn},
        {QStringLiteral("destination-out"), QPainter::CompositionMode_DestinationOut},
        {QStringLiteral("destination-atop"), QPainter::CompositionMode_DestinationAtop},
        {QStringLiteral("lighter"), QPainter::CompositionMode_Plus},
        {QStringLiteral("copy"), QPainter::CompositionMode_Source},
        {QStringLiteral("xor"), QPainter::CompositionMode_Xor},
        {QStringLiteral("multiply"), QPainter::CompositionMode_Multiply},
        {QStringLiteral("screen"), QPainter::CompositionMode_Screen},
        {QStringLiteral("overlay"), QPainter::CompositionMode_Overlay},
        {QStringLiteral("darken"), QPainter::CompositionMode_Darken},
        {QStringLiteral("lighten"), QPainter::CompositionMode_Lighten},
        {QStringLiteral("color-dodge"), QPainter::CompositionMode_ColorDodge},
        {QStringLiteral("color-burn"), QPainter::CompositionMode_ColorBurn},
        {QStringLiteral("hard-light"), QPainter::CompositionMode_HardLight},
        {QStringLiteral("soft-light"), QPainter::CompositionMode_SoftLight},
        {QStringLiteral("difference"), QPainter::CompositionMode_Difference},
        {QStringLiteral("exclusion"), QPainter::CompositionMode_Exclusion},
    };
    return modes.value(name, QPainter::CompositionMode_SourceOver);
}

// Imagen (premultiplicada) de un lienzo o de un ImageData ({data, width, height}).
QImage imageOf(JSContext* ctx, JSValueConst v)
{
    if (const QImage* image = canvasImage(v))
        return *image;
    if (!JS_IsObject(v))
        return {};
    const int w = int(num(ctx, Owned(ctx, JS_GetPropertyStr(ctx, v, "width")).v));
    const int h = int(num(ctx, Owned(ctx, JS_GetPropertyStr(ctx, v, "height")).v));
    Owned data(ctx, JS_GetPropertyStr(ctx, v, "data"));
    size_t size = 0;
    const uint8_t* bytes = bytesOf(ctx, data.v, &size);
    if (!bytes || w <= 0 || h <= 0 || size < size_t(w) * size_t(h) * 4)
        return {};
    return QImage(bytes, w, h, w * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

// Lo que se dibuja: lo que el preludio pasa en cada llamada (el estado de dibujo de la clase
// CanvasRenderingContext2D, salvo la transformación y el recorte, que van aquí).
struct Style {
    QBrush brush = QBrush(Qt::black);
    double alpha = 1;
    QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver;
    double lineWidth = 1;
    Qt::PenCapStyle cap = Qt::FlatCap;
    Qt::PenJoinStyle join = Qt::SvgMiterJoin;
    double miterLimit = 10;
    QList<qreal> dash;
    double dashOffset = 0;
    QFont font;
    QString align;
    QString baseline;
    bool smoothing = true;

    QPen pen() const
    {
        QPen p(brush, lineWidth, Qt::SolidLine, cap, join);
        p.setMiterLimit(miterLimit);
        if (!dash.isEmpty() && lineWidth > 0) {
            // En el lienzo la raya se mide en píxeles; en QPen, en grosores de línea (y con un
            // número impar de medidas, se repiten).
            QList<qreal> pattern;
            for (qreal d : dash)
                pattern << std::max<qreal>(d / lineWidth, 1e-3);
            if (pattern.size() % 2)
                pattern += pattern;
            p.setDashPattern(pattern);
            p.setDashOffset(dashOffset / lineWidth);
        }
        return p;
    }
};

QBrush brushOf(JSContext* ctx, JSValueConst paint)
{
    if (JS_IsString(paint)) {
        QColor c;
        return parseColor(str(ctx, paint), &c) ? QBrush(c) : QBrush(Qt::black);
    }
    if (!JS_IsObject(paint))
        return QBrush(Qt::black);
    const QString kind = str(ctx, Owned(ctx, JS_GetPropertyStr(ctx, paint, "_kind")).v);
    if (kind == QLatin1String("pattern")) {
        const QImage image = imageOf(ctx, Owned(ctx, JS_GetPropertyStr(ctx, paint, "_image")).v);
        return image.isNull() ? QBrush(Qt::transparent) : QBrush(image);
    }
    Owned coords(ctx, JS_GetPropertyStr(ctx, paint, "_coords"));
    auto c = [&](int i) { return num(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, coords.v, uint32_t(i))).v); };
    QGradientStops stops;
    Owned list(ctx, JS_GetPropertyStr(ctx, paint, "_stops"));
    int64_t count = 0;
    JS_GetLength(ctx, list.v, &count);
    for (int64_t i = 0; i < count; ++i) {
        Owned stop(ctx, JS_GetPropertyUint32(ctx, list.v, uint32_t(i)));
        QColor color;
        if (parseColor(str(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, stop.v, 1)).v), &color))
            stops << QGradientStop(num(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, stop.v, 0)).v), color);
    }
    // Con el mismo desplazamiento, la última parada que se añadió gana por arriba.
    std::stable_sort(stops.begin(), stops.end(), [](const QGradientStop& a, const QGradientStop& b) { return a.first < b.first; });
    if (kind == QLatin1String("radial")) {
        QRadialGradient g(QPointF(c(3), c(4)), std::max(0.0, c(5)), QPointF(c(0), c(1)), std::max(0.0, c(2)));
        g.setStops(stops);
        return QBrush(g);
    }
    if (kind == QLatin1String("conic")) {
        // En el lienzo el ángulo crece en el sentido de las agujas del reloj; en Qt, al revés.
        QConicalGradient g(QPointF(c(1), c(2)), -c(0) * 180 / M_PI);
        QGradientStops reversed;
        for (const QGradientStop& s : std::as_const(stops))
            reversed.prepend(QGradientStop(1 - s.first, s.second));
        g.setStops(reversed);
        return QBrush(g);
    }
    QLinearGradient g(QPointF(c(0), c(1)), QPointF(c(2), c(3)));
    g.setStops(stops);
    return QBrush(g);
}

Style styleOf(JSContext* ctx, JSValueConst o)
{
    Style st;
    if (!JS_IsObject(o))
        return st;
    auto get = [&](const char* key) { return JS_GetPropertyStr(ctx, o, key); };
    st.brush = brushOf(ctx, Owned(ctx, get("paint")).v);
    st.alpha = std::clamp(num(ctx, Owned(ctx, get("alpha")).v, 1), 0.0, 1.0);
    st.mode = compositionOf(str(ctx, Owned(ctx, get("comp")).v));
    st.lineWidth = std::max(0.0, num(ctx, Owned(ctx, get("lineWidth")).v, 1));
    const QString cap = str(ctx, Owned(ctx, get("lineCap")).v);
    st.cap = cap == QLatin1String("round") ? Qt::RoundCap : cap == QLatin1String("square") ? Qt::SquareCap : Qt::FlatCap;
    const QString join = str(ctx, Owned(ctx, get("lineJoin")).v);
    st.join = join == QLatin1String("round") ? Qt::RoundJoin : join == QLatin1String("bevel") ? Qt::BevelJoin : Qt::SvgMiterJoin;
    st.miterLimit = std::max(0.0, num(ctx, Owned(ctx, get("miterLimit")).v, 10));
    Owned dash(ctx, get("dash"));
    int64_t n = 0;
    if (JS_IsArray(dash.v) && JS_GetLength(ctx, dash.v, &n) == 0)
        for (int64_t i = 0; i < n; ++i)
            st.dash << std::max(0.0, num(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, dash.v, uint32_t(i))).v));
    if (std::all_of(st.dash.begin(), st.dash.end(), [](qreal d) { return d == 0; }))
        st.dash.clear();
    st.dashOffset = num(ctx, Owned(ctx, get("dashOffset")).v);
    st.font = parseFont(str(ctx, Owned(ctx, get("font")).v));
    st.align = str(ctx, Owned(ctx, get("textAlign")).v);
    st.baseline = str(ctx, Owned(ctx, get("textBaseline")).v);
    Owned smoothing(ctx, get("smoothing"));
    st.smoothing = JS_IsUndefined(smoothing.v) || JS_ToBool(ctx, smoothing.v) > 0;
    return st;
}

// Dibuja con el estilo, el recorte y la composición del lienzo. Las operaciones que no son
// "source-over" se hacen como en el navegador: lo dibujado va a una capa aparte y esa capa se
// compone con el lienzo entero (así "copy", "destination-in"... también afectan a lo de fuera
// de la figura).
template <typename Draw>
void paint(Canvas& c, const Style& st, Draw&& draw)
{
    if (c.image.isNull())
        return;
    auto setup = [&](QPainter& p) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform, st.smoothing);
    };
    if (st.mode == QPainter::CompositionMode_SourceOver) {
        QPainter p(&c.image);
        setup(p);
        if (c.s.clipped)
            p.setClipPath(c.s.clip);
        p.setOpacity(st.alpha);
        draw(p);
        return;
    }
    QImage layer(c.image.size(), QImage::Format_RGBA8888_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter p(&layer);
        setup(p);
        draw(p);
    }
    QPainter p(&c.image);
    if (c.s.clipped)
        p.setClipPath(c.s.clip);
    p.setCompositionMode(st.mode);
    p.setOpacity(st.alpha);
    p.drawImage(0, 0, layer);
}

// --- Trazados ---------------------------------------------------------------------------------

enum PathOp { BeginPath, ClosePath, MoveTo, LineTo, QuadTo, CubicTo, Arc, ArcTo, Ellipse, Rect, RoundRect };

// Un arco (o una elipse girada) en coordenadas del usuario, añadido al trazado del lienzo: con
// una línea desde el punto actual, como en el navegador.
void addArc(QPainterPath& path, const QTransform& t, double x, double y, double rx, double ry, double rotation, double start,
            double end, bool ccw)
{
    if (rx < 0 || ry < 0)
        return;
    constexpr double tau = 2 * M_PI;
    double sweep;
    if (!ccw) {
        const double d = end - start;
        sweep = d >= tau ? tau : std::fmod(std::fmod(d, tau) + tau, tau);
    } else {
        const double d = start - end;
        sweep = -(d >= tau ? tau : std::fmod(std::fmod(d, tau) + tau, tau));
    }
    const QRectF box(-rx, -ry, 2 * rx, 2 * ry);
    QPainterPath arc;
    // En Qt los ángulos van en grados y en sentido contrario al de las agujas del reloj.
    arc.arcMoveTo(box, -start * 180 / M_PI);
    arc.arcTo(box, -start * 180 / M_PI, -sweep * 180 / M_PI);
    const QPainterPath mapped = (QTransform().translate(x, y).rotateRadians(rotation) * t).map(arc);
    if (path.elementCount() == 0)
        path.moveTo(mapped.elementAt(0));
    path.connectPath(mapped);
}

void addOp(QPainterPath& path, const QTransform& t, int op, const double* a)
{
    auto ensure = [&](QPointF p) {
        if (path.elementCount() == 0)
            path.moveTo(t.map(p));
    };
    switch (op) {
    case BeginPath: path = QPainterPath(); break;
    case ClosePath:
        if (path.elementCount() > 0)
            path.closeSubpath();
        break;
    case MoveTo: path.moveTo(t.map(QPointF(a[0], a[1]))); break;
    case LineTo:
        if (path.elementCount() == 0)
            path.moveTo(t.map(QPointF(a[0], a[1])));
        else
            path.lineTo(t.map(QPointF(a[0], a[1])));
        break;
    case QuadTo:
        ensure(QPointF(a[0], a[1]));
        path.quadTo(t.map(QPointF(a[0], a[1])), t.map(QPointF(a[2], a[3])));
        break;
    case CubicTo:
        ensure(QPointF(a[0], a[1]));
        path.cubicTo(t.map(QPointF(a[0], a[1])), t.map(QPointF(a[2], a[3])), t.map(QPointF(a[4], a[5])));
        break;
    case Arc: addArc(path, t, a[0], a[1], a[2], a[2], 0, a[3], a[4], a[5] != 0); break;
    case Ellipse: addArc(path, t, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7] != 0); break;
    case ArcTo: {
        const QPointF p1(a[0], a[1]), p2(a[2], a[3]);
        const double r = a[4];
        bool invertible = false;
        const QTransform inverse = t.inverted(&invertible);
        if (path.elementCount() == 0 || !invertible || r < 0) {
            ensure(p1);
            break;
        }
        const QPointF p0 = inverse.map(path.currentPosition());
        const QPointF d1 = p1 - p0, d2 = p2 - p1;
        const double cross = d1.x() * d2.y() - d1.y() * d2.x();
        if (p0 == p1 || p1 == p2 || r == 0 || std::abs(cross) < 1e-9) {
            path.lineTo(t.map(p1));
            break;
        }
        auto unit = [](QPointF v) { const double l = std::hypot(v.x(), v.y()); return v / l; };
        const QPointF v1 = unit(p0 - p1), v2 = unit(p2 - p1);
        const double angle = std::acos(std::clamp(v1.x() * v2.x() + v1.y() * v2.y(), -1.0, 1.0));
        const double tangent = r / std::tan(angle / 2);
        const QPointF t1 = p1 + v1 * tangent, t2 = p1 + v2 * tangent;
        const QPointF center = p1 + unit(v1 + v2) * (r / std::sin(angle / 2));
        path.lineTo(t.map(t1));
        addArc(path, t, center.x(), center.y(), r, r, 0, std::atan2(t1.y() - center.y(), t1.x() - center.x()),
               std::atan2(t2.y() - center.y(), t2.x() - center.x()), cross < 0);
        break;
    }
    case Rect: {
        QPainterPath rect;
        rect.moveTo(a[0], a[1]);
        rect.lineTo(a[0] + a[2], a[1]);
        rect.lineTo(a[0] + a[2], a[1] + a[3]);
        rect.lineTo(a[0], a[1] + a[3]);
        rect.closeSubpath();
        path.addPath(t.map(rect));
        path.moveTo(t.map(QPointF(a[0], a[1])));
        break;
    }
    case RoundRect: {
        QPainterPath rect;
        const double r = std::clamp(a[4], 0.0, std::min(std::abs(a[2]), std::abs(a[3])) / 2);
        rect.addRoundedRect(QRectF(a[0], a[1], a[2], a[3]).normalized(), r, r);
        path.addPath(t.map(rect));
        path.moveTo(t.map(QPointF(a[0], a[1])));
        break;
    }
    default: break;
    }
}

constexpr int opArgs(int op)
{
    switch (op) {
    case MoveTo:
    case LineTo: return 2;
    case QuadTo: return 4;
    case CubicTo: return 6;
    case Arc: return 6;
    case ArcTo: return 5;
    case Ellipse: return 8;
    case Rect: return 4;
    case RoundRect: return 5;
    default: return 0;
    }
}

// Trazado de un Path2D (sus operaciones grabadas: [[op, ...números], ...]) con la transformación
// de ahora.
QPainterPath opsPath(JSContext* ctx, JSValueConst ops, const QTransform& t)
{
    QPainterPath path;
    int64_t n = 0;
    if (!JS_IsArray(ops) || JS_GetLength(ctx, ops, &n) < 0)
        return path;
    for (int64_t i = 0; i < n; ++i) {
        Owned item(ctx, JS_GetPropertyUint32(ctx, ops, uint32_t(i)));
        const int op = int(num(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, item.v, 0)).v, -1));
        double a[8] = {};
        for (int k = 0; k < opArgs(op); ++k)
            a[k] = num(ctx, Owned(ctx, JS_GetPropertyUint32(ctx, item.v, uint32_t(k + 1))).v);
        addOp(path, t, op, a);
    }
    return path;
}

QPainterPath pathArg(JSContext* ctx, Canvas* c, JSValueConst ops)
{
    return JS_IsArray(ops) ? opsPath(ctx, ops, c->s.transform) : c->path;
}

// --- Funciones nativas -------------------------------------------------------------------------

#define CANVAS_ARG                                       \
    Canvas* c = canvasOf(ctx, arg(argc, argv, 0));       \
    if (!c)                                              \
        return JS_EXCEPTION;

JSValue jsNew(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    JSValue obj = JS_NewObjectClass(ctx, int(g_canvasClass));
    if (JS_IsException(obj))
        return obj;
    auto* c = new Canvas;
    c->resize(int(num(ctx, arg(argc, argv, 0), 300)), int(num(ctx, arg(argc, argv, 1), 150)));
    JS_SetOpaque(obj, c);
    return obj;
}

JSValue jsResize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    c->resize(int(num(ctx, arg(argc, argv, 1))), int(num(ctx, arg(argc, argv, 2))));
    return JS_UNDEFINED;
}

JSValue jsSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    JSValue out = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, out, 0, JS_NewInt32(ctx, c->image.width()));
    JS_SetPropertyUint32(ctx, out, 1, JS_NewInt32(ctx, c->image.height()));
    return out;
}

JSValue jsSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    if (c->stack.size() < 4096)
        c->stack << c->s;
    return JS_UNDEFINED;
}

JSValue jsRestore(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    if (!c->stack.isEmpty())
        c->s = c->stack.takeLast();
    return JS_UNDEFINED;
}

// (c, a, b, c, d, e, f, multiplicar): la matriz del lienzo (a c e / b d f).
JSValue jsTransform(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    double m[6];
    for (int i = 0; i < 6; ++i)
        m[i] = num(ctx, arg(argc, argv, i + 1), (i == 0 || i == 3) ? 1 : 0);
    const QTransform t(m[0], m[1], m[2], m[3], m[4], m[5]);
    if (JS_ToBool(ctx, arg(argc, argv, 7)) > 0)
        c->s.transform = t * c->s.transform;
    else
        c->s.transform = t;
    return JS_UNDEFINED;
}

JSValue jsGetTransform(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const QTransform& t = c->s.transform;
    const double m[6] = {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
    JSValue out = JS_NewArray(ctx);
    for (int i = 0; i < 6; ++i)
        JS_SetPropertyUint32(ctx, out, uint32_t(i), JS_NewFloat64(ctx, m[i]));
    return out;
}

// (c, op, ...números): una operación del trazado actual.
JSValue jsPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const int op = int(num(ctx, arg(argc, argv, 1), -1));
    double a[8] = {};
    for (int k = 0; k < opArgs(op); ++k) {
        a[k] = num(ctx, arg(argc, argv, k + 2), std::nan(""));
        // Como en el navegador: con un número que no vale, la operación no hace nada.
        if (!std::isfinite(a[k]))
            return JS_UNDEFINED;
    }
    addOp(c->path, c->s.transform, op, a);
    return JS_UNDEFINED;
}

Qt::FillRule ruleOf(JSContext* ctx, JSValueConst v)
{
    return str(ctx, v) == QLatin1String("evenodd") ? Qt::OddEvenFill : Qt::WindingFill;
}

// (c, estilo, regla, path2d?)
JSValue jsFill(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const Style st = styleOf(ctx, arg(argc, argv, 1));
    QPainterPath path = pathArg(ctx, c, arg(argc, argv, 3));
    path.setFillRule(ruleOf(ctx, arg(argc, argv, 2)));
    QBrush brush = st.brush;
    brush.setTransform(c->s.transform);
    paint(*c, st, [&](QPainter& p) { p.fillPath(path, brush); });
    return JS_UNDEFINED;
}

void strokePath(Canvas* c, const Style& st, const QPainterPath& devicePath)
{
    bool invertible = false;
    const QTransform inverse = c->s.transform.inverted(&invertible);
    if (!invertible || st.lineWidth <= 0)
        return;
    const QPainterPath user = inverse.map(devicePath);
    paint(*c, st, [&](QPainter& p) {
        p.setTransform(c->s.transform);
        p.strokePath(user, st.pen());
    });
}

// (c, estilo, path2d?)
JSValue jsStroke(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    strokePath(c, styleOf(ctx, arg(argc, argv, 1)), pathArg(ctx, c, arg(argc, argv, 2)));
    return JS_UNDEFINED;
}

// (c, regla, path2d?)
JSValue jsClip(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    QPainterPath path = pathArg(ctx, c, arg(argc, argv, 2));
    path.setFillRule(ruleOf(ctx, arg(argc, argv, 1)));
    c->s.clip = c->s.clipped ? c->s.clip.intersected(path) : path;
    c->s.clipped = true;
    return JS_UNDEFINED;
}

// (c, x, y, regla, path2d?)
JSValue jsIsPointInPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    QPainterPath path = pathArg(ctx, c, arg(argc, argv, 4));
    path.setFillRule(ruleOf(ctx, arg(argc, argv, 3)));
    return JS_NewBool(ctx, path.contains(QPointF(num(ctx, arg(argc, argv, 1)), num(ctx, arg(argc, argv, 2)))));
}

QPainterPath rectPath(const QTransform& t, double x, double y, double w, double h)
{
    QPainterPath p;
    p.addPolygon(t.map(QPolygonF(QRectF(x, y, w, h))));
    p.closeSubpath();
    return p;
}

// (c, estilo, x, y, w, h, modo): 0 rellenar, 1 contorno, 2 borrar.
JSValue jsRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const double x = num(ctx, arg(argc, argv, 2)), y = num(ctx, arg(argc, argv, 3));
    const double w = num(ctx, arg(argc, argv, 4)), h = num(ctx, arg(argc, argv, 5));
    const int mode = int(num(ctx, arg(argc, argv, 6)));
    const Style st = styleOf(ctx, arg(argc, argv, 1));
    if (mode == 1) {
        QPainterPath outline;
        outline.addRect(QRectF(x, y, w, h));
        strokePath(c, st, c->s.transform.map(outline));
    } else if (mode == 2) {
        if (!c->image.isNull()) {
            QPainter p(&c->image);
            if (c->s.clipped)
                p.setClipPath(c->s.clip);
            p.setRenderHint(QPainter::Antialiasing);
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.fillPath(rectPath(c->s.transform, x, y, w, h), Qt::black);
        }
    } else if (w != 0 && h != 0) {
        QBrush brush = st.brush;
        brush.setTransform(c->s.transform);
        const QPainterPath path = rectPath(c->s.transform, x, y, w, h);
        paint(*c, st, [&](QPainter& p) { p.fillPath(path, brush); });
    }
    return JS_UNDEFINED;
}

// El texto en coordenadas del usuario, colocado según textAlign y textBaseline.
QPainterPath textPath(const Style& st, const QString& text, double x, double y)
{
    const QFontMetricsF fm(st.font);
    double dx = 0, dy = 0;
    const double width = fm.horizontalAdvance(text);
    if (st.align == QLatin1String("center"))
        dx = -width / 2;
    else if (st.align == QLatin1String("right") || st.align == QLatin1String("end"))
        dx = -width;
    if (st.baseline == QLatin1String("top") || st.baseline == QLatin1String("hanging"))
        dy = fm.ascent();
    else if (st.baseline == QLatin1String("middle"))
        dy = (fm.ascent() - fm.descent()) / 2;
    else if (st.baseline == QLatin1String("bottom") || st.baseline == QLatin1String("ideographic"))
        dy = -fm.descent();
    QPainterPath path;
    path.addText(QPointF(x + dx, y + dy), st.font, text);
    return path;
}

// (c, estilo, texto, x, y, contorno)
JSValue jsText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const Style st = styleOf(ctx, arg(argc, argv, 1));
    const QPainterPath user = textPath(st, str(ctx, arg(argc, argv, 2)), num(ctx, arg(argc, argv, 3)), num(ctx, arg(argc, argv, 4)));
    if (JS_ToBool(ctx, arg(argc, argv, 5)) > 0) {
        strokePath(c, st, c->s.transform.map(user));
    } else {
        QBrush brush = st.brush;
        brush.setTransform(c->s.transform);
        const QPainterPath path = c->s.transform.map(user);
        paint(*c, st, [&](QPainter& p) { p.fillPath(path, brush); });
    }
    return JS_UNDEFINED;
}

// (estilo, texto) → [ancho, ascenso, descenso, izquierda, derecha]
JSValue jsMeasure(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    const Style st = styleOf(ctx, arg(argc, argv, 0));
    const QString text = str(ctx, arg(argc, argv, 1));
    const QFontMetricsF fm(st.font);
    const QRectF box = fm.tightBoundingRect(text);
    const double values[5] = {fm.horizontalAdvance(text), fm.ascent(), fm.descent(), -box.left(), box.right()};
    JSValue out = JS_NewArray(ctx);
    for (int i = 0; i < 5; ++i)
        JS_SetPropertyUint32(ctx, out, uint32_t(i), JS_NewFloat64(ctx, values[i]));
    return out;
}

// Un rectángulo con medidas negativas, como lo normaliza el navegador.
QRect regionOf(JSContext* ctx, int argc, JSValueConst* argv, int first)
{
    double x = num(ctx, arg(argc, argv, first)), y = num(ctx, arg(argc, argv, first + 1));
    double w = num(ctx, arg(argc, argv, first + 2)), h = num(ctx, arg(argc, argv, first + 3));
    if (w < 0)
        x += w, w = -w;
    if (h < 0)
        y += h, h = -h;
    return QRect(int(std::floor(x)), int(std::floor(y)), int(std::ceil(w)), int(std::ceil(h)));
}

// (c, x, y, w, h) → Uint8ClampedArray en RGBA sin premultiplicar.
JSValue jsGetImageData(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const QRect r = regionOf(ctx, argc, argv, 1);
    if (r.width() <= 0 || r.height() <= 0)
        return JS_ThrowRangeError(ctx, "The source width and height must not be zero.");
    if (qint64(r.width()) * r.height() > kMaxPixels)
        return JS_ThrowRangeError(ctx, "The requested image data is too large.");
    QImage straight(r.size(), QImage::Format_RGBA8888);
    straight.fill(Qt::transparent);
    if (!c->image.isNull()) {
        QPainter p(&straight);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(QPoint(0, 0), c->image, r);
    }
    QByteArray bytes(qsizetype(r.width()) * r.height() * 4, Qt::Uninitialized);
    for (int y = 0; y < r.height(); ++y)
        std::memcpy(bytes.data() + qsizetype(y) * r.width() * 4, straight.constScanLine(y), size_t(r.width()) * 4);
    return newClampedArray(ctx, reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
}

// (c, data, anchoData, altoData, dx, dy, sucioX, sucioY, sucioAncho, sucioAlto): sin transformación,
// recorte, opacidad ni composición, como en el navegador.
JSValue jsPutImageData(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    size_t size = 0;
    const uint8_t* data = bytesOf(ctx, arg(argc, argv, 1), &size);
    const int dw = int(num(ctx, arg(argc, argv, 2))), dh = int(num(ctx, arg(argc, argv, 3)));
    if (!data || dw <= 0 || dh <= 0 || size < size_t(dw) * size_t(dh) * 4 || c->image.isNull())
        return JS_UNDEFINED;
    const int dx = int(std::lround(num(ctx, arg(argc, argv, 4)))), dy = int(std::lround(num(ctx, arg(argc, argv, 5))));
    QRect dirty(0, 0, dw, dh);
    if (argc > 6)
        dirty = regionOf(ctx, argc, argv, 6).intersected(QRect(0, 0, dw, dh));
    dirty = dirty.intersected(QRect(-dx, -dy, c->image.width(), c->image.height()));
    for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
        const uint8_t* src = data + (size_t(y) * size_t(dw) + size_t(dirty.left())) * 4;
        uint8_t* dst = c->image.scanLine(y + dy) + size_t(dirty.left() + dx) * 4;
        for (int x = 0; x < dirty.width(); ++x, src += 4, dst += 4) {
            const int a = src[3];
            dst[0] = uint8_t((src[0] * a + 127) / 255);
            dst[1] = uint8_t((src[1] * a + 127) / 255);
            dst[2] = uint8_t((src[2] * a + 127) / 255);
            dst[3] = uint8_t(a);
        }
    }
    return JS_UNDEFINED;
}

// (c, estilo, imagen, sx, sy, sw, sh, dx, dy, dw, dh)
JSValue jsDrawImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    const QImage source = imageOf(ctx, arg(argc, argv, 2));
    if (source.isNull())
        return JS_ThrowTypeError(ctx, "drawImage(): the image is not a canvas or ImageData.");
    double v[8];
    for (int i = 0; i < 8; ++i)
        v[i] = num(ctx, arg(argc, argv, i + 3), std::nan(""));
    QRectF from(v[0], v[1], v[2], v[3]), to(v[4], v[5], v[6], v[7]);
    if (!std::isfinite(v[0]))
        from = QRectF(source.rect());
    if (!std::isfinite(v[6]) || !std::isfinite(v[7]))
        to.setSize(from.size());
    if (from.isEmpty() || to.isEmpty())
        return JS_UNDEFINED;
    const Style st = styleOf(ctx, arg(argc, argv, 1));
    paint(*c, st, [&](QPainter& p) {
        p.setTransform(c->s.transform);
        p.drawImage(to, source, from);
    });
    return JS_UNDEFINED;
}

// (c) → ArrayBuffer con el PNG.
JSValue jsPng(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    CANVAS_ARG
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!c->image.isNull())
        c->image.save(&buffer, "PNG");
    return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t*>(png.constData()), size_t(png.size()));
}

// (texto) → el color normalizado, o null si no es un color de CSS.
JSValue jsCssColor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    QColor c;
    if (!parseColor(str(ctx, arg(argc, argv, 0)), &c))
        return JS_NULL;
    const QByteArray text = cssText(c).toUtf8();
    return JS_NewStringLen(ctx, text.constData(), size_t(text.size()));
}

} // namespace

const QImage* canvasImage(JSValueConst value)
{
    const auto* c = static_cast<const Canvas*>(JS_GetOpaque(value, g_canvasClass));
    return c ? &c->image : nullptr;
}

void installCanvas(JSContext* ctx, JSValueConst native)
{
    {
        const std::lock_guard lock(g_classMutex);
        JSRuntime* rt = JS_GetRuntime(ctx);
        JS_NewClassID(rt, &g_canvasClass);
        if (!JS_IsRegisteredClass(rt, g_canvasClass)) {
            JSClassDef def{};
            def.class_name = "KaintCanvas";
            def.finalizer = finalizeCanvas;
            JS_NewClass(rt, g_canvasClass, &def);
        }
    }
    const struct {
        const char* name;
        JSCFunction* fn;
        int length;
    } functions[] = {
        {"canvasNew", jsNew, 2},
        {"canvasResize", jsResize, 3},
        {"canvasSize", jsSize, 1},
        {"canvasSave", jsSave, 1},
        {"canvasRestore", jsRestore, 1},
        {"canvasTransform", jsTransform, 8},
        {"canvasGetTransform", jsGetTransform, 1},
        {"canvasPath", jsPath, 10},
        {"canvasFill", jsFill, 4},
        {"canvasStroke", jsStroke, 3},
        {"canvasClip", jsClip, 3},
        {"canvasIsPointInPath", jsIsPointInPath, 5},
        {"canvasRect", jsRect, 7},
        {"canvasText", jsText, 6},
        {"canvasMeasure", jsMeasure, 2},
        {"canvasGetImageData", jsGetImageData, 5},
        {"canvasPutImageData", jsPutImageData, 10},
        {"canvasDrawImage", jsDrawImage, 11},
        {"canvasPng", jsPng, 1},
        {"cssColor", jsCssColor, 1},
    };
    for (const auto& f : functions)
        JS_SetPropertyStr(ctx, native, f.name, JS_NewCFunction(ctx, f.fn, f.name, f.length));
}

} // namespace PaintJs::detail
