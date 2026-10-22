// Efectos > Distorsionar (algoritmos de Paint.NET 3.36 y equivalentes de 5.x)

#include "core/effects/EffectUtils.h"

#include <deque>
#include <vector>

namespace fx {

namespace {


void renderBulge(const RenderArgs& a)
{
    const double amt = dval(a.values, "amount");
    const double maxrad = defaultRadius(a.roi);
    WarpSetup setup;
    setup.offset = pval(a.values, "offset");
    setup.quality = ival(a.values, "quality");
    setup.edge = edgeValue(a.values, pdnEdges());
    warp(a, setup, [&](double u, double v) {
        const double r = std::sqrt(u * u + v * v);
        const double s1 = 1.0 - r / maxrad;
        if (s1 <= 0)
            return QPointF(u, v);
        const double s2 = 1.0 - amt * s1 * s1;
        return QPointF(u * s2, v * s2);
    });
}

void renderCrystalize(const RenderArgs& a)
{
    // Diagrama de Voronoi: un punto aleatorio por celda de la rejilla; cada
    // píxel toma el color del punto más cercano.
    const int cell = std::max(2, ival(a.values, "cell"));
    const uint32_t seed = uint32_t(ival(a.values, "seed"));
    const auto aa = rgssOffsets(ival(a.values, "quality"));
    auto site = [&](int cx, int cy) {
        const double jx = hashUnit(uint32_t(cx), uint32_t(cy), seed);
        const double jy = hashUnit(uint32_t(cx), uint32_t(cy), seed + 7u);
        return QPointF((cx + jx) * cell, (cy + jy) * cell);
    };
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(aa.size());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            for (size_t p = 0; p < aa.size(); ++p) {
                const double px = x + 0.5 + aa[p].x(), py = y + 0.5 + aa[p].y();
                const int cx = int(std::floor(px / cell)), cy = int(std::floor(py / cell));
                double best = 1e18;
                QPointF bestSite;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        const QPointF s = site(cx + i, cy + j);
                        const double dd = (s.x() - px) * (s.x() - px) + (s.y() - py) * (s.y() - py);
                        if (dd < best)
                            best = dd, bestSite = s;
                    }
                samples[p] = in.clamped(int(bestSite.x()), int(bestSite.y()));
            }
            d[x] = average(samples.data(), int(samples.size()));
        }
    });
}

void renderDents(const RenderArgs& a)
{
    // DentsEffect de Paint.NET: refracción según un ruido de Perlin girado.
    const double scale = dval(a.values, "scale");
    const double refraction = dval(a.values, "refraction");
    const double roughnessIn = dval(a.values, "roughness");
    const double tension = dval(a.values, "tension");
    const uint8_t seed = uint8_t(ival(a.values, "seed"));
    const double scaleR = (400.0 / defaultRadius(a.roi)) / scale;
    const double refractionScale = (refraction / 100.0) / scaleR;
    const double theta = M_PI * 2.0 * tension / 10.0;
    const double roughness = roughnessIn / 100.0;
    double detail = 1.0 + roughnessIn / 10.0;
    const double maxDetail = std::floor(std::log(scaleR) / std::log(0.5));
    if (detail > maxDetail && maxDetail >= 1.0)
        detail = maxDetail;
    // Ángulo: gira el campo de abolladuras.
    const double rot = dval(a.values, "angle") * M_PI / 180.0;
    const double cr = std::cos(rot), sr = std::sin(rot);
    WarpSetup setup;
    setup.quality = ival(a.values, "quality");
    setup.edge = EdgeBehavior::Wrap;
    warp(a, setup, [&](double x, double y) {
        const double rx = x * cr - y * sr, ry = x * sr + y * cr;
        const double bump = theta * pdnPerlin(rx * scaleR, ry * scaleR, detail, roughness, seed);
        return QPointF(x + refractionScale * std::sin(-bump), y + refractionScale * std::cos(bump));
    });
}

