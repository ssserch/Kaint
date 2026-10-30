#include "app/tools/ShapeTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/tools/Handles.h"
#include "app/tools/MoveTools.h"
#include "core/Raster.h"

#include <QKeyEvent>
#include <QPainter>
#include <QPainterPathStroker>

#include <cmath>

void renderVector(LayerEdit& edit, const QRect& area, const VectorDrawFn& draw)
{
    const ToolSettings& s = AppState::instance()->tools();
    Raster::setSelectionAntialias(s.selectionAntialias);
    const QRect r = area.intersected(edit.document()->rect());
    edit.restorePrevious();
    if (r.isEmpty()) {
        edit.touched(QRect());
        return;
    }
    QImage overlay(r.size(), QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    QImage coverage(r.size(), QImage::Format_Alpha8);
    coverage.fill(0);
    {
        QPainter p(&overlay);
        p.setRenderHint(QPainter::Antialiasing, s.antialias);
        p.translate(-r.topLeft());
        draw(p, false);
    }
    {
        QPainter p(&coverage);
        p.setRenderHint(QPainter::Antialiasing, s.antialias);
        p.translate(-r.topLeft());
        draw(p, true);
    }
    Raster::compositeRegion(edit.image(), edit.before(), Raster::overlayToSource(overlay, coverage), r, s.blend,
                            edit.document()->selection(), coverage);
    edit.touched(r);
}

namespace {

QPointF snapAngle(const QPointF& origin, const QPointF& p)
{
    const QPointF d = p - origin;
    const qreal len = std::hypot(d.x(), d.y());
    const qreal angle = std::round(std::atan2(d.y(), d.x()) / (M_PI / 12)) * (M_PI / 12);
    return origin + QPointF(std::cos(angle), std::sin(angle)) * len;
}

bool isFinishKey(QKeyEvent* e)
{
    return e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape;
}

QPoint arrowDelta(QKeyEvent* e)
{
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return {};
    }
    return (e->modifiers() & Qt::ControlModifier) ? d * 10 : d;
}

// Pluma con el estilo de la barra (el ancho admite decimales).
QPen strokePen(const QBrush& brush, Qt::PenCapStyle cap)
{
    const ToolSettings& s = AppState::instance()->tools();
    QPen pen(brush, s.brushSize, s.dash, s.dash == Qt::SolidLine ? cap : Qt::FlatCap, Qt::RoundJoin);
    return pen;
}

// Punta de flecha en 'tip' apuntando en la dirección 'angleDeg' (de Qt).
QPainterPath arrowHead(const QPointF& tip, double angleDeg, double width, bool filled)
{
    const double size = std::max(8.0, width * 3);
    const double a = angleDeg * M_PI / 180.0;
    const QPointF dir(std::cos(a), -std::sin(a));
    const QPointF side(-dir.y(), dir.x());
    const QPointF back = tip - dir * size;
    QPainterPath p;
    if (filled) {
        p.addPolygon(QPolygonF{tip, back + side * size * 0.5, back - side * size * 0.5});
        p.closeSubpath();
    } else {
        p.moveTo(back + side * size * 0.5);
        p.lineTo(tip);
        p.lineTo(back - side * size * 0.5);
    }
    return p;
}

} // namespace

// ---------------------------------------------------------------------------
// Línea/Curva
// ---------------------------------------------------------------------------

namespace {

struct LineState : ToolStepState {
    std::array<QPointF, 4> pts;
    bool swapped = false;
};

struct ShapeState : ToolStepState {
    QRectF rect;
    QTransform transform;
    bool swapped = false;
};

// Opciones y colores de la herramienta en este momento.
template<typename State>
std::shared_ptr<State> captureState()
{
    auto st = std::make_shared<State>();
    st->settings = AppState::instance()->tools();
    st->primary = AppState::instance()->primary();
    st->secondary = AppState::instance()->secondary();
    return st;
}

// Vuelve a las opciones de un paso: solo las que usa la herramienta, y sus colores.
void restoreSettings(const ToolStepState& st, bool shape)
{
    AppState::instance()->updateTools([&](ToolSettings& s) {
        const ToolSettings& o = st.settings;
        s.brushSize = o.brushSize;
        s.dash = o.dash;
        s.antialias = o.antialias;
        s.blend = o.blend;
        s.fill = o.fill;
        if (shape) {
            s.shape = o.shape;
            s.customShape = o.customShape;
            s.shapeStyle = o.shapeStyle;
            s.cornerRadius = o.cornerRadius;
        } else {
            s.curveType = o.curveType;
            s.startCap = o.startCap;
            s.endCap = o.endCap;
        }
    });
    AppState::instance()->setPrimary(st.primary);
    AppState::instance()->setSecondary(st.secondary);
}

} // namespace

