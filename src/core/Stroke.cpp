#include "core/Stroke.h"

#include "core/Commands.h"
#include "core/FloodFill.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QLineF>

#include <cmath>
#include <cstring>

namespace {

double linearToSrgb(double v)
{
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
}

// e^-x · I0(x), la función de Bessel modificada de orden 0 escalada (Abramowitz y Stegun 9.8.1 y
// 9.8.2): sin escalar se desborda con los desenfoques finos.
double besselI0Scaled(double x)
{
    if (x < 3.75) {
        const double t = (x / 3.75) * (x / 3.75);
        const double i0 = 1 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (0.0360768 + t * 0.0045813)))));
        return i0 * std::exp(-x);
    }
    const double t = 3.75 / x;
    return (0.39894228
            + t * (0.01328592
                   + t * (0.00225319 + t * (-0.00157565 + t * (0.00916281 + t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377))))))))
         / std::sqrt(x);
}

// Cobertura a la distancia 'u' del centro de un disco de radio 'edge' desenfocado con una
// gaussiana de desviación 'sigma': la integral en anillos de la gaussiana desplazada (con la
// función de Bessel), por Simpson.
double blurredDisc(double u, double edge, double sigma)
{
    const double from = std::max(0.0, u - 7 * sigma), to = std::min(edge, u + 7 * sigma);
    if (to <= from)
        return 0;
    constexpr int kSteps = 96;
    const double h = (to - from) / kSteps, s2 = sigma * sigma;
    auto f = [&](double r) { return r / s2 * std::exp(-(r - u) * (r - u) / (2 * s2)) * besselI0Scaled(r * u / s2); };
    double sum = f(from) + f(to);
    for (int i = 1; i < kSteps; ++i)
        sum += f(from + i * h) * (i % 2 ? 4 : 2);
    return std::clamp(sum * h / 3, 0.0, 1.0);
}

} // namespace

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
    m_points.clear();
    m_drySegments = 0;
    m_walker = Walker();
    m_wetRect = QRect();
    m_wetBackup = QImage();
    m_recolorTarget = params.background.rgba();
    m_stabilized = false;
    m_strokeLength = -1;
    m_speedPressure = 1;
    m_speedStarted = false;
    buildProfile();
}

void StrokeEngine::buildProfile()
{
    // Como el de Paint.NET: con la dureza crece el disco (de un 59 % del radio al radio entero) y
    // se afina el desenfoque (de un tercio del radio a nada). El desenfoque va en luz lineal, así
    // el borde suave no se ve más gordo de lo que es. En radios del pincel: vale para cualquier
    // tamaño (también con la presión).
    const double h = std::clamp(m_params.hardness, 0.0, 1.0);
    m_edge = 0.594375 + 0.405625 * h;
    m_sigma = (1 - h) / 3;
    const bool hard = m_sigma < 0.004;
    m_extent = m_edge + (hard ? 0.0 : 3.5 * m_sigma) + 0.01;
    constexpr int kEntries = 1024;
    m_profile.assign(kEntries + 2, 0.f);
    for (int i = 0; i <= kEntries; ++i) {
        const double u = i * m_extent / kEntries;
        const double ink = hard ? (u <= m_edge ? 1.0 : 0.0) : blurredDisc(u, m_edge, m_sigma);
        m_profile[size_t(i)] = float(std::clamp(1 - linearToSrgb(1 - ink), 0.0, 1.0));
    }
}

float StrokeEngine::profile(qreal u) const
{
    if (u >= m_extent)
        return 0;
    const qreal f = u / m_extent * (m_profile.size() - 2);
    const int i = int(f);
    const float t = float(f - i);
    return m_profile[size_t(i)] + (m_profile[size_t(i) + 1] - m_profile[size_t(i)]) * t;
}