void renderFrostedGlass(const RenderArgs& a)
{
    // Difusión: escala la distancia de dispersión (1 = la de los radios).
    const double diffusion = dval(a.values, "diffusion");
    const double maxR = dval(a.values, "max") * diffusion;
    const double minR = std::min(dval(a.values, "min") * diffusion, std::min(a.src.width(), a.src.height()) / 2.0);
    const int samples = std::clamp(ival(a.values, "smoothness"), 1, 8);
    const uint32_t seed = uint32_t(ival(a.values, "seed"));
    const double delta = std::max(0.0, maxR - minR);
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> s(static_cast<size_t>(samples));
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            for (int i = 0; i < samples; ++i) {
                double sx = x, sy = y;
                for (int attempt = 0; attempt < 8; ++attempt) {
                    const uint32_t k = uint32_t(i * 8 + attempt);
                    const double ang = hashUnit(uint32_t(x), uint32_t(y) * 64u + k, seed) * 2 * M_PI;
                    const double dist = minR + hashUnit(uint32_t(x) * 64u + k, uint32_t(y), seed + 13u) * delta;
                    sx = x + std::cos(ang) * dist;
                    sy = y + std::sin(ang) * dist;
                    if (sx >= 0 && sx <= in.w && sy >= 0 && sy <= in.h)
                        break;
                }
                s[size_t(i)] = sampleBilinear(in, float(sx + 0.5), float(sy + 0.5));
            }
            d[x] = average(s.data(), samples);
        }
    });
}

// Máximo o mínimo deslizante en una dirección (algoritmo de la cola monótona).
void morphPass(const QImage& inImg, QImage& outImg, int radius, bool horizontal, bool dilate, const std::atomic_bool& cancel)
{
    const ConstPixels in(inImg);
    const Pixels out(outImg);
    const int n = horizontal ? in.w : in.h;
    const int lines = horizontal ? in.h : in.w;
    parallelRows(0, lines, [&](int l0, int l1) {
        std::vector<int> values(static_cast<size_t>(n));
        std::deque<int> dq;
        for (int line = l0; line < l1 && !cancel; ++line) {
            for (int c = 0; c < 4; ++c) {
                const int shift = c * 8;
                for (int i = 0; i < n; ++i)
                    values[size_t(i)] = int(((horizontal ? in.at(i, line) : in.at(line, i)) >> shift) & 0xff);
                dq.clear();
                int next = 0;
                for (int i = 0; i < n; ++i) {
                    // Ventana [i - r, i + r]
                    while (next < n && next <= i + radius) {
                        const int v = values[size_t(next)];
                        while (!dq.empty() && (dilate ? values[size_t(dq.back())] <= v : values[size_t(dq.back())] >= v))
                            dq.pop_back();
                        dq.push_back(next++);
                    }
                    while (dq.front() < i - radius)
                        dq.pop_front();
                    const int v = values[size_t(dq.front())];
                    QRgb& px = horizontal ? out.row(line)[i] : out.row(i)[line];
                    px = (px & ~(0xffu << shift)) | (uint32_t(v) << shift);
                }
            }
        }
    });
}

void renderMorphology(const RenderArgs& a)
{
    const bool dilate = ival(a.values, "mode") == 0;
    const int rw = ival(a.values, "width"), rh = ival(a.values, "height");
    const int m = std::max(rw, rh) + 1;
    const QRect big = a.roi.adjusted(-m, -m, m, m).intersected(a.src.rect());
    QImage t1 = a.src.copy(big), t2 = t1.copy();
    morphPass(t1, t2, rw, true, dilate, a.cancel);
    morphPass(t2, t1, rh, false, dilate, a.cancel);
    if (a.cancel)
        return;
    const ConstPixels res(t1);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        const QRgb* s = res.row(y - big.top());
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            // En premultiplicado cada color puede superar al alfa tras max/min.
            const QRgb p = s[x - big.left()];
            const int al = qAlpha(p);
            d[x] = qRgba(std::min(al, qRed(p)), std::min(al, qGreen(p)), std::min(al, qBlue(p)), al);
        }
    });
}

