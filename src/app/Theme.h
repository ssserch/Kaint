#pragma once

#include <QPalette>

// Esquema de colores de la aplicación: el del sistema, claro u oscuro.
namespace Theme {

enum class Scheme { System = 0, Light = 1, Dark = 2 };

// Guarda el tema de iconos y el estilo del sistema; hay que llamarla una vez al arrancar,
// antes de aplicar ningún esquema.
void captureSystemDefaults();
// Al arrancar, antes de crear ventanas: estilo Breeze (o Fusion) para un esquema propio, o el
// del sistema.
void initStyle(Scheme scheme);
// Paleta e iconos del esquema.
void apply(Scheme scheme);
// El esquema necesita otro estilo de widgets: se verá bien tras reiniciar.
bool needsRestart(Scheme scheme);
Scheme current();
// La paleta efectiva es oscura (sirve para el esquema "Seguir al sistema").
bool isDark();
// Paletas Breeze clara y oscura (colores de BreezeLight.colors / BreezeDark.colors).
QPalette breezePalette(bool dark);

} // namespace Theme
