#include "app/Workspace.h"

#include "app/Animations.h"
#include "app/Canvas.h"
#include "app/Icons.h"
#include "app/Theme.h"
#include "app/WindowFrame.h"
#include "core/Document.h"

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
#include <QVariantAnimation>

namespace {

FloatingPanel::Style g_chromeStyle = FloatingPanel::Style::Windows10;
int g_chromeBorder = 1;
bool g_snapping = true;
bool g_overImageOnly = true;

// Marcos propios de las ventanas flotantes con los temas Windows XP (el de Luna: la cabecera azul
// en degradado con las esquinas de arriba redondeadas y el borde azul) y Windows 7 (el de cristal
// de Aero: el borde translúcido y el contenido en un recuadro), con los dos aspectos.
enum class PanelFrame { Plain, Luna, Aero };

PanelFrame panelFrame()
{
    if (Theme::current() == Theme::Scheme::Kde)
        return PanelFrame::Plain;
    const Theme::Spec s = Theme::active();
    if (s.classic)
        return PanelFrame::Plain;
    return s.xp ? PanelFrame::Luna : s.win7 ? PanelFrame::Aero : PanelFrame::Plain;
}

bool windows10Chrome()
{
    // El aspecto de Paint.NET lleva siempre su botón de cerrar rojo y cuadrado.
    return (g_chromeStyle == FloatingPanel::Style::Windows10 || Theme::paintNetLook()) && Theme::current() != Theme::Scheme::Kde
        && !Theme::active().classic && panelFrame() == PanelFrame::Plain;
}

// El aspecto de Paint.NET sin marco (con los de XP y 7, el marco manda).
bool paintNetChrome()
{
    return Theme::paintNetLook() && panelFrame() == PanelFrame::Plain;
}

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
        // Con el estilo de Windows 10, el cuadrado de 19 px de paint.js; con XP, el de Luna; con
        // Windows 7, el rojo de cristal, más ancho.
        switch (panelFrame()) {
        case PanelFrame::Luna: return {19, 19};
        case PanelFrame::Aero: return {28, 17};
        case PanelFrame::Plain: break;
        }
        const int side = windows10Chrome() ? std::max(19, fontMetrics().height() + 4) : fontMetrics().height() + 2;
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
        if (const PanelFrame frame = panelFrame(); frame != PanelFrame::Plain) {
            WindowFrame::paintPanelClose(p, rect(), frame == PanelFrame::Luna ? WindowFrame::Buttons::WindowsXP : WindowFrame::Buttons::Windows7,
                                         underMouse(), isDown());
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
        if (windows10Chrome()) {
            // Como en Paint.NET: cuadrado rojo apenas redondeado con una «x» blanca en negrita.
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(rect()), 2, 2);
            QFont f = font();
            f.setBold(true);
            f.setPixelSize(std::max(10, height() / 2));
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRectF(rect()).adjusted(0, -1, 0, -1), Qt::AlignCenter, QStringLiteral("x"));
            return;
        }
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
        applyChrome();
        if (m_opacity && !m_fade)
            m_opacity->setOpacity(restingOpacity());
    });
    applyChrome();
    updateGeometry();
}

void FloatingPanel::setChrome(Style style, int border)
{
    g_chromeStyle = style;
    g_chromeBorder = std::clamp(border, 1, 12);
}

void FloatingPanel::setSnapping(bool on)
{
    g_snapping = on;
}

