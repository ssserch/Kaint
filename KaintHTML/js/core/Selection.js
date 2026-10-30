// Selección como en Kaint (src/core/Selection.cpp): una máscara de 0..255 del tamaño de la
// imagen; sin máscara no hay nada seleccionado (y las herramientas trabajan en toda la imagen).
import { context, createCanvas } from "./Raster.js";

export const SelectionMode = { Replace: "replace", Union: "union", Exclude: "exclude", Intersect: "intersect", Xor: "xor" };

// Distancia (euclídea, al cuadrado) de cada píxel al más cercano con 'inside' igual al pedido:
// transformada de Felzenszwalb y Huttenlocher, separable y lineal.
function squaredDistance(mask, w, h, toInside) {
    const INF = 1e20, d = new Float32Array(w * h);
    for (let i = 0; i < d.length; ++i)
        d[i] = (mask[i] >= 128) === toInside ? 0 : INF;
    const n = Math.max(w, h), tmp = new Float32Array(n), v = new Int32Array(n), z = new Float32Array(n + 1);
    const pass = (offset, count, stride) => {
        for (let i = 0; i < count; ++i)
            tmp[i] = d[offset + i * stride];
        let k = 0;
        v[0] = 0;
        z[0] = -INF;
        z[1] = INF;
        for (let q = 1; q < count; ++q) {
            let s = (tmp[q] + q * q - (tmp[v[k]] + v[k] * v[k])) / (2 * (q - v[k]));
            while (s <= z[k]) {
                --k;
                s = (tmp[q] + q * q - (tmp[v[k]] + v[k] * v[k])) / (2 * (q - v[k]));
            }
            ++k;
            v[k] = q;
            z[k] = s;
            z[k + 1] = INF;
        }
        k = 0;
        for (let q = 0; q < count; ++q) {
            while (z[k + 1] < q)
                ++k;
            const dq = q - v[k];
            d[offset + q * stride] = dq * dq + tmp[v[k]];
        }
    };
    for (let x = 0; x < w; ++x)
        pass(x, h, w);
    for (let y = 0; y < h; ++y)
        pass(y * w, w, 1);
    return d;
}

export class Selection {
    constructor(width, height, mask = null, rect = null) {
        this.width = width;
        this.height = height;
        this.mask = mask;
        // Rectángulo exacto si la selección es un rectángulo (contorno nítido y más rápido).
        this.rect = rect;
        this._bounds = undefined;
        this._outline = null;
        this._maskCanvas = null;
    }

    static none(width, height) {
        return new Selection(width, height);
    }

    static rect(width, height, x, y, w, h) {
        const r = { x: Math.max(0, Math.round(x)), y: Math.max(0, Math.round(y)) };
        r.w = Math.min(width, Math.round(x + w)) - r.x;
        r.h = Math.min(height, Math.round(y + h)) - r.y;
        if (r.w <= 0 || r.h <= 0)
            return Selection.none(width, height);
        const mask = new Uint8Array(width * height);
        for (let yy = r.y; yy < r.y + r.h; ++yy)
            mask.fill(255, yy * width + r.x, yy * width + r.x + r.w);
        return new Selection(width, height, mask, r);
    }

    // Rasteriza un trazado (función que recibe el contexto y define el trazado) relleno.
    static fromPath(width, height, buildPath, antialias = true, fillRule = "nonzero") {
        const canvas = createCanvas(width, height);
        const ctx = context(canvas);
        ctx.imageSmoothingEnabled = antialias;
        ctx.beginPath();
        buildPath(ctx);
        ctx.fillStyle = "#fff";
        ctx.fill(fillRule);
        return Selection.fromAlpha(canvas, antialias);
    }