LineTool::LineTool(QObject* parent)
    : Tool(parent)
{
    auto refresh = [this] {
        if (!m_edit.isActive() || m_restoring)
            return;
        render();
        m_steps.changed();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, refresh);
    connect(AppState::instance(), &AppState::colorsChanged, this, refresh);
}

QIcon LineTool::icon() const { return Icons::tool(QStringLiteral("linecurve"), Icons::theme({"draw-bezier-curves", "draw-line"})); }

QString LineTool::hint() const
{
    return m_edit.isActive()
             ? tr("Arrastra los nodos para curvar la línea; icono o flechas: mover; clic derecho: girar. "
                  ", . / cambian remates y estilo. Intro: terminar.")
             : tr("Arrastra para dibujar una línea con el color principal (clic derecho: secundario). "
                  "Mayús: 15°. Alt: desde el centro.");
}

void LineTool::setInitialNodes(const QPointF& a, const QPointF& b)
{
    m_pts = {a, a + (b - a) / 3.0, a + (b - a) * 2.0 / 3.0, b};
}

QPainterPath LineTool::curve() const
{
    QPainterPath path(m_pts[0]);
    switch (AppState::instance()->tools().curveType) {
    case CurveType::Straight:
        for (int i = 1; i < 4; ++i)
            path.lineTo(m_pts[size_t(i)]);
        break;
    case CurveType::Bezier:
        path.cubicTo(m_pts[1], m_pts[2], m_pts[3]);
        break;
    case CurveType::Spline:
        for (int i = 0; i < 3; ++i) {
            const QPointF p0 = m_pts[size_t(std::max(0, i - 1))], p1 = m_pts[size_t(i)];
            const QPointF p2 = m_pts[size_t(i + 1)], p3 = m_pts[size_t(std::min(3, i + 2))];
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
        break;
    }
    return path;
}

QPointF LineTool::moveIcon() const
{
    return canvas()->imageToView(m_pts[3]) + QPointF(18, 18);
}

void LineTool::render()
{
    // Cada extremo con su remate: plano, redondo, flecha o flecha rellena (el uno no cambia al
    // otro). Con flecha rellena el trazo acaba antes, para que no asome por la punta.
    const ToolSettings& s = AppState::instance()->tools();
    const AppState* app = AppState::instance();
    const QColor main = m_secondary ? app->secondary() : app->primary();
    const QColor other = m_secondary ? app->primary() : app->secondary();
    const QPainterPath path = curve();
    const double w = s.brushSize;
    const double length = path.length();
    const double headSize = std::max(8.0, w * 3);
    double from = 0, to = 1;
    if (length > 0 && s.startCap == LineCap::ArrowFilled)
        from = path.percentAtLength(std::min(length / 2, headSize * 0.6));
    if (length > 0 && s.endCap == LineCap::ArrowFilled)
        to = path.percentAtLength(std::max(length / 2, length - headSize * 0.6));
    QPainterPath body = path;
    if (from > 0 || to < 1) {
        body = QPainterPath(path.pointAtPercent(from));
        constexpr int kSteps = 256;
        for (int i = 1; i <= kSteps; ++i)
            body.lineTo(path.pointAtPercent(from + (to - from) * i / kSteps));
    }
    QPainterPath openHeads, filledHeads, dots;
    auto cap = [&](LineCap c, const QPointF& at, double angle) {
        switch (c) {
        case LineCap::Arrow: openHeads.addPath(arrowHead(at, angle, w, false)); break;
        case LineCap::ArrowFilled: filledHeads.addPath(arrowHead(at, angle, w, true)); break;
        case LineCap::Round: dots.addEllipse(at, w / 2, w / 2); break;
        case LineCap::Flat: break;
        }
    };
    cap(s.startCap, m_pts[0], path.angleAtPercent(0) + 180);
    cap(s.endCap, m_pts[3], path.angleAtPercent(1));
    const QRectF extent = path.boundingRect() | openHeads.boundingRect() | filledHeads.boundingRect() | dots.boundingRect();
    const QRect region = extent.adjusted(-w * 2 - 4, -w * 2 - 4, w * 2 + 4, w * 2 + 4).toAlignedRect();
    renderVector(m_edit, region, [&](QPainter& p, bool coverage) {
        const QBrush brush = coverage ? QBrush(Qt::black) : Raster::patternBrush(s.fill, main, other);
        p.setBrush(Qt::NoBrush);
        p.setPen(strokePen(brush, Qt::FlatCap));
        p.drawPath(body);
        if (!openHeads.isEmpty()) {
            p.setPen(QPen(brush, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(openHeads);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(brush);
        if (!filledHeads.isEmpty())
            p.drawPath(filledHeads);
        if (!dots.isEmpty())
            p.drawPath(dots);
    });
    repaint();
}

void LineTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (m_edit.isActive()) {
        for (int i = 0; i < 4; ++i) {
            if (Handles::hit(e.viewPos, canvas()->imageToView(m_pts[size_t(i)]))) {
                m_drag = Drag::Point;
                m_dragIndex = i;
                return;
            }
        }
        if (e.button == Qt::LeftButton && Handles::hit(e.viewPos, moveIcon(), 11)) {
            m_drag = Drag::Move;
            m_grab = e.pos;
            return;
        }
        if (e.button == Qt::RightButton) {
            m_drag = Drag::Rotate;
            m_grab = e.pos;
            m_ptsStart = m_pts;
            return;
        }
        finish();
    }
    m_edit.begin(doc, doc->activeLayerPtr());
    m_steps.start(m_edit);
    m_secondary = e.button == Qt::RightButton;
    m_anchor = e.pos;
    setInitialNodes(e.pos, e.pos);
    m_drag = Drag::Create;
}

void LineTool::mouseMove(const ToolEvent& e)
{
    if (!m_edit.isActive() || m_drag == Drag::None)
        return;
    switch (m_drag) {
    case Drag::Create: {
        QPointF end = (e.modifiers & Qt::ShiftModifier) ? snapAngle(m_anchor, e.pos) : e.pos;
        const QPointF start = (e.modifiers & Qt::AltModifier) ? m_anchor * 2 - end : m_anchor;
        setInitialNodes(start, end);
        break;
    }
    case Drag::Point:
        m_pts[size_t(m_dragIndex)] = e.pos;
        break;
    case Drag::Move: {
        const QPointF d = e.pos - m_grab;
        for (QPointF& p : m_pts)
            p += d;
        m_grab = e.pos;
        break;
    }
    case Drag::Rotate: {
        QPointF c(0, 0);
        for (const QPointF& p : m_ptsStart)
            c += p / 4.0;
        const QPointF a = m_grab - c, b = e.pos - c;
        double angle = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180.0 / M_PI;
        if (e.modifiers & Qt::ShiftModifier)
            angle = std::round(angle / 15.0) * 15.0;
        QTransform t;
        t.translate(c.x(), c.y());
        t.rotate(angle);
        t.translate(-c.x(), -c.y());
        for (int i = 0; i < 4; ++i)
            m_pts[size_t(i)] = t.map(m_ptsStart[size_t(i)]);
        break;
    }
    case Drag::None: break;
    }
    render();
}

void LineTool::mouseRelease(const ToolEvent&)
{
    if (m_drag == Drag::Create && m_pts[0] == m_pts[3]) {
        m_edit.cancel();
        m_steps.leave(m_edit);
    } else if (m_drag != Drag::None) {
        m_steps.changed();
        commitStep();
    }
    m_drag = Drag::None;
    emit hintChanged(hint());
    repaint();
}

bool LineTool::keyPress(QKeyEvent* e)
{
    if (!m_edit.isActive())
        return false;
    if (isFinishKey(e)) {
        finish();
        return true;
    }
    const QPoint d = arrowDelta(e);
    if (!d.isNull()) {
        for (QPointF& p : m_pts)
            p += QPointF(d);
        render();
        m_steps.changed();
        return true;
    }
    auto nextCap = [](LineCap c) { return LineCap((int(c) + 1) % 4); };
    if (e->text() == QLatin1String(",")) {
        AppState::instance()->updateTools([&](ToolSettings& s) { s.startCap = nextCap(s.startCap); });
        return true;
    }
    if (e->text() == QLatin1String("/")) {
        AppState::instance()->updateTools([&](ToolSettings& s) { s.endCap = nextCap(s.endCap); });
        return true;
    }
    if (e->text() == QLatin1String(".")) {
        static const Qt::PenStyle styles[] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine};
        AppState::instance()->updateTools([&](ToolSettings& s) {
            int i = 0;
            while (i < 5 && styles[i] != s.dash)
                ++i;
            s.dash = styles[(i + 1) % 5];
        });
        return true;
    }
    return false;
}

void LineTool::paintOverlay(QPainter& p)
{
    if (!m_edit.isActive() || m_drag == Drag::Create)
        return;
    if (AppState::instance()->tools().curveType == CurveType::Bezier) {
        p.setPen(QPen(QColor(0, 120, 215), 0, Qt::DashLine));
        p.drawLine(canvas()->imageToView(m_pts[0]), canvas()->imageToView(m_pts[1]));
        p.drawLine(canvas()->imageToView(m_pts[2]), canvas()->imageToView(m_pts[3]));
    }
    for (const QPointF& pt : m_pts)
        Handles::draw(p, canvas()->imageToView(pt), Handles::Shape::Circle);
    Handles::draw(p, moveIcon(), Handles::Shape::Move);
}

void LineTool::commitStep()
{
    auto st = captureState<LineState>();
    st->pts = m_pts;
    st->swapped = m_secondary;
    m_steps.commit(m_edit, name(), tr("Editar línea/curva"), st);
}

void LineTool::finish()
{
    if (!m_edit.isActive())
        return;
    commitStep();
    m_steps.finish(m_edit, tr("Finalizar línea/curva"));
    m_drag = Drag::None;
    emit hintChanged(hint());
    repaint();
}

void LineTool::beforeHistoryJump()
{
    // Lo pendiente (opciones o colores cambiados) se registra; el historial decide si se retoma.
    commitStep();
}

void LineTool::historyChanged()
{
    if (m_steps.pushing())
        return;
    const ToolStepCommand* step = m_steps.top(document());
    if (!step) {
        if (m_edit.isActive()) {
            m_steps.leave(m_edit);
            emit hintChanged(hint());
            repaint();
        }
        return;
    }
    const auto* st = static_cast<const LineState*>(step->state());
    m_restoring = true;
    restoreSettings(*st, false);
    m_restoring = false;
    m_steps.resume(m_edit, document(), step);
    m_pts = st->pts;
    m_secondary = st->swapped;
    m_drag = Drag::None;
    emit hintChanged(hint());
    repaint();
}

void LineTool::deactivate()
{
    finish();
    Tool::deactivate();
}

// ---------------------------------------------------------------------------
// Formas
// ---------------------------------------------------------------------------

ShapesTool::ShapesTool(QObject* parent)
    : Tool(parent)
{
    auto refresh = [this] {
        if (!m_edit.isActive() || m_restoring || m_creating)
            return;
        render();
        m_steps.changed();
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, refresh);
    connect(AppState::instance(), &AppState::colorsChanged, this, refresh);
}

QIcon ShapesTool::icon() const { return Icons::tool(QStringLiteral("shapes"), Icons::theme({"shapes", "draw-rectangle"})); }

QCursor ShapesTool::cursor() const
{
    if (Icons::cursorStyle() == Icons::CursorStyle::PaintNet)
        return Tool::cursor();
    // Junto al puntero, la forma elegida (también las personalizadas), en negro con borde blanco
    // para que se vea sobre cualquier imagen.
    const ToolSettings s = AppState::instance()->tools();
    const QString key = s.shape == ShapeType::Custom ? QStringLiteral("shape/custom/") + s.customShape
                                                     : QStringLiteral("shape/%1").arg(int(s.shape));
    return Icons::miniCursor(key, [&s](QPainter& p, const QRectF& r) {
        const QRectF box = r.adjusted(1, 2, -1, -2);
        const QPainterPath path = s.shape == ShapeType::Custom ? toolShapePath(s, box) : shapePath(s.shape, box, 2.5);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(Qt::black, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    });
}

QString ShapesTool::hint() const
{
    return m_edit.isActive()
             ? tr("Tiradores: redimensionar (Mayús: proporción, Alt: desde el centro). Dentro: mover. "
                  "Clic derecho o fuera del borde: girar. A: siguiente forma. Intro: terminar.")
             : tr("Arrastra para dibujar la forma: contorno con el color principal y relleno con el secundario "
                  "(clic derecho: al revés). Mayús: proporciones iguales. Alt: desde el centro.");
}

QPointF ShapesTool::moveIcon() const
{
    const QPolygonF corners = canvas()->imageToViewTransform().map(m_box.canvasCorners());
    return Handles::moveIconPos(corners.boundingRect());
}

void ShapesTool::render()
{
    const ToolSettings& s = AppState::instance()->tools();
    const AppState* app = AppState::instance();
    const QColor main = m_secondary ? app->secondary() : app->primary();
    const QColor other = m_secondary ? app->primary() : app->secondary();
    const QPainterPath path = m_box.transform().map(toolShapePath(s, m_box.localRect()));
    const double w = s.brushSize;
    const QRect region = path.boundingRect().adjusted(-w - 3, -w - 3, w + 3, w + 3).toAlignedRect();
    renderVector(m_edit, region, [&](QPainter& p, bool coverage) {
        const QBrush fillBrush = coverage ? QBrush(Qt::black)
                                          : Raster::patternBrush(s.fill, s.shapeStyle == ShapeStyle::OutlineFill ? other : main,
                                                                 s.shapeStyle == ShapeStyle::OutlineFill ? main : other);
        const QBrush lineBrush = coverage ? QBrush(Qt::black) : Raster::patternBrush(s.fill, main, other);
        if (s.shapeStyle != ShapeStyle::Outline) {
            p.setPen(Qt::NoPen);
            p.setBrush(fillBrush);
            p.drawPath(path);
        }
        if (s.shapeStyle != ShapeStyle::Fill) {
            p.setBrush(Qt::NoBrush);
            p.setPen(strokePen(lineBrush, Qt::SquareCap));
            p.drawPath(path);
        }
    });
    repaint();
}

void ShapesTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (m_edit.isActive()) {
        int handle = -1;
        TransformBox::Mode mode = m_box.hit(e.viewPos, canvas()->imageToViewTransform(), e.button == Qt::RightButton, &handle);
        if (mode == TransformBox::Mode::None && Handles::hit(e.viewPos, moveIcon(), 11))
            mode = TransformBox::Mode::Translate;
        if (mode != TransformBox::Mode::None) {
            m_box.begin(mode, handle, e.pos);
            return;
        }
        finish();
    }
    m_edit.begin(doc, doc->activeLayerPtr());
    m_steps.start(m_edit);
    m_secondary = e.button == Qt::RightButton;
    m_start = QPointF(std::round(e.pos.x()), std::round(e.pos.y()));
    m_box.set(QRectF(m_start, QSizeF(0, 0)), QTransform());
    m_creating = true;
}

void ShapesTool::mouseMove(const ToolEvent& e)
{
    if (!m_edit.isActive()) {
        return;
    }
    if (m_creating) {
        QPointF end(std::round(e.pos.x()), std::round(e.pos.y()));
        if (e.modifiers & Qt::ShiftModifier) {
            const qreal side = std::max(std::abs(end.x() - m_start.x()), std::abs(end.y() - m_start.y()));
            end = QPointF(m_start.x() + (end.x() >= m_start.x() ? side : -side),
                          m_start.y() + (end.y() >= m_start.y() ? side : -side));
        }
        const QPointF start = (e.modifiers & Qt::AltModifier) ? m_start * 2 - end : m_start;
        m_box.set(QRectF(start, end).normalized(), QTransform());
        render();
        return;
    }
    if (m_box.mode() != TransformBox::Mode::None) {
        m_box.update(e.pos, e.modifiers);
        if (m_box.mode() == TransformBox::Mode::Rotate)
            canvas()->setToolCursor(Icons::rotateCursor(rotateAngle(m_box, canvas()->imageToViewTransform(), e.viewPos)));
        render();
        return;
    }
    int handle = -1;
    const TransformBox::Mode mode = m_box.hit(e.viewPos, canvas()->imageToViewTransform(), false, &handle);
    canvas()->setToolCursor(transformCursor(mode, cursor(), rotateAngle(m_box, canvas()->imageToViewTransform(), e.viewPos)));
}

void ShapesTool::mouseRelease(const ToolEvent&)
{
    if (m_creating) {
        m_creating = false;
        if (m_box.localRect().width() < 1 || m_box.localRect().height() < 1) {
            m_edit.cancel();
            m_steps.leave(m_edit);
        } else {
            m_steps.changed();
        }
    } else if (m_box.mode() != TransformBox::Mode::None) {
        m_steps.changed();
    }
    m_box.end();
    commitStep();
    emit hintChanged(hint());
    repaint();
}

bool ShapesTool::keyPress(QKeyEvent* e)
{
    if (!m_edit.isActive()) {
        if (e->key() == Qt::Key_A) {
            const int dir = (e->modifiers() & Qt::ShiftModifier) ? -1 : 1;
            AppState::instance()->updateTools([&](ToolSettings& s) { stepShape(s, dir); });
            return true;
        }
        return false;
    }
    if (isFinishKey(e)) {
        finish();
        return true;
    }
    if (e->key() == Qt::Key_A) {
        const int dir = (e->modifiers() & Qt::ShiftModifier) ? -1 : 1;
        AppState::instance()->updateTools([&](ToolSettings& s) { stepShape(s, dir); });
        return true;
    }
    const QPoint d = arrowDelta(e);
    if (!d.isNull()) {
        m_box.set(m_box.localRect(), m_box.transform() * QTransform::fromTranslate(d.x(), d.y()));
        render();
        m_steps.changed();
        return true;
    }
    return false;
}

void ShapesTool::paintOverlay(QPainter& p)
{
    if (!m_edit.isActive() || m_creating)
        return;
    m_box.draw(p, canvas()->imageToViewTransform());
    if (m_box.mode() == TransformBox::Mode::None)
        Handles::draw(p, moveIcon(), Handles::Shape::Move);
}

void ShapesTool::commitStep()
{
    auto st = captureState<ShapeState>();
    st->rect = m_box.localRect();
    st->transform = m_box.transform();
    st->swapped = m_secondary;
    m_steps.commit(m_edit, toolShapeName(st->settings), tr("Editar forma"), st);
}

void ShapesTool::finish()
{
    if (!m_edit.isActive())
        return;
    if (!m_creating)
        commitStep();
    m_steps.finish(m_edit, tr("Finalizar forma"));
    m_creating = false;
    m_box.end();
    emit hintChanged(hint());
    repaint();
}

void ShapesTool::beforeHistoryJump()
{
    if (!m_creating)
        commitStep();
}

void ShapesTool::historyChanged()
{
    if (m_steps.pushing())
        return;
    const ToolStepCommand* step = m_steps.top(document());
    if (!step) {
        if (m_edit.isActive()) {
            m_steps.leave(m_edit);
            m_creating = false;
            m_box.end();
            emit hintChanged(hint());
            repaint();
        }
        return;
    }
    const auto* st = static_cast<const ShapeState*>(step->state());
    m_restoring = true;
    restoreSettings(*st, true);
    m_restoring = false;
    m_steps.resume(m_edit, document(), step);
    m_box.set(st->rect, st->transform);
    m_secondary = st->swapped;
    m_creating = false;
    emit hintChanged(hint());
    repaint();
}

void ShapesTool::deactivate()
{
    finish();
    Tool::deactivate();
}
