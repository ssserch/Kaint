#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

// Atajos de teclado editables (Configuración > Atajos de teclado), como en paint.js: se recuerdan
// los de fábrica de cada acción y se aplican los que eligió el usuario (QSettings "shortcuts").
namespace Shortcuts {

struct Entry {
    QString id;
    QAction* action;
    QList<QKeySequence> defaults;
};

// Registra las acciones de la ventana (con sus atajos de fábrica) y aplica los guardados.
void registerActions(const QHash<QString, QAction*>& actions);
// Acciones con nombre, en orden alfabético por su texto.
QList<Entry> entries();
// Texto de la acción sin "&" ni "...".
QString displayName(const QAction* action);
// Cambia el atajo (vacío: ninguno) y lo guarda; si otra acción lo usaba, se lo quita y la
// devuelve.
QAction* assign(const QString& id, const QKeySequence& sequence);
void reset(const QString& id);
void resetAll();
bool isDefault(const QString& id);

} // namespace Shortcuts
