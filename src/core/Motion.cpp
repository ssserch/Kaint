#include "core/Motion.h"

#include "core/Layer.h"

#include <QJsonArray>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace Motion {

namespace {

// 0..1 dentro del tramo, con la salida del primero y la llegada del segundo más lentas si se pide.
double eased(double u, bool easeOut, bool easeIn)
{
    if (easeOut && easeIn)
        return u * u * (3 - 2 * u);
    if (easeOut)
        return u * u;
    if (easeIn)
        return 1 - (1 - u) * (1 - u);
    return u;
}

// Pendiente (valor por ms) de la curva continua en el punto i (Catmull-Rom; en los extremos, 0).
double slope(const QList<Key>& keys, int i)
{
    if (i <= 0 || i + 1 >= keys.size())
        return 0;
    const double dt = keys[i + 1].t - keys[i - 1].t;
    return dt <= 0 ? 0 : (keys[i + 1].value - keys[i - 1].value) / dt;
}

}  // namespace

double bezierProgress(double u, const std::array<double, 4>& b)
{
    u = std::clamp(u, 0.0, 1.0);
    const double x1 = std::clamp(b[0], 0.0, 1.0), y1 = b[1], x2 = std::clamp(b[2], 0.0, 1.0), y2 = b[3];
    auto curve = [](double s, double p1, double p2) {
        const double r = 1 - s;
        return 3 * r * r * s * p1 + 3 * r * s * s * p2 + s * s * s;
    };
    // El parámetro s con x(s) = u: Newton y, si no converge, bisección (x es creciente: x1 y x2 están
    // entre 0 y 1).
    double s = u;
    for (int i = 0; i < 8; ++i) {
        const double x = curve(s, x1, x2) - u;
        const double r = 1 - s;
        const double dx = 3 * r * r * x1 + 6 * r * s * (x2 - x1) + 3 * s * s * (1 - x2);
        if (std::abs(x) < 1e-7)
            return curve(s, y1, y2);
        if (std::abs(dx) < 1e-6)
            break;
        s = std::clamp(s - x / dx, 0.0, 1.0);
    }
    double lo = 0, hi = 1;
    s = u;
    for (int i = 0; i < 40; ++i) {
        const double x = curve(s, x1, x2);
        if (std::abs(x - u) < 1e-7)
            break;
        (x < u ? lo : hi) = s;
        s = (lo + hi) / 2;
    }
    return curve(s, y1, y2);
}

double Track::value(int t) const
{
    if (keys.isEmpty())
        return base;
    if (t <= keys.first().t)
        return keys.first().value;
    if (t >= keys.last().t)
        return keys.last().value;
    int i = 0;
    while (i + 1 < keys.size() && keys[i + 1].t <= t)
        ++i;
    const Key& a = keys[i];
    const Key& b = keys[i + 1];
    const double span = std::max(1, b.t - a.t);
    if (a.interp == Interp::Bezier)
        return a.value + (b.value - a.value) * bezierProgress((t - a.t) / span, a.bezier);
    const double u = eased((t - a.t) / span, a.easeOut, b.easeIn);
    switch (a.interp) {
    case Interp::Freeze: return a.value;
    case Interp::Linear: return a.value + (b.value - a.value) * u;
    case Interp::Curve: return a.value + (b.value - a.value) * (u * u * (3 - 2 * u));
    case Interp::Continuous: {
        // Hermite cúbica con las pendientes de Catmull-Rom.
        const double m0 = slope(keys, i) * span;
        const double m1 = slope(keys, i + 1) * span;
        const double u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * a.value + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * b.value + (u3 - u2) * m1;
    }
    case Interp::Bezier: break;
    }
    return a.value;
}

int Track::keyAt(int t) const
{
    for (int i = 0; i < keys.size(); ++i)
        if (keys[i].t == t)
            return i;
    return -1;
}

void Track::setKey(int t, double v)
{
    const int i = keyAt(t);
    if (i >= 0) {
        keys[i].value = v;
        return;
    }
    Key k;
    k.t = t;
    k.value = v;
    // Con la forma del punto anterior (la que se eligió para este tramo).
    auto it = std::lower_bound(keys.begin(), keys.end(), t, [](const Key& a, int time) { return a.t < time; });
    if (it != keys.begin()) {
        k.interp = (it - 1)->interp;
        k.bezier = (it - 1)->bezier;
    }
    keys.insert(it, k);
}

bool Transform::isIdentity() const
{
    return x == 0 && y == 0 && scaleX == 100 && scaleY == 100 && std::fmod(rotation, 360) == 0 && opacity >= 100
        && rotX == 0 && rotY == 0 && std::all_of(warp.begin(), warp.end(), [](const QPointF& p) { return p.isNull(); });
}

QTransform tiltTransform(const QRectF& rect, double ax, double ay)
{
    if (ax == 0 && ay == 0)
        return {};
    const QPointF c = rect.center();
    const double d = 1.5 * std::max({rect.width(), rect.height(), 1.0});
    const double a = ax * M_PI / 180.0, b = ay * M_PI / 180.0;
    const QTransform ry(std::cos(b), 0, -std::sin(b) / d, 0, 1, 0, 0, 0, 1);
    const QTransform rx(1, 0, 0, 0, std::cos(a), -std::sin(a) / d, 0, 0, 1);
    return QTransform::fromTranslate(-c.x(), -c.y()) * ry * rx * QTransform::fromTranslate(c.x(), c.y());
}

