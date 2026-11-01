#include "app/WindowFrame.h"

#include "app/Icons.h"
#include "app/Theme.h"

#include <QApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QWindow>

#ifdef KAINT_HAVE_KWINDOWSHADOW
#include <KWindowShadow>
#endif

namespace WindowFrame {

namespace {

bool g_enabled = false;
// Alto de la barra de título de los diálogos (Windows 10 y 98) y ancho de sus botones.
constexpr int kTitle = 31;
constexpr int kTitle98 = 18;
constexpr int kButton = 46;
// Franja de los bordes (dentro de la ventana) desde la que se cambia el tamaño.
constexpr int kGrab = 5;
// Como Windows 10: bajo el puntero, el botón se aclara (u oscurece) un 10 %, un 20 % al pulsarlo;
// el de cerrar, rojo.
const QColor kCloseHover(0xe8, 0x11, 0x23);
const QColor kClosePressed(0xf1, 0x70, 0x7a);

bool classic()
{
    return Theme::active().classic;
}

QColor overlay(int alpha)
{
    const QColor title = titleColor();
    return title.lightness() < 128 ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha);
}

QColor borderColor()
{
    const QColor title = Theme::titleBarColor();
    if (title.isValid())
        return title;
    return Theme::isDark() ? QColor(0x66, 0x66, 0x6a) : QColor(0xa0, 0xa0, 0xa0);
}

// Grosor del borde: el fino de Windows 10 o el de cuatro píxeles en relieve de Windows 98.
int borderWidth()
{
    return classic() ? 4 : 1;
}

// --- Windows 98 ------------------------------------------------------------------------------

// Relieve de dos líneas: 'raised' hacia fuera (botones, marco) o hundido (botón pulsado).
void bevel98(QPainter& p, QRect r, bool raised)
{
    const QColor face = Theme::active().panel;
    const QColor white(Qt::white), light = face.lighter(116), shadow = face.darker(150), dark(Qt::black);
    auto lines = [&p](const QRect& box, const QColor& topLeft, const QColor& bottomRight) {
        p.setPen(topLeft);
        p.drawLine(box.topLeft(), box.topRight() - QPoint(1, 0));
        p.drawLine(box.topLeft(), box.bottomLeft() - QPoint(0, 1));
        p.setPen(bottomRight);
        p.drawLine(box.bottomLeft(), box.bottomRight());
        p.drawLine(box.topRight(), box.bottomRight());
    };
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    if (raised) {
        lines(r, light, dark);
        lines(r.adjusted(1, 1, -1, -1), white, shadow);
    } else {
        lines(r, dark, white);
        lines(r.adjusted(1, 1, -1, -1), shadow, light);
    }
    p.restore();
}

// La barra de título de Windows 98: degradado azul marino (gris si no está activa).
void paintTitle98(QPainter& p, const QRect& r, bool active)
{
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0, active ? QColor(0x00, 0x00, 0x80) : QColor(0x80, 0x80, 0x80));
    g.setColorAt(1, active ? QColor(0x10, 0x84, 0xd0) : QColor(0xb5, 0xb5, 0xb5));
    p.fillRect(r, g);
}

// Botón de la barra de Windows 98 (16 × 14, gris en relieve) con su dibujo en negro: 0 minimizar,
// 1 maximizar (o restaurar), 2 cerrar.
void paintButton98(QPainter& p, const QRect& r, int which, bool maximized, bool down)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(r, Theme::active().panel);
    bevel98(p, r, !down);
    // Los dibujos de Windows 98, píxel a píxel (al pulsar, un píxel más abajo a la derecha).
    const QPoint o = r.topLeft() + (down ? QPoint(1, 1) : QPoint());
    const QColor ink(Qt::black);
    auto px = [&](int x, int y, int w = 1, int h = 1) { p.fillRect(QRect(o + QPoint(x, y), QSize(w, h)), ink); };
    // Una ventana con el borde de arriba doble.
    auto box = [&](int x, int y, int w, int h) {
        px(x, y, w, 2);
        px(x, y + h - 1, w, 1);
        px(x, y, 1, h);
        px(x + w - 1, y, 1, h);
    };
    switch (which) {
    case 0:
        px(4, 9, 6, 2);
        break;
    case 1:
        if (maximized) {
            box(6, 2, 6, 6);
            p.fillRect(QRect(o + QPoint(3, 5), QSize(6, 6)), Theme::active().panel);
            box(3, 5, 6, 6);
        } else {
            box(3, 2, 9, 9);
        }
        break;
    default:
        for (int i = 0; i < 7; ++i) {
            px(4 + i, 3 + i, 2, 1);
            px(10 - i, 3 + i, 2, 1);
        }
        break;
    }
    p.restore();
}

