// Retoque de PhotoCraft (como core/retouch/Inpaint.cpp de Kaint): el relleno de huecos de Relleno
// según el contenido y el Pincel corrector puntual. Algoritmos de PhotoCraft (licencia MIT).
//
// · completeMasked: el completado multiescala de Wexler, Shechtman e Irani con el campo de vecinos
//   de PatchMatch (Barnes et al.); el nivel más grueso se rellena anillo a anillo desde el borde y,
//   con ajuste de color, cada parche copiado puede escalarse y desplazarse (Image Melding).
// · bestOffset: el desplazamiento cuyo anillo mejor coincide con el del hueco.
// · synthesize: síntesis de textura por bloques (Efros y Freeman) con cortes de error mínimo.
import { MixRng, NEVER, bandSeed, distanceTransform, membraneFill, seed64, seedAdd, seedXorShift } from "./Common.js";

export const Px = { Void: 0, Known: 1, Source: 2, Hole: 3 };

const SOURCE_MOMENTS_MAX = 1 << 20;
const MOM_CH = 8;
const FLAT = 0.02;
const NO_MATCH = -2147483648;
const THIN_RATIO = 4;
const PEEL_BUDGET = 2 ** 28;
const SOURCE_BLOCK = 64;

export const NO_ADAPT = { gainLo: 1, gainHi: 1, bias: 0 };
const adaptNone = (a) => a.gainLo === 1 && a.gainHi === 1 && a.bias === 0;

export function defaultCompleteParams() {
    return { patchRadius: 3, emIters: 10, minEmIters: 3, pmIters: 2, refineExtent: 192, adapt: NO_ADAPT, seed: 1 };
}

const bGrow = (b, r, w, h) => ({ x0: Math.max(b.x0 - r, 0), y0: Math.max(b.y0 - r, 0), x1: Math.min(b.x1 + r, w), y1: Math.min(b.y1 + r, h) });
const bHalf = (b) => ({ x0: b.x0 >> 1, y0: b.y0 >> 1, x1: (b.x1 + 1) >> 1, y1: (b.y1 + 1) >> 1 });
const bW = (b) => Math.max(b.x1 - b.x0, 0);
const bH = (b) => Math.max(b.y1 - b.y0, 0);
const bArea = (b) => bW(b) * bH(b);

function downsample(l, ch) {
    const w = (l.w + 1) >> 1, h = (l.h + 1) >> 1;
    const img = new Float32Array(w * h * ch), px = new Uint8Array(w * h);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            let n = 0, hole = false, allSource = true, any = false;
            const o = (y * w + x) * ch;
            for (const [dx, dy] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
                const sx = x * 2 + dx, sy = y * 2 + dy;
                if (sx >= l.w || sy >= l.h)
                    continue;
                const si = sy * l.w + sx, k = l.px[si];
                if (k === Px.Hole)
                    hole = true;
                else if (k === Px.Void)
                    allSource = false;
                else {
                    any = true;
                    allSource = allSource && k === Px.Source;
                    for (let c = 0; c < ch; ++c)
                        img[o + c] += l.img[si * ch + c];
                    n += 1;
                }
            }
            if (n > 0)
                for (let c = 0; c < ch; ++c)
                    img[o + c] /= n;
            px[y * w + x] = hole ? Px.Hole : !any ? Px.Void : allSource ? Px.Source : Px.Known;
        }
    return { w, h, img, px };
}

export function integral(w, h, m) {
    const s = new Uint32Array((w + 1) * (h + 1));
    for (let y = 0; y < h; ++y) {
        let row = 0;
        for (let x = 0; x < w; ++x) {
            row += m[y * w + x] ? 1 : 0;
            s[(y + 1) * (w + 1) + x + 1] = s[y * (w + 1) + x + 1] + row;
        }
    }
    return s;
}

export function windowCount(s, gw, gh, x0, y0, x1, y1) {
    x0 = Math.max(x0, 0); y0 = Math.max(y0, 0);
    x1 = Math.min(Math.max(x1, 0), gw); y1 = Math.min(Math.max(y1, 0), gh);
    if (x1 <= x0 || y1 <= y0)
        return 0;
    const w1 = gw + 1;
    return s[y1 * w1 + x1] + s[y0 * w1 + x0] - s[y0 * w1 + x1] - s[y1 * w1 + x0];
}

function holeBBox(w, h, hole) {
    let b = null;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x)
            if (hole[y * w + x]) {
                if (!b)
                    b = { x0: x, y0: y, x1: x + 1, y1: y + 1 };
                else {
                    b.x0 = Math.min(b.x0, x); b.y0 = Math.min(b.y0, y);
                    b.x1 = Math.max(b.x1, x + 1); b.y1 = Math.max(b.y1, y + 1);
                }
            }
    return b;
}

function holeThickness(w, hb, px) {
    const gw = bW(hb) + 2, gh = bH(hb) + 2;
    const outside = new Uint8Array(gw * gh);
    for (let gy = 0; gy < gh; ++gy)
        for (let gx = 0; gx < gw; ++gx)
            outside[gy * gw + gx] = gx === 0 || gy === 0 || gx > bW(hb) || gy > bH(hb)
                || px[(hb.y0 + gy - 1) * w + hb.x0 + gx - 1] !== Px.Hole ? 1 : 0;
    let deepest = 0;
    for (const d of distanceTransform(outside, gw, gh))
        deepest = Math.max(deepest, d);
    return Math.ceil(2 * deepest);
}

function peelAffordable(w, px, hb, sources) {
    let holes = 0;
    for (let y = hb.y0; y < hb.y1; ++y)
        for (let x = hb.x0; x < hb.x1; ++x)
            if (px[y * w + x] === Px.Hole)
                ++holes;
    return holes * sources <= PEEL_BUDGET;
}

function holeBounds(w, h, px) {
    let b = null;
    for (let y = 0; y < h; ++y) {
        let x0 = -1, x1 = -1;
        for (let x = 0; x < w; ++x)
            if (px[y * w + x] === Px.Hole) {
                if (x0 < 0)
                    x0 = x;
                x1 = x + 1;
            }
        if (x0 < 0)
            continue;
        if (!b)
            b = { x0, y0: y, x1, y1: y + 1 };
        else {
            b.x0 = Math.min(b.x0, x0);
            b.x1 = Math.max(b.x1, x1);
            b.y1 = y + 1;
        }
    }
    return b;
}

