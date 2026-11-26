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

KeyGraph::KeyGraph(GifEditor* editor, QWidget* parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setMinimumHeight(110);
    setMouseTracking(true);
    setToolTip(GifEditor::tr("Cómo cambia el valor a lo largo del fotograma. Arrastra un rombo para cambiar su valor o su "
                             "momento; clic derecho para la forma de la curva."));
}

void KeyGraph::setProp(Motion::Prop prop)
{
    m_prop = prop;
    update();
}

QRectF KeyGraph::plotRect() const
{
    return QRectF(rect()).adjusted(34, 8, -10, -16);
}

Motion::Clip KeyGraph::dragged() const
{
    Motion::Clip clip = m_dragBase;
    Motion::Track& track = clip.tracks[m_prop];
    if (m_drag >= 0 && m_drag < track.keys.size()) {
        Motion::Key k = track.keys.takeAt(m_drag);
        k.t = m_dragT;
        k.value = m_dragV;
        if (const int existing = track.keyAt(k.t); existing >= 0)
            track.keys.removeAt(existing);
        auto it = std::lower_bound(track.keys.begin(), track.keys.end(), k.t,
                                   [](const Motion::Key& a, int time) { return a.t < time; });
        track.keys.insert(it, k);
    }
    return clip;
}

void KeyGraph::range(double* lo, double* hi) const
{
    // (Mientras se arrastra, la de antes: la escala no se mueve sola.)
    const Motion::Clip clip = m_drag >= 0 ? m_dragBase : m_editor->motion(m_editor->selectedFrame());
    const Motion::Track& track = clip.tracks[m_prop];
    double a = track.base, b = track.base;
    for (const Motion::Key& k : track.keys) {
        a = std::min(a, k.value);
        b = std::max(b, k.value);
    }
    if (m_drag >= 0) {
        a = std::min(a, m_dragLo);
        b = std::max(b, m_dragHi);
    }
    const double pad = std::max(1.0, (b - a) * 0.15);
    *lo = a - pad;
    *hi = b + pad;
}

QPointF KeyGraph::pointFor(int t, double v, double lo, double hi) const
{
    const QRectF r = plotRect();
    const int duration = std::max(1, m_editor->frameDelay(m_editor->selectedFrame()));
    return {r.left() + r.width() * std::clamp(double(t) / duration, 0.0, 1.0), r.bottom() - r.height() * (v - lo) / (hi - lo)};
}

int KeyGraph::keyAt(QPointF pos) const
{
    const Motion::Track track = m_editor->motion(m_editor->selectedFrame()).tracks[m_prop];
    double lo, hi;
    range(&lo, &hi);
    for (int i = 0; i < track.keys.size(); ++i)
        if (QLineF(pointFor(track.keys[i].t, track.keys[i].value, lo, hi), pos).length() <= 7)
            return i;
    return -1;
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
    const Motion::Clip clip = m_drag >= 0 ? dragged() : m_editor->motion(frame);
    const Motion::Track& track = clip.tracks[m_prop];
    double lo, hi;
    range(&lo, &hi);
    // Cuadrícula y escala
    p.setPen(QPen(pal.color(QPalette::Mid), 1, Qt::DotLine));
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
    // La curva
    const int duration = std::max(1, m_editor->frameDelay(frame));
    QPainterPath curve;
    for (int x = 0; x <= int(r.width()); ++x) {
        const int t = int(std::round(duration * x / r.width()));
        const QPointF pt = pointFor(t, track.value(t), lo, hi);
        if (x == 0)
            curve.moveTo(pt);
        else
            curve.lineTo(pt);
    }
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(64, 200, 170), 2));
    p.drawPath(curve);
    // El cabezal
    const int now = m_editor->localTime();
    const double px = pointFor(now, lo, lo, hi).x();
    p.setPen(QPen(QColor(230, 60, 60), 1.5));
    p.drawLine(QPointF(px, r.top()), QPointF(px, r.bottom()));
    // Los puntos
    for (int i = 0; i < track.keys.size(); ++i) {
        const Motion::Key& k = track.keys[i];
        paintDiamond(p, pointFor(k.t, k.value, lo, hi), 5.5, k.t == m_editor->selectedKeyTime(), true, pal);
    }
    if (track.keys.isEmpty()) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
                   GifEditor::tr("Sin puntos clave: pulsa el rombo de un valor para poner uno en el cabezal."));
    }
}

