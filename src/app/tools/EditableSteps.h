#pragma once

#include "app/AppState.h"
#include "app/tools/Tool.h"
#include "core/Commands.h"
#include "core/LayerEdit.h"

#include <memory>

// Estado de una herramienta guardado en un paso editable: sus opciones y colores, más lo que
// añada cada herramienta (nodos de la línea, cuadro de la forma...).
struct ToolStepState {
    virtual ~ToolStepState() = default;
    ToolSettings settings;
    QColor primary;
    QColor secondary;
};

// Paso editable del historial (línea, formas), como en Paint.NET: guarda la capa de antes de
// empezar y el estado de la herramienta para seguir editando al volver a él.
class ToolStepCommand : public PixelPatchCommand, public EditableStep {
public:
    ToolStepCommand(Document* doc, LayerPtr layer, const QImage& before, const QRect& area, const QString& text, ToolId tool,
                    std::shared_ptr<const ToolStepState> state, const QImage& base);
    ToolId tool() const override { return m_tool; }
    const LayerPtr& layer() const { return m_layer; }
    const ToolStepState* state() const { return m_state.get(); }
    const QImage& base() const { return m_base; }
    QRect area() const { return m_area; }
    bool editable() const { return !m_base.isNull(); }
    void releaseMemory() override;

private:
    LayerPtr m_layer;
    ToolId m_tool;
    std::shared_ptr<const ToolStepState> m_state;
    QImage m_base;
    QRect m_area;
};

// Pasos de una edición: el primero al crear ("Línea/Curva"), uno por cada cambio terminado
// ("Editar…") y "Finalizar…" al acabar. Al volver a uno de sus pasos, la herramienta lo retoma.
class StepRecorder {
public:
    StepRecorder(ToolId tool, const QString& icon);

    void start(const LayerEdit& edit);
    void changed() { m_dirty = true; }
    void commit(LayerEdit& edit, const QString& createText, const QString& editText,
                std::shared_ptr<const ToolStepState> state);
    // "Finalizar…" si hubo pasos; si no, se descarta lo dibujado.
    void finish(LayerEdit& edit, const QString& finishText);
    void leave(LayerEdit& edit);

    // Paso de esta herramienta en lo alto del historial (para retomarlo), o nulo.
    const ToolStepCommand* top(Document* doc) const;
    void resume(LayerEdit& edit, Document* doc, const ToolStepCommand* step);
    bool pushing() const { return m_pushing; }

private:
    void push(Document* doc, QUndoCommand* cmd, const QString& text);

    ToolId m_tool;
    QString m_icon;
    int m_steps = 0;
    QImage m_stepImage;
    bool m_dirty = false;
    bool m_pushing = false;
};
