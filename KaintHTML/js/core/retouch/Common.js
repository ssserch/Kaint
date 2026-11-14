// Retoque de PhotoCraft (como core/retouch de Kaint): Pincel corrector, Pincel corrector puntual,
// Quitar, Parche, Lazo magnético y Selección de objetos. Los algoritmos son los de PhotoCraft
// (ArtCraft Team y los colaboradores de PhotoCraft, licencia MIT; licenses/PhotoCraft-LICENSE.txt),
// escritos para Kaint.HTML. Colores RGBA directos en coma flotante (0..1) entrelazados (w × h × ch,
// Float32Array) y máscaras de w × h (Uint8Array, 0 o 1).
//
// Esta parte: los números pseudoaleatorios de 64 bits, rectángulos, máscaras, la transformada de
// distancia, la fusión de Poisson y la curación de los pinceles correctores.

// Se canceló (Esc, otra herramienta...).
export class Cancelled extends Error {
    constructor() {
        super("cancelled");
    }
}

// Si hay que parar: los bucles largos lo consultan. 'flag' es una función que devuelve true.
export class Stop {
    constructor(flag = null) {
        this.flag = flag;
    }
    requested() {
        return !!(this.flag && this.flag());
    }
    check() {
        if (this.requested())
            throw new Cancelled();
    }
}
export const NEVER = new Stop();

// ---------------------------------------------------------------------------------------------
// Aritmética de 64 bits con dos mitades de 32 (para SplitMix64 y xorshift, sin BigInt).

function mul32(a, b) {
    // a·b de 32 × 32 bits → [hi, lo] de 64.
    const a0 = a & 0xffff, a1 = a >>> 16, b0 = b & 0xffff, b1 = b >>> 16;
    const p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const mid = (p00 >>> 16) + (p01 & 0xffff) + (p10 & 0xffff);
    const lo = ((mid & 0xffff) << 16 | (p00 & 0xffff)) >>> 0;
    const hi = (p11 + (p01 >>> 16) + (p10 >>> 16) + (mid >>> 16)) >>> 0;
    return [hi, lo];
}

// (ah:al)·(bh:bl) mod 2^64.
export function mul64(ah, al, bh, bl) {
    const [h, l] = mul32(al, bl);
    const cross = (Math.imul(ah, bl) + Math.imul(al, bh)) >>> 0;
    return [(h + cross) >>> 0, l];
}

export function add64(ah, al, bh, bl) {
    const lo = (al + bl) >>> 0;
    const carry = lo < al ? 1 : 0;
    return [(ah + bh + carry) >>> 0, lo];
}

function shr64(h, l, n) {
    if (n >= 32)
        return [0, h >>> (n - 32)];
    return [h >>> n, ((l >>> n) | (h << (32 - n))) >>> 0];
}

function shl64(h, l, n) {
    if (n >= 32)
        return [(l << (n - 32)) >>> 0, 0];
    return [((h << n) | (l >>> (32 - n))) >>> 0, (l << n) >>> 0];
}

// (h:l) mod n, para n < 2^26.
export function mod64(h, l, n) {
    const t = 4294967296 % n;
    return (((h % n) * t) % n + (l % n)) % n;
}

const GOLD_H = 0x9e3779b9, GOLD_L = 0x7f4a7c15;

// SplitMix64: la secuencia de Kaint, igual en todos los equipos.
export class MixRng {
    constructor(seedH, seedL) {
        // semilla ^ 0x9E3779B97F4A7C15
        this.h = (seedH ^ GOLD_H) >>> 0;
        this.l = (seedL ^ GOLD_L) >>> 0;
    }
    // El siguiente, como [hi, lo].
    next64() {
        [this.h, this.l] = add64(this.h, this.l, GOLD_H, GOLD_L);
        let [zh, zl] = [this.h, this.l];
        let [sh, sl] = shr64(zh, zl, 30);
        [zh, zl] = mul64((zh ^ sh) >>> 0, (zl ^ sl) >>> 0, 0xbf58476d, 0x1ce4e5b9);
        [sh, sl] = shr64(zh, zl, 27);
        [zh, zl] = mul64((zh ^ sh) >>> 0, (zl ^ sl) >>> 0, 0x94d049bb, 0x133111eb);
        [sh, sl] = shr64(zh, zl, 31);
        return [(zh ^ sh) >>> 0, (zl ^ sl) >>> 0];
    }
    // El siguiente módulo n.
    nextMod(n) {
        const [h, l] = this.next64();
        return mod64(h, l, n);
    }
    range(lo, hi) {
        if (hi <= lo)
            return lo;
        return lo + this.nextMod(hi - lo + 1);
    }
}

