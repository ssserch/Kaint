#include "core/effects/Effect.h"

#include "core/Raster.h"
#include "core/effects/EffectUtils.h"

#include <QRandomGenerator>
#include <QVector3D>

#include <algorithm>

// ---------------------------------------------------------------------------
// Parámetros
// ---------------------------------------------------------------------------

EffectParam EffectParam::integer(const QString& key, const QString& label, int min, int max, int def)
{
    EffectParam p;
    p.type = Type::Int;
    p.key = key;
    p.label = label;
    p.min = min;
    p.max = max;
    p.def = def;
    return p;
}

EffectParam EffectParam::real(const QString& key, const QString& label, double min, double max, double def, int decimals)
{
    EffectParam p = integer(key, label, 0, 0, 0);
    p.type = Type::Double;
    p.min = min;
    p.max = max;
    p.def = def;
    p.decimals = decimals;
    return p;
}

EffectParam EffectParam::boolean(const QString& key, const QString& label, bool def)
{
    EffectParam p = integer(key, label, 0, 1, def ? 1 : 0);
    p.type = Type::Bool;
    return p;
}

EffectParam EffectParam::choice(const QString& key, const QString& label, const QStringList& choices, int def)
{
    EffectParam p = integer(key, label, 0, int(choices.size()) - 1, def);
    p.type = Type::Choice;
    p.choices = choices;
    return p;
}

EffectParam EffectParam::seed(const QString& label)
{
    EffectParam p = integer(QStringLiteral("seed"), label.isEmpty() ? QObject::tr("Aleatorizar") : label, 0, 255, 0);
    p.type = Type::Seed;
    return p;
}

EffectParam EffectParam::angle(const QString& key, const QString& label, double min, double max, double def)
{
    EffectParam p = real(key, label, min, max, def, 2);
    p.type = Type::Angle;
    return p;
}

EffectParam EffectParam::offset(const QString& key, const QString& label)
{
    EffectParam p = real(key, label, -1, 1, 0, 2);
    p.type = Type::Offset;
    return p;
}

EffectParam EffectParam::color(const QString& key, const QString& label, ColorDefault def)
{
    EffectParam p = integer(key, label, 0, 0, 0);
    p.type = Type::Color;
    p.colorDefault = def;
    return p;
}

EffectParam EffectParam::curves(const QString& key)
{
    EffectParam p = integer(key, QString(), 0, 0, 0);
    p.type = Type::Curves;
    return p;
}

EffectParam EffectParam::levels(const QString& key)
{
    EffectParam p = integer(key, QString(), 0, 0, 0);
    p.type = Type::Levels;
    return p;
}

EffectParam EffectParam::rollBall(const QString& key, const QString& label, double maxTilt)
{
    EffectParam p = real(key, label, -maxTilt, maxTilt, 0, 2);
    p.type = Type::RollBall;
    return p;
}

EffectParam EffectParam::text(const QString& key, const QString& label, const QString& def, bool multiline)
{
    EffectParam p = integer(key, label, 0, 0, 0);
    p.type = Type::Text;
    p.defaultValue = def;
    p.multiline = multiline;
    return p;
}

bool EffectParam::isReadOnly(const ParamValues& values) const
{
    if (readOnly)
        return true;
    if (readOnlyWhen.isEmpty())
        return false;
    bool match = false;
    for (const ReadOnlyCondition& c : readOnlyWhen) {
        const QVariant v = values.value(c.key);
        for (const QVariant& w : c.values) {
            // Comparación por tipo del valor actual (los índices llegan como double).
            if (v.typeId() == QMetaType::Bool ? v.toBool() == w.toBool() : v.toDouble() == w.toDouble())
                match = true;
        }
    }
    return match != readOnlyInverse;
}

bool Effect::showCustomDialog(const QImage&, const QRect&, const EffectContext&, QString*) const
{
    return true;
}

EffectParam EffectParam::linked(const QString& boolKey) const
{
    EffectParam p = *this;
    p.linkKey = boolKey;
    return p;
}

// ---------------------------------------------------------------------------
// Curvas: interpolación cúbica monótona (Fritsch–Carlson), sin sobreoscilación.
// ---------------------------------------------------------------------------

CurvesValue::CurvesValue()
{
    for (auto& list : points)
        list = {QPointF(0, 0), QPointF(255, 255)};
}

