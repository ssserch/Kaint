#include "core/CustomShapes.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainterPathStroker>
#include <QStandardPaths>
#include <QTransform>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace CustomShapes {

namespace {

// --- Sintaxis de trazados -----------------------------------------------------

class MarkupReader {
public:
    explicit MarkupReader(const QString& text) : m_s(text) {}

    void skip()
    {
        while (m_i < m_s.size() && (m_s[m_i].isSpace() || m_s[m_i] == QLatin1Char(',')))
            ++m_i;
    }
    bool atEnd()
    {
        skip();
        return m_i >= m_s.size();
    }
    bool atNumber()
    {
        skip();
        if (m_i >= m_s.size())
            return false;
        const QChar c = m_s[m_i];
        return c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('.');
    }
    QChar command()
    {
        skip();
        return m_i < m_s.size() && m_s[m_i].isLetter() ? m_s[m_i++] : QChar();
    }
    // Número con signo, decimales y exponente; nullopt si no hay.
    std::optional<double> number()
    {
        if (!atNumber())
            return std::nullopt;
        const qsizetype start = m_i;
        if (m_s[m_i] == QLatin1Char('-') || m_s[m_i] == QLatin1Char('+'))
            ++m_i;
        bool dot = false;
        while (m_i < m_s.size() && (m_s[m_i].isDigit() || (!dot && m_s[m_i] == QLatin1Char('.')))) {
            dot |= m_s[m_i] == QLatin1Char('.');
            ++m_i;
        }
        if (m_i < m_s.size() && (m_s[m_i] == QLatin1Char('e') || m_s[m_i] == QLatin1Char('E'))) {
            qsizetype j = m_i + 1;
            if (j < m_s.size() && (m_s[j] == QLatin1Char('-') || m_s[j] == QLatin1Char('+')))
                ++j;
            if (j < m_s.size() && m_s[j].isDigit()) {
                m_i = j;
                while (m_i < m_s.size() && m_s[m_i].isDigit())
                    ++m_i;
            }
        }
        bool ok = false;
        const double v = m_s.mid(start, m_i - start).toDouble(&ok);
        if (!ok) {
            m_i = start;
            return std::nullopt;
        }
        return v;
    }

private:
    const QString& m_s;
    qsizetype m_i = 0;
};

double angleBetween(QPointF u, QPointF v)
{
    return std::atan2(u.x() * v.y() - u.y() * v.x(), u.x() * v.x() + u.y() * v.y());
}

// Arco elíptico de WPF/SVG (por sus extremos) convertido en curvas de Bézier.
// 'clockwise' es el sentido horario en pantalla (y hacia abajo).
void arcTo(QPainterPath& path, QPointF from, double rx, double ry, double angleDeg, bool largeArc, bool clockwise,
           QPointF to)
{
    if (from == to)
        return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-12 || ry < 1e-12) {
        path.lineTo(to);
        return;
    }
    const double phi = angleDeg * M_PI / 180.0, cs = std::cos(phi), sn = std::sin(phi);
    const double dx2 = (from.x() - to.x()) / 2, dy2 = (from.y() - to.y()) / 2;
    const double x1 = cs * dx2 + sn * dy2, y1 = -sn * dx2 + cs * dy2;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const double coef = (largeArc != clockwise ? 1 : -1) * std::sqrt(std::max(0.0, den > 0 ? num / den : 0.0));
    const double cxp = coef * rx * y1 / ry, cyp = -coef * ry * x1 / rx;
    const double cx = cs * cxp - sn * cyp + (from.x() + to.x()) / 2;
    const double cy = sn * cxp + cs * cyp + (from.y() + to.y()) / 2;
    const QPointF u((x1 - cxp) / rx, (y1 - cyp) / ry), v((-x1 - cxp) / rx, (-y1 - cyp) / ry);
    const double theta = angleBetween(QPointF(1, 0), u);
    double sweep = angleBetween(u, v);
    if (!clockwise && sweep > 0)
        sweep -= 2 * M_PI;
    else if (clockwise && sweep < 0)
        sweep += 2 * M_PI;
    const int segments = std::max(1, int(std::ceil(std::abs(sweep) / (M_PI / 2) - 1e-9)));
    const double delta = sweep / segments, t = 4.0 / 3.0 * std::tan(delta / 4);
    auto at = [&](double a) {
        return QPointF(cx + rx * std::cos(a) * cs - ry * std::sin(a) * sn, cy + rx * std::cos(a) * sn + ry * std::sin(a) * cs);
    };
    auto tangent = [&](double a) {
        return QPointF(-rx * std::sin(a) * cs - ry * std::cos(a) * sn, -rx * std::sin(a) * sn + ry * std::cos(a) * cs);
    };
    for (int i = 0; i < segments; ++i) {
        const double a1 = theta + i * delta, a2 = a1 + delta;
        const QPointF end = i == segments - 1 ? to : at(a2);
        path.cubicTo(at(a1) + t * tangent(a1), at(a2) - t * tangent(a2), end);
    }
}

