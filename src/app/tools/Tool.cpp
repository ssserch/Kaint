#include "app/tools/Tool.h"

#include "app/Canvas.h"

#include <cmath>

QPoint ToolEvent::pixel() const
{
    return QPoint(int(std::floor(pos.x())), int(std::floor(pos.y())));
}

Document* Tool::document() const
{
    return m_canvas ? m_canvas->document() : nullptr;
}

void Tool::repaint() const
{
    if (m_canvas)
        m_canvas->updateOverlay();
}
