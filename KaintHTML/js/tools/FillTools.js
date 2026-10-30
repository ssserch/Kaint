// Cubo de pintura y degradado (src/app/tools/FillTools.cpp): siguen editables (el origen del
// relleno, los extremos del degradado, las opciones y los colores) hasta pulsar Intro.
import { Tool } from "./Tool.js";
import { drawMoveHandle, nearPoint, StepRecorder } from "./EditableSteps.js";
import { AppState } from "../AppState.js";
import { paintBucket } from "../core/PaintBucket.js";
import { blendPixels, cloneCanvas, context, lerpStraight, scaleAlpha, unionRect } from "../core/Raster.js";
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
    get options() { return ["flood", "fill", "tolerance", "tolAlpha", "sampling", "antialias", "accumulate", "blend", "selectionQuality", "finish"]; }
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
        const clip = doc.selection.clipMask(AppState.selectionAntialias());
        if (x < 0 || y < 0 || x >= doc.width || y >= doc.height || (clip && !clip[y * doc.width + x]))
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
        const clip = doc.selection.clipMask(AppState.selectionAntialias());
        if (clip && !clip[y * doc.width + x])
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
                 settings: { tolerance: s.tolerance, floodGlobal: s.floodGlobal, sampleImage: s.sampleImage, antialias: s.antialias, blend: s.blend,
                             toleranceStraight: !!s.toleranceStraight, fillAccumulate: s.fillAccumulate !== false, fill: s.fill || 0 } };
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

    // El relleno sobre la capa de antes de empezar, con las opciones y colores actuales
    // (PaintBucketTool::render de Kaint).
    render() {
        const doc = this.doc, s = this.settings, w = doc.width, h = doc.height;
        const clip = doc.selection.clipMask(AppState.selectionAntialias());
        const pctx = context(this.preview);
        const old = this.rect;
        if (old) {
            pctx.clearRect(old.x, old.y, old.w, old.h);
            pctx.drawImage(this.start, old.x, old.y, old.w, old.h, old.x, old.y, old.w, old.h);
        }
        if (this.startFor !== this.start) {
            this.startFor = this.start;
            this.startData = context(this.start).getImageData(0, 0, w, h);
        }
        const r = paintBucket(this.startData, this.sample, this.seed, {
            tolerance: s.tolerance, global: s.floodGlobal !== this.shiftGlobal, straight: !!s.toleranceStraight, antialias: s.antialias,
            accumulate: s.fillAccumulate !== false, blend: s.blend, color: this.useSecondary ? AppState.secondary : AppState.primary,
            background: this.useSecondary ? AppState.primary : AppState.secondary, pattern: s.fill || 0,
            sampleIsLayer: !s.sampleImage,
        }, clip);
        if (!r) {
            if (old)
                doc.pixelsChanged(this.layer, old);
            this.rect = old;
            return;
        }
        pctx.putImageData(r.data, r.rect.x, r.rect.y);
        this.rect = old ? unionRect(old, r.rect) : r.rect;
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
    get options() { return ["gradient", "antialias", "blend", "selectionQuality", "finish"]; }
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

    // Parámetro del degradado (0..1) en el centro de un píxel, según el tipo y la repetición
    // (GradientTool::render de Kaint).
    value(px, py) {
        const s = this.settings, a = this.a, dx = this.b.x - a.x, dy = this.b.y - a.y;
        const len2 = dx * dx + dy * dy, len = Math.sqrt(len2), repeat = s.gradientRepeat;
        const shape = (t) => {
            if (repeat === "wrapped")
                return t - Math.floor(t);
            if (repeat === "reflected") {
                const m = Math.abs(t) % 2;
                return m <= 1 ? m : 2 - m;
            }
            return Math.min(1, Math.max(0, t));
        };
        const vx = px - a.x, vy = py - a.y;
        const dot = (vx * dx + vy * dy) / len2, cross = (vx * dy - vy * dx) / len2;
        switch (s.gradient) {
        case "reflected": return shape(Math.abs(dot));
        case "diamond": return shape(Math.abs(dot) + Math.abs(cross));
        case "radial": return shape(Math.hypot(vx, vy) / len);
        case "conical": return Math.abs(Math.atan2(vx * dy - vy * dx, vx * dx + vy * dy)) / Math.PI;
        case "spiralcw":
        case "spiralccw": {
            const ang = Math.atan2(vx * dy - vy * dx, vx * dx + vy * dy) / (2 * Math.PI);
            const t = Math.hypot(vx, vy) / len + (s.gradient === "spiralcw" ? -ang : ang);
            return t - Math.floor(t);
        }
        default: return shape(dot);
        }
    }

    // Modo de color: del principal al secundario, compuesto con el modo de fusión; modo de
    // transparencia: del alfa del principal al inverso del alfa del secundario.
    render() {
        const doc = this.doc, s = this.settings, w = doc.width, h = doc.height, sel = doc.selection;
        const clip = sel.clipMask(AppState.selectionAntialias());
        const b = sel.bounds() || { x: 0, y: 0, w, h };
        const startCtx = context(this.start), base = startCtx.getImageData(b.x, b.y, b.w, b.h), d = base.data;
        const pctx = context(this.preview);
        const ddx = this.b.x - this.a.x, ddy = this.b.y - this.a.y;
        if (ddx * ddx + ddy * ddy < 1) {
            pctx.putImageData(base, b.x, b.y);
            doc.pixelsChanged(this.layer, b);
            this.rect = b;
            return;
        }
        let c0 = AppState.primary, c1 = AppState.secondary;
        if (this.reversed) [c0, c1] = [c1, c0];
        const weight = (x, y) => (clip ? clip[(b.y + y) * w + b.x + x] : 255);
        if (s.gradientTransparency) {
            const a0 = (c0.a ?? 255) / 255, a1 = 1 - (c1.a ?? 255) / 255;
            for (let y = 0; y < b.h; ++y)
                for (let x = 0; x < b.w; ++x) {
                    const t = this.value(b.x + x + 0.5, b.y + y + 0.5), i = (y * b.w + x) * 4;
                    const faded = scaleAlpha(d[i + 3], Math.trunc((a0 + (a1 - a0) * t) * 255 + 0.5));
                    lerpStraight(d, i, d[i], d[i + 1], d[i + 2], faded, weight(x, y));
                }
        } else {
            const top = new ImageData(b.w, b.h), o = top.data, overwrite = s.blend === "overwrite";
            const mix = (p, q, t) => Math.trunc(Math.fround(Math.fround(p + Math.fround((q - p) * t)) + 0.5));
            const ca0 = c0.a ?? 255, ca1 = c1.a ?? 255;
            for (let y = 0; y < b.h; ++y)
                for (let x = 0; x < b.w; ++x) {
                    const t = Math.fround(this.value(b.x + x + 0.5, b.y + y + 0.5)), i = (y * b.w + x) * 4, m = weight(x, y);
                    const r = mix(c0.r, c1.r, t), g = mix(c0.g, c1.g, t), bl = mix(c0.b, c1.b, t), al = mix(ca0, ca1, t);
                    if (overwrite) {
                        lerpStraight(d, i, r, g, bl, al, m);
                        continue;
                    }
                    o[i] = r; o[i + 1] = g; o[i + 2] = bl; o[i + 3] = scaleAlpha(al, m);
                }
            if (!overwrite)
                blendPixels(base, top, s.blend);
        }
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
