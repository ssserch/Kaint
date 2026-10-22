#include "app/widgets/FontChooser.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QStyledItemDelegate>

namespace {

// Cada fuente con su nombre y una frase de ejemplo escrita con ella.
class FontPreviewDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static QString sample() { return QObject::tr("El veloz murciélago hindú comía feliz cardillo y kiwi"); }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString family = opt.text;
        opt.text.clear();
        const QWidget* w = opt.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const bool selected = opt.state & QStyle::State_Selected;
        p->save();
        p->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        QRect r = opt.rect.adjusted(4, 0, -4, 0);
        p->setFont(opt.font);
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, family);
        r.setLeft(r.left() + QFontMetrics(opt.font).horizontalAdvance(family) + 6);
        QFont preview(family);
        preview.setPointSizeF(opt.font.pointSizeF() * 1.25);
        p->setFont(preview);
        p->setClipRect(r);
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, sample());
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QFont preview(index.data(Qt::DisplayRole).toString());
        preview.setPointSizeF(option.font.pointSizeF() * 1.25);
        const int h = std::max(QFontMetrics(option.font).height(), QFontMetrics(preview).height()) + 6;
        return QSize(420, h);
    }
};

} // namespace

FontChooser::FontChooser(QWidget* parent)
    : QFontComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setItemDelegate(new FontPreviewDelegate(this));
    view()->setMinimumWidth(460);
    lineEdit()->setReadOnly(true);
    lineEdit()->installEventFilter(this);
    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        if (!m_editing)
            showPopup();
    });
    connect(lineEdit(), &QLineEdit::returnPressed, this, [this] {
        if (m_editing)
            stopEditing(true);
    });
}

void FontChooser::startEditing()
{
    m_editing = true;
    m_before = currentText();
    lineEdit()->setReadOnly(false);
    lineEdit()->setFocus(Qt::MouseFocusReason);
    lineEdit()->selectAll();
}

void FontChooser::stopEditing(bool commit)
{
    if (!m_editing)
        return;
    m_editing = false;
    lineEdit()->setReadOnly(true);
    if (commit) {
        // Se acepta si coincide con una fuente instalada (sin distinguir mayúsculas).
        const QString typed = lineEdit()->text().trimmed();
        const int i = findText(typed, Qt::MatchFixedString);
        if (i >= 0) {
            setCurrentIndex(i);
            return;
        }
    }
    lineEdit()->setText(m_before);
}

bool FontChooser::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != lineEdit())
        return QFontComboBox::eventFilter(watched, event);
    if (!m_editing) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
            if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
                m_clickTimer.start(QApplication::doubleClickInterval());
            return true;
        case QEvent::MouseButtonDblClick:
            m_clickTimer.stop();
            startEditing();
            return true;
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove:
            return true;
        default:
            break;
        }
        return QFontComboBox::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            stopEditing(false);
            return true;
        }
        break;
    case QEvent::FocusOut:
        // La lista del autocompletado no cuenta como "clic fuera".
        if (static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
            stopEditing(false);
        break;
    default:
        break;
    }
    return QFontComboBox::eventFilter(watched, event);
}
