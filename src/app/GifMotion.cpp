#include "app/GifMotion.h"

#include "app/GifEditor.h"
#include "app/Icons.h"
#include "core/Document.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace GifEdit {

void paintDiamond(QPainter& p, QPointF c, double r, bool selected, bool filled, const QPalette& pal)
{
    QPolygonF d{QPointF(c.x(), c.y() - r), QPointF(c.x() + r, c.y()), QPointF(c.x(), c.y() + r), QPointF(c.x() - r, c.y())};
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QColor accent = pal.color(QPalette::Highlight);
    const QColor fill = selected ? accent : (filled ? QColor(250, 250, 250) : QColor(0, 0, 0, 0));
    p.setBrush(fill);
    // El borde con el color del texto (se ve en temas claros y oscuros).
    QColor outline = pal.color(QPalette::WindowText);
    if (filled && !selected)
        outline = QColor(60, 60, 60);
    p.setPen(QPen(selected ? accent.darker(160) : outline, 1.2));
    p.drawPolygon(d);
    p.restore();
}

void fillInterpMenu(QMenu* menu, Motion::Interp current, bool easeIn, bool easeOut,
                    const std::function<void(Motion::Interp)>& setInterp, const std::function<void(bool, bool)>& setEase)
{
    auto* group = new QActionGroup(menu);
    const std::pair<Motion::Interp, QString> kinds[] = {
        {Motion::Interp::Linear, GifEditor::tr("Lineal")},
        {Motion::Interp::Curve, GifEditor::tr("Curva")},
        {Motion::Interp::Continuous, GifEditor::tr("Curva continua")},
        {Motion::Interp::Freeze, GifEditor::tr("Congelar")},
        {Motion::Interp::Bezier, GifEditor::tr("Bézier (con asas)")},
    };
    for (const auto& [kind, label] : kinds) {
        QAction* a = menu->addAction(label, menu, [setInterp, kind] { setInterp(kind); });
        a->setCheckable(true);
        a->setChecked(kind == current);
        group->addAction(a);
    }
    menu->addSeparator();
    QAction* in = menu->addAction(GifEditor::tr("Entrada suave"), menu, [setEase](bool on) { setEase(true, on); });
    in->setCheckable(true);
    in->setChecked(easeIn);
    in->setToolTip(GifEditor::tr("Llega a este punto despacio"));
    QAction* out = menu->addAction(GifEditor::tr("Salida suave"), menu, [setEase](bool on) { setEase(false, on); });
    out->setCheckable(true);
    out->setChecked(easeOut);
    out->setToolTip(GifEditor::tr("Sale de este punto despacio"));
}

// ---------------------------------------------------------------------------
// Gráfica de un valor a lo largo del fotograma
// ---------------------------------------------------------------------------

namespace {

// Los colores de las curvas: la de X (o la de un valor solo) y la de Y, en «Posición XY».
const QColor kCurveColors[] = {QColor(64, 200, 170), QColor(240, 150, 60)};

// El tramo de 'track' en el que cae 't' (el índice de su primer punto), o -1.
int segmentAt(const Motion::Track& track, double t)
{
    for (int i = 0; i + 1 < track.keys.size(); ++i)
        if (t >= track.keys[i].t && t <= track.keys[i + 1].t)
            return i;
    return -1;
}

// La curva Bézier que da (o que más se parece a) la forma de un tramo: así, al estirarlo, empieza
// donde estaba.
std::array<double, 4> bezierOf(const Motion::Track& track, int i)
{
    const Motion::Key& a = track.keys[i];
    const Motion::Key& b = track.keys[i + 1];
    switch (a.interp) {
    case Motion::Interp::Bezier: return a.bezier;
    case Motion::Interp::Curve: return {1.0 / 3, 0, 2.0 / 3, 1};
    case Motion::Interp::Continuous: {
        // La de Hermite con las pendientes de Catmull-Rom, exacta como Bézier.
        const double dv = b.value - a.value;
        if (std::abs(dv) < 1e-9)
            break;
        auto slope = [&](int k) {
            if (k <= 0 || k + 1 >= track.keys.size())
                return 0.0;
            const double dt = track.keys[k + 1].t - track.keys[k - 1].t;
            return dt <= 0 ? 0.0 : (track.keys[k + 1].value - track.keys[k - 1].value) / dt;
        };
        const double span = std::max(1, b.t - a.t);
        return {1.0 / 3, slope(i) * span / 3 / dv, 2.0 / 3, 1 - slope(i + 1) * span / 3 / dv};
    }
    default: break;
    }
    return {1.0 / 3, 1.0 / 3, 2.0 / 3, 2.0 / 3};
}

// El tramo i de 'track' pasa a ser Bézier con la curva 'b' (sin la entrada y salida suaves, que ya
// dice la curva).
void setBezier(Motion::Track& track, int i, const std::array<double, 4>& b)
{
    if (i < 0 || i + 1 >= track.keys.size())
        return;
    track.keys[i].interp = Motion::Interp::Bezier;
    track.keys[i].bezier = b;
    track.keys[i].easeOut = false;
    track.keys[i + 1].easeIn = false;
}

// En «Posición XY», lo que se cambia en un tramo de una se cambia igual en el de la otra, si tiene
// sus puntos en los mismos momentos.
int sameSegment(const Motion::Track& track, int ta, int tb)
{
    const int i = track.keyAt(ta);
    return i >= 0 && i + 1 < track.keys.size() && track.keys[i + 1].t == tb ? i : -1;
}

} // namespace

