#include "core/FloodFill.h"

#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Pixels.h"
#include "core/Raster.h"

#include <QPainter>

#include <cmath>

#include <vector>

namespace FloodFill {

int threshold(qreal tolerance)
{
    const int tol = int(tolerance * tolerance * 256);
    return tol * tol * 4;
}

QImage mask(const QImage& source, QPoint seed, qreal tolerance, bool contiguous, bool straight)
{
    QImage out(source.size(), QImage::Format_Alpha8);
    out.fill(0);
    if (!source.rect().contains(seed))
        return out;

    const ConstPixels src(source);
    const int w = src.w;
    const int h = src.h;
    const int limit = threshold(tolerance);
    const bool pm = isPremultiplied(source);
    const QRgb reference = toStraight(src.at(seed.x(), seed.y()), pm);

    if (!contiguous) {
        uchar* bits = out.bits();
        const qsizetype bpl = out.bytesPerLine();
        parallelRows(0, h, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const QRgb* row = src.row(y);
                uchar* m = bits + y * bpl;
                for (int x = 0; x < w; ++x)
                    m[x] = similar(toStraight(row[x], pm), reference, limit, straight) ? 255 : 0;
            }
        });
        return out;
    }

    // Relleno por líneas: se expande a izquierda y derecha y se siembran las filas vecinas al
    // comienzo de cada tramo compatible.
    auto matches = [&](int x, int y) { return similar(toStraight(src.at(x, y), pm), reference, limit, straight); };
    std::vector<QPoint> stack;
    stack.push_back(seed);
    while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        uchar* row = out.scanLine(p.y());
        if (row[p.x()] || !matches(p.x(), p.y()))
            continue;
        int left = p.x();
        while (left > 0 && !row[left - 1] && matches(left - 1, p.y()))
            --left;
        int right = p.x();
        while (right < w - 1 && !row[right + 1] && matches(right + 1, p.y()))
            ++right;
        for (int x = left; x <= right; ++x)
            row[x] = 255;

        for (int ny : {p.y() - 1, p.y() + 1}) {
            if (ny < 0 || ny >= h)
                continue;
            const uchar* nrow = out.constScanLine(ny);
            bool inRun = false;
            for (int x = left; x <= right; ++x) {
                const bool ok = !nrow[x] && matches(x, ny);
                if (ok && !inRun)
                    stack.emplace_back(x, ny);
                inRun = ok;
            }
        }
    }
    return out;
}

namespace {

// Distancia entre dos colores premultiplicados (también cuenta el alfa).
inline double colorDistance(QRgb a, QRgb b)
{
    const double dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b);
    const double db = qBlue(a) - qBlue(b), da = qAlpha(a) - qAlpha(b);
    return std::sqrt(dr * dr + dg * dg + db * db + da * da);
}

} // namespace