// Una semilla de 64 bits como [hi, lo] a partir de un número (hasta 2^53).
export function seed64(v) {
    return [Math.floor(v / 4294967296) >>> 0, v >>> 0];
}

// semilla ^ (k << shift) para k pequeño.
export function seedXorShift(seed, k, shift) {
    const [kh, kl] = shl64(0, k >>> 0, shift);
    return [(seed[0] ^ kh) >>> 0, (seed[1] ^ kl) >>> 0];
}

export function seedAdd(seed, k) {
    return add64(seed[0], seed[1], 0, k >>> 0);
}

export function seedXor(seed, other) {
    return [(seed[0] ^ other[0]) >>> 0, (seed[1] ^ other[1]) >>> 0];
}

// La semilla de la banda bi de una pasada: seed ^ bi·0x2545F4914F6CDD1D ^ (pass << 40).
export function bandSeed(seed, bi, pass) {
    const m = mul64(0, bi >>> 0, 0x2545f491, 0x4f6cdd1d);
    const p = shl64(0, pass >>> 0, 40);
    return [(seed[0] ^ m[0] ^ p[0]) >>> 0, (seed[1] ^ m[1] ^ p[1]) >>> 0];
}

// xorshift64 (la Selección de objetos).
export class XorRng {
    constructor(seed) {
        const [h, l] = mul64(...seed64(seed), GOLD_H, GOLD_L);
        this.h = h;
        this.l = (l | 1) >>> 0;
    }
    next64() {
        let [h, l] = [this.h, this.l];
        let [th, tl] = shl64(h, l, 13);
        h = (h ^ th) >>> 0; l = (l ^ tl) >>> 0;
        [th, tl] = shr64(h, l, 7);
        h = (h ^ th) >>> 0; l = (l ^ tl) >>> 0;
        [th, tl] = shl64(h, l, 17);
        h = (h ^ th) >>> 0; l = (l ^ tl) >>> 0;
        this.h = h;
        this.l = l;
        return [h, l];
    }
    nextMod(n) {
        const [h, l] = this.next64();
        return mod64(h, l, n);
    }
    // Uniforme en [0, 1): los 24 bits altos.
    unit() {
        const [h] = this.next64();
        return (h >>> 8) / 16777216;
    }
}

// ---------------------------------------------------------------------------------------------
// Rectángulos [x0, x1) × [y0, y1).

export const box = (x0, y0, x1, y1) => ({ x0, y0, x1, y1 });
export const boxW = (b) => Math.max(b.x1 - b.x0, 0);
export const boxH = (b) => Math.max(b.y1 - b.y0, 0);
export const boxArea = (b) => boxW(b) * boxH(b);
export const boxEmpty = (b) => b.x1 <= b.x0 || b.y1 <= b.y0;
export const boxContains = (b, x, y) => x >= b.x0 && x < b.x1 && y >= b.y0 && y < b.y1;
export const boxIntersect = (a, b) => box(Math.max(a.x0, b.x0), Math.max(a.y0, b.y0), Math.min(a.x1, b.x1), Math.min(a.y1, b.y1));
export const boxUnite = (a, b) => (boxEmpty(a) ? b : boxEmpty(b) ? a
    : box(Math.min(a.x0, b.x0), Math.min(a.y0, b.y0), Math.max(a.x1, b.x1), Math.max(a.y1, b.y1)));
export const boxInflate = (b, d) => box(b.x0 - d, b.y0 - d, b.x1 + d, b.y1 + d);
export const boxTranslate = (b, dx, dy) => box(b.x0 + dx, b.y0 + dy, b.x1 + dx, b.y1 + dy);
export const boxEquals = (a, b) => a.x0 === b.x0 && a.y0 === b.y0 && a.x1 === b.x1 && a.y1 === b.y1;

// El trazo más grande (su rectángulo), en píxeles: 16 Mi.
export const MAX_PIXELS = 16 * 1024 * 1024;

// ---------------------------------------------------------------------------------------------
// Máscaras.

