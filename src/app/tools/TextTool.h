#pragma once

#include "app/tools/Tool.h"
#include "core/LayerEdit.h"

#include <QStringList>
#include <QTimer>

class QFontMetricsF;

// Texto editable hasta que se termina (Esc, botón Terminar, clic fuera del texto o cambio de
// herramienta), como en Paint.NET: cursor con flechas, Inicio/Fin, Ctrl para saltar palabras,
// clic dentro del texto para mover el cursor y el tirador (o el botón derecho) para mover el
// texto.
class TextTool : public Tool {
    Q_OBJECT
public:
    explicit TextTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Text; }
    QString name() const override { return tr("Texto"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_T); }
    quint32 options() const override { return OptText | OptAntialias | OptBlend | OptSelectionQuality | OptFinish; }
    QCursor cursor() const override { return Qt::IBeamCursor; }

    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void inputMethod(QInputMethodEvent* e) override;
    bool wantsKeyboard() const override { return m_edit.isActive(); }
    QRectF inputCursorRect() const override;
    void paintOverlay(QPainter& p) override;
    void deactivate() override;
    void finish() override;
    bool isBusy() const override { return m_edit.isActive(); }

    // Inserta texto en la posición del cursor (Pegar mientras se escribe).
    void insertText(const QString& text);

private:
    QFont font() const;
    void render();
    void touch();
    void setInputMethodEnabled(bool on);
    QStringList displayLines() const;
    qreal lineLeft(const QFontMetricsF& fm, const QString& line) const;
    QRectF caretRect() const;
    QPoint positionAt(const QPointF& imagePos) const;
    QPointF moveIcon() const;
    void ensureCaretVisible();

    LayerEdit m_edit;
    QPointF m_pos;
    QStringList m_lines;
    int m_line = 0;
    int m_col = 0;
    QString m_preedit;
    QRectF m_bounds;
    QTimer m_blink;
    bool m_caretOn = true;
    bool m_dragging = false;
    QPointF m_grab;
};
