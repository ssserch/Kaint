// Cubo de pintura y degradado (src/app/tools/FillTools.cpp).
import { Tool } from "./Tool.js";
import { AppState } from "../AppState.js";
import { floodMask } from "../core/FloodFill.js";
import { blendPixels, cloneCanvas, context } from "../core/Raster.js";
import { tr } from "../i18n.js";

export class PaintBucketTool extends Tool {
    get id() { return "paintbucket"; }
    get name() { return tr("Cubo de pintura"); }
    get icon() { return "paintbucket"; }
    get letter() { return "F"; }
    get options() { return ["flood", "tolerance", "sampling", "antialias", "blend"]; }
    hint() { return tr("Clic izquierdo: rellenar con el color principal · clic derecho: con el secundario. Mayús: modo global."); }
    cursor() { return this.iconCursor(3, 20); }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right"))
            return;
        const x = Math.floor(e.x), y = Math.floor(e.y);
        if (x < 0 || y < 0 || x >= doc.width || y >= doc.height)
            return;
        const s = this.settings, layer = doc.activeLayer, w = doc.width, h = doc.height;
        const source = s.sampleImage ? doc.flattened() : layer.canvas;
        const mask = floodMask(context(source).getImageData(0, 0, w, h), x, y, s.tolerance, s.floodGlobal || e.shift);
        const sel = doc.selection;
        let x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (let yy = 0; yy < h; ++yy)
            for (let xx = 0; xx < w; ++xx) {
                const i = yy * w + xx;
                if (sel.mask)
                    mask[i] = (mask[i] * sel.mask[i]) / 255;
                if (mask[i]) {
                    if (xx < x0) x0 = xx;
                    if (xx > x1) x1 = xx;
                    if (yy < y0) y0 = yy;
                    y1 = yy;
                }
            }
        if (x1 < 0)
            return;
        // Borde suavizado: los píxeles del borde de la zona se cubren a medias.
        const rect = { x: Math.max(0, x0 - 1), y: Math.max(0, y0 - 1) };
        rect.w = Math.min(w, x1 + 2) - rect.x;
        rect.h = Math.min(h, y1 + 2) - rect.y;
        const before = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
        const out = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
        const top = new ImageData(rect.w, rect.h), c = this.colorFor(e.button);
        for (let yy = 0; yy < rect.h; ++yy)
            for (let xx = 0; xx < rect.w; ++xx) {
                const i = (rect.y + yy) * w + rect.x + xx;
                let m = mask[i];
                if (!m && s.antialias) {
                    let n = 0;
                    for (const [dx, dy] of [[-1, 0], [1, 0], [0, -1], [0, 1]]) {
                        const nx = rect.x + xx + dx, ny = rect.y + yy + dy;
                        if (nx >= 0 && ny >= 0 && nx < w && ny < h && mask[ny * w + nx]) ++n;
                    }
                    m = n ? 64 * n : 0;
                    if (sel.mask) m = (m * sel.mask[i]) / 255;
                }
                if (!m) continue;
                const j = (yy * rect.w + xx) * 4;
                top.data[j] = c.r; top.data[j + 1] = c.g; top.data[j + 2] = c.b; top.data[j + 3] = (Math.min(255, m) * (c.a ?? 255)) / 255;
            }
        blendPixels(out, top, s.blend);
        layer.ctx.putImageData(out, rect.x, rect.y);
        doc.pixelsChanged(layer, rect);
        doc.pushPixels(layer, rect, before, this.name, this.icon);
    }
}

export class GradientTool extends Tool {
    get id() { return "gradient"; }
    get name() { return tr("Degradado"); }
    get icon() { return "gradient"; }
    get letter() { return "G"; }
    get options() { return ["gradient"]; }

    hint() {
        return this.editing
            ? tr("Arrastra los extremos o el icono de mover; clic derecho en un extremo invierte los colores. Intro: terminar.")
            : tr("Arrastra para dibujar el degradado del color principal al secundario (clic derecho: al revés). Mayús: ángulos de 15°.");
    }

    activate() {
        this.onSettings = () => this.editing && this.render();
        AppState.addEventListener("tools", this.onSettings);
        AppState.addEventListener("colors", this.onSettings);
    }

    deactivate() {
        AppState.removeEventListener("tools", this.onSettings);
        AppState.removeEventListener("colors", this.onSettings);
        this.finish();
    }

    handleAt(e) {
        if (!this.editing)
            return -1;
        for (const [i, p] of [this.a, this.b].entries()) {
            const v = this.view.imageToView(p.x, p.y);
            if (Math.hypot(v.x - e.vx, v.y - e.vy) <= 8)
                return i;
        }
        return -1;
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        const handle = this.handleAt(e);
        if (handle >= 0) {
            if (e.button === "right") {
                this.reversed = !this.reversed;
                this.render();
                return;
            }
            this.drag = { handle };
            return;
        }
        this.finish();
        this.editing = true;
        this.reversed = e.button === "right";
        this.a = { x: e.x, y: e.y };
        this.b = { x: e.x, y: e.y };
        this.layer = this.doc.activeLayer;
        this.start = cloneCanvas(this.layer.canvas);
        this.preview = cloneCanvas(this.layer.canvas);
        this.drag = { handle: 1 };
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
        this.setHint(this.hint());
    }

