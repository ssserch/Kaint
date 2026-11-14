// Retoque de PhotoCraft (como core/retouch/Segment.cpp de Kaint): la Selección de objetos.
// Algoritmos de PhotoCraft (licencia MIT).
//
// · GrabCut (Rother, Kolmogorov y Blake): trimapa, mezclas de 5 gaussianas por lado e iteraciones
//   «asignar → aprender → corte mínimo» con el término de suavidad γ·e^(−β‖z_m − z_n‖²)/dist.
// · Corte mínimo: Boykov–Kolmogorov, o empujar/reetiquetar FIFO en grafos grandes.
// · Buscador de objetos: crece la zona de color parecido bajo el puntero y GrabCut la afina.
// · Seleccionar sujeto: superpíxeles SLIC, prominencia (Zhu et al.; Achanta et al.), Otsu y dos
//   GrabCut. No es un segmentador semántico.
//
// Un «sampler» lee RGBA directos (0..1) de un rectángulo: sampler(box) → Float32Array (w·h·4).
import { NEVER, XorRng, box, boxContains, boxH, boxInflate, boxIntersect, boxW } from "./Common.js";

// ---------------------------------------------------------------------------------------------
// Imágenes RGB (Float32Array de w·h·3, 0..1).

function fromRgba(px, w, h) {
    const out = new Float32Array(w * h * 3);
    for (let i = 0; i < w * h; ++i) {
        const a = Math.min(Math.max(px[i * 4 + 3], 0), 1);
        for (let c = 0; c < 3; ++c)
            out[i * 3 + c] = Math.min(Math.max(px[i * 4 + c], 0), 1) * a + 0.5 * (1 - a);
    }
    return { w, h, px: out };
}

function downsampleRgb(img, step) {
    if (step <= 1)
        return img;
    const ow = Math.ceil(img.w / step), oh = Math.ceil(img.h / step), px = new Float32Array(ow * oh * 3);
    for (let oy = 0; oy < oh; ++oy)
        for (let ox = 0; ox < ow; ++ox) {
            let r = 0, g = 0, b = 0, n = 0;
            for (let y = oy * step; y < Math.min((oy + 1) * step, img.h); ++y)
                for (let x = ox * step; x < Math.min((ox + 1) * step, img.w); ++x) {
                    const i = (y * img.w + x) * 3;
                    r += img.px[i]; g += img.px[i + 1]; b += img.px[i + 2];
                    ++n;
                }
            const o = (oy * ow + ox) * 3;
            px[o] = r / n; px[o + 1] = g / n; px[o + 2] = b / n;
        }
    return { w: ow, h: oh, px };
}

const readRgb = (sampler, r) => fromRgba(sampler(r), boxW(r), boxH(r));

function readRgbScaled(sampler, r, step) {
    if (step <= 1)
        return readRgb(sampler, r);
    const w = boxW(r), h = boxH(r), ow = Math.ceil(w / step), oh = Math.ceil(h / step);
    const rowsPer = Math.max(Math.ceil(64 / step), 1), px = new Float32Array(ow * oh * 3);
    for (let oy0 = 0; oy0 < oh; oy0 += rowsPer) {
        const oy1 = Math.min(oy0 + rowsPer, oh);
        const y0 = r.y0 + oy0 * step, y1 = Math.min(r.y0 + oy1 * step, r.y1);
        px.set(downsampleRgb(readRgb(sampler, box(r.x0, y0, r.x1, y1)), step).px, oy0 * ow * 3);
    }
    return { w: ow, h: oh, px };
}

const scaleFor = (area, maxPx) => Math.max(Math.ceil(Math.sqrt((boxW(area) * boxH(area)) / Math.max(maxPx, 1))), 1);

function d2(p, i, q, j) {
    const x = p[i] - q[j], y = p[i + 1] - q[j + 1], z = p[i + 2] - q[j + 2];
    return x * x + y * y + z * z;
}

function subsample(v, max) {
    // v: Float32Array de tríos
    const n = v.length / 3;
    if (n <= max)
        return v;
    const k = Math.ceil(n / max), out = [];
    for (let i = 0; i < n; i += k)
        out.push(v[i * 3], v[i * 3 + 1], v[i * 3 + 2]);
    return Float32Array.from(out);
}

// ---------------------------------------------------------------------------------------------
// Mezclas de gaussianas.

const DEFAULT_REG = 2e-5;

function makeComponent(weight, mean, cov, reg) {
    const c = cov.map((row, i) => row.map((v, j) => v + (i === j ? reg : 0)));
    const det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0])
              + c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
    if (det <= 1e-30 || weight <= 0)
        return null;
    const inv = [
        [(c[1][1] * c[2][2] - c[1][2] * c[2][1]) / det, (c[0][2] * c[2][1] - c[0][1] * c[2][2]) / det, (c[0][1] * c[1][2] - c[0][2] * c[1][1]) / det],
        [(c[1][2] * c[2][0] - c[1][0] * c[2][2]) / det, (c[0][0] * c[2][2] - c[0][2] * c[2][0]) / det, (c[0][2] * c[1][0] - c[0][0] * c[1][2]) / det],
        [(c[1][0] * c[2][1] - c[1][1] * c[2][0]) / det, (c[0][1] * c[2][0] - c[0][0] * c[2][1]) / det, (c[0][0] * c[1][1] - c[0][1] * c[1][0]) / det],
    ];
    const logNorm = Math.log(weight) - 0.5 * Math.log(det) - 1.5 * Math.log(2 * Math.PI);
    return { weight: Math.fround(weight), mean: mean.map(Math.fround), inv, logNorm };
}

function logWeighted(k, p, i) {
    const d0 = Math.fround(p[i] - k.mean[0]), d1 = Math.fround(p[i + 1] - k.mean[1]), d2v = Math.fround(p[i + 2] - k.mean[2]);
    const m = k.inv;
    const q = d0 * (m[0][0] * d0 + m[0][1] * d1 + m[0][2] * d2v) + d1 * (m[1][0] * d0 + m[1][1] * d1 + m[1][2] * d2v)
            + d2v * (m[2][0] * d0 + m[2][1] * d1 + m[2][2] * d2v);
    return k.logNorm - 0.5 * q;
}

class Acc {
    constructor() {
        this.n = 0;
        this.s = [0, 0, 0];
        this.ss = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
    }
    add(p, i, w) {
        const z = [p[i], p[i + 1], p[i + 2]];
        this.n += w;
        for (let a = 0; a < 3; ++a) {
            this.s[a] += w * z[a];
            for (let b = 0; b < 3; ++b)
                this.ss[a][b] += w * z[a] * z[b];
        }
    }
    component(total, reg) {
        if (this.n <= 0)
            return null;
        const m = this.s.map((v) => v / this.n);
        const c = this.ss.map((row, a) => row.map((v, b) => v / this.n - m[a] * m[b]));
        return makeComponent(this.n / total, m, c, reg);
    }
}