std::array<uint8_t, 256> CurvesValue::lut(int channel) const
{
    QList<QPointF> pts = points[size_t(std::clamp(channel, 0, 3))];
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return qFuzzyCompare(a.x(), b.x()); }),
              pts.end());

    std::array<uint8_t, 256> out{};
    const int n = int(pts.size());
    if (n == 0) {
        for (int i = 0; i < 256; ++i)
            out[size_t(i)] = uint8_t(i);
        return out;
    }
    if (n == 1) {
        out.fill(uint8_t(clamp255f(float(pts[0].y()))));
        return out;
    }

    std::vector<double> d(static_cast<size_t>(n - 1)), m(static_cast<size_t>(n));
    for (int k = 0; k < n - 1; ++k)
        d[size_t(k)] = (pts[k + 1].y() - pts[k].y()) / (pts[k + 1].x() - pts[k].x());
    m[0] = d[0];
    m[size_t(n - 1)] = d[size_t(n - 2)];
    for (int k = 1; k < n - 1; ++k)
        m[size_t(k)] = (d[size_t(k - 1)] * d[size_t(k)] <= 0) ? 0.0 : (d[size_t(k - 1)] + d[size_t(k)]) / 2.0;
    for (int k = 0; k < n - 1; ++k) {
        if (d[size_t(k)] == 0) {
            m[size_t(k)] = m[size_t(k + 1)] = 0;
            continue;
        }
        const double a = m[size_t(k)] / d[size_t(k)];
        const double b = m[size_t(k + 1)] / d[size_t(k)];
        const double s = a * a + b * b;
        if (s > 9) {
            const double t = 3.0 / std::sqrt(s);
            m[size_t(k)] = t * a * d[size_t(k)];
            m[size_t(k + 1)] = t * b * d[size_t(k)];
        }
    }

    int k = 0;
    for (int i = 0; i < 256; ++i) {
        const double x = i;
        double y;
        if (x <= pts.front().x()) {
            y = pts.front().y();
        } else if (x >= pts.back().x()) {
            y = pts.back().y();
        } else {
            while (k < n - 2 && x > pts[k + 1].x())
                ++k;
            const double h = pts[k + 1].x() - pts[k].x();
            const double t = (x - pts[k].x()) / h;
            const double t2 = t * t, t3 = t2 * t;
            y = (2 * t3 - 3 * t2 + 1) * pts[k].y() + (t3 - 2 * t2 + t) * h * m[size_t(k)]
              + (-2 * t3 + 3 * t2) * pts[k + 1].y() + (t3 - t2) * h * m[size_t(k + 1)];
        }
        out[size_t(i)] = uint8_t(clamp255f(float(y)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Niveles (UnaryPixelOps.Level de Paint.NET)
// ---------------------------------------------------------------------------

std::array<uint8_t, 256> LevelsValue::lut(int c) const
{
    std::array<uint8_t, 256> out{};
    const int lo = inLow[size_t(c)], hi = std::max(lo + 1, inHigh[size_t(c)]);
    const int olo = outLow[size_t(c)], ohi = outHigh[size_t(c)];
    const double g = std::clamp(gamma[size_t(c)], 0.1, 10.0);
    for (int i = 0; i < 256; ++i) {
        const double v = i - lo;
        if (v < 0)
            out[size_t(i)] = uint8_t(olo);
        else if (i >= hi)
            out[size_t(i)] = uint8_t(ohi);
        else
            out[size_t(i)] = uint8_t(std::clamp(olo + (ohi - olo) * std::pow(v / (hi - lo), g), 0.0, 255.0));
    }
    return out;
}

bool LevelsValue::isIdentity() const
{
    return *this == LevelsValue();
}

LevelsValue LevelsValue::automatic(const QImage& image, const QRect& rect)
{
    std::array<std::array<qint64, 256>, 3> hist{};
    qint64 total = 0;
    std::array<double, 3> sum{};
    const ConstPixels in(image);
    const bool pm = isPremultiplied(image);
    const QRect r = rect.intersected(image.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb* row = in.row(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            if (qAlpha(row[x]) == 0)
                continue;
            const QRgb u = toStraight(row[x], pm);
            const int c[3] = {qRed(u), qGreen(u), qBlue(u)};
            for (int i = 0; i < 3; ++i) {
                ++hist[size_t(i)][size_t(c[i])];
                sum[size_t(i)] += c[i];
            }
            ++total;
        }
    }
    LevelsValue v;
    if (total == 0)
        return v;
    auto percentile = [&](int ch, double fraction) {
        const qint64 target = qint64(fraction * total);
        qint64 acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[size_t(ch)][size_t(i)];
            if (acc > target)
                return i;
        }
        return 255;
    };
    for (int c = 0; c < 3; ++c) {
        const int lo = percentile(c, 0.005);
        const int hi = percentile(c, 0.995);
        const double md = sum[size_t(c)] / double(total);
        v.inLow[size_t(c)] = lo;
        v.inHigh[size_t(c)] = std::max(lo + 1, hi);
        if (lo < md && md < hi)
            v.gamma[size_t(c)] = std::clamp(std::log(0.5) / std::log((md - lo) / double(hi - lo)), 0.1, 10.0);
    }
    return v;
}

std::any Effect::prepare(const QImage&, const QRect&, const ParamValues&, const EffectContext&) const
{
    return {};
}

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

namespace fx {

BrightnessContrast::BrightnessContrast(int brightness, int contrast)
    : m_table(65536)
{
    int multiply, divide;
    if (contrast < 0) {
        multiply = contrast + 100;
        divide = 100;
    } else if (contrast > 0) {
        multiply = 100;
        divide = 100 - contrast;
    } else {
        multiply = 1;
        divide = 1;
    }
    m_divide = divide;
    if (divide == 0) {
        for (int i = 0; i < 256; ++i)
            m_table[size_t(i)] = (i + brightness < 128) ? 0 : 255;
        return;
    }
    for (int i = 0; i < 256; ++i) {
        const int shift = divide == 100 ? (i - 127) * multiply / divide + 127 - i + brightness
                                        : (i - 127 + brightness) * multiply / divide + 127 - i;
        for (int col = 0; col < 256; ++col)
            m_table[size_t(i * 256 + col)] = uint8_t(clamp255(col + shift));
    }
}

QRgb BrightnessContrast::apply(QRgb c) const
{
    const int i = intensity(c);
    if (m_divide == 0) {
        const int v = m_table[size_t(i)];
        return qRgba(v, v, v, qAlpha(c));
    }
    const size_t base = size_t(i) * 256;
    return qRgba(m_table[base + size_t(qRed(c))], m_table[base + size_t(qGreen(c))], m_table[base + size_t(qBlue(c))],
                 qAlpha(c));
}

double toLinear(int c)
{
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

int toSrgb(double l)
{
    l = std::clamp(l, 0.0, 1.0);
    const double v = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1 / 2.4) - 0.055;
    return clamp255f(float(v * 255.0));
}

QStringList edgeBehaviorNames(const QList<EdgeBehavior>& list)
{
    QStringList names;
    for (EdgeBehavior e : list) {
        switch (e) {
        // Los nombres de Paint.NET 5.
        case EdgeBehavior::Clamp: names << QObject::tr("Sujetar"); break;
        case EdgeBehavior::Wrap: names << QObject::tr("Ajuste"); break;
        case EdgeBehavior::Reflect: names << QObject::tr("Reflejo"); break;
        case EdgeBehavior::Primary: names << QObject::tr("Color principal"); break;
        case EdgeBehavior::Secondary: names << QObject::tr("Color secundario"); break;
        case EdgeBehavior::Transparent: names << QObject::tr("Transparente"); break;
        case EdgeBehavior::Original: names << QObject::tr("Original"); break;
        }
    }
    return names;
}

QRgb sampleWrapped(const ConstPixels& src, float x, float y)
{
    auto wrap = [](float v, int n) {
        if (!std::isfinite(v))
            return 0.0f;
        v = std::fmod(v, float(n));
        return v < 0 ? v + n : v;
    };
    x = wrap(x, src.w);
    y = wrap(y, src.h);
    // Bilineal con vecinos envueltos.
    const float fx = x - 0.5f, fy = y - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const int tx = int((fx - x0) * 256), ty = int((fy - y0) * 256);
    auto at = [&](int px, int py) { return src.at(((px % src.w) + src.w) % src.w, ((py % src.h) + src.h) % src.h); };
    const QRgb p00 = at(x0, y0), p10 = at(x0 + 1, y0), p01 = at(x0, y0 + 1), p11 = at(x0 + 1, y0 + 1);
    auto mix = [&](int c00, int c10, int c01, int c11) {
        const int top = c00 * (256 - tx) + c10 * tx;
        const int bottom = c01 * (256 - tx) + c11 * tx;
        return (top * (256 - ty) + bottom * ty + 32768) >> 16;
    };
    return qRgba(mix(qRed(p00), qRed(p10), qRed(p01), qRed(p11)), mix(qGreen(p00), qGreen(p10), qGreen(p01), qGreen(p11)),
                 mix(qBlue(p00), qBlue(p10), qBlue(p01), qBlue(p11)), mix(qAlpha(p00), qAlpha(p10), qAlpha(p01), qAlpha(p11)));
}

QRgb sampleReflected(const ConstPixels& src, float x, float y)
{
    // Periodo 2·max sin bucles: algunas distorsiones dan coordenadas enormes.
    auto reflect = [](float v, int max) {
        if (!std::isfinite(v))
            return 0.0f;
        const float period = 2.0f * max;
        v = std::fmod(v, period);
        if (v < 0)
            v += period;
        return v > max ? period - v : v;
    };
    return sampleBilinear(src, reflect(x, src.w), reflect(y, src.h));
}

QRgb average(const QRgb* s, int n)
{
    if (n <= 0)
        return 0;
    int r = 0, g = 0, b = 0, a = 0;
    for (int i = 0; i < n; ++i)
        r += qRed(s[i]), g += qGreen(s[i]), b += qBlue(s[i]), a += qAlpha(s[i]);
    return qRgba(r / n, g / n, b / n, a / n);
}

std::vector<QPointF> rgssOffsets(int quality)
{
    const int count = quality * quality;
    std::vector<QPointF> pts(static_cast<size_t>(std::max(1, count)));
    if (count <= 1)
        return {QPointF(0, 0)};
    for (int i = 0; i < count; ++i) {
        const double y = (i + 1.0) / (count + 1.0);
        double x = y * quality;
        x -= std::floor(x);
        pts[size_t(i)] = QPointF(x - 0.5, y - 0.5);
    }
    return pts;
}

void warp(const RenderArgs& a, const WarpSetup& setup, const std::function<QPointF(double, double)>& inverse)
{
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    const double xc = a.roi.left() + a.roi.width() * (1.0 + setup.offset.x()) * 0.5;
    const double yc = a.roi.top() + a.roi.height() * (1.0 + setup.offset.y()) * 0.5;
    const std::vector<QPointF> aa = rgssOffsets(std::clamp(setup.quality, 1, 5));
    const QRgb primary = qPremultiply(a.ctx.primary.rgba()); // warp trabaja en premultiplicado
    const QRgb secondary = qPremultiply(a.ctx.secondary.rgba());
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(aa.size());
        QRgb* d = out.row(y);
        const double ry = y - yc;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double rx = x - xc;
            for (size_t p = 0; p < aa.size(); ++p) {
                const QPointF t = inverse(rx + aa[p].x(), ry - aa[p].y());
                const float sx = float(t.x() + xc), sy = float(t.y() + yc);
                QRgb s;
                if (sx >= 0 && sy >= 0 && sx <= in.w - 1 && sy <= in.h - 1) {
                    s = sampleBilinear(in, sx + 0.5f, sy + 0.5f);
                } else {
                    switch (setup.edge) {
                    case EdgeBehavior::Clamp: s = sampleBilinear(in, sx + 0.5f, sy + 0.5f); break;
                    case EdgeBehavior::Wrap: s = sampleWrapped(in, sx + 0.5f, sy + 0.5f); break;
                    case EdgeBehavior::Reflect: s = sampleReflected(in, sx + 0.5f, sy + 0.5f); break;
                    case EdgeBehavior::Primary: s = primary; break;
                    case EdgeBehavior::Secondary: s = secondary; break;
                    case EdgeBehavior::Transparent: s = 0; break;
                    case EdgeBehavior::Original: default: s = in.at(x, y); break;
                    }
                }
                samples[p] = s;
            }
            d[x] = average(samples.data(), int(samples.size()));
        }
    });
}

