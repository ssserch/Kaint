// Texto «zalgo» (src/core/Zalgo.cpp): marcas diacríticas combinantes (U+0300...U+036F) apiladas
// encima, atravesando o debajo de cada letra. Las marcas de una letra dependen solo de 'seed' y de
// su posición (las mismas cuentas que en Kaint: el mismo texto sale igual en los dos).
const Up = [0x030d, 0x030e, 0x0304, 0x0305, 0x033f, 0x0311, 0x0306, 0x0310, 0x0352, 0x0357, 0x0351, 0x0307, 0x0308, 0x030a, 0x0342,
            0x0343, 0x0344, 0x034a, 0x034b, 0x034c, 0x0303, 0x0302, 0x030c, 0x0350, 0x0300, 0x0301, 0x030b, 0x030f, 0x0312, 0x0313,
            0x0314, 0x033d, 0x0309, 0x0363, 0x0364, 0x0365, 0x0366, 0x0367, 0x0368, 0x0369, 0x036a, 0x036b, 0x036c, 0x036d, 0x036e,
            0x036f, 0x033e, 0x035b, 0x0346, 0x031a];
const Middle = [0x0315, 0x031b, 0x0340, 0x0341, 0x0358, 0x0321, 0x0322, 0x0327, 0x0328, 0x0334, 0x0335, 0x0336, 0x035c, 0x035d,
                0x035e, 0x035f, 0x0360, 0x0362, 0x0338, 0x0337, 0x0361];
const Down = [0x0316, 0x0317, 0x0318, 0x0319, 0x031c, 0x031d, 0x031e, 0x031f, 0x0320, 0x0324, 0x0325, 0x0326, 0x0329, 0x032a,
              0x032b, 0x032c, 0x032d, 0x032e, 0x032f, 0x0330, 0x0331, 0x0332, 0x0333, 0x0339, 0x033a, 0x033b, 0x033c, 0x0345,
              0x0347, 0x0348, 0x0349, 0x034d, 0x034e, 0x0353, 0x0354, 0x0355, 0x0356, 0x0359, 0x035a, 0x0323];

// Tipos: 0 leve, 1 normal, 2 máximo, 3 solo hacia arriba, 4 solo hacia abajo, 5 atravesado.
// [mínimo y máximo de las que suben, de las de en medio y de las que bajan].
const Amounts = [[0, 2, 0, 1, 0, 2], [1, 8, 0, 2, 1, 8], [4, 16, 1, 4, 4, 16], [2, 10, 0, 0, 0, 0], [0, 0, 0, 0, 2, 10], [0, 0, 1, 3, 0, 0]];

export const ZalgoTypes = ["Leve", "Normal", "Máximo", "Solo hacia arriba", "Solo hacia abajo", "Atravesado"];

export function zalgo(text, type = 1, seed = 0) {
    const [upMin, upMax, midMin, midMax, downMin, downMax] = Amounts[type] || Amounts[1];
    let out = "";
    for (let i = 0; i < text.length; ++i) {
        const c = text[i], code = text.charCodeAt(i);
        out += c;
        // Ni en los espacios ni entre las dos mitades de un carácter de fuera del plano básico.
        if (/\s/.test(c) || (code >= 0xd800 && code <= 0xdbff) || /\p{M}/u.test(c))
            continue;
        // xorshift32, como Kaint.
        let state = ((seed ^ Math.imul(i + 1, 2654435761 | 0)) | 1) >>> 0;
        const next = () => {
            state = (state ^ (state << 13)) >>> 0;
            state = (state ^ (state >>> 17)) >>> 0;
            state = (state ^ (state << 5)) >>> 0;
            return state;
        };
        const between = (lo, hi) => (hi <= lo ? lo : lo + (next() % (hi - lo + 1)));
        next();
        const add = (marks, count) => {
            for (let k = 0; k < count; ++k)
                out += String.fromCharCode(marks[next() % marks.length]);
        };
        add(Up, between(upMin, upMax));
        add(Middle, between(midMin, midMax));
        add(Down, between(downMin, downMax));
    }
    return out;
}