void KeyGraph::mousePressEvent(QMouseEvent* e)
{
    const int frame = m_editor->selectedFrame();
    if (frame < 0)
        return;
    const int i = keyAt(e->position());
    const Motion::Clip clip = m_editor->motion(frame);
    if (i < 0) {
        // Clic en la gráfica: el cabezal va a ese momento.
        const QRectF r = plotRect();
        if (e->button() == Qt::LeftButton && r.width() > 0)
            m_editor->seekLocal(int(std::round(m_editor->frameDelay(frame) * std::clamp((e->position().x() - r.left()) / r.width(), 0.0, 1.0))));
        return;
    }
    const Motion::Key k = clip.tracks[m_prop].keys[i];
    m_editor->selectKeyTime(k.t);
    if (e->button() == Qt::RightButton) {
        QMenu menu(this);
        const Motion::Prop prop = m_prop;
        fillInterpMenu(
            &menu, k.interp, k.easeIn, k.easeOut,
            [this, frame, prop, i](Motion::Interp interp) {
                Motion::Clip c = m_editor->motion(frame);
                if (i < c.tracks[prop].keys.size())
                    c.tracks[prop].keys[i].interp = interp;
                m_editor->setMotion(frame, c, GifEditor::tr("Forma de la curva"));
            },
            [this, frame, prop, i](bool in, bool on) {
                Motion::Clip c = m_editor->motion(frame);
                if (i < c.tracks[prop].keys.size())
                    (in ? c.tracks[prop].keys[i].easeIn : c.tracks[prop].keys[i].easeOut) = on;
                m_editor->setMotion(frame, c, GifEditor::tr("Forma de la curva"));
            });
        menu.addSeparator();
        menu.addAction(Icons::theme({"edit-delete"}), GifEditor::tr("Quitar el punto"), this, [this, frame, prop, i] {
            Motion::Clip c = m_editor->motion(frame);
            if (i < c.tracks[prop].keys.size()) {
                if (c.tracks[prop].keys.size() == 1)
                    c.tracks[prop].base = c.tracks[prop].keys[i].value;
                c.tracks[prop].keys.removeAt(i);
            }
            m_editor->setMotion(frame, c, GifEditor::tr("Quitar punto clave"));
        });
        menu.exec(e->globalPosition().toPoint());
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    m_dragBase = clip;
    range(&m_dragLo, &m_dragHi);
    m_drag = i;
    m_dragT = k.t;
    m_dragV = k.value;
}

void KeyGraph::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0) {
        setCursor(keyAt(e->position()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    const QRectF r = plotRect();
    const int duration = std::max(1, m_editor->frameDelay(m_editor->selectedFrame()));
    // De 10 en 10 ms y con la escala fija mientras se arrastra (si no, la gráfica se movía sola).
    m_dragT = std::clamp(int(std::round(duration * (e->position().x() - r.left()) / r.width() / 10.0)) * 10, 0, duration);
    m_dragV = m_dragLo + (m_dragHi - m_dragLo) * (r.bottom() - e->position().y()) / r.height();
    if (m_prop == Motion::Opacity)
        m_dragV = std::clamp(m_dragV, 0.0, 100.0);
    m_dragV = std::round(m_dragV * 10) / 10;
    // La línea de tiempo, la vista previa y los valores siguen al punto mientras se arrastra.
    m_editor->setMotionPreview(m_editor->selectedFrame(), dragged());
    update();
}

void KeyGraph::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag < 0)
        return;
    const int frame = m_editor->selectedFrame();
    const Motion::Clip c = dragged();
    const int t = m_dragT;
    m_drag = -1;
    m_editor->clearMotionPreview();
    m_editor->setMotion(frame, c, GifEditor::tr("Mover punto clave"));
    // El punto (y el cabezal) donde quedó.
    m_editor->selectKeyTime(t);
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
    section(GifEditor::tr("Opacidad"));
    addRow(grid, row++, Motion::Opacity, QString(), QStringLiteral(" %"), 0, 100, 0);
    grid->setColumnStretch(1, 1);
    root->addLayout(grid);

    auto* graphRow = new QHBoxLayout;
    graphRow->addWidget(new QLabel(GifEditor::tr("Gráfica:"), this));
    m_graphProp = new QComboBox(this);
    const std::pair<Motion::Prop, QString> props[] = {
        {Motion::PosX, GifEditor::tr("Posición X")}, {Motion::PosY, GifEditor::tr("Posición Y")},
        {Motion::ScaleX, GifEditor::tr("Escala X")},  {Motion::ScaleY, GifEditor::tr("Escala Y")},
        {Motion::Rotation, GifEditor::tr("Rotación")}, {Motion::Opacity, GifEditor::tr("Opacidad")},
    };
    for (const auto& [p, label] : props)
        m_graphProp->addItem(label, int(p));
    graphRow->addWidget(m_graphProp, 1);
    root->addLayout(graphRow);
    m_graph = new KeyGraph(editor, this);
    connect(m_graphProp, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_graph->setProp(Motion::Prop(m_graphProp->itemData(i).toInt())); });
    root->addWidget(m_graph, 1);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel(GifEditor::tr("Al exportar:"), this));
    m_fps = new QComboBox(this);
    for (int fps : {10, 15, 20, 25, 30, 50})
        m_fps->addItem(GifEditor::tr("%1 fps").arg(fps), 1000 / fps);
    m_fps->setToolTip(GifEditor::tr("Cuántas imágenes por segundo tiene el GIF mientras este fotograma se mueve"));
    connect(m_fps, &QComboBox::activated, this, [this](int i) {
        Motion::Clip c = m_editor->motion(m_editor->selectedFrame());
        c.stepMs = m_fps->itemData(i).toInt();
        m_editor->setMotion(m_editor->selectedFrame(), c, GifEditor::tr("Fotogramas por segundo"));
    });
    bottom->addWidget(m_fps, 1);
    root->addLayout(bottom);

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
    m_fps->setCurrentIndex(fps >= 0 ? fps : 2);
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