// Contornos con la orientación de WPF: los exteriores en un sentido y los
// agujeros en el contrario. Las operaciones booleanas de Qt no la garantizan y,
// dentro de un GeometryGroup con regla Nonzero, un agujero se rellenaría.
QPainterPath oriented(const QPainterPath& path)
{
    const QList<QPolygonF> polys = path.toSubpathPolygons();
    auto area = [](const QPolygonF& poly) {
        double a = 0;
        for (qsizetype i = 0; i < poly.size(); ++i) {
            const QPointF& p = poly[i];
            const QPointF& q = poly[(i + 1) % poly.size()];
            a += p.x() * q.y() - q.x() * p.y();
        }
        return a;
    };
    QPainterPath out;
    out.setFillRule(Qt::WindingFill);
    for (qsizetype i = 0; i < polys.size(); ++i) {
        if (polys[i].size() < 3)
            continue;
        int depth = 0; // contornos que lo rodean
        for (qsizetype j = 0; j < polys.size(); ++j)
            if (j != i && polys[j].containsPoint(polys[i].first(), Qt::OddEvenFill))
                ++depth;
        QPolygonF poly = polys[i];
        if ((area(poly) > 0) != (depth % 2 == 0))
            std::reverse(poly.begin(), poly.end());
        out.addPolygon(poly);
        out.closeSubpath();
    }
    return out;
}

// Curva (Catmull-Rom) que pasa por todos los puntos: PolyCurveSegment.
void curveThrough(QPainterPath& path, QPointF start, const QList<QPointF>& points)
{
    QList<QPointF> p{start};
    p += points;
    for (qsizetype i = 0; i + 1 < p.size(); ++i) {
        const QPointF p0 = p[std::max<qsizetype>(0, i - 1)], p1 = p[i], p2 = p[i + 1];
        const QPointF p3 = p[std::min<qsizetype>(p.size() - 1, i + 2)];
        path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
    }
}

} // namespace

