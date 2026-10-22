#include "app/tools/TransformBox.h"

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

} // namespace

void TransformBox::set(const QRectF& localRect, const QTransform& toCanvas)
{
    if (localRect != m_rect)
        m_localPivot = localRect.center();
    m_rect = localRect;
    m_transform = toCanvas;
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
    // Junto al punto central, hacia arriba a la derecha (lo sigue al moverlo).
    return (m_transform * imageToView).map(m_localPivot) + QPointF(18, -18);
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
            return Mode::Scale;
        }
    }
    // Fuera del cuadro pero cerca de su borde se gira, como en Paint.NET; en
    // cualquier otro sitio (dentro, lejos o con el icono de cuatro flechas) se mueve.
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
        // Sin giro ni escala fraccionaria se mueve en píxeles enteros (nítido).
        d = QPointF(std::round(d.x()), std::round(d.y()));
        m_transform = m_start * QTransform::fromTranslate(d.x(), d.y());
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
            // Mayús: conserva la proporción.
            const double s = Handles::isCorner(m_handle) ? std::max(std::abs(sx), std::abs(sy))
                                                         : (axes.x() != 0 ? std::abs(sx) : std::abs(sy));
            sx = (axes.x() != 0 && sx < 0 ? -1 : 1) * s;
            sy = (axes.y() != 0 && sy < 0 ? -1 : 1) * s;
        }
        // Evita escalas nulas que harían la transformación no invertible.
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

void TransformBox::draw(QPainter& p, const QTransform& imageToView, bool handles) const
{
    const QTransform toView = m_transform * imageToView;
    const QPolygonF poly = toView.map(QPolygonF(m_rect));
    Handles::drawBox(p, poly);
    if (!handles)
        return;
    for (int i = 0; i < 8; ++i)
        Handles::draw(p, toView.map(Handles::boxHandle(m_rect, i)), Handles::Shape::Circle);
    // Punto central: la cruz gira con el cuadro (indica la inclinación).
    Handles::drawPivot(p, toView.map(m_localPivot), rotationDegrees());
    if (pivotMoved())
        Handles::draw(p, resetButtonView(imageToView), Handles::Shape::ResetPivot);
    Handles::draw(p, Handles::moveIconPos(poly.boundingRect()), Handles::Shape::Move);
}