void renderPixelate(const RenderArgs& a)
{
    // Paint.NET 5: la rejilla pasa por el ancla; cada celda toma un color
    // (reducir a escala) y luego se amplía con el filtro elegido.
    const int s = std::max(1, ival(a.values, "cell"));
    const QPointF anchor = pval(a.values, "anchor");
    const int down = ival(a.values, "down");
    const int up = ival(a.values, "up");
    const double ax = a.roi.left() + a.roi.width() * (1 + anchor.x()) / 2;
    const double ay = a.roi.top() + a.roi.height() * (1 + anchor.y()) / 2;
    const int ox = int(std::floor(std::fmod(ax, double(s)))), oy = int(std::floor(std::fmod(ay, double(s))));
    const ConstPixels in(a.src);
    // Celdas que tocan la zona (con una de margen para interpolar).
    const int c0 = int(std::floor(double(a.roi.left() - ox) / s)) - 1;
    const int r0 = int(std::floor(double(a.roi.top() - oy) / s)) - 1;
    const int c1 = int(std::floor(double(a.roi.right() - ox) / s)) + 1;
    const int r1 = int(std::floor(double(a.roi.bottom() - oy) / s)) + 1;
    const int cols = c1 - c0 + 1, rows = r1 - r0 + 1;
    std::vector<QRgb> cells(size_t(cols) * rows);
    parallelRows(0, rows, [&](int y0, int y1) {
        for (int r = y0; r < y1; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int left = ox + (c0 + c) * s, top = oy + (r0 + r) * s;
                const QRect cell = QRect(left, top, s, s).intersected(QRect(0, 0, in.w, in.h));
                QRgb color = 0;
                if (!cell.isEmpty()) {
                    const float cx = float(left + s / 2.0), cy = float(top + s / 2.0);
                    switch (down) {
                    case 3: // Bicúbico: muestras cerca del centro
                    case 4: { // Bilineal
                        const float d = down == 3 ? float(s) / 4 : 0;
                        const QRgb q[4] = {sampleBilinear(in, cx - d, cy - d), sampleBilinear(in, cx + d, cy - d),
                                           sampleBilinear(in, cx - d, cy + d), sampleBilinear(in, cx + d, cy + d)};
                        color = average(q, 4);
                        break;
                    }
                    case 5: // Vecino más próximo
                        color = in.clamped(int(cx), int(cy));
                        break;
                    default: { // Anisotrópico, bicúbico de alta calidad, multimuestra: media de la celda
                        quint64 sum[4] = {0, 0, 0, 0};
                        for (int y = cell.top(); y <= cell.bottom(); ++y)
                            for (int x = cell.left(); x <= cell.right(); ++x) {
                                const QRgb p = in.at(x, y);
                                sum[0] += quint64(qRed(p)), sum[1] += quint64(qGreen(p)), sum[2] += quint64(qBlue(p)),
                                    sum[3] += quint64(qAlpha(p));
                            }
                        const quint64 n = quint64(cell.width()) * quint64(cell.height());
                        color = qRgba(int(sum[0] / n), int(sum[1] / n), int(sum[2] / n), int(sum[3] / n));
                        break;
                    }
                    }
                }
                cells[size_t(r) * cols + size_t(c)] = color;
            }
        }
    });
    auto cellAt = [&](int c, int r) {
        c = std::clamp(c, 0, cols - 1);
        r = std::clamp(r, 0, rows - 1);
        return cells[size_t(r) * cols + size_t(c)];
    };
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        QRgb* d = out.row(y);
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            // Posición en celdas (el centro de la celda c está en c + 0,5).
            const double fx = double(x + 0.5 - ox) / s - c0, fy = double(y + 0.5 - oy) / s - r0;
            if (up == 2) { // Vecino más próximo: celdas lisas
                d[x] = cellAt(int(std::floor(fx)), int(std::floor(fy)));
                continue;
            }
            const double gx = fx - 0.5, gy = fy - 0.5;
            const int ix = int(std::floor(gx)), iy = int(std::floor(gy));
            const double tx = gx - ix, ty = gy - iy;
            double acc[4] = {0, 0, 0, 0}, total = 0;
            const int reach = up == 0 ? 2 : 1; // bicúbico usa 4×4 celdas
            for (int j = 1 - reach; j <= reach; ++j)
                for (int i = 1 - reach; i <= reach; ++i) {
                    auto weight = [&](double t) {
                        t = std::abs(t);
                        if (up == 1)
                            return std::max(0.0, 1 - t);
                        // Catmull-Rom
                        return t < 1 ? 1.5 * t * t * t - 2.5 * t * t + 1
                                     : (t < 2 ? -0.5 * t * t * t + 2.5 * t * t - 4 * t + 2 : 0);
                    };
                    const double w = weight(i - tx) * weight(j - ty);
                    const QRgb p = cellAt(ix + i, iy + j);
                    acc[0] += qRed(p) * w, acc[1] += qGreen(p) * w, acc[2] += qBlue(p) * w, acc[3] += qAlpha(p) * w;
                    total += w;
                }
            auto ch = [&](double v) { return std::clamp(int(std::lround(v / total)), 0, 255); };
            const int al = ch(acc[3]);
            d[x] = qRgba(std::min(al, ch(acc[0])), std::min(al, ch(acc[1])), std::min(al, ch(acc[2])), al);
        }
    });
}

