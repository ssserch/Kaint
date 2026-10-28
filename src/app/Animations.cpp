#include "app/Animations.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGraphicsEffect>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>
#include <QWindow>

#include <cmath>
#include <cstdint>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Animations {

namespace {

bool g_enabled = true;

// Fundido de una ventana emergente. Una ventana no tiene nada debajo con lo que mezclarse (en
// su lugar quedaría lo que hubiera en memoria): primero se pinta su color de fondo y encima el
// contenido con la opacidad del momento.
class FadeEffect : public QGraphicsEffect {
public:
    using QGraphicsEffect::QGraphicsEffect;

    void setOpacity(qreal opacity)
    {
        m_opacity = opacity;
        update();
    }

protected:
    void draw(QPainter* painter) override
    {
        QPoint offset;
        const QPixmap content = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::NoPad);
        if (!m_background.isValid())
            m_background = background(content.toImage());
        painter->save();
        painter->fillRect(QRectF(offset, QSizeF(content.size()) / content.devicePixelRatio()), m_background);
        painter->setOpacity(m_opacity);
        painter->drawPixmap(offset, content);
        painter->restore();
    }

private:
    // El color de fondo de verdad (el de la hoja de estilo, no el de la paleta): el que más se
    // repite en las esquinas de dentro del borde.
    static QColor background(const QImage& image)
    {
        if (image.width() < 8 || image.height() < 8)
            return image.isNull() ? QColor(Qt::transparent) : image.pixelColor(0, 0);
        const int m = int(std::ceil(3 * image.devicePixelRatio()));
        const QPoint corners[] = {{m, m}, {image.width() - 1 - m, m}, {m, image.height() - 1 - m},
                                  {image.width() - 1 - m, image.height() - 1 - m}};
        QHash<QRgb, int> votes;
        for (const QPoint& c : corners)
            ++votes[image.pixel(c)];
        QRgb best = image.pixel(corners[0]);
        for (auto it = votes.constBegin(); it != votes.constEnd(); ++it)
            if (it.value() > votes.value(best))
                best = it.key();
        return QColor::fromRgba(best);
    }

    qreal m_opacity = 1.0;
    QColor m_background;
};

// Fundido de los menús, las listas desplegables y las ayudas emergentes.
class PopupFader : public QObject {
public:
    using QObject::QObject;

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && g_enabled && watched->isWidgetType()) {
            auto* w = static_cast<QWidget*>(watched);
            const Qt::WindowType type = w->windowType();
            if (w->isWindow() && (type == Qt::Popup || type == Qt::ToolTip))
                fadeIn(w, kPopupFade);
        }
        return false;
    }
};

// Como el diálogo modal de paint.js: si se pulsa una ventana bloqueada, la modal parpadea
// cinco veces (160 ms cada una) para llamar la atención. Las pulsaciones bloqueadas no llegan
// a Qt, así que se miran los eventos nativos (X11 y Windows; Wayland no los da).
class ModalAttention : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override
    {
        WId target = 0;
#ifdef Q_OS_WIN
        if (type == "windows_generic_MSG") {
            const MSG* msg = static_cast<const MSG*>(message);
            switch (msg->message) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_NCLBUTTONDOWN:
            case WM_NCRBUTTONDOWN:
                target = WId(GetAncestor(msg->hwnd, GA_ROOT));
                break;
            default:
                break;
            }
        }
#else
        if (type == "xcb_generic_event_t") {
            // ButtonPress del protocolo base o de XInput 2 (sin depender de las cabeceras de xcb).
            const auto* bytes = static_cast<const std::uint8_t*>(message);
            const int response = bytes[0] & 0x7f;
            auto word = [bytes](int offset) {
                std::uint32_t v;
                std::memcpy(&v, bytes + offset, sizeof v);
                return v;
            };
            if (response == 4) {
                target = WId(word(12));
            } else if (response == 35) {
                std::uint16_t event;
                std::memcpy(&event, bytes + 8, sizeof event);
                if (event == 4)
                    target = WId(word(24));
            }
        }
#endif
        if (target)
            attention(target);
        return false;
    }

