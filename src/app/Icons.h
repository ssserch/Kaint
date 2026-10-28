#pragma once

#include "app/AppState.h"

#include <QCursor>
#include <QIcon>
#include <QPainterPath>

#include <initializer_list>

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

// Icono de estilo Paint.NET: el PNG de la interfaz (named) o, si no hay, uno dibujado por código
// (las herramientas que llevan los colores activos y la mano); si no, nulo.
QIcon pdn(const QString& name);
// Icono de la interfaz en PNG (data/ui-icons/<archivo>.png), en gris si está desactivado; onFile
// es el del estado marcado de los botones que se activan y desactivan.
QIcon png(const QString& file, const QString& onFile = {});
// El PNG que corresponde a un nombre de Kaint ("document-save", "layer-new"...), o nulo.
QIcon named(const QString& name);
// Nombres de la tabla de PNG cuyo archivo no está entre los recursos (para las pruebas).
QStringList missingNamedIcons();
// Nombre del icono de un ajuste o efecto ("effect-" + su identificador) si hay PNG; si no, fallback.
QString effectIcon(const QString& id, const QString& fallback);
// Cursores de la mano (Mano, botón central y barra espaciadora): abierta o agarrando.
QCursor handCursor(bool grabbing);
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
