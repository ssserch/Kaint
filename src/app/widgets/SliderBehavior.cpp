#include "app/widgets/SliderBehavior.h"

#include <QApplication>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>

namespace {

// Lo mismo que QSlider::initStyleOption (que no es público).
QStyleOptionSlider sliderOption(const QSlider* slider)
{
    QStyleOptionSlider opt;
    opt.initFrom(slider);
    opt.subControls = QStyle::SC_None;
    opt.activeSubControls = QStyle::SC_None;
    opt.orientation = slider->orientation();
    opt.minimum = slider->minimum();
    opt.maximum = slider->maximum();
    opt.tickPosition = slider->tickPosition();
    opt.tickInterval = slider->tickInterval();
    opt.upsideDown = slider->orientation() == Qt::Horizontal
                         ? slider->invertedAppearance() != (opt.direction == Qt::RightToLeft)
                         : !slider->invertedAppearance();
    opt.direction = Qt::LeftToRight;
    opt.sliderPosition = slider->sliderPosition();
    opt.sliderValue = slider->value();
    opt.singleStep = slider->singleStep();
    opt.pageStep = slider->pageStep();
    if (slider->orientation() == Qt::Horizontal)
        opt.state |= QStyle::State_Horizontal;
    return opt;
}

} // namespace

void SliderBehavior::install()
{
    static SliderBehavior* instance = nullptr;
    if (instance || !qApp)
        return;
    instance = new SliderBehavior(qApp);
    qApp->installEventFilter(instance);
}

SliderBehavior::SliderBehavior(QObject* parent)
    : QObject(parent)
{
    // Manteniendo pulsado en la barra: un paso cada poco hasta llegar al puntero.
    m_repeat.setSingleShot(true);
    connect(&m_repeat, &QTimer::timeout, this, [this] {
        if (m_slider && !stepToward(m_slider, m_lastPos))
            m_repeat.start(60);
    });
}

void SliderBehavior::setPressed(QSlider* slider, bool pressed)
{
    slider->setProperty("kaintPressed", pressed);
    slider->style()->unpolish(slider);
    slider->style()->polish(slider);
    slider->update();
}

int SliderBehavior::valueAt(QSlider* slider, QPoint pos)
{
    // Con el centro del tirador bajo el puntero.
    const QStyleOptionSlider opt = sliderOption(slider);
    const QRect groove = slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, slider);
    const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, slider);
    const bool horizontal = slider->orientation() == Qt::Horizontal;
    const int length = horizontal ? handle.width() : handle.height();
    const int start = horizontal ? groove.x() : groove.y();
    const int end = (horizontal ? groove.right() : groove.bottom()) - length + 1;
    const int at = (horizontal ? pos.x() : pos.y()) - length / 2;
    return QStyle::sliderValueFromPosition(slider->minimum(), slider->maximum(), at - start, std::max(1, end - start),
                                           opt.upsideDown);
}

int SliderBehavior::stepFor(const QSlider* slider)
{
    // Un paso es una décima parte del recorrido (o el paso de página, si es mayor): de un
    // extremo al otro son unos diez clics.
    return std::max({1, slider->pageStep(), (slider->maximum() - slider->minimum() + 5) / 10});
}

bool SliderBehavior::stepToward(QSlider* slider, QPoint pos)
{
    // Un paso hacia el puntero; si el puntero queda a menos de un paso, el tirador va justo ahí.
    const int target = valueAt(slider, pos);
    const int step = stepFor(slider);
    const int value = slider->value();
    if (std::abs(target - value) <= step) {
        slider->setValue(target);
        return true;
    }
    slider->setValue(value + (target > value ? step : -step));
    return false;
}

bool SliderBehavior::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto* slider = qobject_cast<QSlider*>(watched);
        const auto* me = static_cast<QMouseEvent*>(event);
        if (!slider || !slider->isEnabled() || me->button() != Qt::LeftButton)
            return false;
        const QStyleOptionSlider opt = sliderOption(slider);
        const QRect handle = slider->style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, slider);
        m_slider = slider;
        m_pressPos = m_lastPos = me->position().toPoint();
        m_following = false;
        setPressed(slider, true);
        // Sobre el tirador, Qt lo arrastra como siempre.
        m_onGroove = !handle.contains(m_pressPos);
        if (!m_onGroove)
            return false;
        // En la barra: cerca del tirador va directo al puntero; lejos avanza un paso hacia él y,
        // si se mantiene pulsado, sigue a pasos hasta llegar.
        slider->setFocus(Qt::MouseFocusReason);
        if (!stepToward(slider, m_pressPos))
            m_repeat.start(500);
        return true;
    }
    case QEvent::MouseMove: {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (!m_slider || watched != m_slider || !m_onGroove || !(me->buttons() & Qt::LeftButton))
            return false;
        m_lastPos = me->position().toPoint();
        // Al mover el ratón con el botón pulsado, el tirador pasa a seguir al puntero.
        if (!m_following) {
            if ((m_lastPos - m_pressPos).manhattanLength() < 3)
                return true;
            m_repeat.stop();
            m_following = true;
            m_slider->setSliderDown(true);
        }
        m_slider->setSliderPosition(valueAt(m_slider, m_lastPos));
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (!m_slider || watched != m_slider || me->button() != Qt::LeftButton)
            return false;
        QSlider* slider = m_slider;
        m_slider = nullptr;
        m_repeat.stop();
        setPressed(slider, false);
        if (!m_onGroove)
            return false;
        if (m_following)
            slider->setSliderDown(false);
        m_following = false;
        return true;
    }
    default:
        return false;
    }
}
