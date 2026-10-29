#include "app/widgets/FontChooser.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
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

ComboClickToOpen::ComboClickToOpen(QComboBox* combo)
    : QObject(combo)
    , m_combo(combo)
{
    combo->lineEdit()->setReadOnly(true);
    combo->lineEdit()->installEventFilter(this);
    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        if (!m_editing)
            m_combo->showPopup();
    });
    connect(combo->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        if (m_editing)
            stopEditing(true);
    });
}

void ComboClickToOpen::startEditing()
{
    m_editing = true;
    // Un clic en cualquier otro sitio (aunque no tome el foco) vuelve a dejarlo como botón.
    qApp->installEventFilter(this);
    m_before = m_combo->lineEdit()->text();
    m_combo->lineEdit()->setReadOnly(false);
    m_combo->lineEdit()->setFocus(Qt::MouseFocusReason);
    m_combo->lineEdit()->selectAll();
}

void ComboClickToOpen::stopEditing(bool commit)
{
    if (!m_editing)
        return;
    m_editing = false;
    qApp->removeEventFilter(this);
    QLineEdit* edit = m_combo->lineEdit();
    edit->setReadOnly(true);
    if (!commit) {
        // Se vuelve al valor de antes (el campo lo aplica al editarlo).
        edit->setText(m_before);
        emit edit->textEdited(m_before);
    }
    edit->deselect();
    edit->setCursorPosition(0);
    m_combo->clearFocus();
}

bool ComboClickToOpen::eventFilter(QObject* watched, QEvent* event)
{
    if (m_editing && event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(watched);
        if (w != m_combo && !m_combo->isAncestorOf(w) && w->window() != m_combo->view()->window())
            stopEditing(true);
    }
    if (watched != m_combo->lineEdit())
        return QObject::eventFilter(watched, event);
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
            return false;
        }
    }
    switch (event->type()) {
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            stopEditing(false);
            return true;
        }
        break;
    case QEvent::FocusOut:
        if (static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
            stopEditing(true);
        break;
    default:
        break;
    }
    return false;
}

FontChooser::FontChooser(QWidget* parent)
    : QFontComboBox(parent)
{
    setEditable(true);
    setInsertPolicy(QComboBox::NoInsert);
    setItemDelegate(new FontPreviewDelegate(this));
    // Ancho fijo como en Paint.NET (si no, cambia con el nombre de cada fuente).
    setMinimumContentsLength(24);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setSizePolicy(QSizePolicy::Fixed, sizePolicy().verticalPolicy());
    view()->setMinimumWidth(460);
    setMaxVisibleItems(24);
    // La lista lleva barra de desplazamiento (el estilo la cambiaba por flechas arriba y abajo).
    setProperty("kaintScrollableList", true);
    lineEdit()->setReadOnly(true);
    lineEdit()->installEventFilter(this);
    completer()->setCaseSensitivity(Qt::CaseInsensitive);
    completer()->setFilterMode(Qt::MatchStartsWith);
    completer()->setCompletionMode(QCompleter::PopupCompletion);
    // Elegir de la lista no deja el nombre marcado ni el teclado en el campo.
    connect(this, &QComboBox::activated, this, [this] {
        QTimer::singleShot(0, this, [this] {
            lineEdit()->deselect();
            lineEdit()->setCursorPosition(0);
            clearFocus();
            emit chosen();
        });
    });
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
    qApp->installEventFilter(this);
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
    qApp->removeEventFilter(this);
    lineEdit()->setReadOnly(true);
    if (commit) {
        const QString typed = lineEdit()->text().trimmed();
        int i = findText(typed, Qt::MatchFixedString);
        if (i < 0 && !typed.isEmpty())
            i = findText(typed, Qt::MatchStartsWith);
        if (i >= 0) {
            setCurrentIndex(i);
            lineEdit()->deselect();
            lineEdit()->setCursorPosition(0);
            clearFocus();
            emit chosen();
            return;
        }
    }
    lineEdit()->setText(m_before);
    lineEdit()->deselect();
}

bool FontChooser::eventFilter(QObject* watched, QEvent* event)
{
    if (m_editing && event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(watched);
        if (w != this && !isAncestorOf(w) && w->window() != view()->window() && w->window() != completer()->popup()->window())
            stopEditing(true);
    }
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
        if (static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason)
            stopEditing(true);
        break;
    default:
        break;
    }
    return QFontComboBox::eventFilter(watched, event);
}