// La máscara crecida 'r' celdas (elemento cuadrado, en dos pasadas).
export function dilate(m, w, h, r) {
    if (r <= 0 || m.length !== w * h)
        return m.slice();
    const pass = (src, horizontal) => {
        const lines = horizontal ? h : w, len = horizontal ? w : h;
        const at = horizontal ? (line, k) => line * w + k : (line, k) => k * w + line;
        const out = new Uint8Array(src.length);
        for (let line = 0; line < lines; ++line) {
            let count = 0;
            for (let k = 0; k < Math.min(len, r); ++k)
                count += src[at(line, k)];
            for (let k = 0; k < len; ++k) {
                if (k + r < len)
                    count += src[at(line, k + r)];
                if (k > r)
                    count -= src[at(line, k - r - 1)];
                out[at(line, k)] = count > 0 ? 1 : 0;
            }
        }
        return out;
    };
    return pass(pass(m, true), false);
}

// Añade a la máscara lo que encierra (lo que no llega al borde sin cruzarla).
export function closeLoops(m, w, h) {
    if (w <= 0 || h <= 0 || m.length !== w * h)
        return;
    const outside = new Uint8Array(m.length);
    const stack = [];
    const visit = (i) => {
        if (!m[i] && !outside[i]) {
            outside[i] = 1;
            stack.push(i);
        }
    };
    for (let x = 0; x < w; ++x) {
        visit(x);
        visit((h - 1) * w + x);
    }
    for (let y = 0; y < h; ++y) {
        visit(y * w);
        visit(y * w + w - 1);
    }
    while (stack.length) {
        const i = stack.pop(), x = i % w, y = (i / w) | 0;
        if (x > 0) visit(i - 1);
        if (x + 1 < w) visit(i + 1);
        if (y > 0) visit(i - w);
        if (y + 1 < h) visit(i + w);
    }
    for (let i = 0; i < m.length; ++i)
        m[i] = outside[i] ? 0 : 1;
}

// La mayor distancia de tablero de ajedrez de una celda del hueco a la más cercana de fuera.
export function holeRadius(w, h, hole) {
    if (hole.length !== w * h)
        return 0;
    const INF = 4294967295;
    const d = new Float64Array(hole.length);
    for (let i = 0; i < hole.length; ++i)
        d[i] = hole[i] ? INF : 0;
    const relax = (i, j) => {
        const v = d[j] === INF ? INF : d[j] + 1;
        if (v < d[i])
            d[i] = v;
    };
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const i = y * w + x;
            if (x > 0) relax(i, i - 1);
            if (y > 0) {
                relax(i, i - w);
                if (x > 0) relax(i, i - w - 1);
                if (x + 1 < w) relax(i, i - w + 1);
            }
        }
    for (let y = h - 1; y >= 0; --y)
        for (let x = w - 1; x >= 0; --x) {
            const i = y * w + x;
            if (x + 1 < w) relax(i, i + 1);
            if (y + 1 < h) {
                relax(i, i + w);
                if (x + 1 < w) relax(i, i + w + 1);
                if (x > 0) relax(i, i + w - 1);
            }
        }
    let best = 0;
    for (let i = 0; i < hole.length; ++i)
        if (hole[i])
            best = Math.max(best, d[i]);
    return best;
}

// La ventana de Quitar: el trazo y un margen de 3/4 de su lado mayor (de 32 a 512).
export function removeWindow(stroke, canvas) {
    stroke = boxIntersect(stroke, canvas);
    if (boxEmpty(stroke))
        return box(0, 0, 0, 0);
    const ext = Math.max(boxW(stroke), boxH(stroke));
    const margin = Math.max(Math.floor((ext * 3) / 4), 32);
    return boxIntersect(boxInflate(stroke, Math.min(margin, 512)), canvas);
}

// La ventana de Relleno según el contenido (la de Photoshop): un cuadrado de 4·√(área).
export function samplingWindow(hole, canvas) {
    hole = boxIntersect(hole, canvas);
    if (boxEmpty(hole) || boxEmpty(canvas))
        return box(0, 0, 0, 0);
    const w = Math.max(boxW(hole), 50), h = Math.max(boxH(hole), 50);
    const side = Math.round(4 * Math.sqrt(w * h));
    const span = (c2, extent, lo, hi) => {
        const s = Math.max(side, extent);
        if (s >= hi - lo)
            return [lo, hi];
        const a = Math.min(Math.max(Math.trunc((c2 - s) / 2), lo), hi - s);
        return [a, a + s];
    };
    const [x0, x1] = span(hole.x0 + hole.x1, boxW(hole), canvas.x0, canvas.x1);
    const [y0, y1] = span(hole.y0 + hole.y1, boxH(hole), canvas.y0, canvas.y1);
    return box(x0, y0, x1, y1);
}

