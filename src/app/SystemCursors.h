#pragma once

#include <QImage>
#include <QPoint>

// Punteros del sistema (el tema de cursores XCursor de Linux, como Breeze o Adwaita, o los
// punteros de Windows con su esquema y tamaño), para dibujar encima los cursores con una
// miniatura al lado sin cambiar la flecha del sistema.
namespace SystemCursors {

enum class Shape { Arrow, IBeam };

struct Image {
    QImage image;   // en píxeles físicos
    QPoint hotSpot; // en píxeles de la imagen
};

// El puntero del tema activo a unos 'pixelSize' píxeles físicos (el tamaño más cercano que traiga,
// escalado si hace falta); nulo si no hay punteros que leer (macOS o Linux sin tema de cursores).
Image load(Shape shape, int pixelSize);

// Nombre del tema de cursores (XCURSOR_THEME, el de KDE o el de GTK; en Windows, el esquema de
// punteros); vacío si no se usa ninguno.
QString themeName();

} // namespace SystemCursors