qreal StrokeEngine::diameterFor(qreal pressure) const
{
    qreal d = m_params.diameter;
    if (m_params.pressureSize) {
        if (m_params.advanced) {
            // Con la curva de la presión y el tamaño mínimo elegidos.
            const qreal p = std::pow(std::clamp(pressure, 0.0, 1.0), std::clamp(m_params.pressureGamma, 0.2, 5.0));
            const qreal lo = std::clamp(m_params.minSize, 0.01, 1.0);
            d *= lo + (1 - lo) * p;
        } else {
            d *= std::clamp(pressure, 0.05, 1.0);
        }
    }
    return std::max(d, 0.1);
}

qreal StrokeEngine::spacingFor(qreal diameter) const
{
    // Como Paint.NET: una fracción del diámetro, nunca menos de un píxel.
    return std::max(1.0, std::max(1.0, diameter) * std::max(0.01, m_params.spacing));
}

void StrokeEngine::moveTo(QPointF pos, qreal pressure)
{
    if (!m_doc)
        return;
    m_last = pos;
    const QPoint px(int(std::floor(pos.x())), int(std::floor(pos.y())));
    if (m_params.kind == StrokeKind::Recolor && m_params.recolorSampleOnce && m_before.rect().contains(px))
        m_recolorTarget = m_before.pixel(px);
    if (m_params.pixelExact) {
        composite(pixelLine(px, px));
        return;
    }
    m_points = {Point{pos, pressure}};
    m_drySegments = 0;
    m_walker = Walker();
    composite(walk(m_walker, m_points));
}

qreal StrokeEngine::taperFactor(qreal distance) const
{
    if (!m_params.advanced)
        return 1;
    qreal f = 1;
    if (m_params.taperIn > 0)
        f = std::min(f, 0.08 + 0.92 * std::clamp(distance / m_params.taperIn, 0.0, 1.0));
    if (m_params.taperOut > 0 && m_strokeLength >= 0)
        f = std::min(f, 0.08 + 0.92 * std::clamp((m_strokeLength - distance) / m_params.taperOut, 0.0, 1.0));
    return f;
}

void StrokeEngine::lineTo(QPointF pos, qreal pressure)
{
    if (!m_doc)
        return;
    // Presión por la velocidad (ratón o dedo, que dan siempre 1): los tramos largos entre dos puntos
    // (movimiento rápido) adelgazan el trazo, suavizado para que no salte.
    if (m_params.advanced && m_params.speedPressure && pressure >= 0.999) {
        const qreal step = QLineF(m_last, pos).length();
        const qreal target = std::clamp(1.15 - step / 45.0, 0.25, 1.0);
        m_speedPressure = m_speedStarted ? m_speedPressure + (target - m_speedPressure) * 0.3 : target;
        m_speedStarted = true;
        pressure = m_speedPressure;
    }
    // Estabilizador (como la corrección de FireAlpaca): el trazo sigue al puntero con retraso; cuanto
    // más alto, más suave y más atrás se queda. Al terminar llega hasta el último punto.
    if (m_params.advanced && m_params.stabilizer > 0 && !m_params.pixelExact) {
        if (!m_stabilized) {
            m_stabilized = true;
            m_stable = m_last;
            m_stablePressure = pressure;
        }
        m_raw = pos;
        m_rawPressure = pressure;
        const qreal k = 1.0 / (1.0 + m_params.stabilizer * 0.6);
        m_stable += (pos - m_stable) * k;
        m_stablePressure += (pressure - m_stablePressure) * k;
        lineToPoint(m_stable, m_stablePressure);
        return;
    }
    lineToPoint(pos, pressure);
}

