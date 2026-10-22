#pragma once

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

// Modos de fusión de Paint.NET.
enum class BlendMode {
    Normal,
    Multiply,
    Additive,
    ColorBurn,
    ColorDodge,
    Reflect,
    Glow,
    Overlay,
    Difference,
    Negation,
    Lighten,
    Darken,
    Screen,
    Xor,
    Overwrite,
};

QString blendModeName(BlendMode mode);
// Paint.NET traduce distinto la lista de la barra de herramientas ("Aditivo", "Reflejar",
// "Resplandor", "Trama"...).
QString blendModeToolName(BlendMode mode);
const QList<BlendMode>& layerBlendModes();
const QList<BlendMode>& toolBlendModes();

// Nombres usados por OpenRaster (atributo composite-op de stack.xml).
QString blendModeOraName(BlendMode mode);
BlendMode blendModeFromOra(const QString& name);

// Compone 'src' (la región srcRect) sobre 'dst' en la posición 'at' con el modo y la opacidad
// (0..255) indicados.
void blendImage(QImage& dst, QPoint at, const QImage& src, const QRect& srcRect, BlendMode mode, int opacity = 255);
inline void blendImage(QImage& dst, QPoint at, const QImage& src, BlendMode mode, int opacity = 255)
{
    blendImage(dst, at, src, src.rect(), mode, opacity);
}
