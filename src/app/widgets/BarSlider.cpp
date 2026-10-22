#include "app/widgets/BarSlider.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QWheelEvent>

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
    const QRectF r = QRectF(rect()).adjusted(0.5, 2.5, -0.5, -2.5);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(pal.color(QPalette::Mid));
    p.setBrush(pal.color(QPalette::Base));
    p.drawRoundedRect(r, 3, 3);

    const qreal t = m_max > m_min ? qreal(m_value - m_min) / (m_max - m_min) : 0;
    QRectF filled = r.adjusted(1, 1, -1, -1);
    filled.setWidth(filled.width() * t);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(hasFocus() ? QPalette::Highlight : QPalette::Highlight).lighter(hasFocus() ? 100 : 115));
    p.drawRoundedRect(filled, 2, 2);

    const QString text = QString::number(m_value) + m_suffix;
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
    const int steps = e->angleDelta().y() / 120;
    if (steps != 0)
        setValue(m_value + steps * std::max(1, (m_max - m_min) / 100));
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
