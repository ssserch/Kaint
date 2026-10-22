#include "core/Selection.h"

#include "core/Pixels.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cstring>

namespace {

QRect maskBounds(const QImage& mask)
{
    int top = -1, bottom = -1, left = mask.width(), right = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar* row = mask.constScanLine(y);
        int x0 = 0;
        while (x0 < mask.width() && row[x0] == 0)
            ++x0;
        if (x0 == mask.width())
            continue;
        int x1 = mask.width() - 1;
        while (x1 > x0 && row[x1] == 0)
            --x1;
        if (top < 0)
            top = y;
        bottom = y;
        left = std::min(left, x0);
        right = std::max(right, x1);
    }
    if (top < 0)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

QImage emptyMask(QSize size)
{
    QImage mask(size, QImage::Format_Alpha8);
    mask.fill(0);
    return mask;
}

QPainterPath rectPath(QSize canvas)
{
    QPainterPath p;
    p.addRect(QRectF(QPointF(0, 0), QSizeF(canvas)));
    return p;
}

} // namespace

Selection Selection::fromPath(const QPainterPath& path, QSize canvas, bool antialias)
{
    if (path.isEmpty() || canvas.isEmpty())
        return {};

    QImage mask = emptyMask(canvas);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing, antialias);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 255));
        p.drawPath(path);
    }
    const QRect bounds = path.boundingRect().toAlignedRect().intersected(QRect(QPoint(0, 0), canvas));
    if (bounds.isEmpty())
        return {};

    Selection s;
    s.d = std::make_shared<Data>();
    s.d->hasPath = true;
    s.d->path = path;
    s.d->mask = mask;
    s.d->bounds = bounds;
    return s;
}

Selection Selection::fromRect(const QRect& rect, QSize canvas)
{
    QPainterPath p;
    p.addRect(QRectF(rect.normalized()));
    return fromPath(p, canvas, false);
}

Selection Selection::fromMask(const QImage& source)
{
    if (source.isNull())
        return {};
    QImage mask = source.format() == QImage::Format_Alpha8 ? source : source.convertToFormat(QImage::Format_Alpha8);
    const QRect bounds = maskBounds(mask);
    if (bounds.isEmpty())
        return {};

    Selection s;
    s.d = std::make_shared<Data>();
    s.d->mask = mask;
    s.d->bounds = bounds;
    return s;
}

bool Selection::isRectangular() const
{
    if (!d || !d->hasPath)
        return false;
    const QPolygonF poly = d->path.toFillPolygon();
    if (poly.size() < 4 || poly.size() > 5)
        return false;
    const QRectF r = poly.boundingRect();
    auto near = [](qreal a, qreal b) { return std::abs(a - b) < 1e-6; };
    return std::all_of(poly.begin(), poly.end(), [&](const QPointF& pt) {
        return (near(pt.x(), r.left()) || near(pt.x(), r.right()))
            && (near(pt.y(), r.top()) || near(pt.y(), r.bottom()));
    });
}

const QPainterPath& Selection::path() const
{
    static const QPainterPath empty;
    return d ? d->path : empty;
}

const QImage& Selection::mask() const
{
    static const QImage empty;
    return d ? d->mask : empty;
}

int Selection::coverage(int x, int y) const
{
    if (!d)
        return 255;
    if (x < 0 || y < 0 || x >= d->mask.width() || y >= d->mask.height())
        return 0;
    return d->mask.constScanLine(y)[x];
}

const QVector<QLineF>& Selection::outline() const
{
    static const QVector<QLineF> empty;
    if (!d)
        return empty;
    if (d->outlineValid)
        return d->outline;

    // Bordes horizontales y verticales entre píxeles seleccionados (>= 128)
    // y no seleccionados, fusionando tramos consecutivos.
    const QImage& m = d->mask;
    const int w = m.width();
    const int h = m.height();
    auto sel = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && m.constScanLine(y)[x] >= 128;
    };

    QVector<QLineF> lines;
    const QRect b = d->bounds.adjusted(-1, -1, 1, 1);
    for (int y = std::max(0, b.top()); y <= std::min(h, b.bottom() + 1); ++y) {
        int start = -1;
        for (int x = std::max(0, b.left()); x <= std::min(w, b.right() + 1); ++x) {
            const bool edge = x < w && sel(x, y) != sel(x, y - 1);
            if (edge && start < 0)
                start = x;
            else if (!edge && start >= 0) {
                lines.append(QLineF(start, y, x, y));
                start = -1;
            }
        }
    }
    for (int x = std::max(0, b.left()); x <= std::min(w, b.right() + 1); ++x) {
        int start = -1;
        for (int y = std::max(0, b.top()); y <= std::min(h, b.bottom() + 1); ++y) {
            const bool edge = y < h && sel(x, y) != sel(x - 1, y);
            if (edge && start < 0)
                start = y;
            else if (!edge && start >= 0) {
                lines.append(QLineF(x, start, x, y));
                start = -1;
            }
        }
    }
    d->outline = lines;
    d->outlineValid = true;
    return d->outline;
}

