#include "app/Workspace.h"

#include "app/Canvas.h"
#include "app/Icons.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

FloatingPanel::FloatingPanel(const QString& id, const QString& title, QWidget* content, QWidget* parent)
    : QFrame(parent, Qt::SubWindow)
    , m_id(id)
    , m_fullTitle(title)
    , m_content(content)
{
    setObjectName(QStringLiteral("FloatingPanel"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setAutoFillBackground(true);
    setBackgroundRole(QPalette::Window);
    setMouseTracking(true); // cursor de cambio de tamaño en los bordes

    // Cabecera con el color de resalte del tema, como la de Paint.NET (que
    // usa el color de acento de Windows).
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("FloatingPanelTitle"));
    m_titleBar->setAutoFillBackground(true);
    m_titleBar->setBackgroundRole(QPalette::Highlight);
    m_titleBar->setForegroundRole(QPalette::HighlightedText);
    m_titleBar->setCursor(Qt::SizeAllCursor);
    m_titleBar->setMouseTracking(true);
    m_titleBar->installEventFilter(this);
    auto* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(7, 1, 1, 1);
    titleLayout->setSpacing(2);
    m_title = new QLabel(title, m_titleBar);
    m_title->setForegroundRole(QPalette::HighlightedText);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMouseTracking(true);
    m_title->installEventFilter(this);
    m_close = new QToolButton(m_titleBar);
    m_close->setAutoRaise(true);
    m_close->setIcon(Icons::theme({"window-close", "dialog-close"}));
    if (m_close->icon().isNull())
        m_close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_close->setIconSize(QSize(12, 12));
    m_close->setToolTip(tr("Cerrar"));
    m_close->setCursor(Qt::ArrowCursor);
    connect(m_close, &QToolButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    titleLayout->addWidget(m_title, 1);
    titleLayout->addWidget(m_close);

    // La cabecera se coloca a mano (de borde a borde); el contenido deja un
    // margen alrededor que sirve para arrastrar y cambiar el tamaño.
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->addWidget(content, 1);
    content->installEventFilter(this);
    updateGeometry();
}

QSize FloatingPanel::chromeSize() const
{
    // Márgenes del contenido más el marco (el diseño trabaja dentro de contentsRect()).
    const QMargins m = layout()->contentsMargins();
    const int fw = frameWidth();
    return QSize(m.left() + m.right() + 2 * fw, m.top() + m.bottom() + 2 * fw);
}

void FloatingPanel::updateTitleText()
{
    m_title->setText(m_title->fontMetrics().elidedText(m_fullTitle, Qt::ElideRight, std::max(0, m_title->width())));
    m_title->setToolTip(m_title->text() == m_fullTitle ? QString() : m_fullTitle);
}

bool FloatingPanel::event(QEvent* e)
{
    switch (e->type()) {
    case QEvent::Polish:
    case QEvent::StyleChange:
    case QEvent::FontChange: {
        // El diseño ya empieza dentro del marco; la cabecera ocupa la parte de arriba.
        const int titleHeight = m_titleBar->sizeHint().height();
        layout()->setContentsMargins(kBorder - frameWidth(), titleHeight + 2, kBorder - frameWidth(),
                                     kBorder - frameWidth());
        break;
    }
    default:
        break;
    }
    return QFrame::event(e);
}

void FloatingPanel::setAnchor(Corner corner, QPoint offset)
{
    m_corner = corner;
    m_offset = offset;
    reposition();
}

void FloatingPanel::reposition()
{
    QWidget* p = parentWidget();
    if (!p)
        return;
    const QSize area = p->size();
    QPoint pos;
    switch (m_corner) {
    case TopLeft: pos = m_offset; break;
    case TopRight: pos = QPoint(area.width() - width() - m_offset.x(), m_offset.y()); break;
    case BottomLeft: pos = QPoint(m_offset.x(), area.height() - height() - m_offset.y()); break;
    case BottomRight: pos = QPoint(area.width() - width() - m_offset.x(), area.height() - height() - m_offset.y()); break;
    }
    pos.setX(std::clamp(pos.x(), 0, std::max(0, area.width() - width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, area.height() - height())));
    move(pos);
}

void FloatingPanel::updateAnchorFromGeometry()
{
    QWidget* p = parentWidget();
    if (!p)
        return;
    const QRect g = geometry();
    const bool right = g.center().x() > p->width() / 2;
    const bool bottom = g.center().y() > p->height() / 2;
    m_corner = bottom ? (right ? BottomRight : BottomLeft) : (right ? TopRight : TopLeft);
    m_offset = QPoint(right ? p->width() - g.right() - 1 : g.left(), bottom ? p->height() - g.bottom() - 1 : g.top());
}

Qt::Edges FloatingPanel::edgesAt(const QPoint& pos) const
{
    if (!m_resizable)
        return {};
    const int b = kBorder;
    const int corner = 3 * b; // las esquinas se atrapan con más facilidad
    Qt::Edges e;
    if (pos.x() < b)
        e |= Qt::LeftEdge;
    else if (pos.x() >= width() - b)
        e |= Qt::RightEdge;
    if (pos.y() < b)
        e |= Qt::TopEdge;
    else if (pos.y() >= height() - b)
        e |= Qt::BottomEdge;
    if (e & (Qt::LeftEdge | Qt::RightEdge)) {
        if (pos.y() < corner)
            e |= Qt::TopEdge;
        else if (pos.y() >= height() - corner)
            e |= Qt::BottomEdge;
    }
    if (e & (Qt::TopEdge | Qt::BottomEdge)) {
        if (pos.x() < corner)
            e |= Qt::LeftEdge;
        else if (pos.x() >= width() - corner)
            e |= Qt::RightEdge;
    }
    return e;
}

void FloatingPanel::updateEdgeCursor(Qt::Edges edges)
{
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
    if (horizontal && vertical) {
        // "\" en las esquinas superior izquierda e inferior derecha, "/" en las otras.
        const bool falling = bool(edges & Qt::LeftEdge) == bool(edges & Qt::TopEdge);
        setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if (horizontal) {
        setCursor(Qt::SizeHorCursor);
    } else if (vertical) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
    // La cabecera tiene su propio cursor (mover) salvo en su franja de borde.
    if (edges)
        m_titleBar->setCursor(cursor());
    else
        m_titleBar->setCursor(Qt::SizeAllCursor);
}

QRect FloatingPanel::constrained(const QRect& proposed, Qt::Edges edges) const
{
    QRect g = proposed;
    const QRect area = parentWidget() ? parentWidget()->rect() : QRect(QPoint(0, 0), QSize(100000, 100000));
    if (edges & Qt::LeftEdge)
        g.setLeft(std::max(g.left(), area.left()));
    if (edges & Qt::RightEdge)
        g.setRight(std::min(g.right(), area.right()));
    if (edges & Qt::TopEdge)
        g.setTop(std::max(g.top(), area.top()));
    if (edges & Qt::BottomEdge)
        g.setBottom(std::min(g.bottom(), area.bottom()));

    const QSize chrome = chromeSize();
    const QSize minSize = minimumSizeHint().expandedTo(chrome + QSize(24, 24));
    QSize size = g.size().expandedTo(minSize);
    if (const auto* fit = dynamic_cast<const PanelContent*>(m_content)) {
        Qt::Orientations changed;
        if (edges & (Qt::LeftEdge | Qt::RightEdge))
            changed |= Qt::Horizontal;
        if (edges & (Qt::TopEdge | Qt::BottomEdge))
            changed |= Qt::Vertical;
        size = fit->fitSize(size - chrome, changed) + chrome;
    }
    // Los bordes que no se arrastran no se mueven.
    if (edges & Qt::LeftEdge)
        g.setLeft(g.right() - size.width() + 1);
    else
        g.setWidth(size.width());
    if (edges & Qt::TopEdge)
        g.setTop(g.bottom() - size.height() + 1);
    else
        g.setHeight(size.height());
    return g;
}

void FloatingPanel::fitToContent()
{
    if (const auto* fit = dynamic_cast<const PanelContent*>(m_content)) {
        const QSize chrome = chromeSize();
        resize(fit->fitSize(size() - chrome, Qt::Horizontal) + chrome);
    }
}

void FloatingPanel::mousePressEvent(QMouseEvent* e)
{
    const Qt::Edges edges = edgesAt(e->position().toPoint());
    if (e->button() == Qt::LeftButton && edges) {
        m_resizeEdges = edges;
        m_pressGlobal = e->globalPosition().toPoint();
        m_pressGeometry = geometry();
        raise();
        e->accept();
        return;
    }
    raise();
    QFrame::mousePressEvent(e);
}

void FloatingPanel::mouseMoveEvent(QMouseEvent* e)
{
    if (m_resizeEdges) {
        const QPoint d = e->globalPosition().toPoint() - m_pressGlobal;
        QRect g = m_pressGeometry;
        if (m_resizeEdges & Qt::LeftEdge)
            g.setLeft(g.left() + d.x());
        if (m_resizeEdges & Qt::RightEdge)
            g.setRight(g.right() + d.x());
        if (m_resizeEdges & Qt::TopEdge)
            g.setTop(g.top() + d.y());
        if (m_resizeEdges & Qt::BottomEdge)
            g.setBottom(g.bottom() + d.y());
        setGeometry(constrained(g, m_resizeEdges));
        e->accept();
        return;
    }
    updateEdgeCursor(edgesAt(e->position().toPoint()));
    QFrame::mouseMoveEvent(e);
}

void FloatingPanel::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_resizeEdges && e->button() == Qt::LeftButton) {
        m_resizeEdges = {};
        updateAnchorFromGeometry();
        saveState();
        e->accept();
        return;
    }
    QFrame::mouseReleaseEvent(e);
}

bool FloatingPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_title && e->type() == QEvent::Resize)
        updateTitleText();
    if (obj == m_content) {
        // Al entrar en el contenido se recupera el cursor normal.
        if (e->type() == QEvent::Enter && !m_resizeEdges)
            updateEdgeCursor({});
        return QFrame::eventFilter(obj, e);
    }
    if (obj != m_titleBar && obj != m_title)
        return QFrame::eventFilter(obj, e);

    // En la cabecera, la franja del borde también cambia el tamaño.
    auto* me = static_cast<QMouseEvent*>(e);
    switch (e->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        if (me->button() != Qt::LeftButton)
            break;
        const QPoint local = mapFromGlobal(me->globalPosition().toPoint());
        if (const Qt::Edges edges = edgesAt(local)) {
            m_resizeEdges = edges;
            m_pressGlobal = me->globalPosition().toPoint();
            m_pressGeometry = geometry();
            raise();
            return true;
        }
        m_dragging = true;
        m_dragOffset = me->globalPosition().toPoint() - pos();
        raise();
        return true;
    }
    case QEvent::MouseMove:
        if (m_resizeEdges) {
            QMouseEvent forwarded(QEvent::MouseMove, mapFromGlobal(me->globalPosition()), me->globalPosition(),
                                  me->button(), me->buttons(), me->modifiers());
            mouseMoveEvent(&forwarded);
            return true;
        }
        if (m_dragging) {
            QPoint p = me->globalPosition().toPoint() - m_dragOffset;
            const QSize area = parentWidget()->size();
            p.setX(std::clamp(p.x(), 0, std::max(0, area.width() - width())));
            p.setY(std::clamp(p.y(), 0, std::max(0, area.height() - height())));
            move(p);
            return true;
        }
        updateEdgeCursor(edgesAt(mapFromGlobal(me->globalPosition().toPoint())));
        break;
    case QEvent::MouseButtonRelease:
        if (m_resizeEdges) {
            m_resizeEdges = {};
            updateAnchorFromGeometry();
            saveState();
            return true;
        }
        if (m_dragging) {
            m_dragging = false;
            updateAnchorFromGeometry();
            return true;
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter(obj, e);
}