QPainterPath parsePathMarkup(const QString& data, bool* ok)
{
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill); // WPF: EvenOdd salvo "F1"
    MarkupReader r(data);
    bool good = true;
    QPointF cur, start, lastCtrl;
    QChar last;
    QChar cmd;
    auto num = [&](double& v) {
        const auto n = r.number();
        if (!n) {
            good = false;
            return false;
        }
        v = *n;
        return true;
    };
    auto pt = [&](bool relative, QPointF& p) {
        double x = 0, y = 0;
        if (!num(x) || !num(y))
            return false;
        p = relative ? cur + QPointF(x, y) : QPointF(x, y);
        return true;
    };
    while (good && !r.atEnd()) {
        if (!r.atNumber()) {
            cmd = r.command();
            if (cmd.isNull()) {
                good = false;
                break;
            }
            if (cmd == QLatin1Char('F')) {
                double rule = 0;
                if (!num(rule))
                    break;
                path.setFillRule(rule != 0 ? Qt::WindingFill : Qt::OddEvenFill);
                continue;
            }
        } else if (cmd.isNull()) {
            good = false;
            break;
        }
        const bool rel = cmd.isLower();
        switch (cmd.toUpper().unicode()) {
        case 'M': {
            QPointF p;
            if (!pt(rel, p))
                break;
            path.moveTo(p);
            cur = start = p;
            // Las parejas siguientes son líneas.
            cmd = rel ? QLatin1Char('l') : QLatin1Char('L');
            break;
        }
        case 'L': {
            QPointF p;
            if (pt(rel, p))
                path.lineTo(cur = p);
            break;
        }
        case 'H': {
            double x = 0;
            if (num(x))
                path.lineTo(cur = QPointF(rel ? cur.x() + x : x, cur.y()));
            break;
        }
        case 'V': {
            double y = 0;
            if (num(y))
                path.lineTo(cur = QPointF(cur.x(), rel ? cur.y() + y : y));
            break;
        }
        case 'C': {
            QPointF c1, c2, p;
            if (pt(rel, c1) && pt(rel, c2) && pt(rel, p)) {
                path.cubicTo(c1, c2, p);
                lastCtrl = c2;
                cur = p;
            }
            break;
        }
        case 'S': {
            QPointF c2, p;
            if (pt(rel, c2) && pt(rel, p)) {
                const bool smooth = last.toUpper() == QLatin1Char('C') || last.toUpper() == QLatin1Char('S');
                const QPointF c1 = smooth ? 2 * cur - lastCtrl : cur;
                path.cubicTo(c1, c2, p);
                lastCtrl = c2;
                cur = p;
            }
            break;
        }
        case 'Q': {
            QPointF c, p;
            if (pt(rel, c) && pt(rel, p)) {
                path.quadTo(c, p);
                lastCtrl = c;
                cur = p;
            }
            break;
        }
        case 'T': {
            QPointF p;
            if (pt(rel, p)) {
                const bool smooth = last.toUpper() == QLatin1Char('Q') || last.toUpper() == QLatin1Char('T');
                const QPointF c = smooth ? 2 * cur - lastCtrl : cur;
                path.quadTo(c, p);
                lastCtrl = c;
                cur = p;
            }
            break;
        }
        case 'A': {
            double rx = 0, ry = 0, angle = 0, large = 0, sweep = 0;
            QPointF p;
            if (num(rx) && num(ry) && num(angle) && num(large) && num(sweep) && pt(rel, p)) {
                arcTo(path, cur, rx, ry, angle, large != 0, sweep != 0, p);
                cur = p;
            }
            break;
        }
        case 'Z':
            path.closeSubpath();
            cur = start;
            if (r.atNumber())
                good = false; // números sueltos tras Z: no se repite el cierre
            break;
        default:
            good = false;
            break;
        }
        last = cmd;
    }
    if (ok)
        *ok = good;
    return path;
}

namespace {

// --- XAML -------------------------------------------------------------------------

struct Node {
    QString name; // nombre local: "PathFigure" o, en una propiedad, "PathFigure.Segments"
    QHash<QString, QString> attrs;
    std::vector<Node> children; // vector: admite el tipo aún incompleto
};

bool readTree(QXmlStreamReader& xml, Node& root)
{
    QList<Node*> stack;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            Node n;
            n.name = xml.name().toString();
            for (const QXmlStreamAttribute& a : xml.attributes())
                n.attrs.insert(a.name().toString(), a.value().toString().trimmed());
            // Solo se añaden hijos al elemento abierto más profundo: los
            // punteros a sus antecesores siguen siendo válidos.
            if (stack.isEmpty()) {
                root = n;
                stack.append(&root);
            } else {
                stack.last()->children.push_back(n);
                stack.append(&stack.last()->children.back());
            }
        } else if (xml.isEndElement() && !stack.isEmpty()) {
            stack.removeLast();
        }
    }
    return !xml.hasError() && !root.name.isEmpty();
}

QList<double> numbers(const QString& text)
{
    QList<double> list;
    MarkupReader r(text);
    while (auto n = r.number())
        list.append(*n);
    return list;
}

QPointF toPoint(const QString& text, QPointF fallback = {})
{
    const QList<double> n = numbers(text);
    return n.size() >= 2 ? QPointF(n[0], n[1]) : fallback;
}

