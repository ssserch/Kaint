#pragma once

#include <QString>
#include <QStringList>

// Texto «zalgo»: marcas diacríticas combinantes (U+0300...U+036F) apiladas encima, atravesando o
// debajo de cada letra, como los generadores de zalgo de la web. Las marcas de una letra dependen
// solo de 'seed' y de su posición: el mismo texto sale siempre igual (al escribir no cambian las
// letras de antes).
namespace Zalgo {

enum class Type { Light = 0, Normal = 1, Max = 2, Up = 3, Down = 4, Through = 5 };

QString apply(const QString& text, Type type, quint32 seed = 0);
// Nombres para la lista de tipos (en el orden de Type).
QStringList typeNames();

} // namespace Zalgo