function nearest(centers, p, i) {
    let best = 0, bd = Infinity;
    for (let k = 0; k < centers.length; ++k) {
        const d = d2(centers[k], 0, p, i);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}

function kmeans(samples, k, iters) {
    const n = samples.length / 3, rng = new XorRng(0x5eed + n);
    const pick0 = rng.nextMod(n);
    let centers = [Float32Array.of(samples[pick0 * 3], samples[pick0 * 3 + 1], samples[pick0 * 3 + 2])];
    const dist = new Float32Array(n);
    for (let i = 0; i < n; ++i)
        dist[i] = d2(samples, i * 3, centers[0], 0);
    while (centers.length < k) {
        let total = 0;
        for (let i = 0; i < n; ++i)
            total += dist[i];
        if (total <= 1e-12)
            break;
        let t = rng.unit() * total, pick = n - 1;
        for (let i = 0; i < n; ++i) {
            t -= dist[i];
            if (t <= 0) {
                pick = i;
                break;
            }
        }
        const c = Float32Array.of(samples[pick * 3], samples[pick * 3 + 1], samples[pick * 3 + 2]);
        centers.push(c);
        for (let i = 0; i < n; ++i)
            dist[i] = Math.min(dist[i], d2(samples, i * 3, c, 0));
    }
    for (let it = 0; it < iters; ++it) {
        const sums = centers.map(() => [0, 0, 0, 0]);
        for (let i = 0; i < n; ++i) {
            const q = nearest(centers, samples, i * 3);
            for (let c = 0; c < 3; ++c)
                sums[q][c] += samples[i * 3 + c];
            sums[q][3] += 1;
        }
        const fresh = sums.filter((s) => s[3] > 0).map((s) => Float32Array.of(s[0] / s[3], s[1] / s[3], s[2] / s[3]));
        const done = fresh.length === centers.length && fresh.every((f, q) => f[0] === centers[q][0] && f[1] === centers[q][1] && f[2] === centers[q][2]);
        centers = fresh;
        if (done)
            break;
    }
    return centers;
}

class Gmm {
    constructor(comps, reg) {
        this.comps = comps;
        this.reg = reg;
    }
    static fromLabels(samples, labels, k, reg) {
        const acc = Array.from({ length: k }, () => new Acc());
        const n = samples.length / 3;
        for (let i = 0; i < n; ++i)
            acc[labels[i]].add(samples, i * 3, 1);
        const comps = acc.map((a) => a.component(n, reg)).filter(Boolean);
        return comps.length ? new Gmm(comps, reg) : null;
    }
    static fit(samples, k, reg, emIters = 3) {
        const n = samples.length / 3;
        if (!n)
            return null;
        k = Math.min(Math.max(k, 1), n);
        const centers = kmeans(samples, k, 10);
        const labels = new Int32Array(n);
        for (let i = 0; i < n; ++i)
            labels[i] = nearest(centers, samples, i * 3);
        let g = Gmm.fromLabels(samples, labels, centers.length, reg);
        if (!g)
            return null;
        for (let i = 0; i < emIters; ++i)
            g = g.emStep(samples) || g;
        return g;
    }
    emStep(samples) {
        const k = this.comps.length, n = samples.length / 3;
        const acc = Array.from({ length: k }, () => new Acc()), lw = new Float64Array(k);
        for (let i = 0; i < n; ++i) {
            let mx = -Infinity;
            for (let c = 0; c < k; ++c) {
                lw[c] = logWeighted(this.comps[c], samples, i * 3);
                mx = Math.max(mx, lw[c]);
            }
            let sum = 0;
            for (let c = 0; c < k; ++c) {
                lw[c] = Math.exp(lw[c] - mx);
                sum += lw[c];
            }
            for (let c = 0; c < k; ++c) {
                const r = lw[c] / sum;
                if (r > 1e-6)
                    acc[c].add(samples, i * 3, r);
            }
        }
        const comps = acc.map((a) => a.component(n, this.reg)).filter(Boolean);
        return comps.length ? new Gmm(comps, this.reg) : null;
    }
    componentOf(p, i) {
        let best = 0, bv = -Infinity;
        for (let c = 0; c < this.comps.length; ++c) {
            const v = logWeighted(this.comps[c], p, i);
            if (v > bv) {
                bv = v;
                best = c;
            }
        }
        return best;
    }
    refit(samples) {
        const n = samples.length / 3, labels = new Int32Array(n);
        for (let i = 0; i < n; ++i)
            labels[i] = this.componentOf(samples, i * 3);
        return Gmm.fromLabels(samples, labels, this.comps.length, this.reg);
    }
    negLog(p, i) {
        let mx = -Infinity;
        const n = Math.min(this.comps.length, 16), vals = new Float64Array(n);
        for (let c = 0; c < n; ++c) {
            vals[c] = logWeighted(this.comps[c], p, i);
            mx = Math.max(mx, vals[c]);
        }
        let s = 0;
        for (let c = 0; c < n; ++c)
            s += Math.exp(vals[c] - mx);
        return Math.fround(Math.min(Math.max(-(mx + Math.log(s)), -50), 200));
    }
}

// ---------------------------------------------------------------------------------------------
// Flujo máximo / corte mínimo.

const NONE = 0xffffffff, TERMINAL = 0xfffffffe, ORPHAN = 0xfffffffd, INF_DIST = 0xffffffff;
const EPS = 1e-5, CANCEL_EVERY = 1024;

class Graph {
    constructor(nodes, edges) {
        this.first = new Uint32Array(nodes).fill(NONE);
        this.trCap = new Float32Array(nodes);
        this.parent = new Uint32Array(nodes).fill(NONE);
        this.isSink = new Uint8Array(nodes);
        this.ts = new Uint32Array(nodes);
        this.dist = new Uint32Array(nodes);
        this.active = new Uint8Array(nodes);
        this.cap = edges * 2;
        this.head = new Uint32Array(this.cap);
        this.next = new Uint32Array(this.cap);
        this.rCap = new Float32Array(this.cap);
        this.edges = 0;
        this.flow = 0;
    }
    get nodeCount() {
        return this.first.length;
    }
    addTWeights(i, capSource, capSink) {
        const delta = this.trCap[i];
        let s = capSource, t = capSink;
        if (delta > 0) s += delta;
        else t -= delta;
        this.flow += Math.min(s, t);
        this.trCap[i] = s - t;
    }
    grow() {
        const cap = this.cap * 2 + 8;
        for (const key of ["head", "next"]) {
            const a = new Uint32Array(cap);
            a.set(this[key]);
            this[key] = a;
        }
        const r = new Float32Array(cap);
        r.set(this.rCap);
        this.rCap = r;
        this.cap = cap;
    }
    addEdge(i, j, cap, revCap) {
        if (this.edges + 2 > this.cap)
            this.grow();
        const a = this.edges;
        this.head[a] = j; this.next[a] = this.first[i]; this.rCap[a] = cap; this.first[i] = a;
        this.head[a + 1] = i; this.next[a + 1] = this.first[j]; this.rCap[a + 1] = revCap; this.first[j] = a + 1;
        this.edges += 2;
    }
    inSource(i) {
        return this.parent[i] !== NONE && !this.isSink[i];
    }
    maxflow(stop) {
        if (this.nodeCount >= 4096) {
            let source = false, sink = false;
            for (const c of this.trCap) {
                if (!Number.isFinite(c))
                    return this.bk(stop);
                source = source || c > EPS;
                sink = sink || c < -EPS;
            }
            let ok = true;
            for (let a = 0; a < this.edges; ++a)
                if (!Number.isFinite(this.rCap[a]) || this.rCap[a] < 0) {
                    ok = false;
                    break;
                }
            if (source && sink && ok)
                return this.pushRelabel(stop);
        }
        return this.bk(stop);
    }
    bk(stop) {
        const n = this.nodeCount, queue = new Deque(), orphans = new Deque();
        for (let i = 0; i < n; ++i) {
            this.active[i] = 0;
            this.ts[i] = 0;
            if (Math.abs(this.trCap[i]) > EPS) {
                this.isSink[i] = this.trCap[i] < 0 ? 1 : 0;
                this.parent[i] = TERMINAL;
                this.dist[i] = 1;
                this.active[i] = 1;
                queue.pushBack(i);
            } else {
                this.parent[i] = NONE;
            }
        }
        let time = 0, steps = 0;
        while (queue.size) {
            const i = queue.popFront();
            if (++steps % CANCEL_EVERY === 0 && stop.requested())
                return false;
            this.active[i] = 0;
            if (this.parent[i] === NONE)
                continue;
            let contact = NONE;
            const sink = this.isSink[i];
            for (let a = this.first[i]; a !== NONE; a = this.next[a]) {
                const j = this.head[a], cap = sink ? this.rCap[a ^ 1] : this.rCap[a];
                if (cap > EPS) {
                    if (this.parent[j] === NONE) {
                        this.isSink[j] = sink;
                        this.parent[j] = a ^ 1;
                        this.ts[j] = this.ts[i];
                        this.dist[j] = this.dist[i] + 1;
                        if (!this.active[j]) {
                            this.active[j] = 1;
                            queue.pushBack(j);
                        }
                    } else if (this.isSink[j] !== sink) {
                        contact = sink ? a ^ 1 : a;
                        break;
                    } else if (this.ts[j] <= this.ts[i] && this.dist[j] > this.dist[i]) {
                        this.parent[j] = a ^ 1;
                        this.ts[j] = this.ts[i];
                        this.dist[j] = this.dist[i] + 1;
                    }
                }
            }
            if (contact === NONE)
                continue;
            if (!this.active[i]) {
                this.active[i] = 1;
                queue.pushFront(i);
            }
            ++time;
            this.augment(contact, orphans);
            while (orphans.size)
                this.adopt(orphans.popFront(), time, queue, orphans);
        }
        return true;
    }
    augment(m, orphans) {
        const u = this.head[m ^ 1], v = this.head[m];
        let b = this.rCap[m], x = u;
        for (;;) {
            const a = this.parent[x];
            if (a === TERMINAL) break;
            b = Math.min(b, this.rCap[a ^ 1]);
            x = this.head[a];
        }
        b = Math.min(b, this.trCap[x]);
        x = v;
        for (;;) {
            const a = this.parent[x];
            if (a === TERMINAL) break;
            b = Math.min(b, this.rCap[a]);
            x = this.head[a];
        }
        b = Math.fround(Math.min(b, -this.trCap[x]));
        this.rCap[m ^ 1] += b;
        this.rCap[m] -= b;
        x = u;
        for (;;) {
            const a = this.parent[x];
            if (a === TERMINAL) break;
            this.rCap[a] += b;
            this.rCap[a ^ 1] -= b;
            const up = this.head[a];
            if (this.rCap[a ^ 1] <= EPS) {
                this.parent[x] = ORPHAN;
                orphans.pushBack(x);
            }
            x = up;
        }
        this.trCap[x] -= b;
        if (this.trCap[x] <= EPS) {
            this.parent[x] = ORPHAN;
            orphans.pushBack(x);
        }
        x = v;
        for (;;) {
            const a = this.parent[x];
            if (a === TERMINAL) break;
            this.rCap[a ^ 1] += b;
            this.rCap[a] -= b;
            const up = this.head[a];
            if (this.rCap[a] <= EPS) {
                this.parent[x] = ORPHAN;
                orphans.pushBack(x);
            }
            x = up;
        }
        this.trCap[x] += b;
        if (this.trCap[x] >= -EPS) {
            this.parent[x] = ORPHAN;
            orphans.pushBack(x);
        }
        this.flow += b;
    }
    adopt(i, time, queue, orphans) {
        const sink = this.isSink[i];
        let best = NONE, dMin = INF_DIST;
        for (let a = this.first[i]; a !== NONE; a = this.next[a]) {
            const cap = sink ? this.rCap[a] : this.rCap[a ^ 1], j = this.head[a];
            if (cap > EPS && this.isSink[j] === sink && this.parent[j] !== NONE) {
                let d = 0, k = j;
                for (;;) {
                    if (this.ts[k] === time) {
                        d = Math.min(d + this.dist[k], INF_DIST);
                        break;
                    }
                    const pa = this.parent[k];
                    ++d;
                    if (pa === TERMINAL) {
                        this.ts[k] = time;
                        this.dist[k] = 1;
                        break;
                    }
                    if (pa === ORPHAN) {
                        d = INF_DIST;
                        break;
                    }
                    k = this.head[pa];
                }
                if (d < INF_DIST) {
                    if (d < dMin) {
                        best = a;
                        dMin = d;
                    }
                    let kk = j, dd = d;
                    while (this.ts[kk] !== time) {
                        this.ts[kk] = time;
                        this.dist[kk] = dd;
                        --dd;
                        kk = this.head[this.parent[kk]];
                    }
                }
            }
        }
        if (best !== NONE) {
            this.parent[i] = best;
            this.ts[i] = time;
            this.dist[i] = dMin + 1;
            return;
        }
        this.parent[i] = NONE;
        for (let a = this.first[i]; a !== NONE; a = this.next[a]) {
            const j = this.head[a], pj = this.parent[j];
            if (this.isSink[j] === sink && pj !== NONE) {
                const cap = sink ? this.rCap[a] : this.rCap[a ^ 1];
                if (cap > EPS && !this.active[j]) {
                    this.active[j] = 1;
                    queue.pushBack(j);
                }
                if (pj !== TERMINAL && pj !== ORPHAN && this.head[pj] === i) {
                    this.parent[j] = ORPHAN;
                    orphans.pushBack(j);
                }
            }
        }
    }
    pushRelabel(stop) {
        const n = this.nodeCount, sourceHeight = n + 2;
        const back = new Float32Array(n), excess = new Float64Array(n);
        for (let i = 0; i < n; ++i) {
            this.active[i] = 0;
            if (this.trCap[i] > EPS) {
                back[i] = this.trCap[i];
                excess[i] = this.trCap[i];
                this.trCap[i] = 0;
            }
        }
        this.globalRelabel(sourceHeight, back);
        const queue = new Deque();
        for (let i = 0; i < n; ++i)
            if (excess[i] > EPS) {
                this.active[i] = 1;
                queue.pushBack(i);
            }
        let work = 0, steps = 0;
        const frequency = this.edges * 2 + n;
        while (queue.size) {
            const i = queue.popFront();
            if (++steps % CANCEL_EVERY === 0 && stop.requested())
                return false;
            this.active[i] = 0;
            while (excess[i] > EPS) {
                if (this.dist[i] === 1 && this.trCap[i] < -EPS) {
                    const delta = Math.min(Math.fround(excess[i]), -this.trCap[i]);
                    this.trCap[i] += delta;
                    excess[i] -= delta;
                    this.flow += delta;
                    if (excess[i] <= EPS)
                        break;
                }
                if (this.dist[i] === sourceHeight + 1 && back[i] > EPS) {
                    const delta = Math.min(Math.fround(excess[i]), back[i]);
                    back[i] -= delta;
                    this.trCap[i] += delta;
                    excess[i] -= delta;
                    if (excess[i] <= EPS)
                        break;
                }
                let a = this.parent[i];
                while (a !== NONE) {
                    const j = this.head[a];
                    ++work;
                    if (this.rCap[a] > EPS && this.dist[j] !== INF_DIST && this.dist[j] + 1 === this.dist[i]) {
                        const delta = Math.min(Math.fround(excess[i]), this.rCap[a]);
                        this.rCap[a] -= delta;
                        this.rCap[a ^ 1] += delta;
                        excess[i] -= delta;
                        excess[j] += delta;
                        if (!this.active[j] && excess[j] > EPS) {
                            this.active[j] = 1;
                            queue.pushBack(j);
                        }
                        if (excess[i] <= EPS)
                            break;
                    }
                    a = this.next[a];
                    this.parent[i] = a;
                }
                if (excess[i] <= EPS)
                    break;
                let height = this.trCap[i] < -EPS ? 0 : INF_DIST;
                if (back[i] > EPS)
                    height = Math.min(height, sourceHeight);
                for (let b = this.first[i]; b !== NONE; b = this.next[b]) {
                    ++work;
                    if (this.rCap[b] > EPS)
                        height = Math.min(height, this.dist[this.head[b]]);
                }
                if (height === INF_DIST)
                    break;
                this.dist[i] = height + 1;
                this.parent[i] = this.first[i];
            }
            if (work >= frequency) {
                this.globalRelabel(sourceHeight, back);
                work = 0;
            }
        }
        this.parent.fill(NONE);
        this.isSink.fill(1);
        const q = new Deque();
        for (let i = 0; i < n; ++i)
            if (this.trCap[i] > EPS) {
                this.parent[i] = TERMINAL;
                this.isSink[i] = 0;
                q.pushBack(i);
            }
        while (q.size) {
            const i = q.popFront();
            for (let a = this.first[i]; a !== NONE; a = this.next[a]) {
                const j = this.head[a];
                if (this.rCap[a] > EPS && this.parent[j] === NONE) {
                    this.parent[j] = TERMINAL;
                    this.isSink[j] = 0;
                    q.pushBack(j);
                }
            }
        }
        return true;
    }
    globalRelabel(sourceHeight, back) {
        this.dist.fill(INF_DIST);
        this.parent.set(this.first);
        const queue = new Deque();
        for (let i = 0; i < this.nodeCount; ++i)
            if (this.trCap[i] < -EPS) {
                this.dist[i] = 1;
                queue.pushBack(i);
            }
        const bfs = () => {
            while (queue.size) {
                const i = queue.popFront();
                for (let a = this.first[i]; a !== NONE; a = this.next[a]) {
                    const j = this.head[a];
                    if (this.rCap[a ^ 1] > EPS && this.dist[j] === INF_DIST) {
                        this.dist[j] = this.dist[i] + 1;
                        queue.pushBack(j);
                    }
                }
            }
        };
        bfs();
        for (let i = 0; i < back.length; ++i)
            if (back[i] > EPS && this.dist[i] === INF_DIST) {
                this.dist[i] = sourceHeight + 1;
                queue.pushBack(i);
            }
        bfs();
    }
}

// Cola doble de enteros.
class Deque {
    constructor() {
        this.buf = new Uint32Array(1024);
        this.head = 0;
        this.size = 0;
    }
    grow() {
        const b = new Uint32Array(this.buf.length * 2);
        for (let k = 0; k < this.size; ++k)
            b[k] = this.buf[(this.head + k) % this.buf.length];
        this.buf = b;
        this.head = 0;
    }
    pushBack(v) {
        if (this.size === this.buf.length)
            this.grow();
        this.buf[(this.head + this.size) % this.buf.length] = v;
        ++this.size;
    }
    pushFront(v) {
        if (this.size === this.buf.length)
            this.grow();
        this.head = (this.head - 1 + this.buf.length) % this.buf.length;
        this.buf[this.head] = v;
        ++this.size;
    }
    popFront() {
        const v = this.buf[this.head];
        this.head = (this.head + 1) % this.buf.length;
        --this.size;
        return v;
    }
}

// ---------------------------------------------------------------------------------------------
// Corte de una cuadrícula.

const FREE = 0, HARD_FG = 1, HARD_BG = 2;
const NB = [[1, 0, 1], [0, 1, 1], [1, 1, Math.SQRT2], [-1, 1, Math.SQRT2]];

function contrastBeta(img) {
    const { w, h, px } = img;
    let sum = 0, n = 0;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x)
            for (const [dx, dy] of NB) {
                const nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < w && ny < h) {
                    sum += d2(px, (y * w + x) * 3, px, (ny * w + nx) * 3);
                    ++n;
                }
            }
    return n === 0 || sum <= 1e-12 ? 0 : n / (2 * sum);
}

