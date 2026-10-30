// Línea/curva y formas (src/app/tools/ShapeTools.cpp): se dibujan arrastrando y siguen editables
// con sus tiradores hasta pulsar Intro, cambiar de herramienta o empezar otra. Como en Kaint, cada
// cambio es un paso del historial y al volver a uno se sigue editando desde él.
import { Tool } from "./Tool.js";
import { StepRecorder } from "./EditableSteps.js";
import { AppState } from "../AppState.js";
import { cloneCanvas, compositeTool, context, createCanvas, unionRect } from "../core/Raster.js";
import { miniCursor, shapeFillRule, shapeName, shapePath, ShapeTypes, toolPixelCursor } from "../ui/Icons.js";
import { CustomShapes } from "../core/CustomShapes.js";
import { patternTile } from "../core/FillPattern.js";
import { tr } from "../i18n.js";

const dashPatterns = { solid: [], dash: [3, 1], dot: [1, 1], dashdot: [3, 1, 1, 1], dashdotdot: [3, 1, 1, 1, 1, 1] };

const rgba = (c) => `rgba(${c.r},${c.g},${c.b},${(c.a ?? 255) / 255})`;

// Base de las herramientas editables que dibujan con el lienzo del navegador.
class EditableTool extends Tool {
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
        this.temp = createCanvas(this.doc.width, this.doc.height);
        this.bounds = null;
        this.steps.start();
        this.doc.setPreview({ layer: this.layer, canvas: this.preview });
        this.setHint(this.hint());
    }

    // Dibuja la figura sobre la copia del principio: draw(ctx, paint) recibe el contexto y la
    // función que da el estilo de cada color (con la trama de relleno, el otro color va donde la
    // trama no tiene bit). Se dibuja dos veces, con los colores opacos (la cobertura) y con los de
    // verdad, y se compone como en Kaint (compositeTool).
    renderWith(draw, margin) {
        const doc = this.doc, s = this.settings;
        this.real ||= createCanvas(doc.width, doc.height);
        for (const [canvas, opaque] of [[this.temp, true], [this.real, false]]) {
            const ctx = context(canvas);
            ctx.clearRect(0, 0, doc.width, doc.height);
            ctx.save();
            draw(ctx, (c) => {
                const main = opaque ? { ...c, a: 255 } : c;
                if (!s.fill)
                    return rgba(main);
                const o = c === AppState.primary ? AppState.secondary : AppState.primary, other = opaque ? { ...o, a: 255 } : o;
                return ctx.createPattern(patternTile(s.fill, main, other), "repeat");
            });
            ctx.restore();
        }
        const box = this.shapeBounds(margin);
        const pctx = context(this.preview);
        const dirty = unionRect(this.bounds, box);
        if (dirty) {
            pctx.clearRect(dirty.x, dirty.y, dirty.w, dirty.h);
            pctx.drawImage(this.start, dirty.x, dirty.y, dirty.w, dirty.h, dirty.x, dirty.y, dirty.w, dirty.h);
            if (box)
                compositeTool(pctx, this.temp, this.real, box, { mode: s.blend, antialias: s.antialias, width: doc.width,
                                                               clip: doc.selection.clipMask(AppState.selectionAntialias()) });
            doc.pixelsChanged(this.layer, dirty);
        }
        this.bounds = box;
    }

    clampBox(r) {
        const doc = this.doc;
        const x0 = Math.max(0, Math.floor(r.x)), y0 = Math.max(0, Math.floor(r.y));
        const x1 = Math.min(doc.width, Math.ceil(r.x + r.w)), y1 = Math.min(doc.height, Math.ceil(r.y + r.h));
        return x1 > x0 && y1 > y0 ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : null;
    }

    // Solo mientras se arrastra (el resto del tiempo, deshacer recorre sus pasos).
    isBusy() {
        return !!this.drag;
    }

    // Paso con lo que se ve ahora ("Línea/Curva" la primera vez, "Editar…" después).
    commitStep() {
        if (!this.editing || !this.hasContent())
            return;
        this.steps.commit({ layer: this.layer, preview: this.preview, rect: this.bounds, base: this.start, state: this.saveState(),
                            createText: this.historyName(), editText: this.editName(), icon: this.icon });
    }

    finish() {
        if (!this.editing)
            return;
        this.commitStep();
        this.editing = false;
        this.drag = null;
        const doc = this.doc, layer = this.layer;
        doc.preview = null;
        if (!this.steps.finish(this.finishName(), this.icon))
            doc.pixelsChanged(layer, null);
        this.setHint(this.hint());
    }

    // Deja de editar sin añadir pasos (lo que ya está en el historial se queda).
    leave() {
        this.editing = false;
        this.drag = null;
        this.steps.leave();
        if (this.doc?.preview?.canvas === this.preview)
            this.doc.setPreview(null);
        this.setHint(this.hint());
    }

    // Deshacer en mitad de un arrastre: vuelve al último paso (o descarta si no hay ninguno).
    cancel() {
        if (!this.editing)
            return;
        this.drag = null;
        const step = this.steps.top();
        if (step)
            this.resumeFrom(step);
        else
            this.leave();
    }

    // Lo pendiente se registra antes de moverse por el historial.
    beforeHistoryJump() {
        if (this.editing && !this.drag)
            this.commitStep();
    }

    // Al cambiar el historial: si lo último es un paso suyo, se retoma; si no, se deja de editar.
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
        this.temp = createCanvas(this.doc.width, this.doc.height);
        this.bounds = step.rect;
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

    onPointerUp() {
        const dragged = this.drag && this.drag.moved;
        this.drag = null;
        if (dragged)
            this.commitStep();
    }

    drawHandle(ctx, view, p, round = false) {
        const v = view.imageToView(p.x, p.y);
        ctx.beginPath();
        round ? ctx.arc(v.x, v.y, 4.5, 0, Math.PI * 2) : ctx.rect(v.x - 4, v.y - 4, 8, 8);
        ctx.fillStyle = "#fff";
        ctx.fill();
        ctx.strokeStyle = "#000";
        ctx.lineWidth = 1;
        ctx.stroke();
    }

    nearHandle(e, points) {
        for (const [i, p] of points.entries()) {
            const v = this.view.imageToView(p.x, p.y);
            if (Math.hypot(v.x - e.vx, v.y - e.vy) <= 7)
                return i;
        }
        return -1;
    }
}

