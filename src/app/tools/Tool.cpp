#include "app/tools/Tool.h"

#include "app/Canvas.h"
#include "app/Icons.h"

#include <QHash>
#include <QSet>

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

QCursor Tool::cursor() const
{
    static const QHash<int, QString> names{
        {int(ToolId::RectSelect), QStringLiteral("rectangleselect")},
        {int(ToolId::MoveSelected), QStringLiteral("moveselectedpixels")},
        {int(ToolId::LassoSelect), QStringLiteral("lassoselect")},
        {int(ToolId::MoveSelection), QStringLiteral("moveselection")},
        {int(ToolId::EllipseSelect), QStringLiteral("ellipseselect")},
        {int(ToolId::Zoom), QStringLiteral("zoom")},
        {int(ToolId::MagicWand), QStringLiteral("magicwand")},
        {int(ToolId::PaintBucket), QStringLiteral("paintbucket")},
        {int(ToolId::Gradient), QStringLiteral("gradient")},
        {int(ToolId::Paintbrush), QStringLiteral("paintbrush")},
        {int(ToolId::Eraser), QStringLiteral("eraser")},
        {int(ToolId::Pencil), QStringLiteral("pencil")},
        {int(ToolId::ColorPicker), QStringLiteral("colorpicker")},
        {int(ToolId::CloneStamp), QStringLiteral("clonestamp")},
        {int(ToolId::Recolor), QStringLiteral("recolor")},
        {int(ToolId::Line), QStringLiteral("linecurve")},
        {int(ToolId::Shapes), QStringLiteral("shapes")},
    };
    // Como en Paint.NET, con estas se sigue viendo el puntero y el icono va a su lado en miniatura.
    static const QSet<int> mini{int(ToolId::RectSelect), int(ToolId::LassoSelect), int(ToolId::EllipseSelect),
                                int(ToolId::Gradient), int(ToolId::Line)};
    const auto it = names.constFind(int(id()));
    if (it == names.cend())
        return QCursor(Qt::CrossCursor);
    return mini.contains(int(id())) ? Icons::miniToolCursor(*it) : Icons::toolCursor(*it);
}
