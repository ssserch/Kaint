#include "app/Canvas.h"

#include "app/Icons.h"
#include "app/Theme.h"
#include "app/tools/Tool.h"
#include "core/Document.h"
#include "core/Raster.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QLabel>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTabletEvent>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>

#include <cmath>

namespace {
constexpr int kMargin = 24;
}

namespace {
// La selección como en Paint.NET: el interior lavanda translúcido (sobre blanco, #e4eaf7) y el
// contorno con hormigas blancas sobre una línea negra, que se distinguen sobre cualquier color (las
// grises y casi blancas de antes apenas se veían sobre lo oscuro o lo muy claro).
const QColor kSelectionTint(30, 80, 188, 31);
const QColor kAntsDark(0, 0, 0);
const QColor kAntsLight(255, 255, 255);
// Un paso de un píxel cada 40 ms: 25 por segundo, una vuelta del patrón (8 px) en 0,32 s, casi como
// en Paint.NET (a 120 ms por paso se arrastraban) sin repintar la selección más de lo necesario.
constexpr int kAntsInterval = 40;
// Con más tramos que estos (una varita sobre ruido), el contorno se dibuja por tramos sueltos:
// trazar con hormigas un contorno tan largo 25 veces por segundo sería lento.
constexpr int kMaxAntsLines = 60000;
} // namespace

Canvas::Canvas(QWidget* parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAttribute(Qt::WA_TabletTracking);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    connect(Theme::notifier(), &Theme::Notifier::changed, viewport(), qOverload<>(&QWidget::update));
    // Fondo animado (GIF o vídeo): solo alrededor de la imagen, que no cambia.
    connect(Theme::notifier(), &Theme::Notifier::backgroundFrame, this, [this] {
        QRegion area(viewport()->rect());
        if (m_doc)
            area -= imageToView(QRectF(m_doc->rect())).toAlignedRect().adjusted(1, 1, -1, -1);
        updateView(area);
    });
    m_checker = QPixmap::fromImage(Raster::checkerboard(8, QColor(191, 191, 191), QColor(143, 143, 143)));
    m_hRuler = new Ruler(Qt::Horizontal, this, this);
    m_vRuler = new Ruler(Qt::Vertical, this, this);
    // La esquina entre las reglas: la unidad (px, in, cm), como en Paint.NET; al pulsarla, un menú
    // para cambiarla.
    {
        auto* corner = new QToolButton(this);
        corner->setAutoRaise(true);
        corner->setPopupMode(QToolButton::InstantPopup);
        corner->setToolButtonStyle(Qt::ToolButtonTextOnly);
        corner->setToolTip(tr("Unidad de las reglas"));
        corner->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0px; font-size: 9px; }"
                                             "QToolButton::menu-indicator { image: none; width: 0px; }"));
        auto* menu = new QMenu(corner);
        const QString names[3] = {tr("Píxeles"), tr("Pulgadas"), tr("Centímetros")};
        for (int u = 0; u < 3; ++u)
            menu->addAction(names[u], this, [this, u] { emit unitRequested(u); });
        corner->setMenu(menu);
        m_rulerCorner = corner;
    }
    m_rulerCorner->setAutoFillBackground(true);
    // La esquina entre las reglas, del mismo color que ellas.
    auto paintCorner = [this] {
        QPalette pal = m_rulerCorner->palette();
        pal.setColor(QPalette::Window, Ruler::background());
        const QColor text = Ruler::background().lightnessF() < 0.5 ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x1e, 0x1e, 0x1e);
        pal.setColor(QPalette::ButtonText, text);
        pal.setColor(QPalette::WindowText, text);
        m_rulerCorner->setPalette(pal);
    };
    paintCorner();
    connect(Theme::notifier(), &Theme::Notifier::changed, m_rulerCorner, paintCorner);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, &Canvas::updateRulers);
    m_hRuler->hide();
    m_vRuler->hide();
    m_rulerCorner->hide();

    m_autoScrollTimer.setInterval(16);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, &Canvas::autoScrollStep);
    m_cursorTimer.setSingleShot(true);
    connect(&m_cursorTimer, &QTimer::timeout, this, &Canvas::nextCursorFrame);

    connect(&m_antsTimer, &QTimer::timeout, this, [this] {
        const bool selection = m_doc && !m_doc->selection().isEmpty();
        if (!selection && !m_overlayAnts) {
            m_antsTimer.stop();
            return;
        }
        m_antsOffset = (m_antsOffset + 1) % 8;
        if (m_overlayAnts) {
            viewport()->update();
            return;
        }
        // Lo que ocupa su contorno, también lo que sale de la imagen (la máscara solo llega a su
        // borde: fuera, las hormigas se quedaban quietas).
        const Selection& sel = m_doc->selection();
        QRectF area(sel.bounds());
        if (!sel.path().isEmpty())
            area = area.united(sel.path().boundingRect());
        // (Con margen: los tiradores de las herramientas Mover, que laten, y su icono de mover.)
        const QRect r = imageToView(area).toAlignedRect().adjusted(-34, -34, 34, 34);
        updateView(r.intersected(viewport()->rect()));
    });
}

// ---------------------------------------------------------------------------
// Documento y herramienta
// ---------------------------------------------------------------------------

