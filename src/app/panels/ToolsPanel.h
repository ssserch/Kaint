#pragma once

#include "app/Workspace.h"

#include <QWidget>

class QButtonGroup;
class QToolButton;
class ToolManager;
class Tool;

// Ventana Herramientas: rejilla de iconos como en Paint.NET.
class ToolsPanel : public QWidget, public PanelContent {
    Q_OBJECT
public:
    explicit ToolsPanel(ToolManager* manager, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    QSize fitSize(QSize proposed, Qt::Orientations changed) const override;
    int columnsFor(int width) const;
    int columns() const { return columnsFor(width()); }
    void setIconSize(int size);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void onToolChanged(Tool* tool);
    QSize cell() const;
    QSize sizeFor(int columns) const;
    void layoutButtons();

    ToolManager* m_manager;
    QButtonGroup* m_group;
    QList<QToolButton*> m_buttons;
    int m_columns = 2;
};