// --- Windows 10 ------------------------------------------------------------------------------

// Las aspas de cerrar, el cuadrado de maximizar (dos al restaurar) y la raya de minimizar, de
// 10 px como los de Windows 10.
void paintGlyph(QPainter& p, const QRect& box, int which, bool maximized, const QColor& color)
{
    p.save();
    const QPointF c = QRectF(box).center();
    p.setPen(QPen(color, 1));
    p.setBrush(Qt::NoBrush);
    switch (which) {
    case 0:
        p.setRenderHint(QPainter::Antialiasing, false);
        p.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
        break;
    case 1:
        p.setRenderHint(QPainter::Antialiasing, false);
        if (maximized) {
            p.drawRect(QRectF(c.x() - 5, c.y() - 3, 8, 8));
            p.drawPolyline(QPolygonF{QPointF(c.x() - 3, c.y() - 3), QPointF(c.x() - 3, c.y() - 5), QPointF(c.x() + 5, c.y() - 5),
                                     QPointF(c.x() + 5, c.y() + 3), QPointF(c.x() + 3, c.y() + 3)});
        } else {
            p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
        }
        break;
    default:
        p.setRenderHint(QPainter::Antialiasing);
        p.drawLine(QPointF(c.x() - 5, c.y() - 5), QPointF(c.x() + 5, c.y() + 5));
        p.drawLine(QPointF(c.x() - 5, c.y() + 5), QPointF(c.x() + 5, c.y() - 5));
        break;
    }
    p.restore();
}

#ifdef KAINT_HAVE_KWINDOWSHADOW
// La sombra la dibuja el gestor de ventanas (KWin), como la de las ventanas con decoración: ocho
// piezas de una imagen con un difuminado alrededor de un punto. Igual por los cuatro lados: con
// la sombra desplazada, KWin dejaba una franja vacía bajo la ventana.
KWindowShadow* makeShadow(QObject* parent)
{
    constexpr int kSize = 18;
    const int side = 2 * kSize + 1;
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
            const double dx = std::max(0, std::abs(x - kSize) - 1), dy = std::max(0, std::abs(y - kSize) - 1);
            const double t = std::clamp(1 - std::sqrt(dx * dx + dy * dy) / (kSize - 1), 0.0, 1.0);
            img.setPixel(x, y, qPremultiply(qRgba(0, 0, 0, int(110 * t * t))));
        }
    auto tile = [&img](int x, int y, int w, int h) {
        auto t = KWindowShadowTile::Ptr::create();
        t->setImage(img.copy(x, y, w, h));
        return t;
    };
    auto* shadow = new KWindowShadow(parent);
    shadow->setTopLeftTile(tile(0, 0, kSize, kSize));
    shadow->setTopTile(tile(kSize, 0, 1, kSize));
    shadow->setTopRightTile(tile(kSize + 1, 0, kSize, kSize));
    shadow->setLeftTile(tile(0, kSize, kSize, 1));
    shadow->setRightTile(tile(kSize + 1, kSize, kSize, 1));
    shadow->setBottomLeftTile(tile(0, kSize + 1, kSize, kSize));
    shadow->setBottomTile(tile(kSize, kSize + 1, 1, kSize));
    shadow->setBottomRightTile(tile(kSize + 1, kSize + 1, kSize, kSize));
    shadow->setPadding(QMargins(kSize, kSize, kSize, kSize));
    return shadow;
}
#endif

