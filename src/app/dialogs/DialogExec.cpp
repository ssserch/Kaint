#include "app/dialogs/DialogExec.h"

#include <QApplication>
#include <QDialog>
#include <QEventLoop>
#include <QPointer>
#include <QWidget>

namespace {

// Deja pasar solo la entrada dirigida al diálogo, a sus ventanas hijas (listas desplegables,
// menús, selectores de color) y a otras ventanas modales abiertas desde él.
class InputBlocker : public QObject {
public:
    explicit InputBlocker(QDialog* dialog) : m_dialog(dialog) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::TabletPress:
        case QEvent::TabletRelease:
        case QEvent::TouchBegin:
        case QEvent::Drop:
        case QEvent::Close:
            break;
        default:
            return false;
        }
        auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || !m_dialog)
            return false;
        for (QWidget* w = widget; w; w = w->parentWidget())
            if (w == m_dialog)
                return false;
        if (QWidget* modal = QApplication::activeModalWidget(); modal && modal->isAncestorOf(widget))
            return false;
        if (QApplication::activeModalWidget() == widget->window())
            return false;
        if (event->type() == QEvent::MouseButtonPress)
            m_dialog->activateWindow();
        return true;
    }

private:
    QPointer<QDialog> m_dialog;
};

} // namespace

int execWithoutDimming(QDialog& dialog)
{
    dialog.setWindowModality(Qt::NonModal);
    InputBlocker blocker(&dialog);
    qApp->installEventFilter(&blocker);
    QEventLoop loop;
    QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    dialog.setProperty("kaintPreviewDialog", true);
    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    if (dialog.isVisible())
        loop.exec(QEventLoop::DialogExec);
    qApp->removeEventFilter(&blocker);
    return dialog.result();
}
