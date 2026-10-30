#pragma once

#include "app/AppState.h"

#include <QCursor>
#include <QIcon>
#include <QPainterPath>

#include <functional>
#include <initializer_list>

class QPainter;

namespace Icons {

// Asegura que se encuentran los temas de iconos del sistema (Breeze) aunque no haya un tema de
// plataforma de KDE cargado (otros escritorios, pruebas) y, si el sistema no tiene Breeze
// (Windows), usa los iconos de Breeze que lleva Kaint (data/icons).
void setupTheme();
// Breeze claro u oscuro: el del sistema o, si no hay, el incluido en Kaint.
QString breezeTheme(bool dark);

// Icono de herramienta: el de img/tools/<nombre>.svg|png si se incluyó al compilar (iconos al
// estilo Paint.NET) o, si no, el del tema.
QIcon tool(const QString& name, const QIcon& fallback);

// Primer icono disponible: los de estilo Paint.NET de Kaint y, si no hay, el del tema del
// sistema (Breeze en KDE).
QIcon theme(std::initializer_list<const char*> names);
QIcon theme(const QString& name);
// Primer icono disponible del tema del sistema (Breeze en KDE o el incluido), sin los de estilo
// Paint.NET.
QIcon system(std::initializer_list<const char*> names);

// Icono de estilo Paint.NET: el de herramienta dibujado por código (el del paquete de iconos
// activo si trae ese archivo) o, si no hay, el PNG de la interfaz (named); si no, nulo.
QIcon pdn(const QString& name);
// Icono de la interfaz en PNG (data/ui-icons/<archivo>.png), en gris si está desactivado; onFile
// es el del estado marcado de los botones que se activan y desactivan.
QIcon png(const QString& file, const QString& onFile = {});
// El PNG que corresponde a un nombre de Kaint ("document-save", "layer-new"...), o nulo.
QIcon named(const QString& name);
// Archivo de paint.js (sin ".png") de un nombre de Kaint, o vacío.
QString pngFile(const QString& name);
// Gris y medio transparente: el aspecto de los iconos desactivados.
QImage disabledImage(QImage image);
// Nombres de la tabla de PNG cuyo archivo no está entre los recursos (para las pruebas).
QStringList missingNamedIcons();
// Nombre del icono de un ajuste o efecto ("effect-" + su identificador) si hay PNG; si no, fallback.
QString effectIcon(const QString& id, const QString& fallback);
// Estrella naranja de ocho rayos que marca las imágenes con cambios sin guardar (como Paint.NET).
void paintUnsavedStar(QPainter& p, const QRectF& r);
// Cursores de la mano (Mano, botón central y barra espaciadora): abierta o agarrando.
QCursor handCursor(bool grabbing);
// Tamaño de los cursores del sistema (XCURSOR_SIZE, el de KDE o GTK, o el de Windows), en
// píxeles lógicos.
int cursorSize();
// Cursor con el icono actual de una herramienta ("paintbrush", "zoom"...), a la medida del cursor
// del sistema y con el punto activo en su punta; si no hay icono, 'fallback'.
QCursor toolCursor(const QString& name, Qt::CursorShape fallback = Qt::CrossCursor);
// Flecha curva de girar (como la de paint.js) apuntando a lo largo del giro: 'angle' es la
// dirección, en radianes, desde el puntero hacia el centro del giro.
QCursor rotateCursor(qreal angle);
// Como en Paint.NET, el puntero normal (la flecha o, con 'ibeam', la I del texto) con un dibujo en
// miniatura al lado: 'paint' lo dibuja en el rectángulo que recibe y 'key' lo identifica en la
// caché. Lo usan la selección, el degradado, la línea, el texto y las formas.
QCursor miniCursor(const QString& key, const std::function<void(QPainter&, const QRectF&)>& paint, bool ibeam = false);
// El de una herramienta ("gradient", "text"...): su icono actual en miniatura.
QCursor miniToolCursor(const QString& name, bool ibeam = false);
// Un fotograma de un cursor animado y los milisegundos que se ve.
struct CursorFrame {
    QCursor cursor;
    int delay = 0;
};
// Si el puntero del sistema está animado (temas XCursor con varios fotogramas, .ani de Windows),
// los fotogramas del cursor que devolvió miniCursor() o miniToolCursor(), el primero el mismo;
// vacío con los demás cursores.
QList<CursorFrame> cursorFrames(const QCursor& cursor);
// Con 'on' (por defecto), esos cursores usan la flecha y la I del tema de cursores del sistema
// (donde se puede leer, como los temas XCursor de Linux); si no, las que dibuja Kaint.
void setSystemPointer(bool on);
// Colores con los que se tiñen el cubo de pintura, el pincel, cambiar color y el degradado
// (inválidos: sus colores de siempre). Los iconos ya creados cambian al volver a dibujarse.
void setActiveColors(const QColor& primary, const QColor& secondary);

// Iconos dibujados por código para opciones sin equivalente en el tema.
QIcon gradient(GradientType type);
QIcon gradientRepeat(GradientRepeat repeat);
QIcon fillPattern(int pattern);
QIcon shape(ShapeType type);
QIcon customShape(const QPainterPath& path);
QIcon shapeStyle(ShapeStyle style);
QIcon curveType(CurveType type);
QIcon lineCap(LineCap cap, bool start);
QIcon antialias(bool on);
QIcon selectionQuality(bool antialias);
QIcon selectionMode(SelectionMode mode);
QIcon dash(Qt::PenStyle style);
QIcon floodMode(bool global);
QIcon toleranceAlpha(bool straight);
QIcon fillAccumulate(bool on);
QIcon textAlign(TextAlign align);
QIcon smoothing(bool on);
QIcon gamma(bool corrected);
// Ojo de visibilidad (abierto: se muestra).
QIcon eye(bool open);
QIcon fontMetric(FontMetric metric);
QIcon recolorSampling(RecolorSampling sampling);
QIcon rectSelectMode(RectSelectMode mode);
QIcon afterSelect(AfterSelect after);
QIcon sampleSize(int pixels);
// Botones − y + junto a los deslizadores y tamaños de la barra.
QIcon stepper(bool plus);
QIcon blendMode();
QIcon swatch(const QColor& color);
QIcon app();

} // namespace Icons
