#pragma once

#include "app/AppState.h"
#include "app/tools/Tool.h"
#include "core/Commands.h"
#include "core/LayerEdit.h"

#include <QColor>

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
    QPointF m_grab;
};

// Degradado editable con sus dos extremos y el icono de mover.
// Extremos y opciones con los que se dibujó un degradado.
struct GradientState {
    QPointF start;
    QPointF end;
    bool reversed = false;
    GradientType type = GradientType::Linear;
    GradientRepeat repeat = GradientRepeat::None;
    bool transparency = false;
    BlendMode blend = BlendMode::Normal;
    QColor primary;
    QColor secondary;
};

// Paso del degradado en el historial ("Degradado", "Mover degradado"), como en Paint.NET: al
// volver a él se puede seguir editando. Guarda la capa de antes del degradado para redibujarlo.
class GradientStepCommand : public PixelPatchCommand, public EditableStep {
public:
    GradientStepCommand(Document* doc, LayerPtr layer, const QImage& before, const QRect& area, const QString& text,
                        const GradientState& state, const QImage& base);
    ToolId tool() const override { return ToolId::Gradient; }
    const LayerPtr& layer() const { return m_layer; }
    const GradientState& state() const { return m_state; }
    const QImage& base() const { return m_base; }
    QRect area() const { return m_area; }
    bool editable() const { return !m_base.isNull(); }
    void releaseMemory() override;

private:
    LayerPtr m_layer;
    GradientState m_state;
    QImage m_base;
    QRect m_area;
};

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
    void beforeHistoryJump() override;
    void historyChanged() override;

private:
    enum class Drag { None, Start, End, Both };
    void render();
    QPointF moveHandle() const;
    GradientState state() const;
    void pushStep();
    void leaveEditing();

    LayerEdit m_edit;
    QPointF m_start;
    QPointF m_end;
    bool m_reversed = false;
    Drag m_drag = Drag::None;
    QPointF m_grab;
    // Pasos ya registrados de esta edición y la capa tal como quedó en el último.
    int m_steps = 0;
    QImage m_stepImage;
    bool m_dirty = false;
    bool m_pushing = false;
    bool m_restoring = false;
};
