#pragma once

#include <QEasingCurve>

#include <functional>

class QObject;
class QVariantAnimation;
class QWidget;

// Animaciones de la interfaz, como las de paint.js; se desactivan todas a la vez en
// Configuración > Interfaz de usuario > Animaciones.
namespace Animations {

void setEnabled(bool on);
bool enabled();

// Duraciones de paint.js (en milisegundos).
constexpr int kPopupFade = 100;  // menús y listas: fadeIn 0.1s ease-out
constexpr int kPanelFade = 180;  // ventanas translúcidas: opacity 180ms ease-out
constexpr int kListMove = 140;   // filas que cambian de sitio: 140ms cubic-bezier(.2,.8,.2,1)
constexpr int kIndicator = 200;  // muestra activa de Colores: clip-path 0.2s
constexpr int kCloseFade = 200;  // X de cerrar de las pestañas: opacity 0.2s

// La curva de las filas que se mueven: cubic-bezier(.2,.8,.2,1).
QEasingCurve listMoveCurve();

// Anima un valor de from a to llamando a step en cada paso y a done al terminar. La animación
// pertenece a owner (se detiene si se destruye). Sin animaciones llama a step con el valor final
// y a done al momento, y devuelve nulo.
QVariantAnimation* run(QObject* owner, qreal from, qreal to, int ms, const QEasingCurve& curve,
                       const std::function<void(qreal)>& step, const std::function<void()>& done = {});

// Aparición con fundido de una ventana emergente (menús, listas desplegables y ayudas).
void fadeIn(QWidget* widget, int ms);

// Filtro de toda la aplicación: fundido de menús, listas desplegables y ayudas emergentes, y
// el aviso de la ventana modal (parpadea si se pulsa la ventana que bloquea, donde la plataforma
// lo permite: X11 y Windows).
void install();

} // namespace Animations