void Canvas::setDocument(Document* doc)
{
    if (doc == m_doc)
        return;
    Tool* tool = m_tool;
    if (tool)
        tool->deactivate();

    if (m_doc) {
        m_views.insert(m_doc, {m_zoom, m_origin});
        disconnect(m_doc, nullptr, this, nullptr);
        disconnect(m_doc->undoStack(), nullptr, this, nullptr);
    }
    m_doc = doc;
    m_toolButton = Qt::NoButton;

    if (m_doc) {
        connect(m_doc, &Document::pixelsChanged, this, &Canvas::onPixelsChanged);
        connect(m_doc, &Document::sizeChanged, this, &Canvas::onSizeChanged);
        connect(m_doc, &Document::selectionChanged, this, &Canvas::onSelectionChanged);
        connect(m_doc, &Document::colorSpaceChanged, this, &Canvas::onColorSpaceChanged);
        // Deshacer y rehacer cambian lo que hay bajo el puntero (el cuadro de la selección, la
        // forma que se edita): el cursor se vuelve a elegir sin esperar a que se mueva el ratón.
        connect(m_doc->undoStack(), &QUndoStack::indexChanged, this,
                [this] { QTimer::singleShot(0, this, &Canvas::refreshToolCursor); });
        m_composite = QImage(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
        onColorSpaceChanged();

        const auto it = m_views.constFind(m_doc);
        if (it != m_views.cend()) {
            m_fitPending = false;
            m_zoom = it->zoom;
            m_origin = it->origin;
            clampOrigin();
            updateScrollBars();
        } else {
            m_fitPending = true;
            fitInitial();
        }
        onSelectionChanged();
    } else {
        m_composite = QImage();
        updateScrollBars();
    }

    if (tool)
        tool->activate(this);
    updateCursor();
    viewport()->update();
    emit zoomChanged(m_zoom);
    updateRulers();
}

void Canvas::forgetDocument(Document* doc)
{
    m_views.remove(doc);
    if (doc == m_doc)
        setDocument(nullptr);
}

void Canvas::setTool(Tool* tool)
{
    if (tool == m_tool)
        return;
    if (m_tool)
        m_tool->deactivate();
    if (m_notice)
        m_notice->hide();
    m_tool = tool;
    m_toolButton = Qt::NoButton;
    if (m_tool)
        m_tool->activate(this);
    updateCursor();
    viewport()->update();
}

void Canvas::renderRegion(const QRect& area)
{
    const QRect r = area.intersected(m_composite.rect());
    if (r.isEmpty())
        return;
    m_doc->renderComposite(m_composite, r);
    if (m_toDisplay.isIdentity())
        return;
    QImage part = m_composite.copy(r);
    part.applyColorTransform(m_toDisplay);
    Raster::blit(m_composite, part, r.topLeft());
}

void Canvas::onColorSpaceChanged()
{
    const QColorSpace cs = m_doc ? m_doc->colorSpace() : QColorSpace();
    const QColorSpace srgb(QColorSpace::SRgb);
    m_toDisplay = (cs.isValid() && cs != srgb) ? cs.transformationToColorSpace(srgb) : QColorTransform();
    if (m_doc) {
        renderRegion(m_doc->rect());
        viewport()->update();
    }
}

void Canvas::onPixelsChanged(const QRect& r)
{
    if (!m_doc || r.isEmpty())
        return;
    renderRegion(r);
    updateView(imageToView(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void Canvas::onSizeChanged()
{
    m_composite = QImage(m_doc->size(), QImage::Format_ARGB32_Premultiplied);
    renderRegion(m_doc->rect());
    clampOrigin();
    updateScrollBars();
    viewport()->update();
}

void Canvas::onSelectionChanged()
{
    if ((m_doc && !m_doc->selection().isEmpty()) || m_overlayAnts) {
        if (!m_antsTimer.isActive())
            m_antsTimer.start(kAntsInterval);
    } else {
        m_antsTimer.stop();
    }
    viewport()->update();
}

void Canvas::setOverlayAnts(bool on)
{
    m_overlayAnts = on;
    onSelectionChanged();
}

void Canvas::showNotice(const QString& text)
{
    if (!m_notice) {
        m_notice = new QLabel(viewport());
        m_notice->setObjectName(QStringLiteral("CanvasNotice"));
        m_notice->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_notice->setTextFormat(Qt::PlainText);
        // Oscuro con el borde naranja de los avisos, sobre cualquier tema e imagen.
        m_notice->setStyleSheet(QStringLiteral("QLabel#CanvasNotice { background: #2b2b2b; color: #ffffff; border: 1px solid #f0a030;"
                                               " border-radius: 4px; padding: 6px 10px; }"));
        m_noticeTimer.setSingleShot(true);
        connect(&m_noticeTimer, &QTimer::timeout, m_notice, &QWidget::hide);
    }
    m_notice->setText(text);
    m_notice->adjustSize();
    // Debajo y a la derecha del puntero (encima si no cabe), sin salirse de la vista.
    const QRect area = viewport()->rect().adjusted(6, 6, -6, -6);
    const QPoint pointer = m_mouseSeen ? m_mouseView.toPoint() : area.center();
    QPoint pos = pointer + QPoint(18, 22);
    if (pos.y() + m_notice->height() > area.bottom())
        pos.setY(pointer.y() - 14 - m_notice->height());
    pos.setX(std::clamp(pos.x(), area.left(), std::max(area.left(), area.right() - m_notice->width())));
    pos.setY(std::clamp(pos.y(), area.top(), std::max(area.top(), area.bottom() - m_notice->height())));
    m_notice->move(pos);
    m_notice->show();
    m_notice->raise();
    m_noticeTimer.start(3500);
}

// ---------------------------------------------------------------------------
// Geometría y zoom
// ---------------------------------------------------------------------------

const QList<qreal>& Canvas::zoomPresets()
{
    // Los mismos pasos de zoom que Paint.NET.
    static const QList<qreal> presets{0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.12, 0.16, 0.20, 0.25,
                                      1.0 / 3, 0.50, 2.0 / 3, 1.0, 1.5, 2, 3, 4, 5, 6, 8, 10, 12, 14,
                                      16, 20, 24, 28, 32, 40, 48, 56, 64};
    return presets;
}

qreal Canvas::scale() const
{
    return m_zoom / viewport()->devicePixelRatioF();
}

QPointF Canvas::origin() const
{
    if (!m_doc)
        return {};
    const qreal dpr = viewport()->devicePixelRatioF();
    return QPointF(std::round(m_origin.x() * dpr) / dpr, std::round(m_origin.y() * dpr) / dpr);
}

void Canvas::originLimits(qreal content, qreal view, qreal* lo, qreal* hi) const
{
    if (m_overscroll) {
        *lo = view / 2.0 - content;
        *hi = view / 2.0;
    } else if (content + 2 * kMargin <= view) {
        *lo = *hi = (view - content) / 2.0;
    } else {
        *lo = view - content - kMargin;
        *hi = kMargin;
    }
}

void Canvas::clampOrigin()
{
    if (!m_doc)
        return;
    const QSizeF content = QSizeF(m_doc->size()) * scale();
    const QSize vp = viewport()->size();
    qreal lo, hi;
    originLimits(content.width(), vp.width(), &lo, &hi);
    m_origin.setX(std::clamp(m_origin.x(), lo, hi));
    originLimits(content.height(), vp.height(), &lo, &hi);
    m_origin.setY(std::clamp(m_origin.y(), lo, hi));
}

qreal Canvas::edgeOrigin(Qt::Orientation axis, bool start) const
{
    const bool h = axis == Qt::Horizontal;
    const qreal content = (h ? m_doc->size().width() : m_doc->size().height()) * scale();
    const qreal view = h ? viewport()->width() : viewport()->height();
    if (content + 2 * kMargin <= view)
        return (view - content) / 2.0;
    qreal lo, hi;
    originLimits(content, view, &lo, &hi);
    return std::clamp(start ? qreal(kMargin) : view - content - kMargin, lo, hi);
}

QPointF Canvas::viewToImage(const QPointF& p) const
{
    return (p - origin()) / scale();
}

QPointF Canvas::imageToView(const QPointF& p) const
{
    return origin() + p * scale();
}

QRectF Canvas::imageToView(const QRectF& r) const
{
    return QRectF(imageToView(r.topLeft()), r.size() * scale());
}

QTransform Canvas::imageToViewTransform() const
{
    const QPointF o = origin();
    const qreal s = scale();
    return QTransform(s, 0, 0, s, o.x(), o.y());
}

void Canvas::setViewRotation(qreal degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees > 180)
        degrees -= 360;
    if (degrees <= -180)
        degrees += 360;
    if (std::abs(degrees) < 0.01)
        degrees = 0;
    if (degrees == m_rotation)
        return;
    m_rotation = degrees;
    viewport()->update();
    emit viewChanged();
}

QPointF Canvas::toLogical(const QPointF& device) const
{
    if (m_rotation == 0)
        return device;
    const QPointF c = QRectF(viewport()->rect()).center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(-m_rotation);
    t.translate(-c.x(), -c.y());
    return t.map(device);
}

void Canvas::updateView(const QRect& r)
{
    if (m_rotation != 0)
        viewport()->update();
    else
        viewport()->update(r);
}

void Canvas::updateView(const QRegion& r)
{
    if (m_rotation != 0)
        viewport()->update();
    else
        viewport()->update(r);
}

QRect Canvas::visibleImageRect() const
{
    if (!m_doc)
        return {};
    const QRectF r(viewToImage(QPointF(0, 0)), viewToImage(QPointF(viewport()->width(), viewport()->height())));
    return r.toAlignedRect().intersected(m_doc->rect());
}

void Canvas::updateScrollBars()
{
    // Las barras solo reflejan m_origin: valor = límite superior - origen.
    const QScopedValueRollback<bool> guard(m_syncingBars, true);
    if (!m_doc) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        emit viewChanged();
        return;
    }
    const QSizeF content = QSizeF(m_doc->size()) * scale();
    const QSize vp = viewport()->size();
    // Como en Paint.NET: con la imagen como la deja «Zum a ventana», sin barras de desplazamiento.
    const bool atWindow = atWindowZoom();
    auto sync = [&](QScrollBar* bar, qreal c, int view, qreal origin) {
        qreal lo, hi;
        originLimits(c, view, &lo, &hi);
        if (atWindow) {
            bar->setRange(0, 0);
            return;
        }
        bar->setRange(0, std::max(0, int(std::round(hi - lo))));
        bar->setPageStep(view);
        bar->setSingleStep(32);
        bar->setValue(int(std::round(hi - origin)));
    };
    sync(horizontalScrollBar(), content.width(), vp.width(), m_origin.x());
    sync(verticalScrollBar(), content.height(), vp.height(), m_origin.y());
    emit viewChanged();
}

void Canvas::setOverscroll(bool on)
{
    if (on == m_overscroll)
        return;
    m_overscroll = on;
    clampOrigin();
    updateScrollBars();
    viewport()->update();
    updateRulers();
}

void Canvas::setZoom(qreal zoom, std::optional<QPointF> anchor)
{
    m_fitPending = false;
    m_zoomBeforeFit = 0;
    applyZoom(zoom, anchor);
}

qreal Canvas::windowZoom() const
{
    // Como Paint.NET: 100 % si la imagen cabe; si no, ajustada a la ventana. Con el sitio de la vista
    // sin barras de desplazamiento (en ese zum se ocultan): así no cambia al ocultarlas.
    if (!m_doc)
        return 1.0;
    const QSize area = maximumViewportSize();
    const qreal dpr = viewport()->devicePixelRatioF();
    const qreal zx = (area.width() - 2.0 * kMargin) / m_doc->size().width();
    const qreal zy = (area.height() - 2.0 * kMargin) / m_doc->size().height();
    return std::clamp(std::min(zx, zy) * dpr, 0.01, 1.0);
}

bool Canvas::atWindowZoom() const
{
    if (!m_doc)
        return false;
    const qreal target = windowZoom();
    if (std::abs(m_zoom - target) > target * 1e-6)
        return false;
    // Centrada (con medio píxel de margen por el redondeo).
    const QSizeF content = QSizeF(m_doc->size()) * scale();
    const QPointF centered((viewport()->width() - content.width()) / 2.0, (viewport()->height() - content.height()) / 2.0);
    return std::abs(m_origin.x() - centered.x()) <= 1.0 && std::abs(m_origin.y() - centered.y()) <= 1.0;
}

void Canvas::fitInitial()
{
    if (!m_doc)
        return;
    applyZoom(windowZoom(), std::nullopt);
    centerOn(QPointF(m_doc->size().width() / 2.0, m_doc->size().height() / 2.0));
}

void Canvas::applyZoom(qreal zoom, std::optional<QPointF> anchor)
{
    zoom = std::clamp(zoom, 0.01, 64.0);
    if (!m_doc) {
        m_zoom = zoom;
        emit zoomChanged(m_zoom);
        updateRulers();
        return;
    }
    const QPointF a = anchor.value_or(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    const QPointF img = (a - origin()) / scale();
    m_zoom = zoom;
    m_origin = a - img * scale();
    clampOrigin();
    updateScrollBars();
    viewport()->update();
    emit zoomChanged(m_zoom);
    updateRulers();
}

void Canvas::zoomIn(std::optional<QPointF> anchor)
{
    for (qreal z : zoomPresets())
        if (z > m_zoom * 1.0001)
            return setZoom(z, anchor);
}

void Canvas::zoomOut(std::optional<QPointF> anchor)
{
    const auto& presets = zoomPresets();
    for (auto it = presets.crbegin(); it != presets.crend(); ++it)
        if (*it < m_zoom * 0.9999)
            return setZoom(*it, anchor);
}

void Canvas::zoomToWindow()
{
    if (!m_doc)
        return;
    // (Antes, sin el tope del 100 %: una imagen pequeña se ampliaba hasta llenar la ventana en vez de
    // volver al zum con que se abrió.)
    m_fitPending = false;
    const qreal before = m_zoomBeforeFit;
    applyZoom(windowZoom(), std::nullopt);
    m_zoomBeforeFit = before;
    centerOn(QPointF(m_doc->size().width() / 2.0, m_doc->size().height() / 2.0));
}

void Canvas::toggleZoomToWindow()
{
    if (!m_doc)
        return;
    // Solo con la vista aún como la dejó «Zum a ventana» vuelve a la de antes; si se movió la
    // cámara o se cambió el zum, vuelve a la de la ventana.
    if (m_zoomBeforeFit > 0 && atWindowZoom()) {
        const qreal z = m_zoomBeforeFit;
        m_zoomBeforeFit = 0;
        setZoom(z);
        centerOn(m_centerBeforeFit);
        return;
    }
    if (atWindowZoom()) {
        m_zoomBeforeFit = 0;
        return;
    }
    m_zoomBeforeFit = m_zoom;
    m_centerBeforeFit = viewToImage(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    zoomToWindow();
}

void Canvas::zoomToRect(const QRectF& r)
{
    if (!m_doc || r.isEmpty())
        return;
    // Justo ese rectángulo, sin margen: llena la vista a lo ancho o a lo alto y queda centrado.
    const qreal dpr = viewport()->devicePixelRatioF();
    const qreal zx = viewport()->width() / r.width();
    const qreal zy = viewport()->height() / r.height();
    setZoom(std::min(zx, zy) * dpr);
    centerOn(r.center());
}

void Canvas::actualSize()
{
    setZoom(1.0);
}

void Canvas::centerOn(const QPointF& imagePoint)
{
    m_origin = QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0) - imagePoint * scale();
    clampOrigin();
    updateScrollBars();
    viewport()->update();
    updateRulers();
}

void Canvas::scrollByView(const QPointF& delta)
{
    m_origin -= delta;
    clampOrigin();
    updateScrollBars();
    viewport()->update();
    updateRulers();
}

void Canvas::setPixelGrid(bool on)
{
    m_pixelGrid = on;
    viewport()->update();
}

void Canvas::setSmoothRotation(bool on)
{
    m_smoothRotation = on;
    viewport()->update();
}

void Canvas::updateOverlay()
{
    viewport()->update();
}

void Canvas::updateCursor()
{
    if (m_panning)
        applyCursor(Icons::handCursor(true));
    else if (m_spaceDown)
        applyCursor(Icons::handCursor(false));
    else
        applyCursor(m_tool && m_doc ? m_tool->cursor() : QCursor(Qt::ArrowCursor));
}

void Canvas::setToolCursor(const QCursor& cursor)
{
    if (!m_panning && !m_spaceDown)
        applyCursor(cursor);
}

void Canvas::refreshToolCursor()
{
    if (!m_doc || !m_tool || m_panning || m_spaceDown || m_toolButton != Qt::NoButton)
        return;
    // Primero el de la herramienta; si el puntero está sobre el lienzo, la herramienta lo cambia
    // por el que toca ahí, como al pasar el ratón.
    applyCursor(m_tool->cursor());
    if (!m_mouseInside || !viewport()->underMouse())
        return;
    ToolEvent ev;
    ev.viewPos = m_mouseView;
    ev.pos = viewToImage(m_mouseView);
    ev.buttons = Qt::NoButton;
    ev.modifiers = QGuiApplication::keyboardModifiers();
    m_tool->mouseMove(ev);
}

void Canvas::applyCursor(const QCursor& cursor)
{
    // Las herramientas vuelven a poner su cursor al mover el ratón: si es el animado que ya se ve,
    // sigue por su fotograma.
    QList<Icons::CursorFrame> frames = Icons::cursorFrames(cursor);
    if (!frames.isEmpty() && !m_cursorFrames.isEmpty()
        && frames.first().cursor.pixmap().cacheKey() == m_cursorFrames.first().cursor.pixmap().cacheKey())
        return;
    m_cursorFrames = std::move(frames);
    m_cursorFrame = 0;
    if (m_cursorFrames.isEmpty())
        m_cursorTimer.stop();
    else
        m_cursorTimer.start(std::max(20, m_cursorFrames.first().delay));
    viewport()->setCursor(cursor);
}

void Canvas::nextCursorFrame()
{
    if (m_cursorFrames.isEmpty())
        return;
    m_cursorFrame = (m_cursorFrame + 1) % int(m_cursorFrames.size());
    const Icons::CursorFrame& frame = m_cursorFrames.at(m_cursorFrame);
    // Fuera del lienzo no hace falta cambiarlo (al volver, el siguiente fotograma llega enseguida).
    if (viewport()->underMouse())
        viewport()->setCursor(frame.cursor);
    m_cursorTimer.start(std::max(20, frame.delay));
}

void Canvas::setRulersVisible(bool on)
{
    m_rulersVisible = on;
    m_hRuler->setVisible(on);
    m_vRuler->setVisible(on);
    m_rulerCorner->setVisible(on);
    setViewportMargins(on ? Ruler::kThickness : 0, on ? Ruler::kThickness : 0, 0, 0);
    layoutRulers();
    emit rulersVisibilityChanged(on);
}

void Canvas::setRulerStyle(Ruler::Style style)
{
    Ruler::setStyle(style);
    QPalette pal = m_rulerCorner->palette();
    pal.setColor(QPalette::Window, Ruler::background());
    const QColor text = Ruler::background().lightnessF() < 0.5 ? QColor(0xd6, 0xd6, 0xd6) : QColor(0x1e, 0x1e, 0x1e);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::WindowText, text);
    m_rulerCorner->setPalette(pal);
    updateRulers();
}

void Canvas::setUnit(Unit unit)
{
    if (auto* corner = qobject_cast<QToolButton*>(m_rulerCorner))
        corner->setText(unitSuffix(unit));
    m_unit = unit;
    updateRulers();
}

void Canvas::layoutRulers()
{
    if (!m_rulersVisible)
        return;
    const QRect vp = viewport()->geometry();
    const int t = Ruler::kThickness;
    m_hRuler->setGeometry(vp.left(), vp.top() - t, vp.width(), t);
    m_vRuler->setGeometry(vp.left() - t, vp.top(), t, vp.height());
    m_rulerCorner->setGeometry(vp.left() - t, vp.top() - t, t, t);
}

void Canvas::updateRulers()
{
    if (!m_rulersVisible)
        return;
    m_hRuler->update();
    m_vRuler->update();
}

void Canvas::setAppearance(bool dropShadow, const QColor& border, int checkerBrightness)
{
    m_dropShadow = dropShadow;
    m_border = border;
    if (m_border.isValid())
        m_border.setAlpha(255);
    // Como Paint.NET: grises 191 y 143 por defecto.
    const int light = std::clamp(checkerBrightness, 0, 255);
    const int dark = int(std::lround(light * 0.75));
    m_checker = QPixmap::fromImage(Raster::checkerboard(8, QColor(light, light, light), QColor(dark, dark, dark)));
    viewport()->update();
}

const QImage& Canvas::selectionTint() const
{
    // Interior de la selección en el lavanda translúcido de Paint.NET 5.
    const Selection& sel = m_doc->selection();
    const void* key = &sel.mask();
    if (m_tintKey != key || m_tint.isNull()) {
        const QRect b = sel.bounds();
        m_tint = QImage(b.size(), QImage::Format_ARGB32_Premultiplied);
        const QImage& mask = sel.mask();
        for (int y = 0; y < b.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(m_tint.scanLine(y));
            const uchar* m = mask.constScanLine(b.top() + y) + b.left();
            for (int x = 0; x < b.width(); ++x)
                row[x] = qPremultiply(qRgba(kSelectionTint.red(), kSelectionTint.green(), kSelectionTint.blue(),
                                            m[x] * kSelectionTint.alpha() / 255));
        }
        m_tintKey = key;
    }
    return m_tint;
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------

void Canvas::paintEvent(QPaintEvent* e)
{
    QPainter p(viewport());
    // La imagen de fondo se coloca respecto a todo el lienzo (con el sitio de las barras de
    // desplazamiento): si no, al aparecer o desaparecer las barras (ajustar a la ventana) se movía.
    Theme::paintWorkspace(p, QRect(viewport()->mapFrom(this, QPoint(0, 0)), size()), e->rect(), m_border);
    const bool dark = (m_border.isValid() ? m_border : Theme::workspaceColor()).lightness() < 128;
    if (!m_doc)
        return;

    // Vista girada: todo (la imagen, la selección, lo de las herramientas) se dibuja girado alrededor
    // del centro; las posiciones siguen siendo las de la vista sin girar.
    QRect exposed = e->rect();
    if (m_rotation != 0) {
        const QPointF c = QRectF(viewport()->rect()).center();
        p.translate(c);
        p.rotate(m_rotation);
        p.translate(-c);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothRotation);
        exposed = p.transform().inverted().mapRect(QRectF(viewport()->rect())).toAlignedRect();
    }
    const QRectF imgView = imageToView(QRectF(m_doc->rect()));
    if (m_dropShadow) {
        for (int i = 1; i <= 6; ++i) {
            p.setPen(QColor(0, 0, 0, (dark ? 70 : 40) * (7 - i) / 6));
            p.setBrush(Qt::NoBrush);
            p.drawRect(imgView.adjusted(-i, -i, i - 1, i - 1).translated(1, 1));
        }
    }
    p.setPen(QColor(0, 0, 0, dark ? 160 : 70));
    p.drawRect(imgView.adjusted(-1, -1, 0, 0));

    const QRectF visible = imgView.intersected(QRectF(exposed));
    if (!visible.isEmpty()) {
        // Fijo en la ventana, como en Paint.NET: no cambia con el zum ni al desplazarse.
        p.fillRect(visible, QBrush(m_checker));

        const QRect src = QRectF(viewToImage(visible.topLeft()), viewToImage(visible.bottomRight()))
                              .toAlignedRect()
                              .intersected(m_doc->rect());
        // Siempre la imagen entera, recortada a lo que se repinta: un trozo suelto (lo que cambia
        // al mover píxeles, por ejemplo) Qt lo colocaba con otro redondeo que el resto, y con zum
        // o escalas fraccionarias el fondo de alrededor parecía moverse un píxel.
        p.save();
        p.setClipRect(visible, Qt::IntersectClip);
        // (Girada, suavizada solo si se pide en Ver: si no, el pixel art se veía borroso.)
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0 || (m_rotation != 0 && m_smoothRotation));
        p.drawImage(imgView, m_composite);
        p.restore();
        if (m_pixelGrid && m_zoom >= 2)
            drawPixelGrid(p, src);
    }

    drawSelection(p);

    if (m_tool) {
        p.save();
        m_tool->paintOverlay(p);
        p.restore();
        const qreal diameter = m_tool->brushOutlineDiameter();
        if (diameter > 0 && m_mouseInside && !m_panning) {
            const qreal r = diameter * scale() / 2.0;
            if (r >= 2.0) {
                p.setRenderHint(QPainter::Antialiasing);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor(255, 255, 255, 190), 0));
                p.drawEllipse(m_mouseView, r + 1, r + 1);
                p.setPen(QPen(QColor(0, 0, 0, 190), 0));
                p.drawEllipse(m_mouseView, r, r);
            }
        }
    }
    drawRemotePointers(p);
}