// Etiquetado binario por un corte mínimo; null si se canceló.
function gridCut(img, costFg, costBg, fixed, gamma, beta, stop) {
    const { w, h, px } = img, n = w * h;
    const idx = new Uint32Array(n).fill(NONE);
    let count = 0;
    for (let i = 0; i < n; ++i)
        if (fixed[i] === FREE)
            idx[i] = count++;
    const out = new Uint8Array(n);
    for (let i = 0; i < n; ++i)
        out[i] = fixed[i] === HARD_FG ? 1 : 0;
    if (!count)
        return out;
    if (stop.requested())
        return null;
    const g = new Graph(count, count * 4);
    for (let i = 0; i < n; ++i)
        if (idx[i] !== NONE)
            g.addTWeights(idx[i], costBg[i], costFg[i]);
    const fixedLink = (node, f, wt) => (f === HARD_FG ? g.addTWeights(node, wt, 0) : g.addTWeights(node, 0, wt));
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const i = y * w + x;
            for (const [dx, dy, len] of NB) {
                const nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny >= h)
                    continue;
                const j = ny * w + nx;
                if (idx[i] === NONE && idx[j] === NONE)
                    continue;
                const wt = Math.fround((gamma * Math.exp(-beta * d2(px, i * 3, px, j * 3))) / len);
                if (idx[i] !== NONE && idx[j] !== NONE)
                    g.addEdge(idx[i], idx[j], wt, wt);
                else if (idx[i] !== NONE)
                    fixedLink(idx[i], fixed[j], wt);
                else
                    fixedLink(idx[j], fixed[i], wt);
            }
        }
    if (stop.requested() || !g.maxflow(stop))
        return null;
    for (let i = 0; i < n; ++i)
        if (idx[i] !== NONE)
            out[i] = g.inSource(idx[i]) ? 1 : 0;
    return out;
}

