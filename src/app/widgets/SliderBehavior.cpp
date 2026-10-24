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
        m_pressPos = me->position().toPoint();
        m_onGroove = !handle.contains(m_pressPos);
        m_following = false;
        setPressed(slider, true);
        return false;
    }
    case QEvent::MouseMove: {
        const auto* me = static_cast<QMouseEvent*>(event);
        if (!m_slider || watched != m_slider || !(me->buttons() & Qt::LeftButton))
            return false;
        if (m_onGroove && !m_following) {
            if ((me->position().toPoint() - m_pressPos).manhattanLength() < 2)
                return false;
            // El clic en la barra avanza a saltos hasta soltar: Qt lo deja al recibir la
            // liberación, y desde aquí el tirador sigue al puntero.
            m_following = true;
            QMouseEvent release(QEvent::MouseButtonRelease, me->position(), me->globalPosition(), Qt::LeftButton,
                                Qt::NoButton, me->modifiers());
            m_synthetic = true;
            QCoreApplication::sendEvent(m_slider, &release);
            m_synthetic = false;
            if (!m_slider)
                return true;
            m_slider->setSliderDown(true);
        }
        if (!m_following)
            return false;
        m_slider->setSliderPosition(valueAt(m_slider, me->position().toPoint()));
        return true;
    }
    case QEvent::MouseButtonRelease: {
        const auto* me = static_cast<QMouseEvent*>(event);
        // La liberación que se envía al empezar a seguir al puntero pasa sin más.
        if (m_synthetic || !m_slider || watched != m_slider || me->button() != Qt::LeftButton)
            return false;
        QSlider* slider = m_slider;
        const bool following = m_following;
        m_slider = nullptr;
        m_following = false;
        setPressed(slider, false);
        if (following) {
            slider->setSliderDown(false);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}