function strokeStyle(ctx, s, css) {
    ctx.strokeStyle = css;
    ctx.lineWidth = s.brushSize;
    ctx.lineJoin = "round";
    ctx.setLineDash((dashPatterns[s.dash] || []).map((d) => d * s.brushSize));
}

export class LineTool extends EditableTool {
    get id() { return "linecurve"; }
    get name() { return tr("Línea/Curva"); }
    get icon() { return "linecurve"; }
    get letter() { return "O"; }
    get options() { return ["curve", "brushSize", "dash", "caps", "fill", "antialias", "blend", "selectionQuality", "finish"]; }

    hint() {
        return this.editing
            ? tr("Arrastra los nodos para curvar la línea; icono o flechas: mover; clic derecho: girar. , . / cambian remates y estilo. Intro: terminar.")
            : tr("Arrastra para dibujar una línea con el color principal (clic derecho: secundario). Mayús: 15°. Alt: desde el centro.");
    }

    historyName() {
        return this.name;
    }

    editName() {
        return tr("Editar línea/curva");
    }

    finishName() {
        return tr("Finalizar línea/curva");
    }

    saveState() {
        const s = this.settings;
        return { pts: this.pts.map((p) => ({ ...p })), useSecondary: this.useSecondary, primary: { ...AppState.primary }, secondary: { ...AppState.secondary },
                 settings: { brushSize: s.brushSize, antialias: s.antialias, blend: s.blend, dash: s.dash, startCap: s.startCap,
                             endCap: s.endCap, curveType: s.curveType, fill: s.fill || 0 } };
    }

    restoreState(st) {
        this.pts = st.pts.map((p) => ({ ...p }));
        this.useSecondary = st.useSecondary;
        AppState.updateTools(st.settings);
        AppState.setPrimary(st.primary);
        AppState.setSecondary(st.secondary);
    }