QPoint FloatingPanel::snapped(QPoint pos) const
{
    // Como en Paint.NET: cerca de un borde del área de trabajo, la ventana se pega a él (con el
    // margen de las colocadas de fábrica); cerca de otra ventana, junto a ella con una pequeña
    // separación o alineada con su borde. Gana el borde más cercano de cada eje.
    constexpr int kReach = 14;
    constexpr int kInset = 8;
    constexpr int kGap = 6;
    const QRect r(pos, size());
    int dx = kReach + 1, dy = kReach + 1;
    auto nearer = [](int& best, int delta) {
        if (std::abs(delta) < std::abs(best))
            best = delta;
    };
    if (parentWidget()) {
        for (const FloatingPanel* o : parentWidget()->findChildren<FloatingPanel*>(Qt::FindDirectChildrenOnly)) {
            if (o == this || o->isHidden())
                continue;
            const QRect g = o->geometry();
            if (r.top() <= g.bottom() + kReach && g.top() <= r.bottom() + kReach) {
                nearer(dx, g.right() + 1 + kGap - r.left());
                nearer(dx, g.left() - kGap - (r.right() + 1));
                nearer(dx, g.left() - r.left());
                nearer(dx, g.right() - r.right());
            }
            if (r.left() <= g.right() + kReach && g.left() <= r.right() + kReach) {
                nearer(dy, g.bottom() + 1 + kGap - r.top());
                nearer(dy, g.top() - kGap - (r.bottom() + 1));
                nearer(dy, g.top() - r.top());
                nearer(dy, g.bottom() - r.bottom());
            }
        }
    }
    const QRect a = area();
    nearer(dx, a.left() + kInset - r.left());
    nearer(dx, a.right() + 1 - kInset - (r.right() + 1));
    nearer(dy, a.top() + kInset - r.top());
    nearer(dy, a.bottom() + 1 - kInset - (r.bottom() + 1));
    return pos + QPoint(std::abs(dx) <= kReach ? dx : 0, std::abs(dy) <= kReach ? dy : 0);
}

FloatingPanel::Style FloatingPanel::chromeStyle()
{
    return g_chromeStyle;
}

int FloatingPanel::chromeBorder()
{
    return g_chromeBorder;
}

bool FloatingPanel::windows10() const
{
    return windows10Chrome();
}

int FloatingPanel::borderWidth() const
{
    if (Theme::current() == Theme::Scheme::Kde)
        return frameWidth();
    // XP: el borde azul de tres píxeles; Windows 7: el de cristal, de seis.
    switch (panelFrame()) {
    case PanelFrame::Luna: return 3;
    case PanelFrame::Aero: return 6;
    case PanelFrame::Plain: break;
    }
    // Como Paint.NET: sin borde.
    if (Theme::paintNetLook())
        return 0;
    return Theme::active().classic ? 2 : g_chromeBorder;
}

int FloatingPanel::grip() const
{
    return std::max(kBorder, borderWidth());
}

