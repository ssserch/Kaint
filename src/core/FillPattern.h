#pragma once

#include <QString>

// Estilos de relleno de Paint.NET: color sólido o una trama de 8x8 que usa
// el color principal (bits a 1) y el secundario (bits a 0).
namespace FillPattern {

constexpr int Solid = 0;

int count();
QString name(int index);
// Bit de la trama en (x, y) del lienzo; siempre true para el color sólido.
bool bit(int index, int x, int y);

} // namespace FillPattern
