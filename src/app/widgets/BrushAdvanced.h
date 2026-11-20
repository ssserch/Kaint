#pragma once

#include <QToolButton>
#include <QWidget>

class QSlider;
class QLabel;

// Pincel avanzado (Configuración → Herramientas → Opciones avanzadas del pincel), como en Ibis
// Paint y FireAlpaca. Con el interruptor, la barra de opciones del pincel, la goma y el tampón
// muestra «Avanzado…» con estas opciones, y al lado del lienzo pueden ir los deslizadores
// verticales de tamaño y opacidad (para dibujar con el dedo o el lápiz).

// El panel con las opciones (punta, presión, variación, estabilizador, entrada y salida).
class AdvancedBrushPanel : public QWidget {
    Q_OBJECT
public:
    explicit AdvancedBrushPanel(QWidget* parent = nullptr);
    void sync();

private:
    bool m_syncing = false;
    QList<std::function<void()>> m_syncers;
};

// El botón «Avanzado…» de la barra de opciones: abre el panel en un menú.
class AdvancedBrushButton : public QToolButton {
    Q_OBJECT
public:
    explicit AdvancedBrushButton(QWidget* parent = nullptr);
};

// Dos deslizadores verticales (tamaño y opacidad del pincel) sobre el borde izquierdo del lienzo,
// como los de Ibis Paint. Se ven con el pincel avanzado y la opción activada.
class BrushSideSliders : public QWidget {
    Q_OBJECT
public:
    explicit BrushSideSliders(QWidget* parent = nullptr);
    // Se coloca junto al borde izquierdo de 'area' (en coordenadas del padre).
    void place(const QRect& area);
    void sync();

private:
    QSlider* m_size = nullptr;
    QSlider* m_opacity = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_opacityLabel = nullptr;
    bool m_syncing = false;
};
