#include "app/panels/ToolsPanel.h"

#include "app/Customization.h"
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
        m_all.append(b);
    }
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    applyCustomization();
    // Herramientas que aparecen o desaparecen (las de PhotoCraft, al abrir Kaint o al activarlas, u
    // ocultas en Personalización): conserva las columnas y la ventana crece o encoge con las filas.
    const auto relayout = [this] { refit([this] { applyCustomization(); }); };
    connect(manager, &ToolManager::availabilityChanged, this, relayout);
    connect(Customization::instance(), &Customization::toolsChanged, this, relayout);

    connect(m_group, &QButtonGroup::idClicked, this, [this](int id) { m_manager->select(ToolId(id)); });
    connect(manager, &ToolManager::toolChanged, this, &ToolsPanel::onToolChanged);
    if (manager->current())
        onToolChanged(manager->current());
}

void ToolsPanel::applyCustomization()
{
    const Customization* custom = Customization::instance();
    QList<int> defaults;
    for (Tool* t : m_manager->tools())
        defaults << int(t->id());
    m_buttons.clear();
    for (int id : custom->toolOrder(defaults)) {
        auto* b = qobject_cast<QToolButton*>(m_group->button(id));
        if (!b)
            continue;
        // Las de PhotoCraft, solo si están activadas.
        const bool hidden = custom->isToolHidden(id) || !m_manager->isAvailable(m_manager->tool(ToolId(id)));
        b->setVisible(!hidden);
        if (!hidden)
            m_buttons.append(b);
    }
    // Sin ninguna, al menos la primera (la ventana no se queda vacía).
    if (m_buttons.isEmpty() && !m_all.isEmpty()) {
        m_all.first()->setVisible(true);
        m_buttons.append(m_all.first());
    }
}

void ToolsPanel::setIconSize(int side)
{
    // Conserva las columnas: el tamaño de los botones ya puede haber cambiado con el tema.
    refit([this, side] {
        for (QToolButton* b : std::as_const(m_all))
            b->setIconSize(QSize(side, side));
    });
}

void ToolsPanel::refit(const std::function<void()>& change)
{
    // Las columnas, las del ancho de la ventana (no las del contenido: al abrir Kaint todavía no
    // está colocado y su tamaño no es el de la ventana, y la cuenta salía con filas de más). Sin
    // ventana, las últimas colocadas.
    auto* panel = qobject_cast<FloatingPanel*>(parentWidget());
    const int columns = panel ? columnsFor(panel->contentSize().width()) : m_columns;
    change();
    updateGeometry();
    if (panel) {
        panel->resizeForContent(sizeFor(columns));
        panel->reposition();
    }
    layoutButtons();
}

QSize ToolsPanel::cell() const
{
    QSize s(1, 1);
    for (const QToolButton* b : m_all)
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
    return sizeFor(2);
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
        const int ch = cell().height() + kSpacing;
        const int rows = std::clamp((proposed.height() - 2 * kMargin + kSpacing) / ch, 1, n);
        return sizeFor((n + rows - 1) / rows);
    }
    return sizeFor(columnsFor(proposed.width()));
}

QSize ToolsPanel::sizeForLayout(const QVariant& state) const
{
    bool ok = false;
    const int columns = state.toInt(&ok);
    return ok && columns > 0 ? sizeFor(columns) : QSize();
}

void ToolsPanel::layoutButtons()
{
    // Como en Paint.NET, el último botón (Formas) ocupa lo que quede de su fila.
    const int cols = columns();
    m_columns = cols;
    const QSize c = cell();
    for (int i = 0; i < m_buttons.size(); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const int span = i == m_buttons.size() - 1 ? cols - col : 1;
        m_buttons[i]->setGeometry(kMargin + col * (c.width() + kSpacing), kMargin + row * (c.height() + kSpacing),
                                  span * c.width() + (span - 1) * kSpacing, c.height());
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
