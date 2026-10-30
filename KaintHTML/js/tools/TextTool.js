// Texto (src/app/tools/TextTool.cpp): se escribe en un campo oculto (así funcionan las flechas,
// Inicio, Fin, borrar y los métodos de entrada) y se dibuja en la vista previa de la capa hasta
// aplicarlo con Esc, Terminar o un clic fuera. Como en Kaint, lo escrito aparece en el historial al
// momento ("Texto", o "Editar texto" al retomarlo) y al terminar se añade "Finalizar texto".
import { Tool } from "./Tool.js";
import { StepRecorder } from "./EditableSteps.js";
import { AppState } from "../AppState.js";
import { cloneCanvas, compositeTool, context, createCanvas, unionRect } from "../core/Raster.js";
import { Settings } from "../Settings.js";
import { miniCursor, toolPixelCursor } from "../ui/Icons.js";
import { tr } from "../i18n.js";

export class TextTool extends Tool {
    constructor(app) {
        super(app);
        this.steps = new StepRecorder(this);
        this.input = document.getElementById("text-input");
        this.input.addEventListener("input", () => this.editing && this.changed());
        this.input.addEventListener("keydown", (e) => {
            if (!this.editing)
                return;
            e.stopPropagation();
            if (e.key === "Escape") {
                e.preventDefault();
                this.finish();
            } else {
                setTimeout(() => this.render(), 0);
            }
        });
        this.caretOn = true;
        setInterval(() => {
            if (!this.editing)
                return;
            this.caretOn = !this.caretOn || !Settings.get("animations");
            this.repaint();
        }, 530);
    }

    get id() { return "text"; }
    get name() { return tr("Texto"); }
    get icon() { return "text"; }
    get letter() { return "T"; }
    get options() { return ["text", "antialias", "blend", "selectionQuality", "finish"]; }

    hint() {
        return this.editing
            ? tr("Escribe el texto. Flechas, Inicio y Fin mueven el cursor; arrastra el tirador o con el botón derecho para moverlo. Esc o Terminar: aplicar.")
            : tr("Haz clic donde quieras empezar a escribir con el color principal.");
    }

    cursor() {
        return toolPixelCursor(this.id) || miniCursor("text", true);
    }

    wantsKeyboard() {
        return !!this.editing;
    }

    get recordsSteps() {
        return true;
    }

    activate() {
        this.onSettings = () => this.editing && !this.restoring && this.changed();
        AppState.addEventListener("tools", this.onSettings);
        AppState.addEventListener("colors", this.onSettings);
    }

    deactivate() {
        AppState.removeEventListener("tools", this.onSettings);
        AppState.removeEventListener("colors", this.onSettings);
        this.finish();
    }

    font() {
        // Puntos: según los PPP de la imagen; fijo: a 96 PPP (FontMetric de Kaint).
        const s = this.settings, px = (s.fontSize * (s.fontMetric === "fixed96" ? 96 : this.doc?.dpi || 96)) / 72;
        return { css: `${s.italic ? "italic " : ""}${s.bold ? "bold " : ""}${px}px ${s.fontFamily}`, px, line: px * 1.25 };
    }

    // Rectángulo del texto en la imagen.
    layout() {
        const ctx = context(this.temp), f = this.font();
        ctx.font = f.css;
        const lines = this.input.value.split("\n");
        const widths = lines.map((l) => ctx.measureText(l).width);
        const w = Math.max(f.px * 0.5, ...widths);
        return { lines, widths, w, h: lines.length * f.line, f };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (this.editing) {
            const l = this.layout();
            const inside = e.x >= this.pos.x - 4 && e.y >= this.pos.y - 4 && e.x <= this.pos.x + l.w + 8 && e.y <= this.pos.y + l.h + 8;
            if (inside || e.button === "right") {
                this.drag = { from: { x: e.x, y: e.y }, start: { ...this.pos } };
                this.focusInput(e);
                return;
            }
            this.finish();
        }
        this.editing = true;
        this.resumed = false;
        this.pos = { x: e.x, y: e.y };
        this.layer = this.doc.activeLayer;
        this.start = cloneCanvas(this.layer.canvas);
        this.preview = cloneCanvas(this.layer.canvas);
        this.temp = createCanvas(this.doc.width, this.doc.height);
        this.bounds = null;
        this.steps.start();
        this.input.value = "";
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
        this.focusInput(e);
        this.setHint(this.hint());
        this.render();
    }