Selection Selection::combined(const Selection& other, SelectionMode mode) const
{
    if (mode == SelectionMode::Replace)
        return other;
    if (isEmpty()) {
        if (mode == SelectionMode::Union || mode == SelectionMode::Xor)
            return other;
        return {};
    }
    if (other.isEmpty()) {
        if (mode == SelectionMode::Intersect)
            return {};
        return *this;
    }

    const QSize canvas = canvasSize();
    if (isPathBased() && other.isPathBased()) {
        const QPainterPath& a = d->path;
        const QPainterPath& b = other.d->path;
        QPainterPath result;
        switch (mode) {
        case SelectionMode::Union: result = a.united(b); break;
        case SelectionMode::Exclude: result = a.subtracted(b); break;
        case SelectionMode::Intersect: result = a.intersected(b); break;
        case SelectionMode::Xor: result = a.subtracted(b).united(b.subtracted(a)); break;
        case SelectionMode::Replace: result = b; break;
        }
        return fromPath(result, canvas);
    }

    // Combinación píxel a píxel de las máscaras.
    QImage result = emptyMask(canvas);
    const QImage& ma = d->mask;
    const QImage& mb = other.d->mask;
    for (int y = 0; y < canvas.height(); ++y) {
        const uchar* ra = ma.constScanLine(y);
        const uchar* rb = mb.constScanLine(y);
        uchar* out = result.scanLine(y);
        for (int x = 0; x < canvas.width(); ++x) {
            const int a = ra[x];
            const int b = rb[x];
            int v = 0;
            switch (mode) {
            case SelectionMode::Union: v = std::max(a, b); break;
            case SelectionMode::Exclude: v = a * (255 - b) / 255; break;
            case SelectionMode::Intersect: v = std::min(a, b); break;
            case SelectionMode::Xor: v = std::abs(a - b); break;
            case SelectionMode::Replace: v = b; break;
            }
            out[x] = uchar(v);
        }
    }
    return fromMask(result);
}

Selection Selection::transformed(const QTransform& transform) const
{
    if (!d || transform.isIdentity())
        return *this;
    const QSize canvas = canvasSize();
    if (d->hasPath)
        return fromPath(transform.map(d->path), canvas);

    QImage result = emptyMask(canvas);
    {
        QPainter p(&result);
        p.setRenderHint(QPainter::SmoothPixmapTransform, !transform.isAffine() || transform.isRotating());
        p.setTransform(transform);
        p.drawImage(0, 0, d->mask);
    }
    return fromMask(result);
}

Selection Selection::inverted(QSize canvas) const
{
    if (!d)
        return {};
    if (d->hasPath)
        return fromPath(rectPath(canvas).subtracted(d->path), canvas);

    QImage result = d->mask.copy();
    for (int y = 0; y < result.height(); ++y) {
        uchar* row = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x)
            row[x] = uchar(255 - row[x]);
    }
    return fromMask(result);
}

QString Selection::toJson() const
{
    QList<QPolygonF> polygons;
    if (d && d->hasPath) {
        polygons = d->path.toFillPolygons();
    } else if (d) {
        // Máscara: tramos horizontales fusionados en rectángulos.
        const QImage& m = d->mask;
        struct Run {
            int x0, x1, y0;
        };
        QList<Run> open;
        auto flush = [&](const Run& r, int yEnd) {
            polygons.append(QPolygonF(QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, yEnd))));
        };
        for (int y = d->bounds.top(); y <= d->bounds.bottom() + 1; ++y) {
            QList<std::pair<int, int>> runs;
            if (y <= d->bounds.bottom()) {
                const uchar* row = m.constScanLine(y);
                int x = d->bounds.left();
                while (x <= d->bounds.right()) {
                    while (x <= d->bounds.right() && row[x] < 128)
                        ++x;
                    const int start = x;
                    while (x <= d->bounds.right() && row[x] >= 128)
                        ++x;
                    if (x > start)
                        runs.append({start, x});
                }
            }
            QList<Run> next;
            for (const Run& r : std::as_const(open)) {
                const auto it = std::find(runs.begin(), runs.end(), std::make_pair(r.x0, r.x1));
                if (it != runs.end()) {
                    next.append(r);
                    runs.erase(it);
                } else {
                    flush(r, y);
                }
            }
            for (const auto& [x0, x1] : runs)
                next.append({x0, x1, y});
            open = next;
        }
    }

    QJsonArray list;
    for (const QPolygonF& poly : std::as_const(polygons)) {
        QStringList coords;
        for (const QPointF& pt : poly)
            coords << QString::number(pt.x(), 'g', 10) << QString::number(pt.y(), 'g', 10);
        list.append(coords.join(QLatin1Char(',')));
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("polygonList"), list);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

Selection Selection::fromJson(const QString& json, QSize canvas, bool* ok)
{
    if (ok)
        *ok = false;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    const QJsonArray list = doc.object().value(QStringLiteral("polygonList")).toArray();
    if (!doc.isObject() || list.isEmpty())
        return {};
    QPainterPath path;
    for (const QJsonValue& v : list) {
        const QStringList parts = v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        QPolygonF poly;
        for (int i = 0; i + 1 < parts.size(); i += 2)
            poly << QPointF(parts[i].trimmed().toDouble(), parts[i + 1].trimmed().toDouble());
        if (poly.size() >= 3) {
            path.addPolygon(poly);
            path.closeSubpath();
        }
    }
    if (path.isEmpty())
        return {};
    if (ok)
        *ok = true;
    return fromPath(path, canvas);
}
