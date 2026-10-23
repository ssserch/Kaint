#include "app/tools/EditableSteps.h"

#include <QUndoStack>

ToolStepCommand::ToolStepCommand(Document* doc, LayerPtr layer, const QImage& before, const QRect& area, const QString& text,
                                 ToolId tool, std::shared_ptr<const ToolStepState> state, const QImage& base)
    : PixelPatchCommand(doc, layer, before, area, text)
    , m_layer(std::move(layer))
    , m_tool(tool)
    , m_state(std::move(state))
    , m_base(base)
    , m_area(area)
{
}

void ToolStepCommand::releaseMemory()
{
    PixelPatchCommand::releaseMemory();
    m_base = QImage();
}

StepRecorder::StepRecorder(ToolId tool, const QString& icon)
    : m_tool(tool)
    , m_icon(icon)
{
}

void StepRecorder::start(const LayerEdit& edit)
{
    m_steps = 0;
    m_dirty = false;
    m_stepImage = edit.before();
}

void StepRecorder::push(Document* doc, QUndoCommand* cmd, const QString& text)
{
    HistoryIcons::registerIcon(text, m_icon);
    m_pushing = true;
    doc->undoStack()->push(cmd);
    m_pushing = false;
}

void StepRecorder::commit(LayerEdit& edit, const QString& createText, const QString& editText,
                          std::shared_ptr<const ToolStepState> state)
{
    if (!edit.isActive() || !m_dirty)
        return;
    m_dirty = false;
    const QString text = m_steps == 0 ? createText : editText;
    auto* cmd = new ToolStepCommand(edit.document(), edit.layer(), m_stepImage, edit.touchedArea(), text, m_tool,
                                    std::move(state), edit.before());
    if (cmd->isEmpty()) {
        delete cmd;
        return;
    }
    push(edit.document(), cmd, text);
    m_stepImage = edit.layer()->image;
    ++m_steps;
}

void StepRecorder::finish(LayerEdit& edit, const QString& finishText)
{
    if (!edit.isActive())
        return;
    if (m_steps == 0)
        edit.cancel();
    else
        push(edit.document(), new QUndoCommand(finishText), finishText);
    leave(edit);
}

void StepRecorder::leave(LayerEdit& edit)
{
    edit.detach();
    m_steps = 0;
    m_dirty = false;
    m_stepImage = QImage();
}

const ToolStepCommand* StepRecorder::top(Document* doc) const
{
    if (!doc)
        return nullptr;
    const QUndoStack* stack = doc->undoStack();
    const auto* step = stack->index() > 0 ? dynamic_cast<const ToolStepCommand*>(stack->command(stack->index() - 1)) : nullptr;
    return step && step->tool() == m_tool && step->editable() ? step : nullptr;
}

void StepRecorder::resume(LayerEdit& edit, Document* doc, const ToolStepCommand* step)
{
    edit.resume(doc, step->layer(), step->base(), step->area());
    m_stepImage = step->layer()->image;
    m_steps = 1;
    m_dirty = false;
}
