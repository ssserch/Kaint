#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>

class QSlider;

// Deslizadores (QSlider) como los de Paint.NET: un clic delante o detrás del tirador lo hace
// avanzar o retroceder hasta soltar, pero si además se mueve el ratón el tirador pasa a seguir
// al puntero al momento. Mientras se pulsa, el tirador cambia de color (propiedad
// "kaintPressed", que usa la hoja de estilo del tema).
class SliderBehavior : public QObject {
    Q_OBJECT
public:
    // Para todos los deslizadores de la aplicación (una sola vez).
    static void install();

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    using QObject::QObject;
    static void setPressed(QSlider* slider, bool pressed);
    static int valueAt(QSlider* slider, QPoint pos);

    QPointer<QSlider> m_slider;
    QPoint m_pressPos;
    bool m_onGroove = false;
    bool m_following = false;
    bool m_synthetic = false;
};