function validSources(w, h, r, px, stop) {
    const side = 2 * r + 1;
    const run = new Uint8Array(w * h);
    if (w < side || h < side)
        return run;
    for (let y = 0; y < h; ++y) {
        if (y % 64 === 0)
            stop.check();
        const row = y * w;
        let count = 0;
        for (let x = 0; x < side; ++x)
            count += px[row + x] === Px.Source ? 1 : 0;
        run[row + r] = count === side ? 1 : 0;
        for (let x = r + 1; x < w - r; ++x) {
            count += (px[row + x + r] === Px.Source ? 1 : 0) - (px[row + x - r - 1] === Px.Source ? 1 : 0);
            run[row + x] = count === side ? 1 : 0;
        }
    }
    const valid = new Uint8Array(w * h);
    const stack = new Int32Array(w);
    for (let yy = 0; yy < h; ++yy) {
        for (let x = 0; x < w; ++x)
            stack[x] = run[yy * w + x] ? stack[x] + 1 : 0;
        if (yy < 2 * r)
            continue;
        const y = yy - r;
        for (let x = r; x < w - r; ++x)
            valid[y * w + x] = stack[x] >= side ? 1 : 0;
    }
    stop.check();
    return valid;
}

// Las fuentes válidas, para sacar la k-ésima en orden de barrido sin guardarlas todas.
class Sources {
    constructor(valid, w) {
        const h = w > 0 ? (valid.length / w) | 0 : 0;
        this.blocks = Math.ceil(w / SOURCE_BLOCK);
        this.blockStart = new Uint32Array(h * this.blocks);
        this.rowStart = new Float64Array(h + 1);
        let total = 0;
        for (let y = 0; y < h; ++y) {
            let n = 0;
            for (let b = 0; b < this.blocks; ++b) {
                this.blockStart[y * this.blocks + b] = n;
                const xe = Math.min((b + 1) * SOURCE_BLOCK, w);
                for (let x = b * SOURCE_BLOCK; x < xe; ++x)
                    n += valid[y * w + x];
            }
            total += n;
            this.rowStart[y + 1] = total;
        }
        this.total = total;
    }
    get size() {
        return this.total;
    }
    nth(k, valid, w) {
        // La fila: la última cuyo comienzo es ≤ k.
        let lo = 0, hi = this.rowStart.length;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (this.rowStart[mid] <= k) lo = mid + 1;
            else hi = mid;
        }
        const y = Math.max(lo - 1, 0);
        let left = k - this.rowStart[y];
        const base = y * this.blocks;
        let blo = 0, bhi = this.blocks;
        while (blo < bhi) {
            const mid = (blo + bhi) >> 1;
            if (this.blockStart[base + mid] <= left) blo = mid + 1;
            else bhi = mid;
        }
        const b = Math.max(blo - 1, 0);
        left -= this.blockStart[base + b];
        for (let x = b * SOURCE_BLOCK; x < w; ++x)
            if (valid[y * w + x]) {
                if (left === 0)
                    return [x, y];
                --left;
            }
        return [0, 0];
    }
    all(valid, w) {
        const xs = new Int32Array(this.total), ys = new Int32Array(this.total);
        let k = 0;
        for (let i = 0; i < valid.length; ++i)
            if (valid[i]) {
                xs[k] = i % w;
                ys[k] = (i / w) | 0;
                ++k;
            }
        return { xs, ys };
    }
}

function targetsAround(w, r, px, hb, tb) {
    const bw = bW(hb), bh = bH(hb);
    const s = new Uint32Array((bw + 1) * (bh + 1));
    for (let y = 0; y < bh; ++y) {
        let row = 0;
        for (let x = 0; x < bw; ++x) {
            row += px[(hb.y0 + y) * w + hb.x0 + x] === Px.Hole ? 1 : 0;
            s[(y + 1) * (bw + 1) + x + 1] = s[y * (bw + 1) + x + 1] + row;
        }
    }
    const targets = new Uint8Array(bArea(tb));
    for (let y = tb.y0; y < tb.y1; ++y)
        for (let x = tb.x0; x < tb.x1; ++x)
            targets[(y - tb.y0) * bW(tb) + (x - tb.x0)] =
                windowCount(s, bw, bh, x - r - hb.x0, y - r - hb.y0, x + r + 1 - hb.x0, y + r + 1 - hb.y0) > 0 ? 1 : 0;
    return targets;
}

export function fitGainBias(a, n, st, stt, ss, sss) {
    const mt = st / n, ms = ss / n;
    const vt = Math.max(stt / n - mt * mt, 0), vs = Math.max(sss / n - ms * ms, 0);
    const g0 = vs > 1e-10 ? Math.sqrt(vt / vs) : 1;
    let lo = a.gainLo, hi = a.gainHi;
    if (Math.max(vt, vs) < FLAT * FLAT && Math.abs(mt - ms) <= a.bias && ms > 1e-6) {
        const glo = (mt - a.bias) / ms, ghi = (mt + a.bias) / ms;
        if (Math.max(glo, lo) <= Math.min(ghi, hi)) {
            lo = Math.max(glo, lo);
            hi = Math.min(ghi, hi);
        }
    }
    const g = Math.min(Math.max(g0, lo), hi);
    const b = Math.min(Math.max(mt - g * ms, -a.bias), a.bias);
    return [g, b];
}

function patchMoments(img, w, ch, c8, side, x0, y0, out, o) {
    for (let c = 0; c < c8; ++c)
        out[o + c * 2] = out[o + c * 2 + 1] = 0;
    for (let y = y0; y < y0 + side; ++y) {
        const row = (y * w + x0) * ch;
        for (let p = 0; p < side; ++p)
            for (let c = 0; c < c8; ++c) {
                const a = img[row + p * ch + c];
                out[o + c * 2] += a;
                out[o + c * 2 + 1] += a * a;
            }
    }
}

function adaptedCost(a, nf, m, chn) {
    let sum = 0;
    for (let c = 0; c < chn; ++c) {
        const k = c * 5;
        const [g, b] = fitGainBias(a, nf, m[k], m[k + 1], m[k + 2], m[k + 3]);
        sum += m[k + 1] + g * g * m[k + 3] + nf * b * b - 2 * g * m[k + 4] - 2 * b * m[k] + 2 * g * b * m[k + 2];
    }
    return Math.max(sum, 0) / (nf * chn);
}

