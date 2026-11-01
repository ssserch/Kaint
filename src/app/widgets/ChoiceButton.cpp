#include "app/widgets/ChoiceButton.h"

#include "app/Theme.h"

#include <QActionGroup>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QHelpEvent>
#include <QToolTip>
#include <QWidgetAction>

#include <algorithm>

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

// Fila de una sola línea con un icono de otro tamaño (los estilos de guion, más anchos que altos).
class WideIconRow : public QWidget {
public:
    WideIconRow(QAction* action, const QIcon& icon, const QString& title, QSize iconSize, QWidget* parent)
        : QWidget(parent)
        , m_action(action)
        , m_icon(icon)
        , m_title(title)
        , m_iconSize(iconSize)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        return QSize(m_iconSize.width() + 4 * kPad + fontMetrics().horizontalAdvance(m_title) + 3 * kPad,
                     std::max(m_iconSize.height() + 2 * kPad + 4, fontMetrics().height() + 2 * kPad));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Theme::Spec theme = Theme::active();
        const bool hot = underMouse();
        if (hot) {
            p.setPen(theme.selectionBorder);
            p.setBrush(theme.selection);
            p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        // La opción elegida, con el icono enmarcado como en los menús de Paint.NET.
        const QRect icon(2 * kPad, (height() - m_iconSize.height()) / 2, m_iconSize.width(), m_iconSize.height());
        if (m_action->isChecked()) {
            p.setPen(theme.selectionBorder);
            p.setBrush(hot ? Qt::NoBrush : QBrush(theme.selection));
            p.drawRect(QRectF(icon.adjusted(-3, -3, 3, 3)).adjusted(0.5, 0.5, -0.5, -0.5));
        }
        m_icon.paint(&p, icon);
        p.setPen(palette().color(QPalette::Text));
        const int x = icon.right() + 1 + 3 * kPad;
        p.drawText(QRect(x, 0, width() - x - kPad, height()), Qt::AlignLeft | Qt::AlignVCenter, m_title);
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
    static constexpr int kPad = 4;
    QAction* m_action;
    QIcon m_icon;
    QString m_title;
    QSize m_iconSize;
};

// Cuadrícula de iconos con un título por sección (el menú de formas de Paint.NET): la opción
// elegida y la que está bajo el puntero, resaltadas con el color de selección.
class ChoiceGrid : public QWidget {
public:
    ChoiceGrid(const QList<ChoiceButton::Item>& items, int columns, int current, std::function<void(int)> chosen, QWidget* parent)
        : QWidget(parent)
        , m_columns(std::max(1, columns))
        , m_current(current)
        , m_chosen(std::move(chosen))
    {
        setMouseTracking(true);
        int y = 0;
        int column = 0;
        bool open = false;
        for (const ChoiceButton::Item& item : items) {
            if (item.section) {
                if (open)
                    y += kCell + kPad;
                m_headings.append({QRect(0, y, width(), kHeading), item.text});
                y += kHeading + kPad;
                column = 0;
                open = false;
                continue;
            }
            if (open && column == m_columns) {
                column = 0;
                y += kCell + kGap;
            }
            m_cells.append({QRect(kSide + column * (kCell + kGap), y, kCell, kCell), item});
            ++column;
            open = true;
        }
        m_height = y + (open ? kCell + kPad : 0);
    }