void FloatingPanel::applyChrome()
{
    // Las ventanas que encajan su contenido (Herramientas) conservan sus columnas: crecen o
    // encogen con la cabecera y el borde.
    const auto* fit = dynamic_cast<const PanelContent*>(m_content);
    const QVariant layoutState = fit ? fit->layoutState() : QVariant();
    const QSize contentBefore = m_content->size();
    // paint.js: 5 px arriba, abajo y a la izquierda del título; el botón de cerrar casi en el borde.
    // Paint.NET: una cabecera algo más baja, con el título a 8 px.
    auto* titleLayout = static_cast<QBoxLayout*>(m_titleBar->layout());
    if (panelFrame() == PanelFrame::Luna)
        titleLayout->setContentsMargins(6, 3, 4, 3);
    else if (panelFrame() == PanelFrame::Aero)
        titleLayout->setContentsMargins(8, 4, 6, 3);
    else if (Theme::paintNetLook())
        titleLayout->setContentsMargins(8, 3, 3, 3);
    else if (windows10())
        titleLayout->setContentsMargins(6, 5, 2, 5);
    else
        titleLayout->setContentsMargins(7, 1, 1, 1);
    m_close->updateGeometry();
    titleLayout->invalidate();
    QEvent relayout(QEvent::StyleChange);
    event(&relayout);
    const int inset = titleInset();
    m_titleBar->setGeometry(inset, inset, width() - 2 * inset, m_titleBar->sizeHint().height());
    if (fit) {
        // Si lo que pide la distribución guardada es lo mismo que ya mide el contenido (salvo el
        // redondeo de la escala de Colores), se queda exactamente igual: si no, cada vez que se
        // aplicaba un ajuste (los cursores, por ejemplo) la ventana cambiaba un poco de tamaño.
        QSize content = fit->sizeForLayout(layoutState);
        if (content.isValid() && contentBefore.isValid() && std::abs(content.width() - contentBefore.width()) <= 2
            && std::abs(content.height() - contentBefore.height()) <= 2)
            content = contentBefore;
        if (content.isValid() && content + chromeSize() != size())
            resize(content + chromeSize());
    }
    update();
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
    // El color va también en la etiqueta: con la hoja de estilo, no hereda el de la cabecera.
    const Theme::Spec s = Theme::active();
    for (QWidget* w : {m_titleBar, static_cast<QWidget*>(m_title)}) {
        QPalette p = w->palette();
        p.setColor(QPalette::WindowText, s.panelTitleText);
        w->setPalette(p);
    }
    QFont font = m_title->font();
    font.setBold(s.classic || panelFrame() == PanelFrame::Luna);
    font.setStyleStrategy(s.pixelFont ? QFont::NoAntialias : QFont::PreferDefault);
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
    if (const PanelFrame frame = panelFrame(); frame != PanelFrame::Plain) {
        paintFrame(p, frame == PanelFrame::Luna);
        return;
    }
    // Con el estilo de Windows 10, las esquinas algo redondeadas de las ventanas de Paint.NET; el
    // borde, del grosor elegido. Con el aspecto de Paint.NET, rectas y sin borde.
    const bool paintNet = Theme::paintNetLook();
    const qreal radius = paintNet ? 0 : windows10() ? 7 : s.radius;
    const qreal half = s.classic ? 0.5 : borderWidth() / 2.0;
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()).adjusted(half, half, -half, -half), radius, radius);
    p.setRenderHint(QPainter::Antialiasing, radius > 0);
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
    if (paintNet)
        return;
    QColor border = s.panel;
    border = QColor::fromRgbF(border.redF() * 0.78f + s.text.redF() * 0.22f, border.greenF() * 0.78f + s.text.greenF() * 0.22f,
                              border.blueF() * 0.78f + s.text.blueF() * 0.22f);
    p.setPen(QPen(border, borderWidth(), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
}

// Los marcos de XP y de Windows 7: la cabecera ocupa todo el ancho (sobre el borde) y el contenido
// va dentro del borde, debajo.
void FloatingPanel::paintFrame(QPainter& p, bool luna)
{
    const Theme::Spec s = Theme::active();
    const QRectF r = QRectF(rect());
    const int titleBottom = m_titleBar->geometry().bottom() + 1;
    QRect body = layout()->contentsRect();
    p.setRenderHint(QPainter::Antialiasing);
    if (luna) {
        // XP.css: la cabecera de Luna con las esquinas de arriba redondeadas y el borde azul.
        auto roundedTop = [](const QRectF& box) {
            constexpr qreal rad = 7;
            QPainterPath path;
            path.moveTo(box.left(), box.bottom());
            path.lineTo(box.left(), box.top() + rad);
            path.arcTo(QRectF(box.left(), box.top(), 2 * rad, 2 * rad), 180, -90);
            path.lineTo(box.right() - rad, box.top());
            path.arcTo(QRectF(box.right() - 2 * rad, box.top(), 2 * rad, 2 * rad), 90, -90);
            path.lineTo(box.bottomRight());
            path.closeSubpath();
            return path;
        };
        const QPainterPath shape = roundedTop(r);
        p.fillPath(shape, QColor(0x00, 0x55, 0xe5));
        QLinearGradient title(QPointF(0, 0), QPointF(0, titleBottom));
        title.setColorAt(0, QColor(0x09, 0x97, 0xff));
        title.setColorAt(0.08, QColor(0x00, 0x53, 0xee));
        title.setColorAt(0.40, QColor(0x00, 0x50, 0xee));
        title.setColorAt(0.88, QColor(0x00, 0x66, 0xff));
        title.setColorAt(0.93, QColor(0x00, 0x66, 0xff));
        title.setColorAt(0.95, QColor(0x00, 0x5b, 0xff));
        title.setColorAt(0.96, QColor(0x00, 0x3d, 0xd7));
        title.setColorAt(1, QColor(0x00, 0x3d, 0xd7));
        p.save();
        p.setClipPath(shape);
        p.fillRect(QRectF(0, 0, width(), titleBottom), title);
        // Los lados: algo más claros por dentro.
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QColor(0x16, 0x6a, 0xee));
        p.drawLine(QPoint(1, titleBottom), QPoint(1, height() - 2));
        p.drawLine(QPoint(width() - 2, titleBottom), QPoint(width() - 2, height() - 2));
        p.drawLine(QPoint(1, height() - 2), QPoint(width() - 2, height() - 2));
        p.restore();
        p.setPen(QPen(QColor(0x00, 0x13, 0x8c), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(roundedTop(r.adjusted(0.5, 0.5, -0.5, -0.5)));
        p.setRenderHint(QPainter::Antialiasing, false);
        body = QRect(3, titleBottom, width() - 6, height() - titleBottom - 3);
        p.fillRect(body, s.panel);
        return;
    }
    // 7.css: el cristal de Aero (un azul translúcido con brillos), un contorno oscuro con otro
    // blanco por dentro y el contenido gris claro en su recuadro.
    QPainterPath shape;
    shape.addRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.fillPath(shape, QColor(69, 128, 196, 158));
    QLinearGradient across(r.topLeft(), r.topRight());
    across.setColorAt(0, QColor(255, 255, 255, 102));
    across.setColorAt(0.5, QColor(0, 0, 0, 26));
    across.setColorAt(1, QColor(255, 255, 255, 51));
    p.fillPath(shape, across);
    QLinearGradient shine(r.topLeft(), r.bottomLeft());
    shine.setColorAt(0, Qt::transparent);
    shine.setColorAt(0.2, Qt::transparent);
    shine.setColorAt(0.4, QColor(255, 255, 255, 179));
    shine.setColorAt(0.41, Qt::transparent);
    shine.setColorAt(1, Qt::transparent);
    p.fillPath(shape, shine);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 179), 1));
    p.drawPath(shape);
    p.setPen(QPen(QColor(255, 255, 255, 170), 1));
    p.drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 5, 5);
    p.setRenderHint(QPainter::Antialiasing, false);
    body = body.adjusted(-1, -1, 1, 1);
    p.fillRect(body, s.panel);
    p.setPen(QColor(255, 255, 255, 153));
    p.drawRect(body.adjusted(-1, -1, 0, 0));
    p.setPen(QColor(0, 0, 0, 179));
    p.drawRect(body.adjusted(0, 0, -1, -1));
}