QList<EdgePixel> antialiasEdge(const QImage& sample, const QImage& maskImage)
{
    QList<EdgePixel> out;
    const int w = maskImage.width(), h = maskImage.height();
    if (sample.size() != maskImage.size() || w == 0 || h == 0)
        return out;
    const bool pm = isPremultiplied(sample);
    const ConstPixels src(sample);
    const ConstMask mask(maskImage);
    auto inMask = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && mask.row(y)[x] != 0; };
    auto color = [&](int x, int y) { return pm ? src.at(x, y) : qPremultiply(src.at(x, y)); };

    // Anillo 0: píxeles de la máscara junto al exterior; anillos 1 y 2: por fuera. 'ring'
    // guarda a qué anillo pertenece cada píxel (0xff = ninguno).
    std::vector<uint8_t> ring(size_t(w) * h, 0xff);
    std::vector<QRgb> interiorOf(size_t(w) * h, 0);
    std::vector<int> coverageOf(size_t(w) * h, 0);
    auto idx = [w](int x, int y) { return size_t(y) * w + x; };

    // Color del contorno: el más distinto de la zona en un entorno de 5x5 (fuera de la
    // máscara).
    auto outline = [&](int x, int y, QRgb interior) {
        double best = -1;
        QRgb o = interior;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= w || yy >= h || inMask(xx, yy))
                    continue;
                const QRgb c = color(xx, yy);
                const double d = colorDistance(c, interior);
                if (d > best) {
                    best = d;
                    o = c;
                }
            }
        return o;
    };
    auto estimate = [&](int x, int y, QRgb interior) {
        const QRgb p = color(x, y);
        const double dOutline = colorDistance(outline(x, y, interior), interior);
        if (dOutline < 1.0)
            return 255;
        const double t = 1.0 - colorDistance(p, interior) / dOutline;
        return std::clamp(int(t * 255 + 0.5), 0, 255);
    };

    // Anillo 0: el color de la zona es la media de los vecinos de la máscara.
    const QRect mb = Selection::fromMask(maskImage).bounds();
    QList<QPoint> previous;
    for (int y = mb.top(); y <= mb.bottom(); ++y) {
        const uchar* m = mask.row(y);
        for (int x = mb.left(); x <= mb.right(); ++x) {
            if (!m[x])
                continue;
            bool border = false;
            for (int dy = -1; dy <= 1 && !border; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if ((dx || dy) && x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h && !inMask(x + dx, y + dy)) {
                        border = true;
                        break;
                    }
            if (!border)
                continue;
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (inMask(x + dx, y + dy)) {
                        const QRgb c = color(x + dx, y + dy);
                        r += qRed(c), g += qGreen(c), b += qBlue(c), a += qAlpha(c);
                        ++n;
                    }
            const QRgb average = qRgba(r / n, g / n, b / n, a / n);
            const QRgb edgeColor = outline(x, y, average);
            QRgb interior = average;
            double farthest = -1;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    if (inMask(x + dx, y + dy)) {
                        const QRgb c = color(x + dx, y + dy);
                        const double d = colorDistance(c, edgeColor);
                        if (d > farthest) {
                            farthest = d;
                            interior = c;
                        }
                    }
            ring[idx(x, y)] = 0;
            interiorOf[idx(x, y)] = interior;
            coverageOf[idx(x, y)] = std::max(128, estimate(x, y, interior));
            out.append({QPoint(x, y), coverageOf[idx(x, y)], interior});
            previous.append(QPoint(x, y));
        }
    }

    // Anillos 1 y 2: vecinos de fuera del anillo anterior.
    for (int level = 1; level <= 2; ++level) {
        QList<QPoint> current;
        for (const QPoint& p : std::as_const(previous)) {
            const int parentCoverage = coverageOf[idx(p.x(), p.y())];
            if (level > 1 && parentCoverage <= 0)
                continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = p.x() + dx, y = p.y() + dy;
                    if ((!dx && !dy) || x < 0 || y < 0 || x >= w || y >= h || inMask(x, y))
                        continue;
                    const size_t i = idx(x, y);
                    if (ring[i] != 0xff && ring[i] < level)
                        continue;
                    const QRgb interior = interiorOf[idx(p.x(), p.y())];
                    const int cov = std::min(level == 1 ? 255 : parentCoverage, estimate(x, y, interior));
                    if (ring[i] == level) {
                        if (cov > coverageOf[i]) {
                            coverageOf[i] = cov;
                            interiorOf[i] = interior;
                        }
                        continue;
                    }
                    ring[i] = uint8_t(level);
                    coverageOf[i] = cov;
                    interiorOf[i] = interior;
                    current.append(QPoint(x, y));
                }
        }
        for (const QPoint& p : std::as_const(current)) {
            const size_t i = idx(p.x(), p.y());
            if (coverageOf[i] > 0)
                out.append({p, coverageOf[i], interiorOf[i]});
        }
        previous = current;
    }
    return out;
}

QRgb replaceInterior(QRgb pixel, QRgb interior, QRgb fill, int coverage)
{
    // En premultiplicado: píxel + cobertura · (relleno - zona).
    const QRgb p = qPremultiply(pixel);
    const QRgb f = qPremultiply(fill);
    const double t = std::clamp(coverage, 0, 255) / 255.0;
    auto ch = [&](int pc, int fc, int ic) { return std::clamp(int(std::lround(pc + t * (fc - ic))), 0, 255); };
    const int a = ch(qAlpha(p), qAlpha(f), qAlpha(interior));
    if (a == 0)
        return kTransparent;
    const QRgb result = qRgba(std::min(a, ch(qRed(p), qRed(f), qRed(interior))),
                              std::min(a, ch(qGreen(p), qGreen(f), qGreen(interior))),
                              std::min(a, ch(qBlue(p), qBlue(f), qBlue(interior))), a);
    return qUnpremultiply(result);
}

