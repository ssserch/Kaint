#pragma once

#include "app/tools/Tool.h"
#include "core/Selection.h"

#include <QElapsedTimer>
#include <QPolygonF>

// Base común de las selecciones, con las combinaciones de Paint.NET: Ctrl + izq.: unión · Alt
// + izq.: restar · Ctrl + der.: XOR (invertir) Alt + der.: intersección · sin teclas: el modo
// de la barra.
class SelectToolBase : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    quint32 options() const override { return OptSelectionMode | OptAfterSelect | OptSelectionQuality; }
    bool highlightSelection() const override { return true; }
    void deactivate() override;
    void paintOverlay(QPainter& p) override;

signals:
    void selectionFinished();

protected:
    SelectionMode modeFor(const ToolEvent& e) const;
    void begin(const ToolEvent& e);
    void preview(const Selection& shape);
    void done(const QString& text, const QString& icon, Qt::KeyboardModifiers modifiers);
    bool plainReplace(Qt::KeyboardModifiers modifiers) const;
    void commit(const QString& text, const QString& icon);
    void showStatus(const QRectF& r);
    static QPainterPath clippedToCanvas(const QPainterPath& path, QSize canvas);

    bool m_active = false;
    bool m_dragged = false;
    Selection m_before;
    Selection m_shape;
    SelectionMode m_mode = SelectionMode::Replace;
    QElapsedTimer m_timer;
};

class ShapeSelectTool : public SelectToolBase {
    Q_OBJECT
public:
    using SelectToolBase::SelectToolBase;
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    void extraButton(const ToolEvent& e, bool pressed) override;

protected:
    virtual bool ellipse() const = 0;
    QRectF currentRect(const QPointF& end, Qt::KeyboardModifiers mods) const;
    QPointF m_start;
    QPointF m_last;
    bool m_moveOrigin = false;
    QPointF m_originGrab;
};

class RectSelectTool : public ShapeSelectTool {
    Q_OBJECT
public:
    using ShapeSelectTool::ShapeSelectTool;
    ToolId id() const override { return ToolId::RectSelect; }
    QString name() const override { return tr("Selección rectangular"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }
    quint32 options() const override { return SelectToolBase::options() | OptRectMode; }

protected:
    bool ellipse() const override { return false; }
};

class EllipseSelectTool : public ShapeSelectTool {
    Q_OBJECT
public:
    using ShapeSelectTool::ShapeSelectTool;
    ToolId id() const override { return ToolId::EllipseSelect; }
    QString name() const override { return tr("Selección elíptica"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }

protected:
    bool ellipse() const override { return true; }
};

class LassoSelectTool : public SelectToolBase {
    Q_OBJECT
public:
    using SelectToolBase::SelectToolBase;
    ToolId id() const override { return ToolId::LassoSelect; }
    QString name() const override { return tr("Selección de lazo"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    void extraButton(const ToolEvent& e, bool pressed) override;

private:
    void update();
    QPolygonF m_points;
    bool m_moveOrigin = false;
    QPointF m_last;
};

// Varita mágica editable: hasta terminar se puede cambiar la tolerancia, el modo de inundación
// o el muestreo y arrastrar el punto de origen.
class MagicWandTool : public SelectToolBase {
    Q_OBJECT
public:
    explicit MagicWandTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::MagicWand; }
    QString name() const override { return tr("Varita mágica"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }
    quint32 options() const override
    {
        return OptSelectionMode | OptFloodMode | OptTolerance | OptToleranceAlpha | OptSampling | OptAfterSelect
             | OptSelectionQuality | OptFinish;
    }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void finish() override;
    bool isBusy() const override { return m_session; }
    void deactivate() override
    {
        finish();
        SelectToolBase::deactivate();
    }

private:
    void recompute();
    QPointF seedHandle() const;
    bool m_session = false;
    bool m_draggingSeed = false;
    QPointF m_seedGrab;
    bool m_shiftGlobal = false;
    QPoint m_seed;
    QImage m_sample;
};