class Solver {
    constructor(o) {
        Object.assign(this, o);
        this.c8 = Math.min(this.ch, MOM_CH);
        this.mbuf = new Float64Array(MOM_CH * 5);
    }

    isValid(x, y) {
        return x >= 0 && y >= 0 && x < this.w && y < this.h && this.valid[y * this.w + x] === 1;
    }

    prepare(keepSourceMoments) {
        const { w, h, r, tbox: tb, px } = this, side = 2 * r + 1, tw = bW(tb);
        this.full = new Uint8Array(bArea(tb));
        for (let ty = 0; ty < bH(tb); ++ty) {
            const y = tb.y0 + ty;
            if (y < r || y + r >= h)
                continue;
            for (let tx = 0; tx < tw; ++tx) {
                const x = tb.x0 + tx;
                if (x < r || x + r >= w)
                    continue;
                let all = true;
                for (let yy = y - r; yy <= y + r && all; ++yy)
                    for (let k = 0; k < side; ++k)
                        if (px[yy * w + x - r + k] === Px.Void) {
                            all = false;
                            break;
                        }
                this.full[ty * tw + tx] = all ? 1 : 0;
            }
        }
        this.tmom = null;
        this.smom = null;
        if (adaptNone(this.adapt))
            return;
        this.tmom = new Float64Array(bArea(tb) * this.c8 * 2);
        if (!keepSourceMoments)
            return;
        this.smom = new Float64Array(w * h * this.c8 * 2);
        for (let y = 0; y < h; ++y) {
            if (y % 32 === 0)
                this.stop.check();
            for (let x = 0; x < w; ++x)
                if (this.valid[y * w + x])
                    patchMoments(this.img, w, this.ch, this.c8, side, x - r, y - r, this.smom, (y * w + x) * this.c8 * 2);
        }
    }

    dist(tx, ty, k, sx, sy, cutoff) {
        const f = this.full[k];
        if (!adaptNone(this.adapt))
            return f ? this.distAdaptedFull(k, tx, ty, sx, sy) : this.distAdapted(tx, ty, sx, sy);
        return f ? this.distFull(tx, ty, sx, sy, cutoff) : this.distClipped(tx, ty, sx, sy, cutoff);
    }

    distClipped(tx0, ty0, sx, sy0, cutoff) {
        const { w, h, ch, r, img, px } = this;
        const fullN = (2 * r + 1) * (2 * r + 1) * ch;
        let sum = 0, n = 0;
        for (let dy = -r; dy <= r; ++dy) {
            const ty = ty0 + dy;
            if (ty < 0 || ty >= h)
                continue;
            const sy = sy0 + dy;
            for (let dx = -r; dx <= r; ++dx) {
                const tx = tx0 + dx;
                if (tx < 0 || tx >= w || px[ty * w + tx] === Px.Void)
                    continue;
                const ti = (ty * w + tx) * ch, si = (sy * w + (sx + dx)) * ch;
                for (let c = 0; c < ch; ++c) {
                    const d = img[ti + c] - img[si + c];
                    sum += d * d;
                }
                n += ch;
            }
            if (sum > cutoff * fullN)
                return Infinity;
        }
        return n === 0 ? Infinity : sum / n;
    }

    distFull(tx, ty, sx, sy, cutoff) {
        const { w, ch, r, img } = this, side = 2 * r + 1, span = side * ch;
        const limit = cutoff * side * side * ch;
        let sum = 0;
        for (let dy = -r; dy <= r; ++dy) {
            const ti = ((ty + dy) * w + (tx - r)) * ch, si = ((sy + dy) * w + (sx - r)) * ch;
            for (let p = 0; p < span; p += ch) {
                for (let c = 0; c < ch; ++c) {
                    const d = img[ti + p + c] - img[si + p + c];
                    sum += d * d;
                }
                if (sum > limit)
                    return Infinity;
            }
        }
        return sum / (side * side * ch);
    }

    moments(tx0, ty0, sx, sy0, m, chn) {
        const { w, h, ch, r, img, px } = this;
        m.fill(0, 0, chn * 5);
        let n = 0;
        for (let dy = -r; dy <= r; ++dy) {
            const ty = ty0 + dy;
            if (ty < 0 || ty >= h)
                continue;
            const sy = sy0 + dy;
            for (let dx = -r; dx <= r; ++dx) {
                const tx = tx0 + dx;
                if (tx < 0 || tx >= w || px[ty * w + tx] === Px.Void)
                    continue;
                const ti = (ty * w + tx) * ch, si = (sy * w + (sx + dx)) * ch;
                for (let c = 0; c < chn; ++c) {
                    const a = img[ti + c], b = img[si + c], k = c * 5;
                    m[k] += a; m[k + 1] += a * a; m[k + 2] += b; m[k + 3] += b * b; m[k + 4] += a * b;
                }
                ++n;
            }
        }
        return n;
    }

    momentsFull(k, tx, ty, sx, sy, m, chn) {
        const { w, ch, r, img } = this, side = 2 * r + 1;
        const to = k * chn * 2, haveS = !!this.smom, so = (sy * w + sx) * chn * 2;
        const acc = new Float64Array(chn * 3);
        for (let dy = -r; dy <= r; ++dy) {
            const trow = ((ty + dy) * w + (tx - r)) * ch, srow = ((sy + dy) * w + (sx - r)) * ch;
            for (let p = 0; p < side; ++p) {
                const tp = trow + p * ch, sp = srow + p * ch;
                for (let c = 0; c < chn; ++c) {
                    const a = img[tp + c], b = img[sp + c];
                    if (!haveS) {
                        acc[c * 3] += b;
                        acc[c * 3 + 1] += b * b;
                    }
                    acc[c * 3 + 2] += a * b;
                }
            }
        }
        for (let c = 0; c < chn; ++c) {
            const q = c * 5;
            m[q] = this.tmom[to + c * 2];
            m[q + 1] = this.tmom[to + c * 2 + 1];
            m[q + 2] = haveS ? this.smom[so + c * 2] : acc[c * 3];
            m[q + 3] = haveS ? this.smom[so + c * 2 + 1] : acc[c * 3 + 1];
            m[q + 4] = acc[c * 3 + 2];
        }
    }

