#include "app/tools/TransformBox.h"

#include "core/Motion.h"

#include "app/tools/Handles.h"

#include <QLineF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Distancia (en pantalla) de un punto al contorno del cuadro.
qreal distanceToOutline(const QPolygonF& poly, const QPointF& p)
{
    qreal best = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i + 1 < poly.size(); ++i) {
        const QPointF a = poly[i], b = poly[i + 1];
        const QPointF ab = b - a;
        const qreal len2 = QPointF::dotProduct(ab, ab);
        const qreal t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
        best = std::min(best, QLineF(p, a + ab * t).length());
    }
    return best;
}

// Tirador al que equivale un punto cerca del contorno (a menos de kEdgeBand): el del lado más
// cercano (1 arriba, 3 derecha, 5 abajo, 7 izquierda) o, cerca de sus extremos, el de la esquina;
// -1 si está lejos. 'poly' son las esquinas en pantalla en el orden de QPolygonF(QRectF).
int edgeHandle(const QPolygonF& poly, const QPointF& p)
{
    constexpr qreal kCornerBand = 10;
    int best = -1;
    qreal bestDist = TransformBox::kEdgeBand;
    for (int i = 0; i < 4 && i + 1 < poly.size(); ++i) {
        const QPointF a = poly[i], b = poly[i + 1];
        const QPointF ab = b - a;
        const qreal len = std::sqrt(QPointF::dotProduct(ab, ab));
        const qreal t = len > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / (len * len), 0.0, 1.0) : 0.0;
        const qreal d = QLineF(p, a + ab * t).length();
        if (d > bestDist)
            continue;
        bestDist = d;
        // Esquinas: 0 sup-izq, 2 sup-der, 4 inf-der, 6 inf-izq (el lado i va de la 2i a la 2i+2).
        if (t * len < kCornerBand)
            best = (2 * i) % 8;
        else if ((1 - t) * len < kCornerBand)
            best = (2 * i + 2) % 8;
        else
            best = 2 * i + 1;
    }
    return best;
}

} // namespace

void TransformBox::set(const QRectF& localRect, const QTransform& toCanvas)
{
    if (localRect != m_rect)
        m_localPivot = localRect.center();
    m_rect = localRect;
    m_transform = toCanvas;
}

QTransform TransformBox::snappedToPixels(const QTransform& t, const QRectF& local)
{
    if (local.width() <= 0 || local.height() <= 0)
        return t;
    if (std::abs(t.m12()) > 1e-9 || std::abs(t.m21()) > 1e-9) {
        // Girada 90° o 270°: el rectángulo sigue recto y se lleva entero a píxeles enteros.
        if (std::abs(t.m11()) < 1e-9 && std::abs(t.m22()) < 1e-9) {
            const QPointF corner = t.mapRect(local).topLeft();
            return t * QTransform::fromTranslate(std::round(corner.x()) - corner.x(), std::round(corner.y()) - corner.y());
        }
        // Con cualquier otro giro la imagen se remuestrea igualmente: redondear el desplazamiento
        // solo hacía que temblara alrededor del punto de giro a cada paso.
        return t;
    }
    const QPointF a = t.map(local.topLeft()), b = t.map(local.bottomRight());
    auto axis = [](double from, double to) {
        double x0 = std::round(from), x1 = std::round(to);
        if (x0 == x1)
            x1 = x0 + (to >= from ? 1 : -1);
        return std::pair{x0, x1};
    };
    const auto [x0, x1] = axis(a.x(), b.x());
    const auto [y0, y1] = axis(a.y(), b.y());
    const double sx = (x1 - x0) / local.width(), sy = (y1 - y0) / local.height();
    return QTransform(sx, 0, 0, sy, x0 - sx * local.left(), y0 - sy * local.top());
}

void TransformBox::resetPivot()
{
    m_localPivot = m_rect.center();
}

bool TransformBox::pivotMoved() const
{
    return QLineF(m_localPivot, m_rect.center()).length() > 0.5;
}

QPointF TransformBox::resetButtonView(const QTransform& imageToView) const
{
    // Junto al punto de giro, del lado del centro al que vuelve: movido a la izquierda, a su derecha
    // (y al revés), siguiéndolo al moverlo.
    const QTransform toView = m_transform * imageToView;
    const QPointF pivot = toView.map(m_localPivot), center = toView.map(m_rect.center());
    const QLineF toCenter(pivot, center);
    if (toCenter.length() < 1)
        return pivot + QPointF(18, -18);
    return pivot + (center - pivot) * (26.0 / toCenter.length());
}

QPolygonF TransformBox::canvasCorners() const
{
    return m_transform.map(QPolygonF(m_rect));
}