KeyGraph::KeyGraph(GifEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setMinimumHeight(110);
    setMouseTracking(true);
    setToolTip(GifEditor::tr("Cómo cambia el valor a lo largo del fotograma. Doble clic: un punto clave ahí. Arrastra un rombo "
                             "para cambiar su valor o su momento, y la línea entre dos para curvarla (con Alt, para "
                             "moverla entera); las asas de una curva Bézier la ajustan. Clic derecho: la forma de la curva."));
}

void KeyGraph::setProp(int prop)
{
    m_prop = prop;
    update();
}

QList<Motion::Prop> KeyGraph::props() const
{
    if (m_prop < 0)
        return {Motion::PosX, Motion::PosY};
    return {Motion::Prop(m_prop)};
}

QRectF KeyGraph::plotRect() const
{
    return QRectF(rect()).adjusted(34, 8, -10, -16);
}

Motion::Clip KeyGraph::shownClip() const
{
    return m_dragging ? m_dragResult : m_editor->motion(m_editor->selectedFrame());
}

void KeyGraph::range(double* lo, double* hi) const
{
    // (Mientras se arrastra, la de antes: la escala no se mueve sola.)
    const Motion::Clip clip = m_dragging ? m_dragBase : m_editor->motion(m_editor->selectedFrame());
    double a = std::numeric_limits<double>::max(), b = std::numeric_limits<double>::lowest();
    for (Motion::Prop prop : props()) {
        const Motion::Track& track = clip.tracks[prop];
        a = std::min(a, track.base);
        b = std::max(b, track.base);
        for (int i = 0; i < track.keys.size(); ++i) {
            a = std::min(a, track.keys[i].value);
            b = std::max(b, track.keys[i].value);
            // (Y las asas de las curvas Bézier, que se ven.)
            if (track.keys[i].interp == Motion::Interp::Bezier && i + 1 < track.keys.size()) {
                const double dv = track.keys[i + 1].value - track.keys[i].value;
                for (double y : {track.keys[i].bezier[1], track.keys[i].bezier[3]}) {
                    a = std::min(a, track.keys[i].value + y * dv);
                    b = std::max(b, track.keys[i].value + y * dv);
                }
            }
        }
    }
    if (m_dragging) {
        a = std::min(a, m_dragLo);
        b = std::max(b, m_dragHi);
    }
    const double pad = std::max(1.0, (b - a) * 0.15);
    *lo = a - pad;
    *hi = b + pad;
}

QPointF KeyGraph::pointFor(double t, double v, double lo, double hi) const
{
    const QRectF r = plotRect();
    const int duration = std::max(1, m_editor->frameDelay(m_editor->selectedFrame()));
    return {r.left() + r.width() * std::clamp(t / duration, 0.0, 1.0), r.bottom() - r.height() * (v - lo) / (hi - lo)};
}

int KeyGraph::timeAt(double x) const
{
    const QRectF r = plotRect();
    const int duration = std::max(1, m_editor->frameDelay(m_editor->selectedFrame()));
    return std::clamp(int(std::round(duration * (x - r.left()) / std::max(1.0, r.width()))), 0, duration);
}

double KeyGraph::valueAt(double y, double lo, double hi) const
{
    const QRectF r = plotRect();
    return lo + (hi - lo) * (r.bottom() - y) / std::max(1.0, r.height());
}

QPointF KeyGraph::handlePoint(const Motion::Track& track, int i, int handle, double lo, double hi) const
{
    const Motion::Key& a = track.keys[i];
    const Motion::Key& b = track.keys[i + 1];
    const double x = handle == 0 ? a.bezier[0] : a.bezier[2], y = handle == 0 ? a.bezier[1] : a.bezier[3];
    return pointFor(a.t + x * (b.t - a.t), a.value + y * (b.value - a.value), lo, hi);
}

KeyGraph::Hit KeyGraph::hitAt(QPointF pos) const
{
    Hit hit;
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return hit;
    const Motion::Clip clip = m_editor->motion(frame);
    double lo, hi;
    range(&lo, &hi);
    // Primero las asas, después los puntos y, al final, las líneas entre ellos.
    for (Motion::Prop prop : props()) {
        const Motion::Track& track = clip.tracks[prop];
        for (int i = 0; i + 1 < track.keys.size(); ++i)
            if (track.keys[i].interp == Motion::Interp::Bezier)
                for (int h : {0, 1})
                    if (QLineF(handlePoint(track, i, h, lo, hi), pos).length() <= 6)
                        return {Hit::Handle, prop, i, h};
    }
    for (Motion::Prop prop : props()) {
        const Motion::Track& track = clip.tracks[prop];
        for (int i = 0; i < track.keys.size(); ++i)
            if (QLineF(pointFor(track.keys[i].t, track.keys[i].value, lo, hi), pos).length() <= 7)
                return {Hit::Key, prop, i, 0};
    }
    const int t = timeAt(pos.x());
    for (Motion::Prop prop : props()) {
        const Motion::Track& track = clip.tracks[prop];
        const int i = segmentAt(track, t);
        if (i >= 0 && std::abs(pointFor(t, track.value(t), lo, hi).y() - pos.y()) <= 5)
            return {Hit::Segment, prop, i, 0};
    }
    return hit;
}

