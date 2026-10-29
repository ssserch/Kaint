// Cubo de pintura y degradado (src/app/tools/FillTools.cpp): siguen editables (el origen del
// relleno, los extremos del degradado, las opciones y los colores) hasta pulsar Intro.
import { Tool } from "./Tool.js";
import { drawMoveHandle, nearPoint, StepRecorder } from "./EditableSteps.js";
import { AppState } from "../AppState.js";
import { floodMask } from "../core/FloodFill.js";
import { blendPixels, cloneCanvas, context, unionRect } from "../core/Raster.js";
import { tr } from "../i18n.js";

// Base de las dos: pasos editables como en Kaint (crear, "Editar…"/"Mover…" y "Finalizar…") y
// retomar la edición al volver a uno de sus pasos.
class EditableFill extends Tool {
    constructor(app) {
        super(app);
        this.steps = new StepRecorder(this);
    }

    get recordsSteps() {
        return true;
    }

    activate() {
        // Otras opciones o colores: se ve al momento y se registra con el próximo paso.
        this.onSettings = () => {
            if (this.editing && !this.restoring) {
                this.render();
                this.steps.changed();
            }
        };
        AppState.addEventListener("tools", this.onSettings);
        AppState.addEventListener("colors", this.onSettings);
    }

    deactivate() {
        AppState.removeEventListener("tools", this.onSettings);
        AppState.removeEventListener("colors", this.onSettings);
        this.finish();
    }

    begin() {
        this.editing = true;
        this.layer = this.doc.activeLayer;
        this.start = cloneCanvas(this.layer.canvas);
        this.preview = cloneCanvas(this.layer.canvas);
        this.rect = null;
        this.steps.start();
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
    }

    commitStep() {
        if (!this.editing || !this.rect)
            return;
        this.steps.commit({ layer: this.layer, preview: this.preview, rect: this.rect, base: this.start, state: this.saveState(),
                            createText: this.name, editText: this.editName(), icon: this.icon });
    }

    isBusy() {
        return !!this.drag;
    }

    finish() {
        if (!this.editing)
            return;
        this.commitStep();
        this.editing = false;
        this.drag = null;
        this.doc.preview = null;
        if (!this.steps.finish(this.finishName(), this.icon))
            this.doc.pixelsChanged(this.layer, null);
        this.setHint(this.hint());
        this.repaint();
    }

    leave() {
        this.editing = false;
        this.drag = null;
        this.steps.leave();
        if (this.doc?.preview?.canvas === this.preview)
            this.doc.setPreview(null);
        this.setHint(this.hint());
    }

    cancel() {
        if (!this.editing)
            return;
        this.drag = null;
        const step = this.steps.top();
        step ? this.resumeFrom(step) : this.leave();
    }

    beforeHistoryJump() {
        if (this.editing && !this.drag)
            this.commitStep();
    }

    historyChanged() {
        if (this.steps.pushing)
            return;
        const step = this.steps.top();
        if (step)
            this.resumeFrom(step);
        else if (this.editing)
            this.leave();
    }

    resumeFrom(step) {
        this.restoring = true;
        this.restoreState(step.state);
        this.restoring = false;
        this.editing = true;
        this.layer = step.layer;
        this.start = cloneCanvas(step.base);
        this.preview = cloneCanvas(this.layer.canvas);
        this.rect = step.rect;
        this.steps.resume(step);
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
        this.render();
        this.setHint(this.hint());
        this.repaint();
    }

    onKeyDown(e) {
        if (!this.editing)
            return false;
        // Como en Kaint, Intro y Esc terminan la edición.
        if (e.key === "Enter" || e.key === "Escape") { this.finish(); return true; }
        return false;
    }
}

export class PaintBucketTool extends EditableFill {
    get id() { return "paintbucket"; }
    get name() { return tr("Cubo de pintura"); }
    get icon() { return "paintbucket"; }
    get letter() { return "F"; }
    get options() { return ["flood", "tolerance", "sampling", "antialias", "blend", "finish"]; }
    editName() { return tr("Editar cubo de pintura"); }
    finishName() { return tr("Finalizar cubo de pintura"); }

    hint() {
        return this.editing
            ? tr("Cambia la tolerancia o los colores, o arrastra el punto de origen. Intro: terminar.")
            : tr("Clic izquierdo: rellenar con el color principal · clic derecho: con el secundario. Mayús: modo global.");
    }


    // Tirador para mover el origen del relleno, abajo a la derecha del punto (como Kaint).
    handlePos() {
        const v = this.view.imageToView(this.seed.x + 0.5, this.seed.y + 0.5);
        return { x: v.x + 16, y: v.y + 16 };
    }