TransformBox::Mode TransformBox::hit(const QPointF& viewPos, const QTransform& imageToView, bool rightButton, int* handle,
                                     bool* inside) const
{
    const QTransform toView = m_transform * imageToView;
    const QPolygonF poly = toView.map(QPolygonF(m_rect));
    const bool in = poly.containsPoint(viewPos, Qt::OddEvenFill);
    if (inside)
        *inside = in;
    if (rightButton)
        return Mode::Rotate;
    if (pivotMoved() && Handles::hit(viewPos, resetButtonView(imageToView), 9))
        return Mode::ResetPivot;
    if (Handles::hit(viewPos, toView.map(m_localPivot)))
        return Mode::Pivot;
    for (int i = 0; i < 8; ++i) {
        if (Handles::hit(viewPos, toView.map(Handles::boxHandle(m_rect, i)))) {
            if (handle)
                *handle = i;
            return m_3d ? Mode::Tilt : Mode::Scale;
        }
    }
    // El icono de mover (abajo a la derecha, fuera del cuadro) mueve, como en Paint.NET: así se
    // puede arrastrar una selección muy pequeña.
    if (Handles::hit(viewPos, Handles::moveIconPos(poly.boundingRect()), 10))
        return Mode::Translate;
    // En 3D, lo de dentro (y cerca del borde) inclina la lámina.
    if (m_3d) {
        if (in || edgeHandle(poly, viewPos) >= 0)
            return Mode::Tilt;
        return distanceToOutline(poly, viewPos) <= kRotateBand ? Mode::Rotate : Mode::None;
    }
    // Cerca del borde, por dentro o por fuera, la mano estira hacia ese lado, como en Paint.NET: los
    // lados de arriba y abajo en vertical, los laterales en horizontal y cerca de una esquina en
    // los dos ejes. Por dentro solo si el cuadro es lo bastante grande para poder moverlo aún.
    const QRectF bounds = poly.boundingRect();
    const bool roomInside = std::min(bounds.width(), bounds.height()) > 4 * kEdgeBand;
    if (const int edge = edgeHandle(poly, viewPos); edge >= 0 && (!in || roomInside)) {
        if (handle)
            *handle = edge;
        return Mode::Scale;
    }
    if (!in && distanceToOutline(poly, viewPos) <= kRotateBand)
        return Mode::Rotate;
    return Mode::Translate;
}

void TransformBox::begin(Mode mode, int handle, const QPointF& canvasPos)
{
    m_mode = mode;
    m_handle = handle;
    m_start = m_transform;
    m_startPos = canvasPos;
    m_pivot = m_transform.map(m_localPivot);
}

QTransform TransformBox::tiltTransform(const QRectF& rect, double ax, double ay)
{
    // (La misma que el giro 3D del editor de GIF.)
    return Motion::tiltTransform(rect, ax, ay);
}

double TransformBox::rotationDegrees() const
{
    return std::atan2(m_transform.m12(), m_transform.m11()) * 180.0 / M_PI;
}

