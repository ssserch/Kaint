#pragma once

#include "app/tools/EditableSteps.h"
#include "app/tools/Tool.h"
#include "app/tools/TransformBox.h"
#include "core/LayerEdit.h"

#include <QColor>
#include <array>
#include <functional>

// Dibuja una geometría vectorial dos veces (con colores y como cobertura) y la compone en la
// capa; así funcionan las tramas y "Sobrescribir".
using VectorDrawFn = std::function<void(QPainter& p, bool coverage)>;
void renderVector(LayerEdit& edit, const QRect& region, const VectorDrawFn& draw);

// Línea/Curva: tras dibujarla quedan cuatro nodos editables.
class LineTool : public Tool {
    Q_OBJECT
public:
    explicit LineTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Line; }
    QString name() const override { return tr("Línea/Curva"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_O); }
    quint64 options() const override
    {
        return OptBrushSize | OptCurve | OptDash | OptLineCaps | OptAntialias | OptBlend | OptFill | OptSelectionQuality
             | OptFinish;
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
    enum class Drag { None, Create, Point, Move, Rotate };
    QPainterPath curve() const;
    void render();
    QPointF moveIcon() const;
    void setInitialNodes(const QPointF& a, const QPointF& b);
    void commitStep();

    LayerEdit m_edit;
    StepRecorder m_steps{ToolId::Line, QStringLiteral("draw-line")};
    bool m_restoring = false;
    std::array<QPointF, 4> m_pts;
    std::array<QPointF, 4> m_ptsStart;
    bool m_secondary = false;
    Drag m_drag = Drag::None;
    int m_dragIndex = -1;
    QPointF m_grab;
    QPointF m_anchor;
};

// Formas: 29 formas editables con el cuadro de transformación hasta terminar.
class ShapesTool : public Tool {
    Q_OBJECT
public:
    explicit ShapesTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Shapes; }
    QString name() const override { return tr("Formas"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_O); }
    quint64 options() const override
    {
        return OptShape | OptShapeStyle | OptBrushSize | OptCornerRadius | OptDash | OptAntialias | OptBlend | OptFill
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
    void beforeHistoryJump() override;
    void historyChanged() override;

private:
    void render();
    QPointF moveIcon() const;
    void commitStep();

    LayerEdit m_edit;
    StepRecorder m_steps{ToolId::Shapes, QStringLiteral("shapes")};
    bool m_restoring = false;
    TransformBox m_box;
    QPointF m_start;
    bool m_creating = false;
    bool m_secondary = false;
};
