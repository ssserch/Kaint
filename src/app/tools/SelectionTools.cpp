#include "app/tools/SelectionTools.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/tools/Handles.h"
#include "core/Commands.h"
#include "core/FloodFill.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QPainter>

#include <cmath>

namespace {

QString selectHint()
{
    return QObject::tr("Ctrl: añadir · Alt: restar · Ctrl+clic derecho: invertir (XOR) · Alt+clic derecho: intersección");
}

QPointF snapped(const QPointF& p)
{
    return QPointF(std::round(p.x()), std::round(p.y()));
}

} // namespace

void SelectToolBase::deactivate()
{
    if (m_active)
        commit(name(), {});
    Tool::deactivate();
}

SelectionMode SelectToolBase::modeFor(const ToolEvent& e) const
{
    const bool ctrl = e.modifiers & Qt::ControlModifier;
    const bool alt = e.modifiers & Qt::AltModifier;
    const bool right = e.button == Qt::RightButton;
    if (ctrl && !right)
        return SelectionMode::Union;
    if (alt && !right)
        return SelectionMode::Exclude;
    if (ctrl && right)
        return SelectionMode::Xor;
    if (alt && right)
        return SelectionMode::Intersect;
    return AppState::instance()->tools().selectionMode;
}

void SelectToolBase::begin(const ToolEvent& e)
{
    m_before = document()->selection();
    m_mode = modeFor(e);
    m_active = true;
    m_dragged = false;
    m_timer.start();
}

void SelectToolBase::preview(const Selection& shape)
{
    m_shape = m_mode == SelectionMode::Replace ? Selection() : shape;
    document()->setSelection(m_mode == SelectionMode::Replace ? shape : m_before.combined(shape, m_mode));
}

QPainterPath SelectToolBase::clippedToCanvas(const QPainterPath& path, QSize canvas)
{
    const QRectF bounds(QPointF(0, 0), QSizeF(canvas));
    if (bounds.contains(path.boundingRect()))
        return path;
    QPainterPath clip;
    clip.addRect(bounds);
    return path.intersected(clip);
}

void SelectToolBase::paintOverlay(QPainter& p)
{
    if (!m_active || m_shape.isEmpty())
        return;
    p.save();
    const QTransform t = canvas()->imageToViewTransform();
    // Trazo continuo claro y discontinuo con el color de resalte: se distingue
    // de las "hormigas" blancas y negras de la selección resultante.
    QPen under(Qt::white, 0);
    QPen dashes(QApplication::palette().color(QPalette::Highlight).darker(140), 0, Qt::CustomDashLine);
    dashes.setDashPattern({3, 3});
    if (m_shape.isPathBased()) {
        p.setRenderHint(QPainter::Antialiasing);
        const QPainterPath path = t.map(m_shape.path());
        p.setBrush(Qt::NoBrush);
        p.setPen(under);
        p.drawPath(path);
        p.setPen(dashes);
        p.drawPath(path);
    } else {
        QVector<QLineF> mapped;
        for (const QLineF& l : m_shape.outline())
            mapped.append(t.map(l));
        p.setPen(under);
        p.drawLines(mapped);
        p.setPen(dashes);
        p.drawLines(mapped);
    }
    p.restore();
}

void SelectToolBase::showStatus(const QRectF& r)
{
    emit hintChanged(tr("Selección: %1, %2 · %3 × %4")
                         .arg(qRound(r.left()))
                         .arg(qRound(r.top()))
                         .arg(qRound(r.width()))
                         .arg(qRound(r.height())));
}

bool SelectToolBase::plainReplace(Qt::KeyboardModifiers modifiers) const
{
    return m_mode == SelectionMode::Replace && !(modifiers & (Qt::ControlModifier | Qt::AltModifier));
}

void SelectToolBase::done(const QString& text, const QString& icon, Qt::KeyboardModifiers modifiers)
{
    // Tabla de verdad de SelectionTool.Done() de Paint.NET: un clic (o un
    // arrastre de menos de 50 ms) en modo reemplazar anula la selección; en
    // los otros modos simplemente no cambia nada.
    const bool tooQuick = m_timer.elapsed() <= 50;
    const bool append = m_mode != SelectionMode::Replace;
    if (append) {
        if (!m_dragged)
            document()->setSelection(m_before);
        commit(text, icon);
    } else if (m_dragged && !tooQuick) {
        commit(text, icon);
        if (plainReplace(modifiers) && !document()->selection().isEmpty())
            emit selectionFinished();
    } else {
        document()->setSelection(Selection());
        commit(tr("Anular la selección"), QStringLiteral("edit-select-none"));
    }
    emit hintChanged(hint());
}

