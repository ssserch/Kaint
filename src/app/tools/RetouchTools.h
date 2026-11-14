#pragma once

// Las herramientas de PhotoCraft: Lazo magnético, Selección de objetos, Quitar, Parche, Pincel
// corrector y Pincel corrector puntual. Los algoritmos (core/retouch) son los de PhotoCraft
// (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT). Solo se ven con las funciones
// de PhotoCraft activadas (Configuración > Complementos) y llevan la pieza de puzle en el icono.

#include "app/tools/PaintTools.h"
#include "app/tools/SelectionTools.h"
#include "core/retouch/Retouch.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QTimer>

#include <deque>
#include <functional>
#include <memory>
#include <thread>

// Un cálculo en segundo plano: 'work' corre en otro hilo (puede lanzar retouch::Cancelled) y
// 'done' en el de la interfaz, con true si terminó. Mientras dura, el cursor de espera.
class RetouchJob : public QObject {
    Q_OBJECT
public:
    using Work = std::function<void(const retouch::Stop&)>;
    using Done = std::function<void(bool finished)>;
    explicit RetouchJob(QObject* parent = nullptr) : QObject(parent) {}
    ~RetouchJob() override;
    void start(Work work, Done done);
    bool running() const { return m_state != nullptr; }
    // Lo para y espera, sin llamar a 'done'.
    void cancel();
    // Espera a que termine y llama a 'done' ya.
    void wait();

signals:
    void runningChanged(bool running);

private:
    struct State;
    void deliver(const std::shared_ptr<State>& s);
    std::shared_ptr<State> m_state;
};

// Píxeles RGBA directos de 8 bits (R, G, B, A) de una zona de la capa activa o de la imagen.
retouch::Bytes retouchPixels(Document* doc, bool image, const QRect& r);
QImage retouchImage(const retouch::Bytes& rgba, QSize size);

// ---------------------------------------------------------------------------------------------
// Pinceles correctores: el trazo es el del pincel y al soltar se cura en segundo plano.

class HealStrokeTool : public StrokeTool {
    Q_OBJECT
public:
    explicit HealStrokeTool(QObject* parent = nullptr);
    bool isExtra() const override { return true; }
    // Mientras se cura el trazo anterior no empieza otro.
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    void finish() override;
    bool keyPress(QKeyEvent* e) override;
    QCursor cursor() const override;

protected:
    // El trabajo de la curación sobre el trazo terminado (la cobertura completa): devuelve el
    // rectángulo y una función que calcula los píxeles curados (formato de capa); nullopt si no
    // hay nada que curar.
    struct Plan {
        QRect rect;
        std::function<QImage(const retouch::Stop&)> run;
    };
    virtual std::optional<Plan> plan() = 0;
    void release(bool wait);
    void land(const QRect& rect, const QImage& healed);

    RetouchJob* m_job;
    std::shared_ptr<QImage> m_result;
};

class HealingBrushTool : public HealStrokeTool {
    Q_OBJECT
public:
    using HealStrokeTool::HealStrokeTool;
    ToolId id() const override { return ToolId::HealingBrush; }
    QString name() const override { return tr("Pincel corrector"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_J); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptBlend
             | OptHealAligned | OptRetouchSampling | OptSelectionQuality;
    }
    void mouseMove(const ToolEvent& e) override;
    void paintOverlay(QPainter& p) override;

protected:
    bool canStart(const ToolEvent& e) override;
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("healingbrush"); }
    std::optional<Plan> plan() override;

private:
    bool m_hasSource = false;
    bool m_hasOffset = false;
    QPointF m_source;
    QPoint m_offset;
    QPointF m_hover;
    // Con «Tomar de la imagen», la imagen al empezar el trazo.
    QImage m_sample;
};

class SpotHealingTool : public HealStrokeTool {
    Q_OBJECT
public:
    using HealStrokeTool::HealStrokeTool;
    ToolId id() const override { return ToolId::SpotHealing; }
    QString name() const override { return tr("Pincel corrector puntual"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_J); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptSpotType
             | OptRetouchSampling | OptSelectionQuality;
    }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void paintOverlay(QPainter& p) override;

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("spothealing"); }
    std::optional<Plan> plan() override;
};

// ---------------------------------------------------------------------------------------------
// Quitar: pinta encima (o rodea) de algo y se rellena desde lo de alrededor.

class RemoveTool : public StrokeTool {
    Q_OBJECT
public:
    explicit RemoveTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Remove; }
    QString name() const override { return tr("Quitar"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_J); }
    quint64 options() const override
    {
        return OptBrushSize | OptHardness | OptSpacing | OptSmoothing | OptAntialias | OptPressure | OptRemoveEach
             | OptRetouchSampling | OptFinish;
    }
    bool isExtra() const override { return true; }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void finish() override;
    void deactivate() override;
    bool isBusy() const override { return m_engine.isActive() || !m_pending.isNull() || m_job->running(); }
    QCursor cursor() const override;

protected:
    StrokeParams params(const ToolEvent& e) const override;
    QString historyIcon() const override { return QStringLiteral("remove"); }