void renderPolarInversion(const RenderArgs& a)
{
    const double amount = dval(a.values, "amount");
    const double r2 = defaultRadius(a.roi) * defaultRadius(a.roi);
    WarpSetup setup;
    setup.offset = pval(a.values, "offset");
    setup.quality = ival(a.values, "quality");
    setup.edge = edgeValue(a.values, pdnEdges());
    warp(a, setup, [&](double x, double y) {
        const double dd = x * x + y * y;
        if (dd < 1e-9)
            return QPointF(x, y);
        const double inv = 1.0 + (r2 / dd - 1.0) * amount;
        return QPointF(x * inv, y * inv);
    });
}

void renderTileReflection(const RenderArgs& a)
{
    const double rotation = -dval(a.values, "angle");
    const double size = dval(a.values, "size");
    const double curvature = dval(a.values, "curvature");
    const double sn = std::sin(rotation * M_PI / 180.0), cs = std::cos(rotation * M_PI / 180.0);
    const double scale = M_PI / size;
    const double intensity = curvature * curvature / 10.0 * (curvature < 0 ? -1 : 1);
    int quality = ival(a.values, "quality");
    if (quality != 1)
        ++quality;
    const auto aa = rgssOffsets(quality);
    const EdgeBehavior edge = edgeValue(a.values, pdnEdges());
    const double hw = a.src.width() / 2.0, hh = a.src.height() / 2.0;
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(aa.size());
        QRgb* d = out.row(y);
        const double j = y - hh;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double i = x - hw;
            for (size_t p = 0; p < aa.size(); ++p) {
                const double u1 = i + aa[p].x(), v1 = j - aa[p].y();
                const double s1 = cs * u1 + sn * v1, t1 = -sn * u1 + cs * v1;
                const double s2 = s1 + intensity * std::tan(s1 * scale);
                const double t2 = t1 + intensity * std::tan(t1 * scale);
                const double u2 = cs * s2 - sn * t2, v2 = sn * s2 + cs * t2;
                samples[p] = sampleEdge(in, float(hw + u2 + 0.5), float(hh + v2 + 0.5), edge);
            }
            d[x] = average(samples.data(), int(samples.size()));
        }
    });
}

void renderTwist(const RenderArgs& a)
{
    const double amount = -dval(a.values, "amount");
    const double size = 1.0 / dval(a.values, "size");
    const QPointF off = pval(a.values, "offset");
    const double twist = amount * amount * (amount < 0 ? -1 : 1);
    const double hw = a.roi.left() + a.roi.width() / 2.0 * (1 + off.x());
    const double hh = a.roi.top() + a.roi.height() / 2.0 * (1 + off.y());
    const double invmax = 1.0 / std::min(a.roi.width() / 2.0, a.roi.height() / 2.0);
    const auto aa = rgssOffsets(ival(a.values, "quality"));
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(aa.size());
        QRgb* d = out.row(y);
        const double j = y - hh;
        for (int x = a.roi.left(); x <= a.roi.right(); ++x) {
            const double i = x - hw;
            for (size_t p = 0; p < aa.size(); ++p) {
                const double u = i + aa[p].x(), v = j + aa[p].y();
                const double rad = std::sqrt(u * u + v * v);
                double th = std::atan2(v, u);
                double t = 1 - rad * size * invmax;
                t = t < 0 ? 0 : t * t * t;
                th += t * twist / 100.0;
                samples[p] = sampleBilinear(in, float(hw + rad * std::cos(th) + 0.5), float(hh + rad * std::sin(th) + 0.5));
            }
            d[x] = average(samples.data(), int(samples.size()));
        }
    });
}

} // namespace