    distAdapted(tx, ty, sx, sy) {
        const n = this.moments(tx, ty, sx, sy, this.mbuf, this.c8);
        return n === 0 ? Infinity : adaptedCost(this.adapt, n, this.mbuf, this.c8);
    }

    distAdaptedFull(k, tx, ty, sx, sy) {
        this.momentsFull(k, tx, ty, sx, sy, this.mbuf, this.c8);
        const side = 2 * this.r + 1;
        return adaptedCost(this.adapt, side * side, this.mbuf, this.c8);
    }

    transform(tx, ty, k, sx, sy, out, o) {
        const ch = this.ch;
        if (adaptNone(this.adapt)) {
            for (let c = 0; c < ch; ++c) {
                out[o + c * 2] = 1;
                out[o + c * 2 + 1] = 0;
            }
            return;
        }
        const m = this.mbuf, c8 = this.c8;
        let n;
        if (this.full[k]) {
            this.momentsFull(k, tx, ty, sx, sy, m, c8);
            n = (2 * this.r + 1) ** 2;
        } else {
            n = this.moments(tx, ty, sx, sy, m, c8);
        }
        for (let c = 0; c < ch; ++c) {
            if (n === 0 || c >= c8) {
                out[o + c * 2] = 1;
                out[o + c * 2 + 1] = 0;
            } else {
                const [g, b] = fitGainBias(this.adapt, n, m[c * 5], m[c * 5 + 1], m[c * 5 + 2], m[c * 5 + 3]);
                out[o + c * 2] = g;
                out[o + c * 2 + 1] = b;
            }
        }
    }

    // Una pasada de PatchMatch por bandas de 16 filas (la propagación no sale de la banda).
    patchmatch(nx, ny, cost, pass, seed, searchRadius) {
        const tb = this.tbox, tw = bW(tb);
        if (!tw || !bH(tb))
            return;
        const reverse = pass % 2 === 1, step = reverse ? -1 : 1;
        for (let y = tb.y0; y < tb.y1;) {
            const bi = Math.floor(y / 16), end = Math.min((bi + 1) * 16, tb.y1), rows = end - y, ys = y;
            y = end;
            this.stop.check();
            const s = bandSeed(seed, bi, pass), rng = new MixRng(s[0], s[1]);
            const base = (ys - tb.y0) * tw;
            for (let ry = 0; ry < rows; ++ry) {
                const ly = reverse ? rows - 1 - ry : ry, yy = ys + ly;
                for (let rx = 0; rx < tw; ++rx) {
                    const lx = reverse ? tw - 1 - rx : rx, li = base + ly * tw + lx;
                    if (!this.targets[li])
                        continue;
                    const tx = tb.x0 + lx;
                    let bx = nx[li], by = ny[li], bc = cost[li];
                    const tryCand = (cx, cy) => {
                        if (this.isValid(cx, cy) && (cx !== bx || cy !== by)) {
                            const d = this.dist(tx, yy, li, cx, cy, bc);
                            if (d < bc) {
                                bx = cx; by = cy; bc = d;
                            }
                        }
                    };
                    const nxl = lx - step;
                    if (nxl >= 0 && nxl < tw) {
                        const q = base + ly * tw + nxl;
                        tryCand(nx[q] + step, ny[q]);
                    }
                    const nly = ly - step;
                    if (nly >= 0 && nly < rows) {
                        const q = base + nly * tw + lx;
                        tryCand(nx[q], ny[q] + step);
                    }
                    for (let rad = searchRadius; rad >= 1; rad = Math.floor(rad / 2)) {
                        const cx = bx + rng.range(-rad, rad), cy = by + rng.range(-rad, rad);
                        tryCand(cx, cy);
                    }
                    nx[li] = bx;
                    ny[li] = by;
                    cost[li] = bc;
                }
            }
        }
    }

    costs(nx, ny, cost) {
        const tb = this.tbox, tw = bW(tb), r = this.r, side = 2 * r + 1;
        this.stop.check();
        if (!adaptNone(this.adapt))
            for (let ty = 0; ty < bH(tb); ++ty)
                for (let tx = 0; tx < tw; ++tx)
                    if (this.full[ty * tw + tx])
                        patchMoments(this.img, this.w, this.ch, this.c8, side, tb.x0 + tx - r, tb.y0 + ty - r, this.tmom,
                                     (ty * tw + tx) * this.c8 * 2);
        for (let ty = 0; ty < bH(tb); ++ty) {
            if (ty % 32 === 0)
                this.stop.check();
            for (let tx = 0; tx < tw; ++tx) {
                const k = ty * tw + tx;
                if (this.targets[k])
                    cost[k] = this.dist(tb.x0 + tx, tb.y0 + ty, k, nx[k], ny[k], Infinity);
            }
        }
    }

