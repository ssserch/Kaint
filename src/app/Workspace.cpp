#include "app/Workspace.h"

#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/Theme.h"

#include <QAbstractButton>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Botón de cerrar de los paneles: el cuadrado rojo de Paint.NET o, con el tema de KDE, el icono
// del sistema.
class PanelCloseButton : public QAbstractButton {
public:
    explicit PanelCloseButton(QWidget* parent)
        : QAbstractButton(parent)
    {
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::ArrowCursor);
    }

    QSize sizeHint() const override
    {
        const int side = fontMetrics().height() + 2;
        return {side, side};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse() || isDown();
        if (Theme::current() == Theme::Scheme::Kde) {
            if (hot) {
                QColor c = palette().color(QPalette::HighlightedText);
                c.setAlpha(isDown() ? 90 : 50);
                p.fillRect(rect(), c);
            }
            icon().paint(&p, rect().adjusted(3, 3, -3, -3));
            return;
        }
        const Theme::Spec s = Theme::active();
        if (s.classic) {
            // Botón gris en relieve con la X negra, como en Windows 98.
            const QRect r = rect().adjusted(1, 1, -1, -1);
            const QColor face = s.panelClose;
            const QColor hi = isDown() ? face.darker(250) : QColor(Qt::white);
            const QColor lo = isDown() ? QColor(Qt::white) : face.darker(250);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.fillRect(r, face);
            p.setPen(hi);
            p.drawLine(r.topLeft(), r.topRight());
            p.drawLine(r.topLeft(), r.bottomLeft());
            p.setPen(lo);
            p.drawLine(r.bottomLeft(), r.bottomRight());
            p.drawLine(r.topRight(), r.bottomRight());
            p.setPen(QPen(s.text, 2));
            const QPointF c = QRectF(r).center() + (isDown() ? QPointF(1, 1) : QPointF());
            const qreal k = r.width() * 0.2;
            p.drawLine(c + QPointF(-k, -k), c + QPointF(k, k));
            p.drawLine(c + QPointF(-k, k), c + QPointF(k, -k));
            return;
        }
        const QColor bg = isDown() ? s.panelClose.darker(115) : underMouse() ? s.panelClose.lighter(118) : s.panelClose;
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, s.radius / 2.0, s.radius / 2.0);
        p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
        const qreal k = r.width() * 0.16;
        const QPointF c = r.center();
        p.drawLine(c + QPointF(-k, -k), c + QPointF(k, k));
        p.drawLine(c + QPointF(-k, k), c + QPointF(k, -k));
    }
};

} // namespace

FloatingPanel::FloatingPanel(const QString& id, const QString& title, QWidget* content, QWidget* parent)
    : QFrame(parent, Qt::SubWindow)
    , m_id(id)
    , m_fullTitle(title)
    , m_content(content)
{
    setObjectName(QStringLiteral("FloatingPanel"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setBackgroundRole(QPalette::Window);
    setMouseTracking(true);

    // Cabecera: con los temas de Paint.NET, del color del panel (o el del tema personalizado);
    // con el de KDE, del color de resalte.
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("FloatingPanelTitle"));
    m_titleBar->setCursor(Qt::SizeAllCursor);
    m_titleBar->setMouseTracking(true);
    m_titleBar->installEventFilter(this);
    auto* titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(7, 1, 1, 1);
    titleLayout->setSpacing(2);
    m_title = new QLabel(title, m_titleBar);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_title->setMouseTracking(true);
    m_title->installEventFilter(this);
    m_close = new PanelCloseButton(m_titleBar);
    m_close->setIcon(Icons::theme({"window-close", "dialog-close"}));
    if (m_close->icon().isNull())
        m_close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_close->setToolTip(tr("Cerrar"));
    connect(m_close, &QAbstractButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    titleLayout->addWidget(m_title, 1);
    titleLayout->addWidget(m_close);

    // La cabecera se coloca a mano (de borde a borde); el contenido deja un margen alrededor
    // que sirve para arrastrar y cambiar el tamaño.
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->addWidget(content, 1);
    content->installEventFilter(this);
    updateColors();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        updateColors();
        QEvent relayout(QEvent::StyleChange);
        event(&relayout);
        const int inset = titleInset();
        m_titleBar->setGeometry(inset, inset, width() - 2 * inset, m_titleBar->sizeHint().height());
        if (m_opacity && !underMouse())
            m_opacity->setOpacity(Theme::active().panelOpacity / 100.0);
        update();
    });
    updateGeometry();
}