// --- Desenfoques -------------------------------------------------------------

namespace {

// Media de una ventana [lo, hi] alrededor de cada píxel en un eje, contando
// solo los píxeles dentro de la imagen (como hace Paint.NET en los bordes).
void boxPass(const QImage& inImg, QImage& outImg, int lo, int hi, bool horizontal, const std::atomic_bool& cancel)
{
    const ConstPixels in(inImg);
    const Pixels out(outImg);
    const int n = horizontal ? in.w : in.h;
    const int lines = horizontal ? in.h : in.w;
    parallelRows(0, lines, [&](int l0, int l1) {
        for (int line = l0; line < l1 && !cancel; ++line) {
            auto px = [&](int i) { return horizontal ? in.at(i, line) : in.at(line, i); };
            auto put = [&](int i, QRgb v) {
                if (horizontal)
                    out.row(line)[i] = v;
                else
                    out.row(i)[line] = v;
            };
            int sr = 0, sg = 0, sb = 0, sa = 0, count = 0;
            for (int i = std::max(0, lo); i <= std::min(n - 1, hi); ++i) {
                const QRgb p = px(i);
                sr += qRed(p), sg += qGreen(p), sb += qBlue(p), sa += qAlpha(p);
                ++count;
            }
            for (int i = 0; i < n; ++i) {
                const int c = std::max(1, count);
                put(i, qRgba((sr + c / 2) / c, (sg + c / 2) / c, (sb + c / 2) / c, (sa + c / 2) / c));
                const int leave = i + lo;
                const int enter = i + hi + 1;
                if (leave >= 0 && leave < n) {
                    const QRgb p = px(leave);
                    sr -= qRed(p), sg -= qGreen(p), sb -= qBlue(p), sa -= qAlpha(p);
                    --count;
                }
                if (enter >= 0 && enter < n) {
                    const QRgb p = px(enter);
                    sr += qRed(p), sg += qGreen(p), sb += qBlue(p), sa += qAlpha(p);
                    ++count;
                }
            }
        }
    }, horizontal ? 8 : 16);
}

QRect expanded(const QRect& area, int margin, const QRect& bounds)
{
    return area.adjusted(-margin, -margin, margin, margin).intersected(bounds);
}

} // namespace

