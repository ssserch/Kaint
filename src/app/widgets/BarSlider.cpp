#include "app/widgets/BarSlider.h"

#include "app/Theme.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QWheelEvent>

#include <algorithm>

namespace {
bool g_wheelEnabled = true;
int g_wheelStep = 0;
} // namespace

void BarSlider::setWheelBehavior(bool enabled, int step)
{
    g_wheelEnabled = enabled;
    g_wheelStep = std::clamp(step, 0, 3);
}

bool BarSlider::wheelEnabled()
{
    return g_wheelEnabled;
}

int BarSlider::wheelStep(int min, int max, Qt::KeyboardModifiers modifiers)
{
    const int range = std::max(1, max - min);
    if (modifiers & Qt::ShiftModifier)
        return 1;
    int step = 1;
    switch (g_wheelStep) {
    case 1: step = 1; break;
    case 2: step = std::max(1, range * 5 / 100); break;
    case 3: step = std::max(1, range / 10); break;
    default: step = std::max(1, range / 100); break;
    }
    if (modifiers & Qt::ControlModifier)
        step *= 10;
    return step;
}

BarSlider::BarSlider(int min, int max, const QString& suffix, QWidget* parent)
    : QWidget(parent)
    , m_min(min)
    , m_max(max)
    , m_value(min)
    , m_suffix(suffix)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::SizeHorCursor);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void BarSlider::setValue(int value)
{
    value = std::clamp(value, m_min, m_max);
    if (value == m_value)
        return;
    m_value = value;
    update();
    emit valueChanged(m_value);
}

void BarSlider::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QPalette& pal = palette();
    const qreal t = m_max > m_min ? qreal(m_value - m_min) / (m_max - m_min) : 0;
    const QString text = QString::number(m_value) + m_suffix;
    if (Theme::paintNetLook()) {
        // Paint.NET 5: una barra lisa y recta, gris claro, con la parte llena en azul y el valor
        // encima (blanco sobre el azul, negro sobre el gris), igual en el tema claro y el oscuro.
        const QRect r = rect().adjusted(0, 1, 0, -2);
        p.fillRect(r, QColor(0xcf, 0xcf, 0xcf));
        QRect filled = r;
        filled.setWidth(int(std::lround(r.width() * t)));
        p.fillRect(filled, QColor(0x00, 0x78, 0xd7));
        if (hasFocus()) {
            p.setPen(QColor(0x00, 0x5a, 0xa3));
            p.drawRect(r.adjusted(0, 0, -1, -1));
        }
        const QRect textRect = r.adjusted(8, 0, -6, 0);
        p.setPen(Qt::black);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
        p.setClipRect(filled);
        p.setPen(Qt::white);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
        return;
    }
    const QRectF r = QRectF(rect()).adjusted(0.5, 2.5, -0.5, -2.5);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(pal.color(QPalette::Mid));
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(r, 3, 3);

    QRectF filled = r.adjusted(1, 1, -1, -1);
    filled.setWidth(filled.width() * t);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(hasFocus() ? QPalette::Highlight : QPalette::Highlight).lighter(hasFocus() ? 100 : 115));
    p.drawRoundedRect(filled, 2, 2);

    p.setPen(pal.color(QPalette::Text));
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.save();
    p.setClipRect(filled);
    p.setPen(pal.color(QPalette::HighlightedText));
    p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.restore();
}

void BarSlider::setFromX(qreal x)
{
    const qreal t = std::clamp((x - 1) / std::max(1.0, width() - 2.0), 0.0, 1.0);
    setValue(m_min + int(std::round(t * (m_max - m_min))));
}

void BarSlider::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        setFromX(e->position().x());
}

void BarSlider::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        setFromX(e->position().x());
}

void BarSlider::wheelEvent(QWheelEvent* e)
{
    if (!g_wheelEnabled) {
        // Para la barra o la ventana que la contiene.
        e->ignore();
        return;
    }
    // (Las ruedas de alta resolución y los touchpads mandan trozos de 120: se suman.)
    const QPoint angle = e->angleDelta();
    m_wheelRest += std::abs(angle.y()) >= std::abs(angle.x()) ? angle.y() : -angle.x();
    const int steps = m_wheelRest / 120;
    m_wheelRest %= 120;
    if (steps != 0)
        setValue(m_value + steps * wheelStep(m_min, m_max, e->modifiers()));
    e->accept();
}

void BarSlider::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down: setValue(m_value - 1); break;
    case Qt::Key_Right:
    case Qt::Key_Up: setValue(m_value + 1); break;
    case Qt::Key_PageDown: setValue(m_value - 10); break;
    case Qt::Key_PageUp: setValue(m_value + 10); break;
    case Qt::Key_Home: setValue(m_min); break;
    case Qt::Key_End: setValue(m_max); break;
    default: QWidget::keyPressEvent(e);
    }
}
