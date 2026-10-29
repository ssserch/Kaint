#include "app/panels/HistoryPanel.h"

#include "app/Icons.h"
#include "app/Theme.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QAction>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUndoView>
#include <QVBoxLayout>

namespace {

// Añade el icono de cada paso y atenúa los pasos deshechos, como Paint.NET.
class HistoryDelegate : public QStyledItemDelegate {
public:
    HistoryDelegate(QUndoGroup* group, QObject* parent)
        : QStyledItemDelegate(parent)
        , m_group(group)
    {
    }

    QString rootIcon = QStringLiteral("document-new");

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        const QString name = index.row() == 0 ? rootIcon : HistoryIcons::iconFor(index.data().toString());
        const QIcon icon = Icons::theme(name);
        if (!icon.isNull()) {
            option->icon = icon;
            option->features |= QStyleOptionViewItem::HasDecoration;
            option->decorationSize = QSize(16, 16);
        }
        if (QUndoStack* stack = m_group->activeStack(); stack && index.row() > stack->index()) {
            QColor c = option->palette.color(QPalette::Text);
            c.setAlpha(110);
            option->palette.setColor(QPalette::Text, c);
        }
    }

private:
    QUndoGroup* m_group;
};

} // namespace

HistoryPanel::HistoryPanel(QUndoGroup* group, QAction* undo, QAction* redo, QWidget* parent)
    : QWidget(parent)
    , m_group(group)
    , m_view(new QUndoView(group, this))
{
    m_view->setItemDelegate(new HistoryDelegate(group, m_view));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformItemSizes(true);
    m_view->viewport()->installEventFilter(this);

    // Franja de botones de abajo (con el aspecto de Paint.NET, de otro color y con una línea).
    auto* strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("PanelButtons"));
    auto* buttons = new QHBoxLayout(strip);
    buttons->setContentsMargins(2, 2, 2, 2);
    buttons->setSpacing(0);
    auto jumpButton = [this](const char* icon, const QString& tip, bool toEnd) {
        auto* b = new QToolButton(this);
        b->setIcon(Icons::theme({icon}));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, [this, toEnd] {
            QUndoStack* stack = m_group->activeStack();
            if (!stack)
                return;
            emit aboutToJump();
            stack->setIndex(toEnd ? stack->count() : 0);
        });
        return b;
    };
    m_jumps.append(jumpButton("media-skip-backward", tr("Volver al principio"), false));
    buttons->addWidget(m_jumps.last());
    for (QAction* a : {undo, redo}) {
        auto* b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        buttons->addWidget(b);
    }
    m_jumps.append(jumpButton("media-skip-forward", tr("Avanzar hasta el final"), true));
    buttons->addWidget(m_jumps.last());
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view, 1);
    layout->addWidget(strip);

    // Paint.NET solo tiene deshacer y rehacer.
    auto applyLook = [this] {
        for (QWidget* b : std::as_const(m_jumps))
            b->setVisible(!Theme::paintNetLook());
    };
    applyLook();
    connect(Theme::notifier(), &Theme::Notifier::changed, this, applyLook);
}

void HistoryPanel::setDocument(Document* doc)
{
    auto* delegate = static_cast<HistoryDelegate*>(m_view->itemDelegate());
    if (doc) {
        m_view->setEmptyLabel(doc->historyRootLabel());
        delegate->rootIcon = doc->filePath().isEmpty() ? QStringLiteral("document-new") : QStringLiteral("document-open");
    }
    m_view->viewport()->update();
}

bool HistoryPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_view->viewport() && e->type() == QEvent::MouseButtonPress) {
        emit aboutToJump();
        auto* me = static_cast<QMouseEvent*>(e);
        QUndoStack* stack = m_group->activeStack();
        const QModelIndex index = m_view->indexAt(me->position().toPoint());
        if (stack && index.isValid() && me->button() == Qt::LeftButton && index.row() == stack->index()
            && stack->index() > 0) {
            stack->setIndex(stack->index() - 1);
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}
