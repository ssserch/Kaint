#pragma once

#include <QWidget>

// Deslizador con aspecto de barra de progreso y el valor escrito encima, como los de la barra
// de opciones de Paint.NET 5 (Dureza, Tolerancia...).
class BarSlider : public QWidget {
    Q_OBJECT
public:
    explicit BarSlider(int min, int max, const QString& suffix = QStringLiteral("%"), QWidget* parent = nullptr);

    int value() const { return m_value; }
    void setValue(int value);

    // La rueda del ratón cambia el valor (Configuración → Interfaz de usuario): si no, la rueda pasa
    // a lo que hay debajo (la barra o la ventana se desplazan). 'step': 0, el 1 % del recorrido;
    // 1, una unidad; 2, el 5 %; 3, el 10 %. Con Mayús, de uno en uno; con Ctrl, diez veces más.
    // Vale también para los deslizadores de los efectos (SliderBehavior).
    static void setWheelBehavior(bool enabled, int step);
    static bool wheelEnabled();
    static int wheelStep(int min, int max, Qt::KeyboardModifiers modifiers);

    QSize sizeHint() const override { return {130, 24}; }
    QSize minimumSizeHint() const override { return {60, 20}; }

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void setFromX(qreal x);

    int m_min;
    int m_max;
    int m_value;
    QString m_suffix;
    // Lo que queda de las ruedas de alta resolución (menos de un paso).
    int m_wheelRest = 0;
};