    onPointerMove(e) {
        if (!this.drag)
            return;
        let p = { x: e.x, y: e.y };
        if (e.shift) {
            const o = this.drag.handle === 1 ? this.a : this.b;
            const ang = Math.round(Math.atan2(p.y - o.y, p.x - o.x) / (Math.PI / 12)) * (Math.PI / 12);
            const d = Math.hypot(p.x - o.x, p.y - o.y);
            p = { x: o.x + Math.cos(ang) * d, y: o.y + Math.sin(ang) * d };
        }
        this.drag.handle === 0 ? (this.a = p) : (this.b = p);
        this.render();
    }

    onPointerUp() {
        this.drag = null;
    }

    // Valor del degradado (0..1) en un punto, según el tipo y la repetición.
    value(x, y) {
        const s = this.settings, a = this.a, b = this.b;
        const dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy || 1, len = Math.sqrt(len2);
        let t;
        switch (s.gradient) {
        case "reflected": t = Math.abs(((x - a.x) * dx + (y - a.y) * dy) / len2); break;
        case "diamond": {
            const u = ((x - a.x) * dx + (y - a.y) * dy) / len2, v = ((x - a.x) * -dy + (y - a.y) * dx) / len2;
            t = Math.abs(u) + Math.abs(v);
            break;
        }
        case "radial": t = Math.hypot(x - a.x, y - a.y) / len; break;
        case "conical": {
            const ang = Math.atan2(y - a.y, x - a.x) - Math.atan2(dy, dx);
            t = Math.abs((((ang / Math.PI) % 2) + 3) % 2 - 1);
            break;
        }
        default: t = ((x - a.x) * dx + (y - a.y) * dy) / len2;
        }
        if (s.gradientRepeat === "wrapped") t = t - Math.floor(t);
        else if (s.gradientRepeat === "reflected") { t = Math.abs(t) % 2; if (t > 1) t = 2 - t; }
        return Math.min(1, Math.max(0, t));
    }

    render() {
        const doc = this.doc, s = this.settings, w = doc.width, h = doc.height, sel = doc.selection;
        const b = sel.bounds() || { x: 0, y: 0, w, h };
        const startCtx = context(this.start), base = startCtx.getImageData(b.x, b.y, b.w, b.h);
        let c1 = AppState.primary, c2 = AppState.secondary;
        if (this.reversed) [c1, c2] = [c2, c1];
        if (s.gradientTransparency) {
            // Modo de transparencia: la opacidad de los píxeles va de entera a nada.
            const d = base.data;
            for (let y = 0; y < b.h; ++y)
                for (let x = 0; x < b.w; ++x) {
                    const m = sel.mask ? sel.mask[(b.y + y) * w + b.x + x] / 255 : 1;
                    if (!m) continue;
                    const t = this.value(b.x + x + 0.5, b.y + y + 0.5);
                    const keep = this.reversed ? t : 1 - t;
                    d[(y * b.w + x) * 4 + 3] *= 1 - m + m * keep;
                }
        } else {
            const top = new ImageData(b.w, b.h), d = top.data;
            for (let y = 0; y < b.h; ++y)
                for (let x = 0; x < b.w; ++x) {
                    const m = sel.mask ? sel.mask[(b.y + y) * w + b.x + x] : 255;
                    if (!m) continue;
                    const t = this.value(b.x + x + 0.5, b.y + y + 0.5), i = (y * b.w + x) * 4;
                    d[i] = c1.r + (c2.r - c1.r) * t; d[i + 1] = c1.g + (c2.g - c1.g) * t; d[i + 2] = c1.b + (c2.b - c1.b) * t;
                    d[i + 3] = ((c1.a + (c2.a - c1.a) * t) * m) / 255;
                }
            blendPixels(base, top, "normal");
        }
        const pctx = context(this.preview);
        pctx.putImageData(base, b.x, b.y);
        doc.pixelsChanged(this.layer, b);
        this.rect = b;
    }

    paintOverlay(ctx, view) {
        if (!this.editing)
            return;
        const a = view.imageToView(this.a.x, this.a.y), b = view.imageToView(this.b.x, this.b.y);
        ctx.strokeStyle = "rgba(0,0,0,0.6)";
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
        for (const [p, c] of [[a, AppState.primary], [b, AppState.secondary]]) {
            ctx.beginPath();
            ctx.arc(p.x, p.y, 6, 0, Math.PI * 2);
            ctx.fillStyle = `rgb(${c.r},${c.g},${c.b})`;
            ctx.fill();
            ctx.strokeStyle = "#fff"; ctx.lineWidth = 2; ctx.stroke();
            ctx.strokeStyle = "#000"; ctx.lineWidth = 1; ctx.stroke();
        }
    }

    isBusy() {
        return !!this.editing;
    }

    finish() {
        if (!this.editing)
            return;
        this.editing = false;
        this.drag = null;
        const doc = this.doc, layer = this.layer, r = this.rect;
        doc.preview = null;
        if (r && (this.a.x !== this.b.x || this.a.y !== this.b.y)) {
            const before = context(this.start).getImageData(r.x, r.y, r.w, r.h);
            layer.ctx.putImageData(context(this.preview).getImageData(r.x, r.y, r.w, r.h), r.x, r.y);
            doc.pixelsChanged(layer, r);
            doc.pushPixels(layer, r, before, this.name, this.icon);
        } else {
            doc.pixelsChanged(layer, null);
        }
        this.setHint(this.hint());
    }

    cancel() {
        if (!this.editing)
            return;
        this.editing = false;
        this.drag = null;
        this.doc.setPreview(null);
        this.setHint(this.hint());
    }

    onKeyDown(e) {
        if (!this.editing)
            return false;
        if (e.key === "Enter") { this.finish(); return true; }
        if (e.key === "Escape") { this.cancel(); return true; }
        return false;
    }
}