QTransform TransformBox::update(const QPointF& pos, Qt::KeyboardModifiers mods)
{
    switch (m_mode) {
    case Mode::None:
    case Mode::ResetPivot:
        break;
    case Mode::Pivot: {
        bool ok = false;
        const QTransform inv = m_transform.inverted(&ok);
        if (ok)
            m_localPivot = inv.map(pos);
        break;
    }
    case Mode::Translate: {
        QPointF d = pos - m_startPos;
        d = QPointF(std::round(d.x()), std::round(d.y()));
        m_snapLines.clear();
        if (m_snapTol > 0 && !(mods & Qt::ControlModifier)) {
            // En cada eje, el borde o el centro más cerca de una línea (a la tolerancia o menos) se pega a ella.
            const QRectF r = m_start.mapRect(m_rect);
            auto snapAxis = [this](const QList<SnapTarget>& targets, double a0, double a1, double delta) {
                const double edges[3] = {a0, a1, (a0 + a1) / 2};
                double best = 0;
                bool found = false;
                for (double e : edges)
                    for (const SnapTarget& t : targets) {
                        const double diff = t.pos - (e + delta);
                        if (std::abs(diff) <= m_snapTol && (!found || std::abs(diff) < std::abs(best))) {
                            best = diff;
                            found = true;
                        }
                    }
                return found ? std::round(delta + best) : delta;
            };
            d.setX(snapAxis(m_snapX, r.left(), r.right(), d.x()));
            d.setY(snapAxis(m_snapY, r.top(), r.bottom(), d.y()));
            // Las alineaciones que quedan: cada borde o centro sobre una línea.
            const QRectF moved = r.translated(d);
            auto lines = [this, &moved](const QList<SnapTarget>& targets, bool vertical) {
                const double a0 = vertical ? moved.left() : moved.top(), a1 = vertical ? moved.right() : moved.bottom();
                const double o0 = vertical ? moved.top() : moved.left(), o1 = vertical ? moved.bottom() : moved.right();
                for (double e : {a0, a1, (a0 + a1) / 2})
                    for (const SnapTarget& t : targets)
                        if (std::abs(t.pos - e) < 0.5)
                            m_snapLines.append({vertical, t.pos, std::min(t.from, o0), std::max(t.to, o1)});
            };
            lines(m_snapX, true);
            lines(m_snapY, false);
        }
        m_transform = m_start * QTransform::fromTranslate(d.x(), d.y());
        break;
    }
    case Mode::Tilt: {
        // Hacia donde va el puntero: a los lados gira alrededor del eje vertical y arriba y abajo,
        // del horizontal (todo el ancho o el alto del cuadro, 180°). Con Mayús, solo en un eje.
        const QRectF bounds = m_start.mapRect(m_rect);
        const QPointF d = pos - m_startPos;
        double ay = std::clamp(d.x() / std::max(1.0, bounds.width()) * 180.0, -85.0, 85.0);
        double ax = std::clamp(-d.y() / std::max(1.0, bounds.height()) * 180.0, -85.0, 85.0);
        if (mods & Qt::ShiftModifier)
            (std::abs(d.x()) > std::abs(d.y()) ? ax : ay) = 0;
        m_transform = tiltTransform(m_rect, ax, ay) * m_start;
        break;
    }
    case Mode::Rotate: {
        const QPointF a = m_startPos - m_pivot, b = pos - m_pivot;
        double delta = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180.0 / M_PI;
        if (mods & Qt::ShiftModifier) {
            const double start = std::atan2(m_start.m12(), m_start.m11()) * 180.0 / M_PI;
            delta = std::round((start + delta) / 15.0) * 15.0 - start;
        }
        QTransform r;
        r.translate(m_pivot.x(), m_pivot.y());
        r.rotate(delta);
        r.translate(-m_pivot.x(), -m_pivot.y());
        m_transform = m_start * r;
        break;
    }
    case Mode::Scale: {
        // Deformar: la esquina va sola donde esté el puntero y la imagen queda en perspectiva.
        if (m_warp && Handles::isCorner(m_handle)) {
            QPolygonF quad = m_start.map(QPolygonF(m_rect));
            quad.resize(4);
            quad[m_handle / 2] = pos;
            QPolygonF from(m_rect);
            from.resize(4);
            QTransform warped;
            if (QTransform::quadToQuad(from, quad, warped))
                m_transform = warped;
            break;
        }
        bool ok = false;
        const QTransform inv = m_start.inverted(&ok);
        if (!ok)
            break;
        const QPointF l = inv.map(pos);
        const QPointF h = Handles::boxHandle(m_rect, m_handle);
        const QPointF anchor = (mods & Qt::AltModifier) ? m_rect.center() : Handles::oppositeHandle(m_rect, m_handle);
        const QPointF axes = Handles::handleAxes(m_handle);
        double sx = 1, sy = 1;
        if (axes.x() != 0 && std::abs(h.x() - anchor.x()) > 1e-9)
            sx = (l.x() - anchor.x()) / (h.x() - anchor.x());
        if (axes.y() != 0 && std::abs(h.y() - anchor.y()) > 1e-9)
            sy = (l.y() - anchor.y()) / (h.y() - anchor.y());
        if (mods & Qt::ShiftModifier) {
            const double s = Handles::isCorner(m_handle) ? std::max(std::abs(sx), std::abs(sy))
                                                         : (axes.x() != 0 ? std::abs(sx) : std::abs(sy));
            sx = (axes.x() != 0 && sx < 0 ? -1 : 1) * s;
            sy = (axes.y() != 0 && sy < 0 ? -1 : 1) * s;
        }
        if (std::abs(sx) < 1e-3)
            sx = sx < 0 ? -1e-3 : 1e-3;
        if (std::abs(sy) < 1e-3)
            sy = sy < 0 ? -1e-3 : 1e-3;
        const QTransform delta = QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(sx, sy)
                               * QTransform::fromTranslate(anchor.x(), anchor.y());
        m_transform = delta * m_start;
        break;
    }
    }
    return m_transform;
}

void TransformBox::draw(QPainter& p, const QTransform& imageToView, int parts) const
{
    const QTransform toView = m_transform * imageToView;
    const QPolygonF poly = toView.map(QPolygonF(m_rect));
    if (parts & Outline)
        Handles::drawBox(p, poly);
    // Los tiradores laten como en Paint.NET (Handles::pulse), cada uno con su desfase.
    p.save();
    if (parts & BoxHandles)
        for (int i = 0; i < 8; ++i) {
            p.setOpacity(Handles::pulse(i + 1));
            Handles::draw(p, toView.map(Handles::boxHandle(m_rect, i)), Handles::Shape::Circle);
        }
    if (parts & PivotPoint) {
        p.setOpacity(Handles::pulse(0));
        Handles::drawPivot(p, toView.map(m_localPivot), rotationDegrees());
        p.setOpacity(1.0);
        if (pivotMoved()) {
            // Gira hacia el centro al que vuelve y, además, según lo lejos que quedó (se nota al
            // arrastrar el punto).
            const QLineF toCenter(toView.map(m_localPivot), toView.map(m_rect.center()));
            Handles::draw(p, resetButtonView(imageToView), Handles::Shape::ResetPivot, m_resetHot,
                          -toCenter.angle() + toCenter.length() * 1.5);
        }
    }
    if (parts & MoveIcon) {
        p.setOpacity(Handles::pulse(9));
        Handles::draw(p, Handles::moveIconPos(poly.boundingRect()), Handles::Shape::Move);
    }
    p.restore();
}