    // La máscara a partir del alfa de un lienzo del tamaño de la imagen.
    static fromAlpha(canvas, antialias = true) {
        const data = context(canvas).getImageData(0, 0, canvas.width, canvas.height).data;
        const mask = new Uint8Array(canvas.width * canvas.height);
        let any = false;
        for (let i = 0, j = 3; i < mask.length; ++i, j += 4) {
            const a = antialias ? data[j] : (data[j] >= 128 ? 255 : 0);
            mask[i] = a;
            any = any || a > 0;
        }
        return any ? new Selection(canvas.width, canvas.height, mask) : Selection.none(canvas.width, canvas.height);
    }

    static fromMask(width, height, mask) {
        for (let i = 0; i < mask.length; ++i)
            if (mask[i])
                return new Selection(width, height, mask);
        return Selection.none(width, height);
    }

    isEmpty() {
        return !this.mask;
    }

    value(x, y) {
        if (!this.mask)
            return 255;
        if (x < 0 || y < 0 || x >= this.width || y >= this.height)
            return 0;
        return this.mask[y * this.width + x];
    }

    bounds() {
        if (this._bounds !== undefined)
            return this._bounds;
        if (!this.mask)
            return (this._bounds = null);
        if (this.rect)
            return (this._bounds = { ...this.rect });
        let x0 = this.width, y0 = this.height, x1 = -1, y1 = -1;
        const w = this.width;
        for (let y = 0; y < this.height; ++y) {
            const row = y * w;
            let first = -1, last = -1;
            for (let x = 0; x < w; ++x)
                if (this.mask[row + x]) {
                    if (first < 0) first = x;
                    last = x;
                }
            if (first >= 0) {
                x0 = Math.min(x0, first);
                x1 = Math.max(x1, last);
                y0 = Math.min(y0, y);
                y1 = y;
            }
        }
        this._bounds = x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
        return this._bounds;
    }

    // Área en píxeles (los semitransparentes cuentan en proporción).
    area() {
        if (!this.mask)
            return 0;
        let sum = 0;
        for (let i = 0; i < this.mask.length; ++i)
            sum += this.mask[i];
        return Math.round(sum / 255);
    }

    equals(other) {
        if (!other || this.width !== other.width || this.height !== other.height)
            return false;
        if (!this.mask || !other.mask)
            return !this.mask && !other.mask;
        if (this.mask === other.mask)
            return true;
        for (let i = 0; i < this.mask.length; ++i)
            if (this.mask[i] !== other.mask[i])
                return false;
        return true;
    }

    // Combinación con los modos de selección de Paint.NET.
    combined(shape, mode) {
        if (mode === SelectionMode.Replace)
            return shape;
        const w = this.width, h = this.height, n = w * h;
        const a = this.mask, b = shape.mask;
        if (!a) {
            if (mode === SelectionMode.Union || mode === SelectionMode.Xor)
                return shape;
            return Selection.none(w, h);
        }
        if (!b)
            return mode === SelectionMode.Intersect ? Selection.none(w, h) : this;
        const out = new Uint8Array(n);
        for (let i = 0; i < n; ++i) {
            const A = a[i], B = b[i];
            switch (mode) {
            case SelectionMode.Union: out[i] = Math.max(A, B); break;
            case SelectionMode.Exclude: out[i] = (A * (255 - B) + 127) / 255; break;
            case SelectionMode.Intersect: out[i] = (A * B + 127) / 255; break;
            case SelectionMode.Xor: out[i] = Math.abs(A - B); break;
            }
        }
        return Selection.fromMask(w, h, out);
    }

    inverted() {
        const n = this.width * this.height;
        const out = new Uint8Array(n);
        if (this.mask)
            for (let i = 0; i < n; ++i)
                out[i] = 255 - this.mask[i];
        else
            out.fill(255);
        return Selection.fromMask(this.width, this.height, out);
    }