    vote(nx, ny, cost) {
        const { w, h, ch, r, img, px } = this, tb = this.tbox, hb = this.holeBox, tw = bW(tb);
        const used = (k) => this.targets[k] && Number.isFinite(cost[k]);
        const cs = [];
        for (let k = 0; k < bArea(tb); ++k)
            if (used(k))
                cs.push(cost[k]);
        let sigma2 = 1;
        if (cs.length) {
            cs.sort((a, b) => a - b);
            sigma2 = Math.max(cs[Math.min(Math.floor((cs.length * 3) / 4), cs.length - 1)], 1e-6);
        }
        const adapted = !adaptNone(this.adapt);
        const wt = new Float32Array(bArea(tb)), src = new Float64Array(bArea(tb));
        for (let k = 0; k < bArea(tb); ++k)
            if (used(k)) {
                wt[k] = Math.max(Math.exp(-cost[k] / (2 * sigma2)), 1e-8);
                src[k] = ny[k] * w + nx[k];
            }
        let gb = null;
        if (adapted) {
            gb = new Float64Array(bArea(tb) * ch * 2);
            for (let ty = 0; ty < bH(tb); ++ty)
                for (let tx = 0; tx < tw; ++tx) {
                    const k = ty * tw + tx;
                    if (used(k))
                        this.transform(tb.x0 + tx, tb.y0 + ty, k, nx[k], ny[k], gb, k * ch * 2);
                }
        }
        const hw = bW(hb), out = new Float32Array(bArea(hb) * ch), got = new Uint8Array(bArea(hb));
        const acc = new Float64Array(ch);
        for (let yy = 0; yy < bH(hb); ++yy) {
            if (yy % 32 === 0)
                this.stop.check();
            const y = hb.y0 + yy;
            for (let x = hb.x0; x < hb.x1; ++x) {
                if (px[y * w + x] !== Px.Hole)
                    continue;
                acc.fill(0);
                let wsum = 0;
                for (let ty = Math.max(y - r, 0); ty <= Math.min(y + r, h - 1); ++ty) {
                    const dy = ty - y, krow = (ty - tb.y0) * tw;
                    for (let tx = Math.max(x - r, 0); tx <= Math.min(x + r, w - 1); ++tx) {
                        const k = krow + tx - tb.x0, wk = wt[k];
                        if (wk === 0)
                            continue;
                        const si = (src[k] - dy * w - (tx - x)) * ch;
                        for (let c = 0; c < ch; ++c) {
                            let v = img[si + c];
                            if (adapted)
                                v = gb[(k * ch + c) * 2] * v + gb[(k * ch + c) * 2 + 1];
                            acc[c] += v * wk;
                        }
                        wsum += wk;
                    }
                }
                if (wsum > 0) {
                    const o = (yy * hw + (x - hb.x0)) * ch;
                    for (let c = 0; c < ch; ++c)
                        out[o + c] = acc[c] / wsum;
                    got[yy * hw + (x - hb.x0)] = 1;
                }
            }
        }
        for (let yy = 0; yy < bH(hb); ++yy)
            for (let xx = 0; xx < hw; ++xx)
                if (got[yy * hw + xx]) {
                    const d = ((hb.y0 + yy) * w + hb.x0 + xx) * ch, s = (yy * hw + xx) * ch;
                    for (let c = 0; c < ch; ++c)
                        img[d + c] = out[s + c];
                }
    }
}

// Rellena el hueco anillo a anillo desde el borde: cada píxel toma el centro del parche fuente que
// mejor coincide con lo ya conocido a su alrededor (el primero de los que empatan).
function onionPeel(w, h, ch, r, img, px, hb, sources, stop) {
    const NONE = -1;
    const known = new Uint8Array(px.length);
    for (let i = 0; i < px.length; ++i)
        known[i] = px[i] === Px.Known || px[i] === Px.Source ? 1 : 0;
    const index = new Int32Array(w * h).fill(NONE);
    const n = sources.xs.length;
    for (let j = 0; j < n; ++j)
        index[sources.ys[j] * w + sources.xs[j]] = j;
    const took = new Int32Array(w * h).fill(NONE);
    const nb = [[-1, -1], [0, -1], [1, -1], [-1, 0], [1, 0], [-1, 1], [0, 1], [1, 1]];
    for (;;) {
        stop.check();
        const front = [];
        for (let y = hb.y0; y < hb.y1; ++y)
            for (let x = hb.x0; x < hb.x1; ++x) {
                const i = y * w + x;
                if (known[i] || px[i] !== Px.Hole)
                    continue;
                let edge = false;
                for (let dy = -1; dy <= 1 && !edge; ++dy)
                    for (let dx = -1; dx <= 1; ++dx) {
                        const nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && known[ny * w + nx]) {
                            edge = true;
                            break;
                        }
                    }
                if (edge)
                    front.push(i);
            }
        if (!front.length)
            break;
        const vals = new Float32Array(front.length * ch), picks = new Int32Array(front.length).fill(NONE);
        const offs = [], tv = [];
        for (let f = 0; f < front.length; ++f) {
            if (f % 64 === 0)
                stop.check();
            const i = front[f], x = i % w, y = (i / w) | 0;
            offs.length = 0;
            tv.length = 0;
            for (let dy = -r; dy <= r; ++dy)
                for (let dx = -r; dx <= r; ++dx) {
                    const tx = x + dx, ty = y + dy;
                    if (tx >= 0 && ty >= 0 && tx < w && ty < h && known[ty * w + tx]) {
                        const ti = (ty * w + tx) * ch;
                        offs.push(dy * w + dx);
                        for (let c = 0; c < ch; ++c)
                            tv.push(img[ti + c]);
                    }
                }
            let bv = Infinity, bj = -1;
            if (offs.length) {
                const nf = offs.length;
                const consider = (j) => {
                    if (j < 0 || j >= n)
                        return;
                    const base = sources.ys[j] * w + sources.xs[j];
                    // La suma solo crece: se deja en cuanto ya no puede ganar (empatar solo gana
                    // con un índice menor).
                    const later = bj >= 0 && j > bj;
                    let e = 0;
                    for (let o = 0; o < nf; ++o) {
                        const si = (base + offs[o]) * ch;
                        for (let c = 0; c < ch; ++c) {
                            const d = tv[o * ch + c] - img[si + c];
                            e += d * d;
                        }
                        const v = e / nf;
                        if (v > bv || (later && v >= bv))
                            return;
                    }
                    const v = e / nf;
                    if (v < Infinity && (v < bv || (v === bv && j < bj))) {
                        bv = v;
                        bj = j;
                    }
                };
                for (const [dx, dy] of nb) {
                    const qx = x + dx, qy = y + dy;
                    if (qx < 0 || qy < 0 || qx >= w || qy >= h)
                        continue;
                    const t = took[qy * w + qx];
                    if (t < 0)
                        continue;
                    const cx = sources.xs[t] - dx, cy = sources.ys[t] - dy;
                    if (cx >= 0 && cy >= 0 && cx < w && cy < h && index[cy * w + cx] !== NONE)
                        consider(index[cy * w + cx]);
                }
                if (bj < 0)
                    for (let j = 0; j < n; j += 16)
                        consider(j);
                for (let j = 0; j < n; ++j)
                    consider(j);
            }
            const sx = bj >= 0 ? sources.xs[bj] : 0, sy = bj >= 0 ? sources.ys[bj] : 0;
            const si = (sy * w + sx) * ch;
            for (let c = 0; c < ch; ++c)
                vals[f * ch + c] = img[si + c];
            picks[f] = bj;
        }
        stop.check();
        for (let f = 0; f < front.length; ++f) {
            const i = front[f];
            for (let c = 0; c < ch; ++c)
                img[i * ch + c] = vals[f * ch + c];
            known[i] = 1;
            took[i] = picks[f];
        }
    }
}

