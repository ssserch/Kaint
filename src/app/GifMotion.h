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
    void setProp(Motion::Prop prop);
    Motion::Prop prop() const { return m_prop; }
    QSize sizeHint() const override { return {260, 150}; }

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QRectF plotRect() const;
    void range(double* lo, double* hi) const;
    QPointF pointFor(int t, double v, double lo, double hi) const;
    int keyAt(QPointF pos) const;

    GifEditor* m_editor;
    Motion::Prop m_prop = Motion::PosX;
    int m_drag = -1;  // índice del punto que se arrastra
    int m_dragT = 0;
    double m_dragV = 0;
    double m_dragLo = 0, m_dragHi = 1;
    // La animación de antes de arrastrar y cómo queda con el punto donde está el puntero.
    Motion::Clip m_dragBase;
    Motion::Clip dragged() const;
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