void KeyGraph::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette pal = palette();
    p.fillRect(rect(), pal.color(QPalette::Base));
    const QRectF r = plotRect();
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return;
    const Motion::Clip clip = shownClip();
    double lo, hi;
    range(&lo, &hi);
    // Cuadrícula y escala
    QFont small = font();
    small.setPointSizeF(std::max(6.5, small.pointSizeF() * 0.8));
    p.setFont(small);
    const int decimals = hi - lo < 8 ? 1 : 0;
    for (int i = 0; i <= 4; ++i) {
        const double v = lo + (hi - lo) * i / 4;
        const double y = r.bottom() - r.height() * i / 4;
        p.setPen(QPen(pal.color(QPalette::Mid), 1, Qt::DotLine));
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRectF(0, y - 7, r.left() - 4, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'f', decimals));
    }
    const QList<Motion::Prop> shown = props();
    const int duration = std::max(1, m_editor->frameDelay(frame));
    p.setRenderHint(QPainter::Antialiasing);
    bool anyKeys = false;
    for (int n = 0; n < shown.size(); ++n) {
        const Motion::Track& track = clip.tracks[shown[n]];
        anyKeys = anyKeys || !track.keys.isEmpty();
        // La curva
        QPainterPath curve;
        for (int x = 0; x <= int(r.width()); ++x) {
            const int t = int(std::round(duration * x / r.width()));
            const QPointF pt = pointFor(t, track.value(t), lo, hi);
            if (x == 0)
                curve.moveTo(pt);
            else
                curve.lineTo(pt);
        }
        p.setPen(QPen(kCurveColors[n], 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(curve);
        // Las asas de las curvas Bézier: una línea desde su punto y un círculo.
        for (int i = 0; i + 1 < track.keys.size(); ++i) {
            if (track.keys[i].interp != Motion::Interp::Bezier)
                continue;
            for (int h : {0, 1}) {
                const QPointF from = h == 0 ? pointFor(track.keys[i].t, track.keys[i].value, lo, hi)
                                            : pointFor(track.keys[i + 1].t, track.keys[i + 1].value, lo, hi);
                const QPointF at = handlePoint(track, i, h, lo, hi);
                QColor line = kCurveColors[n];
                line.setAlpha(150);
                p.setPen(QPen(line, 1));
                p.drawLine(from, at);
                const bool active = m_dragging && m_drag.kind == Hit::Handle && m_drag.prop == shown[n] && m_drag.key == i
                                    && m_drag.handle == h;
                p.setPen(QPen(kCurveColors[n].darker(140), 1.2));
                p.setBrush(active ? pal.color(QPalette::Highlight) : QColor(250, 250, 250));
                p.drawEllipse(at, 4, 4);
            }
        }
    }
    // El cabezal
    const double px = pointFor(m_editor->localTime(), lo, lo, hi).x();
    p.setPen(QPen(QColor(230, 60, 60), 1.5));
    p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
    // Los puntos
    for (Motion::Prop prop : shown)
        for (const Motion::Key& k : clip.tracks[prop].keys)
            paintDiamond(p, pointFor(k.t, k.value, lo, hi), 5.5, k.t == m_editor->selectedKeyTime(), true, pal);
    // En «Posición XY», qué curva es cuál.
    if (shown.size() > 1) {
        p.setFont(small);
        double x = r.left() + 6;
        for (int n = 0; n < shown.size(); ++n) {
            p.setPen(kCurveColors[n]);
            const QString label = n == 0 ? QStringLiteral("X") : QStringLiteral("Y");
            p.drawText(QPointF(x, r.top() + 10), label);
            x += p.fontMetrics().horizontalAdvance(label) + 8;
        }
    }
    if (!anyKeys) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
                   GifEditor::tr("Sin puntos clave: pulsa el rombo de un valor para poner uno en el cabezal o haz doble clic aquí."));
    }
}

void KeyGraph::showMenu(int frame, int time, const QList<Motion::Prop>& on, bool removable, QPoint globalPos)
{
    // La forma del tramo que empieza en 'time' (en «Posición XY», en las dos).
    const Motion::Clip clip = m_editor->motion(frame);
    const Motion::Track& first = clip.tracks[on.first()];
    const int i = first.keyAt(time);
    if (i < 0)
        return;
    const Motion::Key k = first.keys[i];
    QMenu menu(this);
    auto change = [this, frame, time, on](const std::function<void(Motion::Track&, int)>& fn, const QString& text) {
        Motion::Clip c = m_editor->motion(frame);
        for (Motion::Prop prop : on)
            if (const int at = c.tracks[prop].keyAt(time); at >= 0)
                fn(c.tracks[prop], at);
        m_editor->setMotion(frame, c, text);
    };
    fillInterpMenu(
        &menu, k.interp, k.easeIn, k.easeOut,
        [change](Motion::Interp interp) {
            change([interp](Motion::Track& track, int at) {
                // A Bézier, con la forma que tenía (luego se ajusta con las asas).
                if (interp == Motion::Interp::Bezier && at + 1 < track.keys.size() && track.keys[at].interp != interp)
                    setBezier(track, at, bezierOf(track, at));
                track.keys[at].interp = interp;
            }, GifEditor::tr("Forma de la curva"));
        },
        [change](bool in, bool on) {
            change([in, on](Motion::Track& track, int at) { (in ? track.keys[at].easeIn : track.keys[at].easeOut) = on; },
                   GifEditor::tr("Forma de la curva"));
        });
    if (removable) {
        menu.addSeparator();
        menu.addAction(Icons::theme({"edit-delete"}), GifEditor::tr("Quitar el punto"), this, [change] {
            change([](Motion::Track& track, int at) {
                if (track.keys.size() == 1)
                    track.base = track.keys[at].value;
                track.keys.removeAt(at);
            }, GifEditor::tr("Quitar punto clave"));
        });
    }
    menu.exec(globalPos);
}