void StrokeEngine::lineToPoint(QPointF pos, qreal pressure)
{
    if (m_params.pixelExact) {
        const QPoint a(int(std::floor(m_last.x())), int(std::floor(m_last.y())));
        const QPoint b(int(std::floor(pos.x())), int(std::floor(pos.y())));
        m_last = pos;
        composite(pixelLine(a, b));
        return;
    }
    m_last = pos;
    if (m_points.isEmpty() || m_points.last().pos == pos)
        return;
    m_points.append(Point{pos, pressure});
    QRect dirty = restoreWet();
    // Sin suavizar, cada tramo es recto y definitivo. Suavizado, un tramo es definitivo cuando ya
    // se conoce el punto que lo sigue (la curva lo necesita); el último se dibuja provisional.
    const int last = int(m_points.size()) - (m_params.smoothing ? 3 : 2);
    QList<Point> path;
    while (m_drySegments <= last) {
        path.clear();
        segment(m_drySegments++, path);
        dirty |= walk(m_walker, path);
    }
    if (m_params.smoothing)
        dirty |= drawWet();
    composite(dirty);
}

void StrokeEngine::segment(int i, QList<Point>& out) const
{
    const qsizetype n = m_points.size();
    const Point& a = m_points[i];
    const Point& b = m_points[i + 1];
    if (!m_params.smoothing) {
        out.append(b);
        return;
    }
    // Catmull-Rom centrípeta por los puntos del ratón; en los extremos, el punto reflejado.
    const QPointF p0 = i > 0 ? m_points[i - 1].pos : 2 * a.pos - b.pos;
    const QPointF p3 = i + 2 < n ? m_points[i + 2].pos : 2 * b.pos - a.pos;
    const QPointF p1 = a.pos, p2 = b.pos;
    auto knot = [](const QPointF& u, const QPointF& v) { return std::max(1e-4, std::sqrt(QLineF(u, v).length())); };
    const qreal t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    const int steps = std::max(1, int(std::ceil(QLineF(p1, p2).length())));
    for (int j = 1; j <= steps; ++j) {
        const qreal t = t1 + (t2 - t1) * j / steps;
        const QPointF a1 = (t1 - t) / t1 * p0 + t / t1 * p1;
        const QPointF a2 = (t2 - t) / (t2 - t1) * p1 + (t - t1) / (t2 - t1) * p2;
        const QPointF a3 = (t3 - t) / (t3 - t2) * p2 + (t - t2) / (t3 - t2) * p3;
        const QPointF b1 = (t2 - t) / t2 * a1 + t / t2 * a2;
        const QPointF b2 = (t3 - t) / (t3 - t1) * a2 + (t - t1) / (t3 - t1) * a3;
        const QPointF c = (t2 - t) / (t2 - t1) * b1 + (t - t1) / (t2 - t1) * b2;
        out.append(Point{c, a.pressure + (b.pressure - a.pressure) * j / steps});
    }
}

QRect StrokeEngine::walk(Walker& w, const QList<Point>& path)
{
    // El primer punto lleva un toque; los demás, separados lo que pida el diámetro en cada sitio.
    QRect dirty;
    for (const Point& p : path) {
        if (!w.started) {
            w.started = true;
            w.last = p;
            w.distance = 0;
            const qreal d = diameterFor(p.pressure) * taperFactor(0);
            m_dabPressure = p.pressure;
            dirty |= dab(p.pos, d);
            w.next = spacingFor(d);
            continue;
        }
        const qreal len = QLineF(w.last.pos, p.pos).length();
        qreal at = w.next;
        while (at <= len) {
            const qreal t = len > 0 ? at / len : 1.0;
            m_dabPressure = w.last.pressure + (p.pressure - w.last.pressure) * t;
            const qreal d = diameterFor(m_dabPressure) * taperFactor(w.distance + at);
            dirty |= dab(w.last.pos + (p.pos - w.last.pos) * t, d);
            at += spacingFor(d);
        }
        w.next = at - len;
        w.distance += len;
        w.last = p;
    }
    return dirty;
}

QRect StrokeEngine::reach(const QList<Point>& path) const
{
    QRectF r;
    qreal margin = m_params.diameter / 2 * m_extent + 2;
    // Con la punta cuadrada y la dispersión, los toques llegan más lejos.
    if (m_params.advanced)
        margin = m_params.diameter / 2 * m_extent * (m_params.tipShape == 1 ? M_SQRT2 : 1.0) + m_params.scatter * m_params.diameter + 2;
    for (const Point& p : path)
        r |= QRectF(p.pos.x() - margin, p.pos.y() - margin, 2 * margin, 2 * margin);
    return r.toAlignedRect().intersected(m_mask.rect());
}

