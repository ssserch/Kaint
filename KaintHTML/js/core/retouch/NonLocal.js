// Retoque de PhotoCraft (como core/retouch/NonLocal.cpp de Kaint): el relleno de Quitar.
// Algoritmos de PhotoCraft (licencia MIT).
//
// Completado no local por parches (Newson, Almansa, Gousseau y Pérez, IPOL 2017): PatchMatch y voto
// de lo grueso a lo fino con rasgos de textura en la distancia, inicio pelando el hueco capa a capa,
// el campo de coincidencias ampliado entre niveles, cada píxel final del mejor parche que lo cubre y
// las costuras escondidas en el dominio del gradiente.
import { MixRng, NEVER, bandSeed, dilate, holeRadius, membraneFill, mul64, seed64, seedAdd, seedXor, seedXorShift,
         solveMembrane } from "./Common.js";

export function defaultNonLocalParams() {
    return { patchRadius: 3, lambda: 50, maxIters: 10, pmPasses: 2, tolerance: 0.1 / 255, seed: 1 };
}

function validSources(w, h, hole, r) {
    const valid = new Uint8Array(w * h), lx = [], ly = [];
    if (w < 2 * r + 1 || h < 2 * r + 1)
        return { valid, lx: new Int32Array(0), ly: new Int32Array(0) };
    const w1 = w + 1, sat = new Uint32Array(w1 * (h + 1));
    for (let y = 0; y < h; ++y) {
        let row = 0;
        for (let x = 0; x < w; ++x) {
            row += hole[y * w + x] ? 1 : 0;
            sat[(y + 1) * w1 + x + 1] = sat[y * w1 + x + 1] + row;
        }
    }
    for (let y = r; y < h - r; ++y)
        for (let x = r; x < w - r; ++x) {
            const x0 = x - r, y0 = y - r, x1 = x + r + 1, y1 = y + r + 1;
            if (sat[y1 * w1 + x1] + sat[y0 * w1 + x0] === sat[y0 * w1 + x1] + sat[y1 * w1 + x0]) {
                valid[y * w + x] = 1;
                lx.push(x);
                ly.push(y);
            }
        }
    return { valid, lx: Int32Array.from(lx), ly: Int32Array.from(ly) };
}

function textureFeatures(w, h, ch, img, hole, side) {
    const n = w * h, g = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
        let s = 0;
        for (let c = 0; c < ch; ++c)
            s += img[i * ch + c];
        g[i] = s / ch;
    }
    const w1 = w + 1;
    const table = (dx, dy) => {
        const sum = new Float64Array(w1 * (h + 1)), cnt = new Uint32Array(w1 * (h + 1));
        for (let y = 0; y < h; ++y) {
            let rs = 0, rc = 0;
            for (let x = 0; x < w; ++x) {
                const i = y * w + x, nx = x + dx, ny = y + dy;
                if (nx < w && ny < h && !hole[i] && !hole[ny * w + nx]) {
                    rs += Math.abs(g[ny * w + nx] - g[i]);
                    ++rc;
                }
                sum[(y + 1) * w1 + x + 1] = sum[y * w1 + x + 1] + rs;
                cnt[(y + 1) * w1 + x + 1] = cnt[y * w1 + x + 1] + rc;
            }
        }
        const half = side >> 1, out = new Float32Array(n);
        for (let i = 0; i < n; ++i) {
            if (hole[i])
                continue;
            const x = i % w, y = (i / w) | 0;
            const x0 = Math.max(x - half, 0), y0 = Math.max(y - half, 0), x1 = Math.min(x + half + 1, w), y1 = Math.min(y + half + 1, h);
            const s = sum[y1 * w1 + x1] + sum[y0 * w1 + x0] - sum[y0 * w1 + x1] - sum[y1 * w1 + x0];
            const c = cnt[y1 * w1 + x1] + cnt[y0 * w1 + x0] - cnt[y0 * w1 + x1] - cnt[y1 * w1 + x0];
            out[i] = c === 0 ? 0 : s / c;
        }
        return out;
    };
    return [table(1, 0), table(0, 1)];
}