void FloatingPanel::resizeEvent(QResizeEvent* e)
{
    QFrame::resizeEvent(e);
    const int fw = frameWidth();
    m_titleBar->setGeometry(fw, fw, width() - 2 * fw, m_titleBar->sizeHint().height());
    // Un cambio de tamaño por código (p. ej. "Más >>" en Colores) conserva la
    // esquina anclada; el del usuario recalcula el ancla al soltar.
    if (!m_dragging && !m_resizeEdges)
        reposition();
}

void FloatingPanel::saveState() const
{
    QSettings s;
    s.beginGroup(QStringLiteral("panels/") + m_id);
    s.setValue(QStringLiteral("corner"), int(m_corner));
    s.setValue(QStringLiteral("offset"), m_offset);
    s.setValue(QStringLiteral("size"), size());
    s.setValue(QStringLiteral("visible"), !isHidden());
    s.endGroup();
}

void FloatingPanel::restoreState(Corner defaultCorner, QPoint defaultOffset, QSize defaultSize)
{
    m_defaultCorner = defaultCorner;
    m_defaultOffset = defaultOffset;
    m_defaultSize = defaultSize;
    ensurePolished(); // márgenes de la cabecera calculados antes de medir
    QSettings s;
    s.beginGroup(QStringLiteral("panels/") + m_id);
    QSize size = s.value(QStringLiteral("size")).toSize();
    if (!size.isValid())
        size = defaultSize.isValid() ? defaultSize : sizeHint();
    resize(size.expandedTo(minimumSizeHint()));
    m_corner = Corner(s.value(QStringLiteral("corner"), int(defaultCorner)).toInt());
    m_offset = s.value(QStringLiteral("offset"), defaultOffset).toPoint();
    setVisible(s.value(QStringLiteral("visible"), true).toBool());
    s.endGroup();
    fitToContent();
    reposition();
}