QRect StrokeEngine::restoreWet()
{
    // Quita el tramo provisional (deja la máscara como estaba antes de dibujarlo).
    const QRect r = m_wetRect;
    if (!r.isEmpty()) {
        for (int y = 0; y < r.height(); ++y)
            std::memcpy(m_mask.scanLine(r.top() + y) + r.left(), m_wetBackup.constScanLine(y), size_t(r.width()));
    }
    m_wetRect = QRect();
    return r;
}

QRect StrokeEngine::drawWet()
{
    // El último tramo, hasta el punto actual: se guarda lo que había debajo para quitarlo cuando
    // llegue el siguiente punto y el tramo sea definitivo.
    const int i = int(m_points.size()) - 2;
    if (i < m_drySegments || i < 0)
        return {};
    QList<Point> path;
    segment(i, path);
    QList<Point> area = path;
    area.prepend(m_walker.last);
    m_wetRect = reach(area);
    if (m_wetRect.isEmpty())
        return {};
    m_wetBackup = m_mask.copy(m_wetRect);
    Walker w = m_walker;
    walk(w, path);
    return m_wetRect;
}

QRect StrokeEngine::dab(QPointF c, qreal diameter)
{
    if (m_params.advanced && m_params.antialias)
        return dabAdvanced(c, diameter);
    // Como Paint.NET: por debajo de un píxel, un toque de un píxel más transparente.
    qreal opacity = 1;
    if (diameter < 1) {
        opacity = diameter * diameter;
        diameter = 1;
    }
    const qreal radius = diameter / 2;
    const bool aa = m_params.antialias;
    const qreal extent = aa ? radius * m_extent : radius;
    const QRect bounds = QRectF(c.x() - extent - 1, c.y() - extent - 1, extent * 2 + 2, extent * 2 + 2)
                             .toAlignedRect()
                             .intersected(m_mask.rect());
    if (bounds.isEmpty())
        return {};
    auto put = [](uchar& m, int s) {
        // "Encima": lo nuevo más lo que queda de lo de antes.
        if (s > 0)
            m = uchar(s + ((255 - s) * m + 127) / 255);
    };
    if (!aa) {
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
            uchar* row = m_mask.scanLine(y);
            const qreal dy = y + 0.5 - c.y();
            for (int x = bounds.left(); x <= bounds.right(); ++x) {
                const qreal dx = x + 0.5 - c.x();
                if (dx * dx + dy * dy <= radius * radius)
                    row[x] = 255;
            }
        }
        const QPoint px(int(std::floor(c.x())), int(std::floor(c.y())));
        if (m_mask.rect().contains(px))
            m_mask.scanLine(px.y())[px.x()] = 255;
        return bounds;
    }
    // Los píxeles del borde (con un pincel duro o pequeño, donde la cobertura cambia mucho dentro
    // del píxel) se muestrean 4×4 veces; los demás, en su centro.
    const qreal edgePx = m_edge * radius, sigmaPx = m_sigma * radius;
    const bool small = radius < 3;
    const float k = float(opacity * 255);
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        uchar* row = m_mask.scanLine(y);
        const qreal dy = y + 0.5 - c.y();
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const qreal dx = x + 0.5 - c.x();
            const qreal d = std::sqrt(dx * dx + dy * dy);
            if (d >= extent + 1)
                continue;
            float v;
            if (small || (sigmaPx < 1 && std::abs(d - edgePx) < 1)) {
                v = 0;
                for (int j = 0; j < 4; ++j)
                    for (int i = 0; i < 4; ++i) {
                        const qreal sx = dx + (i - 1.5) / 4, sy = dy + (j - 1.5) / 4;
                        v += profile(std::sqrt(sx * sx + sy * sy) / radius);
                    }
                v /= 16;
            } else {
                v = profile(d / radius);
            }
            put(row[x], int(v * k + 0.5f));
        }
    }
    return bounds;
}