function downsample(l, cha, ch) {
    const K = [0.6412, 0.8007, 0.6412];
    const w = (l.w + 1) >> 1, h = (l.h + 1) >> 1;
    const img = new Float32Array(w * h * cha), hole = new Uint8Array(w * h);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const fx = x * 2, fy = y * 2, o = (y * w + x) * cha;
            let hl = 0;
            for (let dy = 0; dy < 2; ++dy)
                for (let dx = 0; dx < 2; ++dx)
                    if (fx + dx < l.w && fy + dy < l.h && l.hole[(fy + dy) * l.w + fx + dx])
                        hl = 1;
            hole[y * w + x] = hl;
            let wsum = 0;
            for (let ky = 0; ky < 3; ++ky)
                for (let kx = 0; kx < 3; ++kx) {
                    const sx = fx + kx - 1, sy = fy + ky - 1;
                    if (sx < 0 || sy < 0 || sx >= l.w || sy >= l.h)
                        continue;
                    const si = sy * l.w + sx;
                    if (l.hole[si])
                        continue;
                    const k = K[kx] * K[ky];
                    for (let c = 0; c < ch; ++c)
                        img[o + c] += l.img[si * cha + c] * k;
                    wsum += k;
                }
            if (wsum > 0)
                for (let c = 0; c < ch; ++c)
                    img[o + c] /= wsum;
            const si = (fy * l.w + fx) * cha;
            for (let c = ch; c < cha; ++c)
                img[o + c] = l.img[si + c];
        }
    return { w, h, img, hole };
}

function levelCount(w, h, hole, r) {
    const n = 2 * r + 1, no = Math.min(holeRadius(w, h, hole), 1 << 20);
    const want = Math.max(Math.ceil(Math.log2((2 * no) / n)), 1);
    let lw = w, lh = h, levels = 1;
    while (levels < want && (lw >> 1) >= 3 * n && (lh >> 1) >= 3 * n) {
        lw = (lw + 1) >> 1;
        lh = (lh + 1) >> 1;
        ++levels;
    }
    return levels;
}

function quantile75(cs) {
    if (!cs.length)
        return 1;
    cs.sort((a, b) => a - b);
    return Math.max(cs[Math.min(Math.floor((cs.length * 3) / 4), cs.length - 1)], 1e-6);
}

function median(v) {
    const s = v.slice().sort((a, b) => a - b);
    return s[s.length >> 1];
}

class Solver {
    constructor(o) {
        Object.assign(this, o);
    }

    isValid(x, y) {
        return x >= 0 && y >= 0 && x < this.w && y < this.h && this.valid[y * this.w + x] === 1;
    }

    randomSource(rng) {
        const n = Math.max(this.lx.length, 1), k = rng.nextMod(n);
        return k < this.lx.length ? [this.lx[k], this.ly[k]] : [0, 0];
    }

    dist(tx0, ty0, sx, sy, cutoff, known) {
        const { w, h, cha, r, img } = this, full = (2 * r + 1) * (2 * r + 1) * cha;
        let sum = 0, n = 0;
        for (let dy = -r; dy <= r; ++dy) {
            const ty = ty0 + dy;
            if (ty < 0 || ty >= h)
                continue;
            for (let dx = -r; dx <= r; ++dx) {
                const tx = tx0 + dx;
                if (tx < 0 || tx >= w)
                    continue;
                const tc = ty * w + tx;
                if (known && !known[tc])
                    continue;
                const ti = tc * cha, si = ((sy + dy) * w + (sx + dx)) * cha;
                for (let c = 0; c < cha; ++c) {
                    const d = img[ti + c] - img[si + c];
                    sum += d * d;
                }
                n += cha;
            }
            if (!known && sum > cutoff * full)
                return Infinity;
        }
        return n === 0 ? Infinity : sum / n;
    }