QList<QPointF> toPoints(const QString& text)
{
    const QList<double> n = numbers(text);
    QList<QPointF> list;
    for (qsizetype i = 0; i + 1 < n.size(); i += 2)
        list.append(QPointF(n[i], n[i + 1]));
    return list;
}

double toNumber(const QString& text, double fallback)
{
    bool ok = false;
    const double v = text.toDouble(&ok);
    return ok ? v : fallback;
}

bool toBool(const QString& text, bool fallback)
{
    if (text.isEmpty())
        return fallback;
    return text.compare(QLatin1String("True"), Qt::CaseInsensitive) == 0 || text == QLatin1String("1");
}

class Builder {
public:
    QString error;

    // Elementos hijos que no son propiedades (p. ej. los PathFigure de un
    // PathGeometry), también dentro de su propiedad de contenido o de una
    // colección (PathFigureCollection, GeometryCollection...).
    static QList<const Node*> items(const Node& n, const QString& contentProperty)
    {
        QList<const Node*> list;
        for (const Node& c : n.children) {
            if (c.name.endsWith(QLatin1Char('.') + contentProperty)) {
                for (const Node& g : c.children)
                    list += g.name.endsWith(QLatin1String("Collection")) ? items(g, contentProperty) : QList<const Node*>{&g};
            } else if (c.name.endsWith(QLatin1String("Collection"))) {
                list += items(c, contentProperty);
            } else if (!c.name.contains(QLatin1Char('.'))) {
                list.append(&c);
            }
        }
        return list;
    }

    // Valor de una propiedad dada como elemento: <Tipo.Propiedad><Valor/></Tipo.Propiedad>.
    static const Node* property(const Node& n, const QString& prop)
    {
        for (const Node& c : n.children)
            if (c.name.endsWith(QLatin1Char('.') + prop) && !c.children.empty())
                return &c.children.front();
        return nullptr;
    }

    QTransform transform(const Node& t)
    {
        const auto& a = t.attrs;
        const double cx = toNumber(a.value(QStringLiteral("CenterX")), 0), cy = toNumber(a.value(QStringLiteral("CenterY")), 0);
        const QTransform toCenter = QTransform::fromTranslate(-cx, -cy), back = QTransform::fromTranslate(cx, cy);
        if (t.name == QLatin1String("ScaleTransform"))
            return toCenter
                 * QTransform::fromScale(toNumber(a.value(QStringLiteral("ScaleX")), 1), toNumber(a.value(QStringLiteral("ScaleY")), 1))
                 * back;
        if (t.name == QLatin1String("RotateTransform"))
            return toCenter * QTransform().rotate(toNumber(a.value(QStringLiteral("Angle")), 0)) * back;
        if (t.name == QLatin1String("TranslateTransform"))
            return QTransform::fromTranslate(toNumber(a.value(QStringLiteral("X")), 0), toNumber(a.value(QStringLiteral("Y")), 0));
        if (t.name == QLatin1String("SkewTransform")) {
            const double ax = toNumber(a.value(QStringLiteral("AngleX")), 0) * M_PI / 180;
            const double ay = toNumber(a.value(QStringLiteral("AngleY")), 0) * M_PI / 180;
            return toCenter * QTransform(1, std::tan(ay), std::tan(ax), 1, 0, 0) * back;
        }
        if (t.name == QLatin1String("MatrixTransform"))
            return matrix(a.value(QStringLiteral("Matrix")));
        if (t.name == QLatin1String("TransformGroup")) {
            QTransform m;
            for (const Node* c : items(t, QStringLiteral("Children")))
                m = m * transform(*c);
            return m;
        }
        return {};
    }

    static QTransform matrix(const QString& text)
    {
        const QList<double> n = numbers(text);
        return n.size() >= 6 ? QTransform(n[0], n[1], n[2], n[3], n[4], n[5]) : QTransform();
    }

