#include "app/Canvas.h"

#include "app/Icons.h"
#include "app/Theme.h"
#include "app/tools/Tool.h"
#include "core/Document.h"
#include "core/Raster.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTabletEvent>

#include <cmath>

namespace {
constexpr int kMargin = 24;
}

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
    m_checker = QPixmap::fromImage(Raster::checkerboard(8, QColor(191, 191, 191), QColor(143, 143, 143)));
    m_hRuler = new Ruler(Qt::Horizontal, this, this);
    m_vRuler = new Ruler(Qt::Vertical, this, this);
    m_rulerCorner = new QWidget(this);
    m_rulerCorner->setAutoFillBackground(true);
    // La esquina entre las reglas, del mismo color que ellas.
    auto paintCorner = [this] {
        QPalette pal = m_rulerCorner->palette();
        pal.setColor(QPalette::Window, Ruler::background());
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
        const QRect r = imageToView(QRectF(m_doc->selection().bounds())).toAlignedRect().adjusted(-2, -2, 2, 2);
        viewport()->update(r.intersected(viewport()->rect()));
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
    }
    m_doc = doc;
    m_toolButton = Qt::NoButton;

    if (m_doc) {
        connect(m_doc, &Document::pixelsChanged, this, &Canvas::onPixelsChanged);
        connect(m_doc, &Document::sizeChanged, this, &Canvas::onSizeChanged);
        connect(m_doc, &Document::selectionChanged, this, &Canvas::onSelectionChanged);
        connect(m_doc, &Document::colorSpaceChanged, this, &Canvas::onColorSpaceChanged);
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
    viewport()->update(imageToView(QRectF(r)).toAlignedRect().adjusted(-2, -2, 2, 2));
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
            m_antsTimer.start(120);
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
        return;
    }
    const QSizeF content = QSizeF(m_doc->size()) * scale();
    const QSize vp = viewport()->size();
    auto sync = [&](QScrollBar* bar, qreal c, int view, qreal origin) {
        qreal lo, hi;
        originLimits(c, view, &lo, &hi);
        bar->setRange(0, std::max(0, int(std::round(hi - lo))));
        bar->setPageStep(view);
        bar->setSingleStep(32);
        bar->setValue(int(std::round(hi - origin)));
    };
    sync(horizontalScrollBar(), content.width(), vp.width(), m_origin.x());
    sync(verticalScrollBar(), content.height(), vp.height(), m_origin.y());
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

void Canvas::fitInitial()
{
    // Como Paint.NET: 100 % si la imagen cabe; si no, ajustada a la ventana.
    if (!m_doc)
        return;
    const qreal dpr = viewport()->devicePixelRatioF();
    const qreal zx = (viewport()->width() - 2.0 * kMargin) / m_doc->size().width();
    const qreal zy = (viewport()->height() - 2.0 * kMargin) / m_doc->size().height();
    applyZoom(std::clamp(std::min(zx, zy) * dpr, 0.01, 1.0), std::nullopt);
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
    const qreal dpr = viewport()->devicePixelRatioF();
    const qreal zx = (viewport()->width() - 2.0 * kMargin) / m_doc->size().width();
    const qreal zy = (viewport()->height() - 2.0 * kMargin) / m_doc->size().height();
    m_fitPending = false;
    const qreal before = m_zoomBeforeFit;
    applyZoom(std::max(0.01, std::min(zx, zy) * dpr), std::nullopt);
    m_zoomBeforeFit = before;
    centerOn(QPointF(m_doc->size().width() / 2.0, m_doc->size().height() / 2.0));
}

