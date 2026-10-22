#pragma once

#include "app/AppState.h"

#include <QIcon>
#include <QPainterPath>

#include <initializer_list>

namespace Icons {

// Asegura que se encuentran los temas de iconos del sistema (Breeze) aunque
// no haya un tema de plataforma de KDE cargado (otros escritorios, pruebas).
void setupTheme();

// Icono de herramienta: el de img/tools/<nombre>.svg|png si se incluyó al
// compilar (iconos al estilo Paint.NET) o, si no, el del tema.
QIcon tool(const QString& name, const QIcon& fallback);

// Primer icono disponible del tema del sistema (Breeze en KDE).
QIcon theme(std::initializer_list<const char*> names);
QIcon theme(const QString& name);

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
QIcon textAlign(TextAlign align);
QIcon smoothing(bool on);
QIcon gamma(bool corrected);
QIcon fontMetric(FontMetric metric);
QIcon recolorSampling(RecolorSampling sampling);
QIcon rectSelectMode(RectSelectMode mode);
QIcon afterSelect(AfterSelect after);
QIcon sampleSize(int pixels);
// Botones − y + junto a los deslizadores y tamaños de la barra.
QIcon stepper(bool plus);
QIcon blendMode();
QIcon swatch(const QColor& color);
QIcon cloneStamp();
QIcon app();

} // namespace Icons