// El marco de una ventana: pinta el borde y (en los diálogos) la barra de título, y se ocupa de
// moverla, de cambiarle el tamaño desde los bordes y del botón de cerrar. La ventana es opaca: la
// sombra, si el escritorio la ofrece (KWin), la pone él.
class Frame : public QObject {
public:
    Frame(QWidget* window, bool titleBar)
        : QObject(window)
        , m_window(window)
        , m_titleBar(titleBar)
    {
        setObjectName(QStringLiteral("kaintWindowFrame"));
        window->setMouseTracking(true);
        // Un filtro de toda la aplicación: recibe los eventos de la ventana y los de lo que tiene
        // dentro (los bordes también se agarran encima de las barras y los paneles que llegan
        // hasta ellos).
        qApp->installEventFilter(this);
        updateMargins();
        connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
            updateMargins();
            m_window->update();
        });
#ifdef KAINT_HAVE_KWINDOWSHADOW
        m_shadow = makeShadow(this);
#endif
    }

    ~Frame() override { restoreCursor(); }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (edgeFilter(watched, event))
            return true;
        if (watched != m_window)
            return false;
        switch (event->type()) {
        case QEvent::Paint:
            paint();
            break;
        case QEvent::Show:
            installShadow();
            break;
        case QEvent::Hide:
#ifdef KAINT_HAVE_KWINDOWSHADOW
            m_shadow->destroy();
#endif
            break;
        case QEvent::WindowStateChange:
            updateMargins();
            m_window->update();
            break;
        case QEvent::ActivationChange:
        case QEvent::WindowTitleChange:
        case QEvent::WindowIconChange:
            m_window->update(QRect(0, 0, m_window->width(), titleRect().bottom() + 2));
            break;
        case QEvent::MouseMove:
            setHover(closeRect().contains(static_cast<QMouseEvent*>(event)->position().toPoint()));
            break;
        case QEvent::Leave:
            setHover(false);
            restoreCursor();
            break;
        case QEvent::MouseButtonPress:
            return mousePress(static_cast<QMouseEvent*>(event));
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && m_pressed) {
                m_pressed = false;
                m_window->update(closeRect());
                if (closeRect().contains(me->position().toPoint()))
                    m_window->close();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && titleRect().contains(me->position().toPoint())
                && !closeRect().contains(me->position().toPoint()) && resizable()) {
                m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

private:
    void installShadow()
    {
#ifdef KAINT_HAVE_KWINDOWSHADOW
        if (!m_window->windowHandle())
            return;
        if (m_shadow->isCreated())
            m_shadow->destroy();
        m_shadow->setWindow(m_window->windowHandle());
        m_shadow->create();
#endif
    }

    QRect titleRect() const
    {
        if (!m_titleBar)
            return {};
        const int b = borderWidth();
        return QRect(b, b, m_window->width() - 2 * b, classic() ? kTitle98 : kTitle);
    }

    QRect closeRect() const
    {
        const QRect t = titleRect();
        if (t.isEmpty())
            return {};
        if (classic())
            return QRect(t.right() - 1 - 16, t.top() + 2, 16, 14);
        return QRect(t.right() + 1 - kButton, t.top(), kButton, t.height());
    }

    bool resizable() const
    {
        return m_window->minimumSize() != m_window->maximumSize() && !m_window->isFullScreen();
    }

    void updateMargins()
    {
        const int b = borderWidth();
        m_window->setContentsMargins(b, b + (m_titleBar ? (classic() ? kTitle98 + 1 : kTitle) : 0), b, b);
    }

    // Los bordes desde los que se cambia el tamaño: una franja de unos píxeles por dentro y las
    // esquinas algo más largas. 'pos', en coordenadas de la ventana.
    Qt::Edges edgesAt(const QPoint& pos) const
    {
        if (!resizable() || m_window->isMaximized())
            return {};
        const QRect f = m_window->rect();
        constexpr int corner = 16;
        Qt::Edges e;
        if (pos.x() < f.left() + kGrab)
            e |= Qt::LeftEdge;
        else if (pos.x() > f.right() - kGrab)
            e |= Qt::RightEdge;
        if (pos.y() < f.top() + kGrab)
            e |= Qt::TopEdge;
        else if (pos.y() > f.bottom() - kGrab)
            e |= Qt::BottomEdge;
        if (e & (Qt::LeftEdge | Qt::RightEdge)) {
            if (pos.y() < f.top() + corner)
                e |= Qt::TopEdge;
            else if (pos.y() > f.bottom() - corner)
                e |= Qt::BottomEdge;
        }
        if (e & (Qt::TopEdge | Qt::BottomEdge)) {
            if (pos.x() < f.left() + corner)
                e |= Qt::LeftEdge;
            else if (pos.x() > f.right() - corner)
                e |= Qt::RightEdge;
        }
        return e;
    }

    static Qt::CursorShape cursorFor(Qt::Edges e)
    {
        if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge))
            return Qt::SizeFDiagCursor;
        if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge))
            return Qt::SizeBDiagCursor;
        return e & (Qt::LeftEdge | Qt::RightEdge) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    }

    void restoreCursor()
    {
        if (m_cursorSet) {
            QGuiApplication::restoreOverrideCursor();
            m_cursorSet = false;
        }
    }

    // Movimientos y clics de toda la ventana (también encima de sus barras): cerca del borde, el
    // cursor de cambiar el tamaño y, al pulsar, se cambia.
    bool edgeFilter(QObject* watched, QEvent* event)
    {
        const QEvent::Type type = event->type();
        if ((type != QEvent::MouseMove && type != QEvent::MouseButtonPress) || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (w->window() != m_window)
            return false;
        auto* me = static_cast<QMouseEvent*>(event);
        if (type == QEvent::MouseMove && me->buttons() != Qt::NoButton)
            return false;
        const QPoint pos = m_window->mapFromGlobal(me->globalPosition().toPoint());
        const Qt::Edges edges = edgesAt(pos);
        if (type == QEvent::MouseMove) {
            if (edges) {
                const Qt::CursorShape shape = cursorFor(edges);
                if (!m_cursorSet)
                    QGuiApplication::setOverrideCursor(shape);
                else if (QGuiApplication::overrideCursor()->shape() != shape)
                    QGuiApplication::changeOverrideCursor(shape);
                m_cursorSet = true;
            } else {
                restoreCursor();
            }
            return false;
        }
        if (edges && me->button() == Qt::LeftButton && m_window->windowHandle()) {
            restoreCursor();
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        return false;
    }

    void setHover(bool on)
    {
        if (on == m_hover)
            return;
        m_hover = on;
        m_window->update(closeRect());
    }

    bool mousePress(QMouseEvent* e)
    {
        if (e->button() != Qt::LeftButton)
            return false;
        const QPoint pos = e->position().toPoint();
        if (closeRect().contains(pos)) {
            m_pressed = true;
            m_window->update(closeRect());
            return true;
        }
        if (titleRect().contains(pos) && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemMove();
            return true;
        }
        return false;
    }

    void paint()
    {
        QPainter p(m_window);
        const bool active = m_window->isActiveWindow();
        const QRect f = m_window->rect();
        p.fillRect(f, m_window->palette().color(QPalette::Window));
        if (classic()) {
            bevel98(p, f, true);
        } else {
            p.setPen(borderColor());
            p.drawRect(f.adjusted(0, 0, -1, -1));
        }
        if (!m_titleBar)
            return;
        const QRect t = titleRect();
        const QRect close = closeRect();
        int x = t.left() + (classic() ? 2 : 8);
        if (classic())
            paintTitle98(p, t, active);
        else
            p.fillRect(t, titleColor());
        // El icono solo si el diálogo tiene el suyo (si no, Windows tampoco lo muestra).
        if (m_window->testAttribute(Qt::WA_SetWindowIcon)) {
            m_window->windowIcon().paint(&p, QRect(x, t.top() + (t.height() - 16) / 2, 16, 16));
            x += 16 + (classic() ? 3 : 7);
        }
        if (classic()) {
            paintButton98(p, close, 2, false, m_pressed && m_hover);
        } else {
            if (m_pressed || m_hover)
                p.fillRect(close, m_pressed ? kClosePressed : kCloseHover);
            paintGlyph(p, close, 2, false, m_pressed || m_hover ? QColor(Qt::white) : titleTextColor(active));
        }
        // Como en Paint.NET, el título largo se corta con «…».
        const QRect text(x, t.top(), close.left() - x - 6, t.height());
        QFont font = m_window->font();
        font.setBold(classic());
        p.setFont(font);
        p.setPen(titleTextColor(active));
        p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_window->windowTitle(), Qt::ElideRight, std::max(0, text.width())));
    }

    QWidget* m_window;
    bool m_titleBar;
    bool m_hover = false;
    bool m_pressed = false;
    bool m_cursorSet = false;
