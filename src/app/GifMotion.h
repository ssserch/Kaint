#pragma once

#include "core/Motion.h"

#include <QWidget>

#include <array>
#include <functional>

class GifEditor;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;
class QCheckBox;
class QMenu;
class QGridLayout;
class QPainter;

// Editor de GIF: la animación por puntos clave de un fotograma, como en Filmora. A la derecha de
// la vista previa: los valores (escala, posición, rotación, opacidad) en el cabezal, cada uno con su
// rombo para poner o quitar un punto clave, y debajo la gráfica de cómo cambia el valor elegido.
namespace GifEdit {

// El rombo de un punto clave: blanco sin elegir, del color de acento elegido.
void paintDiamond(QPainter& p, QPointF center, double radius, bool selected, bool filled, const QPalette& pal);
// El menú de la forma de pasar al siguiente punto (como el de Filmora): lineal, curva, curva
// continua, congelar; entrada y salida suaves. 'apply' recibe lo elegido.
void fillInterpMenu(QMenu* menu, Motion::Interp current, bool easeIn, bool easeOut,
                    const std::function<void(Motion::Interp)>& setInterp, const std::function<void(bool in, bool on)>& setEase);

class KeyGraph : public QWidget {
    Q_OBJECT
public:
    KeyGraph(GifEditor* editor, QWidget* parent = nullptr);
    // El valor que se ve; -1: «Posición XY» (X e Y juntas, como en Filmora: lo que se hace en un punto
    // o en un tramo vale para las dos).
    void setProp(int prop);
    QSize sizeHint() const override { return {260, 150}; }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    // Doble clic en un sitio de la gráfica: un punto clave ahí (en ese momento, con ese valor).
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    // Lo que hay bajo el puntero: un punto, un asa de una curva Bézier (0 la de salida, 1 la de
    // llegada) o la línea entre dos puntos (el tramo que empieza en 'key').
    struct Hit {
        enum Kind { None, Key, Handle, Segment } kind = None;
        Motion::Prop prop = Motion::PosX;
        int key = -1;
        int handle = 0;
    };
    QList<Motion::Prop> props() const;
    QRectF plotRect() const;
    Motion::Clip shownClip() const;
    void range(double* lo, double* hi) const;
    QPointF pointFor(double t, double v, double lo, double hi) const;
    int timeAt(double x) const;
    double valueAt(double y, double lo, double hi) const;
    QPointF handlePoint(const Motion::Track& track, int i, int handle, double lo, double hi) const;
    Hit hitAt(QPointF pos) const;
    // La forma del tramo que empieza en 'time' (y quitar el punto), en esos valores.
    void showMenu(int frame, int time, const QList<Motion::Prop>& on, bool removable, QPoint globalPos);

    GifEditor* m_editor;
    int m_prop = -1;
    // Lo que se arrastra: la animación de antes, cómo queda y la escala de la gráfica al pulsar.
    Hit m_drag;
    bool m_dragging = false;
    bool m_moveSegment = false;
    QPointF m_pressPos;
    int m_dragT = 0;
    std::array<double, 4> m_bendStart{};
    Motion::Clip m_dragBase;
    Motion::Clip m_dragResult;
    double m_dragLo = 0, m_dragHi = 1;
};

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(GifEditor* editor, QWidget* parent = nullptr);
    // Todo (al cambiar de fotograma o la animación) o solo los valores (mientras se reproduce).
    void refresh();
    void refreshValues();

private:
    struct Row {
        QDoubleSpinBox* spin = nullptr;
        QToolButton* diamond = nullptr;
        QToolButton* prev = nullptr;
        QToolButton* next = nullptr;
    };
    void addRow(QGridLayout* grid, int row, Motion::Prop prop, const QString& label, const QString& suffix, double min,
                double max, int decimals);
    void valueEdited(Motion::Prop prop, double value);
    void toggleKey(Motion::Prop prop);
    void jump(Motion::Prop prop, int direction);
    QIcon diamondIcon(bool filled) const;

    GifEditor* m_editor;
    QLabel* m_title = nullptr;
    QLabel* m_hint = nullptr;
    std::array<Row, Motion::PropCount> m_rows;
    QCheckBox* m_lock = nullptr;
    QComboBox* m_fps = nullptr;
    QCheckBox* m_smooth = nullptr;
    QComboBox* m_graphProp = nullptr;
    KeyGraph* m_graph = nullptr;
    QToolButton* m_reset = nullptr;
    QToolButton* m_bake = nullptr;
    // El punto clave global (todos los valores a la vez, como el de Filmora).
    QToolButton* m_globalKey = nullptr;
    bool m_updating = false;
};

// El peso aproximado del GIF exportado y lo que dura, con una barrita que se llena hacia los 10 MB
// (el límite de Discord sin Nitro): verde, amarilla y roja cuando ya pesa demasiado.
class SizeMeter : public QWidget {
    Q_OBJECT
public:
    explicit SizeMeter(QWidget* parent = nullptr);
    // 'bytes' < 0: sin calcular todavía.
    void setEstimate(qint64 bytes, int durationMs, int frames);
    qint64 bytes() const { return m_bytes; }
    QSize sizeHint() const override;
    static constexpr qint64 kLimit = 10ll * 1024 * 1024;

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    qint64 m_bytes = -1;
    int m_duration = 0;
    int m_frames = 0;
};

}  // namespace GifEdit