QTransform matrix(QSize size, const Transform& tr)
{
    const QRectF rect(QPointF(0, 0), QSizeF(size));
    QTransform warp;
    if (!std::all_of(tr.warp.begin(), tr.warp.end(), [](const QPointF& p) { return p.isNull(); })) {
        const QPolygonF from{rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
        const QPolygonF to{rect.topLeft() + tr.warp[0], rect.topRight() + tr.warp[1], rect.bottomRight() + tr.warp[2],
                           rect.bottomLeft() + tr.warp[3]};
        if (!QTransform::quadToQuad(from, to, warp))
            warp = QTransform();
    }
    const QPointF center = rect.center();
    QTransform m;
    m.translate(center.x() + tr.x, center.y() + tr.y);
    m.rotate(tr.rotation);
    m.scale(tr.scaleX / 100.0, tr.scaleY / 100.0);
    m.translate(-center.x(), -center.y());
    return warp * tiltTransform(rect, tr.rotX, tr.rotY) * m;
}

double defaultValue(Prop p)
{
    switch (p) {
    case ScaleX:
    case ScaleY:
    case Opacity: return 100;
    default: return 0;
    }
}

QString propName(Prop p)
{
    static const char* names[] = {"x", "y", "scaleX", "scaleY", "rotation", "opacity", "rotX", "rotY"};
    return QString::fromLatin1(names[p]);
}

Clip::Clip()
{
    for (int p = 0; p < PropCount; ++p)
        tracks[p].base = defaultValue(Prop(p));
}

bool Clip::isEmpty() const
{
    for (int p = 0; p < PropCount; ++p)
        if (tracks[p].animated() || tracks[p].base != defaultValue(Prop(p)))
            return false;
    return std::all_of(warp.begin(), warp.end(), [](const QPointF& p) { return p.isNull(); });
}

bool Clip::animated() const
{
    for (const Track& t : tracks)
        if (t.keys.size() > 1)
            return true;
    return false;
}

Transform Clip::sample(int t) const
{
    Transform tr;
    tr.x = tracks[PosX].value(t);
    tr.y = tracks[PosY].value(t);
    tr.scaleX = tracks[ScaleX].value(t);
    tr.scaleY = tracks[ScaleY].value(t);
    tr.rotation = tracks[Rotation].value(t);
    tr.opacity = std::clamp(tracks[Opacity].value(t), 0.0, 100.0);
    tr.rotX = std::clamp(tracks[RotX].value(t), -89.0, 89.0);
    tr.rotY = std::clamp(tracks[RotY].value(t), -89.0, 89.0);
    tr.warp = warp;
    tr.smooth = smooth;
    return tr;
}

QList<int> Clip::keyTimes() const
{
    QList<int> times;
    for (const Track& track : tracks)
        for (const Key& k : track.keys)
            if (!times.contains(k.t))
                times << k.t;
    std::sort(times.begin(), times.end());
    return times;
}

void Clip::moveKeys(int from, int to)
{
    if (from == to)
        return;
    for (Track& track : tracks) {
        const int i = track.keyAt(from);
        if (i < 0)
            continue;
        Key k = track.keys.takeAt(i);
        const int existing = track.keyAt(to);
        if (existing >= 0)
            track.keys.removeAt(existing);
        k.t = to;
        auto it = std::lower_bound(track.keys.begin(), track.keys.end(), to, [](const Key& a, int time) { return a.t < time; });
        track.keys.insert(it, k);
    }
}

void Clip::removeKeys(int t)
{
    for (Track& track : tracks) {
        const int i = track.keyAt(t);
        if (i < 0)
            continue;
        // Sin puntos, el valor que tenía se queda como el de siempre.
        if (track.keys.size() == 1)
            track.base = track.keys[i].value;
        track.keys.removeAt(i);
    }
}

void Clip::setInterp(int t, Interp interp)
{
    for (Track& track : tracks)
        if (const int i = track.keyAt(t); i >= 0)
            track.keys[i].interp = interp;
}

void Clip::setEase(int t, bool in, bool on)
{
    for (Track& track : tracks)
        if (const int i = track.keyAt(t); i >= 0)
            (in ? track.keys[i].easeIn : track.keys[i].easeOut) = on;
}

int Clip::lastKeyTime() const
{
    const QList<int> times = keyTimes();
    return times.isEmpty() ? 0 : times.last();
}

QJsonObject Clip::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("step"), stepMs);
    o.insert(QStringLiteral("lockScale"), lockScale);
    if (smooth)
        o.insert(QStringLiteral("smooth"), true);
    if (!std::all_of(warp.begin(), warp.end(), [](const QPointF& p) { return p.isNull(); })) {
        QJsonArray w;
        for (const QPointF& p : warp)
            w << p.x() << p.y();
        o.insert(QStringLiteral("warp"), w);
    }
    QJsonObject tracksJson;
    for (int p = 0; p < PropCount; ++p) {
        const Track& track = tracks[p];
        if (!track.animated() && track.base == defaultValue(Prop(p)))
            continue;
        QJsonObject t;
        t.insert(QStringLiteral("base"), track.base);
        QJsonArray keys;
        for (const Key& k : track.keys) {
            QJsonObject kj{{QStringLiteral("t"), k.t}, {QStringLiteral("v"), k.value}};
            if (k.interp != Interp::Linear)
                kj.insert(QStringLiteral("i"), int(k.interp));
            if (k.easeIn)
                kj.insert(QStringLiteral("in"), true);
            if (k.easeOut)
                kj.insert(QStringLiteral("out"), true);
            if (k.interp == Interp::Bezier)
                kj.insert(QStringLiteral("b"), QJsonArray{k.bezier[0], k.bezier[1], k.bezier[2], k.bezier[3]});
            keys.append(kj);
        }
        if (!keys.isEmpty())
            t.insert(QStringLiteral("keys"), keys);
        tracksJson.insert(propName(Prop(p)), t);
    }
    o.insert(QStringLiteral("tracks"), tracksJson);
    return o;
}