int tentMargin(int radius)
{
    return radius + 1;
}

QImage tentBlur(const QImage& src, const QRect& area, int radius, const std::atomic_bool& cancel)
{
    if (radius <= 0)
        return src.copy(area);
    const QRect big = expanded(area, tentMargin(radius), src.rect());
    QImage a = src.copy(big);
    QImage b(a.size(), a.format());
    // Dos cajas de ancho r+1 desplazadas medio píxel en sentidos opuestos
    // forman el triángulo simétrico de radio r.
    const int lo1 = -(radius / 2), hi1 = (radius + 1) / 2;
    const int lo2 = -((radius + 1) / 2), hi2 = radius / 2;
    // Los bordes de 'big' que no son bordes de la imagen tienen margen de sobra.
    boxPass(a, b, lo1, hi1, true, cancel);
    boxPass(b, a, lo2, hi2, true, cancel);
    boxPass(a, b, lo1, hi1, false, cancel);
    boxPass(b, a, lo2, hi2, false, cancel);
    if (cancel)
        return {};
    return a.copy(QRect(area.topLeft() - big.topLeft(), area.size()));
}

QImage boxBlur(const QImage& src, const QRect& area, int radius, const std::atomic_bool& cancel)
{
    if (radius <= 0)
        return src.copy(area);
    const QRect big = expanded(area, radius + 1, src.rect());
    QImage a = src.copy(big);
    QImage b(a.size(), a.format());
    boxPass(a, b, -radius, radius, true, cancel);
    boxPass(b, a, -radius, radius, false, cancel);
    if (cancel)
        return {};
    return a.copy(QRect(area.topLeft() - big.topLeft(), area.size()));
}

