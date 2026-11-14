// Retoque de PhotoCraft (como core/retouch/Magnetic.cpp de Kaint): el trazado del Lazo magnético.
// Algoritmos de PhotoCraft (licencia MIT).
//
// Entre dos puntos de anclaje el borde es el camino de píxeles (8-conexo) más barato, donde los
// píxeles sobre un borde fuerte y bien localizado son baratos: el «cable vivo» de Mortensen y
// Barrett. La búsqueda se queda en un pasillo alrededor del recorrido del puntero y los puntos
// sobre un borde van a su centro con precisión de subpíxel.
import { box, boxContains, boxEmpty, boxInflate, boxIntersect, boxUnite, boxW, boxH } from "./Common.js";

const BLOCK = 128, MAX_BLOCKS = 1024, PIECE = 256, MAX_GUIDE_LENGTH = 262144;
const W_CENTRE = 0.43, W_STRENGTH = 0.43, W_DIRECTION = 0.14, W_LENGTH = 0.02, W_GUIDE = 0.4;
const NEUTRAL_DIRECTION = 2 / 3;
const DIRS = [[1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0], [-1, -1], [0, -1], [1, -1]];

const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);

function dist2Segment(p, a, b) {
    const vx = b[0] - a[0], vy = b[1] - a[1], l2 = vx * vx + vy * vy;
    const t = l2 > 0 ? Math.min(Math.max(((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / l2, 0), 1) : 0;
    const dx = a[0] + vx * t - p[0], dy = a[1] + vy * t - p[1];
    return dx * dx + dy * dy;
}

export function magneticSettings(width, contrast) {
    return {
        width: Number.isFinite(width) ? Math.min(Math.max(width, 1), 256) : 10,
        contrast: Number.isFinite(contrast) ? Math.min(Math.max(contrast, 0.01), 1) : 0.1,
    };
}

export function polylineLength(p) {
    let s = 0;
    for (let i = 0; i + 1 < p.length; ++i)
        s += dist(p[i], p[i + 1]);
    return s;
}

// Ramer–Douglas–Peucker: quita los puntos a menos de 'tol' de la recta de sus vecinos.
export function simplifyPolyline(p, tol) {
    const n = p.length;
    if (n < 3)
        return p.slice();
    const keep = new Uint8Array(n);
    keep[0] = keep[n - 1] = 1;
    const tol2 = tol * tol, stack = [[0, n - 1]];
    while (stack.length) {
        const [a, b] = stack.pop();
        if (b <= a + 1)
            continue;
        let far = 0, farK = a;
        for (let k = a + 1; k < b; ++k) {
            const d2 = dist2Segment(p[k], p[a], p[b]);
            if (d2 > far) {
                far = d2;
                farK = k;
            }
        }
        if (far > tol2) {
            keep[farK] = 1;
            stack.push([a, farK], [farK, b]);
        }
    }
    return p.filter((_, i) => keep[i]);
}

function split(g, piece) {
    if (!g.length)
        return [];
    const out = [];
    let cur = [g[0]], acc = 0;
    for (let i = 0; i + 1 < g.length; ++i) {
        let a = g[i];
        const b = g[i + 1];
        let seg = dist(a, b);
        while (acc + seg > piece && seg > 0) {
            const t = Math.min(Math.max((piece - acc) / seg, 0), 1);
            const m = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
            cur.push(m);
            out.push(cur);
            cur = [m];
            a = m;
            seg = dist(a, b);
            acc = 0;
        }
        cur.push(b);
        acc += seg;
    }
    out.push(cur);
    return out;
}

function directionCost(tp0, tp1, tq0, tq1, dx, dy, len) {
    if ((tp0 === 0 && tp1 === 0) || (tq0 === 0 && tq1 === 0))
        return NEUTRAL_DIRECTION;
    let l0 = dx / len, l1 = dy / len;
    if (tp0 * l0 + tp1 * l1 < 0) {
        l0 = -l0;
        l1 = -l1;
    }
    const dp = Math.min(Math.max(tp0 * l0 + tp1 * l1, -1), 1), dq = Math.min(Math.max(l0 * tq0 + l1 * tq1, -1), 1);
    return (2 / (3 * Math.PI)) * (Math.acos(dp) + Math.acos(dq));
}

// Rasgos de borde de los píxeles RGBA de 8 bits de 'ext' (la zona y hasta dos píxeles más).
function makeField(area, ext, px) {
    const ew = boxW(ext), eh = boxH(ext);
    const at = (x, y, out) => {
        const xx = Math.min(Math.max(x - ext.x0, 0), Math.max(ew - 1, 0)), yy = Math.min(Math.max(y - ext.y0, 0), Math.max(eh - 1, 0));
        const i = (yy * ew + xx) * 4;
        const a = px[i + 3] / 255;
        out[0] = (px[i] / 255) * a; out[1] = (px[i + 1] / 255) * a; out[2] = (px[i + 2] / 255) * a; out[3] = a;
    };
    const g1 = boxIntersect(boxInflate(area, 1), ext), gw = boxW(g1);
    const st = new Float32Array(gw * boxH(g1)), gd = new Float32Array(st.length * 2);
    const nw = [0, 0, 0, 0], n = [0, 0, 0, 0], ne = [0, 0, 0, 0], wv = [0, 0, 0, 0], e = [0, 0, 0, 0], sw = [0, 0, 0, 0], s = [0, 0, 0, 0], se = [0, 0, 0, 0];
    for (let y = g1.y0; y < g1.y1; ++y)
        for (let x = g1.x0; x < g1.x1; ++x) {
            at(x - 1, y - 1, nw); at(x, y - 1, n); at(x + 1, y - 1, ne);
            at(x - 1, y, wv); at(x + 1, y, e);
            at(x - 1, y + 1, sw); at(x, y + 1, s); at(x + 1, y + 1, se);
            let bestM = 0, bx = 0, by = 0;
            for (let c = 0; c < 4; ++c) {
                const gx = (ne[c] + 2 * e[c] + se[c]) - (nw[c] + 2 * wv[c] + sw[c]);
                const gy = (sw[c] + 2 * s[c] + se[c]) - (nw[c] + 2 * n[c] + ne[c]);
                const m2 = gx * gx + gy * gy;
                if (m2 > bestM) {
                    bestM = m2; bx = gx; by = gy;
                }
            }
            const mag = Math.sqrt(bestM), i = (y - g1.y0) * gw + (x - g1.x0);
            st[i] = Math.min(mag / 4, 1);
            if (mag > 1e-6) {
                gd[i * 2] = bx / mag;
                gd[i * 2 + 1] = by / mag;
            }
        }
    const sAt = (x, y) => (boxContains(g1, x, y) ? st[(y - g1.y0) * gw + (x - g1.x0)] : 0);
    const w = boxW(area), count = w * boxH(area);
    const f = { area, w, strength: new Float32Array(count), grad: new Float32Array(count * 2), ridge: new Uint8Array(count),
                offset: new Float32Array(count * 2) };
    for (let y = area.y0; y < area.y1; ++y)
        for (let x = area.x0; x < area.x1; ++x) {
            const s0 = sAt(x, y);
            let g0 = 0, g1v = 0;
            if (boxContains(g1, x, y)) {
                const gi = (y - g1.y0) * gw + (x - g1.x0);
                g0 = gd[gi * 2];
                g1v = gd[gi * 2 + 1];
            }
            const i = (y - area.y0) * w + (x - area.x0);
            f.strength[i] = s0;
            f.grad[i * 2] = g0;
            f.grad[i * 2 + 1] = g1v;
            if (g0 === 0 && g1v === 0)
                continue;
            const q = (v, o) => (Math.abs(v) >= 0.4142 * Math.abs(o) ? Math.sign(v) : 0);
            const dx = q(g0, g1v), dy = q(g1v, g0);
            const sm = sAt(x - dx, y - dy), sp = sAt(x + dx, y + dy);
            if (s0 > 0 && s0 >= sm && s0 >= sp) {
                f.ridge[i] = 1;
                const denom = sm - 2 * s0 + sp;
                const t = denom < -1e-6 ? Math.min(Math.max((0.5 * (sm - sp)) / denom, -0.5), 0.5) : 0;
                f.offset[i * 2] = t * dx;
                f.offset[i * 2 + 1] = t * dy;
            }
        }
    return f;
}

const fieldIndex = (f, x, y) => (boxContains(f.area, x, y) ? (y - f.area.y0) * f.w + (x - f.area.x0) : -1);

function fieldPoint(f, i, x, y, contrast) {
    const onEdge = f.ridge[i] && f.strength[i] >= contrast;
    return [x + 0.5 + (onEdge ? f.offset[i * 2] : 0), y + 0.5 + (onEdge ? f.offset[i * 2 + 1] : 0)];
}

function corridor(area, guide, rad) {
    const w = boxW(area), near = new Float32Array(w * boxH(area)).fill(Infinity), r2 = rad * rad;
    const segs = guide.length === 1 ? [guide[0], guide[0]] : guide;
    for (let k = 0; k + 1 < segs.length; ++k) {
        const a = segs[k], b = segs[k + 1];
        const r = boxIntersect(box(Math.floor(Math.min(a[0], b[0]) - rad), Math.floor(Math.min(a[1], b[1]) - rad),
                                   Math.ceil(Math.max(a[0], b[0]) + rad) + 1, Math.ceil(Math.max(a[1], b[1]) + rad) + 1), area);
        for (let y = r.y0; y < r.y1; ++y)
            for (let x = r.x0; x < r.x1; ++x) {
                const d2 = dist2Segment([x + 0.5, y + 0.5], a, b);
                if (d2 <= r2) {
                    const i = (y - area.y0) * w + (x - area.x0);
                    near[i] = Math.min(near[i], d2);
                }
            }
    }
    return near;
}

// Montículo mínimo por (coste, índice), como el BinaryHeap de Reverse((bits, i)).
class MinHeap {
    constructor() {
        this.d = [];
        this.i = [];
    }
    get size() {
        return this.d.length;
    }
    less(a, b) {
        return this.d[a] < this.d[b] || (this.d[a] === this.d[b] && this.i[a] < this.i[b]);
    }
    swap(a, b) {
        [this.d[a], this.d[b]] = [this.d[b], this.d[a]];
        [this.i[a], this.i[b]] = [this.i[b], this.i[a]];
    }
    push(d, i) {
        this.d.push(d);
        this.i.push(i);
        let k = this.d.length - 1;
        while (k > 0) {
            const p = (k - 1) >> 1;
            if (!this.less(k, p))
                break;
            this.swap(k, p);
            k = p;
        }
    }
    pop() {
        const d = this.d[0], i = this.i[0];
        const ld = this.d.pop(), li = this.i.pop();
        if (this.d.length) {
            this.d[0] = ld;
            this.i[0] = li;
            let k = 0;
            for (;;) {
                const l = 2 * k + 1, r = l + 1;
                let m = k;
                if (l < this.d.length && this.less(l, m)) m = l;
                if (r < this.d.length && this.less(r, m)) m = r;
                if (m === k)
                    break;
                this.swap(k, m);
                k = m;
            }
        }
        return [d, i];
    }
}

// Lee los píxeles RGBA de 8 bits de un rectángulo dentro de la imagen: fetch(box) → Uint8ClampedArray.
export class MagneticTracer {
    constructor(bounds, fetch) {
        this.bounds = bounds;
        this.fetch = fetch;
        this.blocks = new Map();
    }

    clampPoint(p) {
        const b = this.bounds;
        if (boxEmpty(b) || !Number.isFinite(p[0]) || !Number.isFinite(p[1]))
            return null;
        return [Math.min(Math.max(p[0], b.x0), b.x1), Math.min(Math.max(p[1], b.y0), b.y1)];
    }

    pixel(p) {
        const b = this.bounds;
        const c = (v, lo, hi) => (Number.isFinite(v) ? Math.min(Math.max(Math.floor(v), lo), Math.max(hi - 1, lo)) : lo);
        return [c(p[0], b.x0, b.x1), c(p[1], b.y0, b.y1)];
    }

    pixels(r) {
        const w = boxW(r), out = new Uint8ClampedArray(w * boxH(r) * 4);
        if (boxEmpty(r))
            return out;
        if (this.blocks.size > MAX_BLOCKS)
            this.blocks.clear();
        const bx0 = Math.floor(r.x0 / BLOCK), by0 = Math.floor(r.y0 / BLOCK);
        const bx1 = Math.floor((r.x1 - 1) / BLOCK), by1 = Math.floor((r.y1 - 1) / BLOCK);
        for (let by = by0; by <= by1; ++by)
            for (let bx = bx0; bx <= bx1; ++bx) {
                const br = boxIntersect(box(bx * BLOCK, by * BLOCK, (bx + 1) * BLOCK, (by + 1) * BLOCK), this.bounds);
                if (boxEmpty(br))
                    continue;
                const key = bx + "," + by;
                let block = this.blocks.get(key);
                if (!block) {
                    block = this.fetch(br);
                    if (block.length !== boxW(br) * boxH(br) * 4)
                        block = new Uint8ClampedArray(boxW(br) * boxH(br) * 4);
                    this.blocks.set(key, block);
                }
                const ov = boxIntersect(br, r), len = boxW(ov) * 4;
                for (let y = ov.y0; y < ov.y1; ++y) {
                    const src = ((y - br.y0) * boxW(br) + (ov.x0 - br.x0)) * 4, dst = ((y - r.y0) * w + (ov.x0 - r.x0)) * 4;
                    out.set(block.subarray(src, src + len), dst);
                }
            }
        return out;
    }

    field(area) {
        const ext = boxIntersect(boxInflate(area, 2), this.bounds);
        return makeField(area, ext, this.pixels(ext));
    }

    // Dónde va un punto de anclaje junto a 'p': el centro del borde más destacado dentro de la
    // anchura de detección, o 'p'.
    snap(p0, s) {
        s = magneticSettings(s.width, s.contrast);
        const p = this.clampPoint(p0);
        if (!p)
            return null;
        const r = s.width, [px, py] = this.pixel(p), ri = Math.ceil(r) + 1;
        const area = boxIntersect(box(px - ri, py - ri, px + ri + 1, py + ri + 1), this.bounds);
        const f = this.field(area);
        let best = null, bestScore = -Infinity;
        for (let y = area.y0; y < area.y1; ++y)
            for (let x = area.x0; x < area.x1; ++x) {
                const i = fieldIndex(f, x, y);
                if (i < 0)
                    continue;
                const strength = f.strength[i];
                if (strength < s.contrast || !f.ridge[i])
                    continue;
                const d = dist([x + 0.5, y + 0.5], p);
                if (d > r)
                    continue;
                const score = strength * (1 - 0.25 * (d / r));
                if (best === null || score > bestScore) {
                    bestScore = score;
                    best = fieldPoint(f, i, x, y, s.contrast);
                }
            }
        return best || p;
    }

    tracePiece(a, b, guide, s) {
        const rad = s.width + 1.5;
        const [ax, ay] = this.pixel(a), [bx, by] = this.pixel(b);
        let bbox = box(Math.min(ax, bx), Math.min(ay, by), Math.max(ax, bx) + 1, Math.max(ay, by) + 1);
        for (const q of guide) {
            const [x, y] = this.pixel(q);
            bbox = boxUnite(bbox, box(x, y, x + 1, y + 1));
        }
        const area = boxIntersect(boxInflate(bbox, Math.ceil(rad) + 1), this.bounds);
        const f = this.field(area), near = corridor(area, guide, rad);
        const start = fieldIndex(f, ax, ay), goal = fieldIndex(f, bx, by);
        if (start < 0 || goal < 0)
            return [a, b];
        near[start] = 0;
        near[goal] = 0;
        const w = f.w, h = boxH(area), n = w * h, rad2 = rad * rad, contrast = s.contrast;
        const node = new Float32Array(n).fill(Infinity), tangent = new Float32Array(n * 2);
        for (let i = 0; i < n; ++i) {
            const d2 = near[i];
            if (!Number.isFinite(d2))
                continue;
            const st = f.strength[i], onEdge = st >= contrast && f.ridge[i];
            const good = Math.min(Math.max((st - contrast) / Math.max(contrast, 0.02), 0), 1) * (0.6 + 0.4 * Math.min(st, 1));
            const pull = Math.min(Math.sqrt(d2 / Math.max(rad2, 1)), 1);
            node[i] = W_CENTRE * (onEdge ? 0 : 1) + W_STRENGTH * (1 - good) + W_LENGTH + W_GUIDE * pull;
            if (st >= 0.5 * contrast) {
                tangent[i * 2] = f.grad[i * 2 + 1];
                tangent[i * 2 + 1] = -f.grad[i * 2];
            }
        }
        const dv = new Float64Array(n).fill(Infinity), from = new Uint8Array(n).fill(255);
        dv[start] = 0;
        const heap = new MinHeap();
        heap.push(0, start);
        while (heap.size) {
            const [d, i] = heap.pop();
            if (d > dv[i])
                continue;
            if (i === goal)
                break;
            const x = i % w, y = (i / w) | 0, tp0 = tangent[i * 2], tp1 = tangent[i * 2 + 1];
            for (let k = 0; k < 8; ++k) {
                const dx = DIRS[k][0], dy = DIRS[k][1], nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                const j = ny * w + nx, cost = node[j];
                if (!Number.isFinite(cost))
                    continue;
                const len = dx !== 0 && dy !== 0 ? Math.SQRT2 : 1;
                const nd = d + cost * len + W_DIRECTION * directionCost(tp0, tp1, tangent[j * 2], tangent[j * 2 + 1], dx, dy, len);
                if (nd < dv[j]) {
                    dv[j] = nd;
                    from[j] = k;
                    heap.push(nd, j);
                }
            }
        }
        if (!Number.isFinite(dv[goal]))
            return [a, b];
        const cells = [goal];
        let cur = goal;
        while (cur !== start && cells.length <= n) {
            if (from[cur] >= 8)
                return [a, b];
            const x = (cur % w) - DIRS[from[cur]][0], y = ((cur / w) | 0) - DIRS[from[cur]][1];
            if (x < 0 || y < 0)
                return [a, b];
            cur = y * w + x;
            cells.push(cur);
        }
        cells.reverse();
        const pts = cells.map((c) => fieldPoint(f, c, (c % w) + area.x0, ((c / w) | 0) + area.y0, contrast));
        if (pts.length <= 1)
            return [a, b];
        pts[0] = a;
        pts[pts.length - 1] = b;
        return pts;
    }

    // El borde de 'from' a 'to' por los bordes cerca de 'guide' (el recorrido del puntero).
    trace(from0, to0, guide, s) {
        s = magneticSettings(s.width, s.contrast);
        const from = this.clampPoint(from0), to = this.clampPoint(to0);
        if (!from || !to)
            return [];
        let g = [from];
        for (const q of guide) {
            const c = this.clampPoint(q);
            if (c)
                g.push(c);
        }
        g.push(to);
        g = simplifyPolyline(g, 0.5);
        if (polylineLength(g) > MAX_GUIDE_LENGTH)
            return g;
        const pieces = split(g, PIECE);
        const out = [from];
        let cur = from;
        for (let k = 0; k < pieces.length; ++k) {
            let target = to;
            if (k !== pieces.length - 1 && pieces[k].length)
                target = this.snap(pieces[k][pieces[k].length - 1], s) || to;
            const part = this.tracePiece(cur, target, pieces[k], s);
            for (let i = 1; i < part.length; ++i)
                out.push(part[i]);
            cur = target;
        }
        return simplifyPolyline(out, 0.5);
    }
}
