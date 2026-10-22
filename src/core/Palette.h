#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

// Paletas de 96 colores en el formato de texto de Paint.NET: un color por línea en hexadecimal
// aarrggbb y comentarios que empiezan por ';'.
namespace Palette {

constexpr int kColorCount = 96;

QList<QColor> defaultColors();
QList<QColor> parse(const QString& text);
QString serialize(const QList<QColor>& colors);

QList<QColor> load(const QString& path, QString* error = nullptr);
bool save(const QList<QColor>& colors, const QString& path, QString* error = nullptr);

// Carpeta de paletas del usuario (~/.local/share/Kaint/Palettes).
QString userFolder();
QStringList available();

} // namespace Palette
