#pragma once

#include "app/tools/Tool.h"

class ZoomTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Zoom; }
    QString name() const override { return tr("Zum"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_Z); }
    QCursor cursor() const override;

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    void paintOverlay(QPainter& p) override;

private:
    bool m_pressed = false;
    bool m_dragging = false;
    QPointF m_start;
    QPointF m_current;
};

class PanTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::Pan; }
    QString name() const override { return tr("Mano"); }
    QIcon icon() const override;
    QString hint() const override { return tr("Arrastra para desplazar la vista. También: botón central o barra espaciadora."); }
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_H); }
    QCursor cursor() const override { return m_dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor; }

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;

private:
    bool m_dragging = false;
    QPointF m_last;
};

class ColorPickerTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::ColorPicker; }
    QString name() const override { return tr("Selector de color"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_K); }
    quint64 options() const override { return OptSampleSize | OptPicker | OptSelectionQuality; }
    QCursor cursor() const override;

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;

signals:
    void switchBackRequested();
    void switchToPencilRequested();

private:
    void pick(const ToolEvent& e, Qt::MouseButton button);
    Qt::MouseButton m_button = Qt::NoButton;
};