QRect Canvas::remotePointerRect(const RemotePointer& r) const
{
    const QPointF v = imageToView(r.pos);
    const QFontMetrics fm(font());
    const qreal width = 10 + 8 + (r.tool.isNull() ? 0 : 19) + fm.horizontalAdvance(r.name) + 4;
    const qreal height = 16 + std::max(18, fm.height() + 4) + 2;
    QRectF rect(v.x() - 3, v.y() - 3, width + 6, height + 6);
    // El globo del chat, encima.
    if (!r.bubble.isEmpty())
        rect |= QRectF(v.x(), v.y() - 12 - fm.lineSpacing() * 5 - 20, 270, fm.lineSpacing() * 5 + 32);
    // Lo que está dibujando, con su grosor.
    if (r.liveKind != 0 && !r.path.isEmpty()) {
        QRectF bounds(imageToView(r.path.first()), QSizeF(0, 0));
        for (const QPointF& pt : r.path)
            bounds |= QRectF(imageToView(pt), QSizeF(0.01, 0.01));
        const qreal pad = std::max<qreal>(4, r.liveSize * scale() / 2 + 4);
        rect |= bounds.adjusted(-pad, -pad, pad, pad);
    }
    return rect.toAlignedRect();
}

void Canvas::setRemotePointers(const QList<RemotePointer>& pointers)
{
    QRegion dirty;
    for (const RemotePointer& r : std::as_const(m_remotePointers))
        dirty += remotePointerRect(r);
    m_remotePointers = pointers;
    for (const RemotePointer& r : std::as_const(m_remotePointers))
        dirty += remotePointerRect(r);
    if (!dirty.isEmpty())
        updateView(dirty);
}

