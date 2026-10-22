#pragma once

#include <QCursor>
#include <QIcon>
#include <QKeySequence>
#include <QObject>
#include <QPointF>

class Canvas;
class Document;
class QPainter;
class QKeyEvent;
class QInputMethodEvent;

enum class ToolId {
    RectSelect,
    MoveSelected,
    LassoSelect,
    MoveSelection,
    EllipseSelect,
    Zoom,
    MagicWand,
    Pan,
    PaintBucket,
    Gradient,
    Paintbrush,
    Eraser,
    Pencil,
    ColorPicker,
    CloneStamp,
    Recolor,
    Text,
    Line,
    Shapes,
};

struct ToolEvent {
    QPointF pos;
    QPointF viewPos;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers modifiers;
    qreal pressure = 1.0;

    QPoint pixel() const;
};

class Tool : public QObject {
    Q_OBJECT
public:
    enum Option : quint32 {
        OptBrushSize = 1u << 0,
        OptHardness = 1u << 1,
        OptAntialias = 1u << 2,
        OptBlend = 1u << 3,
        OptFill = 1u << 4,
        OptTolerance = 1u << 5,
        OptFloodMode = 1u << 6,
        OptSampling = 1u << 7,
        OptSelectionMode = 1u << 8,
        OptGradient = 1u << 9,
        OptShape = 1u << 10,
        OptShapeStyle = 1u << 11,
        OptText = 1u << 12,
        OptDash = 1u << 13,
        OptPicker = 1u << 14,
        OptFinish = 1u << 15,
        OptPressure = 1u << 16,
        OptSpacing = 1u << 17,
        OptSmoothing = 1u << 18,
        OptSelectionQuality = 1u << 19,
        OptToleranceAlpha = 1u << 20,
        OptRectMode = 1u << 21,
        OptGradientRepeat = 1u << 22,
        OptCurve = 1u << 23,
        OptCornerRadius = 1u << 24,
        OptInterpolation = 1u << 25,
        OptRecolorSampling = 1u << 26,
        OptLineCaps = 1u << 27,
        OptSampleSize = 1u << 28,
        OptAfterSelect = 1u << 29,
        OptGamma = 1u << 30,
    };

    explicit Tool(QObject* parent = nullptr) : QObject(parent) {}

    virtual ToolId id() const = 0;
    virtual QString name() const = 0;
    virtual QIcon icon() const = 0;
    virtual QString hint() const = 0;
    virtual QKeySequence shortcut() const = 0;
    virtual quint32 options() const { return 0; }

    virtual void activate(Canvas* canvas) { m_canvas = canvas; }
    virtual void deactivate() { m_canvas = nullptr; }
    Canvas* canvas() const { return m_canvas; }
    Document* document() const;

    virtual void mousePress(const ToolEvent&) {}
    virtual void mouseMove(const ToolEvent&) {}
    virtual void mouseRelease(const ToolEvent&) {}
    virtual void extraButton(const ToolEvent&, bool /*pressed*/) {}
    virtual bool keyPress(QKeyEvent*) { return false; }
    virtual void inputMethod(QInputMethodEvent*) {}
    virtual bool wantsKeyboard() const { return false; }
    virtual QRectF inputCursorRect() const { return {}; }

    // Dibujo de ayudas sobre el lienzo, en coordenadas del widget.
    virtual void paintOverlay(QPainter&) {}
    virtual QCursor cursor() const { return Qt::CrossCursor; }
    virtual qreal brushOutlineDiameter() const { return 0; }

    // Tiñe de azul el interior de la selección (herramientas de selección).
    virtual bool highlightSelection() const { return false; }

    // Termina la operación en curso (botón "Terminar", Intro, cambio de herramienta...).
    virtual void finish() {}
    virtual bool isBusy() const { return false; }

signals:
    void hintChanged(const QString& hint);

protected:
    void repaint() const;

    Canvas* m_canvas = nullptr;
};