void KeyGraph::mousePressEvent(QMouseEvent* e)
{
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return;
    const Hit hit = hitAt(e->position());
    const Motion::Clip clip = m_editor->motion(frame);
    if (hit.kind == Hit::None) {
        // Clic en la gráfica: el cabezal va a ese momento.
        if (e->button() == Qt::LeftButton)
            m_editor->seekLocal(timeAt(e->position().x()));
        return;
    }
    const Motion::Track& track = clip.tracks[hit.prop];
    // En «Posición XY», lo de un punto o un tramo vale para las dos.
    const QList<Motion::Prop> linked = props();
    if (e->button() == Qt::RightButton) {
        const int time = track.keys[hit.key].t;
        m_editor->selectKeyTime(time);
        // (Sobre la línea entre dos, la forma del tramo: la del punto donde empieza.)
        showMenu(frame, time, linked, hit.kind == Hit::Key, e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (hit.kind == Hit::Key)
        m_editor->selectKeyTime(track.keys[hit.key].t);
    m_drag = hit;
    m_dragging = true;
    m_dragBase = clip;
    m_dragResult = clip;
    m_pressPos = e->position();
    range(&m_dragLo, &m_dragHi);
    // Un tramo plano no se puede curvar (no avanza): se mueve entero. Con Alt, siempre.
    m_moveSegment = hit.kind == Hit::Segment
                    && ((e->modifiers() & Qt::AltModifier)
                        || std::abs(track.keys[hit.key + 1].value - track.keys[hit.key].value) < 1e-9);
    if (hit.kind == Hit::Segment)
        m_bendStart = bezierOf(track, hit.key);
}

void KeyGraph::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging) {
        const Hit hit = hitAt(e->position());
        setCursor(hit.kind == Hit::Key || hit.kind == Hit::Handle ? Qt::PointingHandCursor
                  : hit.kind == Hit::Segment                      ? ((e->modifiers() & Qt::AltModifier) ? Qt::SizeAllCursor : Qt::SizeVerCursor)
                                                                  : Qt::ArrowCursor);
        return;
    }
    const int frame = m_editor->selectedFrame();
    const int duration = std::max(1, m_editor->frameDelay(frame));
    // (Con la escala de cuando se pulsó: la gráfica no se mueve sola mientras se arrastra.)
    double lo, hi;
    range(&lo, &hi);
    Motion::Clip c = m_dragBase;
    Motion::Track& track = c.tracks[m_drag.prop];
    const QList<Motion::Prop> shown = props();
    const QPointF pos = e->position();
    if (m_drag.kind == Hit::Key) {
        // El punto: su momento (de 10 en 10 ms) y su valor; en «Posición XY», el momento también el
        // de la otra.
        const int from = track.keys[m_drag.key].t;
        const int t = std::clamp(int(std::round(timeAt(pos.x()) / 10.0)) * 10, 0, duration);
        double v = valueAt(pos.y(), lo, hi);
        if (m_drag.prop == Motion::Opacity)
            v = std::clamp(v, 0.0, 100.0);
        v = std::round(v * 10) / 10;
        for (Motion::Prop prop : shown) {
            Motion::Track& tr = c.tracks[prop];
            const int i = tr.keyAt(from);
            if (i < 0)
                continue;
            Motion::Key k = tr.keys.takeAt(i);
            k.t = t;
            if (prop == m_drag.prop)
                k.value = v;
            if (const int existing = tr.keyAt(t); existing >= 0)
                tr.keys.removeAt(existing);
            auto it = std::lower_bound(tr.keys.begin(), tr.keys.end(), t, [](const Motion::Key& a, int time) { return a.t < time; });
            tr.keys.insert(it, k);
        }
        m_dragT = t;
    } else if (m_drag.kind == Hit::Handle) {
        // Un asa de la curva: dónde está en el tiempo del tramo (de 0 a 1) y cuánto avanza ahí.
        const Motion::Key& a = track.keys[m_drag.key];
        const Motion::Key& b = track.keys[m_drag.key + 1];
        std::array<double, 4> bz = a.bezier;
        const double x = std::clamp((timeAt(pos.x()) - a.t) / double(std::max(1, b.t - a.t)), 0.0, 1.0);
        const double dv = b.value - a.value;
        const int h = m_drag.handle * 2;
        bz[size_t(h)] = x;
        if (std::abs(dv) > 1e-9)
            bz[size_t(h + 1)] = std::clamp((valueAt(pos.y(), lo, hi) - a.value) / dv, -3.0, 4.0);
        const int ta = a.t, tb = b.t;
        for (Motion::Prop prop : shown)
            setBezier(c.tracks[prop], sameSegment(c.tracks[prop], ta, tb), bz);
    } else if (m_drag.kind == Hit::Segment && m_moveSegment) {
        // Mover el tramo entero: sus dos puntos, arriba o abajo y en el tiempo (entre los de al lado).
        const int i = m_drag.key;
        const int prevT = i > 0 ? track.keys[i - 1].t + 10 : 0;
        const int nextT = i + 2 < track.keys.size() ? track.keys[i + 2].t - 10 : duration;
        const int ta = track.keys[i].t, tb = track.keys[i + 1].t;
        const int dt = std::clamp(int(std::round((timeAt(pos.x()) - timeAt(m_pressPos.x())) / 10.0)) * 10, prevT - ta, nextT - tb);
        double dv = valueAt(pos.y(), lo, hi) - valueAt(m_pressPos.y(), lo, hi);
        dv = std::round(dv * 10) / 10;
        for (int j : {i, i + 1}) {
            track.keys[j].t += dt;
            track.keys[j].value += dv;
            if (m_drag.prop == Motion::Opacity)
                track.keys[j].value = std::clamp(track.keys[j].value, 0.0, 100.0);
        }
        // (En «Posición XY», los puntos de la otra en esos momentos se mueven en el tiempo con ellos.)
        for (Motion::Prop prop : shown) {
            if (prop == m_drag.prop)
                continue;
            if (const int j = sameSegment(c.tracks[prop], ta, tb); j >= 0) {
                c.tracks[prop].keys[j].t += dt;
                c.tracks[prop].keys[j + 1].t += dt;
            }
        }
    } else if (m_drag.kind == Hit::Segment) {
        // Curvar el tramo: arriba o abajo cambia cuánto avanza donde se agarró (las dos asas, más la
        // que está más cerca); a los lados, cuándo (las dos asas a la vez).
        const Motion::Key& a = track.keys[m_drag.key];
        const Motion::Key& b = track.keys[m_drag.key + 1];
        const double span = std::max(1, b.t - a.t), dv = b.value - a.value;
        const double s = std::clamp((timeAt(m_pressPos.x()) - a.t) / span, 0.15, 0.85);
        const double dp = (valueAt(pos.y(), lo, hi) - valueAt(m_pressPos.y(), lo, hi)) / dv;
        const double k = 1.0 / (3 * (1 - s) * s * ((1 - s) * (1 - s) + s * s));
        const double du = (timeAt(pos.x()) - timeAt(m_pressPos.x())) / span;
        std::array<double, 4> bz = m_bendStart;
        bz[0] = std::clamp(bz[0] + du, 0.0, 1.0);
        bz[2] = std::clamp(bz[2] + du, 0.0, 1.0);
        bz[1] = std::clamp(bz[1] + dp * k * (1 - s), -3.0, 4.0);
        bz[3] = std::clamp(bz[3] + dp * k * s, -3.0, 4.0);
        const int ta = a.t, tb = b.t;
        for (Motion::Prop prop : shown)
            setBezier(c.tracks[prop], sameSegment(c.tracks[prop], ta, tb), bz);
    }
    m_dragResult = c;
    // La línea de tiempo, la vista previa y los valores siguen a lo que se arrastra.
    m_editor->setMotionPreview(frame, c);
    update();
}