    // Copiar selección (como Paint.NET y Kaint): los polígonos en JSON ("polygonList"), uno por
    // cada rectángulo de píxeles seleccionados que se repite de fila en fila.
    toJson() {
        const polygons = [];
        const box = (x0, y0, x1, y1) => polygons.push([x0, y0, x1, y0, x1, y1, x0, y1, x0, y0].join(","));
        if (this.rect) {
            box(this.rect.x, this.rect.y, this.rect.x + this.rect.w, this.rect.y + this.rect.h);
        } else if (this.mask) {
            const b = this.bounds(), w = this.width;
            let open = [];
            for (let y = b.y; y <= b.y + b.h; ++y) {
                const runs = [];
                if (y < b.y + b.h)
                    for (let x = b.x; x < b.x + b.w;) {
                        while (x < b.x + b.w && this.mask[y * w + x] < 128)
                            ++x;
                        const start = x;
                        while (x < b.x + b.w && this.mask[y * w + x] >= 128)
                            ++x;
                        if (x > start)
                            runs.push([start, x]);
                    }
                const next = [];
                for (const r of open) {
                    const k = runs.findIndex(([a, c]) => a === r.x0 && c === r.x1);
                    if (k >= 0) {
                        next.push(r);
                        runs.splice(k, 1);
                    } else {
                        box(r.x0, r.y0, r.x1, y);
                    }
                }
                for (const [x0, x1] of runs)
                    next.push({ x0, x1, y0: y });
                open = next;
            }
        }
        return JSON.stringify({ polygonList: polygons }, null, 4);
    }

    // Pegar selección: los polígonos de toJson() (o de Paint.NET); null si el texto no es una
    // selección.
    static fromJson(text, width, height) {
        let list;
        try {
            list = JSON.parse(text)?.polygonList;
        } catch {
            return null;
        }
        if (!Array.isArray(list) || !list.length)
            return null;
        const polygons = list.map((p) => String(p).split(",").map((n) => parseFloat(n.trim())).filter(Number.isFinite))
            .filter((p) => p.length >= 6);
        if (!polygons.length)
            return null;
        return Selection.fromPath(width, height, (ctx) => {
            for (const p of polygons) {
                ctx.moveTo(p[0], p[1]);
                for (let i = 2; i + 1 < p.length; i += 2)
                    ctx.lineTo(p[i], p[i + 1]);
                ctx.closePath();
            }
        }, true, "evenodd");
    }

    // Ampliar (radio > 0) o reducir (radio < 0) la selección: cuenta la distancia euclídea a lo
    // seleccionado (o, al reducir, a lo no seleccionado), con el borde suavizado.
    grown(radius) {
        if (!this.mask || !radius)
            return this;
        const w = this.width, h = this.height, grow = radius > 0, r = Math.abs(radius);
        const dist = squaredDistance(this.mask, w, h, grow), out = new Uint8Array(w * h);
        for (let i = 0; i < out.length; ++i) {
            const d = Math.sqrt(dist[i]);
            out[i] = Math.round(Math.min(1, Math.max(0, grow ? r - d + 1 : d - r)) * 255);
        }
        return Selection.fromMask(w, h, out);
    }