function dataCosts(img, fg, bg) {
    const n = img.w * img.h, cf = new Float32Array(n), cb = new Float32Array(n);
    for (let i = 0; i < n; ++i) {
        cf[i] = fg.negLog(img.px, i * 3);
        cb[i] = bg.negLog(img.px, i * 3);
    }
    return [cf, cb];
}

function components(mask, w, h, eight) {
    const lab = new Uint32Array(w * h), sizes = [0], stack = [];
    for (let s = 0; s < w * h; ++s) {
        if (!mask[s] || lab[s])
            continue;
        const l = sizes.length;
        sizes.push(0);
        lab[s] = l;
        stack.push(s);
        while (stack.length) {
            const i = stack.pop();
            ++sizes[l];
            const x = i % w, y = (i / w) | 0;
            if (eight) {
                for (let dy = -1; dy <= 1; ++dy)
                    for (let dx = -1; dx <= 1; ++dx) {
                        const nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                            continue;
                        const j = ny * w + nx;
                        if (mask[j] && !lab[j]) {
                            lab[j] = l;
                            stack.push(j);
                        }
                    }
            } else {
                for (const j of [x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1])
                    if (j >= 0 && mask[j] && !lab[j]) {
                        lab[j] = l;
                        stack.push(j);
                    }
            }
        }
    }
    return [lab, sizes];
}

function keepSeeded(mask, seeds, w, h) {
    const [lab, sizes] = components(mask, w, h, true), keep = new Uint8Array(sizes.length);
    for (let i = 0; i < lab.length; ++i)
        if (seeds[i] && lab[i])
            keep[lab[i]] = 1;
    const out = new Uint8Array(lab.length);
    for (let i = 0; i < lab.length; ++i)
        out[i] = lab[i] && keep[lab[i]] ? 1 : 0;
    return out;
}

function borderTouches(hl, labels, w, h) {
    const t = new Uint8Array(labels);
    for (let x = 0; x < w; ++x) {
        t[hl[x]] = 1;
        t[hl[(h - 1) * w + x]] = 1;
    }
    for (let y = 0; y < h; ++y) {
        t[hl[y * w]] = 1;
        t[hl[y * w + w - 1]] = 1;
    }
    return t;
}

function cleanMask(mask, w, h, frac, holeFrac) {
    const [lab, sizes] = components(mask, w, h, true);
    let largest = 0;
    for (let i = 1; i < sizes.length; ++i)
        largest = Math.max(largest, sizes[i]);
    const out = new Uint8Array(w * h);
    if (!largest)
        return out;
    const min = Math.ceil(largest * frac);
    let area = 0;
    for (let i = 0; i < lab.length; ++i) {
        out[i] = lab[i] && sizes[lab[i]] >= min ? 1 : 0;
        area += out[i];
    }
    const maxHole = Math.floor(area * holeFrac);
    const inv = out.map((v) => (v ? 0 : 1));
    const [hl, hs] = components(inv, w, h, false), touches = borderTouches(hl, hs.length, w, h);
    for (let i = 0; i < out.length; ++i)
        if (hl[i] && !touches[hl[i]] && hs[hl[i]] <= maxHole)
            out[i] = 1;
    return out;
}

function fillHolesByModel(mask, img, fg, bg, speckFrac, holeFrac) {
    const { w, h } = img, out = new Uint8Array(mask);
    let area = 0;
    for (const v of out)
        area += v;
    const maxSpeck = Math.floor(area * speckFrac), maxHole = Math.floor(area * holeFrac);
    const inv = out.map((v) => (v ? 0 : 1));
    const [hl, hs] = components(inv, w, h, false), touches = borderTouches(hl, hs.length, w, h);
    const cf = new Float64Array(hs.length), cb = new Float64Array(hs.length);
    for (let i = 0; i < hl.length; ++i) {
        const l = hl[i];
        if (!l || touches[l] || hs[l] > maxHole || hs[l] <= maxSpeck)
            continue;
        cf[l] += fg.negLog(img.px, i * 3);
        cb[l] += bg.negLog(img.px, i * 3);
    }
    for (let i = 0; i < out.length; ++i) {
        const l = hl[i];
        if (!l || touches[l] || hs[l] > maxHole)
            continue;
        if (hs[l] <= maxSpeck || cf[l] <= cb[l])
            out[i] = 1;
    }
    return out;
}