void KeyGraph::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_dragging)
        return;
    const int frame = m_editor->selectedFrame();
    const Motion::Clip c = m_dragResult;
    const Hit drag = m_drag;
    const bool changed = !(c == m_dragBase);
    m_dragging = false;
    m_editor->clearMotionPreview();
    if (!changed) {
        update();
        return;
    }
    m_editor->setMotion(frame, c, drag.kind == Hit::Key       ? GifEditor::tr("Mover punto clave")
                                  : drag.kind == Hit::Segment && m_moveSegment ? GifEditor::tr("Mover el tramo")
                                                                             : GifEditor::tr("Forma de la curva"));
    // El punto (y el cabezal) donde quedó.
    if (drag.kind == Hit::Key)
        m_editor->selectKeyTime(m_dragT);
}

void KeyGraph::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int frame = m_editor->selectedFrame();
    const QRectF r = plotRect();
    if (frame < 0 || e->button() != Qt::LeftButton || r.width() <= 0 || r.height() <= 0 || hitAt(e->position()).kind == Hit::Key
        || !r.adjusted(-6, -6, 6, 6).contains(e->position()))
        return;
    // Como al arrastrar un rombo: de 10 en 10 ms, con la escala que se ve.
    const int t = std::clamp(int(std::round(timeAt(e->position().x()) / 10.0)) * 10, 0, std::max(1, m_editor->frameDelay(frame)));
    double lo, hi;
    range(&lo, &hi);
    double v = valueAt(e->position().y(), lo, hi);
    Motion::Clip c = m_editor->motion(frame);
    // En «Posición XY», el valor del ratón para la curva más cercana y la otra con el que ya tiene ahí.
    const QList<Motion::Prop> shown = props();
    Motion::Prop nearest = shown.first();
    double best = std::numeric_limits<double>::max();
    for (Motion::Prop prop : shown)
        if (const double d = std::abs(pointFor(t, c.tracks[prop].value(t), lo, hi).y() - e->position().y()); d < best) {
            best = d;
            nearest = prop;
        }
    for (Motion::Prop prop : shown) {
        double value = prop == nearest ? v : c.tracks[prop].value(t);
        if (prop == Motion::Opacity)
            value = std::clamp(value, 0.0, 100.0);
        value = std::round(value * 10) / 10;
        c.tracks[prop].setKey(t, value);
        if (c.lockScale && (prop == Motion::ScaleX || prop == Motion::ScaleY))
            c.tracks[prop == Motion::ScaleX ? Motion::ScaleY : Motion::ScaleX].setKey(t, value);
    }
    m_editor->setMotion(frame, c, GifEditor::tr("Añadir punto clave"));
    m_editor->selectKeyTime(t);
    e->accept();
}

// ---------------------------------------------------------------------------
// Panel de la derecha
// ---------------------------------------------------------------------------

