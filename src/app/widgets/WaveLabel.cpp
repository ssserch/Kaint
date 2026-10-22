#include "app/widgets/WaveLabel.h"

#include <QApplication>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <numbers>

namespace {
constexpr qreal kSpeed = 0.7;
constexpr qreal kStep = 0.55;
}

WaveLabel::WaveLabel(const QString& text, const QString& link, const QUrl& url, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
    , m_link(link)
    , m_url(url)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_phase = m_clock.elapsed() / 1000.0 * kSpeed * 2 * std::numbers::pi;
        update();
    });
}

qreal WaveLabel::amplitude() const
{
    return fontMetrics().height() * 0.12;
}

QSize WaveLabel::sizeHint() const
{
    const QFontMetricsF fm(font());
    return QSize(int(std::ceil(fm.horizontalAdvance(m_text + m_link))) + 2,
                 int(std::ceil(fm.height() + 2 * amplitude())) + 2);
}

QRectF WaveLabel::linkRect() const
{
    const QFontMetricsF fm(font());
    const qreal start = fm.horizontalAdvance(m_text);
    return QRectF(start, 0, fm.horizontalAdvance(m_link), height());
}

void WaveLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QFontMetricsF fm(font());
    const QString all = m_text + m_link;
    const qreal baseline = (height() - fm.height()) / 2 + fm.ascent();
    QFont linkFont = font();
    linkFont.setUnderline(m_hover);
    qreal x = 0;
    for (int i = 0; i < all.size(); ++i) {
        const bool inLink = i >= m_text.size();
        const qreal y = baseline + amplitude() * std::sin(m_phase - i * kStep);
        p.setFont(inLink ? linkFont : font());
        p.setPen(palette().color(inLink ? QPalette::Link : QPalette::WindowText));
        p.drawText(QPointF(x, y), QString(all[i]));
        x += fm.horizontalAdvance(all[i]);
    }
}

void WaveLabel::setHover(bool hover)
{
    if (hover == m_hover)
        return;
    m_hover = hover;
    setCursor(hover ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(hover ? m_url.toString() : QString());
    update();
}

void WaveLabel::mouseMoveEvent(QMouseEvent* e)
{
    setHover(linkRect().contains(e->position()));
}

void WaveLabel::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && linkRect().contains(e->position()))
        QDesktopServices::openUrl(m_url);
}

void WaveLabel::leaveEvent(QEvent* e)
{
    setHover(false);
    QWidget::leaveEvent(e);
}

void WaveLabel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    if (QApplication::isEffectEnabled(Qt::UI_General)) {
        m_clock.start();
        m_timer.start();
    }
}

void WaveLabel::hideEvent(QHideEvent* e)
{
    m_timer.stop();
    QWidget::hideEvent(e);
}