function antialiasMask(mask, w, h) {
    const src = new Uint8Array(mask);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const v = src[y * w + x];
            let sum = 0, edge = false;
            for (let dy = -1; dy <= 1; ++dy)
                for (let dx = -1; dx <= 1; ++dx) {
                    const nx = x + dx, ny = y + dy;
                    const nv = nx < 0 || ny < 0 || nx >= w || ny >= h ? v : src[ny * w + nx];
                    edge = edge || nv !== v;
                    sum += nv;
                }
            if (edge) {
                const avg = sum / (9 * 255), f = v > 127 ? 0.5 + 0.5 * avg : 0.5 * avg;
                mask[y * w + x] = Math.round(f * 255);
            }
        }
}

function trimRegion(r) {
    const w = boxW(r.box);
    if (!w)
        return null;
    const h = r.mask.length / w;
    let x0 = Infinity, y0 = Infinity, x1 = 0, y1 = 0;
    for (let y = 0; y < h; ++y) {
        let a = -1, b = -1;
        for (let x = 0; x < w; ++x)
            if (r.mask[y * w + x]) {
                if (a < 0) a = x;
                b = x;
            }
        if (a >= 0) {
            x0 = Math.min(x0, a); x1 = Math.max(x1, b + 1); y0 = Math.min(y0, y); y1 = y + 1;
        }
    }
    if (x0 === Infinity)
        return null;
    if (x0 === 0 && y0 === 0 && x1 === w && y1 === h)
        return r;
    const nw = x1 - x0, mask = new Uint8Array(nw * (y1 - y0));
    for (let y = y0; y < y1; ++y)
        mask.set(r.mask.subarray(y * w + x0, y * w + x1), (y - y0) * nw);
    const b = r.box;
    return { box: box(b.x0 + x0, b.y0 + y0, b.x0 + x1, b.y0 + y1), mask };
}

function refineBand(sampler, window, step, low, band, lw, lh, fg, bg, mask, stop) {
    const T = 192, HALO = 12, w = boxW(window);
    const block = (x, y) => Math.min(((y - window.y0) / step) | 0, lh - 1) * lw + Math.min(((x - window.x0) / step) | 0, lw - 1);
    for (let ty = window.y0; ty < window.y1; ty += T)
        for (let tx = window.x0; tx < window.x1; tx += T) {
            const t = box(tx, ty, Math.min(tx + T, window.x1), Math.min(ty + T, window.y1));
            const bx0 = ((t.x0 - window.x0) / step) | 0, by0 = ((t.y0 - window.y0) / step) | 0;
            const bx1 = Math.min(((t.x1 - 1 - window.x0) / step) | 0, lw - 1), by1 = Math.min(((t.y1 - 1 - window.y0) / step) | 0, lh - 1);
            let any = false;
            for (let by = by0; by <= by1 && !any; ++by)
                for (let bx = bx0; bx <= bx1; ++bx)
                    if (band[by * lw + bx]) {
                        any = true;
                        break;
                    }
            if (!any)
                continue;
            stop.check();
            const r = boxIntersect(boxInflate(t, HALO), window), img = readRgb(sampler, r), rw = boxW(r), rh = boxH(r);
            const fixed = new Uint8Array(rw * rh), cfg = new Float32Array(rw * rh), cbg = new Float32Array(rw * rh);
            for (let yy = 0; yy < rh; ++yy)
                for (let xx = 0; xx < rw; ++xx) {
                    const i = yy * rw + xx, b = block(r.x0 + xx, r.y0 + yy), lab = low[b];
                    if (!band[b]) {
                        fixed[i] = lab ? HARD_FG : HARD_BG;
                        continue;
                    }
                    cfg[i] = fg.negLog(img.px, i * 3) + (lab ? 0 : 0.4);
                    cbg[i] = bg.negLog(img.px, i * 3) + (lab ? 0.4 : 0);
                }
            const cut = gridCut(img, cfg, cbg, fixed, 50, contrastBeta(img), stop);
            if (!cut)
                stop.check();
            for (let yy = 0; yy < boxH(t); ++yy) {
                const sy = t.y0 - r.y0 + yy, sx = t.x0 - r.x0, o = (t.y0 - window.y0 + yy) * w + (t.x0 - window.x0);
                for (let xx = 0; xx < boxW(t); ++xx)
                    mask[o + xx] = cut && cut[sy * rw + sx + xx] ? 255 : 0;
            }
        }
}

function finishRegion(sampler, window, step, low, lw, lh, fg, bg, stop) {
    const w = boxW(window), h = boxH(window), mask = new Uint8Array(w * h);
    if (step <= 1) {
        for (let i = 0; i < mask.length; ++i)
            mask[i] = low[i] ? 255 : 0;
    } else {
        const band = new Uint8Array(lw * lh);
        for (let y = 0; y < lh; ++y)
            for (let x = 0; x < lw; ++x) {
                const v = low[y * lw + x];
                let diff = false;
                for (let dy = -1; dy <= 1 && !diff; ++dy)
                    for (let dx = -1; dx <= 1; ++dx) {
                        const nx = x + dx, ny = y + dy;
                        if (nx >= 0 && ny >= 0 && nx < lw && ny < lh && low[ny * lw + nx] !== v) {
                            diff = true;
                            break;
                        }
                    }
                band[y * lw + x] = diff ? 1 : 0;
            }
        for (let y = 0; y < h; ++y) {
            const ly = Math.min((y / step) | 0, lh - 1);
            for (let x = 0; x < w; ++x)
                mask[y * w + x] = low[ly * lw + Math.min((x / step) | 0, lw - 1)] ? 255 : 0;
        }
        if (fg && bg)
            refineBand(sampler, window, step, low, band, lw, lh, fg, bg, mask, stop);
    }
    antialiasMask(mask, w, h);
    return trimRegion({ box: window, mask });
}

// ---------------------------------------------------------------------------------------------
// GrabCut.

const BG = 0, FG = 1, PR_BG = 2, PR_FG = 3, GAMMA = 50, K = 5, MAX_SAMPLES = 40000;
const isFg = (t) => t === FG || t === PR_FG;

function grabcut(img, trimap, iters, stop) {
    const beta = contrastBeta(img);
    const fixed = trimap.map((t) => (t === FG ? HARD_FG : t === BG ? HARD_BG : FREE));
    let models = null;
    for (let it = 0; it < Math.max(iters, 1); ++it) {
        stop.check();
        const fs = [], bs = [];
        for (let i = 0; i < trimap.length; ++i)
            (isFg(trimap[i]) ? fs : bs).push(img.px[i * 3], img.px[i * 3 + 1], img.px[i * 3 + 2]);
        const fsa = subsample(Float32Array.from(fs), MAX_SAMPLES), bsa = subsample(Float32Array.from(bs), MAX_SAMPLES);
        let fg, bg;
        if (!models) {
            fg = Gmm.fit(fsa, K, DEFAULT_REG);
            bg = Gmm.fit(bsa, K, DEFAULT_REG);
            if (!fg || !bg)
                return null;
        } else {
            fg = models[0].refit(fsa) || models[0];
            bg = models[1].refit(bsa) || models[1];
        }
        const [cf, cb] = dataCosts(img, fg, bg);
        const cut = gridCut(img, cf, cb, fixed, GAMMA, beta, stop);
        if (!cut)
            stop.check();
        let changed = 0;
        for (let i = 0; i < trimap.length; ++i)
            if (trimap[i] === PR_BG || trimap[i] === PR_FG) {
                const nt = cut && cut[i] ? PR_FG : PR_BG;
                if (nt !== trimap[i])
                    ++changed;
                trimap[i] = nt;
            }
        models = [fg, bg];
        if (changed * 2000 < trimap.length)
            break;
    }
    return models;
}

// ---------------------------------------------------------------------------------------------
// CIELAB, SLIC y prominencia.