    onPointerDown(e) {
        const doc = this.doc;
        if (!doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (this.editing && e.button === "left" && nearPoint(e, this.handlePos(), 11)) {
            this.drag = { grab: { x: this.seed.x + 0.5 - e.x, y: this.seed.y + 0.5 - e.y } };
            return;
        }
        this.finish();
        const x = Math.floor(e.x), y = Math.floor(e.y);
        if (x < 0 || y < 0 || x >= doc.width || y >= doc.height || (doc.selection.mask && !doc.selection.mask[y * doc.width + x]))
            return;
        const s = this.settings;
        this.sample = context(s.sampleImage ? doc.flattened() : doc.activeLayer.canvas).getImageData(0, 0, doc.width, doc.height);
        this.seed = { x, y };
        this.useSecondary = e.button === "right";
        this.shiftGlobal = !!e.shift;
        this.begin();
        this.render();
        // El relleno entra en el historial al momento; los cambios posteriores son "Editar…".
        this.steps.changed();
        this.commitStep();
        this.setHint(this.hint());
    }

    onPointerMove(e) {
        if (!this.drag) {
            if (this.editing)
                this.view.setCursor(nearPoint(e, this.handlePos(), 11) ? "move" : this.cursor());
            return;
        }
        const doc = this.doc;
        const x = Math.floor(e.x + this.drag.grab.x), y = Math.floor(e.y + this.drag.grab.y);
        if ((x === this.seed.x && y === this.seed.y) || x < 0 || y < 0 || x >= doc.width || y >= doc.height)
            return;
        if (doc.selection.mask && !doc.selection.mask[y * doc.width + x])
            return;
        this.seed = { x, y };
        this.drag.moved = true;
        this.render();
        this.steps.changed();
    }

    onPointerUp() {
        const moved = this.drag?.moved;
        this.drag = null;
        if (moved)
            this.commitStep();
    }

    saveState() {
        const s = this.settings;
        return { seed: { ...this.seed }, useSecondary: this.useSecondary, shiftGlobal: this.shiftGlobal, sample: this.sample,
                 primary: { ...AppState.primary }, secondary: { ...AppState.secondary },
                 settings: { tolerance: s.tolerance, floodGlobal: s.floodGlobal, sampleImage: s.sampleImage, antialias: s.antialias, blend: s.blend } };
    }

    restoreState(st) {
        this.seed = { ...st.seed };
        this.useSecondary = st.useSecondary;
        this.shiftGlobal = st.shiftGlobal;
        this.sample = st.sample;
        AppState.updateTools(st.settings);
        AppState.setPrimary(st.primary);
        AppState.setSecondary(st.secondary);
    }

    // El relleno sobre la capa de antes de empezar, con las opciones y colores actuales.
    render() {
        const doc = this.doc, s = this.settings, w = doc.width, h = doc.height, sel = doc.selection;
        const mask = floodMask(this.sample, this.seed.x, this.seed.y, s.tolerance, s.floodGlobal !== this.shiftGlobal);
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
        const pctx = context(this.preview);
        const old = this.rect;
        if (old) {
            pctx.clearRect(old.x, old.y, old.w, old.h);
            pctx.drawImage(this.start, old.x, old.y, old.w, old.h, old.x, old.y, old.w, old.h);
        }
        if (x1 < 0) {
            if (old)
                doc.pixelsChanged(this.layer, old);
            this.rect = old;
            return;
        }
        // Borde suavizado: los píxeles del borde de la zona se cubren a medias.
        const rect = { x: Math.max(0, x0 - 1), y: Math.max(0, y0 - 1) };
        rect.w = Math.min(w, x1 + 2) - rect.x;
        rect.h = Math.min(h, y1 + 2) - rect.y;
        const out = context(this.start).getImageData(rect.x, rect.y, rect.w, rect.h);
        const top = new ImageData(rect.w, rect.h), c = this.useSecondary ? AppState.secondary : AppState.primary;
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
        pctx.putImageData(out, rect.x, rect.y);
        this.rect = old ? unionRect(old, rect) : rect;
        doc.pixelsChanged(this.layer, this.rect);
    }

    paintOverlay(ctx, view) {
        if (!this.editing)
            return;
        const v = view.imageToView(this.seed.x + 0.5, this.seed.y + 0.5);
        ctx.fillStyle = "#fff";
        ctx.strokeStyle = "#000";
        ctx.fillRect(Math.round(v.x) - 2, Math.round(v.y) - 2, 4, 4);
        ctx.strokeRect(Math.round(v.x) - 2.5, Math.round(v.y) - 2.5, 5, 5);
        drawMoveHandle(ctx, this.handlePos());
    }
}

export class GradientTool extends EditableFill {
    get id() { return "gradient"; }
    get name() { return tr("Degradado"); }
    get icon() { return "gradient"; }
    get letter() { return "G"; }
    get options() { return ["gradient", "finish"]; }
    editName() { return tr("Mover degradado"); }
    finishName() { return tr("Finalizar degradado"); }