    // La propiedad Transform de cualquier geometría (atributo o elemento).
    QPainterPath withTransform(const Node& n, QPainterPath path)
    {
        if (const Node* t = property(n, QStringLiteral("Transform")))
            return transform(*t).map(path);
        const QString text = n.attrs.value(QStringLiteral("Transform"));
        if (!text.isEmpty() && text != QLatin1String("Identity"))
            return matrix(text).map(path);
        return path;
    }

    // Geometría de una propiedad (atributo con trazado o elemento).
    std::optional<QPainterPath> geometryProperty(const Node& n, const QString& prop)
    {
        if (n.attrs.contains(prop))
            return markup(n.attrs.value(prop));
        if (const Node* g = property(n, prop))
            return geometry(*g);
        return std::nullopt;
    }

    QPainterPath markup(const QString& text)
    {
        bool ok = false;
        QPainterPath p = parsePathMarkup(text, &ok);
        if (!ok)
            error = QObject::tr("trazado no válido: «%1»").arg(text.left(40));
        return p;
    }

    static Qt::FillRule fillRule(const Node& n)
    {
        // En WPF la regla predeterminada es EvenOdd.
        return n.attrs.value(QStringLiteral("FillRule")) == QLatin1String("Nonzero") ? Qt::WindingFill : Qt::OddEvenFill;
    }

    void figure(const Node& f, QPainterPath& path)
    {
        QPointF cur = toPoint(f.attrs.value(QStringLiteral("StartPoint")));
        path.moveTo(cur);
        for (const Node* s : items(f, QStringLiteral("Segments"))) {
            const auto& a = s->attrs;
            const QString& t = s->name;
            if (t == QLatin1String("LineSegment")) {
                path.lineTo(cur = toPoint(a.value(QStringLiteral("Point")), cur));
            } else if (t == QLatin1String("PolyLineSegment")) {
                for (const QPointF& p : toPoints(a.value(QStringLiteral("Points"))))
                    path.lineTo(cur = p);
            } else if (t == QLatin1String("BezierSegment")) {
                const QPointF p3 = toPoint(a.value(QStringLiteral("Point3")), cur);
                path.cubicTo(toPoint(a.value(QStringLiteral("Point1")), cur), toPoint(a.value(QStringLiteral("Point2")), cur), p3);
                cur = p3;
            } else if (t == QLatin1String("PolyBezierSegment")) {
                const QList<QPointF> p = toPoints(a.value(QStringLiteral("Points")));
                for (qsizetype i = 0; i + 2 < p.size(); i += 3)
                    path.cubicTo(p[i], p[i + 1], cur = p[i + 2]);
            } else if (t == QLatin1String("QuadraticBezierSegment")) {
                const QPointF p2 = toPoint(a.value(QStringLiteral("Point2")), cur);
                path.quadTo(toPoint(a.value(QStringLiteral("Point1")), cur), p2);
                cur = p2;
            } else if (t == QLatin1String("PolyQuadraticBezierSegment")) {
                const QList<QPointF> p = toPoints(a.value(QStringLiteral("Points")));
                for (qsizetype i = 0; i + 1 < p.size(); i += 2)
                    path.quadTo(p[i], cur = p[i + 1]);
            } else if (t == QLatin1String("ArcSegment")) {
                const QPointF end = toPoint(a.value(QStringLiteral("Point")), cur);
                const QPointF size = toPoint(a.value(QStringLiteral("Size")));
                arcTo(path, cur, size.x(), size.y(), toNumber(a.value(QStringLiteral("RotationAngle")), 0),
                      toBool(a.value(QStringLiteral("IsLargeArc")), false),
                      a.value(QStringLiteral("SweepDirection")) == QLatin1String("Clockwise"), end);
                cur = end;
            } else if (t == QLatin1String("PolyCurveSegment")) {
                const QList<QPointF> p = toPoints(a.value(QStringLiteral("Points")));
                curveThrough(path, cur, p);
                if (!p.isEmpty())
                    cur = p.last();
            } else {
                error = QObject::tr("segmento desconocido: %1").arg(t);
            }
        }
        if (toBool(f.attrs.value(QStringLiteral("IsClosed")), false))
            path.closeSubpath();
    }