#ifdef KAINT_HAVE_KWINDOWSHADOW
    KWindowShadow* m_shadow = nullptr;
#endif
};

// Los diálogos (también los de Qt: avisos, preguntas, progreso...) se preparan cuando reciben su
// título, antes de crearse su ventana: después ya no se puede quitar la decoración del sistema.
class Installer : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::WindowTitleChange || !watched->isWidgetType())
            return false;
        auto* w = static_cast<QWidget*>(watched);
        if (w->isWindow() && w->windowType() == Qt::Dialog && !w->testAttribute(Qt::WA_WState_Created)
            && !(w->windowFlags() & Qt::FramelessWindowHint) && Theme::paintNetLook())
            decorate(w, true);
        return false;
    }
};

} // namespace

void setEnabled(bool on)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    on = false;
#endif
    if (on == g_enabled)
        return;
    g_enabled = on;
    static Installer* installer = nullptr;
    if (on && !installer && qApp) {
        installer = new Installer(qApp);
        qApp->installEventFilter(installer);
    } else if (!on && installer) {
        qApp->removeEventFilter(installer);
        delete installer;
        installer = nullptr;
    }
}

bool enabled()
{
    return g_enabled;
}

void decorate(QWidget* window, bool titleBar)
{
    if (!g_enabled || !window || isDecorated(window))
        return;
    window->setProperty("kaintWindowFrame", true);
    window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
    new Frame(window, titleBar);
}