    QSize sizeHint() const override { return {2 * kSide + m_columns * kCell + (m_columns - 1) * kGap, m_height}; }

protected:
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::ToolTip) {
            auto* help = static_cast<QHelpEvent*>(e);
            const int i = cellAt(help->pos());
            if (i >= 0)
                QToolTip::showText(help->globalPos(), m_cells[i].item.text, this, m_cells[i].rect);
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const Theme::Spec theme = Theme::active();
        const QColor text = palette().color(QPalette::Text);
        const QColor menu = palette().color(QPalette::Window);
        QColor gutter = menu;
        gutter = QColor::fromRgbF(gutter.redF() * 0.92f + text.redF() * 0.08f, gutter.greenF() * 0.92f + text.greenF() * 0.08f,
                                  gutter.blueF() * 0.92f + text.blueF() * 0.08f);
        QFont bold = font();
        bold.setBold(true);
        for (const auto& [r, title] : m_headings) {
            const QRect band(0, r.top(), width(), r.height());
            p.fillRect(band, gutter);
            p.setFont(bold);
            p.setPen(text);
            p.drawText(band.adjusted(5, 0, -5, 0), Qt::AlignLeft | Qt::AlignVCenter, title);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
        for (int i = 0; i < m_cells.size(); ++i) {
            const Cell& c = m_cells[i];
            if (i == m_hover || c.item.data == m_current) {
                p.setPen(theme.selectionBorder);
                p.setBrush(theme.selection);
                p.drawRect(c.rect.adjusted(0, 0, -1, -1));
                QColor ring(195, 228, 255, theme.dark ? 70 : 140);
                p.setPen(ring);
                p.setBrush(Qt::NoBrush);
                p.drawRect(c.rect.adjusted(1, 1, -2, -2));
            }
            c.item.icon.paint(&p, c.rect.adjusted(3, 3, -3, -3));
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const int i = cellAt(e->position().toPoint());
        if (i != m_hover) {
            m_hover = i;
            update();
        }
    }

    void leaveEvent(QEvent*) override
    {
        m_hover = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        const int i = cellAt(e->position().toPoint());
        if (e->button() != Qt::LeftButton || i < 0)
            return;
        const int data = m_cells[i].item.data;
        for (QWidget* w = parentWidget(); w; w = w->parentWidget())
            if (auto* menu = qobject_cast<QMenu*>(w)) {
                menu->close();
                break;
            }
        if (m_chosen)
            m_chosen(data);
    }

private:
    int cellAt(const QPoint& pos) const
    {
        for (int i = 0; i < m_cells.size(); ++i)
            if (m_cells[i].rect.contains(pos))
                return i;
        return -1;
    }

    static constexpr int kHeading = 21;
    static constexpr int kCell = 30;
    static constexpr int kGap = 2;
    static constexpr int kPad = 3;
    static constexpr int kSide = 6;
    struct Cell {
        QRect rect;
        ChoiceButton::Item item;
    };
    QList<std::pair<QRect, QString>> m_headings;
    QList<Cell> m_cells;
    int m_columns;
    int m_current;
    int m_height = 0;
    int m_hover = -1;
    std::function<void(int)> m_chosen;
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
    m_items.append({false, icon, text, data});
    if (m_gridColumns > 0) {
        if (m_current < 0)
            setCurrent(data);
        return;
    }
    QAction* a;
    if (description.isEmpty() && m_menuIconSize.isValid()) {
        auto* wa = new QWidgetAction(m_menu);
        wa->setIcon(icon);
        wa->setText(text);
        wa->setDefaultWidget(new WideIconRow(wa, icon, text, m_menuIconSize, m_menu));
        m_menu->addAction(wa);
        a = wa;
    } else if (description.isEmpty()) {
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
    m_items.append({true, QIcon(), title, 0});
    if (m_gridColumns <= 0)
        m_menu->addSection(title);
}

void ChoiceButton::setGrid(int columns)
{
    m_gridColumns = columns;
    // La cuadrícula se rehace al abrir el menú (con la opción elegida resaltada).
    connect(m_menu, &QMenu::aboutToShow, this, &ChoiceButton::rebuildGrid, Qt::UniqueConnection);
}

void ChoiceButton::setMenuIconSize(const QSize& size)
{
    m_menuIconSize = size;
}

void ChoiceButton::rebuildGrid()
{
    if (m_gridColumns <= 0)
        return;
    m_menu->clear();
    auto* wa = new QWidgetAction(m_menu);
    wa->setDefaultWidget(new ChoiceGrid(m_items, m_gridColumns, m_current, [this](int data) { choose(data); }, m_menu));
    m_menu->addAction(wa);
}

void ChoiceButton::choose(int data)
{
    setCurrent(data);
    if (onChosen)
        onChosen(data);
}

void ChoiceButton::removeChoicesFrom(int data)
{
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [data](const Item& i) { return !i.section && i.data >= data; }),
                  m_items.end());
    while (!m_items.isEmpty() && m_items.constLast().section)
        m_items.removeLast();
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
    if (m_gridColumns > 0) {
        for (const Item& item : std::as_const(m_items))
            if (!item.section && item.data == m_current) {
                setIcon(item.icon);
                setText(item.text);
                setToolTip(m_tip + QStringLiteral(": ") + item.text);
                return;
            }
        return;
    }
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