    patchmatch(nx, ny, cost, pass, seed, radius) {
        const { w, h } = this, reverse = pass % 2 === 1, step = reverse ? -1 : 1;
        const bands = Math.ceil(h / 16);
        for (let bi = 0; bi < bands; ++bi) {
            this.stop.check();
            const s = bandSeed(seed, bi, pass), rng = new MixRng(s[0], s[1]);
            const rows = Math.min(16, h - bi * 16), base = bi * 16 * w;
            for (let ry = 0; ry < rows; ++ry) {
                const ly = reverse ? rows - 1 - ry : ry, y = bi * 16 + ly;
                for (let rx = 0; rx < w; ++rx) {
                    const x = reverse ? w - 1 - rx : rx;
                    if (!this.targets[y * w + x])
                        continue;
                    const li = base + ly * w + x;
                    let bx = nx[li], by = ny[li], bc = cost[li];
                    const tryCand = (cx, cy) => {
                        if ((cx !== bx || cy !== by) && this.isValid(cx, cy)) {
                            const d = this.dist(x, y, cx, cy, bc, null);
                            if (d < bc) {
                                bx = cx; by = cy; bc = d;
                            }
                        }
                    };
                    const nxx = x - step;
                    if (nxx >= 0 && nxx < w) {
                        const q = base + ly * w + nxx;
                        tryCand(nx[q] + step, ny[q]);
                    }
                    const nly = ly - step;
                    if (nly >= 0 && nly < rows) {
                        const q = base + nly * w + x;
                        tryCand(nx[q], ny[q] + step);
                    }
                    for (let rad = radius; rad >= 1; rad = Math.floor(rad / 2)) {
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

    refreshCosts(nx, ny, cost) {
        const w = this.w;
        for (let i = 0; i < cost.length; ++i)
            cost[i] = this.targets[i] ? this.dist(i % w, (i / w) | 0, nx[i], ny[i], Infinity, null) : Infinity;
    }

    sources(x, y, nx, ny, cost, f) {
        const { w, h, r } = this;
        for (let dy = -r; dy <= r; ++dy) {
            const ty = y + dy;
            if (ty < 0 || ty >= h)
                continue;
            for (let dx = -r; dx <= r; ++dx) {
                const tx = x + dx;
                if (tx < 0 || tx >= w)
                    continue;
                const ti = ty * w + tx;
                if (!this.targets[ti] || !Number.isFinite(cost[ti]))
                    continue;
                const sx = nx[ti] - dx, sy = ny[ti] - dy;
                if (sx >= 0 && sy >= 0 && sx < w && sy < h)
                    f(sy * w + sx, cost[ti]);
            }
        }
    }

    vote(nx, ny, cost, uniform) {
        const { w, h, cha, ch, img, hole } = this;
        const cs = [];
        for (let i = 0; i < cost.length; ++i)
            if (this.targets[i] && Number.isFinite(cost[i]))
                cs.push(cost[i]);
        const sigma2 = quantile75(cs);
        const cells = [], values = [];
        const v = new Float64Array(cha);
        for (let y = 0; y < h; ++y) {
            if (y % 32 === 0)
                this.stop.check();
            for (let x = 0; x < w; ++x) {
                if (!hole[y * w + x])
                    continue;
                v.fill(0);
                let wsum = 0;
                this.sources(x, y, nx, ny, cost, (si, c) => {
                    const wt = uniform ? 1 : Math.max(Math.exp(-c / (2 * sigma2)), 1e-8);
                    for (let k = 0; k < cha; ++k)
                        v[k] += img[si * cha + k] * wt;
                    wsum += wt;
                });
                if (wsum > 0) {
                    cells.push(y * w + x);
                    for (let k = 0; k < cha; ++k)
                        values.push(v[k] / wsum);
                }
            }
        }
        let change = 0, n = 0;
        for (let q = 0; q < cells.length; ++q) {
            const i = cells[q];
            for (let c = 0; c < ch; ++c)
                change += Math.abs(values[q * cha + c] - img[i * cha + c]);
            n += ch;
            for (let k = 0; k < cha; ++k)
                img[i * cha + k] = values[q * cha + k];
        }
        return n === 0 ? 0 : change / n;
    }

    // La reconstrucción final: cada celda del hueco copia la fuente del parche de menor coste.
    copyBest(nx, ny, cost) {
        const { w, h, cha, img, hole } = this, picked = [];
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x) {
                if (!hole[y * w + x])
                    continue;
                let bs = -1, bc = Infinity;
                this.sources(x, y, nx, ny, cost, (si, c) => {
                    if (bs < 0 || c < bc) {
                        bs = si;
                        bc = c;
                    }
                });
                if (bs >= 0)
                    picked.push([y * w + x, bs, bc]);
            }
        for (const [i, si] of picked)
            for (let k = 0; k < cha; ++k)
                img[i * cha + k] = img[si * cha + k];
        return picked;
    }

    // Esconde la costura de color del borde del hueco con una membrana (Poisson).
    blendSeams(copied, textureScale) {
        const EDGE_MEDIAN = 8, MAX_SHIFT = 0.05;
        if (!copied.length)
            return;
        const { w, h, cha, ch, img, hole } = this;
        const unscale = textureScale > 0 ? 0.5 / textureScale : 0;
        let x0 = w, y0 = h, x1 = 0, y1 = 0;
        for (const [p] of copied) {
            const px = p % w, py = (p / w) | 0;
            x0 = Math.min(x0, px); y0 = Math.min(y0, py); x1 = Math.max(x1, px + 1); y1 = Math.max(y1, py + 1);
        }
        x0 = Math.max(x0 - 1, 0); y0 = Math.max(y0 - 1, 0); x1 = Math.min(x1 + 1, w); y1 = Math.min(y1 + 1, h);
        const bw = Math.max(x1 - x0, 0), bh = Math.max(y1 - y0, 0);
        const local = (i) => (((i / w) | 0) - y0) * bw + ((i % w) - x0);
        const inHole = (lq) => hole[(y0 + ((lq / bw) | 0)) * w + x0 + (lq % bw)];
        const shifts = [];
        for (let c = 0; c < ch; ++c) {
            const v = new Float32Array(bw * bh), n = new Uint8Array(bw * bh);
            for (const [p, s] of copied) {
                const px = p % w, py = (p / w) | 0, sx = s % w, sy = (s / w) | 0;
                for (const [dx, dy] of [[-1, 0], [1, 0], [0, -1], [0, 1]]) {
                    const qx = px + dx, qy = py + dy, tx = sx + dx, ty = sy + dy;
                    if (qx < 0 || qy < 0 || qx >= w || qy >= h || tx < 0 || ty < 0 || tx >= w || ty >= h)
                        continue;
                    const q = qy * w + qx, t = ty * w + tx;
                    if (hole[q])
                        continue;
                    const lq = local(q);
                    v[lq] += img[q * cha + c] - img[t * cha + c];
                    if (n[lq] < 255)
                        ++n[lq];
                }
            }
            for (let i = 0; i < v.length; ++i)
                if (n[i] > 0)
                    v[i] /= n[i];
            const raw = v.slice();
            for (let lq = 0; lq < v.length; ++lq) {
                if (!n[lq])
                    continue;
                const qx = lq % bw, qy = (lq / bw) | 0, near = [];
                for (let y = Math.max(qy - EDGE_MEDIAN, 0); y < Math.min(qy + EDGE_MEDIAN + 1, bh); ++y)
                    for (let x = Math.max(qx - EDGE_MEDIAN, 0); x < Math.min(qx + EDGE_MEDIAN + 1, bw); ++x)
                        if (n[y * bw + x] > 0)
                            near.push(raw[y * bw + x]);
                if (near.length) {
                    const m = median(near), q = (y0 + qy) * w + x0 + qx;
                    const texture = (img[q * cha + ch] + img[q * cha + ch + 1]) * unscale;
                    const cap = MAX_SHIFT + 2 * texture;
                    v[lq] = Math.min(Math.max(m, -cap), cap);
                }
            }
            const queue = [], seen = new Uint8Array(v.length);
            for (let lq = 0; lq < v.length; ++lq)
                if (n[lq] > 0) {
                    seen[lq] = 1;
                    queue.push(lq);
                }
            for (let head = 0; head < queue.length; ++head) {
                const lq = queue[head], x = lq % bw, y = (lq / bw) | 0;
                for (const nb of [x > 0 ? lq - 1 : -1, x + 1 < bw ? lq + 1 : -1, y > 0 ? lq - bw : -1, y + 1 < bh ? lq + bw : -1]) {
                    if (nb < 0 || seen[nb] || inHole(nb))
                        continue;
                    seen[nb] = 1;
                    v[nb] = v[lq];
                    queue.push(nb);
                }
            }
            const unknown = new Uint8Array(v.length);
            for (let i = 0; i < v.length; ++i)
                unknown[i] = inHole(i) ? 1 : 0;
            solveMembrane(bw, bh, unknown, v);
            shifts.push(v);
        }
        for (const [p] of copied) {
            const lp = local(p);
            for (let c = 0; c < ch; ++c)
                img[p * cha + c] += shifts[c][lp];
        }
    }

    // Quita las costuras visibles entre zonas copiadas de sitios distintos (como Image Melding).
    fuseSeams(copied, textureScale, iters) {
        const SEAM_CONTRAST = 3, OMEGA = 1.8;
        const { w, h, cha, ch, img } = this;
        const srcS = new Float64Array(w * h).fill(-1), srcC = new Float64Array(w * h).fill(Infinity);
        for (const [p, s, c] of copied) {
            srcS[p] = s;
            srcC[p] = c;
        }
        const cell = (x, y) => (x >= 0 && y >= 0 && x < w && y < h ? y * w + x : -1);
        const unscale = textureScale > 0 ? 0.5 / textureScale : 0;
        const div = new Float32Array(w * h * ch);
        let any = false;
        const e = new Float64Array(ch);
        for (const [p, sp, cp] of copied) {
            const px = p % w, py = (p / w) | 0;
            const texture = (img[p * cha + ch] + img[p * cha + ch + 1]) * unscale;
            for (const [dx, dy] of [[1, 0], [0, 1]]) {
                const q = cell(px + dx, py + dy);
                if (q < 0)
                    continue;
                const sq = srcS[q], cq = srcC[q];
                if (sq < 0)
                    continue;
                const fromP = cell((sp % w) + dx, ((sp / w) | 0) + dy);
                if (fromP === sq)
                    continue;
                const fromQ = cell((sq % w) - dx, ((sq / w) | 0) - dy);
                let a, b;
                if (fromP >= 0 && fromQ >= 0) {
                    if (cp <= cq) { a = sp; b = fromP; } else { a = fromQ; b = sq; }
                } else if (fromP >= 0) {
                    a = sp; b = fromP;
                } else if (fromQ >= 0) {
                    a = fromQ; b = sq;
                } else {
                    continue;
                }
                let sum = 0;
                for (let c = 0; c < ch; ++c) {
                    e[c] = (img[b * cha + c] - img[a * cha + c]) - (img[q * cha + c] - img[p * cha + c]);
                    sum += Math.abs(e[c]);
                }
                if (sum / ch <= SEAM_CONTRAST * texture)
                    continue;
                for (let c = 0; c < ch; ++c) {
                    div[p * ch + c] += e[c];
                    div[q * ch + c] -= e[c];
                }
                any = true;
            }
        }
        if (!any)
            return;
        const seams = new Uint8Array(w * h);
        for (let i = 0; i < seams.length; ++i)
            for (let c = 0; c < ch; ++c)
                if (div[i * ch + c] !== 0) {
                    seams[i] = 1;
                    break;
                }
        const band = dilate(seams, w, h, 16);
        const halves = [[], []];
        for (let colour = 0; colour < 2; ++colour)
            for (const [p] of copied)
                if (band[p] && ((p % w) + ((p / w) | 0)) % 2 === colour)
                    halves[colour].push(p);
        const delta = new Float32Array(w * h * ch);
        for (let it = 0; it < iters; ++it) {
            if (this.stop.requested())
                return;
            for (const cells of halves) {
                const next = new Float32Array(cells.length * ch);
                for (let k = 0; k < cells.length; ++k) {
                    const p = cells[k], x = p % w, y = (p / w) | 0;
                    let deg = 0;
                    const o = k * ch;
                    for (const [dx, dy] of [[-1, 0], [1, 0], [0, -1], [0, 1]]) {
                        const q = cell(x + dx, y + dy);
                        if (q < 0)
                            continue;
                        deg += 1;
                        for (let c = 0; c < ch; ++c)
                            next[o + c] += delta[q * ch + c];
                    }
                    for (let c = 0; c < ch; ++c) {
                        const cur = delta[p * ch + c];
                        next[o + c] = cur + OMEGA * ((next[o + c] - div[p * ch + c]) / Math.max(deg, 1) - cur);
                    }
                }
                for (let k = 0; k < cells.length; ++k)
                    for (let c = 0; c < ch; ++c)
                        delta[cells[k] * ch + c] = next[k * ch + c];
            }
        }
        for (const [p] of copied)
            for (let c = 0; c < ch; ++c)
                img[p * cha + c] += delta[p * ch + c];
    }

    // Inicio pelando la cebolla: el hueco se rellena capa a capa desde el borde.
    onionPeel(nx, ny, has, seed) {
        const { w, h, r, cha, img, hole } = this;
        const known = new Uint8Array(hole.length);
        let unknown = [];
        for (let i = 0; i < hole.length; ++i) {
            known[i] = hole[i] ? 0 : 1;
            if (hole[i])
                unknown.push(i);
        }
        const centre = new Uint8Array(w * h), layerCost = new Float64Array(w * h).fill(Infinity);
        let layerNo = 0;
        for (;;) {
            this.stop.check();
            const neighbourKnown = (i) => {
                const x = i % w, y = (i / w) | 0;
                return (x > 0 && known[i - 1]) || (x + 1 < w && known[i + 1]) || (y > 0 && known[i - w]) || (y + 1 < h && known[i + w]);
            };
            const layer = unknown.filter((i) => neighbourKnown(i));
            if (!layer.length)
                return;
            const centres = [];
            for (const i of layer) {
                const x = i % w, y = (i / w) | 0;
                for (let cy = Math.max(y - r, 0); cy <= Math.min(y + r, h - 1); ++cy)
                    for (let cx = Math.max(x - r, 0); cx <= Math.min(x + r, w - 1); ++cx) {
                        const ci = cy * w + cx;
                        if (known[ci] && !centre[ci]) {
                            centre[ci] = 1;
                            centres.push(ci);
                        }
                    }
            }
            for (const ci of centres)
                centre[ci] = 0;
            const found = [];
            for (let k = 0; k < centres.length; ++k) {
                if (k % 256 === 0)
                    this.stop.check();
                const ci = centres[k], tx = ci % w, ty = (ci / w) | 0;
                // seed ^ ci·0x9E3779B9 ^ (layerNo << 48)
                const m = mul64(0, ci >>> 0, 0, 0x9e3779b9);
                const s = seedXor(seedXorShift(seed, layerNo, 48), m), rng = new MixRng(s[0], s[1]);
                let bx = 0, by = 0, bc = Infinity;
                const tryCand = (cx, cy) => {
                    if (this.isValid(cx, cy)) {
                        const d = this.dist(tx, ty, cx, cy, bc, known);
                        if (d < bc) {
                            bx = cx; by = cy; bc = d;
                        }
                    }
                };
                if (has[ci])
                    tryCand(nx[ci], ny[ci]);
                for (const [ox, oy] of [[-1, 0], [1, 0], [0, -1], [0, 1]]) {
                    const qx = tx + ox, qy = ty + oy;
                    if (qx >= 0 && qy >= 0 && qx < w && qy < h && has[qy * w + qx])
                        tryCand(nx[qy * w + qx] - ox, ny[qy * w + qx] - oy);
                }
                for (let i = 0; i < 8; ++i)
                    tryCand(...this.randomSource(rng));
                for (let i = 0; i < 2; ++i)
                    for (let rad = Math.max(w, h); rad >= 1; rad = Math.floor(rad / 2)) {
                        const cx = bx + rng.range(-rad, rad), cy = by + rng.range(-rad, rad);
                        tryCand(cx, cy);
                    }
                found.push([ci, bx, by, bc]);
            }
            const cs = [];
            for (const [ci, sx, sy, c] of found)
                if (Number.isFinite(c)) {
                    nx[ci] = sx;
                    ny[ci] = sy;
                    has[ci] = 1;
                    layerCost[ci] = c;
                    cs.push(c);
                }
            const sigma2 = quantile75(cs);
            const values = [];
            const v = new Float64Array(cha);
            for (const i of layer) {
                const x = i % w, y = (i / w) | 0;
                v.fill(0);
                let wsum = 0;
                for (let cy = Math.max(y - r, 0); cy <= Math.min(y + r, h - 1); ++cy)
                    for (let cx = Math.max(x - r, 0); cx <= Math.min(x + r, w - 1); ++cx) {
                        const ci = cy * w + cx;
                        if (!Number.isFinite(layerCost[ci]))
                            continue;
                        const sx = nx[ci] + x - cx, sy = ny[ci] + y - cy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                            continue;
                        const si = sy * w + sx, wt = Math.max(Math.exp(-layerCost[ci] / (2 * sigma2)), 1e-8);
                        for (let c = 0; c < cha; ++c)
                            v[c] += img[si * cha + c] * wt;
                        wsum += wt;
                    }
                if (wsum > 0)
                    values.push([i, Array.from(v, (q) => q / wsum)]);
            }
            for (const [ci] of found)
                layerCost[ci] = Infinity;
            if (!values.length)
                return;
            for (const [i, val] of values) {
                for (let c = 0; c < cha; ++c)
                    img[i * cha + c] = val[c];
                known[i] = 1;
            }
            unknown = unknown.filter((i) => !known[i]);
            ++layerNo;
        }
    }
}

export function completeNonLocal(w, h, ch, img, hole, p = defaultNonLocalParams(), stop = NEVER) {
    const n = w * h;
    if (ch === 0 || hole.length !== n || img.length !== n * ch || !hole.some((v) => v))
        return new Float32Array(img);
    const r = Math.max(p.patchRadius, 1);
    const levels = levelCount(w, h, hole, r);
    stop.check();
    const [tx, ty] = textureFeatures(w, h, ch, img, hole, 1 << Math.min(levels, 8));
    stop.check();
    const cha = ch + 2, k = Math.sqrt(Math.max(p.lambda, 0));
    const data = new Float32Array(n * cha);
    for (let i = 0; i < n; ++i) {
        for (let c = 0; c < ch; ++c)
            data[i * cha + c] = img[i * ch + c];
        data[i * cha + ch] = tx[i] * k;
        data[i * cha + ch + 1] = ty[i] * k;
    }
    const pyramid = [{ w, h, img: data, hole }];
    while (pyramid.length < levels) {
        stop.check();
        const next = downsample(pyramid[pyramid.length - 1], cha, ch);
        if (!validSources(next.w, next.h, next.hole, r).lx.length)
            break;
        pyramid.push(next);
    }
    const seed = seed64(p.seed);
    let prev = null, result = null;
    for (let li = pyramid.length - 1; li >= 0; --li) {
        stop.check();
        const level = pyramid[li], lw = level.w, lh = level.h;
        const { valid, lx, ly } = validSources(lw, lh, level.hole, r);
        if (!lx.length)
            return null;
        const targets = new Uint8Array(lw * lh);
        for (let i = 0; i < level.hole.length; ++i)
            if (level.hole[i]) {
                const x = i % lw, y = (i / lw) | 0;
                for (let yy = Math.max(y - r, 0); yy <= Math.min(y + r, lh - 1); ++yy)
                    for (let xx = Math.max(x - r, 0); xx <= Math.min(x + r, lw - 1); ++xx)
                        targets[yy * lw + xx] = 1;
            }
        const s = new Solver({ w: lw, h: lh, cha, ch, r, img: new Float32Array(level.img), hole: level.hole, valid, lx, ly,
                               targets, stop });
        const nx = new Int32Array(lw * lh), ny = new Int32Array(lw * lh), cost = new Float64Array(lw * lh).fill(Infinity);
        const ls = seedXorShift(seed, li, 20), rng = new MixRng(ls[0], ls[1]);
        if (!prev) {
            const has = new Uint8Array(lw * lh);
            s.onionPeel(nx, ny, has, seedXor(seed, [0, 0x0c104e00]));
            for (let i = 0; i < nx.length; ++i)
                if (targets[i] && !has[i])
                    [nx[i], ny[i]] = s.randomSource(rng);
        } else {
            const { pw, px: pnx, py: pny } = prev;
            for (let y = 0; y < lh; ++y)
                for (let x = 0; x < lw; ++x) {
                    const i = y * lw + x;
                    if (!targets[i])
                        continue;
                    const pi = (y >> 1) * pw + (x >> 1);
                    const cx = pi < pnx.length ? pnx[pi] : -1, cy = pi < pny.length ? pny[pi] : -1;
                    const ax = cx * 2 + (x % 2), ay = cy * 2 + (y % 2);
                    if (s.isValid(ax, ay)) {
                        nx[i] = ax; ny[i] = ay;
                    } else if (s.isValid(cx * 2, cy * 2)) {
                        nx[i] = cx * 2; ny[i] = cy * 2;
                    } else {
                        [nx[i], ny[i]] = s.randomSource(rng);
                    }
                }
            for (let i = 0; i < cost.length; ++i)
                if (targets[i])
                    cost[i] = 0;
            s.vote(nx, ny, cost, true);
        }
        const coarsest = li + 1 === pyramid.length, full = Math.max(lw, lh);
        for (let it = 0; it < Math.max(p.maxIters, 1); ++it) {
            stop.check();
            s.refreshCosts(nx, ny, cost);
            const radius = coarsest || it === 0 ? full : Math.min(8 * (2 * r + 1), full);
            for (let pass = 0; pass < Math.max(p.pmPasses, 1); ++pass) {
                stop.check();
                s.patchmatch(nx, ny, cost, pass + it * p.pmPasses, seedAdd(seed, li * 1000 + it), radius);
            }
            stop.check();
            const change = s.vote(nx, ny, cost, false);
            stop.check();
            if (change < p.tolerance)
                break;
        }
        if (li === 0) {
            s.refreshCosts(nx, ny, cost);
            const copied = s.copyBest(nx, ny, cost);
            stop.check();
            s.blendSeams(copied, k);
            stop.check();
            s.fuseSeams(copied, k, 100);
            stop.check();
            result = s.img;
        }
        prev = { pw: lw, px: nx, py: ny };
    }
    if (!result)
        return null;
    const out = new Float32Array(img);
    for (let i = 0; i < n; ++i)
        if (hole[i])
            for (let c = 0; c < ch; ++c)
                out[i * ch + c] = result[i * cha + c];
    return out;
}

function fillOne(w, h, ch, img, hole, seed, stop) {
    const out = completeNonLocal(w, h, ch, img, hole, { ...defaultNonLocalParams(), seed }, stop);
    return out || membraneFill(w, h, ch, img, hole, stop);
}

// Rellena el hueco desde su alrededor (por mosaicos si es disperso).
export function removeFill(w, h, ch, img, hole, seed, stop = NEVER) {
    const n = w * h;
    if (ch === 0 || hole.length !== n || img.length !== n * ch)
        return new Float32Array(img);
    let bx0 = w, by0 = h, bx1 = 0, by1 = 0, count = 0;
    for (let i = 0; i < n; ++i)
        if (hole[i]) {
            const x = i % w, y = (i / w) | 0;
            bx0 = Math.min(bx0, x); by0 = Math.min(by0, y); bx1 = Math.max(bx1, x + 1); by1 = Math.max(by1, y + 1);
            ++count;
        }
    if (!count)
        return new Float32Array(img);
    const thickness = Math.min(holeRadius(w, h, hole), 1 << 20);
    stop.check();
    const tile = Math.max(8 * thickness, 256);
    const area = (bx1 - bx0) * (by1 - by0);
    if (count >= 0.25 * area || Math.max(bx1 - bx0, by1 - by0) <= 2 * tile)
        return fillOne(w, h, ch, img, hole, seed, stop);
    const margin = Math.max(6 * thickness, 48);
    const out = new Float32Array(img), left = new Uint8Array(hole);
    let k = 0;
    for (let ty0 = by0; ty0 < by1; ty0 += tile)
        for (let tx0 = bx0; tx0 < bx1; tx0 += tile, ++k) {
            stop.check();
            const tx1 = Math.min(tx0 + tile, bx1), ty1 = Math.min(ty0 + tile, by1);
            let any = false;
            for (let y = ty0; y < ty1 && !any; ++y)
                for (let x = tx0; x < tx1; ++x)
                    if (left[y * w + x]) {
                        any = true;
                        break;
                    }
            if (!any)
                continue;
            const x0 = Math.max(tx0 - margin, 0), y0 = Math.max(ty0 - margin, 0), x1 = Math.min(tx1 + margin, w), y1 = Math.min(ty1 + margin, h);
            const ww = x1 - x0, hh = y1 - y0;
            const subImg = new Float32Array(ww * hh * ch), subHole = new Uint8Array(ww * hh);
            for (let y = y0; y < y1; ++y) {
                subImg.set(out.subarray((y * w + x0) * ch, (y * w + x1) * ch), (y - y0) * ww * ch);
                subHole.set(left.subarray(y * w + x0, y * w + x1), (y - y0) * ww);
            }
            const filled = fillOne(ww, hh, ch, subImg, subHole, seed + k, stop);
            for (let y = ty0; y < ty1; ++y)
                for (let x = tx0; x < tx1; ++x) {
                    const i = y * w + x;
                    if (left[i]) {
                        const j = (y - y0) * ww + (x - x0);
                        for (let c = 0; c < ch; ++c)
                            out[i * ch + c] = filled[j * ch + c];
                        left[i] = 0;
                    }
                }
        }
    return out;
}