bool isDecorated(const QWidget* window)
{
    return window && window->property("kaintWindowFrame").toBool();
}

QColor titleColor()
{
    // La de la franja de los menús (negra con los temas oscuros de Paint.NET), así la barra de
    // título y la franja se ven seguidas, como en paint.js; o el color propio de la barra superior.
    const QColor title = Theme::titleBarColor();
    return title.isValid() ? title : Theme::active().menuBar;
}

QColor titleTextColor(bool active)
{
    if (classic())
        return active ? QColor(Qt::white) : QColor(0xc0, 0xc0, 0xc0);
    QColor c = titleColor().lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    // Inactiva, el título y los botones se apagan.
    if (!active)
        c.setAlphaF(0.45);
    return c;
}

bool classicTitle()
{
    return classic();
}

void paintClassicTitle(QPainter& p, const QRect& r, bool active)
{
    paintTitle98(p, r, active);
}

} // namespace WindowFrame

// ---------------------------------------------------------------------------

CaptionTitle::CaptionTitle(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

QSize CaptionTitle::sizeHint() const
{
    return {fontMetrics().horizontalAdvance(window()->windowTitle()) + 40, CaptionButtons::rowHeight()};
}

QSize CaptionTitle::minimumSizeHint() const
{
    return {40, CaptionButtons::rowHeight()};
}

bool CaptionTitle::event(QEvent* e)
{
    // El título y el estado activo son los de la ventana.
    if (e->type() == QEvent::Polish && !m_watching) {
        window()->installEventFilter(this);
        m_watching = true;
    }
    return QWidget::event(e);
}

bool CaptionTitle::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == window() && (e->type() == QEvent::WindowTitleChange || e->type() == QEvent::ActivationChange))
        update();
    return false;
}

