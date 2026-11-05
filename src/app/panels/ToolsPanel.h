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
    QVariant layoutState() const override { return m_columns; }
    QSize sizeForLayout(const QVariant& state) const override;
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
    // Orden y herramientas ocultas de Configuración > Personalización.
    void applyCustomization();

    ToolManager* m_manager;
    QButtonGroup* m_group;
    // Todos los botones y, en el orden elegido, los que se ven.
    QList<QToolButton*> m_all;
    QList<QToolButton*> m_buttons;
    int m_columns = 2;
};