    hint() {
        return this.editing
            ? tr("Arrastra los extremos o el icono de mover; clic derecho en un extremo invierte los colores. Intro: terminar.")
            : tr("Arrastra para dibujar el degradado del color principal al secundario (clic derecho: al revés). Mayús: ángulos de 15°.");
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

    // Icono para mover todo el degradado, junto al centro de la línea (como Kaint).
    moveHandle() {
        const a = this.view.imageToView(this.a.x, this.a.y), b = this.view.imageToView(this.b.x, this.b.y);
        return { x: (a.x + b.x) / 2 + 18, y: (a.y + b.y) / 2 + 18 };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        const handle = this.handleAt(e);
        if (handle >= 0) {
            if (e.button === "right") {
                this.reversed = !this.reversed;
                this.render();
                this.steps.changed();
                this.commitStep();
                return;
            }
            this.drag = { handle };
            return;
        }
        if (this.editing && nearPoint(e, this.moveHandle(), 11)) {
            this.drag = { both: true, grab: { x: e.x, y: e.y } };
            return;
        }
        this.finish();
        this.reversed = e.button === "right";
        this.a = { x: e.x, y: e.y };
        this.b = { x: e.x, y: e.y };
        this.begin();
        this.drag = { handle: 1 };
        this.setHint(this.hint());
    }

    onPointerMove(e) {
        if (!this.drag) {
            if (this.editing)
                this.view.setCursor(nearPoint(e, this.moveHandle(), 11) ? "move" : this.cursor());
            return;
        }
        let p = { x: e.x, y: e.y };
        if (this.drag.both) {
            const dx = p.x - this.drag.grab.x, dy = p.y - this.drag.grab.y;
            this.a = { x: this.a.x + dx, y: this.a.y + dy };
            this.b = { x: this.b.x + dx, y: this.b.y + dy };
            this.drag.grab = p;
        } else {
            if (e.shift) {
                const o = this.drag.handle === 1 ? this.a : this.b;
                const ang = Math.round(Math.atan2(p.y - o.y, p.x - o.x) / (Math.PI / 12)) * (Math.PI / 12);
                const d = Math.hypot(p.x - o.x, p.y - o.y);
                p = { x: o.x + Math.cos(ang) * d, y: o.y + Math.sin(ang) * d };
            }
            this.drag.handle === 0 ? (this.a = p) : (this.b = p);
        }
        this.drag.moved = true;
        this.render();
        this.steps.changed();
    }

    onPointerUp() {
        const moved = this.drag?.moved;
        this.drag = null;
        if (moved && (this.a.x !== this.b.x || this.a.y !== this.b.y))
            this.commitStep();
    }

    saveState() {
        const s = this.settings;
        return { a: { ...this.a }, b: { ...this.b }, reversed: this.reversed, primary: { ...AppState.primary }, secondary: { ...AppState.secondary },
                 settings: { gradient: s.gradient, gradientRepeat: s.gradientRepeat, gradientTransparency: s.gradientTransparency, blend: s.blend } };
    }

    restoreState(st) {
        this.a = { ...st.a };
        this.b = { ...st.b };
        this.reversed = st.reversed;
        AppState.updateTools(st.settings);
        AppState.setPrimary(st.primary);
        AppState.setSecondary(st.secondary);
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
        const first = this.reversed ? AppState.secondary : AppState.primary, second = this.reversed ? AppState.primary : AppState.secondary;
        for (const [p, c] of [[a, first], [b, second]]) {
            ctx.beginPath();
            ctx.arc(p.x, p.y, 6, 0, Math.PI * 2);
            ctx.fillStyle = `rgb(${c.r},${c.g},${c.b})`;
            ctx.fill();
            ctx.strokeStyle = "#fff"; ctx.lineWidth = 2; ctx.stroke();
            ctx.strokeStyle = "#000"; ctx.lineWidth = 1; ctx.stroke();
        }
        if (!this.drag)
            drawMoveHandle(ctx, this.moveHandle());
    }
}