void SelectToolBase::commit(const QString& text, const QString& icon)
{
    m_active = false;
    m_shape = Selection();
    Document* doc = document();
    if (!doc || doc->selection() == m_before)
        return;
    HistoryIcons::registerIcon(text, icon);
    doc->undoStack()->push(new SelectionCommand(doc, m_before, doc->selection(), text));
}

// ---------------------------------------------------------------------------

QRectF ShapeSelectTool::currentRect(const QPointF& endIn, Qt::KeyboardModifiers mods) const
{
    const ToolSettings& s = AppState::instance()->tools();
    QPointF end = snapped(endIn);
    if (!ellipse() && s.rectMode == RectSelectMode::FixedSize) {
        // Tamaño fijo: el rectángulo sigue al puntero y no se sale del lienzo.
        const QSizeF size(std::max(1.0, s.fixedWidth), std::max(1.0, s.fixedHeight));
        QPointF tl = end - QPointF(size.width() / 2, size.height() / 2);
        const QSize canvas = document()->size();
        tl.setX(std::clamp(std::round(tl.x()), 0.0, std::max(0.0, canvas.width() - size.width())));
        tl.setY(std::clamp(std::round(tl.y()), 0.0, std::max(0.0, canvas.height() - size.height())));
        return QRectF(tl, size);
    }
    double ratio = 0;
    if (mods & Qt::ShiftModifier)
        ratio = 1;
    else if (!ellipse() && s.rectMode == RectSelectMode::FixedRatio && s.fixedHeight > 0)
        ratio = s.fixedWidth / s.fixedHeight;
    if (ratio > 0) {
        const double dx = end.x() - m_start.x(), dy = end.y() - m_start.y();
        const double w = std::max(std::abs(dx), std::abs(dy) * ratio);
        const double h = w / ratio;
        end = QPointF(m_start.x() + (dx >= 0 ? w : -w), m_start.y() + (dy >= 0 ? h : -h));
        end = snapped(end);
    }
    return QRectF(m_start, end).normalized();
}