private:
    void collect();
    void run(bool wait);
    void drop();

    RetouchJob* m_job;
    // Los trazos reunidos (sin «Quitar después de cada trazo»): cobertura del tamaño de la imagen.
    QImage m_pending;
    QRect m_pendingRect;
    Document* m_pendingDoc = nullptr;
};

// ---------------------------------------------------------------------------------------------
// Parche: se selecciona como con el lazo y se arrastra la selección.

class PatchTool : public LassoSelectTool {
    Q_OBJECT
public:
    explicit PatchTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::Patch; }
    QString name() const override { return tr("Parche"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_J); }
    quint64 options() const override { return OptSelectionMode | OptPatch | OptRetouchSampling | OptSelectionQuality; }
    bool isExtra() const override { return true; }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void finish() override;
    bool isBusy() const override { return m_dragging || m_job->running(); }
    QCursor cursor() const override;

private:
    QPoint offset() const;
    void updatePreview();
    void cancelDrag();

    RetouchJob* m_job;
    bool m_dragging = false;
    QPointF m_grab;
    QPointF m_pointer;
    QImage m_preview;
    QRect m_previewRect;
    QTimer m_previewTimer;
};

// ---------------------------------------------------------------------------------------------
// Lazo magnético: un borde de selección que se pega a los bordes de la imagen.

class MagneticLassoTool : public SelectToolBase {
    Q_OBJECT
public:
    explicit MagneticLassoTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::MagneticLasso; }
    QString name() const override { return tr("Lazo magnético"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }
    quint64 options() const override { return OptSelectionMode | OptMagnetic | OptAfterSelect | OptSelectionQuality; }
    bool isExtra() const override { return true; }
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    void finish() override;
    void deactivate() override;
    bool isBusy() const override { return active(); }
    void beforeHistoryJump() override { cancel(); }

private:
    using P = retouch::PointF;
    bool active() const { return !m_path.empty(); }
    retouch::MagneticSettings settings() const;
    double spacing() const;
    P inside(P p) const;
    P snap(P p);
    std::vector<P> trace(P from, P to, const std::vector<P>& guide);
    void queue(P p);
    void catchUp(size_t budget);
    void follow(P q, bool fasten);
    void extendTrail(P q);
    void fastenLive();
    void endFreehand();
    bool nearStart(P p) const;
    void close(bool straight, bool atStart);
    void commitBorder();
    void cancel();
    void removeLastPoint();
    QPolygonF polygon() const;
    void show();

    retouch::MagneticTracer m_tracer;
    QImage m_tracerImage;
    const Layer* m_tracerLayer = nullptr;
    std::vector<P> m_path;
    std::vector<size_t> m_anchors;
    std::vector<P> m_live;
    std::vector<P> m_trail;
    std::deque<P> m_pending;
    P m_pointer;
    bool m_pressed = false;
    P m_pressPoint;
    bool m_altAtPress = false;
    // El botón pulsado se alejó más que un clic (al soltarlo se cierra el borde).
    bool m_pressDragged = false;
    bool m_opening = false;
    bool m_freehand = false;
    bool m_hasLastPress = false;
    P m_lastPressPoint;
    QElapsedTimer m_lastPress;
    QTimer m_frame;
};

// ---------------------------------------------------------------------------------------------
// Selección de objetos: un recuadro alrededor de un objeto, o el objeto bajo el puntero.

class ObjectSelectionTool : public SelectToolBase {
    Q_OBJECT
public:
    explicit ObjectSelectionTool(QObject* parent = nullptr);
    ToolId id() const override { return ToolId::ObjectSelection; }
    QString name() const override { return tr("Selección de objetos"); }
    QIcon icon() const override;
    QString hint() const override;
    QKeySequence shortcut() const override { return QKeySequence(Qt::Key_S); }
    quint64 options() const override
    {
        return OptSelectionMode | OptObjectFinder | OptRetouchSampling | OptAfterSelect | OptSelectionQuality;
    }
    bool isExtra() const override { return true; }
    void activate(Canvas* canvas) override;
    void deactivate() override;
    void mousePress(const ToolEvent& e) override;
    void mouseMove(const ToolEvent& e) override;
    void mouseRelease(const ToolEvent& e) override;
    bool keyPress(QKeyEvent* e) override;
    void paintOverlay(QPainter& p) override;
    bool isBusy() const override { return m_pressed || m_job->running(); }
    QCursor cursor() const override;
    // La acción Seleccionar sujeto de la barra.
    void selectSubject();

private:
    retouch::Sampler sampler(bool* ok) const;
    void lookupHover();
    void apply(const retouch::Region& region, SelectionMode mode, const Selection& before);

    RetouchJob* m_job;
    RetouchJob* m_finder;
    bool m_pressed = false;
    bool m_boxDragged = false;
    QPointF m_start;
    QPointF m_end;
    SelectionMode m_pressMode = SelectionMode::Replace;
    QTimer m_hoverTimer;
    QPointF m_hover;
    bool m_hoverInside = false;
    // El objeto bajo el puntero (relleno azul al 50 % y su contorno).
    std::optional<retouch::Region> m_found;
    QPointF m_foundAt;
    QImage m_foundOverlay;
    QPainterPath m_foundOutline;
    quint64 m_contentStamp = 0;
};
