#include "core/Raster.h"

#include "core/FillPattern.h"
#include "core/Layer.h"
#include "core/Parallel.h"
#include "core/Pixels.h"

#include <QPainter>

#include <atomic>
#include <cstring>

namespace Raster {

namespace {
std::atomic_bool g_selectionAntialias{true};
}

void setSelectionAntialias(bool on) { g_selectionAntialias = on; }
bool selectionAntialias() { return g_selectionAntialias; }

void copyRect(QImage& dst, const QImage& src, const QRect& area)
{
    const QRect r = area.intersected(dst.rect()).intersected(src.rect());
    if (r.isEmpty())
        return;
    const int bpp = dst.depth() / 8;
    for (int y = r.top(); y <= r.bottom(); ++y)
        std::memcpy(dst.scanLine(y) + r.left() * bpp, src.constScanLine(y) + r.left() * bpp, size_t(r.width()) * bpp);
}

void blit(QImage& dst, const QImage& src, QPoint at)
{
    const QRect target = QRect(at, src.size()).intersected(dst.rect());
    if (target.isEmpty())
        return;
    const int bpp = dst.depth() / 8;
    for (int y = target.top(); y <= target.bottom(); ++y)
        std::memcpy(dst.scanLine(y) + target.left() * bpp,
                    src.constScanLine(y - at.y()) + (target.left() - at.x()) * bpp,
                    size_t(target.width()) * bpp);
}

void applySelection(QImage& img, QPoint origin, const Selection& selection)
{
    if (selection.isEmpty())
        return;
    // Capas (alfa directo): se reduce solo el alfa; premultiplicado: todo el píxel.
    const bool pm = isPremultiplied(img);
    const bool aa = selectionAntialias();
    const QImage& mask = selection.mask();
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int my = y + origin.y();
        if (my < 0 || my >= mask.height()) {
            std::memset(row, 0, size_t(img.width()) * 4);
            continue;
        }
        const uchar* mrow = mask.constScanLine(my);
        for (int x = 0; x < img.width(); ++x) {
            const int mx = x + origin.x();
            const int m = (mx < 0 || mx >= mask.width()) ? 0 : clipCoverage(mrow[mx], aa);
            row[x] = pm ? scalePixel(row[x], m) : scaleAlpha(row[x], m);
        }
    }
}

void compositeRegion(QImage& layer, const QImage& before, const QImage& source, const QRect& area, BlendMode mode,
                     const Selection& selection, const QImage& coverage)
{
    const QRect r = area.intersected(layer.rect());
    if (r.isEmpty())
        return;
    const QPoint off = r.topLeft() - area.topLeft();
    copyRect(layer, before, r);

    const bool aa = selectionAntialias();
    const bool srcPm = isPremultiplied(source);
    const ConstMask sel(selection.mask());
    const ConstMask cov(coverage);
    auto weight = [&](int x, int y) {
        // x, y en coordenadas del lienzo
        int m = cov.isNull() ? 255 : cov.row(y - area.top())[x - area.left()];
        if (!sel.isNull())
            m = m * clipCoverage(sel.row(y)[x], aa) / 255;
        return m;
    };

    if (mode == BlendMode::Overwrite) {
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const QRgb* s = reinterpret_cast<const QRgb*>(source.constScanLine(y - area.top()));
            const QRgb* b = reinterpret_cast<const QRgb*>(before.constScanLine(y));
            QRgb* d = reinterpret_cast<QRgb*>(layer.scanLine(y));
            for (int x = r.left(); x <= r.right(); ++x)
                d[x] = lerpStraight(b[x], toStraight(s[x - area.left()], srcPm), weight(x, y));
        }
        return;
    }

    // Fuente con el alfa reducido por la cobertura, compuesta con el modo de fusión.
    QImage overlay(r.size(), kLayerFormat);
    for (int y = 0; y < r.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(source.constScanLine(y + off.y())) + off.x();
        QRgb* o = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < r.width(); ++x)
            o[x] = scaleAlpha(toStraight(s[x], srcPm), weight(x + r.left(), y + r.top()));
    }
    blendImage(layer, r.topLeft(), overlay, mode);
}

void renderFill(QImage& out, QPoint origin, int pattern, QColor fg, QColor bg)
{
    if (pattern == FillPattern::Solid) {
        out.fill(fg);
        return;
    }
    const bool pm = isPremultiplied(out);
    const QRgb f = pm ? qPremultiply(fg.rgba()) : fg.rgba();
    const QRgb b = pm ? qPremultiply(bg.rgba()) : bg.rgba();
    for (int y = 0; y < out.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            row[x] = FillPattern::bit(pattern, x + origin.x(), y + origin.y()) ? f : b;
    }
}

