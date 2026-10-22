#include "core/Stroke.h"

#include "core/Commands.h"
#include "core/FloodFill.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QLineF>

#include <cmath>

void StrokeEngine::begin(Document* doc, LayerPtr layer, const StrokeParams& params)
{
    m_doc = doc;
    m_layer = std::move(layer);
    m_params = params;
    m_before = m_layer->image;
    m_layer->image.detach();
    m_mask = QImage(m_layer->image.size(), QImage::Format_Alpha8);
    m_mask.fill(0);
    m_total = QRect();
    m_carry = 0;
    m_recolorTarget = params.background.rgba();
}

qreal StrokeEngine::radiusFor(qreal pressure) const
{
    qreal r = m_params.diameter / 2.0;
    if (m_params.pressureSize)
        r *= std::clamp(pressure, 0.05, 1.0);
    return std::max(r, 0.5);
}

void StrokeEngine::moveTo(QPointF pos, qreal pressure)
{
    if (!m_doc)
        return;
    m_last = m_target = m_raw = pos;
    m_lastPressure = pressure;
    m_carry = 0;
    const QPoint px(int(std::floor(pos.x())), int(std::floor(pos.y())));
    if (m_params.kind == StrokeKind::Recolor && m_params.recolorSampleOnce && m_before.rect().contains(px))
        m_recolorTarget = m_before.pixel(px); // la capa ya tiene alfa directo
    composite(m_params.pixelExact ? pixelLine(px, px) : dab(pos, radiusFor(pressure)));
}

void StrokeEngine::lineTo(QPointF pos, qreal pressure)
{
    if (!m_doc)
        return;
    m_raw = pos;
    if (m_params.smoothing && !m_params.pixelExact) {
        // Filtro exponencial: el trazo sigue al puntero con algo de inercia.
        m_target += (pos - m_target) * 0.4;
        segmentTo(m_target, pressure);
    } else {
        segmentTo(pos, pressure);
    }
}

void StrokeEngine::segmentTo(QPointF pos, qreal pressure)
{
    if (m_params.pixelExact) {
        const QPoint a(int(std::floor(m_last.x())), int(std::floor(m_last.y())));
        const QPoint b(int(std::floor(pos.x())), int(std::floor(pos.y())));
        m_last = pos;
        composite(pixelLine(a, b));
        return;
    }

    const qreal len = QLineF(m_last, pos).length();
    const qreal radius = radiusFor((pressure + m_lastPressure) / 2);
    const qreal spacing = std::max(0.5, 2 * radius * std::max(0.01, m_params.spacing));
    QRect dirty;
    qreal d = spacing - m_carry;
    while (d <= len) {
        const qreal t = len > 0 ? d / len : 1.0;
        const QPointF p = m_last + (pos - m_last) * t;
        dirty |= dab(p, radiusFor(m_lastPressure + (pressure - m_lastPressure) * t));
        d += spacing;
    }
    m_carry = len - (d - spacing);
    m_last = pos;
    m_lastPressure = pressure;
    composite(dirty);
}

QRect StrokeEngine::dab(QPointF c, qreal radius)
{
    const QRect bounds = QRectF(c.x() - radius - 1, c.y() - radius - 1, radius * 2 + 2, radius * 2 + 2)
                             .toAlignedRect()
                             .intersected(m_mask.rect());
    if (bounds.isEmpty())
        return {};

    const qreal inner = radius * std::clamp(m_params.hardness, 0.0, 1.0);
    const bool aa = m_params.antialias;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        uchar* row = m_mask.scanLine(y);
        const qreal dy = y + 0.5 - c.y();
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const qreal dx = x + 0.5 - c.x();
            const qreal d = std::sqrt(dx * dx + dy * dy);
            qreal v;
            if (!aa) {
                v = d <= radius ? 1.0 : 0.0;
            } else {
                v = std::clamp(radius - d + 0.5, 0.0, 1.0);
                if (d > inner && radius > inner) {
                    qreal s = 1.0 - std::clamp((d - inner) / (radius - inner), 0.0, 1.0);
                    s = s * s * (3 - 2 * s);
                    v = std::min(v, s);
                }
            }
            const int m = int(v * 255 + 0.5);
            if (m > row[x])
                row[x] = uchar(m);
        }
    }
    // Un pincel diminuto sin suavizado siempre pinta al menos un píxel.
    const QPoint px(int(std::floor(c.x())), int(std::floor(c.y())));
    if (!aa && m_mask.rect().contains(px))
        m_mask.scanLine(px.y())[px.x()] = 255;
    return bounds;
}