namespace {
// Un número al azar (0..1) fijo para cada toque y cada uso: el tramo provisional se vuelve a
// dibujar al llegar el siguiente punto y debe salir igual.
qreal stableRandom(QPointF c, int salt)
{
    quint32 h = quint32(qRound(c.x() * 8)) * 73856093u ^ quint32(qRound(c.y() * 8)) * 19349663u ^ quint32(salt) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xffffff) / double(0xffffff);
}
}  // namespace

QRect StrokeEngine::dabAdvanced(QPointF c, qreal diameter)
{
    // El toque con las opciones avanzadas: variación al azar de tamaño, opacidad, ángulo y sitio;
    // punta redonda o cuadrada, girada y aplastada; flujo y opacidad por presión.
    const QPointF key = c;
    diameter *= 1 - std::clamp(m_params.sizeJitter, 0.0, 1.0) * stableRandom(key, 1);
    if (m_params.scatter > 0) {
        const qreal a = stableRandom(key, 2) * 2 * M_PI;
        const qreal dist = stableRandom(key, 3) * m_params.scatter * diameter;
        c += QPointF(std::cos(a) * dist, std::sin(a) * dist);
    }
    qreal opacity = std::clamp(m_params.flow, 0.01, 1.0) * (1 - std::clamp(m_params.opacityJitter, 0.0, 1.0) * stableRandom(key, 4));
    if (m_params.pressureOpacity)
        opacity *= std::clamp(m_dabPressure, 0.0, 1.0);
    if (diameter < 1) {
        opacity *= diameter * diameter;
        diameter = 1;
    }
    const qreal angle = (m_params.angle + std::clamp(m_params.angleJitter, 0.0, 1.0) * 180 * (stableRandom(key, 5) * 2 - 1)) * M_PI / 180;
    const qreal roundness = std::clamp(m_params.roundness, 0.05, 1.0);
    const bool square = m_params.tipShape == 1;
    const qreal radius = diameter / 2;
    // Lo que puede abarcar (la diagonal de la punta cuadrada).
    const qreal extent = radius * m_extent * (square ? M_SQRT2 : 1.0);
    const QRect bounds = QRectF(c.x() - extent - 1, c.y() - extent - 1, extent * 2 + 2, extent * 2 + 2).toAlignedRect().intersected(m_mask.rect());
    if (bounds.isEmpty())
        return {};
    const qreal ca = std::cos(angle), sa = std::sin(angle);
    const float k = float(opacity * 255);
    const int samples = radius < 3 ? 4 : 1;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        uchar* row = m_mask.scanLine(y);
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            float v = 0;
            for (int j = 0; j < samples; ++j)
                for (int i = 0; i < samples; ++i) {
                    const qreal dx = x + (i + 0.5) / samples - c.x(), dy = y + (j + 0.5) / samples - c.y();
                    // En los ejes de la punta: girada y aplastada.
                    const qreal u = (dx * ca + dy * sa) / radius;
                    const qreal w = (-dx * sa + dy * ca) / (radius * roundness);
                    const qreal d = square ? std::max(std::abs(u), std::abs(w)) : std::sqrt(u * u + w * w);
                    v += profile(d);
                }
            v /= samples * samples;
            const int s = int(v * k + 0.5f);
            if (s > 0)
                row[x] = uchar(s + ((255 - s) * row[x] + 127) / 255);
        }
    }
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
    // Con los dos extremos incluidos en cualquier sentido (QRect::normalized() los dejaba
    // fuera al trazar hacia la izquierda o hacia arriba).
    return QRect(QPoint(std::min(a.x(), b.x()), std::min(a.y(), b.y())), QPoint(std::max(a.x(), b.x()), std::max(a.y(), b.y())))
        .intersected(m_mask.rect());
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
    case StrokeKind::Mask:
        // La capa no cambia: la herramienta enseña la cobertura a su manera.
        return;
    case StrokeKind::Erase: {
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
        Raster::compositeRegion(img, m_before, source, r, m_params.blend, Selection(), cov);
        break;
    }
    }
    // Alpha-Lock, también mientras se pinta (no solo al terminar el trazo).
    if (m_layer->props.alphaLock)
        Raster::preserveAlpha(img, m_before, r);
    m_doc->notifyPixelsChanged(m_layer.get(), r);
}

