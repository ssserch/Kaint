#pragma once

#include <QImage>
#include <QStringList>

// Modos de fusión de los efectos de Representación de Paint.NET 5 (Nubes,
// Turbulencia, fractales): los de Direct2D más los propios de Paint.NET y
// los operadores de composición, con sus nombres y su orden.
namespace RenderBlend {

enum Mode {
    Over, Behind, Overwrite, Multiply, Screen, Darken, Lighten, Dissolve, ColorBurn, LinearBurn, DarkerColor,
    LighterColor, ColorDodge, LinearDodge, Overlay, SoftLight, HardLight, VividLight, LinearLight, PinLight, HardMix,
    Difference, Exclusion, Hue, Saturation, Color, Luminosity, Subtract, Division, Additive, Reflect, Glow, Negation,
    Minimum, Maximum, In, DestinationIn, Out, DestinationOut, Atop, DestinationAtop, Plus, MaskInvert, Xor8, Xor16,
    XorComposite, Count
};

QStringList names();
// Compone 'top' (premultiplicada, del tamaño de la región) sobre 'base' (alfa
// directo o premultiplicada, del mismo tamaño).
void compose(QImage& base, const QImage& top, int mode);

} // namespace RenderBlend
