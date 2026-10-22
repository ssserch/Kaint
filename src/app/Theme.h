#pragma once

#include <QPalette>

// Esquema de colores de la aplicación: el del sistema, claro u oscuro.
// Un esquema propio aplica una paleta Breeze, el tema de iconos a juego y un
// estilo que respete la paleta. El resto de la interfaz lee la paleta.
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2 };

// Guarda el tema de iconos y el estilo del sistema; hay que llamarla una vez
// al arrancar, antes de aplicar ningún esquema.
void captureSystemDefaults();
void apply(Scheme scheme);
Scheme current();
// La paleta efectiva es oscura (sirve para el esquema "Seguir al sistema").
bool isDark();
// Paletas Breeze clara y oscura (colores de BreezeLight.colors / BreezeDark.colors).
QPalette breezePalette(bool dark);

} // namespace Theme