int FloatingPanel::titleInset() const
{
    // Con el estilo clásico, la cabecera queda dentro del marco en relieve (como en 98.css); con
    // los demás, dentro del borde; con los marcos de XP y 7, de lado a lado.
    if (Theme::current() == Theme::Scheme::Kde)
        return frameWidth();
    if (panelFrame() != PanelFrame::Plain)
        return 0;
    return Theme::active().classic ? 3 : std::max(frameWidth(), borderWidth());
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
        // Paint.NET: el contenido casi pegado a los bordes y justo debajo de la cabecera.
        const bool paintNet = paintNetChrome();
        const int side = paintNet ? 2 : grip() - frameWidth();
        layout()->setContentsMargins(side, titleHeight + (paintNet ? 0 : 2), side, side);
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

QRect FloatingPanel::area() const
{
    // Como en paint.js, con las reglas visibles las ventanas se colocan junto a ellas, sin taparlas.
    if (const auto* workspace = qobject_cast<const Workspace*>(parentWidget()))
        return workspace->panelArea();
    return parentWidget() ? parentWidget()->rect() : QRect();
}

void FloatingPanel::reposition()
{
    if (!parentWidget())
        return;
    const QRect a = area();
    QPoint pos;
    switch (m_corner) {
    case TopLeft: pos = m_offset; break;
    case TopRight: pos = QPoint(a.width() - width() - m_offset.x(), m_offset.y()); break;
    case BottomLeft: pos = QPoint(m_offset.x(), a.height() - height() - m_offset.y()); break;
    case BottomRight: pos = QPoint(a.width() - width() - m_offset.x(), a.height() - height() - m_offset.y()); break;
    }
    pos.setX(std::clamp(pos.x(), 0, std::max(0, a.width() - width())));
    pos.setY(std::clamp(pos.y(), 0, std::max(0, a.height() - height())));
    move(pos + a.topLeft());
}

void FloatingPanel::updateAnchorFromGeometry()
{
    if (!parentWidget())
        return;
    const QRect a = area();
    const QRect g = geometry().translated(-a.topLeft());
    const bool right = g.center().x() > a.width() / 2;
    const bool bottom = g.center().y() > a.height() / 2;
    m_corner = bottom ? (right ? BottomRight : BottomLeft) : (right ? TopRight : TopLeft);
    m_offset = QPoint(right ? a.width() - g.right() - 1 : g.left(), bottom ? a.height() - g.bottom() - 1 : g.top());
}

Qt::Edges FloatingPanel::edgesAt(const QPoint& pos) const
{
    if (!m_resizable)
        return {};
    const int b = grip();
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
    const QRect area = parentWidget() ? this->area() : QRect(QPoint(0, 0), QSize(100000, 100000));
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
            // Mayús suelta la ventana donde esté, sin acoplarla.
            if (g_snapping && !(me->modifiers() & Qt::ShiftModifier))
                p = snapped(p);
            const QRect a = area();
            p.setX(std::clamp(p.x(), a.left(), std::max(a.left(), a.right() + 1 - width())));
            p.setY(std::clamp(p.y(), a.top(), std::max(a.top(), a.bottom() + 1 - height())));
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
    if (auto* workspace = qobject_cast<Workspace*>(parentWidget()))
        workspace->updatePanelsOverImage();
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
    m_translucent = on;
    if (m_fade)
        m_fade->stop();
    if (on) {
        if (!m_opacity) {
            m_opacity = new QGraphicsOpacityEffect(this);
            setGraphicsEffect(m_opacity);
        }
        m_opacity->setOpacity(restingOpacity());
    } else if (m_opacity) {
        setGraphicsEffect(nullptr);
        m_opacity = nullptr;
    }
}

qreal FloatingPanel::restingOpacity() const
{
    const bool translucent = m_translucent && !underMouse() && (!g_overImageOnly || m_overImage);
    return translucent ? Theme::active().panelOpacity / 100.0 : 1.0;
}

void FloatingPanel::setTranslucentOverImageOnly(bool on)
{
    g_overImageOnly = on;
}

bool FloatingPanel::translucentOverImageOnly()
{
    return g_overImageOnly;
}

void FloatingPanel::setOverImage(bool on)
{
    if (on == m_overImage)
        return;
    m_overImage = on;
    // Mientras se arrastra o con el puntero encima sigue opaca; si no, cambia con un fundido.
    if (!m_translucent || !m_opacity || m_dragging || m_resizeEdges || underMouse())
        return;
    if (Animations::enabled() && isVisible())
        fadeOpacity(m_opacity->opacity(), restingOpacity());
    else
        m_opacity->setOpacity(restingOpacity());
}

void FloatingPanel::moveEvent(QMoveEvent* e)
{
    QFrame::moveEvent(e);
    if (auto* workspace = qobject_cast<Workspace*>(parentWidget()))
        workspace->updatePanelsOverImage();
}

void FloatingPanel::showEvent(QShowEvent* e)
{
    QFrame::showEvent(e);
    if (auto* workspace = qobject_cast<Workspace*>(parentWidget()))
        workspace->updatePanelsOverImage();
}

void FloatingPanel::fadeOpacity(qreal from, qreal to)
{
    // Como las ventanas de paint.js: la opacidad cambia en 180 ms. Sin translucidez, el efecto
    // solo existe mientras dura el fundido.
    if (m_fade)
        m_fade->stop();
    if (!m_opacity) {
        m_opacity = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(m_opacity);
    }
    m_fade = Animations::run(this, from, to, Animations::kPanelFade, QEasingCurve::OutCubic,
                             [this](qreal v) {
                                 if (m_opacity)
                                     m_opacity->setOpacity(v);
                             },
                             [this] {
                                 if (!m_translucent && m_opacity) {
                                     setGraphicsEffect(nullptr);
                                     m_opacity = nullptr;
                                 }
                             });
}

void FloatingPanel::showAnimated()
{
    show();
    raise();
    reposition();
    if (Animations::enabled())
        fadeOpacity(0.0, restingOpacity());
}

void FloatingPanel::enterEvent(QEnterEvent* e)
{
    if (m_translucent && m_opacity)
        fadeOpacity(m_opacity->opacity(), 1.0);
    QFrame::enterEvent(e);
}

void FloatingPanel::leaveEvent(QEvent* e)
{
    if (m_translucent && m_opacity)
        fadeOpacity(m_opacity->opacity(), restingOpacity());
    QFrame::leaveEvent(e);
}

// ---------------------------------------------------------------------------

Workspace::Workspace(Canvas* canvas, QWidget* parent)
    : QWidget(parent)
    , m_canvas(canvas)
{
    m_canvas->setParent(this);
    connect(m_canvas, &Canvas::rulersVisibilityChanged, this, [this] {
        for (FloatingPanel* p : std::as_const(m_panels))
            p->reposition();
    });
    connect(m_canvas, &Canvas::viewChanged, this, &Workspace::updatePanelsOverImage);
}

void Workspace::updatePanelsOverImage()
{
    // La imagen tal como se ve (en coordenadas del área de trabajo), recortada a la vista.
    QRectF image;
    if (const Document* doc = m_canvas->document()) {
        const QWidget* vp = m_canvas->viewport();
        const QPoint offset = vp->mapTo(this, QPoint(0, 0));
        image = m_canvas->imageToView(QRectF(doc->rect())).translated(offset).intersected(QRectF(QRect(offset, vp->size())));
    }
    for (FloatingPanel* p : std::as_const(m_panels))
        p->setOverImage(!image.isEmpty() && image.intersects(QRectF(p->geometry())));
}

QRect Workspace::panelArea() const
{
    QRect r = rect();
    if (m_canvas->rulersVisible())
        r.adjust(Ruler::kThickness, Ruler::kThickness, 0, 0);
    return r;
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
    updatePanelsOverImage();
    for (FloatingPanel* p : m_panels)
        p->setTranslucent(on);
}

void Workspace::setTranslucentOverImageOnly(bool on)
{
    FloatingPanel::setTranslucentOverImageOnly(on);
    updatePanelsOverImage();
}

void Workspace::setPanelChrome(FloatingPanel::Style style, int border)
{
    FloatingPanel::setChrome(style, border);
    for (FloatingPanel* p : std::as_const(m_panels)) {
        p->applyChrome();
        p->reposition();
    }
}

void Workspace::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    m_canvas->setGeometry(rect());
    for (FloatingPanel* p : m_panels)
        p->reposition();
}
