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
    // El botón de la colaboración (a la derecha), visible mientras lo esté la acción.
    void setCollabAction(QAction* action);
    // Qué pasos se ven: todos, los del editor de imágenes (sin los del editor de GIF) o solo los del
    // editor de GIF (cada editor tiene su vista del historial; deshacer sigue siendo el de la imagen).
    enum class Filter { All, Image, Gif };
    void setFilter(Filter filter);

signals:
    void aboutToJump();

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void applyFilter();

    Filter m_filter = Filter::All;
    QUndoGroup* m_group;
    QUndoView* m_view;
    // Ir al principio y al final.
    QList<QWidget*> m_jumps;
    QWidget* m_strip = nullptr;
};
