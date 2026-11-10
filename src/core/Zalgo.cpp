#include "core/Zalgo.h"

#include <QObject>

namespace Zalgo {

namespace {

// Las de los generadores de zalgo: las que suben, las que atraviesan y las que bajan.
constexpr char16_t kUp[] = {0x030d, 0x030e, 0x0304, 0x0305, 0x033f, 0x0311, 0x0306, 0x0310, 0x0352, 0x0357, 0x0351, 0x0307,
                            0x0308, 0x030a, 0x0342, 0x0343, 0x0344, 0x034a, 0x034b, 0x034c, 0x0303, 0x0302, 0x030c, 0x0350,
                            0x0300, 0x0301, 0x030b, 0x030f, 0x0312, 0x0313, 0x0314, 0x033d, 0x0309, 0x0363, 0x0364, 0x0365,
                            0x0366, 0x0367, 0x0368, 0x0369, 0x036a, 0x036b, 0x036c, 0x036d, 0x036e, 0x036f, 0x033e, 0x035b,
                            0x0346, 0x031a};
constexpr char16_t kMiddle[] = {0x0315, 0x031b, 0x0340, 0x0341, 0x0358, 0x0321, 0x0322, 0x0327, 0x0328, 0x0334, 0x0335,
                                0x0336, 0x035c, 0x035d, 0x035e, 0x035f, 0x0360, 0x0362, 0x0338, 0x0337, 0x0361};
constexpr char16_t kDown[] = {0x0316, 0x0317, 0x0318, 0x0319, 0x031c, 0x031d, 0x031e, 0x031f, 0x0320, 0x0324, 0x0325, 0x0326,
                              0x0329, 0x032a, 0x032b, 0x032c, 0x032d, 0x032e, 0x032f, 0x0330, 0x0331, 0x0332, 0x0333, 0x0339,
                              0x033a, 0x033b, 0x033c, 0x0345, 0x0347, 0x0348, 0x0349, 0x034d, 0x034e, 0x0353, 0x0354, 0x0355,
                              0x0356, 0x0359, 0x035a, 0x0323};

// Cuántas marcas de cada clase (mínimo y máximo) según el tipo.
struct Amounts {
    int upMin, upMax, midMin, midMax, downMin, downMax;
};

Amounts amounts(Type type)
{
    switch (type) {
    case Type::Light: return {0, 2, 0, 1, 0, 2};
    case Type::Normal: return {1, 8, 0, 2, 1, 8};
    case Type::Max: return {4, 16, 1, 4, 4, 16};
    case Type::Up: return {2, 10, 0, 0, 0, 0};
    case Type::Down: return {0, 0, 0, 0, 2, 10};
    case Type::Through: return {0, 0, 1, 3, 0, 0};
    }
    return {1, 8, 0, 2, 1, 8};
}

// Generador pequeño y repetible (xorshift32).
struct Random {
    quint32 state;
    quint32 next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int between(int lo, int hi) { return hi <= lo ? lo : lo + int(next() % quint32(hi - lo + 1)); }
};

template<size_t N>
void addMarks(QString& out, Random& random, const char16_t (&marks)[N], int count)
{
    for (int i = 0; i < count; ++i)
        out += QChar(marks[random.next() % N]);
}

} // namespace

QString apply(const QString& text, Type type, quint32 seed)
{
    const Amounts a = amounts(type);
    QString out;
    out.reserve(text.size() * 8);
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        out += c;
        // Ni en los espacios ni entre las dos mitades de un carácter de fuera del plano básico.
        if (c.isSpace() || c.isHighSurrogate() || (c.category() >= QChar::Mark_NonSpacing && c.category() <= QChar::Mark_Enclosing))
            continue;
        Random random{(seed ^ (quint32(i) + 1u) * 2654435761u) | 1u};
        random.next();
        addMarks(out, random, kUp, random.between(a.upMin, a.upMax));
        addMarks(out, random, kMiddle, random.between(a.midMin, a.midMax));
        addMarks(out, random, kDown, random.between(a.downMin, a.downMax));
    }
    return out;
}

QStringList typeNames()
{
    return {QObject::tr("Leve"), QObject::tr("Normal"), QObject::tr("Máximo"), QObject::tr("Solo hacia arriba"),
            QObject::tr("Solo hacia abajo"), QObject::tr("Atravesado")};
}

} // namespace Zalgo