// --- Histograma local --------------------------------------------------------

QRgb histogramPercentile(const LocalHistogram& h, int percentile)
{
    const int minCount = h.area * percentile / 100;
    auto channel = [&](const int* hist) {
        int v = 0, count = 0;
        while (v < 255 && hist[v] == 0)
            ++v;
        while (v < 255 && count < minCount) {
            count += hist[v];
            ++v;
        }
        return v;
    };
    return qRgba(channel(h.r), channel(h.g), channel(h.b), channel(h.a));
}

QRgb quantizeBits(QRgb c, int bits)
{
    if (bits >= 8 || bits <= 0) // 0: valores guardados sin "Calidad"
        return c;
    // Centro del intervalo en la escala 0–255 (con calidad 1 el blanco queda en 191).
    const int shift = 8 - bits;
    const double levels = double(1 << bits);
    auto q = [shift, levels](int v) { return int(std::lround(((v >> shift) + 0.5) * 255.0 / levels)); };
    return qRgba(q(qRed(c)), q(qGreen(c)), q(qBlue(c)), q(qAlpha(c)));
}

void localHistogram(const RenderArgs& a, int radius, const HistogramFn& apply)
{
    const int rad = std::max(0, radius);
    const QRect region = expanded(a.roi, rad + 1, a.src.rect());
    const QImage straight = a.src.copy(region).convertToFormat(QImage::Format_ARGB32);
    const ConstPixels s(straight);
    const Pixels out(a.dst);
    const int ox = region.left(), oy = region.top();
    const int width = a.src.width(), height = a.src.height();

    // Semiancho horizontal del círculo en cada fila (aprox. (r + 0,5)^2).
    const int cutoff = ((rad * 2 + 1) * (rad * 2 + 1) + 2) / 4;
    std::vector<int> edge(size_t(rad + 1), 0);
    for (int v = 0; v <= rad; ++v)
        for (int u = 0; u <= rad; ++u)
            if (u * u + v * v <= cutoff)
                edge[size_t(v)] = u;

    forRows(a, [&](int y) {
        LocalHistogram h{};
        auto add = [&](int x, int yy, int sign) {
            const QRgb p = s.at(x - ox, yy - oy);
            h.b[qBlue(p)] += sign;
            h.g[qGreen(p)] += sign;
            h.r[qRed(p)] += sign;
            h.a[qAlpha(p)] += sign;
            h.area += sign;
        };
        const int top = -std::min(rad, y);
        const int bottom = std::min(rad, height - 1 - y);
        const int x0 = a.roi.left();
        for (int v = top; v <= bottom; ++v) {
            const int w = edge[size_t(std::abs(v))];
            for (int u = std::max(-w, -x0); u <= std::min(w, width - 1 - x0); ++u)
                add(x0 + u, y + v, +1);
        }
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            d[x] = qPremultiply(apply(s.at(x - ox, y - oy), h));
            if (x == a.roi.right())
                break;
            for (int v = top; v <= bottom; ++v) {
                const int w = edge[size_t(std::abs(v))];
                if (x - w >= 0)
                    add(x - w, y + v, -1);
                if (x + 1 + w < width)
                    add(x + 1 + w, y + v, +1);
            }
        }
    });
}