void registerDistort(QList<const Effect*>& list)
{
    using P = EffectParam;
    auto add = [&](SimpleEffect::Def def) { list.append(new SimpleEffect(std::move(def))); };
    add({"bulge", QObject::tr("Abultar"), catDistort(), {}, {},
         {P::real("amount", QObject::tr("Abultar"), -1, 1, 0.45, 2), P::offset("offset", QObject::tr("Centrar")),
          edgeParam(pdnEdges(), EdgeBehavior::Clamp), qualityParam(2)},
         renderBulge});
    add({"crystalize", QObject::tr("Cristalizar"), catDistort(), {}, {},
         {P::integer("cell", QObject::tr("Tamaño de celda"), 2, 250, 8), qualityParam(2), P::seed()},
         renderCrystalize});
    add({"dents", QObject::tr("Abolladuras"), catDistort(), {}, {},
         {P::real("scale", QObject::tr("Cambiar tamaño"), 1, 200, 25, 2),
          P::real("refraction", QObject::tr("Refracción"), 0, 200, 50, 2),
          P::real("roughness", QObject::tr("Detalle"), 0, 100, 10, 2),
          P::real("tension", QObject::tr("Turbulencia"), 0, 100, 10, 2), P::angle("angle", QObject::tr("Ángulo"), -180, 180, 0),
          qualityParam(2), P::seed()},
         renderDents});
    add({"frostedglass", QObject::tr("Cristal congelado"), catDistort(), {}, {},
         {P::real("max", QObject::tr("Radio de dispersión máximo"), 0, 200, 3, 2),
          P::real("min", QObject::tr("Radio de dispersión mínimo"), 0, 200, 0, 2),
          P::real("diffusion", QObject::tr("Difusión"), 0, 3, 1, 2),
          P::integer("smoothness", QObject::tr("Suavizado"), 1, 8, 2), P::seed()},
         renderFrostedGlass});
    add({"morphology", QObject::tr("Morfología"), catDistort(), {}, {},
         {P::integer("width", QObject::tr("Anchura"), 1, 100, 5).linked("linked"),
          P::integer("height", QObject::tr("Altura"), 1, 100, 5).linked("linked"),
          P::boolean("linked", QObject::tr("Vinculado"), true),
          P::choice("mode", QObject::tr("Modo"), {QObject::tr("Dilatar"), QObject::tr("Erosionar")}, 0)},
         renderMorphology});
    add({"pixelate", QObject::tr("Pixelado"), catDistort(), "zoom-pixels", {},
         {P::integer("cell", QObject::tr("Tamaño de celda"), 1, 100, 2), P::offset("anchor", QObject::tr("Ancla")),
          P::choice("down", QObject::tr("Reducir a escala"),
                    {QObject::tr("Anisotrópico"), QObject::tr("Bicúbico (calidad alta)"), QObject::tr("Multimuestra bilineal"),
                     QObject::tr("Bicúbico"), QObject::tr("Bilineal"), QObject::tr("Vecino más próximo")},
                    2),
          P::choice("up", QObject::tr("Aumentar a escala"),
                    {QObject::tr("Bicúbico"), QObject::tr("Bilineal"), QObject::tr("Vecino más próximo")}, 2)},
         renderPixelate});
    add({"polarinversion", QObject::tr("Inversión polar"), catDistort(), {}, {},
         {P::real("amount", QObject::tr("Escala"), -4, 4, 1, 2), P::offset("offset", QObject::tr("Desplazamiento")),
          edgeParam(pdnEdges(), EdgeBehavior::Reflect), qualityParam(2)},
         renderPolarInversion});
    add({"tilereflection", QObject::tr("Reflejo en mosaico"), catDistort(), {}, {},
         {P::angle("angle", QObject::tr("Ángulo"), -180, 180, 30), P::real("size", QObject::tr("Tamaño del mosaico"), 1, 800, 40, 2),
          P::real("curvature", QObject::tr("Curvatura"), -100, 100, 8, 2), edgeParam(pdnEdges(), EdgeBehavior::Reflect),
          qualityParam(2)},
         renderTileReflection});
    add({"twist", QObject::tr("Giro"), catDistort(), "draw-spiral", {},
         {P::integer("amount", QObject::tr("Cantidad y dirección"), -200, 200, 30),
          P::real("size", QObject::tr("Tamaño"), 0.01, 2.0, 1.0, 2), P::offset("offset", QObject::tr("Centrar")),
          qualityParam(2)},
         renderTwist});
}

} // namespace fx
