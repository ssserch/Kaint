#pragma once

#include <QByteArray>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QStringList>

// Formas personalizadas de Paint.NET (.xaml): un SimpleGeometryShape con
// geometría de WPF, tal como lo describe la documentación pública de la API
// (paintdotnet.github.io/apidocs, "Custom Shapes"). Se admiten la sintaxis de
// trazados (la de SVG) y el modelo de objetos: PathGeometry con todos sus
// segmentos, grupos, combinaciones, elipses, rectángulos, líneas,
// Transformed/Flattened/Outlined/WidenedGeometry y transformaciones.
namespace CustomShapes {

struct Shape {
    QString name;      // DisplayName (o el nombre del archivo)
    QString file;
    QPainterPath path; // en las coordenadas del archivo
};

// Trazado con la sintaxis de WPF/SVG. 'ok' es false si el texto tiene errores.
QPainterPath parsePathMarkup(const QString& data, bool* ok = nullptr);
// Lee una forma de un texto XAML o de un archivo.
bool parse(const QByteArray& xaml, Shape* shape, QString* error);
bool load(const QString& file, Shape* shape, QString* error);

// Carpetas donde se buscan: ~/.local/share/Kaint/Shapes y share/kaint/shapes.
QStringList searchPaths();
void setSearchPathsForTesting(const QStringList& paths);
// Todas las formas encontradas, en orden alfabético (se leen la primera vez;
// reload() vuelve a buscarlas).
const QList<Shape>& all();
void reload();
const Shape* find(const QString& file);

// La forma estirada hasta llenar el rectángulo, como las integradas.
QPainterPath fitted(const QPainterPath& path, const QRectF& rect);

} // namespace CustomShapes