    hasContent() {
        return Math.hypot(this.pts[3].x - this.pts[0].x, this.pts[3].y - this.pts[0].y) > 0.5;
    }

    handles() {
        return this.settings.curveType === "straight" ? [this.pts[0], this.pts[3]] : this.pts;
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (this.editing) {
            const handles = this.handles();
            const i = this.nearHandle(e, handles);
            if (i >= 0) {
                this.drag = { index: this.pts.indexOf(handles[i]) };
                return;
            }
            this.finish();
        }
        this.useSecondary = e.button === "right";
        const p = { x: e.x, y: e.y };
        this.pts = [p, { ...p }, { ...p }, { ...p }];
        this.drag = { creating: true, origin: p };
        this.begin();
    }

    onPointerMove(e) {
        if (!this.drag)
            return;
        let p = { x: e.x, y: e.y };
        if (this.drag.creating) {
            const o = this.drag.origin;
            if (e.shift) {
                const ang = Math.round(Math.atan2(p.y - o.y, p.x - o.x) / (Math.PI / 12)) * (Math.PI / 12);
                const d = Math.hypot(p.x - o.x, p.y - o.y);
                p = { x: o.x + Math.cos(ang) * d, y: o.y + Math.sin(ang) * d };
            }
            const a = e.alt ? { x: 2 * o.x - p.x, y: 2 * o.y - p.y } : o;
            this.pts = [a, { x: a.x + (p.x - a.x) / 3, y: a.y + (p.y - a.y) / 3 },
                        { x: a.x + ((p.x - a.x) * 2) / 3, y: a.y + ((p.y - a.y) * 2) / 3 }, p];
        } else {
            this.pts[this.drag.index] = p;
        }
        this.drag.moved = true;
        this.steps.changed();
        this.render();
    }

    path(ctx) {
        const [p0, c1, c2, p3] = this.pts, type = this.settings.curveType;
        ctx.beginPath();
        ctx.moveTo(p0.x, p0.y);
        if (type === "straight") {
            ctx.lineTo(p3.x, p3.y);
        } else if (type === "bezier") {
            ctx.bezierCurveTo(c1.x, c1.y, c2.x, c2.y, p3.x, p3.y);
        } else {
            // Catmull-Rom por los cuatro puntos.
            const pts = [p0, p0, c1, c2, p3, p3];
            for (let i = 1; i < 4; ++i) {
                const a = pts[i - 1], b = pts[i], c = pts[i + 1], d = pts[i + 2];
                ctx.bezierCurveTo(b.x + (c.x - a.x) / 6, b.y + (c.y - a.y) / 6, c.x - (d.x - b.x) / 6, c.y - (d.y - b.y) / 6, c.x, c.y);
            }
        }
    }

    // Dirección de la línea en un extremo (para las flechas).
    direction(start) {
        const [p0, c1, c2, p3] = this.pts, type = this.settings.curveType;
        const a = start ? p0 : p3;
        let b = start ? (type === "straight" ? p3 : c1) : (type === "straight" ? p0 : c2);
        if (Math.hypot(b.x - a.x, b.y - a.y) < 0.01) b = start ? p3 : p0;
        const len = Math.hypot(b.x - a.x, b.y - a.y) || 1;
        return { x: (a.x - b.x) / len, y: (a.y - b.y) / len };
    }

    get color() {
        return this.useSecondary ? AppState.secondary : AppState.primary;
    }

