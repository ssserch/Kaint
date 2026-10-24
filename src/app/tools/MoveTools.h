#pragma once

#include "app/tools/Tool.h"
#include "app/tools/TransformBox.h"
#include "core/Commands.h"

// Mover píxeles seleccionados: arrastrar mueve, los tiradores escalan (Mayús = proporcional,
// Alt = desde el centro), fuera del cuadro o con el botón derecho se gira (Mayús = de 15 en
// 15°), Ctrl mueve una copia y las flechas desplazan 1 px (Ctrl: 10 px).
class MoveSelectedTool : public Tool {
    Q_OBJECT
public:
    explicit MoveSelectedTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::MoveSelected; }
    QString name() const override { return tr("Mover píxeles seleccionados"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_M); }
    quint64 options() const override { return OptInterpolation | OptGamma | OptSelectionQuality | OptFinish; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    bool hidesSelectionOutline() const override;
    void finish() override;
    void deactivate() override;

    // Píxeles flotantes de la última operación, si se puede continuar con ellos.
    static FloatingPtr currentFloating(Document* doc);
    static void startPaste(Document* doc, const QImage& image, QPoint pos, const QString& text);

private:
    FloatingPtr lift(bool keepCopy, LayerPtr layer = nullptr);
    void restoreUnmoved();
    void endMove();
    void applySampling();
    void boxForCurrentState(TransformBox& box) const;
    void syncBox();
    void applyLive(const QTransform& t);
    void push();
    void nudge(QPoint d);

    bool m_active = false;
    bool m_first = false;
    FloatingPtr m_float;
    // Las otras capas que se mueven a la vez (Ctrl+clic en Capas).
    QList<FloatingPtr> m_extra;
    TransformBox m_box;
    QTransform m_startTransform;
    QTransform m_current;
    Selection m_selectionBefore;
    bool m_pivotDrag = false;
    Selection m_pivotFor;
};

class MoveSelectionTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    ToolId id() const override { return ToolId::MoveSelection; }
    QString name() const override { return tr("Mover selección"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_M); }
    QCursor cursor() const override { return Qt::SizeAllCursor; }
    bool highlightSelection() const override { return true; }
    quint64 options() const override { return OptMoveSelectionQuality | OptFinish; }

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void finish() override;
    void deactivate() override;

private:
    bool continuing() const;
    void push();
    void syncBox();

    bool m_active = false;
    TransformBox m_box;
    Selection m_base;
    Selection m_result;
    QTransform m_transform;
    Selection m_before;
    bool m_pivotDrag = false;
    Selection m_pivotFor;
};

// Cursor de giro y cursor según la zona del cuadro bajo el puntero.
QCursor transformCursor(TransformBox::Mode mode);