QBrush patternBrush(int pattern, QColor fg, QColor bg)
{
    if (pattern == FillPattern::Solid)
        return QBrush(fg);
    QImage tile(8, 8, kPremulFormat);
    renderFill(tile, QPoint(0, 0), pattern, fg, bg);
    return QBrush(tile);
}

QImage overlayToSource(const QImage& overlay, const QImage& coverage)
{
    // overlay (premultiplicado) = cobertura · fuente, así que la fuente con
    // alfa directo es: color = overlay / alfa del overlay, alfa = alfa / cobertura.
    QImage out(overlay.size(), kLayerFormat);
    for (int y = 0; y < overlay.height(); ++y) {
        const QRgb* o = reinterpret_cast<const QRgb*>(overlay.constScanLine(y));
        const uchar* c = coverage.constScanLine(y);
        QRgb* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < overlay.width(); ++x) {
            const int cov = c[x];
            const int oa = qAlpha(o[x]);
            if (cov == 0 || oa == 0) {
                d[x] = 0;
                continue;
            }
            const QRgb u = qUnpremultiply(o[x]);
            d[x] = qRgba(qRed(u), qGreen(u), qBlue(u), std::min(255, (oa * 255 + cov / 2) / cov));
        }
    }
    return out;
}

void eraseSelection(QImage& layer, const Selection& selection)
{
    // Como Paint.NET: lo borrado del todo queda en blanco transparente; los
    // bordes suavizados solo pierden alfa y conservan su color.
    if (selection.isEmpty()) {
        layer.fill(kTransparent);
        return;
    }
    const bool aa = selectionAntialias();
    const QRect b = selection.bounds();
    const QImage& mask = selection.mask();
    for (int y = b.top(); y <= b.bottom(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(layer.scanLine(y));
        const uchar* mrow = mask.constScanLine(y);
        for (int x = b.left(); x <= b.right(); ++x) {
            const int m = clipCoverage(mrow[x], aa);
            if (m >= 255)
                row[x] = kTransparent;
            else if (m > 0)
                row[x] = scaleAlpha(row[x], 255 - m);
        }
    }
}

QImage extractSelection(const QImage& layer, const Selection& selection)
{
    if (selection.isEmpty())
        return layer.copy();
    const QRect b = selection.bounds();
    QImage out = layer.copy(b);
    applySelection(out, b.topLeft(), selection);
    return out;
}

QImage premultiplied(const QImage& straight)
{
    if (isPremultiplied(straight))
        return straight;
    if (straight.format() != kLayerFormat)
        return straight.convertToFormat(kPremulFormat);
    QImage out(straight.size(), kPremulFormat);
    const ConstPixels in(straight);
    const Pixels res(out);
    parallelRows(0, out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = in.row(y);
            QRgb* d = res.row(y);
            for (int x = 0; x < in.w; ++x)
                d[x] = qPremultiply(s[x]);
        }
    }, 32);
    return out;
}

QImage unpremultiplied(const QImage& premul)
{
    if (!isPremultiplied(premul))
        return premul.convertToFormat(kLayerFormat);
    QImage out(premul.size(), kLayerFormat);
    const ConstPixels in(premul);
    const Pixels res(out);
    parallelRows(0, out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* s = in.row(y);
            QRgb* d = res.row(y);
            for (int x = 0; x < in.w; ++x)
                d[x] = qUnpremultiply(s[x]);
        }
    }, 32);
    return out;
}

QImage checkerboard(int cell, QColor light, QColor dark)
{
    QImage pm(cell * 2, cell * 2, QImage::Format_RGB32);
    pm.fill(light);
    QPainter p(&pm);
    p.fillRect(0, 0, cell, cell, dark);
    p.fillRect(cell, cell, cell, cell, dark);
    return pm;
}

QImage thumbnail(const QImage& image, QSize box, bool checker)
{
    if (image.isNull())
        return {};
    const QSize size = image.size().scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    QImage out(size, kPremulFormat);
    out.fill(Qt::transparent);
    QPainter p(&out);
    if (checker)
        p.fillRect(out.rect(), QBrush(checkerboard(4)));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(out.rect(), image.scaled(size, Qt::IgnoreAspectRatio,
                                         image.width() > size.width() * 4 ? Qt::FastTransformation : Qt::SmoothTransformation));
    return out;
}

} // namespace Raster
