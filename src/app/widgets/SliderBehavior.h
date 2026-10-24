#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

class QSlider;

// Deslizadores (QSlider) como los de Paint.NET: un clic en la barra cerca del tirador lo lleva
// hasta el puntero; más lejos avanza un paso (una décima parte del recorrido) hacia él, y
// manteniendo pulsado sigue a pasos hasta llegar. Si además se mueve el ratón, el tirador sigue
// al puntero. Mientras se pulsa, el tirador cambia de color (propiedad "kaintPressed", que usa
// la hoja de estilo del tema).
class SliderBehavior : public QObject {
    Q_OBJECT
public:
    // Para todos los deslizadores de la aplicación (una sola vez).
    static void install();

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit SliderBehavior(QObject* parent);
    static void setPressed(QSlider* slider, bool pressed);
    static int valueAt(QSlider* slider, QPoint pos);
    static int stepFor(const QSlider* slider);
    // Un paso hacia 'pos'; verdadero si ya llegó.
    static bool stepToward(QSlider* slider, QPoint pos);

    QPointer<QSlider> m_slider;
    QPoint m_pressPos;
    QPoint m_lastPos;
    bool m_onGroove = false;
    bool m_following = false;
    QTimer m_repeat;
};
