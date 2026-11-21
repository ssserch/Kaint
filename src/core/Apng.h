#pragma once

#include "core/Gif.h"

#include <QByteArray>
#include <QList>

// PNG animado (APNG): los fotogramas enteros, sin pérdida y con 8 bits de alfa (lo que el GIF no
// puede), cada uno con su duración. Lo exporta el editor de GIF; lo abren los navegadores, Discord
// y la mayoría de los visores.
namespace Apng {

// 'loops' 0: se repite siempre.
QByteArray encode(const QList<Gif::Frame>& frames, int loops, QString* error);
// Cuántos fotogramas tiene un PNG (1 si no es animado; 0 si no es un PNG).
int frameCount(const QByteArray& data);

} // namespace Apng