void Canvas::drawRemotePointers(QPainter& p) const
{
    if (m_remotePointers.isEmpty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm(font());
    // Primero, debajo de todos los punteros, lo que cada uno está dibujando (un esbozo: el cambio de
    // verdad llega al terminar).
    for (const RemotePointer& r : m_remotePointers) {
        if (r.liveKind == 0 || r.path.isEmpty())
            continue;
        const bool erase = !r.liveColor.isValid();
        QColor c = erase ? r.color : r.liveColor;
        c.setAlpha(erase ? 110 : 215);
        const qreal width = std::max<qreal>(1.0, r.liveSize * scale());
        QPen pen(c, r.liveKind == 1 ? width : std::max<qreal>(1.5, std::min<qreal>(width, 6)), erase ? Qt::DashLine : Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QPointF a = imageToView(r.path.first()), b = imageToView(r.path.last());
        if (r.liveKind == 2) {
            p.drawLine(a, b);
        } else if (r.liveKind == 3) {
            p.drawRect(QRectF(a, b).normalized());
        } else if (r.path.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(a, width / 2, width / 2);
        } else {
            QPainterPath line(a);
            for (int i = 1; i < r.path.size(); ++i)
                line.lineTo(imageToView(r.path[i]));
            p.drawPath(line);
        }
    }
    for (const RemotePointer& r : m_remotePointers) {
        const QPointF v = imageToView(r.pos);
        // El puntero: un triángulo de su color con borde blanco y una sombra suave.
        QPainterPath arrow;
        arrow.moveTo(v);
        arrow.lineTo(v + QPointF(0, 16));
        arrow.lineTo(v + QPointF(4.4, 12.2));
        arrow.lineTo(v + QPointF(11.5, 11.5));
        arrow.closeSubpath();
        p.setPen(QPen(QColor(0, 0, 0, 60), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(arrow.translated(1, 1));
        p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(r.color);
        p.drawPath(arrow);

        // Debajo, su nombre en una etiqueta de su color con el icono de la herramienta.
        const bool light = qGray(r.color.rgb()) > 165;
        const int iconSide = r.tool.isNull() ? 0 : 16;
        const qreal h = std::max(18, fm.height() + 4);
        const qreal w = 4 + (iconSide ? iconSide + 3 : 0) + fm.horizontalAdvance(r.name) + 6;
        const QRectF label(v.x() + 10, v.y() + 16, w, h);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(r.color);
        p.drawRoundedRect(label, 4, 4);
        qreal x = label.left() + 4;
        if (iconSide) {
            const QRectF iconBox(x - 1, label.center().y() - iconSide / 2.0 - 1, iconSide + 2, iconSide + 2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 235));
            p.drawRoundedRect(iconBox, 3, 3);
            r.tool.paint(&p, QRect(int(x), int(label.center().y() - iconSide / 2.0), iconSide, iconSide));
            x += iconSide + 3;
        }
        p.setPen(light ? Qt::black : Qt::white);
        p.drawText(QRectF(x, label.top(), label.right() - x, label.height()), Qt::AlignVCenter | Qt::AlignLeft, r.name);

        // Su último mensaje del chat: un globo encima del puntero, con un pico hacia él.
        if (!r.bubble.isEmpty()) {
            const int maxWidth = 240;
            const QRect textRect = fm.boundingRect(QRect(0, 0, maxWidth, 400), Qt::TextWordWrap, r.bubble);
            const qreal bw = textRect.width() + 16, bh = std::min(textRect.height(), fm.lineSpacing() * 5) + 10;
            const QRectF bubble(v.x() + 4, v.y() - 12 - bh, bw, bh);
            // Un solo contorno con la cola (antes, el globo y la cola se dibujaban por separado y se
            // veía la línea del borde entre los dos).
            const qreal k = 8, l = bubble.left(), t = bubble.top(), rr = bubble.right(), b = bubble.bottom();
            QPainterPath shape;
            shape.moveTo(l + k, t);
            shape.lineTo(rr - k, t);
            shape.quadTo(rr, t, rr, t + k);
            shape.lineTo(rr, b - k);
            shape.quadTo(rr, b, rr - k, b);
            shape.lineTo(l + 22, b);
            shape.lineTo(v.x() + 2, v.y() - 2);
            shape.lineTo(l + 10, b);
            shape.lineTo(l + k, b);
            shape.quadTo(l, b, l, b - k);
            shape.lineTo(l, t + k);
            shape.quadTo(l, t, l + k, t);
            shape.closeSubpath();
            p.setPen(QPen(QColor(0, 0, 0, 50), 3));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape.translated(1, 1));
            p.setPen(QPen(r.color, 1.5));
            p.setBrush(QColor(255, 255, 255, 245));
            p.drawPath(shape);
            p.setPen(QColor(20, 20, 20));
            p.drawText(bubble.adjusted(8, 5, -8, -5), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, r.bubble);
        }
    }
    p.restore();
}

void Canvas::drawSelection(QPainter& p) const
{
    const Selection& sel = m_doc->selection();
    if (sel.isEmpty() || (m_tool && m_tool->hidesSelectionOutline()))
        return;
    p.save();
    const QTransform t = imageToViewTransform();
    const bool highlight = m_tool && m_tool->highlightSelection();
    // Un rectángulo en píxeles enteros lleva el interior y el contorno en los mismos píxeles de
    // la pantalla (la imagen del interior, escalada, podía quedar un píxel corrida).
    const QRectF box = sel.isRectangular() && !t.isRotating() ? sel.path().boundingRect() : QRectF();
    const bool pixelBox = !box.isNull() && box == QRectF(box.toAlignedRect());
    if (highlight && !pixelBox) {
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1);
        p.drawImage(imageToView(QRectF(sel.bounds())), selectionTint());
    }
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    if (!box.isNull()) {
        drawMarchingRect(p, box, highlight && pixelBox);
    } else if (sel.isPathBased()) {
        // El trazado (elipse, lazo, lazo magnético...), suavizado, en píxeles del dispositivo.
        if (!(m_antsCache.selection == sel) || m_antsCache.transform != t || m_antsCache.dpr != dpr) {
            m_antsCache.selection = sel;
            m_antsCache.transform = t;
            m_antsCache.dpr = dpr;
            m_antsCache.path = (t * QTransform::fromScale(dpr, dpr)).map(sel.path());
            m_antsCache.antialias = true;
        }
        p.scale(1 / dpr, 1 / dpr);
        strokeAnts(p, m_antsCache.path, true);
    } else if (sel.outline().size() <= kMaxAntsLines) {
        // Los bordes entre píxeles (la varita mágica, la selección de objetos, las selecciones
        // combinadas...), unidos en contornos cerrados: las hormigas dan la vuelta a la selección.
        drawAntsLoops(p, sel.contours(), sel);
    } else if (!t.isRotating()) {
        // Tantísimos tramos (ruido): cada uno suelto, en el primer píxel de dentro de la selección
        // (Selection::outline dice de qué lado está el interior).
        auto dx = [&](qreal x) { return int(std::lround(t.map(QPointF(x, 0)).x() * dpr)); };
        auto dy = [&](qreal y) { return int(std::lround(t.map(QPointF(0, y)).y() * dpr)); };
        QVector<QLine> spans;
        const QVector<QLineF>& lines = sel.outline();
        spans.reserve(lines.size());
        for (const QLineF& l : lines) {
            if (l.y1() == l.y2()) {
                const int a = dx(std::min(l.x1(), l.x2())), b = std::max(a, dx(std::max(l.x1(), l.x2())) - 1);
                const int y = dy(l.y1()) - (l.x2() > l.x1() ? 0 : 1);
                spans.append(QLine(a, y, b, y));
            } else {
                const int a = dy(std::min(l.y1(), l.y2())), b = std::max(a, dy(std::max(l.y1(), l.y2())) - 1);
                const int x = dx(l.x1()) - (l.y2() < l.y1() ? 0 : 1);
                spans.append(QLine(x, a, x, b));
            }
        }
        p.scale(1 / dpr, 1 / dpr);
        drawAntsSpans(p, spans);
    } else {
        const QVector<QLineF>& lines = sel.outline();
        QVector<QLineF> mapped;
        mapped.reserve(lines.size());
        for (const QLineF& l : lines)
            mapped.append(t.map(l));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(kAntsDark, 0));
        p.drawLines(mapped);
        QPen dashes(kAntsLight, 0);
        dashes.setDashPattern({4, 4});
        dashes.setDashOffset(8 - m_antsOffset);
        p.setPen(dashes);
        p.drawLines(mapped);
    }
    p.restore();
}