// --- Perlin de Paint.NET -----------------------------------------------------

namespace {

const int kPermutation[256] = {
    151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142,
    8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
    117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165,
    71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,
    55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,
    18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250,
    124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189,
    28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,
    129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,
    242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,
    181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114,
    67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180,
};

const std::array<int, 512>& perm()
{
    static const std::array<int, 512> p = [] {
        std::array<int, 512> t{};
        for (int i = 0; i < 256; ++i)
            t[size_t(i)] = t[size_t(256 + i)] = kPermutation[i];
        return t;
    }();
    return p;
}

double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
double grad(int hash, double x, double y)
{
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : ((h == 12 || h == 14) ? x : 0);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

double noise2(double x, double y, uint8_t seed)
{
    const auto& P = perm();
    const double xf = std::floor(x), yf = std::floor(y);
    const int ix = int(xf) & 255, iy = int(yf) & 255;
    x -= xf;
    y -= yf;
    const double u = fade(x), v = fade(y);
    const int a = P[size_t(ix + seed)] + iy;
    const int aa = P[size_t(a)], ab = P[size_t(a + 1)];
    const int b = P[size_t(ix + 1 + seed)] + iy;
    const int ba = P[size_t(b)], bb = P[size_t(b + 1)];
    const double e1 = grad(P[size_t(aa)], x, y) + u * (grad(P[size_t(ba)], x - 1, y) - grad(P[size_t(aa)], x, y));
    const double e2 = grad(P[size_t(ab)], x, y - 1) + u * (grad(P[size_t(bb)], x - 1, y - 1) - grad(P[size_t(ab)], x, y - 1));
    return e1 + v * (e2 - e1);
}

} // namespace

double pdnPerlin(double x, double y, double detail, double roughness, uint8_t seed)
{
    static const double angle = 137.2 / 180.0 * M_PI;
    static const double r11 = std::cos(angle), r12 = -std::sin(angle), r21 = std::sin(angle), r22 = std::cos(angle);
    double total = 0, frequency = 1, amplitude = 1, partial = detail;
    const int octaves = int(std::ceil(detail));
    for (int i = 0; i < octaves; ++i) {
        const double xr = x * r11 + y * r12;
        const double yr = x * r21 + y * r22;
        double n = noise2(xr * frequency, yr * frequency, seed) * amplitude;
        if (partial < 1)
            n *= partial;
        total += n;
        amplitude *= roughness;
        if (amplitude < 0.001)
            break;
        frequency += frequency;
        partial -= 1.0;
        x = xr + 499;
        y = yr + 506;
    }
    return total;
}

// --- Parámetros habituales -------------------------------------------------------

EffectParam qualityParam(int def)
{
    return EffectParam::integer(QStringLiteral("quality"), QObject::tr("Calidad"), 1, 5, def);
}

EffectParam blendParam(const char* key)
{
    QStringList names;
    for (BlendMode m : layerBlendModes())
        names << blendModeName(m);
    return EffectParam::choice(QLatin1String(key), QObject::tr("Modo de fusión"), names, 0);
}

BlendMode blendValue(const ParamValues& v, const char* key)
{
    return layerBlendModes().value(ival(v, key), BlendMode::Normal);
}

EffectParam edgeParam(const QList<EdgeBehavior>& list, EdgeBehavior def)
{
    return EffectParam::choice(QStringLiteral("edge"), QObject::tr("Comportamiento de borde"), edgeBehaviorNames(list),
                               int(std::max<qsizetype>(0, list.indexOf(def))));
}

const QList<EdgeBehavior>& pdnEdges()
{
    static const QList<EdgeBehavior> list{EdgeBehavior::Clamp, EdgeBehavior::Wrap, EdgeBehavior::Reflect,
                                          EdgeBehavior::Transparent};
    return list;
}

QRgb sampleEdge(const ConstPixels& in, float sx, float sy, EdgeBehavior edge)
{
    // sx, sy en coordenadas de centro de píxel (+0,5).
    const bool inside = sx >= 0.5f && sy >= 0.5f && sx <= in.w - 0.5f && sy <= in.h - 0.5f;
    if (inside)
        return sampleBilinear(in, sx, sy);
    switch (edge) {
    case EdgeBehavior::Wrap: return sampleWrapped(in, sx, sy);
    case EdgeBehavior::Reflect: return sampleReflected(in, sx, sy);
    case EdgeBehavior::Transparent: return 0;
    default: return sampleBilinear(in, sx, sy);
    }
}

EdgeBehavior edgeValue(const ParamValues& v, const QList<EdgeBehavior>& list)
{
    return list.value(ival(v, "edge"), EdgeBehavior::Clamp);
}

} // namespace fx

