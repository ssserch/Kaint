#pragma once

#include <QPalette>

// Esquema de colores de la aplicación: el del sistema, claro u oscuro.
//
// Con un esquema propio, Kaint no usa la configuración del escritorio y dibuja
// con el estilo Fusion y su paleta Breeze: estilos como Kvantum pintan con sus
// propios colores y Plasma vuelve a imponer su tema al cambiarlo. Eso se decide
// antes de crear la aplicación, así que pasar del esquema del sistema a uno
// propio (o al revés) se completa al reiniciar.
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2 };

// Guarda el tema de iconos del sistema, una vez al arrancar.
void captureSystemDefaults();
// Antes de crear QApplication.
void prepare(Scheme scheme);
// Después de crear QApplication y antes de las ventanas.
void initStyle(Scheme scheme);
// Paleta e iconos del esquema.
void apply(Scheme scheme);
// El esquema pedido necesita reiniciar Kaint para aplicarse del todo.
bool needsRestart(Scheme scheme);
Scheme current();
// La paleta efectiva es oscura (sirve para el esquema "Seguir al sistema").
bool isDark();
// Paletas Breeze clara y oscura (colores de BreezeLight.colors / BreezeDark.colors).
QPalette breezePalette(bool dark);

} // namespace Theme