void Canvas::toggleZoomToWindow()
{
    if (!m_doc)
        return;
    if (m_zoomBeforeFit > 0) {
        const qreal z = m_zoomBeforeFit;
        m_zoomBeforeFit = 0;
        setZoom(z);
        centerOn(m_centerBeforeFit);
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
    m_rulerCorner->setPalette(pal);
    updateRulers();
}

void Canvas::setUnit(Unit unit)
{
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
    // Interior de la selección en azul translúcido (como en Paint.NET).
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
                row[x] = qPremultiply(qRgba(0, 120, 215, m[x] * 45 / 255));
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
    Theme::paintWorkspace(p, viewport()->rect(), e->rect(), m_border);
    const bool dark = (m_border.isValid() ? m_border : Theme::workspaceColor()).lightness() < 128;
    if (!m_doc)
        return;

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

    const QRectF visible = imgView.intersected(QRectF(e->rect()));
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
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
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
    QPen white(Qt::white, 0);
    QPen black(Qt::black, 0);
    black.setDashPattern({4, 4});
    black.setDashOffset(m_antsOffset);
    if (!box.isNull()) {
        drawMarchingRect(p, box, highlight && pixelBox);
    } else if (sel.isPathBased()) {
        p.setRenderHint(QPainter::Antialiasing);
        const QPainterPath path = t.map(sel.path());
        p.setBrush(Qt::NoBrush);
        p.setPen(white);
        p.drawPath(path);
        p.setPen(black);
        p.drawPath(path);
    } else {
        const QVector<QLineF>& lines = sel.outline();
        QVector<QLineF> mapped;
        mapped.reserve(lines.size());
        for (const QLineF& l : lines)
            mapped.append(t.map(l));
        p.setPen(white);
        p.drawLines(mapped);
        p.setPen(black);
        p.drawLines(mapped);
    }
    p.restore();
}

void Canvas::drawMarchingRect(QPainter& p, const QRectF& imageRect, bool tint) const
{
    p.save();
    const QTransform t = imageToViewTransform();
    QPen white(Qt::white, 0);
    QPen black(Qt::black, 0);
    black.setDashPattern({4, 4});
    black.setDashOffset(m_antsOffset);
    p.setBrush(Qt::NoBrush);
    if (t.isRotating()) {
        const QPolygonF poly = t.map(QPolygonF(imageRect));
        p.setRenderHint(QPainter::Antialiasing);
        if (tint) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 120, 215, 45));
            p.drawPolygon(poly);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(white);
        p.drawPolygon(poly);
        p.setPen(black);
        p.drawPolygon(poly);
        p.restore();
        return;
    }
    // Líneas nítidas en píxeles enteros de la pantalla (también con escalas fraccionarias), con
    // el mismo grosor y trazos con cualquier zum (suavizadas salían grises y borrosas). Como el
    // contorno de las demás selecciones, los bordes izquierdo y de arriba van en el primer píxel
    // de dentro y el derecho y el de abajo en el primero de fuera.
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QRectF v = t.mapRect(imageRect);
    const int left = int(std::lround(v.left() * dpr)), top = int(std::lround(v.top() * dpr));
    const QRect r(left, top, int(std::lround(v.right() * dpr)) - left, int(std::lround(v.bottom() * dpr)) - top);
    p.scale(1 / dpr, 1 / dpr);
    if (tint)
        p.fillRect(r, QColor(0, 120, 215, 45));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(white);
    p.drawRect(r);
    p.setPen(black);
    p.drawRect(r);
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
}

// ---------------------------------------------------------------------------
// Entrada
// ---------------------------------------------------------------------------

ToolEvent Canvas::makeEvent(QMouseEvent* e) const
{
    ToolEvent ev;
    ev.viewPos = e->position();
    ev.pos = viewToImage(e->position());
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
            setZoom(m_zoom * (1.0 + g->value()), g->position());
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
        m_panLast = e->position();
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
    m_lastToolPos = e->position();
    m_lastButtons = e->buttons();
    m_lastModifiers = e->modifiers();
    if (m_tool)
        m_tool->mousePress(makeEvent(e));
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    mousePressEvent(e);
}

void Canvas::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF previous = m_mouseView;
    m_mouseView = e->position();
    m_mouseInside = true;
    if (!m_doc)
        return;
    const QPointF img = viewToImage(e->position());
    const QPoint px(int(std::floor(img.x())), int(std::floor(img.y())));
    emit cursorMoved(px, true);
    updateRulers();

    if (m_panning) {
        scrollByView(m_panLast - e->position());
        m_panLast = e->position();
        return;
    }
    if (m_tool) {
        m_tool->mouseMove(makeEvent(e));
        if (m_tool->brushOutlineDiameter() > 0) {
            viewport()->update(brushOutlineRect(previous));
            viewport()->update(brushOutlineRect(m_mouseView));
        }
        if (m_toolButton != Qt::NoButton) {
            m_lastToolPos = e->position();
            m_lastButtons = e->buttons();
            m_lastModifiers = e->modifiers();
            updateAutoScroll(e->position());
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
}

void Canvas::wheelEvent(QWheelEvent* e)
{
    m_fitPending = false;
    // Con un botón pulsado (seleccionando, pintando...), lo que se hace sigue al puntero sobre la
    // imagen que se desplaza o se acerca debajo.
    if (m_toolButton != Qt::NoButton)
        m_lastToolPos = e->position();
    if (e->modifiers() & Qt::ControlModifier) {
        const QPoint ad = e->angleDelta();
        const int delta = ad.y() != 0 ? ad.y() : ad.x();
        if (delta != 0) {
            // Pasos más finos que los de los botones, como la rueda en Paint.NET.
            qreal z = m_zoom * std::pow(1.12, delta / 120.0);
            if ((m_zoom < 1.0 && z > 1.0) || (m_zoom > 1.0 && z < 1.0))
                z = 1.0;
            setZoom(z, e->position());
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
