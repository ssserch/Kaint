#pragma once

#include "app/tools/Tool.h"
#include "core/LayerEdit.h"

// Bote de pintura editable: hasta terminar se pueden cambiar la tolerancia, los colores, la
// trama o el modo, y arrastrar el punto de origen.
class PaintBucketTool : public Tool {
    Q_OBJECT
public:
    explicit PaintBucketTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::PaintBucket; }
    QString name() const override { return tr("Cubo de pintura"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_F); }
    quint32 options() const override
    {
        return OptFloodMode | OptTolerance | OptToleranceAlpha | OptSampling | OptBlend | OptFill | OptAntialias
             | OptSelectionQuality | OptFinish;
    }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void deactivate() override;
    void finish() override;
    bool isBusy() const override { return m_edit.isActive(); }

private:
    void render();
    QPointF handlePos() const;

    LayerEdit m_edit;
    QImage m_sample;
    QPoint m_seed;
    bool m_secondary = false;
    bool m_shiftGlobal = false;
    bool m_dragging = false;
};

// Degradado editable con sus dos extremos y el icono de mover.
class GradientTool : public Tool {
    Q_OBJECT
public:
    explicit GradientTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Gradient; }
    QString name() const override { return tr("Degradado"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_G); }
    quint32 options() const override
    {
        return OptGradient | OptGradientRepeat | OptAntialias | OptBlend | OptSelectionQuality | OptFinish;
    }

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void deactivate() override;
    void finish() override;
    bool isBusy() const override { return m_edit.isActive(); }

private:
    enum class Drag { None, Start, End, Both };
    void render();
    QPointF moveHandle() const;

    LayerEdit m_edit;
    QPointF m_start;
    QPointF m_end;
    bool m_reversed = false;
    Drag m_drag = Drag::None;
    QPointF m_grab;
};