Inspector::Inspector(GifEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_title = new QLabel(this);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    root->addWidget(m_title);
    // Como en Filmora: un punto clave con todos los valores (posición, escala, rotación y opacidad)
    // en el cabezal; si ya hay uno ahí, lo quita.
    m_globalKey = new QToolButton(this);
    m_globalKey->setText(GifEditor::tr("Punto clave global"));
    m_globalKey->setToolTip(GifEditor::tr("Pone un punto clave con todos los valores (posición, escala, rotación y opacidad) en "
                                          "el cabezal, o lo quita si ya hay uno ahí (K)"));
    m_globalKey->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_globalKey->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_globalKey->setMinimumHeight(30);
    connect(m_globalKey, &QToolButton::clicked, this, [this] { m_editor->toggleGlobalKey(); });
    root->addWidget(m_globalKey);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(3);
    int row = 0;
    auto section = [&](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setFont(bold);
        grid->addWidget(l, row++, 0, 1, 5);
    };
    section(GifEditor::tr("Escala"));
    addRow(grid, row++, Motion::ScaleX, QStringLiteral("X"), QStringLiteral(" %"), 1, 2000, 1);
    addRow(grid, row++, Motion::ScaleY, QStringLiteral("Y"), QStringLiteral(" %"), 1, 2000, 1);
    m_lock = new QCheckBox(GifEditor::tr("Proporcional"), this);
    m_lock->setToolTip(GifEditor::tr("Al cambiar la escala X o Y, la otra cambia igual"));
    connect(m_lock, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        Motion::Clip c = m_editor->motion(m_editor->selectedFrame());
        c.lockScale = on;
        m_editor->setMotion(m_editor->selectedFrame(), c, GifEditor::tr("Escala proporcional"));
    });
    grid->addWidget(m_lock, row++, 1, 1, 4);
    section(GifEditor::tr("Posición"));
    addRow(grid, row++, Motion::PosX, QStringLiteral("X"), QStringLiteral(" px"), -20000, 20000, 1);
    addRow(grid, row++, Motion::PosY, QStringLiteral("Y"), QStringLiteral(" px"), -20000, 20000, 1);
    section(GifEditor::tr("Rotación"));
    addRow(grid, row++, Motion::Rotation, QString(), QStringLiteral("°"), -3600, 3600, 1);
    // La imagen como una lámina fina que se inclina, con perspectiva (en la vista previa, con el modo 3D).
    section(GifEditor::tr("Giro 3D"));
    addRow(grid, row++, Motion::RotX, QStringLiteral("X"), QStringLiteral("°"), -89, 89, 1);
    addRow(grid, row++, Motion::RotY, QStringLiteral("Y"), QStringLiteral("°"), -89, 89, 1);
    section(GifEditor::tr("Opacidad"));
    addRow(grid, row++, Motion::Opacity, QString(), QStringLiteral(" %"), 0, 100, 0);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);

    auto* graphRow = new QHBoxLayout;
    graphRow->addWidget(new QLabel(GifEditor::tr("Gráfica:"), this));
    m_graphProp = new QComboBox(this);
    // Primero (y elegida) X e Y juntas, como en Filmora.
    m_graphProp->addItem(GifEditor::tr("Posición XY"), -1);
    const std::pair<Motion::Prop, QString> props[] = {
        {Motion::PosX, GifEditor::tr("Posición X")}, {Motion::PosY, GifEditor::tr("Posición Y")},
        {Motion::ScaleX, GifEditor::tr("Escala X")},  {Motion::ScaleY, GifEditor::tr("Escala Y")},
        {Motion::Rotation, GifEditor::tr("Rotación")}, {Motion::Opacity, GifEditor::tr("Opacidad")},
        {Motion::RotX, GifEditor::tr("Giro 3D X")},    {Motion::RotY, GifEditor::tr("Giro 3D Y")},
    };
    for (const auto& [p, label] : props)
        m_graphProp->addItem(label, int(p));
    graphRow->addWidget(m_graphProp, 1);
    root->addLayout(graphRow);
    m_graph = new KeyGraph(editor, this);
    connect(m_graphProp, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_graph->setProp(m_graphProp->itemData(i).toInt()); });
    root->addWidget(m_graph, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel(GifEditor::tr("Al exportar:"), this));
    m_fps = new QComboBox(this);
    for (int fps : {10, 12, 15, 20, 24, 25, 30, 50, 60})
        m_fps->addItem(GifEditor::tr("%1 fps").arg(fps), 1000 / fps);
    m_fps->setToolTip(GifEditor::tr("Cuántas imágenes por segundo tiene el GIF mientras este fotograma se mueve"));
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        Motion::Clip c = m_editor->motion(m_editor->selectedFrame());
        c.stepMs = m_fps->itemData(i).toInt();
        m_editor->setMotion(m_editor->selectedFrame(), c, GifEditor::tr("Fotogramas por segundo"));
    });
    bottom->addWidget(m_fps, 1);
    root->addLayout(bottom);
    // Al girar, escalar, inclinar o deformar: suavizada o con sus píxeles nítidos (de fábrica, para el
    // pixel art: suavizada se veía borrosa).
    m_smooth = new QCheckBox(GifEditor::tr("Suavizar al girar o escalar"), this);
    m_smooth->setToolTip(GifEditor::tr("Sin marcar, la imagen conserva sus píxeles nítidos al girarla o escalarla (para el pixel art)."));
    connect(m_smooth, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating)
            return;
        Motion::Clip c = m_editor->motion(m_editor->selectedFrame());
        c.smooth = on;
        m_editor->setMotion(m_editor->selectedFrame(), c, GifEditor::tr("Suavizar"));
    });
    root->addWidget(m_smooth);

    auto* actions = new QHBoxLayout;
    m_reset = new QToolButton(this);
    m_reset->setIcon(Icons::theme({"edit-undo", "view-refresh"}));
    m_reset->setText(GifEditor::tr("Restablecer"));
    m_reset->setToolTipDuration(-1);
    m_reset->setToolTip(GifEditor::tr("Quita la animación de este fotograma"));
    m_reset->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_reset, &QToolButton::clicked, this,
            [this] { m_editor->setMotion(m_editor->selectedFrame(), Motion::Clip(), GifEditor::tr("Quitar la animación")); });
    m_bake = new QToolButton(this);
    m_bake->setIcon(Icons::theme({"insert-image", "list-add"}));
    m_bake->setText(GifEditor::tr("Convertir en fotogramas"));
    m_bake->setToolTip(GifEditor::tr("Sustituye este fotograma por los que salen de su animación (como al exportar), para "
                                     "editarlos uno a uno"));
    m_bake->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_bake, &QToolButton::clicked, this, [this] { m_editor->bakeMotion(m_editor->selectedFrame()); });
    actions->addWidget(m_reset);
    actions->addWidget(m_bake);
    actions->addStretch(1);
    root->addLayout(actions);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);
    m_hint->setText(GifEditor::tr("El rombo pone o quita un punto clave en el cabezal. Con puntos, cambiar un valor pone uno "
                                  "ahí. En la línea de tiempo, separa los rombos para que el movimiento dure más."));
    root->addWidget(m_hint);
    refresh();
}