    // Difuminar el borde: desenfoque gaussiano de la máscara (sigma = radio / 2).
    feathered(radius) {
        if (!this.mask || radius <= 0)
            return this;
        const w = this.width, h = this.height, sigma = radius / 2, half = Math.ceil(sigma * 3);
        const kernel = new Float32Array(2 * half + 1);
        let sum = 0;
        for (let i = -half; i <= half; ++i)
            sum += kernel[i + half] = Math.exp(-(i * i) / (2 * sigma * sigma));
        for (let i = 0; i < kernel.length; ++i)
            kernel[i] /= sum;
        const a = Float32Array.from(this.mask), b = new Float32Array(w * h), out = new Uint8Array(w * h);
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x) {
                let acc = 0;
                for (let i = -half; i <= half; ++i)
                    acc += kernel[i + half] * a[y * w + Math.min(w - 1, Math.max(0, x + i))];
                b[y * w + x] = acc;
            }
        for (let y = 0; y < h; ++y)
            for (let x = 0; x < w; ++x) {
                let acc = 0;
                for (let i = -half; i <= half; ++i)
                    acc += kernel[i + half] * b[Math.min(h - 1, Math.max(0, y + i)) * w + x];
                out[y * w + x] = Math.min(255, Math.max(0, Math.round(acc)));
            }
        return Selection.fromMask(w, h, out);
    }

    // La misma máscara en otro tamaño de imagen (recortar, tamaño del lienzo), desplazada.
    remapped(width, height, dx = 0, dy = 0) {
        if (!this.mask)
            return Selection.none(width, height);
        const out = new Uint8Array(width * height);
        for (let y = 0; y < height; ++y) {
            const sy = y - dy;
            if (sy < 0 || sy >= this.height)
                continue;
            for (let x = 0; x < width; ++x) {
                const sx = x - dx;
                if (sx >= 0 && sx < this.width)
                    out[y * width + x] = this.mask[sy * this.width + sx];
            }
        }
        return Selection.fromMask(width, height, out);
    }

    // La máscara transformada por una matriz (DOMMatrix) (Mover selección, girar, voltear).
    transformed(matrix, antialias = false) {
        if (!this.mask)
            return this;
        const canvas = createCanvas(this.width, this.height);
        const ctx = context(canvas);
        ctx.imageSmoothingEnabled = antialias;
        ctx.setTransform(matrix);
        ctx.drawImage(this.maskCanvas(), 0, 0);
        return Selection.fromAlpha(canvas, antialias);
    }

    // La cobertura con la que se recorta lo que se pinta (Raster::clipCoverage de Kaint): con la
    // calidad de selección pixelada, cada píxel queda dentro (128 o más) o fuera del todo.
    clipMask(antialias) {
        if (!this.mask || antialias)
            return this.mask;
        this._hardMask ||= this.mask.map((v) => (v >= 128 ? 255 : 0));
        return this._hardMask;
    }

    // Como maskCanvas(), con la cobertura de clipMask().
    clipCanvas(antialias) {
        if (!this.mask || antialias)
            return this.maskCanvas();
        if (!this._hardCanvas) {
            const canvas = createCanvas(this.width, this.height), ctx = context(canvas);
            const img = ctx.createImageData(this.width, this.height), d = img.data, mask = this.clipMask(false);
            for (let i = 0, j = 0; i < mask.length; ++i, j += 4) {
                d[j] = d[j + 1] = d[j + 2] = 255;
                d[j + 3] = mask[i];
            }
            ctx.putImageData(img, 0, 0);
            this._hardCanvas = canvas;
        }
        return this._hardCanvas;
    }

    // Lienzo blanco con el alfa de la máscara (para recortar con 'destination-in').
    maskCanvas() {
        if (this._maskCanvas)
            return this._maskCanvas;
        const canvas = createCanvas(this.width, this.height);
        const ctx = context(canvas);
        if (!this.mask) {
            ctx.fillStyle = "#fff";
            ctx.fillRect(0, 0, this.width, this.height);
        } else {
            const img = ctx.createImageData(this.width, this.height);
            const d = img.data;
            for (let i = 0, j = 0; i < this.mask.length; ++i, j += 4) {
                d[j] = d[j + 1] = d[j + 2] = 255;
                d[j + 3] = this.mask[i];
            }
            ctx.putImageData(img, 0, 0);
        }
        this._maskCanvas = canvas;
        return canvas;
    }

    // Bordes entre píxeles dentro y fuera (umbral 128), unidos en tramos: [x1, y1, x2, y2, ...].
    outline() {
        if (this._outline)
            return this._outline;
        const segs = [];
        if (this.rect) {
            const r = this.rect;
            segs.push(r.x, r.y, r.x + r.w, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h,
                      r.x + r.w, r.y + r.h, r.x, r.y + r.h, r.x, r.y + r.h, r.x, r.y);
        } else if (this.mask) {
            const w = this.width, h = this.height, m = this.mask;
            const inside = (x, y) => x >= 0 && y >= 0 && x < w && y < h && m[y * w + x] >= 128;
            const b = this.bounds();
            for (let y = b.y; y <= b.y + b.h; ++y) {
                let start = -1;
                for (let x = b.x; x <= b.x + b.w; ++x) {
                    const edge = x < b.x + b.w && inside(x, y) !== inside(x, y - 1);
                    if (edge && start < 0) start = x;
                    if (!edge && start >= 0) {
                        segs.push(start, y, x, y);
                        start = -1;
                    }
                }
            }
            for (let x = b.x; x <= b.x + b.w; ++x) {
                let start = -1;
                for (let y = b.y; y <= b.y + b.h; ++y) {
                    const edge = y < b.y + b.h && inside(x, y) !== inside(x - 1, y);
                    if (edge && start < 0) start = y;
                    if (!edge && start >= 0) {
                        segs.push(x, start, x, y);
                        start = -1;
                    }
                }
            }
        }
        this._outline = segs;
        return segs;
    }
}

