#include "app/tools/ToolManager.h"

#include "app/AppState.h"
#include "app/Canvas.h"
#include "app/tools/FillTools.h"
#include "app/tools/MoveTools.h"
#include "app/tools/PaintTools.h"
#include "app/tools/SelectionTools.h"
#include "app/tools/ShapeTools.h"
#include "app/tools/TextTool.h"
#include "app/tools/ViewTools.h"
#include "core/Raster.h"

#include <QTimer>

ToolManager::ToolManager(Canvas* canvas, QObject* parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    // Mismo orden que la ventana Herramientas de Paint.NET (dos columnas).
    m_tools = {
        new RectSelectTool(this),   new MoveSelectedTool(this),
        new LassoSelectTool(this),  new MoveSelectionTool(this),
        new EllipseSelectTool(this), new ZoomTool(this),
        new MagicWandTool(this),    new PanTool(this),
        new PaintBucketTool(this),  new GradientTool(this),
        new BrushTool(this),        new EraserTool(this),
        new PencilTool(this),       new ColorPickerTool(this),
        new CloneStampTool(this),   new RecolorTool(this),
        new TextTool(this),         new LineTool(this),
        new ShapesTool(this),
    };
    for (Tool* t : m_tools) {
        if (auto* picker = qobject_cast<ColorPickerTool*>(t)) {
            connect(picker, &ColorPickerTool::switchBackRequested, this, &ToolManager::selectPrevious);
            connect(picker, &ColorPickerTool::switchToPencilRequested, this, [this] { select(ToolId::Pencil); });
        }
        if (auto* selector = qobject_cast<SelectToolBase*>(t)) {
            connect(selector, &SelectToolBase::selectionFinished, this, [this, selector] {
                const AfterSelect after = AppState::instance()->tools().afterSelect;
                if (after == AfterSelect::DoNotSwitch)
                    return;
                QTimer::singleShot(0, this, [this, selector, after] {
                    if (m_current == selector)
                        select(after == AfterSelect::MovePixels ? ToolId::MoveSelected : ToolId::MoveSelection);
                });
            });
        }
    }
    // Calidad de selección para recortar con la selección: la de Mover selección mientras se usa.
    auto syncQuality = [this] {
        const ToolSettings& s = AppState::instance()->tools();
        Raster::setSelectionAntialias(m_current && m_current->id() == ToolId::MoveSelection ? s.moveSelectionAntialias
                                                                                            : s.selectionAntialias);
    };
    connect(AppState::instance(), &AppState::toolSettingsChanged, this, syncQuality);
    connect(this, &ToolManager::toolChanged, this, syncQuality);
    syncQuality();
}

Tool* ToolManager::tool(ToolId id) const
{
    for (Tool* t : m_tools)
        if (t->id() == id)
            return t;
    return nullptr;
}

void ToolManager::select(ToolId id)
{
    Tool* t = tool(id);
    if (!t || t == m_current)
        return;
    m_previous = m_current;
    m_current = t;
    m_canvas->setTool(t);
    emit toolChanged(t);
}

void ToolManager::selectPrevious()
{
    if (m_previous)
        select(m_previous->id());
}

void ToolManager::activateShortcut(const QKeySequence& key, bool reverse)
{
    QList<Tool*> group;
    for (Tool* t : m_tools)
        if (t->shortcut() == key)
            group.append(t);
    if (group.isEmpty())
        return;
    const int n = int(group.size());
    const int index = int(group.indexOf(m_current));
    const int next = index < 0 ? (reverse ? n - 1 : 0) : (index + (reverse ? n - 1 : 1)) % n;
    select(group.at(next)->id());
}

QList<QKeySequence> ToolManager::shortcuts() const
{
    QList<QKeySequence> keys;
    for (Tool* t : m_tools)
        if (!keys.contains(t->shortcut()))
            keys.append(t->shortcut());
    return keys;
}