void Canvas::strokeAnts(QPainter& p, const QPainterPath& path, bool antialias) const
{
    p.setRenderHint(QPainter::Antialiasing, antialias);
    p.setBrush(Qt::NoBrush);
    // Trazos de 4 px (8 con la pantalla al 200 %) que avanzan hacia el final del contorno.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const qreal unit = std::max(1.0, std::round(dpr));
    p.setPen(QPen(kAntsDark, 0, Qt::SolidLine, Qt::FlatCap, Qt::BevelJoin));
    p.drawPath(path);
    QPen dashes(kAntsLight, 0, Qt::CustomDashLine, Qt::FlatCap, Qt::BevelJoin);
    dashes.setDashPattern({4 * unit, 4 * unit});
    dashes.setDashOffset((8 - m_antsOffset) * unit);
    p.setPen(dashes);
    p.drawPath(path);
}

void Canvas::drawSelectionAnts(QPainter& p, const Selection& sel, const QTransform& imageTransform) const
{
    if (sel.isEmpty())
        return;
    if (sel.isPathBased()) {
        drawAntsPath(p, sel.path(), imageTransform);
        return;
    }
    p.save();
    drawAntsLoops(p, sel.contours(), {}, imageTransform);
    p.restore();
}

void Canvas::drawAntsPath(QPainter& p, const QPainterPath& imagePath, const QTransform& imageTransform) const
{
    if (imagePath.isEmpty())
        return;
    p.save();
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QPainterPath path = (imageTransform * imageToViewTransform() * QTransform::fromScale(dpr, dpr)).map(imagePath);
    p.scale(1 / dpr, 1 / dpr);
    strokeAnts(p, path, true);
    p.restore();
}