bool StrokeEngine::end(const QString& text, const QString& icon)
{
    if (!m_doc)
        return false;
    finishPath();
    return commit(text, icon);
}

QRect StrokeEngine::finishPath()
{
    if (!m_doc)
        return {};
    // Con el estabilizador, el trazo acaba donde se soltó el puntero (unos pasos hasta allí).
    if (m_stabilized) {
        m_stabilized = false;
        for (int i = 1; i <= 4; ++i)
            lineToPoint(m_stable + (m_raw - m_stable) * (i / 4.0), m_stablePressure + (m_rawPressure - m_stablePressure) * (i / 4.0));
    }
    if (!m_params.pixelExact) {
        // El tramo provisional pasa a definitivo (con el final de la curva reflejado).
        QRect dirty = restoreWet();
        QList<Point> path;
        while (m_drySegments <= int(m_points.size()) - 2) {
            path.clear();
            segment(m_drySegments++, path);
            dirty |= walk(m_walker, path);
        }
        // Salida (manga): ya se sabe dónde acaba; el trazo se vuelve a dibujar entero, afinándose
        // hacia el final.
        if (m_params.advanced && m_params.taperOut > 0 && m_points.size() > 1) {
            m_strokeLength = m_walker.distance;
            dirty |= m_total;
            m_mask.fill(0);
            Walker w;
            QList<Point> all{m_points.first()};
            for (int i = 0; i + 1 < m_points.size(); ++i)
                segment(i, all);
            dirty |= walk(w, all);
        }
        composite(dirty);
    }
    return m_total;
}

bool StrokeEngine::commit(const QString& text, const QString& icon)
{
    if (!m_doc)
        return false;
    const bool changed = pushPixelPatch(m_doc, m_layer, m_before, m_total, text, icon);
    m_doc = nullptr;
    m_layer.reset();
    m_before = QImage();
    m_mask = QImage();
    m_wetBackup = QImage();
    m_points.clear();
    return changed;
}

void StrokeEngine::cancel()
{
    if (!m_doc)
        return;
    const QRect r = m_total.intersected(m_layer->image.rect());
    if (!r.isEmpty()) {
        Raster::blit(m_layer->image, m_before.copy(r), r.topLeft());
        m_doc->notifyPixelsChanged(m_layer.get(), r);
    }
    m_doc = nullptr;
    m_layer.reset();
    m_before = QImage();
    m_mask = QImage();
    m_wetBackup = QImage();
    m_points.clear();
}

void StrokeEngine::applyThroughCoverage(const QRect& area, const QImage& source)
{
    if (!m_doc)
        return;
    const QRect r = area.intersected(m_layer->image.rect());
    if (r.isEmpty())
        return;
    m_total |= r;
    const bool aa = Raster::selectionAntialias();
    const ConstMask sel(m_doc->selection().mask());
    QImage cov(r.size(), QImage::Format_Alpha8);
    for (int y = 0; y < r.height(); ++y) {
        uchar* row = cov.scanLine(y);
        const uchar* m = m_mask.constScanLine(y + r.top());
        for (int x = 0; x < r.width(); ++x) {
            const int c = m[x + r.left()];
            row[x] = uchar(sel.isNull() ? c : c * Raster::clipCoverage(sel.row(y + r.top())[x + r.left()], aa) / 255);
        }
    }
    const QImage src = source.copy(r.translated(-area.topLeft()));
    Raster::compositeRegion(m_layer->image, m_before, src, r, m_params.blend, Selection(), cov);
    m_doc->notifyPixelsChanged(m_layer.get(), r);
}