// Completa el hueco (los píxeles Hole) de 'image' (se cambia). Falso, sin tocarla, si no hay de
// dónde copiar.
export function completeMasked(w, h, ch, image, px, p, stop = NEVER) {
    const hb0 = holeBounds(w, h, px);
    if (!hb0 || ch === 0)
        return true;
    const r = Math.max(p.patchRadius, 1), psz = 2 * r + 1;
    const levels = [{ w, h, img: new Float32Array(image), px }];
    const boxes = [hb0];
    let ext = Math.max(bW(hb0), bH(hb0));
    const thickness = holeThickness(w, hb0, px);
    const thin = thickness * THIN_RATIO <= ext;
    if (thin)
        ext = thickness;
    const exts = [ext];
    for (;;) {
        stop.check();
        const l = levels[levels.length - 1];
        if (ext <= (thin ? 2 * psz : 4 * psz) || (l.w >> 1) < 3 * psz || (l.h >> 1) < 3 * psz)
            break;
        levels.push(downsample(l, ch));
        boxes.push(bHalf(boxes[boxes.length - 1]));
        ext = Math.ceil(ext / 2);
        exts.push(ext);
    }
    const nlev = levels.length;
    const seed = seed64(p.seed);
    let prev = null;
    for (let li = nlev - 1; li >= 0; --li) {
        stop.check();
        const lv = levels[li];
        levels[li] = null;
        const lw = lv.w, lh = lv.h, lpx = lv.px, hb = boxes[li], tb = bGrow(hb, r, lw, lh), tw = bW(tb);
        const valid = validSources(lw, lh, r, lpx, stop);
        const sources = new Sources(valid, lw);
        if (li === 0 && sources.size === 0)
            return false;
        const limg = lv.img;
        const targets = targetsAround(lw, r, lpx, hb, tb);
        stop.check();
        const coarser = prev;
        prev = null;
        if (coarser) {
            for (let y = hb.y0; y < hb.y1; ++y)
                for (let x = hb.x0; x < hb.x1; ++x)
                    if (lpx[y * lw + x] === Px.Hole) {
                        const sx = Math.min(x >> 1, coarser.w - 1), sy = Math.min(y >> 1, coarser.h - 1);
                        const d = (y * lw + x) * ch, s = (sy * coarser.w + sx) * ch;
                        for (let c = 0; c < ch; ++c)
                            limg[d + c] = coarser.img[s + c];
                    }
        } else if (sources.size > 0 && (!thin || peelAffordable(lw, lpx, hb, sources.size))) {
            onionPeel(lw, lh, ch, r, limg, lpx, hb, sources.all(valid, lw), stop);
        } else {
            const unknown = new Uint8Array(lpx.length);
            for (let i = 0; i < lpx.length; ++i)
                unknown[i] = lpx[i] === Px.Hole || lpx[i] === Px.Void ? 1 : 0;
            if (thin) {
                const hole = new Uint8Array(lpx.length);
                for (let i = 0; i < lpx.length; ++i)
                    hole[i] = lpx[i] === Px.Hole ? 1 : 0;
                const d = distanceTransform(hole, lw, lh);
                for (let i = 0; i < unknown.length; ++i)
                    if (d[i] <= r)
                        unknown[i] = 1;
            }
            const m = membraneFill(lw, lh, ch, limg, unknown);
            for (let i = 0; i < lpx.length; ++i)
                if (lpx[i] === Px.Hole)
                    for (let c = 0; c < ch; ++c)
                        limg[i * ch + c] = m[i * ch + c];
        }
        if (sources.size === 0) {
            prev = { w: lw, h: lh, img: limg, fx: null, fy: null, tbox: tb };
            continue;
        }
        const refineOnly = li + 1 < nlev && exts[li] > p.refineExtent;
        let nTargets = 0;
        for (const t of targets)
            nTargets += t;
        const keepSourceMoments = lw * lh <= SOURCE_MOMENTS_MAX && sources.size <= nTargets * (refineOnly ? 4 : 64);
        const solver = new Solver({ w: lw, h: lh, ch, r, img: limg, px: lpx, valid, targets, holeBox: hb, tbox: tb,
                                    adapt: p.adapt, stop });
        solver.prepare(keepSourceMoments);
        stop.check();
        const ls = seedXorShift(seed, li, 20), rng = new MixRng(ls[0], ls[1]);
        const nx = new Int32Array(bArea(tb)), ny = new Int32Array(bArea(tb));
        let upsampled = false;
        for (let y = tb.y0; y < tb.y1; ++y) {
            if (y % 32 === 0)
                stop.check();
            for (let x = tb.x0; x < tb.x1; ++x) {
                const k = (y - tb.y0) * tw + (x - tb.x0);
                if (!targets[k])
                    continue;
                let has = false;
                if (coarser && coarser.fx) {
                    const cx = Math.min(x >> 1, coarser.w - 1), cy = Math.min(y >> 1, coarser.h - 1);
                    const t = coarser.tbox;
                    if (cx >= t.x0 && cx < t.x1 && cy >= t.y0 && cy < t.y1) {
                        const q = (cy - t.y0) * bW(t) + (cx - t.x0), sx = coarser.fx[q], sy = coarser.fy[q];
                        if (sx !== NO_MATCH) {
                            const ax = x + 2 * (sx - cx), ay = y + 2 * (sy - cy);
                            if (solver.isValid(ax, ay)) {
                                nx[k] = ax;
                                ny[k] = ay;
                                has = true;
                            }
                        }
                    }
                }
                upsampled = upsampled || has;
                if (!has) {
                    const [sx, sy] = sources.nth(rng.nextMod(sources.size), valid, lw);
                    nx[k] = sx;
                    ny[k] = sy;
                }
            }
        }
        const cost = new Float64Array(bArea(tb)).fill(Infinity);
        if (upsampled) {
            solver.costs(nx, ny, cost);
            solver.vote(nx, ny, cost);
        }
        const em = li + 1 === nlev ? p.emIters : refineOnly ? 1 : Math.max(p.emIters >> (nlev - 1 - li), p.minEmIters);
        const pmIters = refineOnly ? 1 : p.pmIters;
        const fullSearch = Math.max(lw, lh);
        for (let it = 0; it < em; ++it) {
            solver.costs(nx, ny, cost);
            const radius = refineOnly ? 2 : li + 1 === nlev || it === 0 ? fullSearch : Math.min(4 * psz, fullSearch);
            for (let pass = 0; pass < pmIters; ++pass) {
                stop.check();
                solver.patchmatch(nx, ny, cost, pass + it * pmIters, seedAdd(seed, li * 1000 + it), radius);
            }
            stop.check();
            solver.vote(nx, ny, cost);
            stop.check();
        }
        let fx = null, fy = null;
        if (li !== 0) {
            fx = new Int32Array(nx.length);
            fy = new Int32Array(ny.length);
            for (let k = 0; k < nx.length; ++k) {
                fx[k] = targets[k] ? nx[k] : NO_MATCH;
                fy[k] = targets[k] ? ny[k] : NO_MATCH;
            }
        }
        prev = { w: lw, h: lh, img: limg, fx, fy, tbox: tb };
    }
    if (prev)
        image.set(prev.img);
    return true;
}