private:
    void attention(WId target)
    {
        QWidget* modal = QApplication::activeModalWidget();
        if (!g_enabled || !modal || !modal->windowHandle())
            return;
        QWindow* clicked = nullptr;
        for (QWindow* w : QGuiApplication::topLevelWindows())
            if (w->handle() && w->winId() == target)
                clicked = w;
        if (!clicked)
            return;
        // Las ventanas de la propia modal (sus menús y listas) no cuentan.
        for (QWindow* w = clicked; w; w = w->transientParent())
            if (w == modal->windowHandle())
                return;
        if (m_last.isValid() && m_last.elapsed() < 250)
            return;
        m_last.start();
        flash(modal);
    }

    static void flash(QWidget* modal)
    {
        QPointer<QWidget> guard(modal);
        run(modal, 0, 1, 5 * 160, QEasingCurve::Linear,
            [guard](qreal t) {
                if (guard)
                    guard->setWindowOpacity(1.0 - 0.35 * std::abs(std::sin(t * 5 * M_PI)));
            },
            [guard] {
                if (guard)
                    guard->setWindowOpacity(1.0);
            });
    }

    QElapsedTimer m_last;
};

} // namespace

void setEnabled(bool on)
{
    g_enabled = on;
    // El logo del diálogo Acerca de y otras animaciones de Qt.
    QApplication::setEffectEnabled(Qt::UI_General, on);
}

bool enabled()
{
    return g_enabled;
}

QEasingCurve listMoveCurve()
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.2, 0.8), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QVariantAnimation* run(QObject* owner, qreal from, qreal to, int ms, const QEasingCurve& curve,
                       const std::function<void(qreal)>& step, const std::function<void()>& done)
{
    if (!g_enabled || ms <= 0 || !owner) {
        step(to);
        if (done)
            done();
        return nullptr;
    }
    auto* animation = new QVariantAnimation(owner);
    animation->setDuration(ms);
    animation->setStartValue(from);
    animation->setEndValue(to);
    animation->setEasingCurve(curve);
    QObject::connect(animation, &QVariantAnimation::valueChanged, owner, [step](const QVariant& v) { step(v.toReal()); });
    if (done)
        QObject::connect(animation, &QVariantAnimation::finished, owner, [done] { done(); });
    step(from);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    return animation;
}

void fadeIn(QWidget* widget, int ms)
{
    // Las ventanas con fondo transparente (los menús de Breeze) y las que ya llevan otro efecto
    // se quedan como están.
    if (!g_enabled || !widget || widget->testAttribute(Qt::WA_TranslucentBackground))
        return;
    QGraphicsEffect* current = widget->graphicsEffect();
    if (current && !current->property("kaintFade").toBool())
        return;
    auto* effect = static_cast<FadeEffect*>(current);
    if (!effect) {
        effect = new FadeEffect(widget);
        effect->setProperty("kaintFade", true);
        widget->setGraphicsEffect(effect);
    }
    // Si vuelve a mostrarse mientras aparecía, empieza de nuevo.
    if (auto* old = widget->findChild<QVariantAnimation*>(QStringLiteral("kaintFade"), Qt::FindDirectChildrenOnly))
        old->stop();
    const QPointer<FadeEffect> fade(effect);
    const QPointer<QWidget> guard(widget);
    QVariantAnimation* animation = run(widget, 0, 1, ms, QEasingCurve::OutCubic,
        [fade](qreal v) {
            if (fade)
                fade->setOpacity(v);
        },
        [fade, guard] {
            // Sin el efecto al terminar: dibujar con él cuesta más.
            if (fade && guard && guard->graphicsEffect() == fade)
                guard->setGraphicsEffect(nullptr);
        });
    if (animation)
        animation->setObjectName(QStringLiteral("kaintFade"));
}

void install()
{
    static bool installed = false;
    if (installed || !qApp)
        return;
    installed = true;
    qApp->installEventFilter(new PopupFader(qApp));
    static ModalAttention attention;
    qApp->installNativeEventFilter(&attention);
}

} // namespace Animations