function rgbToLab(r0, g0, b0) {
    const lin = (v) => (v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4));
    const r = lin(r0), g = lin(g0), b = lin(b0);
    const x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const y = 0.2126729 * r + 0.7151522 * g + 0.072175 * b;
    const z = (0.0193339 * r + 0.119192 * g + 0.9503041 * b) / 1.08883;
    const f = (t) => (t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116);
    const fx = f(x), fy = f(y), fz = f(z);
    return [116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)];
}

function labImage(px, n) {
    const lab = new Float32Array(n * 3);
    for (let i = 0; i < n; ++i) {
        const l = rgbToLab(px[i * 3], px[i * 3 + 1], px[i * 3 + 2]);
        lab[i * 3] = l[0]; lab[i * 3 + 1] = l[1]; lab[i * 3 + 2] = l[2];
    }
    return lab;
}

function box3(px, w, h) {
    const out = new Float32Array(px.length);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            let r = 0, g = 0, b = 0, n = 0;
            for (let yy = Math.max(y - 1, 0); yy < Math.min(y + 2, h); ++yy)
                for (let xx = Math.max(x - 1, 0); xx < Math.min(x + 2, w); ++xx) {
                    const i = (yy * w + xx) * 3;
                    r += px[i]; g += px[i + 1]; b += px[i + 2];
                    ++n;
                }
            const o = (y * w + x) * 3, d = Math.max(n, 1);
            out[o] = r / d; out[o + 1] = g / d; out[o + 2] = b / d;
        }
    return out;
}

function enforceConnectivity(labels, w, h, minSize) {
    const n = w * h, out = new Uint32Array(n).fill(NONE);
    let next = 0;
    const comp = [];
    for (let s = 0; s < n; ++s) {
        if (out[s] !== NONE)
            continue;
        const sx = s % w, sy = (s / w) | 0;
        let adj = -1;
        if (sx > 0 && out[s - 1] !== NONE) adj = out[s - 1];
        if (sy > 0 && out[s - w] !== NONE) adj = out[s - w];
        comp.length = 0;
        comp.push(s);
        out[s] = next;
        for (let k = 0; k < comp.length; ++k) {
            const i = comp[k], x = i % w, y = (i / w) | 0;
            for (const j of [x > 0 ? i - 1 : -1, x + 1 < w ? i + 1 : -1, y > 0 ? i - w : -1, y + 1 < h ? i + w : -1])
                if (j >= 0 && out[j] === NONE && labels[j] === labels[s]) {
                    out[j] = next;
                    comp.push(j);
                }
        }
        if (adj >= 0 && comp.length < minSize)
            for (const i of comp)
                out[i] = adj;
        else
            ++next;
    }
    return [out, next];
}

function slicLab(lab, w, h, target, compactness, iters) {
    const n = w * h;
    if (!n)
        return { labels: new Uint32Array(0), count: 0 };
    const s = Math.max(Math.sqrt(n / Math.max(target, 1)), 1);
    const at = (x, y) => (Math.min(y, h - 1) * w + Math.min(x, w - 1)) * 3;
    const grad = (x, y) => d2(lab, at(Math.max(x - 1, 0), y), lab, at(x + 1, y)) + d2(lab, at(x, Math.max(y - 1, 0)), lab, at(x, y + 1));
    const centers = [];
    for (let y = s / 2; y < h; y += s)
        for (let x = s / 2; x < w; x += s) {
            const cx = Math.trunc(x), cy = Math.trunc(y);
            let bx = cx, by = cy, bg = Infinity;
            for (let yy = Math.max(cy - 1, 0); yy <= Math.min(cy + 1, h - 1); ++yy)
                for (let xx = Math.max(cx - 1, 0); xx <= Math.min(cx + 1, w - 1); ++xx) {
                    const g = grad(xx, yy);
                    if (g < bg) {
                        bx = xx; by = yy; bg = g;
                    }
                }
            const i = (by * w + bx) * 3;
            centers.push([lab[i], lab[i + 1], lab[i + 2], bx, by]);
        }
    const labels = new Uint32Array(n).fill(NONE), dist = new Float32Array(n);
    const wxy = (compactness / s) ** 2, r = Math.ceil(s);
    for (let it = 0; it < Math.max(iters, 1); ++it) {
        dist.fill(Infinity);
        for (let k = 0; k < centers.length; ++k) {
            const c = centers[k], cx = Math.round(c[3]), cy = Math.round(c[4]);
            for (let yy = Math.max(cy - r, 0); yy < Math.min(cy + r + 1, h); ++yy)
                for (let xx = Math.max(cx - r, 0); xx < Math.min(cx + r + 1, w); ++xx) {
                    const i = yy * w + xx;
                    const dc = (lab[i * 3] - c[0]) ** 2 + (lab[i * 3 + 1] - c[1]) ** 2 + (lab[i * 3 + 2] - c[2]) ** 2;
                    const dx = xx - c[3], dy = yy - c[4], d = dc + (dx * dx + dy * dy) * wxy;
                    if (d < dist[i]) {
                        dist[i] = d;
                        labels[i] = k;
                    }
                }
        }
        const sums = centers.map(() => [0, 0, 0, 0, 0, 0]);
        for (let i = 0; i < n; ++i) {
            if (labels[i] === NONE)
                continue;
            const sm = sums[labels[i]];
            sm[0] += lab[i * 3]; sm[1] += lab[i * 3 + 1]; sm[2] += lab[i * 3 + 2];
            sm[3] += i % w; sm[4] += (i / w) | 0; sm[5] += 1;
        }
        for (let k = 0; k < centers.length; ++k)
            if (sums[k][5] > 0)
                for (let j = 0; j < 5; ++j)
                    centers[k][j] = Math.fround(sums[k][j] / sums[k][5]);
    }
    for (let i = 0; i < n; ++i)
        if (labels[i] === NONE)
            labels[i] = i > 0 ? labels[i - 1] : 0;
    const [lbl, count] = enforceConnectivity(labels, w, h, Math.floor((s * s) / 4));
    return { labels: lbl, count };
}

function otsu(v) {
    const hist = new Float64Array(256);
    for (const x of v)
        ++hist[Math.round(Math.min(Math.max(x, 0), 1) * 255)];
    const total = v.length;
    let sum = 0;
    for (let i = 0; i < 256; ++i)
        sum += i * hist[i];
    let wb = 0, sb = 0, best = -1, bt = 0;
    for (let t = 0; t < 256; ++t) {
        wb += hist[t];
        if (wb === 0)
            continue;
        const wf = total - wb;
        if (wf === 0)
            break;
        sb += t * hist[t];
        const mb = sb / wb, mf = (sum - sb) / wf, between = wb * wf * (mb - mf) * (mb - mf);
        if (between > best) {
            best = between;
            bt = t;
        }
    }
    return (bt + 0.5) / 255;
}

function blurLab(img, w, h) {
    let cur = new Float32Array(img);
    for (let pass = 0; pass < 2; ++pass) {
        const tmp = new Float32Array(cur);
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x)
                for (let k = 0; k < 3; ++k)
                    tmp[(y * w + x) * 3 + k] = (cur[(y * w + Math.max(x - 1, 0)) * 3 + k] + 2 * cur[(y * w + x) * 3 + k]
                                                + cur[(y * w + Math.min(x + 1, w - 1)) * 3 + k]) / 4;
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x)
                for (let k = 0; k < 3; ++k)
                    cur[(y * w + x) * 3 + k] = (tmp[(Math.max(y - 1, 0) * w + x) * 3 + k] + 2 * tmp[(y * w + x) * 3 + k]
                                                + tmp[(Math.min(y + 1, h - 1) * w + x) * 3 + k]) / 4;
    }
    return cur;
}