void FloatingPanel::updateColors()
{
    const bool kde = Theme::current() == Theme::Scheme::Kde;
    setAutoFillBackground(kde);
    m_titleBar->setAutoFillBackground(kde);
    m_titleBar->setBackgroundRole(QPalette::Highlight);
    m_title->setForegroundRole(kde ? QPalette::HighlightedText : QPalette::WindowText);
    if (kde) {
        m_titleBar->setPalette(QPalette());
        return;
    }
    QPalette p = m_titleBar->palette();
    p.setColor(QPalette::WindowText, Theme::active().panelTitleText);
    m_titleBar->setPalette(p);
    QFont font = m_title->font();
    font.setBold(Theme::active().classic);
    m_title->setFont(font);
}

void FloatingPanel::paintEvent(QPaintEvent* e)
{
    if (Theme::current() == Theme::Scheme::Kde) {
        QFrame::paintEvent(e);
        return;
    }
    const Theme::Spec s = Theme::active();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, s.radius > 0);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), s.radius, s.radius);
    p.fillPath(shape, s.panel);
    // Cabecera de un color o en degradado (Windows 98: azul marino a azul).
    const QRect title(0, 0, width(), m_titleBar->geometry().bottom() + 1);
    if (s.panelTitle != s.panel || s.panelTitleEnd != s.panelTitle) {
        QLinearGradient gradient(title.topLeft(), title.topRight());
        gradient.setColorAt(0, s.panelTitle);
        gradient.setColorAt(1, s.panelTitleEnd);
        p.save();
        p.setClipPath(shape);
        p.fillRect(s.classic ? title.adjusted(3, 3, -3, 0) : title, gradient);
        p.restore();
    }
    if (s.classic) {
        // Marco en relieve de dos líneas, como las ventanas de 98.css.
        const QRect r = rect().adjusted(0, 0, -1, -1);
        const QColor face = s.panel;
        auto bevel = [&p](const QRect& box, const QColor& topLeft, const QColor& bottomRight) {
            p.setPen(topLeft);
            p.drawLine(box.topLeft(), box.topRight());
            p.drawLine(box.topLeft(), box.bottomLeft());
            p.setPen(bottomRight);
            p.drawLine(box.bottomLeft(), box.bottomRight());
            p.drawLine(box.topRight(), box.bottomRight());
        };
        p.setRenderHint(QPainter::Antialiasing, false);
        bevel(r, face.lighter(115), face.darker(280));
        bevel(r.adjusted(1, 1, -1, -1), Qt::white, face.darker(150));
        return;
    }
    QColor border = s.panel;
    border = QColor::fromRgbF(border.redF() * 0.78f + s.text.redF() * 0.22f, border.greenF() * 0.78f + s.text.greenF() * 0.22f,
                              border.blueF() * 0.78f + s.text.blueF() * 0.22f);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
}

int FloatingPanel::titleInset() const
{
    // Con el estilo clásico, la cabecera queda dentro del marco en relieve (como en 98.css).
    return Theme::current() != Theme::Scheme::Kde && Theme::active().classic ? 3 : frameWidth();
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
        const int titleHeight = m_titleBar->sizeHint().height() + titleInset() - frameWidth();
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
    const int corner = 3 * b;
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
        const bool falling = bool(edges & Qt::LeftEdge) == bool(edges & Qt::TopEdge);
        setCursor(falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    } else if (horizontal) {
        setCursor(Qt::SizeHorCursor);
    } else if (vertical) {
        setCursor(Qt::SizeVerCursor);
    } else {
        unsetCursor();
    }
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
    const int inset = titleInset();
    m_titleBar->setGeometry(inset, inset, width() - 2 * inset, m_titleBar->sizeHint().height());
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
    if (const auto* fit = dynamic_cast<const PanelContent*>(m_content))
        s.setValue(QStringLiteral("layout"), fit->layoutState());
    s.setValue(QStringLiteral("visible"), !isHidden());
    s.endGroup();
}

void FloatingPanel::restoreState(Corner defaultCorner, QPoint defaultOffset, QSize defaultSize)
{
    m_defaultCorner = defaultCorner;
    m_defaultOffset = defaultOffset;
    m_defaultSize = defaultSize;
    ensurePolished();
    QSettings s;
    s.beginGroup(QStringLiteral("panels/") + m_id);
    QSize size = s.value(QStringLiteral("size")).toSize();
    if (const auto* fit = dynamic_cast<const PanelContent*>(m_content)) {
        const QSize fromLayout = fit->sizeForLayout(s.value(QStringLiteral("layout")));
        size = fromLayout.isValid() ? fromLayout + chromeSize() : QSize();
    }
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
        m_opacity->setOpacity(underMouse() ? 1.0 : Theme::active().panelOpacity / 100.0);
        setGraphicsEffect(m_opacity);
    } else if (!on && m_opacity) {
        setGraphicsEffect(nullptr);
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
        m_opacity->setOpacity(Theme::active().panelOpacity / 100.0);
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
