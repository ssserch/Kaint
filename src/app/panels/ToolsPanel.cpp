#include "app/panels/ToolsPanel.h"

#include "app/tools/ToolManager.h"

#include <QButtonGroup>
#include <QToolButton>

namespace {
constexpr int kMargin = 2;
constexpr int kSpacing = 1;
}

ToolsPanel::ToolsPanel(ToolManager* manager, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_group(new QButtonGroup(this))
{
    for (Tool* t : manager->tools()) {
        auto* b = new QToolButton(this);
        b->setIcon(t->icon());
        b->setIconSize(QSize(22, 22));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolTip(QStringLiteral("%1 (%2)").arg(t->name(), t->shortcut().toString(QKeySequence::NativeText)));
        if (t->icon().isNull())
            b->setText(t->name().left(2));
        m_group->addButton(b, int(t->id()));
        m_buttons.append(b);
    }
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) { m_manager->select(ToolId(id)); });
    connect(manager, &ToolManager::toolChanged, this, &ToolsPanel::onToolChanged);
    if (manager->current())
        onToolChanged(manager->current());
}

QSize ToolsPanel::cell() const
{
    QSize s(1, 1);
    for (const QToolButton* b : m_buttons)
        s = s.expandedTo(b->sizeHint());
    return s;
}

QSize ToolsPanel::sizeFor(int columns) const
{
    const int n = std::max<int>(1, int(m_buttons.size()));
    columns = std::clamp(columns, 1, n);
    const int rows = (n + columns - 1) / columns;
    const QSize c = cell();
    return QSize(2 * kMargin + columns * c.width() + (columns - 1) * kSpacing,
                 2 * kMargin + rows * c.height() + (rows - 1) * kSpacing);
}

int ToolsPanel::columnsFor(int width) const
{
    const int n = std::max<int>(1, int(m_buttons.size()));
    const int c = cell().width() + kSpacing;
    return std::clamp((width - 2 * kMargin + kSpacing) / c, 1, n);
}

QSize ToolsPanel::sizeHint() const
{
    return sizeFor(2); // dos columnas, como Paint.NET
}

QSize ToolsPanel::minimumSizeHint() const
{
    const QSize c = cell();
    return QSize(2 * kMargin + c.width(), 2 * kMargin + c.height());
}

QSize ToolsPanel::fitSize(QSize proposed, Qt::Orientations changed) const
{
    const int n = std::max<int>(1, int(m_buttons.size()));
    if (changed == Qt::Vertical) {
        // Al estirar en vertical, caben más filas y hacen falta menos columnas.
        const int ch = cell().height() + kSpacing;
        const int rows = std::clamp((proposed.height() - 2 * kMargin + kSpacing) / ch, 1, n);
        return sizeFor((n + rows - 1) / rows);
    }
    // El ancho manda: tantas columnas como quepan y el alto justo.
    return sizeFor(columnsFor(proposed.width()));
}

void ToolsPanel::layoutButtons()
{
    const int cols = columns();
    const QSize c = cell();
    for (int i = 0; i < m_buttons.size(); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        m_buttons[i]->setGeometry(kMargin + col * (c.width() + kSpacing), kMargin + row * (c.height() + kSpacing),
                                  c.width(), c.height());
    }
}

void ToolsPanel::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    layoutButtons();
}

void ToolsPanel::onToolChanged(Tool* tool)
{
    if (QAbstractButton* b = m_group->button(int(tool->id())))
        b->setChecked(true);
}