void Canvas::drawAntsLoops(QPainter& p, const QVector<QPolygonF>& loops, const Selection& cacheFor,
                           const QTransform& imageTransform) const
{
    const QTransform t = imageTransform * imageToViewTransform();
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const bool cached = !cacheFor.isEmpty();
    const bool valid = cached && m_antsCache.selection == cacheFor && m_antsCache.transform == t && m_antsCache.dpr == dpr;
    QPainterPath built;
    bool antialias = false;
    if (!valid) {
        const QTransform device = t * QTransform::fromScale(dpr, dpr);
        // Nítido si los píxeles de la imagen no se ven más pequeños que los de la pantalla (y la
        // vista no está girada): los vértices van al píxel entero de dentro de la selección.
        const bool crisp = !t.isRotating() && std::abs(device.m11()) >= 1.0 && std::abs(device.m22()) >= 1.0;
        antialias = !crisp;
        auto sign = [](qreal v) { return v > 0 ? 1 : v < 0 ? -1 : 0; };
        for (const QPolygonF& loop : loops) {
            const qsizetype n = loop.size();
            if (n < 3)
                continue;
            QPolygonF points;
            points.reserve(n + 1);
            for (qsizetype i = 0; i < n; ++i) {
                const QPointF v = device.map(loop[i]);
                if (!crisp) {
                    // Suavizado: al centro del píxel. Los vértices a menos de medio píxel del anterior
                    // sobran (los escalones del zum por debajo del 100 %).
                    const QPointF c = v + QPointF(0.5, 0.5);
                    if (points.isEmpty() || (c - points.last()).manhattanLength() >= 0.5)
                        points << c;
                    continue;
                }
                // La normal hacia dentro de un tramo con sentido (dx, dy) es (-dy, dx): el vértice va
                // un píxel hacia atrás en cada eje en el que alguno de sus dos tramos tiene el
                // interior de ese lado (así los cuatro bordes caen en el primer píxel de dentro).
                const QPointF a = loop[(i + n - 1) % n], b = loop[i], c = loop[(i + 1) % n];
                const int inX = sign(b.x() - a.x()), inY = sign(b.y() - a.y());
                const int outX = sign(c.x() - b.x()), outY = sign(c.y() - b.y());
                const bool left = inY > 0 || outY > 0, up = inX < 0 || outX < 0;
                points << QPointF(std::round(v.x()) - (left ? 1 : 0), std::round(v.y()) - (up ? 1 : 0));
            }
            if (points.size() < 3) {
                // Una isla de menos de un píxel de la pantalla: un punto.
                const QPointF c = device.map(loop.boundingRect().center());
                built.moveTo(c);
                built.lineTo(c + QPointF(1, 0));
                continue;
            }
            built.addPolygon(points);
            built.closeSubpath();
        }
        if (cached) {
            m_antsCache.selection = cacheFor;
            m_antsCache.transform = t;
            m_antsCache.dpr = dpr;
            m_antsCache.path = built;
            m_antsCache.antialias = antialias;
        }
    }
    p.save();
    p.scale(1 / dpr, 1 / dpr);
    if (cached)
        strokeAnts(p, m_antsCache.path, m_antsCache.antialias);
    else
        strokeAnts(p, built, antialias);
    p.restore();
}

void Canvas::drawAntsSpans(QPainter& p, const QVector<QLine>& spans) const
{
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setBrush(Qt::NoBrush);
    QPen white(kAntsLight, 0), black(kAntsDark, 0);
    p.setPen(black);
    p.drawLines(spans);
    // Tramos de cuatro píxeles blancos y cuatro negros según x + y (y el desplazamiento animado).
    QVector<QLine> dark;
    dark.reserve(spans.size() * 2);
    for (const QLine& s : spans) {
        const bool horizontal = s.y1() == s.y2();
        const int from = horizontal ? std::min(s.x1(), s.x2()) : std::min(s.y1(), s.y2());
        const int to = horizontal ? std::max(s.x1(), s.x2()) : std::max(s.y1(), s.y2());
        const int fixed = horizontal ? s.y1() : s.x1();
        for (int k = from; k <= to;) {
            const int phase = ((k + fixed - m_antsOffset) % 8 + 8) % 8;
            const int end = std::min(to, k + (phase < 4 ? 3 - phase : 7 - phase));
            if (phase < 4)
                dark.append(horizontal ? QLine(k, fixed, end, fixed) : QLine(fixed, k, fixed, end));
            k = end + 1;
        }
    }
    p.setPen(white);
    p.drawLines(dark);
}

void Canvas::drawMarchingRect(QPainter& p, const QRectF& imageRect, bool tint) const
{
    p.save();
    const QTransform t = imageToViewTransform();
    p.setBrush(Qt::NoBrush);
    if (t.isRotating()) {
        if (tint) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(kSelectionTint);
            p.drawPolygon(t.map(QPolygonF(imageRect)));
            p.setBrush(Qt::NoBrush);
        }
        // El rectángulo como contorno cerrado, en el sentido de las agujas del reloj (el interior a
        // su derecha): las hormigas le dan la vuelta.
        drawAntsLoops(p, {QPolygonF(QList<QPointF>{imageRect.topLeft(), imageRect.topRight(), imageRect.bottomRight(),
                                                   imageRect.bottomLeft()})});
        p.restore();
        return;
    }
    // Líneas nítidas en píxeles enteros de la pantalla (también con escalas fraccionarias), con
    // el mismo grosor y trazos con cualquier zum (suavizadas salían grises y borrosas). Como el
    // contorno de las demás selecciones, los cuatro bordes van en el primer píxel de dentro.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QRectF v = t.mapRect(imageRect);
    const int left = int(std::lround(v.left() * dpr)), top = int(std::lround(v.top() * dpr));
    const int right = std::max(left, int(std::lround(v.right() * dpr)) - 1), bottom = std::max(top, int(std::lround(v.bottom() * dpr)) - 1);
    p.scale(1 / dpr, 1 / dpr);
    if (tint)
        p.fillRect(QRect(QPoint(left, top), QPoint(right, bottom)), kSelectionTint);
    // El contorno de esos píxeles, en el sentido de las agujas del reloj (sin suavizar, los píxeles
    // enteros son los de la línea).
    QPainterPath box;
    box.addPolygon(QPolygonF(QList<QPointF>{QPointF(left, top), QPointF(right, top), QPointF(right, bottom), QPointF(left, bottom)}));
    box.closeSubpath();
    strokeAnts(p, box, false);
    p.restore();
}

void Canvas::drawPixelGrid(QPainter& p, const QRect& src) const
{
    // Como en Paint.NET: líneas punteadas (un píxel sí y otro no) de gris claro opaco, que se
    // distinguen sobre colores claros y oscuros.
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(QColor(187, 187, 187), 1);
    pen.setCosmetic(true);
    pen.setDashPattern({1.0, 1.0});
    p.setPen(pen);
    const QRectF dst = imageToView(QRectF(src));
    p.setClipRect(dst.adjusted(0, 0, 1, 1));
    const qreal top = std::floor(dst.top() / 2) * 2, left = std::floor(dst.left() / 2) * 2;
    for (int x = src.left(); x <= src.right() + 1; ++x) {
        const qreal vx = imageToView(QPointF(x, 0)).x();
        p.drawLine(QPointF(vx, top), QPointF(vx, dst.bottom()));
    }
    for (int y = src.top(); y <= src.bottom() + 1; ++y) {
        const qreal vy = imageToView(QPointF(0, y)).y();
        p.drawLine(QPointF(left, vy), QPointF(dst.right(), vy));
    }
    p.restore();
}

