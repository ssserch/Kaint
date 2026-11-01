#include "app/dialogs/DialogExec.h"

#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QDialog>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <QWidget>

namespace {

// Deja pasar solo la entrada dirigida al diálogo, a sus ventanas hijas (listas desplegables,
// menús, selectores de color) y a otras ventanas modales abiertas desde él. También el paso del
// puntero: si no, las herramientas y las capas se seguían resaltando debajo, y arrastrar sobre la
// lista de capas cambiaba la capa elegida aunque el clic no le llegase. Las salidas sí pasan, para
// que nada se quede resaltado.
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
        case QEvent::TabletMove:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::MouseMove:
        case QEvent::Enter:
        case QEvent::HoverEnter:
        case QEvent::HoverMove:
        case QEvent::ToolTip:
        case QEvent::DragEnter:
        case QEvent::DragMove:
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

// Al cerrar el diálogo, el control bajo el puntero no recibió su entrada (estaba bloqueada):
// se la da ahora para que se resalte sin tener que salir y volver a entrar.
void reenterUnderCursor()
{
    QWidget* under = QApplication::widgetAt(QCursor::pos());
    for (QWidget* w = under; w; w = w->isWindow() ? nullptr : w->parentWidget()) {
        if (w->testAttribute(Qt::WA_UnderMouse))
            continue;
        const QPointF local = w->mapFromGlobal(QCursor::pos());
        QEnterEvent enter(local, local, QCursor::pos());
        QApplication::sendEvent(w, &enter);
        if (w->testAttribute(Qt::WA_Hover)) {
            QHoverEvent hover(QEvent::HoverEnter, local, QCursor::pos(), QPointF(-1, -1));
            QApplication::sendEvent(w, &hover);
        }
    }
}

} // namespace

int execWithoutDimming(QDialog& dialog)
{
    dialog.setWindowModality(Qt::NonModal);
    InputBlocker blocker(&dialog);
    qApp->installEventFilter(&blocker);
    QEventLoop loop;
    QObject::connect(&dialog, &QDialog::finished, &loop, &QEventLoop::quit);
    dialog.setProperty("kaintPreviewDialog", true);
    // Los clics fuera del diálogo no llegan a la ventana, pero el gestor de ventanas aún podía
    // activarla y dejar el diálogo detrás: si el foco pasa a otra ventana de Kaint que no es suya
    // (ni un menú o un diálogo abierto desde él), vuelve al diálogo, como con uno modal.
    const QPointer<QDialog> guarded(&dialog);
    const QMetaObject::Connection focus = QObject::connect(qApp, &QGuiApplication::focusWindowChanged, &dialog, [guarded](QWindow* w) {
        if (!w || !guarded || !guarded->windowHandle())
            return;
        for (QWindow* p = w; p; p = p->transientParent())
            if (p == guarded->windowHandle())
                return;
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
            return;
        QTimer::singleShot(0, guarded, [guarded] {
            if (!guarded)
                return;
            guarded->raise();
            guarded->activateWindow();
        });
    });
    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    if (dialog.isVisible())
        loop.exec(QEventLoop::DialogExec);
    QObject::disconnect(focus);
    qApp->removeEventFilter(&blocker);
    reenterUnderCursor();
    return dialog.result();
}