void CaptionTitle::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool active = window()->isActiveWindow();
    const bool classic = WindowFrame::classicTitle();
    const QRect r = rect();
    const int x0 = classic ? 3 : 8;
    Icons::app().paint(&p, QRect(x0, (r.height() - 16) / 2, 16, 16));
    const QRect text = r.adjusted(x0 + 16 + (classic ? 4 : 8), 0, -4, 0);
    QFont font = this->font();
    font.setBold(classic);
    p.setFont(font);
    p.setPen(WindowFrame::titleTextColor(active));
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(window()->windowTitle(), Qt::ElideRight, std::max(0, text.width())));
}

void CaptionTitle::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CaptionTitle::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return QWidget::mouseDoubleClickEvent(e);
    QWidget* w = window();
    w->isMaximized() ? w->showNormal() : w->showMaximized();
}

// ---------------------------------------------------------------------------

CaptionButtons::CaptionButtons(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(Theme::notifier(), &Theme::Notifier::changed, this, [this] {
        updateGeometry();
        update();
    });
}

int CaptionButtons::rowHeight()
{
    // Windows 98: la barra de 18 px con un poco de aire; Windows 10: 32 px.
    return WindowFrame::classicTitle() ? 20 : 32;
}

QSize CaptionButtons::sizeHint() const
{
    return WindowFrame::classicTitle() ? QSize(3 * 16 + 2 + 4, rowHeight()) : QSize(3 * kWidth, rowHeight());
}

bool CaptionButtons::event(QEvent* e)
{
    if (e->type() == QEvent::Polish && !m_watching) {
        window()->installEventFilter(this);
        m_watching = true;
    }
    return QWidget::event(e);
}

bool CaptionButtons::eventFilter(QObject* watched, QEvent* e)
{
    // Maximizar pasa a restaurar al cambiar el estado; inactiva, los iconos se apagan.
    if (watched == window() && (e->type() == QEvent::WindowStateChange || e->type() == QEvent::ActivationChange))
        update();
    return false;
}

QRect CaptionButtons::buttonRect(int i) const
{
    if (WindowFrame::classicTitle()) {
        // Minimizar y maximizar juntos; cerrar, dos píxeles más allá.
        const int top = (QWidget::height() - 14) / 2;
        return QRect(i * 16 + (i == 2 ? 2 : 0), top, 16, 14);
    }
    return QRect(i * kWidth, 0, kWidth, QWidget::height());
}

int CaptionButtons::buttonAt(const QPoint& pos) const
{
    for (int i = 0; i < 3; ++i)
        if (buttonRect(i).contains(pos))
            return i;
    return -1;
}

void CaptionButtons::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const bool active = window()->isActiveWindow();
    const bool maximized = window()->isMaximized();
    for (int i = 0; i < 3; ++i) {
        const QRect box = buttonRect(i);
        const bool hot = m_hover == i, down = m_pressed == i && hot;
        if (WindowFrame::classicTitle()) {
            WindowFrame::paintButton98(p, box, i, maximized, down);
            continue;
        }
        QColor glyph = WindowFrame::titleTextColor(active);
        if (i == 2 && (hot || down)) {
            p.fillRect(box, down ? WindowFrame::kClosePressed : WindowFrame::kCloseHover);
            glyph = Qt::white;
        } else if (hot || down) {
            p.fillRect(box, WindowFrame::overlay(down ? 51 : 26));
        }
        WindowFrame::paintGlyph(p, box, i, maximized, glyph);
    }
}

void CaptionButtons::mouseMoveEvent(QMouseEvent* e)
{
    const int b = buttonAt(e->position().toPoint());
    if (b != m_hover) {
        m_hover = b;
        update();
    }
}

void CaptionButtons::leaveEvent(QEvent*)
{
    m_hover = -1;
    update();
}

void CaptionButtons::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = buttonAt(e->position().toPoint());
    // Fuera de los botones (el hueco entre ellos), se arrastra la ventana.
    if (m_pressed < 0 && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        return;
    }
    update();
}

void CaptionButtons::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int b = m_pressed;
    m_pressed = -1;
    update();
    if (b < 0 || b != buttonAt(e->position().toPoint()))
        return;
    QWidget* w = window();
    if (b == 0)
        w->showMinimized();
    else if (b == 1)
        w->isMaximized() ? w->showNormal() : w->showMaximized();
    else
        w->close();
}