Clip Clip::fromJson(const QJsonObject& o)
{
    Clip c;
    c.stepMs = std::clamp(o.value(QStringLiteral("step")).toInt(50), 10, 1000);
    c.lockScale = o.value(QStringLiteral("lockScale")).toBool(true);
    c.smooth = o.value(QStringLiteral("smooth")).toBool(false);
    if (const QJsonArray w = o.value(QStringLiteral("warp")).toArray(); w.size() == 8)
        for (int i = 0; i < 4; ++i)
            c.warp[size_t(i)] = QPointF(w[2 * i].toDouble(), w[2 * i + 1].toDouble());
    const QJsonObject tracksJson = o.value(QStringLiteral("tracks")).toObject();
    for (int p = 0; p < PropCount; ++p) {
        const QJsonObject t = tracksJson.value(propName(Prop(p))).toObject();
        if (t.isEmpty())
            continue;
        Track& track = c.tracks[p];
        track.base = t.value(QStringLiteral("base")).toDouble(defaultValue(Prop(p)));
        for (const QJsonValue& v : t.value(QStringLiteral("keys")).toArray()) {
            const QJsonObject kj = v.toObject();
            Key k;
            k.t = std::max(0, kj.value(QStringLiteral("t")).toInt());
            k.value = kj.value(QStringLiteral("v")).toDouble();
            k.interp = Interp(std::clamp(kj.value(QStringLiteral("i")).toInt(0), 0, 4));
            k.easeIn = kj.value(QStringLiteral("in")).toBool();
            k.easeOut = kj.value(QStringLiteral("out")).toBool();
            if (const QJsonArray b = kj.value(QStringLiteral("b")).toArray(); b.size() == 4)
                for (int j = 0; j < 4; ++j)
                    k.bezier[size_t(j)] = j % 2 == 0 ? std::clamp(b[j].toDouble(), 0.0, 1.0) : std::clamp(b[j].toDouble(), -3.0, 4.0);
            if (track.keyAt(k.t) < 0)
                track.keys << k;
        }
        std::sort(track.keys.begin(), track.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    }
    return c;
}

QImage render(const QImage& source, const Transform& tr)
{
    if (tr.isIdentity())
        return source;
    QImage out(source.size(), kPremulFormat);
    out.fill(Qt::transparent);
    QPainter p(&out);
    // (Suavizada solo si se pide: si no, al girar o escalar el pixel art se veía borroso.)
    p.setRenderHint(QPainter::SmoothPixmapTransform, tr.smooth);
    p.setRenderHint(QPainter::Antialiasing, tr.smooth);
    p.setOpacity(std::clamp(tr.opacity, 0.0, 100.0) / 100.0);
    p.setTransform(matrix(source.size(), tr));
    p.drawImage(0, 0, source);
    p.end();
    QImage straight = out.convertToFormat(kLayerFormat);
    // Lo que queda totalmente transparente, como el transparente de Kaint.
    for (int y = 0; y < straight.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(straight.scanLine(y));
        for (int x = 0; x < straight.width(); ++x)
            if (qAlpha(row[x]) == 0)
                row[x] = kTransparent;
    }
    return straight;
}

QList<std::pair<QImage, int>> expand(const QImage& source, const Clip& clip, int durationMs)
{
    QList<std::pair<QImage, int>> out;
    const int step = std::max(10, clip.stepMs);
    if (!clip.animated()) {
        out.append({render(source, clip.sample(0)), durationMs});
        return out;
    }
    for (int t = 0; t < durationMs; t += step) {
        const int length = std::min(step, durationMs - t);
        // Lo que sobre al final, menos de 20 ms (el mínimo de los GIF), se suma al anterior.
        if (length < 20 && !out.isEmpty()) {
            out.last().second += length;
            break;
        }
        out.append({render(source, clip.sample(t)), length});
    }
    return out;
}

}  // namespace Motion