QRect Canvas::brushOutlineRect(const QPointF& center) const
{
    if (!m_tool)
        return {};
    const qreal r = m_tool->brushOutlineDiameter() * scale() / 2.0 + 3;
    return QRectF(center.x() - r, center.y() - r, 2 * r, 2 * r).toAlignedRect();
}

void Canvas::resizeEvent(QResizeEvent* e)
{
    QAbstractScrollArea::resizeEvent(e);
    const QSize vp = viewport()->size();
    if (m_lastViewport.isValid())
        m_origin += QPointF(vp.width() - m_lastViewport.width(), vp.height() - m_lastViewport.height()) / 2.0;
    m_lastViewport = vp;
    layoutRulers();
    clampOrigin();
    updateScrollBars();
    if (m_fitPending)
        fitInitial();
}

void Canvas::scrollContentsBy(int, int)
{
    // El usuario movió una barra (o la rueda): el origen sale de su valor.
    if (!m_syncingBars && m_doc) {
        const QSizeF content = QSizeF(m_doc->size()) * scale();
        const QSize vp = viewport()->size();
        qreal lo, hi;
        originLimits(content.width(), vp.width(), &lo, &hi);
        m_origin.setX(hi - horizontalScrollBar()->value());
        originLimits(content.height(), vp.height(), &lo, &hi);
        m_origin.setY(hi - verticalScrollBar()->value());
    }
    viewport()->update();
    updateRulers();
    emit viewChanged();
}

// ---------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------

bool Canvas::nudgePointer(QKeyEvent* e)
{
    QPoint d;
    switch (e->key()) {
    case Qt::Key_Left: d = QPoint(-1, 0); break;
    case Qt::Key_Right: d = QPoint(1, 0); break;
    case Qt::Key_Up: d = QPoint(0, -1); break;
    case Qt::Key_Down: d = QPoint(0, 1); break;
    default: return false;
    }
    if (e->modifiers() & ~(Qt::ControlModifier | Qt::KeypadModifier))
        return false;
    if (e->modifiers() & Qt::ControlModifier)
        d *= 10;
    const QPointF from = m_nudged ? m_nudgeView : m_mouseInside ? m_mouseView : QPointF(viewport()->mapFromGlobal(QCursor::pos()));
    // Del centro de un píxel al centro del siguiente.
    const QPointF img = viewToImage(from);
    const QPointF target = QPointF(std::floor(img.x()) + 0.5 + d.x(), std::floor(img.y()) + 0.5 + d.y());
    const QPointF previous = m_mouseView;
    m_nudgeView = imageToView(target);
    m_nudgeGlobal = viewport()->mapToGlobal(m_nudgeView).toPoint();
    m_nudged = true;
    m_mouseView = m_nudgeView;
    m_mouseInside = true;
    // El puntero del sistema va también (Wayland no deja moverlo: entonces solo lo sigue el pincel).
    QCursor::setPos(m_nudgeGlobal);
    emit cursorMoved(QPoint(int(std::floor(target.x())), int(std::floor(target.y()))), true);
    updateRulers();
    ToolEvent ev;
    ev.viewPos = m_nudgeView;
    ev.pos = target;
    ev.buttons = m_toolButton != Qt::NoButton ? m_lastButtons : Qt::NoButton;
    ev.modifiers = e->modifiers() & ~Qt::ControlModifier;
    ev.pressure = m_tabletDown ? m_pressure : 1.0;
    m_tool->mouseMove(ev);
    if (m_toolButton != Qt::NoButton)
        m_lastToolPos = m_nudgeView;
    if (m_tool->brushOutlineDiameter() > 0) {
        updateView(brushOutlineRect(previous));
        updateView(brushOutlineRect(m_mouseView));
    }
    return true;
}

ToolEvent Canvas::makeEvent(QMouseEvent* e) const
{
    ToolEvent ev;
    // El movimiento que llega de la posición en que dejaron el puntero las flechas: en la exacta
    // (la de la pantalla está redondeada a píxeles de la pantalla).
    const bool nudged = m_nudged && e->globalPosition().toPoint() == m_nudgeGlobal;
    ev.viewPos = nudged ? m_nudgeView : toLogical(e->position());
    ev.pos = viewToImage(ev.viewPos);
    ev.button = e->button();
    ev.buttons = e->buttons();
    ev.modifiers = e->modifiers();
    const auto type = e->pointingDevice() ? e->pointingDevice()->type() : QInputDevice::DeviceType::Mouse;
    const bool stylus = type == QInputDevice::DeviceType::Stylus || type == QInputDevice::DeviceType::Airbrush;
    ev.pressure = (m_tabletDown || stylus) ? m_pressure : 1.0;
    return ev;
}

bool Canvas::event(QEvent* e)
{
    // Mientras se escribe texto, las teclas sin modificadores van al lienzo y no disparan los
    // atajos de una letra (B = pincel, etc.).
    if (e->type() == QEvent::ShortcutOverride && m_tool) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const auto mods = ke->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier);
        if (m_tool->wantsKeyboard() && mods == Qt::NoModifier) {
            e->accept();
            return true;
        }
        static const QList<int> textKeys = {Qt::Key_Left, Qt::Key_Right, Qt::Key_Home, Qt::Key_End, Qt::Key_Backspace,
                                            Qt::Key_Delete, Qt::Key_Return, Qt::Key_Enter, Qt::Key_V};
        if (m_tool->wantsKeyboard() && mods == Qt::ControlModifier && textKeys.contains(ke->key())) {
            e->accept();
            return true;
        }
        const int k = ke->key();
        if (m_tool->isBusy() && (k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Escape)) {
            e->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(e);
}

bool Canvas::viewportEvent(QEvent* e)
{
    switch (e->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
        auto* te = static_cast<QTabletEvent*>(e);
        m_pressure = te->pressure();
        const bool eraser = te->pointerType() == QPointingDevice::PointerType::Eraser;
        if (eraser != m_eraserTip) {
            m_eraserTip = eraser;
            emit tabletEraserChanged(eraser);
        }
        if (e->type() == QEvent::TabletPress)
            m_tabletDown = true;
        else if (e->type() == QEvent::TabletRelease)
            m_tabletDown = false;
        e->ignore();
        return false;
    }
    case QEvent::NativeGesture: {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture) {
            setZoom(m_zoom * (1.0 + g->value()), toLogical(g->position()));
            return true;
        }
        break;
    }
    case QEvent::Enter:
        m_mouseInside = true;
        break;
    case QEvent::Leave:
        m_mouseInside = false;
        viewport()->update();
        emit cursorMoved({}, false);
        break;
    default:
        break;
    }
    return QAbstractScrollArea::viewportEvent(e);
}

void Canvas::mousePressEvent(QMouseEvent* e)
{
    setFocus(Qt::MouseFocusReason);
    m_fitPending = false;
    if (!m_doc)
        return;
    if (e->button() == Qt::MiddleButton || (m_spaceDown && e->button() == Qt::LeftButton)) {
        m_panning = true;
        m_panLast = toLogical(e->position());
        updateCursor();
        return;
    }
    if (m_panning)
        return;
    if (m_toolButton != Qt::NoButton) {
        if (e->button() != m_toolButton && m_tool && !m_extraButtons.contains(e->button())) {
            m_extraButtons.append(e->button());
            m_tool->extraButton(makeEvent(e), true);
        }
        return;
    }
    m_toolButton = e->button();
    m_lastToolPos = toLogical(e->position());
    m_lastButtons = e->buttons();
    m_lastModifiers = e->modifiers();
    if (m_tool)
        m_tool->mousePress(makeEvent(e));
    emit toolButtonChanged();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    mousePressEvent(e);
}

void Canvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF previous = m_mouseView;
    // El ratón de verdad se movió: las flechas vuelven a partir de donde esté.
    if (m_nudged && e->globalPosition().toPoint() != m_nudgeGlobal)
        m_nudged = false;
    m_mouseView = m_nudged ? m_nudgeView : toLogical(e->position());
    m_mouseInside = true;
    m_mouseSeen = true;
    if (!m_doc)
        return;
    const QPointF img = viewToImage(m_mouseView);
    const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
    emit cursorMoved(px, true);
    updateRulers();

    if (m_panning) {
        scrollByView(m_panLast - toLogical(e->position()));
        m_panLast = toLogical(e->position());
        return;
    }
    if (m_tool) {
        m_tool->mouseMove(makeEvent(e));
        if (m_tool->brushOutlineDiameter() > 0) {
            updateView(brushOutlineRect(previous));
            updateView(brushOutlineRect(m_mouseView));
        }
        if (m_toolButton != Qt::NoButton) {
            m_lastToolPos = toLogical(e->position());
            m_lastButtons = e->buttons();
            m_lastModifiers = e->modifiers();
            updateAutoScroll(toLogical(e->position()));
        }
    }
}

void Canvas::updateAutoScroll(const QPointF& pos)
{
    // Al arrastrar con una herramienta junto al borde (o fuera) de la vista, se desplaza más
    // deprisa cuanto más lejos esté el puntero.
    if (!m_autoScroll || !m_tool || m_tool->id() == ToolId::Pan || m_tool->id() == ToolId::Zoom) {
        m_autoScrollTimer.stop();
        return;
    }
    constexpr qreal margin = 20;
    auto axis = [](qreal v, qreal size) {
        if (v < margin)
            return -(margin - v);
        if (v > size - margin)
            return v - (size - margin);
        return 0.0;
    };
    const QPointF d(axis(pos.x(), viewport()->width()), axis(pos.y(), viewport()->height()));
    m_autoScrollDelta = QPointF(std::clamp(d.x() * 0.6, -40.0, 40.0), std::clamp(d.y() * 0.6, -40.0, 40.0));
    if (m_autoScrollDelta.isNull())
        m_autoScrollTimer.stop();
    else if (!m_autoScrollTimer.isActive())
        m_autoScrollTimer.start();
}

void Canvas::autoScrollStep()
{
    if (!m_doc || !m_tool || m_toolButton == Qt::NoButton || m_autoScrollDelta.isNull()) {
        m_autoScrollTimer.stop();
        return;
    }
    const QPointF before = m_origin;
    scrollByView(m_autoScrollDelta);
    if (m_origin == before) {
        m_autoScrollTimer.stop();
        return;
    }
    followPointer();
}

void Canvas::followPointer()
{
    // La vista se movió bajo el puntero quieto mientras se arrastra con una herramienta: le
    // llega la nueva posición en la imagen, como si se hubiera movido el ratón.
    if (!m_doc || !m_tool || m_toolButton == Qt::NoButton || m_panning)
        return;
    ToolEvent ev;
    ev.viewPos = m_lastToolPos;
    ev.pos = viewToImage(m_lastToolPos);
    ev.buttons = m_lastButtons;
    ev.modifiers = m_lastModifiers;
    ev.pressure = m_tabletDown ? m_pressure : 1.0;
    m_tool->mouseMove(ev);
}

void Canvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
        m_panning = false;
        updateCursor();
        return;
    }
    if (m_extraButtons.removeAll(e->button()) > 0) {
        if (m_tool && m_doc)
            m_tool->extraButton(makeEvent(e), false);
        return;
    }
    if (e->button() != m_toolButton)
        return;
    m_toolButton = Qt::NoButton;
    m_autoScrollTimer.stop();
    m_extraButtons.clear();
    if (m_tool && m_doc)
        m_tool->mouseRelease(makeEvent(e));
    emit toolButtonChanged();
}

void Canvas::wheelEvent(QWheelEvent* e)
{
    m_fitPending = false;
    // Con un botón pulsado (seleccionando, pintando...), lo que se hace sigue al puntero sobre la
    // imagen que se desplaza o se acerca debajo.
    if (m_toolButton != Qt::NoButton)
        m_lastToolPos = toLogical(e->position());
    // Alt + rueda: gira la vista (de 15 en 15 grados; con Mayús, de grado en grado), como girar el
    // lienzo con dos dedos en Kaint.APK.
    if (e->modifiers() & Qt::AltModifier) {
        const QPoint ad = e->angleDelta();
        // (Con Alt, algunos sistemas mandan la rueda vertical como horizontal.)
        const int delta = ad.y() != 0 ? ad.y() : ad.x();
        if (delta != 0) {
            const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 15.0;
            setViewRotation(m_rotation + (delta > 0 ? -step : step) * std::max(1, std::abs(delta) / 120));
        }
        e->accept();
        return;
    }
    if (e->modifiers() & Qt::ControlModifier) {
        const QPoint ad = e->angleDelta();
        const int delta = ad.y() != 0 ? ad.y() : ad.x();
        if (delta != 0) {
            // Pasos más finos que los de los botones, como la rueda en Paint.NET.
            qreal z = m_zoom * std::pow(1.12, delta / 120.0);
            if ((m_zoom < 1.0 && z > 1.0) || (m_zoom > 1.0 && z < 1.0))
                z = 1.0;
            setZoom(z, toLogical(e->position()));
        }
        e->accept();
        followPointer();
        return;
    }
    if ((e->modifiers() & Qt::ShiftModifier) && e->angleDelta().x() == 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - e->angleDelta().y());
        e->accept();
        followPointer();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
    followPointer();
}

void Canvas::keyPressEvent(QKeyEvent* e)
{
    m_fitPending = false;
    if (m_spaceDown && keyboardScroll(e))
        return;
    if (m_tool && m_doc && m_tool->arrowKeysMovePointer() && nudgePointer(e))
        return;
    if (m_tool && m_doc && m_tool->keyPress(e))
        return;
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_doc && !m_doc->selection().isEmpty()) {
        emit deselectRequested();
        return;
    }
    if (e->key() == Qt::Key_Space && !(m_tool && m_tool->wantsKeyboard())) {
        if (!e->isAutoRepeat()) {
            m_spaceDown = true;
            updateCursor();
        }
        return;
    }
    if (keyboardScroll(e))
        return;
    QAbstractScrollArea::keyPressEvent(e);
}

bool Canvas::keyboardScroll(QKeyEvent* e)
{
    QScrollBar* h = horizontalScrollBar();
    QScrollBar* v = verticalScrollBar();
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const int step = (ctrl ? 10 : 1) * 16;
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (!m_spaceDown)
            return false;
        if (e->key() == Qt::Key_Left)
            h->setValue(h->value() - step);
        else if (e->key() == Qt::Key_Right)
            h->setValue(h->value() + step);
        else if (e->key() == Qt::Key_Up)
            v->setValue(v->value() - step);
        else
            v->setValue(v->value() + step);
        return true;
    case Qt::Key_PageUp:
        (shift ? h : v)->triggerAction(QAbstractSlider::SliderPageStepSub);
        return true;
    case Qt::Key_PageDown:
        (shift ? h : v)->triggerAction(QAbstractSlider::SliderPageStepAdd);
        return true;
    case Qt::Key_Home:
    case Qt::Key_End:
        if (!m_doc)
            return true;
        if (ctrl) {
            centerOn(e->key() == Qt::Key_Home ? QPointF(0, 0) : QPointF(m_doc->size().width(), m_doc->size().height()));
        } else {
            const bool start = e->key() == Qt::Key_Home;
            m_origin.setX(edgeOrigin(Qt::Horizontal, start));
            if (shift || e->isAutoRepeat())
                m_origin.setY(edgeOrigin(Qt::Vertical, start));
            updateScrollBars();
            viewport()->update();
            updateRulers();
        }
        return true;
    default:
        return false;
    }
}

void Canvas::keyReleaseEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_spaceDown) {
        m_spaceDown = false;
        m_panning = false;
        updateCursor();
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(e);
}

void Canvas::focusOutEvent(QFocusEvent* e)
{
    m_spaceDown = false;
    QAbstractScrollArea::focusOutEvent(e);
}

void Canvas::inputMethodEvent(QInputMethodEvent* e)
{
    if (m_tool && m_doc)
        m_tool->inputMethod(e);
}

QVariant Canvas::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImEnabled)
        return m_tool && m_tool->wantsKeyboard();
    if (query == Qt::ImCursorRectangle && m_tool)
        return m_tool->inputCursorRect();
    return QAbstractScrollArea::inputMethodQuery(query);
}