const NO_SEED = 4294967295;
const NO_DISTANCE = 1e10;

// Distancia euclídea a la celda marcada más cercana (Felzenszwalb y Huttenlocher; 1e10 sin ninguna).
export function distanceTransform(inside, w, h) {
    const n = w * h;
    const out = new Float32Array(n);
    if (!n || inside.length !== n)
        return out;
    let any = false, all = true;
    for (let i = 0; i < n; ++i) {
        any = any || !!inside[i];
        all = all && !!inside[i];
    }
    if (!any)
        return out.fill(NO_DISTANCE);
    if (all)
        return out;
    const vertical = new Float64Array(n).fill(NO_SEED); // x·h + y
    for (let x = 0; x < w; ++x) {
        const base = x * h;
        let nearest = -1;
        for (let y = 0; y < h; ++y) {
            if (inside[y * w + x])
                nearest = y;
            if (nearest >= 0)
                vertical[base + y] = y - nearest;
        }
        nearest = -1;
        for (let y = h - 1; y >= 0; --y) {
            if (vertical[base + y] === 0)
                nearest = y;
            if (nearest >= 0)
                vertical[base + y] = Math.min(vertical[base + y], nearest - y);
        }
    }
    const costs = new Float64Array(w), sites = new Int32Array(w), starts = new Float64Array(w);
    for (let y = 0; y < h; ++y) {
        for (let x = 0; x < w; ++x)
            costs[x] = vertical[x * h + y];
        let count = 0;
        for (let q = 0; q < w; ++q) {
            if (costs[q] === NO_SEED)
                continue;
            const fq = costs[q] * costs[q];
            let start = -Infinity;
            while (count > 0) {
                const p = sites[count - 1], dp = costs[p];
                start = (fq - dp * dp) / (2 * (q - p)) + (q + p) * 0.5;
                if (start > starts[count - 1])
                    break;
                --count;
            }
            if (count === 0)
                start = -Infinity;
            sites[count] = q;
            starts[count] = start;
            ++count;
        }
        const row = y * w;
        if (count === 0) {
            out.fill(NO_DISTANCE, row, row + w);
            continue;
        }
        let k = 0;
        for (let x = 0; x < w; ++x) {
            while (k + 1 < count && starts[k + 1] < x)
                ++k;
            const dx = x - sites[k], v = costs[sites[k]];
            out[row + x] = Math.sqrt(dx * dx + v * v);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Poisson (Pérez, Gangnet y Blake): la membrana armónica, resuelta en cascada.

const TOL = 2e-5, FINE_ITERS = 30, FINE_OMEGA = 1.7;

function sor(w, h, unknown, v, maxIter, omega, stop) {
    const cells = [], masks = [];
    for (let i = 0; i < w * h; ++i)
        if (unknown[i]) {
            const x = i % w, y = (i / w) | 0;
            cells.push(i);
            masks.push((x > 0 ? 1 : 0) | (x + 1 < w ? 2 : 0) | (y > 0 ? 4 : 0) | (y + 1 < h ? 8 : 0));
        }
    for (let it = 0; it < maxIter; ++it) {
        if (stop.requested())
            return;
        let maxD = 0;
        for (let k = 0; k < cells.length; ++k) {
            const i = cells[k], m = masks[k];
            let s, n;
            if (m === 15) {
                s = v[i - 1] + v[i + 1] + v[i - w] + v[i + w];
                n = 4;
            } else {
                s = 0;
                n = 0;
                if (m & 1) { s += v[i - 1]; n += 1; }
                if (m & 2) { s += v[i + 1]; n += 1; }
                if (m & 4) { s += v[i - w]; n += 1; }
                if (m & 8) { s += v[i + w]; n += 1; }
                if (n === 0)
                    continue;
            }
            const d = omega * (s / n - v[i]);
            v[i] += d;
            maxD = Math.max(maxD, Math.abs(d));
        }
        if (maxD < TOL)
            break;
    }
}

function solveRec(w, h, unknown, v, depth, stop) {
    if (stop.requested())
        return;
    let nUnknown = 0, knownSum = 0, knownCount = 0;
    for (let i = 0; i < w * h; ++i) {
        if (unknown[i])
            ++nUnknown;
        else {
            knownSum = Math.fround(knownSum + v[i]);
            ++knownCount;
        }
    }
    if (!nUnknown || !knownCount)
        return;
    if (w >= 8 && h >= 8 && nUnknown > 64 && depth < 16) {
        const cw = (w + 1) >> 1, chh = (h + 1) >> 1;
        const cu = new Uint8Array(cw * chh).fill(1), cv = new Float32Array(cw * chh);
        for (let cy = 0; cy < chh; ++cy)
            for (let cx = 0; cx < cw; ++cx) {
                let s = 0, n = 0;
                for (const [dx, dy] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
                    const x = cx * 2 + dx, y = cy * 2 + dy;
                    if (x < w && y < h && !unknown[y * w + x]) {
                        s += v[y * w + x];
                        ++n;
                    }
                }
                if (n > 0) {
                    cu[cy * cw + cx] = 0;
                    cv[cy * cw + cx] = s / n;
                }
            }
        solveRec(cw, chh, cu, cv, depth + 1, stop);
        for (let y = 0; y < h; ++y) {
            const fy = Math.min(Math.max((y + 0.5) / 2 - 0.5, 0), chh - 1);
            const y0 = Math.floor(fy), y1 = Math.min(y0 + 1, chh - 1), ty = fy - y0;
            for (let x = 0; x < w; ++x) {
                if (!unknown[y * w + x])
                    continue;
                const fx = Math.min(Math.max((x + 0.5) / 2 - 0.5, 0), cw - 1);
                const x0 = Math.floor(fx), x1 = Math.min(x0 + 1, cw - 1), tx = fx - x0;
                const a = cv[y0 * cw + x0] + (cv[y0 * cw + x1] - cv[y0 * cw + x0]) * tx;
                const b = cv[y1 * cw + x0] + (cv[y1 * cw + x1] - cv[y1 * cw + x0]) * tx;
                v[y * w + x] = a + (b - a) * ty;
            }
        }
        sor(w, h, unknown, v, FINE_ITERS, FINE_OMEGA, stop);
    } else {
        const mean = knownSum / knownCount;
        for (let i = 0; i < w * h; ++i)
            if (unknown[i])
                v[i] = mean;
        sor(w, h, unknown, v, 2000, 1.85, stop);
    }
}

export function solveMembrane(w, h, unknown, v, stop = NEVER) {
    if (unknown.length !== w * h || v.length !== w * h)
        return;
    solveRec(w, h, unknown, v, 0, stop);
}

function perChannel(w, h, ch, f) {
    const n = w * h, out = new Float32Array(n * ch);
    for (let c = 0; c < ch; ++c) {
        const plane = f(c);
        for (let i = 0; i < n; ++i)
            out[i * ch + c] = plane[i];
    }
    return out;
}

// Clonado sin costuras: 'src' dentro de la máscara con los colores de 'dst' alrededor.
export function seamlessClone(w, h, ch, src, dst, mask, stop = NEVER) {
    stop.check();
    const n = w * h;
    const out = perChannel(w, h, ch, (c) => {
        const hv = new Float32Array(n);
        for (let i = 0; i < n; ++i)
            hv[i] = mask[i] ? 0 : dst[i * ch + c] - src[i * ch + c];
        solveRec(w, h, mask, hv, 0, stop);
        const r = new Float32Array(n);
        for (let i = 0; i < n; ++i)
            r[i] = mask[i] ? src[i * ch + c] + hv[i] : dst[i * ch + c];
        return r;
    });
    stop.check();
    return out;
}

// Rellena el hueco con la membrana armónica de su borde.
export function membraneFill(w, h, ch, img, hole, stop = NEVER) {
    stop.check();
    const n = w * h;
    const out = perChannel(w, h, ch, (c) => {
        const v = new Float32Array(n);
        for (let i = 0; i < n; ++i)
            v[i] = hole[i] ? 0 : img[i * ch + c];
        if (hole.length === n)
            solveRec(w, h, hole, v, 0, stop);
        return v;
    });
    stop.check();
    return out;
}

// ---------------------------------------------------------------------------------------------
// La curación: bytes RGBA de 8 bits (Uint8Array/Uint8ClampedArray) ↔ coma flotante.

export function toFloats(bytes) {
    const f = new Float32Array(bytes.length);
    for (let i = 0; i < bytes.length; ++i)
        f[i] = bytes[i] / 255;
    return f;
}

// ×255 y redondeado hacia arriba en la mitad; negativo (o NaN) 0, de más 255.
export function byteOf(v) {
    const t = Math.fround(v * 255) + 0.5;
    return t >= 255 ? 255 : t > 0 ? Math.trunc(t) : 0;
}

export function toBytes(v) {
    const b = new Uint8ClampedArray(v.length);
    for (let i = 0; i < v.length; ++i)
        b[i] = byteOf(v[i]);
    return b;
}

// Una curación sobre w × h: el clon 'src' encajado en 'dst' donde la cobertura 'cov' (0..255) no es 0.
export function heal(w, h, src, dst, cov, stop = NEVER) {
    const out = new Uint8ClampedArray(dst);
    let x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x)
            if (cov[y * w + x] > 0) {
                x0 = Math.min(x0, x); y0 = Math.min(y0, y);
                x1 = Math.max(x1, x + 1); y1 = Math.max(y1, y + 1);
            }
    if (x0 >= x1)
        return out;
    // La solución solo sobre la cobertura y un píxel más.
    x0 = Math.max(x0 - 1, 0); y0 = Math.max(y0 - 1, 0);
    x1 = Math.min(x1 + 1, w); y1 = Math.min(y1 + 1, h);
    const cw = x1 - x0, chh = y1 - y0;
    const s = new Float32Array(cw * chh * 4), d = new Float32Array(cw * chh * 4), m = new Uint8Array(cw * chh);
    for (let y = 0; y < chh; ++y)
        for (let x = 0; x < cw; ++x) {
            const from = (y + y0) * w + x + x0, to = y * cw + x;
            m[to] = cov[from] > 0 ? 1 : 0;
            for (let k = 0; k < 4; ++k) {
                s[to * 4 + k] = src[from * 4 + k] / 255;
                d[to * 4 + k] = dst[from * 4 + k] / 255;
            }
        }
    const healed = seamlessClone(cw, chh, 4, s, d, m, stop);
    for (let y = 0; y < chh; ++y)
        for (let x = 0; x < cw; ++x) {
            const from = y * cw + x, to = (y + y0) * w + x + x0;
            for (let k = 0; k < 4; ++k)
                out[to * 4 + k] = byteOf(healed[from * 4 + k]);
        }
    return out;
}

// La curación con la membrana 'step' veces más gruesa (la vista previa del Parche).
export function healCoarse(w, h, src, dst, cov, step, stop = NEVER) {
    if (step <= 1)
        return heal(w, h, src, dst, cov, stop);
    const N = 4, cw = Math.ceil(w / step), chh = Math.ceil(h / step);
    const diff = new Float32Array(cw * chh * N), hole = new Uint8Array(cw * chh).fill(1), count = new Uint32Array(cw * chh);
    for (let y = 0; y < h; ++y) {
        stop.check();
        for (let x = 0; x < w; ++x) {
            const i = y * w + x;
            if (cov[i] > 0)
                continue;
            const c = ((y / step) | 0) * cw + ((x / step) | 0);
            hole[c] = 0;
            ++count[c];
            for (let k = 0; k < N; ++k)
                diff[c * N + k] += dst[i * N + k] / 255 - src[i * N + k] / 255;
        }
    }
    for (let c = 0; c < count.length; ++c)
        if (count[c] > 0)
            for (let k = 0; k < N; ++k)
                diff[c * N + k] /= count[c];
    const membrane = membraneFill(cw, chh, N, diff, hole, stop);
    const lerpAt = (p, cells) => {
        const f = Math.min(Math.max((p + 0.5) / step - 0.5, 0), cells - 1), c0 = Math.floor(f);
        return [c0, Math.min(c0 + 1, cells - 1), f - c0];
    };
    const xs = [];
    for (let x = 0; x < w; ++x)
        xs.push(lerpAt(x, cw));
    const out = new Uint8ClampedArray(dst);
    for (let y = 0; y < h; ++y) {
        const [y0, y1, ty] = lerpAt(y, chh);
        for (let x = 0; x < w; ++x) {
            const i = y * w + x;
            if (cov[i] === 0)
                continue;
            const [x0, x1, tx] = xs[x];
            for (let k = 0; k < N; ++k) {
                const v = (cx, cy) => membrane[(cy * cw + cx) * N + k];
                const m = (v(x0, y0) * (1 - tx) + v(x1, y0) * tx) * (1 - ty) + (v(x0, y1) * (1 - tx) + v(x1, y1) * tx) * ty;
                out[i * N + k] = byteOf(src[i * N + k] / 255 + m);
            }
        }
    }
    return out;
}
