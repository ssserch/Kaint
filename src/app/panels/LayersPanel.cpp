#include "app/panels/LayersPanel.h"

#include "core/Document.h"
#include "core/Raster.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kRowHeight = 50;
constexpr int kThumb = 42;
constexpr int VisibleRole = Qt::UserRole + 1;
constexpr int ThumbRole = Qt::UserRole + 2;

class LayerDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<void(int row, bool ctrl)> onToggle;

    static QRect checkRect(const QRect& item)
    {
        return QRect(item.right() - 24, item.center().y() - 9, 18, 18);
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {180, kRowHeight}; }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        opt.text.clear();
        opt.icon = QIcon();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const QRect r = option.rect;
        const QRect thumbBox(r.left() + 4, r.top() + (r.height() - kThumb) / 2, kThumb, kThumb);
        const QImage thumb = index.data(ThumbRole).value<QImage>();
        if (!thumb.isNull()) {
            const QSizeF size = QSizeF(thumb.size()) / thumb.devicePixelRatio();
            const QRectF target(thumbBox.left() + (kThumb - size.width()) / 2, thumbBox.top() + (kThumb - size.height()) / 2,
                                size.width(), size.height());
            p->drawImage(target, thumb);
            p->setPen(option.palette.color(QPalette::Mid));
            p->drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5));
        }

        const bool selected = option.state & QStyle::State_Selected;
        p->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        const QRect textRect(thumbBox.right() + 8, r.top(), r.width() - kThumb - 44, r.height());
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width()));

        QStyleOptionButton check;
        check.rect = checkRect(r);
        check.state = QStyle::State_Enabled | (index.data(VisibleRole).toBool() ? QStyle::State_On : QStyle::State_Off);
        style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, p, w);
    }

    bool editorEvent(QEvent* e, QAbstractItemModel*, const QStyleOptionViewItem& option, const QModelIndex& index) override
    {
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
            // La casilla no selecciona la capa ni abre sus propiedades.
            auto* me = static_cast<QMouseEvent*>(e);
            if (checkRect(option.rect).adjusted(-4, -4, 4, 4).contains(me->position().toPoint()))
                return true;
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(e);
            if (checkRect(option.rect).adjusted(-4, -4, 4, 4).contains(me->position().toPoint())) {
                if (onToggle)
                    onToggle(index.row(), me->modifiers() & Qt::ControlModifier);
                return true;
            }
        }
        return false;
    }
};

} // namespace

LayersPanel::LayersPanel(const QList<QAction*>& actions, QWidget* parent)
    : QWidget(parent)
    , m_list(new QListWidget(this))
{
    auto* delegate = new LayerDelegate(m_list);
    delegate->onToggle = [this](int row, bool ctrl) {
        if (ctrl)
            emit soloRequested(layerForRow(row));
        else
            emit visibilityToggled(layerForRow(row));
    };
    m_list->setItemDelegate(delegate);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    // Arrastrar y soltar para reordenar: se detecta a mano y se hace con un
    // comando del historial en lugar del movimiento interno de la lista.
    m_list->viewport()->installEventFilter(this);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(2, 2, 2, 2);
    buttons->setSpacing(0);
    for (QAction* a : actions) {
        auto* b = new QToolButton(this);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(16, 16));
        buttons->addWidget(b);
    }
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_doc && row >= 0)
            m_doc->setActiveIndex(layerForRow(row));
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { emit propertiesRequested(); });

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(250);
    connect(&m_thumbTimer, &QTimer::timeout, this, &LayersPanel::updateThumbnails);
}

bool LayersPanel::eventFilter(QObject* obj, QEvent* e)
{
    if (obj != m_list->viewport() || !m_doc)
        return QWidget::eventFilter(obj, e);
    switch (e->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(e);
        m_dragRow = me->button() == Qt::LeftButton ? m_list->indexAt(me->position().toPoint()).row() : -1;
        m_pressPos = me->position().toPoint();
        m_dragging = false;
        break;
    }
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (m_dragRow >= 0 && (me->buttons() & Qt::LeftButton)) {
            if (!m_dragging && (me->position().toPoint() - m_pressPos).manhattanLength() > QApplication::startDragDistance())
                m_dragging = true;
            if (m_dragging) {
                m_list->viewport()->setCursor(Qt::DragMoveCursor);
                return true;
            }
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (m_dragging) {
            m_list->viewport()->unsetCursor();
            int target = m_list->indexAt(me->position().toPoint()).row();
            if (target < 0)
                target = me->position().y() < 0 ? 0 : m_list->count() - 1;
            const int from = layerForRow(m_dragRow);
            const int to = layerForRow(target);
            m_dragging = false;
            m_dragRow = -1;
            if (from != to)
                emit moveRequested(from, to);
            return true;
        }
        m_dragRow = -1;
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(obj, e);
}

int LayersPanel::rowForLayer(int index) const
{
    return m_doc ? m_doc->layerCount() - 1 - index : -1;
}

int LayersPanel::layerForRow(int row) const
{
    return m_doc ? m_doc->layerCount() - 1 - row : -1;
}

void LayersPanel::setDocument(Document* doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (m_doc) {
        connect(m_doc, &Document::layersChanged, this, &LayersPanel::rebuild);
        connect(m_doc, &Document::sizeChanged, this, &LayersPanel::rebuild);
        connect(m_doc, &Document::activeLayerChanged, this, &LayersPanel::syncSelection);
        connect(m_doc, &Document::layerPixelsChanged, this, [this](int index) {
            m_dirty.insert(index);
            if (!m_thumbTimer.isActive())
                m_thumbTimer.start();
        });
    }
    rebuild();
}

void LayersPanel::rebuild()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    m_dirty.clear();
    if (!m_doc)
        return;
    for (int row = 0; row < m_doc->layerCount(); ++row) {
        const Layer* l = m_doc->layer(layerForRow(row));
        auto* item = new QListWidgetItem(l->props.name, m_list);
        item->setData(VisibleRole, l->props.visible);
        item->setToolTip(tr("%1\nOpacidad: %2 · Fusión: %3")
                             .arg(l->props.name)
                             .arg(l->props.opacity)
                             .arg(blendModeName(l->props.blend)));
        m_dirty.insert(layerForRow(row));
    }
    updateThumbnails();
    syncSelection();
}

void LayersPanel::syncSelection()
{
    if (!m_doc)
        return;
    const QSignalBlocker block(m_list);
    m_list->setCurrentRow(rowForLayer(m_doc->activeIndex()));
}

void LayersPanel::updateThumbnails()
{
    if (!m_doc)
        return;
    const qreal dpr = devicePixelRatioF();
    for (int index : std::as_const(m_dirty)) {
        QListWidgetItem* item = m_list->item(rowForLayer(index));
        const Layer* l = m_doc->layer(index);
        if (!item || !l)
            continue;
        QImage thumb = Raster::thumbnail(l->image, QSize(kThumb, kThumb) * dpr);
        thumb.setDevicePixelRatio(dpr);
        item->setData(ThumbRole, thumb);
    }
    m_dirty.clear();
}