// Con todo lo que no es hueco como fuente; null si no hay de dónde copiar.
export function complete(w, h, ch, img, hole, p = defaultCompleteParams(), stop = NEVER) {
    const px = new Uint8Array(hole.length);
    for (let i = 0; i < hole.length; ++i)
        px[i] = hole[i] ? Px.Hole : Px.Source;
    const out = new Float32Array(img);
    return completeMasked(w, h, ch, out, px, p, stop) ? out : null;
}

// Coincidencia por proximidad: el desplazamiento (dentro de maxRadius) cuyo alrededor mejor
// coincide con el anillo de 'ring' píxeles del hueco.
export function bestOffset(w, h, ch, img, hole, ring, maxRadius) {
    const bb = holeBBox(w, h, hole);
    if (!bb)
        return null;
    const extent = Math.min(Math.max(w, h), Math.floor(2147483647 / 4));
    maxRadius = Math.min(Math.max(maxRadius, 0), extent);
    if (!maxRadius)
        return null;
    ring = Math.min(Math.max(ring, 1), extent);
    const hw = bW(bb), hh = bH(bb), hs = new Uint32Array((hw + 1) * (hh + 1));
    for (let y = 0; y < hh; ++y) {
        let count = 0;
        for (let x = 0; x < hw; ++x) {
            count += hole[(bb.y0 + y) * w + bb.x0 + x] ? 1 : 0;
            hs[(y + 1) * (hw + 1) + x + 1] = hs[y * (hw + 1) + x + 1] + count;
        }
    }
    const holeCount = (x0, y0, x1, y1) => windowCount(hs, hw, hh, x0 - bb.x0, y0 - bb.y0, x1 - bb.x0, y1 - bb.y0);
    let pts = [];
    const x0 = bb.x0 - ring, y0 = bb.y0 - ring, x1 = bb.x1 + ring, y1 = bb.y1 + ring;
    for (let y = Math.max(y0, 0); y < Math.min(y1, h); ++y)
        for (let x = Math.max(x0, 0); x < Math.min(x1, w); ++x)
            if (!hole[y * w + x] && holeCount(x - ring, y - ring, x + ring + 1, y + ring + 1) > 0)
                pts.push(x, y);
    const total = pts.length / 2, stride = Math.max(Math.floor(total / 2000), 1);
    if (stride > 1) {
        const s = [];
        for (let i = 0; i < total; i += stride)
            s.push(pts[i * 2], pts[i * 2 + 1]);
        pts = s;
    }
    const np = pts.length / 2;
    const evalAt = (dx, dy, cutoff) => {
        const hx0 = x0 + dx, hy0 = y0 + dy, hx1 = x1 + dx, hy1 = y1 + dy;
        const inside = hx0 >= 0 && hy0 >= 0 && hx1 <= w && hy1 <= h;
        if ((dx === 0 && dy === 0) || !inside || holeCount(hx0, hy0, hx1, hy1) !== 0)
            return null;
        let e = 0;
        const n = Math.max(np, 1), penalty = 1 + 1e-3 * (Math.hypot(dx, dy) / maxRadius);
        for (let i = 0; i < np; ++i) {
            const px = pts[i * 2], py = pts[i * 2 + 1];
            const a = (py * w + px) * ch, b = ((py + dy) * w + (px + dx)) * ch;
            for (let c = 0; c < ch; ++c) {
                const d = img[a + c] - img[b + c];
                e += d * d;
            }
            if (i % 32 === 31 && Number.isFinite(cutoff) && (e / n) * penalty >= cutoff)
                return null;
        }
        return (e / n) * penalty;
    };
    const step = Math.max(Math.floor(maxRadius / 24), 1);
    let best = null;
    const consider = (dx, dy) => {
        const e = evalAt(dx, dy, best ? best.e : Infinity);
        if (e !== null && (!best || e < best.e))
            best = { dx, dy, e };
    };
    for (let dy = -maxRadius; dy <= maxRadius; dy += step)
        for (let dx = -maxRadius; dx <= maxRadius; dx += step)
            consider(dx, dy);
    if (step > 1 && best) {
        const { dx: bx, dy: by } = best;
        for (let dy = by - step; dy <= by + step; ++dy)
            for (let dx = bx - step; dx <= bx + step; ++dx)
                consider(dx, dy);
    }
    return best ? [best.dx, best.dy] : null;
}

function minCut(across, along, err) {
    if (across <= 0 || along <= 0)
        return [];
    const acc = new Float64Array(across * along);
    let any = false;
    for (let v = 0; v < along; ++v)
        for (let u = 0; u < across; ++u) {
            acc[v * across + u] = err(u, v);
            any = any || acc[v * across + u] !== 0;
        }
    if (!any)
        return [];
    for (let v = 1; v < along; ++v)
        for (let u = 0; u < across; ++u) {
            let m = Infinity;
            for (let k = Math.max(u - 1, 0); k <= Math.min(u + 1, across - 1); ++k)
                m = Math.min(m, acc[(v - 1) * across + k]);
            acc[v * across + u] += m;
        }
    const argmin = (v, lo, hi) => {
        let best = lo;
        for (let k = lo + 1; k <= hi; ++k)
            if (acc[v * across + k] < acc[v * across + best])
                best = k;
        return best;
    };
    const cut = new Array(along).fill(0);
    let u = argmin(along - 1, 0, across - 1);
    for (let v = along - 1; v >= 0; --v) {
        if (v + 1 < along)
            u = argmin(v, Math.max(u - 1, 0), Math.min(u + 1, across - 1));
        cut[v] = u;
    }
    return cut;
}

