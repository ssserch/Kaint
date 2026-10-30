#pragma once

#include <QWidget>

class QAction;
class QUndoGroup;
class QUndoView;
class Document;

// Ventana Historial: lista de pasos; un clic salta a ese punto y hacer clic en el paso actual
// lo deshace (y otra vez lo rehace), como en Paint.NET.
class HistoryPanel : public QWidget {
    Q_OBJECT
public:
    HistoryPanel(QUndoGroup* group, QAction* undo, QAction* redo, QWidget* parent = nullptr);
    void setDocument(Document* doc);
    // Ir al principio y al final, junto a deshacer y rehacer (Configuración > Interfaz de usuario).
    void setJumpButtonsVisible(bool on);

signals:
    void aboutToJump();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    QUndoGroup* m_group;
    QUndoView* m_view;
    // Ir al principio y al final.
    QList<QWidget*> m_jumps;
};