// ---------------------------------------------------------------------------
// Registro y ejecución
// ---------------------------------------------------------------------------

namespace Effects {

namespace {
QList<const Effect*>& registry()
{
    static QList<const Effect*> list = [] {
        QList<const Effect*> l;
        fx::registerAdjustments(l);
        fx::registerArtistic(l);
        fx::registerBlurs(l);
        fx::registerColor(l);
        fx::registerDistort(l);
        fx::registerNoise(l);
        fx::registerObject(l);
        fx::registerPhoto(l);
        fx::registerRender(l);
        fx::registerStylize(l);
        fx::registerLayerEffects(l);
        return l;
    }();
    return list;
}
} // namespace

const QList<const Effect*>& all()
{
    return registry();
}

void addExternal(const Effect* effect)
{
    if (effect && !registry().contains(effect))
        registry().append(effect);
}

void removeExternal(const Effect* effect)
{
    registry().removeAll(effect);
}

const Effect* find(const QString& id)
{
    for (const Effect* e : all())
        if (e->id() == id)
            return e;
    return nullptr;
}

QStringList categories()
{
    return {fx::catArtistic(), fx::catBlurs(), fx::catColor(), fx::catDistort(), fx::catNoise(),
            fx::catObject(), fx::catPhoto(), fx::catRender(), fx::catStylize()};
}

ParamValues defaults(const Effect& effect, const EffectContext& ctx)
{
    ParamValues v;
    for (const EffectParam& p : effect.params()) {
        if (p.defaultValue.isValid() && p.type != EffectParam::Type::Seed) {
            v.insert(p.key, p.defaultValue);
            continue;
        }
        switch (p.type) {
        case EffectParam::Type::Int:
        case EffectParam::Type::Choice: v.insert(p.key, int(p.def)); break;
        case EffectParam::Type::Double:
        case EffectParam::Type::Angle: v.insert(p.key, p.def); break;
        case EffectParam::Type::Bool: v.insert(p.key, p.def != 0); break;
        case EffectParam::Type::Seed: v.insert(p.key, int(QRandomGenerator::global()->bounded(256))); break;
        case EffectParam::Type::Offset: v.insert(p.key, QPointF(0, 0)); break;
        case EffectParam::Type::Curves: v.insert(p.key, QVariant::fromValue(CurvesValue())); break;
        case EffectParam::Type::Levels: v.insert(p.key, QVariant::fromValue(LevelsValue())); break;
        case EffectParam::Type::RollBall: v.insert(p.key, QVector3D(0, 0, 0)); break;
        case EffectParam::Type::Text: v.insert(p.key, QString()); break;
        case EffectParam::Type::Color: {
            QColor c;
            switch (p.colorDefault) {
            case EffectParam::ColorDefault::Primary: c = ctx.primary; break;
            case EffectParam::ColorDefault::Secondary: c = ctx.secondary; break;
            case EffectParam::ColorDefault::Black: c = Qt::black; break;
            case EffectParam::ColorDefault::White: c = Qt::white; break;
            }
            v.insert(p.key, c);
            break;
        }
        }
    }
    return v;
}

QImage apply(const Effect& effect, const QImage& src, const Selection& selection, const ParamValues& values,
             const EffectContext& ctx, const std::atomic_bool& cancel)
{
    // Los efectos espaciales calculan en premultiplicado; los ajustes píxel a
    // píxel reciben la capa tal cual (alfa directo).
    const bool straightIn = !isPremultiplied(src);
    const bool convert = straightIn && !effect.straightAlpha();
    const QImage work = convert ? Raster::premultiplied(src) : src;
    QImage dst = work.copy();
    const QRect roi = selection.isEmpty() ? work.rect() : selection.bounds().intersected(work.rect());
    if (roi.isEmpty())
        return src.copy();
    if (ctx.progress) {
        ctx.progress->done = 0;
        ctx.progress->total = roi.height();
    }
    const std::any state = effect.prepare(work, roi, values, ctx);
    effect.render(RenderArgs{work, dst, roi, values, ctx, cancel, state});
    if (cancel)
        return {};

    const bool premul = isPremultiplied(dst);
    if (!selection.isEmpty()) {
        const bool aa = Raster::selectionAntialias();
        const ConstMask mask(selection.mask());
        const ConstPixels in(work);
        const Pixels out(dst);
        parallelRows(roi.top(), roi.bottom() + 1, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const uchar* m = mask.row(y);
                const QRgb* s = in.row(y);
                QRgb* d = out.row(y);
                for (int x = roi.left(); x <= roi.right(); ++x) {
                    const int t = Raster::clipCoverage(m[x], aa);
                    d[x] = premul ? lerpPixel(s[x], d[x], t) : lerpStraight(s[x], d[x], t);
                }
            }
        });
    }
    if (!convert)
        return dst;

    // De vuelta a alfa directo. Donde el efecto no cambió nada se recupera el
    // píxel original exacto (con su color aunque sea transparente).
    QImage out(dst.size(), kLayerFormat);
    const ConstPixels orig(src), before(work), after(dst);
    const Pixels res(out);
    parallelRows(0, out.height(), [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const QRgb* o = orig.row(y);
            const QRgb* b = before.row(y);
            const QRgb* a = after.row(y);
            QRgb* r = res.row(y);
            for (int x = 0; x < out.width(); ++x)
                r[x] = a[x] == b[x] ? o[x] : qUnpremultiply(a[x]);
        }
    });
    return out;
}

} // namespace Effects