function saliency(img) {
    const { w, h } = img, lab = labImage(img.px, w * h);
    const target = Math.min(Math.max(Math.floor((w * h) / 150), 16), 400);
    const sp = slicLab(lab, w, h, target, 10, 8), n = sp.count;
    const mean = new Float64Array(n * 3), pos = new Float64Array(n * 2), cnt = new Float64Array(n), onBorder = new Uint8Array(n);
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const l = sp.labels[y * w + x], i = (y * w + x) * 3;
            for (let k = 0; k < 3; ++k)
                mean[l * 3 + k] += lab[i + k];
            pos[l * 2] += x / w;
            pos[l * 2 + 1] += y / h;
            cnt[l] += 1;
            if (x === 0 || y === 0 || x + 1 === w || y + 1 === h)
                onBorder[l] = 1;
        }
    for (let l = 0; l < n; ++l) {
        const c = Math.max(cnt[l], 1);
        for (let k = 0; k < 3; ++k)
            mean[l * 3 + k] = Math.fround(mean[l * 3 + k] / c);
        pos[l * 2] = Math.fround(pos[l * 2] / c);
        pos[l * 2 + 1] = Math.fround(pos[l * 2 + 1] / c);
    }
    const dapp = (a, b) => Math.sqrt(d2(mean, a * 3, mean, b * 3));
    const adj = Array.from({ length: n }, () => []);
    const link = (a, b) => {
        if (a !== b && !adj[a].includes(b)) {
            adj[a].push(b);
            adj[b].push(a);
        }
    };
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const l = sp.labels[y * w + x];
            if (x + 1 < w)
                link(l, sp.labels[y * w + x + 1]);
            if (y + 1 < h)
                link(l, sp.labels[(y + 1) * w + x]);
        }
    const wbg = new Float64Array(n), geo = new Float64Array(n);
    for (let p = 0; p < n; ++p) {
        // Dijkstra desde p por la distancia de color.
        geo.fill(Infinity);
        geo[p] = 0;
        const heap = [[0, p]];
        while (heap.length) {
            let bi = 0;
            for (let q = 1; q < heap.length; ++q)
                if (heap[q][0] < heap[bi][0])
                    bi = q;
            const [d, u] = heap[bi];
            heap[bi] = heap[heap.length - 1];
            heap.pop();
            if (d > geo[u])
                continue;
            for (const v of adj[u]) {
                const nd = d + dapp(u, v);
                if (nd < geo[v]) {
                    geo[v] = nd;
                    heap.push([nd, v]);
                }
            }
        }
        let area = 0, len = 0;
        for (let q = 0; q < n; ++q) {
            const s = Math.exp((-geo[q] * geo[q]) / (2 * 10 * 10));
            area += s;
            if (onBorder[q])
                len += s;
        }
        const bnd = len / Math.sqrt(Math.max(area, 1e-6));
        wbg[p] = 1 - Math.exp((-bnd * bnd) / 2);
    }
    const wctr = new Float64Array(n);
    for (let p = 0; p < n; ++p) {
        let s = 0;
        for (let q = 0; q < n; ++q) {
            const dx = pos[p * 2] - pos[q * 2], dy = pos[p * 2 + 1] - pos[q * 2 + 1];
            s += dapp(p, q) * Math.exp(-(dx * dx + dy * dy) / (2 * 0.25 * 0.25)) * wbg[q];
        }
        wctr[p] = s;
    }
    let mx = 0;
    for (const v of wctr)
        mx = Math.max(mx, v);
    mx = Math.max(mx, 1e-6);
    const wfg = wctr.map((v) => v / mx);
    const wij = adj.map((list, p) => list.map((q) => Math.exp(-(dapp(p, q) ** 2) / (2 * 10 * 10)) + 0.1));
    const s = new Float64Array(wfg);
    for (let it = 0; it < 200; ++it)
        for (let p = 0; p < n; ++p) {
            let num = wfg[p], den = wbg[p] + wfg[p];
            for (let k = 0; k < adj[p].length; ++k) {
                num += wij[p][k] * s[adj[p][k]];
                den += wij[p][k];
            }
            s[p] = num / Math.max(den, 1e-6);
        }
    const meanAll = [0, 0, 0];
    for (let i = 0; i < w * h; ++i)
        for (let k = 0; k < 3; ++k)
            meanAll[k] += lab[i * 3 + k];
    const ma = Float32Array.of(meanAll[0] / (w * h), meanAll[1] / (w * h), meanAll[2] / (w * h));
    const blurred = blurLab(lab, w, h), ft = new Float32Array(w * h);
    let ftmax = 0;
    for (let i = 0; i < w * h; ++i) {
        ft[i] = Math.sqrt(d2(blurred, i * 3, ma, 0));
        ftmax = Math.max(ftmax, ft[i]);
    }
    ftmax = Math.max(ftmax, 1e-6);
    let smin = Infinity, smax = -Infinity;
    for (const v of s) {
        smin = Math.min(smin, v);
        smax = Math.max(smax, v);
    }
    const srange = Math.max(smax - smin, 1e-6), out = new Float32Array(w * h);
    let lo = Infinity, hi = -Infinity;
    for (let i = 0; i < out.length; ++i) {
        out[i] = (0.75 * (s[sp.labels[i]] - smin)) / srange + (0.25 * ft[i]) / ftmax;
        lo = Math.min(lo, out[i]);
        hi = Math.max(hi, out[i]);
    }
    const rr = Math.max(hi - lo, 1e-6);
    for (let i = 0; i < out.length; ++i)
        out[i] = (out[i] - lo) / rr;
    return out;
}

function backdropPass(img, blob, stop) {
    const { w, h } = img;
    let x0 = w, y0 = h, x1 = 0, y1 = 0, any = false;
    for (let i = 0; i < blob.length; ++i)
        if (blob[i]) {
            const x = i % w, y = (i / w) | 0;
            x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y);
            any = true;
        }
    if (!any)
        return null;
    const margin = Math.max(Math.ceil(Math.max(x1 - x0 + 1, y1 - y0 + 1) * 0.15), 4);
    const bx0 = Math.max(x0 - margin, 0), by0 = Math.max(y0 - margin, 0), bx1 = Math.min(x1 + margin, w - 1), by1 = Math.min(y1 + margin, h - 1);
    const frame = Math.max(Math.round(Math.min(w, h) * 0.06), 2);
    const inFrame = (x, y) => x < frame || y < frame || x + frame >= w || y + frame >= h;
    const nearBlob = (x, y) => x >= bx0 && x <= bx1 && y >= by0 && y <= by1;
    const samples = [];
    for (let i = 0; i < w * h; ++i) {
        const x = i % w, y = (i / w) | 0;
        if (inFrame(x, y) && !nearBlob(x, y))
            samples.push(img.px[i * 3], img.px[i * 3 + 1], img.px[i * 3 + 2]);
    }
    const sa = Float32Array.from(samples);
    const backdrop = Gmm.fit(subsample(sa, 40000), K, DEFAULT_REG);
    if (!backdrop)
        return null;
    const frameCosts = new Float32Array(sa.length / 3);
    for (let i = 0; i < frameCosts.length; ++i)
        frameCosts[i] = backdrop.negLog(sa, i * 3);
    frameCosts.sort();
    const at = Math.floor((frameCosts.length * 99) / 100);
    if (at >= frameCosts.length)
        return null;
    const limit = frameCosts[at] + 4;
    const trimap = new Uint8Array(w * h);
    for (let i = 0; i < trimap.length; ++i) {
        const x = i % w, y = (i / w) | 0;
        trimap[i] = inFrame(x, y) && !nearBlob(x, y) ? BG : blob[i] || backdrop.negLog(img.px, i * 3) > limit ? PR_FG : PR_BG;
    }
    const models = grabcut(img, trimap, 4, stop);
    if (!models)
        return null;
    const cut = trimap.map((t) => (t === PR_FG || t === FG ? 1 : 0));
    const low = keepSeeded(cut, blob, w, h);
    return low.some((v) => v) ? [low, models[0], models[1]] : null;
}