    render() {
        const s = this.settings, w = s.brushSize;
        const xs = this.pts.map((p) => p.x), ys = this.pts.map((p) => p.y), m = w * 3 + 4;
        this.box = { x: Math.min(...xs) - m, y: Math.min(...ys) - m, w: Math.max(...xs) - Math.min(...xs) + 2 * m,
                     h: Math.max(...ys) - Math.min(...ys) + 2 * m };
        this.renderWith((ctx, paint) => {
            strokeStyle(ctx, s, paint(this.color));
            ctx.lineCap = s.startCap === "round" && s.endCap === "round" ? "round" : "butt";
            this.path(ctx);
            ctx.stroke();
            ctx.setLineDash([]);
            for (const [cap, start] of [[s.startCap, true], [s.endCap, false]]) {
                const p = start ? this.pts[0] : this.pts[3], d = this.direction(start);
                ctx.fillStyle = paint(this.color);
                if (cap === "round") {
                    ctx.beginPath(); ctx.arc(p.x, p.y, w / 2, 0, Math.PI * 2); ctx.fill();
                } else if (cap === "arrow" || cap === "filled") {
                    const size = Math.max(6, w * 3), n = { x: -d.y, y: d.x };
                    const back = { x: p.x - d.x * size, y: p.y - d.y * size };
                    const l = { x: back.x + n.x * size * 0.6, y: back.y + n.y * size * 0.6 };
                    const r = { x: back.x - n.x * size * 0.6, y: back.y - n.y * size * 0.6 };
                    ctx.beginPath();
                    ctx.moveTo(l.x, l.y); ctx.lineTo(p.x + d.x * w * 0.5, p.y + d.y * w * 0.5); ctx.lineTo(r.x, r.y);
                    if (cap === "filled") { ctx.closePath(); ctx.fill(); }
                    else { ctx.lineCap = "round"; ctx.stroke(); }
                }
            }
        });
    }

    shapeBounds() {
        return this.clampBox(this.box);
    }

    paintOverlay(ctx, view) {
        if (!this.editing || this.drag?.creating)
            return;
        for (const p of this.handles())
            this.drawHandle(ctx, view, p, true);
    }
}

export class ShapesTool extends EditableTool {
    get id() { return "shapes"; }
    get name() { return tr("Formas"); }
    get icon() { return "shapes"; }
    get letter() { return "O"; }
    get options() { return ["shape", "shapeStyle", "cornerRadius", "brushSize", "dash", "fill", "antialias", "blend", "selectionQuality", "finish"]; }

    // Junto al puntero, la figura elegida.
    cursor() {
        return toolPixelCursor(this.id) || miniCursor(`opt-shape:${this.settings.shape}`);
    }

    hint() {
        return this.editing
            ? tr("Tiradores: redimensionar (Mayús: proporción, Alt: desde el centro). Dentro: mover. Clic derecho o fuera del borde: girar. A: siguiente forma. Intro: terminar.")
            : tr("Arrastra para dibujar la forma: contorno con el color principal y relleno con el secundario (clic derecho: al revés). Mayús: proporciones iguales. Alt: desde el centro.");
    }

    historyName() {
        const name = shapeName(this.settings.shape);
        return name ? (this.settings.shape.startsWith("custom:") ? name : tr(name)) : this.name;
    }

    editName() {
        return tr("Editar forma");
    }

    finishName() {
        return tr("Finalizar forma");
    }

    saveState() {
        const s = this.settings;
        return { r: { ...this.r }, reversed: this.reversed, primary: { ...AppState.primary }, secondary: { ...AppState.secondary },
                 settings: { brushSize: s.brushSize, shape: s.shape, shapeStyle: s.shapeStyle, antialias: s.antialias, blend: s.blend,
                             dash: s.dash, cornerRadius: s.cornerRadius, fill: s.fill || 0 } };
    }

    restoreState(st) {
        this.r = { ...st.r };
        this.reversed = st.reversed;
        AppState.updateTools(st.settings);
        AppState.setPrimary(st.primary);
        AppState.setSecondary(st.secondary);
    }

    hasContent() {
        return Math.abs(this.r.w) >= 1 && Math.abs(this.r.h) >= 1;
    }

    corners() {
        const r = this.norm();
        return [
            { x: r.x, y: r.y }, { x: r.x + r.w / 2, y: r.y }, { x: r.x + r.w, y: r.y }, { x: r.x + r.w, y: r.y + r.h / 2 },
            { x: r.x + r.w, y: r.y + r.h }, { x: r.x + r.w / 2, y: r.y + r.h }, { x: r.x, y: r.y + r.h }, { x: r.x, y: r.y + r.h / 2 },
        ];
    }

