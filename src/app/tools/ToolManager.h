#pragma once

#include "app/tools/Tool.h"

#include <QList>
#include <QObject>

class Canvas;

class ToolManager : public QObject {
    Q_OBJECT
public:
    explicit ToolManager(Canvas* canvas, QObject* parent = nullptr);

    const QList<Tool*>& tools() const { return m_tools; }
    Tool* tool(ToolId id) const;
    Tool* current() const { return m_current; }

    void select(ToolId id);
    void selectPrevious();
    void activateShortcut(const QKeySequence& key, bool reverse = false);
    QList<QKeySequence> shortcuts() const;
    // Las herramientas de PhotoCraft (Configuración > Complementos): sin ellas no se ven ni se eligen.
    void setExtrasEnabled(bool on);
    bool extrasEnabled() const { return m_extras; }
    bool isAvailable(const Tool* t) const { return t && (!t->isExtra() || m_extras); }

    // Configuración > Herramientas: al terminar una selección se pasa a la herramienta Mover
    // selección (Paint.NET no lo hace; activado por defecto).

signals:
    void toolChanged(Tool* tool);
    void availabilityChanged();

private:
    Canvas* m_canvas;
    QList<Tool*> m_tools;
    Tool* m_current = nullptr;
    Tool* m_previous = nullptr;
    bool m_extras = false;
};
