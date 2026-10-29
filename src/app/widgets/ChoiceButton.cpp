#include "app/widgets/ChoiceButton.h"

#include "app/Theme.h"

#include <QActionGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWidgetAction>

namespace {

// Fila de menú con icono, título y descripción.
class ChoiceRow : public QWidget {
public:
    ChoiceRow(QAction* action, const QIcon& icon, const QString& title, const QString& description, QWidget* parent)
        : QWidget(parent)
        , m_action(action)
        , m_icon(icon)
        , m_title(title)
        , m_description(description)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const int text = std::max(fm.horizontalAdvance(m_title), fm.horizontalAdvance(m_description));
        return QSize(kIcon + 3 * kPad + text + kPad, 2 * fm.height() + 2 * kPad);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const bool hot = underMouse();
        // Con los temas de Paint.NET, el resalte de sus menús (relleno claro con borde); con el
        // de KDE, el color de resalte del sistema.
        const bool kde = Theme::current() == Theme::Scheme::Kde;
        const Theme::Spec theme = Theme::active();
        if (hot && kde) {
            p.fillRect(rect(), palette().highlight());
        } else if (hot) {
            p.setPen(theme.selectionBorder);
            p.setBrush(theme.selection);
            p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        } else if (m_action->isChecked()) {
            QColor c = kde ? palette().color(QPalette::Highlight) : theme.selection;
            c.setAlpha(kde ? 60 : 140);
            p.fillRect(rect(), c);
        }
        const QFontMetrics fm(font());
        const int iconY = (height() - kIcon) / 2;
        m_icon.paint(&p, QRect(kPad, iconY, kIcon, kIcon));
        p.setPen(palette().color(hot && kde ? QPalette::HighlightedText : QPalette::Text));
        const int x = kIcon + 2 * kPad;
        p.drawText(QRect(x, kPad, width() - x - kPad, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        p.drawText(QRect(x, kPad + fm.height(), width() - x - kPad, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                   m_description);
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        m_action->trigger();
        for (QWidget* w = parentWidget(); w; w = w->parentWidget())
            if (auto* menu = qobject_cast<QMenu*>(w)) {
                menu->close();
                break;
            }
    }

    void mouseMoveEvent(QMouseEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

private:
    static constexpr int kIcon = 20;
    static constexpr int kPad = 4;
    QAction* m_action;
    QIcon m_icon;
    QString m_title;
    QString m_description;
};

} // namespace

ChoiceButton::ChoiceButton(const QString& toolTip, QWidget* parent)
    : QToolButton(parent)
    , m_menu(new QMenu(this))
    , m_group(new QActionGroup(this))
    , m_tip(toolTip)
{
    setPopupMode(QToolButton::InstantPopup);
    setAutoRaise(true);
    setMenu(m_menu);
    setToolTip(toolTip);
    m_group->setExclusive(true);
}

void ChoiceButton::addChoice(const QIcon& icon, const QString& text, int data, const QString& description)
{
    QAction* a;
    if (description.isEmpty()) {
        a = m_menu->addAction(icon, text);
    } else {
        auto* wa = new QWidgetAction(m_menu);
        wa->setIcon(icon);
        wa->setText(text);
        wa->setDefaultWidget(new ChoiceRow(wa, icon, text, description, m_menu));
        m_menu->addAction(wa);
        a = wa;
    }
    a->setData(data);
    a->setCheckable(true);
    m_group->addAction(a);
    connect(a, &QAction::triggered, this, [this, data] {
        setCurrent(data);
        if (onChosen)
            onChosen(data);
    });
    if (m_current < 0)
        setCurrent(data);
}

void ChoiceButton::addSection(const QString& title)
{
    m_menu->addSection(title);
}

void ChoiceButton::removeChoicesFrom(int data)
{
    for (QAction* a : m_menu->actions())
        if (a->isCheckable() && a->data().toInt() >= data) {
            m_group->removeAction(a);
            m_menu->removeAction(a);
            a->deleteLater();
        }
    while (!m_menu->actions().isEmpty() && m_menu->actions().constLast()->isSeparator()) {
        QAction* section = m_menu->actions().constLast();
        m_menu->removeAction(section);
        section->deleteLater();
    }
}

void ChoiceButton::setCurrent(int data)
{
    m_current = data;
    refresh();
}

void ChoiceButton::setShowText(bool on)
{
    m_showText = on;
    setToolButtonStyle(on ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    refresh();
}

void ChoiceButton::refresh()
{
    for (QAction* a : m_menu->actions()) {
        if (a->isSeparator() || a->data().toInt() != m_current || !a->isCheckable())
            continue;
        a->setChecked(true);
        setIcon(a->icon());
        setText(a->text());
        setToolTip(m_tip + QStringLiteral(": ") + a->text());
        return;
    }
}