    norm() {
        const r = this.r;
        return { x: Math.min(r.x, r.x + r.w), y: Math.min(r.y, r.y + r.h), w: Math.abs(r.w), h: Math.abs(r.h) };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        if (this.editing) {
            const i = this.nearHandle(e, this.corners());
            if (i >= 0) {
                this.drag = { handle: i, start: this.norm() };
                return;
            }
            const n = this.norm();
            if (e.x >= n.x && e.y >= n.y && e.x <= n.x + n.w && e.y <= n.y + n.h) {
                this.drag = { move: true, from: { x: e.x, y: e.y }, start: { ...this.r } };
                return;
            }
            this.finish();
        }
        this.reversed = e.button === "right";
        this.r = { x: e.x, y: e.y, w: 0, h: 0 };
        this.drag = { creating: true, origin: { x: e.x, y: e.y } };
        this.begin();
    }

    onPointerMove(e) {
        if (!this.drag)
            return;
        if (this.drag.creating) {
            const o = this.drag.origin;
            let w = e.x - o.x, h = e.y - o.y;
            if (e.shift) {
                const side = Math.max(Math.abs(w), Math.abs(h));
                w = Math.sign(w || 1) * side;
                h = Math.sign(h || 1) * side;
            }
            this.r = e.alt ? { x: o.x - w, y: o.y - h, w: 2 * w, h: 2 * h } : { x: o.x, y: o.y, w, h };
        } else if (this.drag.move) {
            this.r = { ...this.drag.start, x: this.drag.start.x + e.x - this.drag.from.x, y: this.drag.start.y + e.y - this.drag.from.y };
        } else {
            const s = this.drag.start, h = this.drag.handle;
            let x0 = s.x, y0 = s.y, x1 = s.x + s.w, y1 = s.y + s.h;
            if ([0, 6, 7].includes(h)) x0 = e.x;
            if ([2, 3, 4].includes(h)) x1 = e.x;
            if ([0, 1, 2].includes(h)) y0 = e.y;
            if ([4, 5, 6].includes(h)) y1 = e.y;
            this.r = { x: x0, y: y0, w: x1 - x0, h: y1 - y0 };
        }
        this.drag.moved = true;
        this.steps.changed();
        this.render();
    }

    render() {
        const s = this.settings, n = this.norm(), m = s.brushSize + 2;
        this.box = { x: n.x - m, y: n.y - m, w: n.w + 2 * m, h: n.h + 2 * m };
        let line = AppState.primary, fill = s.shapeStyle === "fill" ? AppState.primary : AppState.secondary;
        if (this.reversed) {
            line = AppState.secondary;
            fill = s.shapeStyle === "fill" ? AppState.secondary : AppState.primary;
        }
        this.renderWith((ctx, paint) => {
            ctx.beginPath();
            shapePath(ctx, s.shape, n.x, n.y, n.w, n.h, s.cornerRadius);
            if (s.shapeStyle !== "outline") {
                ctx.fillStyle = paint(fill);
                ctx.fill(shapeFillRule(s.shape));
            }
            if (s.shapeStyle !== "fill") {
                strokeStyle(ctx, s, paint(line));
                ctx.stroke();
            }
        });
    }

    shapeBounds() {
        return this.clampBox(this.box);
    }

    onKeyDown(e) {
        if (this.editing && e.key.toLowerCase() === "a" && !e.ctrlKey) {
            // Las integradas y, después, las personalizadas.
            const all = [...ShapeTypes.map(([id]) => id), ...CustomShapes.all().map((c) => `custom:${c.id}`)];
            const i = all.indexOf(this.settings.shape);
            AppState.updateTools({ shape: all[(i + (e.shiftKey ? -1 : 1) + all.length) % all.length] });
            return true;
        }
        return super.onKeyDown(e);
    }

    paintOverlay(ctx, view) {
        if (!this.editing || this.drag?.creating)
            return;
        const n = this.norm(), a = view.imageToView(n.x, n.y), b = view.imageToView(n.x + n.w, n.y + n.h);
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = "rgba(0,0,0,0.6)";
        ctx.strokeRect(a.x + 0.5, a.y + 0.5, b.x - a.x, b.y - a.y);
        ctx.setLineDash([]);
        for (const p of this.corners())
            this.drawHandle(ctx, view, p);
    }
}
