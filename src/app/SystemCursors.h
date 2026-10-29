#pragma once

#include <QImage>
#include <QPoint>

// Punteros del tema de cursores del escritorio (los archivos XCursor de Linux: Breeze, Adwaita...),
// para dibujar encima los cursores con una miniatura al lado sin cambiar la flecha del sistema.
namespace SystemCursors {

enum class Shape { Arrow, IBeam };

struct Image {
    QImage image;   // en píxeles físicos
    QPoint hotSpot; // en píxeles de la imagen
};

// El puntero del tema activo a unos 'pixelSize' píxeles físicos (el tamaño más cercano que traiga,
// escalado si hace falta); nulo si no hay tema de cursores que leer (Windows, macOS o sin tema).
Image load(Shape shape, int pixelSize);

// Nombre del tema de cursores (XCURSOR_THEME, el de KDE o el de GTK); vacío si no se usa ninguno.
QString themeName();

} // namespace SystemCursors