// Crear textura: rellena el hueco con bloques de la textura de alrededor.
export function synthesize(w, h, ch, img, hole, block, seed, stop = NEVER) {
    const out = new Float32Array(img);
    const bb = holeBBox(w, h, hole);
    if (!bb)
        return out;
    const b = Math.max(block, 4), ov = Math.max(Math.floor(b / 3), 1);
    const hs = integral(w, h, hole);
    const cands = [];
    for (let y = ov; y < h - b - ov; ++y) {
        stop.check();
        for (let x = ov; x < w - b - ov; ++x)
            if (windowCount(hs, w, h, x - ov, y - ov, x + b + ov, y + b + ov) === 0)
                cands.push(x, y);
    }
    if (!cands.length)
        return membraneFill(w, h, ch, img, hole, stop);
    const nc = cands.length / 2;
    const filled = new Uint8Array(hole.length);
    for (let i = 0; i < hole.length; ++i)
        filled[i] = hole[i] ? 0 : 1;
    const s = seed64(seed), rng = new MixRng(s[0], s[1]);
    for (let by = bb.y0; by < bb.y1; by += b) {
        stop.check();
        for (let bx = bb.x0; bx < bb.x1; bx += b) {
            if (windowCount(hs, w, h, bx, by, bx + b, by + b) === 0)
                continue;
            let bestX = cands[0], bestY = cands[1], bestE = Infinity;
            for (let t = 0; t < 48; ++t) {
                const q = rng.nextMod(nc), cx = cands[q * 2], cy = cands[q * 2 + 1];
                let e = 0, n = 0;
                for (let y = by - ov; y < by + b + ov; ++y)
                    for (let x = bx - ov; x < bx + b + ov; ++x) {
                        if (x < 0 || y < 0 || x >= w || y >= h || !filled[y * w + x])
                            continue;
                        const a = (y * w + x) * ch, si = ((y - by + cy) * w + (x - bx + cx)) * ch;
                        for (let k = 0; k < ch; ++k) {
                            const d = out[a + k] - img[si + k];
                            e += d * d;
                        }
                        ++n;
                    }
                const ev = n === 0 ? 0 : e / n;
                if (ev < bestE) {
                    bestX = cx; bestY = cy; bestE = ev;
                }
            }
            const x0 = Math.max(bx - ov, 0), y0 = Math.max(by - ov, 0), x1 = Math.min(bx + b + ov, w), y1 = Math.min(by + b + ov, h);
            const src = (x, y) => ((y - by + bestY) * w + (x - bx + bestX)) * ch;
            const err = (x, y) => {
                const i = y * w + x;
                if (!(hole[i] && filled[i]))
                    return 0;
                const a = i * ch, si = src(x, y);
                let sum = 0;
                for (let q = 0; q < ch; ++q)
                    sum += (out[a + q] - img[si + q]) ** 2;
                return sum;
            };
            const left = minCut(Math.max(Math.min(bx + ov, x1) - x0, 0), y1 - y0, (u, v) => err(x0 + u, y0 + v));
            const top = minCut(Math.max(Math.min(by + ov, y1) - y0, 0), x1 - x0, (u, v) => err(x0 + v, y0 + u));
            for (let y = y0; y < y1; ++y)
                for (let x = x0; x < x1; ++x) {
                    const i = y * w + x;
                    if (!hole[i])
                        continue;
                    const u = x - x0, v = y - y0;
                    const keep = filled[i] && ((v < left.length && u < left[v]) || (u < top.length && v < top[u]));
                    if (!keep) {
                        const a = i * ch, si = src(x, y);
                        for (let q = 0; q < ch; ++q)
                            out[a + q] = img[si + q];
                    }
                    filled[i] = 1;
                }
        }
    }
    return out;
}

// Relleno según el contenido de Edición (y el del Parche).
export function colorLevel(level) {
    if (level === "none")
        return NO_ADAPT;
    if (level === "high")
        return { gainLo: 0.8, gainHi: 1.25, bias: 0.08 };
    if (level === "veryHigh")
        return { gainLo: 0.67, gainHi: 1.5, bias: 0.14 };
    return { gainLo: 0.9, gainHi: 1.1, bias: 0.04 };
}

export function colorAmount(color) {
    if (color <= 0)
        return NO_ADAPT;
    const c = Math.min(color, 10);
    return { gainLo: 1 - 0.033 * c, gainHi: 1 + 0.05 * c, bias: 0.04 * c };
}

export function levelTransform(ch, from, to, a) {
    const moments = (v, c) => {
        let s = 0, s2 = 0, n = 0;
        for (let i = 0; i + ch <= v.length; i += Math.max(ch, 1)) {
            s += v[i + c];
            s2 += v[i + c] * v[i + c];
            n += 1;
        }
        return [s, s2, n];
    };
    const out = [];
    for (let c = 0; c < ch; ++c) {
        const [fs, fs2, fn] = moments(from, c), [ts, ts2, tn] = moments(to, c);
        if (adaptNone(a) || fn === 0 || tn === 0) {
            out.push([1, 0]);
            continue;
        }
        const k = fn / tn;
        out.push(fitGainBias(a, fn, ts * k, ts2 * k, fs, fs2));
    }
    return out;
}

export function contentAwareFill(w, h, ch, img, hole, source, adapt, seed, stop = NEVER) {
    if (!hole.some((v) => v))
        return new Float32Array(img);
    const params = { ...defaultCompleteParams(), seed, adapt };
    const roles = new Uint8Array(w * h);
    for (let s = 0; s < roles.length; ++s)
        roles[s] = hole[s] ? Px.Hole : source[s] ? Px.Source : Px.Known;
    stop.check();
    let out = new Float32Array(img);
    if (!completeMasked(w, h, ch, out, roles, params, stop))
        out = membraneFill(w, h, ch, img, hole, stop);
    stop.check();
    return out;
}