    QPainterPath geometry(const Node& n)
    {
        const QString& t = n.name;
        QPainterPath p;
        if (t == QLatin1String("PathGeometry")) {
            if (n.attrs.contains(QStringLiteral("Figures")))
                p = markup(n.attrs.value(QStringLiteral("Figures")));
            for (const Node* f : items(n, QStringLiteral("Figures")))
                figure(*f, p);
            if (n.attrs.contains(QStringLiteral("FillRule")) || !n.attrs.contains(QStringLiteral("Figures")))
                p.setFillRule(fillRule(n));
        } else if (t == QLatin1String("GeometryGroup")) {
            for (const Node* c : items(n, QStringLiteral("Children")))
                p.addPath(geometry(*c));
            p.setFillRule(fillRule(n));
        } else if (t == QLatin1String("CombinedGeometry")) {
            const QPainterPath a = geometryProperty(n, QStringLiteral("Geometry1")).value_or(QPainterPath());
            const QPainterPath b = geometryProperty(n, QStringLiteral("Geometry2")).value_or(QPainterPath());
            const QString mode = n.attrs.value(QStringLiteral("GeometryCombineMode"), QStringLiteral("Union"));
            if (mode == QLatin1String("Intersect"))
                p = a.intersected(b);
            else if (mode == QLatin1String("Exclude"))
                p = a.subtracted(b);
            else if (mode == QLatin1String("Xor"))
                p = a.subtracted(b).united(b.subtracted(a));
            else
                p = a.united(b);
            p = oriented(p);
        } else if (t == QLatin1String("EllipseGeometry")) {
            p.addEllipse(toPoint(n.attrs.value(QStringLiteral("Center"))), toNumber(n.attrs.value(QStringLiteral("RadiusX")), 0),
                         toNumber(n.attrs.value(QStringLiteral("RadiusY")), 0));
        } else if (t == QLatin1String("RectangleGeometry")) {
            const QList<double> r = numbers(n.attrs.value(QStringLiteral("Rect")));
            if (r.size() >= 4) {
                const double rx = toNumber(n.attrs.value(QStringLiteral("RadiusX")), 0);
                const double ry = toNumber(n.attrs.value(QStringLiteral("RadiusY")), 0);
                p.addRoundedRect(QRectF(r[0], r[1], r[2], r[3]), rx, ry);
            }
        } else if (t == QLatin1String("LineGeometry")) {
            p.moveTo(toPoint(n.attrs.value(QStringLiteral("StartPoint"))));
            p.lineTo(toPoint(n.attrs.value(QStringLiteral("EndPoint"))));
        } else if (t == QLatin1String("TransformedGeometry") || t == QLatin1String("FlattenedGeometry")) {
            p = geometryProperty(n, QStringLiteral("Geometry")).value_or(QPainterPath());
        } else if (t == QLatin1String("OutlinedGeometry")) {
            p = oriented(geometryProperty(n, QStringLiteral("Geometry")).value_or(QPainterPath()).simplified());
        } else if (t == QLatin1String("WidenedGeometry")) {
            QPainterPathStroker stroker;
            stroker.setWidth(toNumber(n.attrs.value(QStringLiteral("Thickness")), 1));
            if (const Node* s = property(n, QStringLiteral("StrokeStyle"))) {
                const QString cap = s->attrs.value(QStringLiteral("StartLineCap"));
                stroker.setCapStyle(cap == QLatin1String("Round") ? Qt::RoundCap
                                    : cap == QLatin1String("Square") ? Qt::SquareCap
                                                                     : Qt::FlatCap);
                const QString join = s->attrs.value(QStringLiteral("LineJoin"));
                stroker.setJoinStyle(join == QLatin1String("Round") ? Qt::RoundJoin
                                     : join == QLatin1String("Bevel") ? Qt::BevelJoin
                                                                      : Qt::MiterJoin);
                stroker.setMiterLimit(toNumber(s->attrs.value(QStringLiteral("MiterLimit")), 10));
            }
            p = stroker.createStroke(geometryProperty(n, QStringLiteral("Geometry")).value_or(QPainterPath()));
        } else if (t != QLatin1String("EmptyGeometry")) {
            error = QObject::tr("geometría desconocida: %1").arg(t);
        }
        return withTransform(n, p);
    }
};

