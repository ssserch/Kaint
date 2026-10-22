// Capas > Rotar/Zoom: giro, inclinación con perspectiva, desplazamiento y
// zoom de la capa, con mosaico opcional para rellenar lo que queda fuera.

#include "core/effects/EffectUtils.h"

#include <QTransform>
#include <QVector3D>

namespace fx {

namespace {

void renderRotateZoom(const RenderArgs& a)
{
    // Rotación y giro: ángulo, dirección de la inclinación e inclinación
    // (de -90 a 90°; en ±90 el plano se vería de canto, así que se limita).
    const QVector3D rotation = a.values.value(QStringLiteral("rotation")).value<QVector3D>();
    const double angle = rotation.x();
    const double direction = rotation.y();
    const double tilt = std::clamp(double(rotation.z()), -89.5, 89.5);
    const QPointF pan = pval(a.values, "pan");
    const double zoom = dval(a.values, "zoom");
    const int tiling = ival(a.values, "tiling");
    const bool nearest = ival(a.values, "sampling") == 1;
    const QRect r = a.roi;
    const QPointF c = QRectF(r).center();

    QTransform t;
    t.translate(c.x() + pan.x() * r.width() / 2.0, c.y() + pan.y() * r.height() / 2.0);
    t.rotate(-direction);
    t.rotate(tilt, Qt::XAxis);
    t.rotate(direction);
    t.rotate(angle);
    t.scale(zoom, zoom);
    t.translate(-c.x(), -c.y());
    bool ok = false;
    const QTransform inv = t.inverted(&ok);
    if (!ok)
        return;

    const auto aa = rgssOffsets(ival(a.values, "quality"));
    const ConstPixels in(a.src);
    const Pixels out(a.dst);
    auto fold = [&](double v, int lo, int size, bool mirror) {
        double rel = std::fmod(v - lo, 2.0 * size);
        if (rel < 0)
            rel += 2.0 * size;
        if (!mirror)
            return lo + std::fmod(rel, double(size));
        return lo + (rel < size ? rel : 2.0 * size - rel);
    };
    forRows(a, [&](int y) {
        std::vector<QRgb> samples(aa.size());
        QRgb* d = out.row(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            for (size_t p = 0; p < aa.size(); ++p) {
                QPointF s = inv.map(QPointF(x + 0.5 + aa[p].x(), y + 0.5 + aa[p].y()));
                const bool inside = s.x() >= r.left() && s.y() >= r.top() && s.x() < r.right() + 1 && s.y() < r.bottom() + 1;
                if (!inside) {
                    if (tiling == 0) {
                        samples[p] = 0;
                        continue;
                    }
                    s = QPointF(fold(s.x(), r.left(), r.width(), tiling == 2), fold(s.y(), r.top(), r.height(), tiling == 2));
                }
                samples[p] = nearest ? in.clamped(int(s.x()), int(s.y())) : sampleBilinear(in, float(s.x()), float(s.y()));
            }
            d[x] = average(samples.data(), int(samples.size()));
        }
    });
}

} // namespace

void registerLayerEffects(QList<const Effect*>& list)
{
    using P = EffectParam;
    // Mismo orden y textos que el diálogo Giro y zum de Paint.NET 5.
    SimpleEffect::Def def{"rotatezoom", QObject::tr("Giro y zum"), catLayer(), "transform-rotate",
                          QKeySequence(QStringLiteral("Ctrl+Shift+Z")),
                          {P::rollBall("rotation", QObject::tr("Rotación y giro")),
                           P::offset("pan", QObject::tr("Desplazamiento")),
                           P::real("zoom", QObject::tr("Zum"), 0.05, 16, 1, 2),
                           qualityParam(2),
                           P::choice("tiling", QObject::tr("Modo de mosaico"),
                                     {QObject::tr("Ninguno"), QObject::tr("Repetir"), QObject::tr("Reflejar")}, 0),
                           P::choice("sampling", QObject::tr("Muestreo"),
                                     {QObject::tr("Bilineal"), QObject::tr("Vecino más próximo")}, 0)},
                          renderRotateZoom};
    def.hidden = true;
    list.append(new SimpleEffect(std::move(def)));
}

} // namespace fx