// Cobertura de cada píxel de fuera de la máscara si esta se ensancha medio píxel con suavizado.
// Se parte del contorno suavizado: con los escalones de píxel, los bordes inclinados alternan
// coberturas y, al repetir el relleno, la tolerancia deja dientes que crecen con cada clic. Con
// un borde enorme (ruido de una foto) sería muy lento y no se notaría: cada cuarto del píxel
// cuenta si toca un píxel de la máscara.
static QImage halfPixelRing(const QImage& m, const QRect& bounds)
{
    QImage ring(bounds.size(), QImage::Format_Alpha8);
    ring.fill(0);
    const ConstMask mask(m);
    auto in = [&](int x, int y) { return x >= 0 && y >= 0 && x < m.width() && y < m.height() && mask.row(y)[x]; };
    qsizetype edge = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
        for (int x = bounds.left(); x <= bounds.right(); ++x)
            edge += !in(x, y) && (in(x - 1, y) || in(x + 1, y) || in(x, y - 1) || in(x, y + 1));
    if (edge <= 60000) {
        QPainter p(&ring);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(-bounds.topLeft());
        p.setPen(QPen(Qt::black, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::black);
        p.drawPath(Selection::smoothOutline(m));
    }
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        uchar* out = ring.scanLine(y - bounds.top());
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (in(x, y)) {
                out[x - bounds.left()] = 0;
            } else if (edge > 60000) {
                int quarters = 0;
                for (int sy : {-1, 1})
                    for (int sx : {-1, 1})
                        quarters += in(x + sx, y) || in(x, y + sy) || in(x + sx, y + sy);
                out[x - bounds.left()] = uchar(std::min(255, quarters * 64));
            }
        }
    }
    return ring;
}

QRect paintBucket(QImage& layer, const QImage& before, const QImage& sample, QPoint seed, const BucketOptions& o,
                  const Selection& selection)
{
    const QImage m = mask(sample, seed, o.tolerance, o.contiguous, o.toleranceStraight);
    QRect bounds = Selection::fromMask(m).bounds();
    if (bounds.isEmpty())
        return {};
    const QList<EdgePixel> edge = o.antialias ? antialiasEdge(sample, m) : QList<EdgePixel>();
    for (const EdgePixel& e : edge)
        bounds |= QRect(e.pos, QSize(1, 1));
    // Acumular: como en Paint.NET, el borde suavizado sale medio píxel de la zona y se mezcla con
    // lo que había, así que cada relleno repetido engrosa el borde.
    const bool accumulate = o.antialias && o.accumulate;
    QImage ring;
    if (accumulate) {
        bounds = bounds.adjusted(-1, -1, 1, 1) & layer.rect();
        ring = halfPixelRing(m, bounds);
    }

    // Relleno (color o trama) y cobertura de cada píxel.
    QImage source(bounds.size(), kLayerFormat);
    Raster::renderFill(source, bounds.topLeft(), o.pattern, o.primary, o.secondary);
    QImage coverage = m.copy(bounds);
    const bool unmix = o.blend == BlendMode::Normal && o.sampleIsLayer;
    if (!unmix) {
        for (const EdgePixel& e : edge)
            if (!m.constScanLine(e.pos.y())[e.pos.x()])
                coverage.scanLine(e.pos.y() - bounds.top())[e.pos.x() - bounds.left()] = uchar(e.coverage);
        if (accumulate)
            for (int y = 0; y < coverage.height(); ++y) {
                uchar* c = coverage.scanLine(y);
                const uchar* r = ring.constScanLine(y);
                for (int x = 0; x < coverage.width(); ++x)
                    c[x] = std::max(c[x], r[x]);
            }
    }
    Raster::compositeRegion(layer, before, source, bounds, o.blend, selection, coverage);

    if (unmix) {
        const bool aa = Raster::selectionAntialias();
        const ConstMask sel(selection.mask());
        for (const EdgePixel& e : edge) {
            const int x = e.pos.x(), y = e.pos.y();
            // Acumulando, lo que entra en la tolerancia queda relleno del todo (como en Paint.NET):
            // así el borde avanza parejo con cada clic.
            if (accumulate && m.constScanLine(y)[x])
                continue;
            const QRgb p = before.pixel(x, y);
            const QRgb f = source.pixel(x - bounds.left(), y - bounds.top());
            const QRgb unmixed = replaceInterior(p, e.interior, f, e.coverage);
            const int weight = sel.isNull() ? 255 : Raster::clipCoverage(sel.row(y)[x], aa);
            layer.setPixel(x, y, lerpStraight(p, unmixed, weight));
        }
        // Lo desmezclado no deja halo; encima, el medio píxel de más que acumula.
        if (accumulate) {
            for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
                const uchar* r = ring.constScanLine(y - bounds.top());
                for (int x = bounds.left(); x <= bounds.right(); ++x) {
                    const int cov = r[x - bounds.left()];
                    const int weight = sel.isNull() ? 255 : Raster::clipCoverage(sel.row(y)[x], aa);
                    if (!cov || !weight)
                        continue;
                    const QRgb f = source.pixel(x - bounds.left(), y - bounds.top());
                    layer.setPixel(x, y, overStraight(layer.pixel(x, y), scaleAlpha(f, cov * weight / 255)));
                }
            }
        }
    }
    return bounds;
}

} // namespace FloodFill