QStringList g_testPaths;
bool g_testing = false;
QList<Shape> g_shapes;
bool g_loaded = false;

} // namespace

bool parse(const QByteArray& xaml, Shape* shape, QString* error)
{
    QXmlStreamReader xml(xaml);
    Node root;
    if (!readTree(xml, root)) {
        if (error)
            *error = xml.hasError() ? xml.errorString() : QObject::tr("archivo vacío");
        return false;
    }
    if (root.name != QLatin1String("SimpleGeometryShape")) {
        if (error)
            *error = QObject::tr("no es un SimpleGeometryShape (%1)").arg(root.name);
        return false;
    }
    Builder b;
    QPainterPath path;
    if (root.attrs.contains(QStringLiteral("Geometry"))) {
        path = b.markup(root.attrs.value(QStringLiteral("Geometry")));
    } else if (auto g = b.geometryProperty(root, QStringLiteral("Geometry"))) {
        path = *g;
    } else {
        // La geometría como contenido directo del elemento.
        const QList<const Node*> content = Builder::items(root, QStringLiteral("Geometry"));
        if (content.isEmpty()) {
            if (error)
                *error = QObject::tr("la forma no tiene geometría");
            return false;
        }
        path = b.geometry(*content.first());
    }
    if (!b.error.isEmpty()) {
        if (error)
            *error = b.error;
        return false;
    }
    if (path.isEmpty() || path.boundingRect().isEmpty()) {
        if (error)
            *error = QObject::tr("la geometría está vacía");
        return false;
    }
    shape->name = root.attrs.value(QStringLiteral("DisplayName"));
    shape->path = path;
    return true;
}

bool load(const QString& file, Shape* shape, QString* error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (!parse(f.readAll(), shape, error))
        return false;
    shape->file = QFileInfo(file).absoluteFilePath();
    if (shape->name.isEmpty())
        shape->name = QFileInfo(file).completeBaseName();
    return true;
}

QStringList searchPaths()
{
    if (g_testing)
        return g_testPaths;
    QStringList paths{QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Kaint/Shapes")};
    if (QCoreApplication::instance()) {
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString& c : {appDir + QStringLiteral("/shapes"), appDir + QStringLiteral("/../share/kaint/shapes")}) {
            const QString canonical = QFileInfo(c).canonicalFilePath();
            if (!canonical.isEmpty() && !paths.contains(canonical))
                paths << canonical;
        }
    }
    return paths;
}

void setSearchPathsForTesting(const QStringList& paths)
{
    g_testing = true;
    g_testPaths = paths;
    g_loaded = false;
}

void reload()
{
    g_shapes.clear();
    for (const QString& dir : searchPaths()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.xaml")}, QDir::Files);
        for (const QFileInfo& fi : files) {
            Shape s;
            QString error;
            if (load(fi.absoluteFilePath(), &s, &error))
                g_shapes.append(s);
            else
                qWarning("Kaint: forma personalizada no válida %s: %s", qPrintable(fi.fileName()), qPrintable(error));
        }
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(g_shapes.begin(), g_shapes.end(),
                     [&](const Shape& a, const Shape& b) { return collator.compare(a.name, b.name) < 0; });
    g_loaded = true;
}

const QList<Shape>& all()
{
    if (!g_loaded)
        reload();
    return g_shapes;
}

const Shape* find(const QString& file)
{
    for (const Shape& s : all())
        if (s.file == file)
            return &s;
    return nullptr;
}

QPainterPath fitted(const QPainterPath& path, const QRectF& rect)
{
    const QRectF b = path.boundingRect();
    const QRectF r = rect.normalized();
    const double sx = b.width() > 1e-9 ? r.width() / b.width() : 1;
    const double sy = b.height() > 1e-9 ? r.height() / b.height() : 1;
    return (QTransform::fromTranslate(-b.left(), -b.top()) * QTransform::fromScale(sx, sy)
            * QTransform::fromTranslate(r.left(), r.top()))
        .map(path);
}

} // namespace CustomShapes
