// Selección como en Kaint (src/core/Selection.cpp): una máscara de 0..255 del tamaño de la
// imagen; sin máscara no hay nada seleccionado (y las herramientas trabajan en toda la imagen).
import { context, createCanvas } from "./Raster.js";

export const SelectionMode = { Replace: "replace", Union: "union", Exclude: "exclude", Intersect: "intersect", Xor: "xor" };

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