    // El campo oculto recibe el teclado al momento (sin que el clic devuelva el foco al lienzo).
    focusInput(e) {
        e.original?.preventDefault();
        this.input.focus({ preventScroll: true });
    }

    onPointerMove(e) {
        if (!this.drag)
            return;
        this.pos = { x: this.drag.start.x + e.x - this.drag.from.x, y: this.drag.start.y + e.y - this.drag.from.y };
        this.drag.moved = true;
        this.render();
    }

    onPointerUp() {
        const moved = this.drag?.moved;
        this.drag = null;
        if (moved)
            this.changed();
    }

    // Lo escrito (o movido, o con otras opciones) se ve y queda en el historial al momento.
    changed() {
        this.render();
        if (!this.input.value && !this.resumed)
            return;
        this.steps.changed();
        this.commitLive();
    }

    commitLive() {
        const name = this.resumed ? tr("Editar texto") : this.name;
        this.steps.commit({ layer: this.layer, preview: this.preview, rect: this.bounds, base: this.start, state: this.saveState(),
                            createText: name, editText: name, icon: this.icon, live: true });
    }

    saveState() {
        const s = this.settings;
        return { pos: { ...this.pos }, text: this.input.value, primary: { ...AppState.primary }, secondary: { ...AppState.secondary },
                 settings: { fontFamily: s.fontFamily, fontSize: s.fontSize, bold: s.bold, italic: s.italic, underline: s.underline,
                             textAlign: s.textAlign, antialias: s.antialias, blend: s.blend, strikeout: !!s.strikeout,
                             fontMetric: s.fontMetric, textRendering: s.textRendering } };
    }

    historyChanged() {
        if (this.steps.pushing)
            return;
        const step = this.steps.top();
        if (step) {
            this.resumeFrom(step);
        } else if (this.editing) {
            this.editing = false;
            this.drag = null;
            this.steps.leave();
            this.input.value = "";
            this.input.blur();
            if (this.doc?.preview?.canvas === this.preview)
                this.doc.setPreview(null);
            this.setHint(this.hint());
        }
    }

    // Se vuelve a un paso del texto: su posición, lo escrito, sus opciones y colores.
    resumeFrom(step) {
        const st = step.state;
        this.restoring = true;
        AppState.updateTools(st.settings);
        AppState.setPrimary(st.primary);
        AppState.setSecondary(st.secondary);
        this.restoring = false;
        this.editing = true;
        this.resumed = true;
        this.pos = { ...st.pos };
        this.layer = step.layer;
        this.start = cloneCanvas(step.base);
        this.preview = cloneCanvas(this.layer.canvas);
        this.temp = createCanvas(this.doc.width, this.doc.height);
        this.bounds = step.rect;
        this.steps.resume(step);
        // Los cambios siguientes van en un paso nuevo ("Editar texto").
        this.steps.last = null;
        this.input.value = st.text;
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
        this.render();
        this.input.focus({ preventScroll: true });
        this.setHint(this.hint());
    }

    beforeHistoryJump() {
        if (this.editing && this.steps.dirty)
            this.commitLive();
    }

    render() {
        const doc = this.doc, s = this.settings, l = this.layout(), f = l.f, c = AppState.primary;
        // Dos pasadas, con el color opaco (la cobertura) y con el de verdad, compuestas como en
        // Kaint (compositeTool): así funcionan "Sobrescribir" y los colores translúcidos sin suavizado.
        if (this.real?.width !== doc.width || this.real?.height !== doc.height)
            this.real = createCanvas(doc.width, doc.height);
        for (const [canvas, alpha] of [[this.temp, 255], [this.real, c.a ?? 255]]) {
            const ctx = context(canvas);
            ctx.clearRect(0, 0, doc.width, doc.height);
            ctx.font = f.css;
            ctx.textBaseline = "top";
            // Suave o nítido (lo más parecido del navegador al ajuste a la cuadrícula de Kaint).
            ctx.textRendering = { sharpModern: "optimizeLegibility", sharpClassic: "optimizeSpeed" }[s.textRendering] || "geometricPrecision";
            ctx.fillStyle = `rgba(${c.r},${c.g},${c.b},${alpha / 255})`;
            l.lines.forEach((line, i) => {
                const x = s.textAlign === "center" ? this.pos.x + (l.w - l.widths[i]) / 2 : s.textAlign === "right" ? this.pos.x + l.w - l.widths[i] : this.pos.x;
                const y = this.pos.y + i * f.line;
                ctx.fillText(line, x, y);
                if (s.underline && line)
                    ctx.fillRect(x, y + f.px * 1.05, l.widths[i], Math.max(1, f.px / 14));
                if (s.strikeout && line)
                    ctx.fillRect(x, y + f.px * 0.55, l.widths[i], Math.max(1, f.px / 14));
            });
        }
        const box = this.clampBox({ x: this.pos.x - 4, y: this.pos.y - 4, w: l.w + f.px + 8, h: l.h + 8 });
        const pctx = context(this.preview), dirty = unionRect(this.bounds, box);
        if (dirty) {
            pctx.clearRect(dirty.x, dirty.y, dirty.w, dirty.h);
            pctx.drawImage(this.start, dirty.x, dirty.y, dirty.w, dirty.h, dirty.x, dirty.y, dirty.w, dirty.h);
            if (box)
                compositeTool(pctx, this.temp, this.real, box, { mode: s.blend, antialias: s.antialias, width: doc.width,
                                                               clip: doc.selection.clipMask(AppState.selectionAntialias()) });
            doc.pixelsChanged(this.layer, dirty);
        }
        this.bounds = box;
        this.caretOn = true;
    }