QIcon Inspector::diamondIcon(bool filled) const
{
    QPixmap pm(QSize(16, 16) * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintDiamond(p, QPointF(8, 8), 6, filled, false, palette());
    return QIcon(pm);
}

void Inspector::addRow(QGridLayout* grid, int row, Motion::Prop prop, const QString& label, const QString& suffix, double min,
                       double max, int decimals)
{
    Row& r = m_rows[prop];
    grid->addWidget(new QLabel(label, this), row, 0);
    r.spin = new QDoubleSpinBox(this);
    r.spin->setRange(min, max);
    r.spin->setDecimals(decimals);
    r.spin->setSuffix(suffix);
    r.spin->setKeyboardTracking(false);
    r.spin->setAccelerated(true);
    connect(r.spin, &QDoubleSpinBox::valueChanged, this, [this, prop](double v) {
        if (!m_updating)
            valueEdited(prop, v);
    });
    grid->addWidget(r.spin, row, 1);
    auto button = [this](const QString& tip) {
        auto* b = new QToolButton(this);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setIconSize(QSize(14, 14));
        return b;
    };
    r.prev = button(GifEditor::tr("Punto clave anterior"));
    r.prev->setArrowType(Qt::LeftArrow);
    r.diamond = button(GifEditor::tr("Pone o quita un punto clave en el cabezal"));
    r.next = button(GifEditor::tr("Punto clave siguiente"));
    r.next->setArrowType(Qt::RightArrow);
    connect(r.prev, &QToolButton::clicked, this, [this, prop] { jump(prop, -1); });
    connect(r.next, &QToolButton::clicked, this, [this, prop] { jump(prop, 1); });
    connect(r.diamond, &QToolButton::clicked, this, [this, prop] { toggleKey(prop); });
    grid->addWidget(r.prev, row, 2);
    grid->addWidget(r.diamond, row, 3);
    grid->addWidget(r.next, row, 4);
}

void Inspector::refresh()
{
    const int frame = m_editor->selectedFrame();
    const bool has = frame >= 0;
    setEnabled(has);
    m_title->setText(has ? GifEditor::tr("Animación del fotograma %1").arg(frame + 1) : GifEditor::tr("Animación"));
    const Motion::Clip clip = m_editor->motion(frame);
    m_updating = true;
    m_lock->setChecked(clip.lockScale);
    const int fps = m_fps->findData(clip.stepMs);
    m_fps->setCurrentIndex(fps >= 0 ? fps : std::max(0, m_fps->findData(50)));
    m_smooth->setChecked(clip.smooth);
    m_reset->setEnabled(has && !clip.isEmpty());
    m_bake->setEnabled(has && clip.animated());
    m_updating = false;
    refreshValues();
    m_graph->update();
}

void Inspector::refreshValues()
{
    const int frame = m_editor->selectedFrame();
    const Motion::Clip clip = m_editor->motion(frame);
    const int t = m_editor->localTime();
    m_updating = true;
    for (int p = 0; p < Motion::PropCount; ++p) {
        Row& r = m_rows[p];
        const Motion::Track& track = clip.tracks[p];
        if (!r.spin->hasFocus())
            r.spin->setValue(track.value(t));
        r.diamond->setIcon(diamondIcon(track.keyAt(t) >= 0));
        bool before = false, after = false;
        for (const Motion::Key& k : track.keys) {
            before = before || k.t < t;
            after = after || k.t > t;
        }
        r.prev->setEnabled(before);
        r.next->setEnabled(after);
    }
    m_globalKey->setIcon(diamondIcon(m_editor->hasKeyAtPlayhead()));
    m_globalKey->setText(m_editor->hasKeyAtPlayhead() ? GifEditor::tr("Quitar el punto clave global")
                                                      : GifEditor::tr("Punto clave global"));
    m_updating = false;
    m_graph->update();
}

void Inspector::valueEdited(Motion::Prop prop, double value)
{
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return;
    Motion::Clip c = m_editor->motion(frame);
    const int t = m_editor->localTime();
    auto set = [&](Motion::Prop p, double v) {
        Motion::Track& track = c.tracks[p];
        // Con puntos clave, el valor se pone como un punto en el cabezal; sin ellos, es el de siempre.
        if (track.animated())
            track.setKey(t, v);
        else
            track.base = v;
    };
    set(prop, value);
    if (c.lockScale && (prop == Motion::ScaleX || prop == Motion::ScaleY))
        set(prop == Motion::ScaleX ? Motion::ScaleY : Motion::ScaleX, value);
    m_editor->setMotion(frame, c, GifEditor::tr("Cambiar la animación"));
}

void Inspector::toggleKey(Motion::Prop prop)
{
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return;
    Motion::Clip c = m_editor->motion(frame);
    const int t = m_editor->localTime();
    auto toggle = [&](Motion::Prop p) {
        Motion::Track& track = c.tracks[p];
        const int i = track.keyAt(t);
        if (i >= 0) {
            if (track.keys.size() == 1)
                track.base = track.keys[i].value;
            track.keys.removeAt(i);
        } else {
            track.setKey(t, track.value(t));
        }
    };
    const bool adding = c.tracks[prop].keyAt(t) < 0;
    toggle(prop);
    if (c.lockScale && (prop == Motion::ScaleX || prop == Motion::ScaleY)) {
        const Motion::Prop other = prop == Motion::ScaleX ? Motion::ScaleY : Motion::ScaleX;
        if ((c.tracks[other].keyAt(t) < 0) == adding)
            toggle(other);
    }
    m_editor->selectKeyTime(t);
    m_editor->setMotion(frame, c, adding ? GifEditor::tr("Añadir punto clave") : GifEditor::tr("Quitar punto clave"));
}

void Inspector::jump(Motion::Prop prop, int direction)
{
    const Motion::Track track = m_editor->motion(m_editor->selectedFrame()).tracks[prop];
    const int t = m_editor->localTime();
    int best = -1;
    for (const Motion::Key& k : track.keys)
        if ((direction < 0 && k.t < t && (best < 0 || k.t > best)) || (direction > 0 && k.t > t && (best < 0 || k.t < best)))
            best = k.t;
    if (best >= 0)
        m_editor->selectKeyTime(best);
}

// ---------------------------------------------------------------------------
// Peso y duración del GIF
// ---------------------------------------------------------------------------

SizeMeter::SizeMeter(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize SizeMeter::sizeHint() const
{
    return {fontMetrics().horizontalAdvance(QStringLiteral("≈ 00,0 MB · 00:00,0 s")) + 96, std::max(22, fontMetrics().height() + 6)};
}

void SizeMeter::setEstimate(qint64 bytes, int durationMs, int frames)
{
    m_bytes = bytes;
    m_duration = durationMs;
    m_frames = frames;
    const QLocale locale;
    const double mb = std::max<qint64>(0, bytes) / (1024.0 * 1024.0);
    QString tip = GifEditor::tr("Peso aproximado del GIF exportado: %1 MB, %2 fotogramas, %3 s.")
                      .arg(locale.toString(mb, 'f', 1))
                      .arg(frames)
                      .arg(locale.toString(durationMs / 1000.0, 'f', 1));
    if (bytes >= kLimit)
        tip += QLatin1Char('\n') + GifEditor::tr("Pesa mucho: Discord no deja enviar más de 10 MB sin Nitro y tarda en cargar. "
                                                  "Acorta la animación, reduce el tamaño de la imagen, usa menos fotogramas por segundo "
                                                  "o expórtala como vídeo MP4.");
    else if (bytes >= kLimit * 6 / 10)
        tip += QLatin1Char('\n') + GifEditor::tr("Se acerca a los 10 MB (el límite de Discord sin Nitro).");
    setToolTip(tip);
    update();
}

void SizeMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF bar(1, height() / 2.0 - 4, 84, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Mid));
    p.drawRoundedRect(bar, 4, 4);
    const double f = m_bytes < 0 ? 0 : std::clamp(double(m_bytes) / kLimit, 0.0, 1.0);
    const QColor color = m_bytes >= kLimit ? QColor(220, 50, 47) : m_bytes >= kLimit * 6 / 10 ? QColor(230, 170, 20) : QColor(60, 180, 90);
    if (f > 0) {
        p.setBrush(color);
        p.drawRoundedRect(QRectF(bar.left(), bar.top(), std::max(8.0, bar.width() * f), bar.height()), 4, 4);
    }
    const QLocale locale;
    const QString text = m_bytes < 0 ? QStringLiteral("…")
                                     : QStringLiteral("%1%2 MB · %3 s")
                                           .arg(m_bytes >= kLimit ? QStringLiteral("⚠ ") : QStringLiteral("≈ "))
                                           .arg(locale.toString(m_bytes / (1024.0 * 1024.0), 'f', 1))
                                           .arg(locale.toString(m_duration / 1000.0, 'f', 1));
    p.setPen(m_bytes >= kLimit ? color : pal.color(QPalette::WindowText));
    p.drawText(QRectF(bar.right() + 6, 0, width() - bar.right() - 6, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
}

}  // namespace GifEdit