function growSimilar(lab, w, h, sx, sy) {
    const MEAN_DIST = 32, EDGE_STEP = 10;
    const out = new Uint8Array(w * h), si = (sy * w + sx) * 3;
    const sum = [lab[si], lab[si + 1], lab[si + 2]];
    let n = 1;
    const stack = [sy * w + sx];
    out[sy * w + sx] = 1;
    const mean = new Float32Array(3);
    while (stack.length) {
        const c = stack.pop(), x = c % w, y = (c / w) | 0;
        mean[0] = sum[0] / n; mean[1] = sum[1] / n; mean[2] = sum[2] / n;
        for (const [nx, ny] of [[x - 1, y], [x + 1, y], [x, y - 1], [x, y + 1]]) {
            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                continue;
            const i = ny * w + nx;
            if (out[i])
                continue;
            if (Math.sqrt(d2(lab, i * 3, lab, c * 3)) <= EDGE_STEP && Math.sqrt(d2(lab, i * 3, mean, 0)) <= MEAN_DIST) {
                out[i] = 1;
                sum[0] += lab[i * 3]; sum[1] += lab[i * 3 + 1]; sum[2] += lab[i * 3 + 2];
                ++n;
                stack.push(i);
            }
        }
    }
    return out;
}

function objectIn(sampler, canvas, px, py, step, stop) {
    const img = readRgbScaled(sampler, canvas, step), { w, h } = img;
    if (w < 2 || h < 2)
        return null;
    const lab = labImage(box3(box3(img.px, w, h), w, h), w * h);
    const sx = Math.min(((px - canvas.x0) / step) | 0, w - 1), sy = Math.min(((py - canvas.y0) / step) | 0, h - 1);
    const grown = growSimilar(lab, w, h, sx, sy);
    let count = 0, boundary = 0;
    for (let i = 0; i < grown.length; ++i)
        if (grown[i]) {
            ++count;
            const x = i % w, y = (i / w) | 0;
            if (x === 0 || x + 1 === w || y === 0 || y + 1 === h)
                ++boundary;
        }
    if (boundary * 2 >= 2 * w + 2 * h - 4 || count < 4)
        return null;
    let x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (let i = 0; i < grown.length; ++i)
        if (grown[i]) {
            const gx = i % w, gy = (i / w) | 0;
            x0 = Math.min(x0, gx); y0 = Math.min(y0, gy); x1 = Math.max(x1, gx + 1); y1 = Math.max(y1, gy + 1);
        }
    const pad = Math.max(Math.floor(Math.max(x1 - x0, y1 - y0) / 6), 2);
    const bx0 = Math.max(x0 - pad, 0), by0 = Math.max(y0 - pad, 0), bx1 = Math.min(x1 + pad, w), by1 = Math.min(y1 + pad, h);
    const trimap = new Uint8Array(w * h);
    let anyBg = false;
    for (let yy = 0; yy < h; ++yy)
        for (let xx = 0; xx < w; ++xx) {
            const i = yy * w + xx, inside = xx >= bx0 && xx < bx1 && yy >= by0 && yy < by1;
            if (grown[i]) trimap[i] = PR_FG;
            else if (inside) trimap[i] = PR_BG;
            else {
                anyBg = true;
                trimap[i] = BG;
            }
        }
    if (!anyBg)
        for (let yy = 0; yy < h; ++yy)
            for (let xx = 0; xx < w; ++xx)
                if ((xx === 0 || yy === 0 || xx + 1 === w || yy + 1 === h) && !(xx === sx && yy === sy))
                    trimap[yy * w + xx] = BG;
    trimap[sy * w + sx] = FG;
    const models = grabcut(img, trimap, 5, stop);
    if (!models)
        return null;
    let low = trimap.map((t) => (isFg(t) ? 1 : 0));
    const seeds = new Uint8Array(w * h);
    seeds[sy * w + sx] = 1;
    low = keepSeeded(low, seeds, w, h);
    low = cleanMask(low, w, h, 0.1, 0);
    low = fillHolesByModel(low, img, models[0], models[1], 0, 0.05);
    if (!low.some((v) => v))
        return null;
    return finishRegion(sampler, canvas, step, low, w, h, models[0], models[1], stop);
}

// GrabCut iniciado con un rectángulo; después el borde se vuelve a cortar a resolución completa.
export function objectInRect(sampler, rect, canvas, workPx, stop = NEVER) {
    rect = boxIntersect(rect, canvas);
    if (boxW(rect) < 2 || boxH(rect) < 2)
        return null;
    const margin = Math.max(Math.ceil(Math.max(boxW(rect), boxH(rect)) * 0.15), 8);
    const window = boxIntersect(boxInflate(rect, margin), canvas), step = scaleFor(window, workPx);
    const img = readRgbScaled(sampler, window, step), lw = img.w, lh = img.h;
    const trimap = new Uint8Array(lw * lh);
    let anyBg = false;
    for (let y = 0; y < lh; ++y)
        for (let x = 0; x < lw; ++x) {
            const cx = window.x0 + x * step + (step >> 1), cy = window.y0 + y * step + (step >> 1);
            if (boxContains(rect, cx, cy))
                trimap[y * lw + x] = PR_FG;
            else
                anyBg = true;
        }
    if (!anyBg) {
        const ring = Math.max(Math.floor(Math.min(lw, lh) / 50), 1);
        for (let y = 0; y < lh; ++y)
            for (let x = 0; x < lw; ++x)
                if (x < ring || y < ring || x + ring >= lw || y + ring >= lh)
                    trimap[y * lw + x] = BG;
    }
    const models = grabcut(img, trimap, 8, stop);
    if (!models)
        return null;
    let low = trimap.map((t) => (isFg(t) ? 1 : 0));
    low = cleanMask(low, lw, lh, 0.1, 0);
    low = fillHolesByModel(low, img, models[0], models[1], 0, 0.05);
    return finishRegion(sampler, window, step, low, lw, lh, models[0], models[1], stop);
}

export const OBJECT_AT_WORK_PX = 60000;
export const SUBJECT_WORK_PX = 110000;

// El Buscador de objetos: el objeto bajo el punto (x, y).
export function objectAt(sampler, canvas, x, y, workPx, stop = NEVER) {
    if (boxW(canvas) < 4 || boxH(canvas) < 4 || !Number.isFinite(x) || !Number.isFinite(y))
        return null;
    const px = Math.floor(x), py = Math.floor(y);
    if (!boxContains(canvas, px, py))
        return null;
    workPx = Math.max(workPx, 1024);
    let step = scaleFor(canvas, workPx);
    const r = objectIn(sampler, canvas, px, py, step, stop);
    if (r)
        return r;
    const side = Math.floor(Math.sqrt(workPx));
    while (step > 1) {
        step = Math.max(Math.floor(step / 4), 1);
        const half = Math.floor((side * step) / 2);
        const window = boxIntersect(box(px - half, py - half, px + half, py + half), canvas);
        if (boxW(window) < 4 || boxH(window) < 4)
            continue;
        const q = objectIn(sampler, window, px, py, step, stop);
        if (q)
            return q;
    }
    return null;
}

// Seleccionar sujeto: el objeto más destacado de la imagen.
export function selectSubject(sampler, canvas, workPx, stop = NEVER) {
    if (boxW(canvas) < 4 || boxH(canvas) < 4)
        return null;
    const step = scaleFor(canvas, Math.min(Math.max(workPx, 1024), 4000000));
    const img = readRgbScaled(sampler, canvas, step), { w, h } = img;
    const sal = saliency(img);
    stop.check();
    const t = Math.min(Math.max(otsu(sal), 0.15), 0.85);
    const trimap = new Uint8Array(w * h).fill(PR_BG);
    let anyFg = false;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const i = y * w + x, border = x === 0 || y === 0 || x + 1 === w || y + 1 === h;
            if (sal[i] >= t) {
                anyFg = true;
                trimap[i] = PR_FG;
            } else if (border || sal[i] < 0.4 * t) {
                trimap[i] = BG;
            }
        }
    if (!anyFg)
        return null;
    const models = grabcut(img, trimap, 6, stop);
    if (!models)
        return null;
    let low = trimap.map((v) => (v === PR_FG || v === FG ? 1 : 0));
    low = cleanMask(low, w, h, 0.2, 0);
    if (!low.some((v) => v))
        return null;
    let [fg, bg] = models;
    const back = backdropPass(img, low, stop);
    if (back)
        [low, fg, bg] = back;
    stop.check();
    low = fillHolesByModel(low, img, fg, bg, 0.002, 0.05);
    return finishRegion(sampler, canvas, step, low, w, h, fg, bg, stop);
}