void FloatingPanel::resetToDefault()
{
    resize((m_defaultSize.isValid() ? m_defaultSize : sizeHint()).expandedTo(minimumSizeHint()));
    fitToContent();
    m_corner = m_defaultCorner;
    m_offset = m_defaultOffset;
    show();
    raise();
    reposition();
}

void FloatingPanel::setTranslucent(bool on)
{
    if (on && !m_opacity) {
        m_opacity = new QGraphicsOpacityEffect(this);
        m_opacity->setOpacity(underMouse() ? 1.0 : 0.75);
        setGraphicsEffect(m_opacity);
    } else if (!on && m_opacity) {
        setGraphicsEffect(nullptr); // destruye el efecto
        m_opacity = nullptr;
    }
}

void FloatingPanel::enterEvent(QEnterEvent* e)
{
    if (m_opacity)
        m_opacity->setOpacity(1.0);
    QFrame::enterEvent(e);
}

void FloatingPanel::leaveEvent(QEvent* e)
{
    if (m_opacity)
        m_opacity->setOpacity(0.75);
    QFrame::leaveEvent(e);
}

// ---------------------------------------------------------------------------

Workspace::Workspace(Canvas* canvas, QWidget* parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    m_canvas->setParent(this);
}

FloatingPanel* Workspace::addPanel(const QString& id, const QString& title, QWidget* content,
                                   FloatingPanel::Corner corner, QPoint offset, QSize size, bool resizable)
{
    auto* panel = new FloatingPanel(id, title, content, this);
    panel->setResizable(resizable);
    panel->restoreState(corner, offset, size);
    panel->raise();
    m_panels.append(panel);
    return panel;
}

void Workspace::saveState() const
{
    for (FloatingPanel* p : m_panels)
        p->saveState();
}

void Workspace::resetPanels()
{
    for (FloatingPanel* p : m_panels)
        p->resetToDefault();
}

void Workspace::setTranslucentPanels(bool on)
{
    for (FloatingPanel* p : m_panels)
        p->setTranslucent(on);
}

void Workspace::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_canvas->setGeometry(rect());
    for (FloatingPanel* p : m_panels)
        p->reposition();
}