    clampBox(r) {
        const doc = this.doc;
        const x0 = Math.max(0, Math.floor(r.x)), y0 = Math.max(0, Math.floor(r.y));
        const x1 = Math.min(doc.width, Math.ceil(r.x + r.w)), y1 = Math.min(doc.height, Math.ceil(r.y + r.h));
        return x1 > x0 && y1 > y0 ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : null;
    }

    paintOverlay(ctx, view) {
        if (!this.editing)
            return;
        const l = this.layout(), f = l.f;
        const a = view.imageToView(this.pos.x - 2, this.pos.y - 2), b = view.imageToView(this.pos.x + l.w + 2, this.pos.y + l.h + 2);
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = "rgba(0,0,0,0.6)";
        ctx.lineWidth = 1;
        ctx.strokeRect(a.x + 0.5, a.y + 0.5, b.x - a.x, b.y - a.y);
        ctx.setLineDash([]);
        // Tirador para mover, abajo a la derecha.
        ctx.fillStyle = "#fff";
        ctx.fillRect(b.x - 3, b.y - 3, 7, 7);
        ctx.strokeStyle = "#000";
        ctx.strokeRect(b.x - 2.5, b.y - 2.5, 6, 6);
        if (!this.caretOn)
            return;
        // Cursor de texto en la posición del campo oculto.
        const caret = this.input.selectionStart ?? this.input.value.length;
        const before = this.input.value.slice(0, caret).split("\n");
        const row = before.length - 1;
        const tctx = context(this.temp);
        tctx.font = f.css;
        const s = this.settings;
        const lineX = s.textAlign === "center" ? (l.w - l.widths[row]) / 2 : s.textAlign === "right" ? l.w - l.widths[row] : 0;
        const x = this.pos.x + lineX + tctx.measureText(before[row]).width, y = this.pos.y + row * f.line;
        const p = view.imageToView(x, y), q = view.imageToView(x, y + f.px * 1.15);
        ctx.strokeStyle = "#000";
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(p.x, p.y);
        ctx.lineTo(q.x, q.y);
        ctx.stroke();
    }

    isBusy() {
        return !!this.drag;
    }

    finish() {
        if (!this.editing)
            return;
        this.editing = false;
        this.drag = null;
        this.input.blur();
        const doc = this.doc, layer = this.layer;
        if (!this.input.value.trim() && !this.resumed) {
            // Nada escrito: no queda nada.
            doc.preview = null;
            doc.pixelsChanged(layer, null);
            this.steps.leave();
        } else {
            this.render();
            if (this.steps.dirty)
                this.commitLive();
            doc.preview = null;
            this.steps.finish(tr("Finalizar texto"), this.icon);
            doc.pixelsChanged(layer, null);
        }
        this.input.value = "";
        this.setHint(this.hint());
        this.app.view.canvas.focus({ preventScroll: true });
    }

    // Deshacer en mitad de un arrastre: vuelve al último paso o, si no hay, se descarta.
    cancel() {
        if (!this.editing)
            return;
        this.drag = null;
        const step = this.steps.top();
        if (step) {
            this.resumeFrom(step);
            return;
        }
        this.editing = false;
        this.input.value = "";
        this.input.blur();
        this.steps.leave();
        this.doc.setPreview(null);
        this.setHint(this.hint());
    }
}