void ShapeSelectTool::mousePress(const ToolEvent& e)
{
    if (!document() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    begin(e);
    m_start = snapped(e.pos);
    m_last = e.pos;
    m_moveOrigin = false;
}

void ShapeSelectTool::extraButton(const ToolEvent& e, bool pressed)
{
    if (!m_active)
        return;
    m_moveOrigin = pressed;
    m_originGrab = e.pos;
}

void ShapeSelectTool::mouseMove(const ToolEvent& e)
{
    if (!m_active)
        return;
    if (m_moveOrigin) {
        // Con los dos botones pulsados se desplaza la forma entera.
        const QPointF d = e.pos - m_originGrab;
        m_start += QPointF(std::round(d.x()), std::round(d.y()));
        m_originGrab += QPointF(std::round(d.x()), std::round(d.y()));
    }
    m_last = e.pos;
    const QRectF r = currentRect(e.pos, e.modifiers);
    if (r.width() < 1 && r.height() < 1)
        return;
    m_dragged = true;
    QPainterPath path;
    if (ellipse())
        path = clippedToCanvas([&] { QPainterPath e; e.addEllipse(r); return e; }(), document()->size());
    else
        path.addRect(r.intersected(QRectF(document()->rect())));
    if (path.isEmpty()) {
        preview(Selection()); // fuera del lienzo: no queda nada seleccionado
        showStatus(r);
        return;
    }
    preview(Selection::fromPath(path, document()->size(), ellipse() && AppState::instance()->tools().selectionAntialias));
    showStatus(r);
}

void ShapeSelectTool::mouseRelease(const ToolEvent& e)
{
    if (!m_active)
        return;
    done(name(), ellipse() ? QStringLiteral("tool_elliptical_selection") : QStringLiteral("select-rectangular"),
         e.modifiers);
}

QIcon RectSelectTool::icon() const { return Icons::tool(QStringLiteral("rectangleselect"), Icons::theme({"select-rectangular", "tool_rect_selection"})); }
QString RectSelectTool::hint() const { return tr("Arrastra para seleccionar un rectángulo. Mayús: cuadrado. ") + selectHint(); }
QIcon EllipseSelectTool::icon() const { return Icons::tool(QStringLiteral("ellipseselect"), Icons::theme({"tool_elliptical_selection", "draw-ellipse"})); }
QString EllipseSelectTool::hint() const { return tr("Arrastra para seleccionar una elipse. Mayús: círculo. ") + selectHint(); }

// ---------------------------------------------------------------------------

QIcon LassoSelectTool::icon() const { return Icons::tool(QStringLiteral("lassoselect"), Icons::theme({"edit-select-lasso", "tool_free_form_selection"})); }
QString LassoSelectTool::hint() const { return tr("Arrastra para dibujar el contorno de la selección. ") + selectHint(); }

void LassoSelectTool::mousePress(const ToolEvent& e)
{
    if (!document() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    begin(e);
    m_points = QPolygonF{e.pos};
    m_moveOrigin = false;
}

void LassoSelectTool::extraButton(const ToolEvent& e, bool pressed)
{
    if (!m_active)
        return;
    m_moveOrigin = pressed;
    m_last = e.pos;
}

void LassoSelectTool::update()
{
    if (m_points.size() < 3)
        return;
    m_dragged = true;
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addPolygon(m_points);
    path.closeSubpath();
    path = clippedToCanvas(path, document()->size());
    preview(path.isEmpty() ? Selection()
                           : Selection::fromPath(path, document()->size(), AppState::instance()->tools().selectionAntialias));
    showStatus(m_points.boundingRect());
}

void LassoSelectTool::mouseMove(const ToolEvent& e)
{
    if (!m_active)
        return;
    if (m_moveOrigin) {
        m_points.translate(e.pos - m_last);
        m_last = e.pos;
        update();
        return;
    }
    if (QLineF(m_points.last(), e.pos).length() * canvas()->scale() < 2.0)
        return;
    m_points << e.pos;
    update();
}

void LassoSelectTool::mouseRelease(const ToolEvent& e)
{
    if (!m_active)
        return;
    done(name(), QStringLiteral("edit-select-lasso"), e.modifiers);
    m_points.clear();
}

// ---------------------------------------------------------------------------

MagicWandTool::MagicWandTool(QObject* parent)
    : SelectToolBase(parent)
{
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, [this] {
        if (m_session)
            recompute();
    });
}

QIcon MagicWandTool::icon() const { return Icons::tool(QStringLiteral("magicwand"), Icons::theme({"tools-wizard", "games-hint"})); }

QString MagicWandTool::hint() const
{
    return m_session ? tr("Ajusta la tolerancia o arrastra el punto de origen. Intro: terminar.")
                     : tr("Haz clic para seleccionar una zona de color parecido. Mayús: modo global. ") + selectHint();
}

void MagicWandTool::recompute()
{
    Document* doc = document();
    if (!doc || m_sample.isNull())
        return;
    const ToolSettings& s = AppState::instance()->tools();
    const bool global = s.floodGlobal != m_shiftGlobal;
    const QImage mask = FloodFill::mask(m_sample, m_seed, s.tolerance / 100.0, !global, s.toleranceStraight);
    preview(Selection::fromMask(mask));
    repaint();
}

void MagicWandTool::mousePress(const ToolEvent& e)
{
    Document* doc = document();
    if (!doc || !doc->activeLayer() || (e.button != Qt::LeftButton && e.button != Qt::RightButton))
        return;
    if (m_session && e.button == Qt::LeftButton
        && Handles::hit(e.viewPos, Handles::moveIconPos(QRectF(canvas()->imageToView(QPointF(m_seed) + QPointF(0.5, 0.5)), QSizeF())))) {
        m_draggingSeed = true;
        return;
    }
    finish();
    if (!doc->rect().contains(e.pixel()))
        return;
    const ToolSettings& s = AppState::instance()->tools();
    m_sample = s.sampleImage ? canvas()->composite().copy() : doc->activeLayer()->image;
    begin(e);
    m_session = true;
    m_shiftGlobal = e.modifiers & Qt::ShiftModifier;
    m_seed = e.pixel();
    m_dragged = true;
    recompute();
    emit hintChanged(hint());
}

void MagicWandTool::mouseMove(const ToolEvent& e)
{
    if (!m_draggingSeed)
        return;
    const QPoint p = e.pixel();
    if (p != m_seed && document()->rect().contains(p)) {
        m_seed = p;
        recompute();
    }
}

void MagicWandTool::mouseRelease(const ToolEvent& e)
{
    if (m_draggingSeed) {
        m_draggingSeed = false;
        return;
    }
    if (m_session && plainReplace(e.modifiers) && !document()->selection().isEmpty())
        emit selectionFinished();
}

bool MagicWandTool::keyPress(QKeyEvent* e)
{
    if (m_session && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)) {
        finish();
        return true;
    }
    return false;
}

void MagicWandTool::paintOverlay(QPainter& p)
{
    SelectToolBase::paintOverlay(p);
    if (!m_session)
        return;
    const QPointF seed = canvas()->imageToView(QPointF(m_seed) + QPointF(0.5, 0.5));
    p.setPen(QPen(Qt::black, 1));
    p.setBrush(Qt::white);
    p.drawRect(QRectF(seed - QPointF(2, 2), QSizeF(4, 4)));
    Handles::draw(p, Handles::moveIconPos(QRectF(seed, QSizeF())), Handles::Shape::Move);
}

void MagicWandTool::finish()
{
    if (!m_session)
        return;
    m_session = false;
    m_draggingSeed = false;
    m_sample = QImage();
    commit(name(), QStringLiteral("tools-wizard"));
    emit hintChanged(hint());
    repaint();
}