// Contorno suavizado de la máscara (>= 128): cuadrados en marcha con las esquinas cortadas
// (Selection::smoothOutline de Kaint). Devuelve los polígonos cerrados: [[x, y, x, y, ...], ...].
export function smoothContour(mask, w, h, bounds) {
    const inside = (x, y) => x >= 0 && y >= 0 && x < w && y < h && mask[y * w + x] >= 128;
    // Puntos con coordenadas dobladas para que sean enteros.
    const stride = 2 * h + 16, key = (px, py) => (px + 4) * stride + (py + 4);
    const segs = [], from = new Map();
    for (let y = bounds.y - 1; y <= bounds.y + bounds.h - 1; ++y)
        for (let x = bounds.x - 1; x <= bounds.x + bounds.w - 1; ++x) {
            const code = (inside(x, y) << 3) | (inside(x + 1, y) << 2) | (inside(x + 1, y + 1) << 1) | inside(x, y + 1);
            if (code === 0 || code === 15)
                continue;
            const T = [2 * x + 2, 2 * y + 1], R = [2 * x + 3, 2 * y + 2], B = [2 * x + 2, 2 * y + 3], L = [2 * x + 1, 2 * y + 2];
            const add = (a, b) => {
                from.set(key(a[0], a[1]), segs.length);
                segs.push([a, b]);
            };
            switch (code) {
            case 8: add(T, L); break;
            case 4: add(R, T); break;
            case 2: add(B, R); break;
            case 1: add(L, B); break;
            case 12: add(R, L); break;
            case 6: add(B, T); break;
            case 3: add(L, R); break;
            case 9: add(T, B); break;
            case 14: add(B, L); break;
            case 13: add(R, B); break;
            case 11: add(T, R); break;
            case 7: add(L, T); break;
            case 10: add(T, L); add(B, R); break;
            case 5: add(R, T); add(L, B); break;
            }
        }
    const loops = [], used = new Uint8Array(segs.length);
    for (let start = 0; start < segs.length; ++start) {
        if (used[start])
            continue;
        const loop = [];
        for (let i = start; !used[i];) {
            used[i] = 1;
            loop.push(segs[i][0][0] / 2, segs[i][0][1] / 2);
            const next = from.get(key(segs[i][1][0], segs[i][1][1]));
            if (next === undefined)
                break;
            i = next;
        }
        const n = loop.length / 2;
        if (n < 3)
            continue;
        const smooth = [];
        for (let i = 0; i < n; ++i) {
            const ax = loop[2 * i], ay = loop[2 * i + 1], bx = loop[2 * ((i + 1) % n)], by = loop[2 * ((i + 1) % n) + 1];
            smooth.push(ax * 0.75 + bx * 0.25, ay * 0.75 + by * 0.25, ax * 0.25 + bx * 0.75, ay * 0.25 + by * 0.75);
        }
        loops.push(smooth);
    }
    return loops;
}