QRect StrokeEngine::pixelLine(QPoint a, QPoint b)
{
    // Bresenham.
    int x0 = a.x(), y0 = a.y();
    const int x1 = b.x(), y1 = b.y();
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (m_mask.rect().contains(x0, y0))
            m_mask.scanLine(y0)[x0] = 255;
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
    return QRect(a, b).normalized().adjusted(0, 0, 1, 1).intersected(m_mask.rect());
}

void StrokeEngine::composite(const QRect& area)
{
    const QRect r = area.intersected(m_layer->image.rect());
    if (r.isEmpty())
        return;
    m_total |= r;

    QImage& img = m_layer->image;
    const bool aa = Raster::selectionAntialias();
    const ConstMask sel(m_doc->selection().mask());
    auto coverage = [&](int x, int y) {
        const int m = m_mask.constScanLine(y)[x];
        return sel.isNull() ? m : m * Raster::clipCoverage(sel.row(y)[x], aa) / 255;
    };

    switch (m_params.kind) {
    case StrokeKind::Erase: {
        // La goma hereda la opacidad del color: con alfa 128 solo quita la mitad.
        const int strength = m_params.color.alpha();
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(m_before.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = r.left(); x <= r.right(); ++x)
                dst[x] = scaleAlpha(src[x], 255 - coverage(x, y) * strength / 255);
        }
        break;
    }

    case StrokeKind::Recolor: {
        const int limit = FloodFill::threshold(m_params.tolerance);
        const QColor c = m_params.color;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(m_before.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = r.left(); x <= r.right(); ++x) {
                const int m = coverage(x, y);
                if (m == 0 || !FloodFill::similar(src[x], m_recolorTarget, limit, m_params.toleranceStraight)) {
                    dst[x] = src[x];
                    continue;
                }
                const QRgb repl = qRgba(c.red(), c.green(), c.blue(), qAlpha(src[x]));
                dst[x] = lerpStraight(src[x], repl, m * c.alpha() / 255);
            }
        }
        break;
    }

    case StrokeKind::Paint:
    case StrokeKind::Clone: {
        QImage source(r.size(), kLayerFormat);
        if (m_params.kind == StrokeKind::Paint) {
            Raster::renderFill(source, r.topLeft(), m_params.fill, m_params.color, m_params.background);
        } else {
            // El tampón copia con la opacidad del color elegido.
            source.fill(kTransparent);
            const QImage& from = m_params.cloneSource.isNull() ? m_before : m_params.cloneSource;
            Raster::blit(source, from, -(r.topLeft() + m_params.cloneOffset));
            const int opacity = m_params.color.alpha();
            if (opacity < 255)
                for (int y = 0; y < source.height(); ++y) {
                    QRgb* row = reinterpret_cast<QRgb*>(source.scanLine(y));
                    for (int x = 0; x < source.width(); ++x)
                        row[x] = scaleAlpha(row[x], opacity);
                }
        }
        QImage cov(r.size(), QImage::Format_Alpha8);
        for (int y = 0; y < r.height(); ++y) {
            uchar* row = cov.scanLine(y);
            for (int x = 0; x < r.width(); ++x)
                row[x] = uchar(coverage(x + r.left(), y + r.top()));
        }
        // La selección ya está incluida en la cobertura.
        Raster::compositeRegion(img, m_before, source, r, m_params.blend, Selection(), cov);
        break;
    }
    }
    m_doc->notifyPixelsChanged(m_layer.get(), r);
}

bool StrokeEngine::end(const QString& text, const QString& icon)
{
    if (!m_doc)
        return false;
    if (m_params.smoothing && !m_params.pixelExact && m_raw != m_last)
        segmentTo(m_raw, m_lastPressure); // el trazo termina donde se soltó el botón
    const bool changed = pushPixelPatch(m_doc, m_layer, m_before, m_total, text, icon);
    m_doc = nullptr;
    m_layer.reset();
    m_before = QImage();
    m_mask = QImage();
    return changed;
}
