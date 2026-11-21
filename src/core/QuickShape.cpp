#include "core/QuickShape.h"

#include <QCoreApplication>
#include <QLineF>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace QuickShape {

namespace {

qreal distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF d = b - a;
    const qreal len2 = d.x() * d.x() + d.y() * d.y();
    if (len2 <= 1e-9)
        return QLineF(p, a).length();
    const qreal t = std::clamp(((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / len2, 0.0, 1.0);
    return QLineF(p, a + d * t).length();
}

// Douglas-Peucker: los puntos que hacen falta para seguir la línea con un error menor que 'eps'.
void simplify(const QList<QPointF>& pts, int from, int to, qreal eps, QList<int>& keep)
{
    qreal worst = -1;
    int at = -1;
    for (int i = from + 1; i < to; ++i) {
        const qreal d = distanceToSegment(pts[i], pts[from], pts[to]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (at >= 0 && worst > eps) {
        simplify(pts, from, at, eps, keep);
        keep << at;
        simplify(pts, at, to, eps, keep);
    }
}

qreal pathLength(const QList<QPointF>& pts)
{
    qreal len = 0;
    for (int i = 1; i < pts.size(); ++i)
        len += QLineF(pts[i - 1], pts[i]).length();
    return len;
}

// La elipse que mejor encaja (sus ejes, los de la nube de puntos) y lo que se aparta de ella (0: nada).
struct Fit {
    QPointF center;
    qreal a = 0, b = 0, angle = 0, error = 1;
};

Fit fitEllipse(const QList<QPointF>& pts)
{
    Fit f;
    QPointF c;
    for (const QPointF& p : pts)
        c += p;
    c /= qreal(pts.size());
    qreal sxx = 0, syy = 0, sxy = 0;
    for (const QPointF& p : pts) {
        const QPointF d = p - c;
        sxx += d.x() * d.x();
        syy += d.y() * d.y();
        sxy += d.x() * d.y();
    }
    f.angle = 0.5 * std::atan2(2 * sxy, sxx - syy);
    const qreal ca = std::cos(f.angle), sa = std::sin(f.angle);
    qreal u0 = 1e18, u1 = -1e18, v0 = 1e18, v1 = -1e18;
    for (const QPointF& p : pts) {
        const QPointF d = p - c;
        const qreal u = d.x() * ca + d.y() * sa, v = -d.x() * sa + d.y() * ca;
        u0 = std::min(u0, u);
        u1 = std::max(u1, u);
        v0 = std::min(v0, v);
        v1 = std::max(v1, v);
    }
    f.a = (u1 - u0) / 2;
    f.b = (v1 - v0) / 2;
    f.center = c + QPointF(ca * (u0 + u1) / 2 - sa * (v0 + v1) / 2, sa * (u0 + u1) / 2 + ca * (v0 + v1) / 2);
    if (f.a < 2 || f.b < 2)
        return f;
    qreal err = 0;
    for (const QPointF& p : pts) {
        const QPointF d = p - f.center;
        const qreal u = d.x() * ca + d.y() * sa, v = -d.x() * sa + d.y() * ca;
        err += std::abs(std::hypot(u / f.a, v / f.b) - 1);
    }
    f.error = err / pts.size();
    return f;
}

QList<QPointF> ellipsePoints(const Fit& f)
{
    QList<QPointF> out;
    const int n = std::clamp(int((f.a + f.b) * 1.2), 32, 360);
    const qreal ca = std::cos(f.angle), sa = std::sin(f.angle);
    for (int i = 0; i <= n; ++i) {
        const qreal t = 2 * M_PI * i / n, x = f.a * std::cos(t), y = f.b * std::sin(t);
        out << f.center + QPointF(x * ca - y * sa, x * sa + y * ca);
    }
    return out;
}

qreal cornerAngle(QPointF a, QPointF b, QPointF c)
{
    const QPointF u = a - b, v = c - b;
    const qreal lu = std::hypot(u.x(), u.y()), lv = std::hypot(v.x(), v.y());
    if (lu < 1e-6 || lv < 1e-6)
        return 180;
    return std::acos(std::clamp((u.x() * v.x() + u.y() * v.y()) / (lu * lv), -1.0, 1.0)) * 180 / M_PI;
}

} // namespace

Result recognize(const QList<QPointF>& raw)
{
    Result r;
    // Sin los puntos repetidos.
    QList<QPointF> pts;
    for (const QPointF& p : raw)
        if (pts.isEmpty() || QLineF(pts.last(), p).length() > 0.5)
            pts << p;
    if (pts.size() < 3)
        return r;
    QRectF box;
    for (const QPointF& p : pts)
        box |= QRectF(p, QSizeF(0.001, 0.001));
    const qreal diag = std::hypot(box.width(), box.height()), length = pathLength(pts);
    if (diag < 8 || length < 12)
        return r;
    const QPointF first = pts.first(), last = pts.last();
    const bool closed = QLineF(first, last).length() < std::max(14.0, 0.22 * diag) && length > 2.2 * QLineF(first, last).length() + diag * 0.8;

    if (!closed) {
        // Una línea: nada se aparta mucho de la recta del principio al final.
        const qreal straight = QLineF(first, last).length();
        qreal worst = 0;
        for (const QPointF& p : pts)
            worst = std::max(worst, distanceToSegment(p, first, last));
        if (straight > 8 && worst < std::max(4.0, 0.09 * straight)) {
            r.kind = Kind::Line;
            r.points = {first, last};
        }
        return r;
    }

    // Cerrada: un polígono con pocas esquinas o una elipse.
    QList<QPointF> ring = pts;
    ring.last() = ring.first();
    QList<int> keep{0};
    simplify(ring, 0, int(ring.size()) - 1, 0.07 * diag, keep);
    keep << int(ring.size()) - 1;
    std::sort(keep.begin(), keep.end());
    QList<QPointF> poly;
    for (int i : keep)
        poly << ring[i];
    poly.removeLast();
    // Sin las «esquinas» casi rectas (180°).
    for (bool changed = true; changed && poly.size() > 3;) {
        changed = false;
        for (int i = 0; i < poly.size(); ++i) {
            const int n = int(poly.size());
            if (cornerAngle(poly[(i + n - 1) % n], poly[i], poly[(i + 1) % n]) > 155) {
                poly.removeAt(i);
                changed = true;
                break;
            }
        }
    }
    const Fit fit = fitEllipse(pts);
    const int corners = int(poly.size());
    const bool roundish = fit.error < 0.09 || (corners > 6 && fit.error < 0.16);
    if (roundish && fit.a >= 3 && fit.b >= 3) {
        Fit f = fit;
        r.kind = Kind::Ellipse;
        if (std::min(f.a, f.b) / std::max(f.a, f.b) > 0.88) {
            f.a = f.b = (f.a + f.b) / 2;
            r.kind = Kind::Circle;
        }
        r.points = ellipsePoints(f);
        return r;
    }
    if (corners >= 3 && corners <= 8) {
        r.kind = corners == 3 ? Kind::Triangle : Kind::Polygon;
        // Cuatro esquinas casi rectas: un rectángulo (con los lados del primero, sin torcer).
        if (corners == 4) {
            bool square = true;
            for (int i = 0; i < 4; ++i)
                square = square && std::abs(cornerAngle(poly[(i + 3) % 4], poly[i], poly[(i + 1) % 4]) - 90) < 20;
            if (square) {
                r.kind = Kind::Rectangle;
                const QPointF c = (poly[0] + poly[1] + poly[2] + poly[3]) / 4;
                const QPointF side = poly[1] - poly[0];
                const qreal ang = std::atan2(side.y(), side.x()), ca = std::cos(ang), sa = std::sin(ang);
                qreal w = 0, h = 0;
                for (const QPointF& p : poly) {
                    const QPointF d = p - c;
                    w = std::max(w, std::abs(d.x() * ca + d.y() * sa));
                    h = std::max(h, std::abs(-d.x() * sa + d.y() * ca));
                }
                // Casi recto: del todo (horizontal).
                const qreal a = std::abs(std::remainder(ang, M_PI / 2)) < 0.12 ? std::round(ang / (M_PI / 2)) * (M_PI / 2) : ang;
                const qreal c2 = std::cos(a), s2 = std::sin(a);
                poly.clear();
                for (const QPointF& k : {QPointF(-w, -h), QPointF(w, -h), QPointF(w, h), QPointF(-w, h)})
                    poly << c + QPointF(k.x() * c2 - k.y() * s2, k.x() * s2 + k.y() * c2);
            }
        }
        r.points = poly;
        r.points << poly.first();
        return r;
    }
    if (fit.error < 0.2 && fit.a >= 3 && fit.b >= 3) {
        r.kind = Kind::Ellipse;
        r.points = ellipsePoints(fit);
    }
    return r;
}

QString name(Kind kind)
{
    switch (kind) {
    case Kind::Line: return QCoreApplication::translate("QuickShape", "Línea");
    case Kind::Ellipse: return QCoreApplication::translate("QuickShape", "Elipse");
    case Kind::Circle: return QCoreApplication::translate("QuickShape", "Círculo");
    case Kind::Rectangle: return QCoreApplication::translate("QuickShape", "Rectángulo");
    case Kind::Triangle: return QCoreApplication::translate("QuickShape", "Triángulo");
    case Kind::Polygon: return QCoreApplication::translate("QuickShape", "Polígono");
    case Kind::None: break;
    }
    return {};
}

} // namespace QuickShape
