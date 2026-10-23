#pragma once

#include "app/tools/Tool.h"
#include "core/Stroke.h"

// Base de las herramientas que pintan con trazos (pincel, goma, lápiz, tampón de clonar y
// recolorear).
class StrokeTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    void deactivate() override;
    void finish() override;
    bool isBusy() const override { return m_engine.isActive(); }
    qreal brushOutlineDiameter() const override;

protected:
    virtual StrokeParams params(const ToolEvent& e) const = 0;
    virtual QString historyIcon() const = 0;
    virtual bool canStart(const ToolEvent&) { return true; }
    StrokeParams baseParams() const;

    StrokeEngine m_engine;
};

class BrushTool : public StrokeTool {
    Q_OBJECT
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Paintbrush; }
    QString name() const override { return tr("Pincel"); }
    QIcon icon() const override;
    QString hint() const override { return tr("Clic izquierdo: pintar con el color principal · clic derecho: con el secundario."); }
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_B); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptBlend | OptFill
             | OptSelectionQuality;
    }

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("draw-brush"); }
};

class EraserTool : public StrokeTool {
    Q_OBJECT
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Eraser; }
    QString name() const override { return tr("Goma de borrar"); }
    QIcon icon() const override;
    QString hint() const override
    {
        return tr("Arrastra para borrar. La opacidad del color principal (clic derecho: secundario) regula la fuerza.");
    }
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_E); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptSelectionQuality;
    }

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("draw-eraser"); }
};

class PencilTool : public StrokeTool {
    Q_OBJECT
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Pencil; }
    QString name() const override { return tr("Lápiz"); }
    QIcon icon() const override;
    QString hint() const override
    {
        return tr("Dibuja píxeles sueltos con el color principal (clic derecho: secundario), ideal para pixel art.");
    }
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_P); }
    quint64 options() const override { return OptBlend | OptSelectionQuality; }
    qreal brushOutlineDiameter() const override { return 0; }

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("draw-freehand"); }
};

class CloneStampTool : public StrokeTool {
    Q_OBJECT
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::CloneStamp; }
    QString name() const override { return tr("Tampón de clonar"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_L); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptBlend
             | OptSelectionQuality;
    }
    QCursor cursor() const override;
    void mouseMove(const ToolEvent& e) override;
    void paintOverlay(QPainter& p) override;

protected:
    bool canStart(const ToolEvent& e) override;
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("edit-clone"); }

private:
    bool m_hasSource = false;
    bool m_hasOffset = false;
    std::weak_ptr<Layer> m_sourceLayer;
    const Document* m_sourceDoc = nullptr;
    QPointF m_source;
    QPoint m_offset;
    QPointF m_hover;
};

class RecolorTool : public StrokeTool {
    Q_OBJECT
public:
    using StrokeTool::StrokeTool;
    ToolId id() const override { return ToolId::Recolor; }
    QString name() const override { return tr("Volver a colorear"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_R); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptTolerance
             | OptToleranceAlpha | OptRecolorSampling | OptSelectionQuality;
    }

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("color-mode-hue-shift-positive"); }
};
