// Pincel, goma, lápiz, tampón de clonar y volver a colorear (src/app/tools/PaintTools.cpp).
import { Tool } from "./Tool.js";
import { Stroke } from "./Stroke.js";
import { recognize, shapeNames } from "../core/QuickShape.js";
import { AppState } from "../AppState.js";
import { Settings } from "../Settings.js";
import { tr } from "../i18n.js";

// La simetría y las formas rápidas del pincel, la goma y el lápiz (StrokeTool de Kaint).
const SymmetryAndShapes = {
    symmetryOptions() {
        const s = this.settings;
        return this.options.includes("symmetry") ? { symmetry: s.symmetry || 0, symmetryCount: s.symmetryCount || 6 } : {};
    },

    // Formas rápidas: quieto un momento al final del trazo, lo dibujado se vuelve la forma perfecta.
    startShape(e) {
        this.snapped = false;
        this.shapePoints = [{ x: e.x, y: e.y }];
        this.holdAt = { x: e.x, y: e.y };
        clearTimeout(this.holdTimer);
        // (Solo las que tienen la opción: el pincel, la goma y el lápiz.)
        if (this.settings.quickShape && this.options.includes("symmetry"))
            this.holdTimer = setTimeout(() => this.snapShape(), 550);
        else
            this.shapePoints = null;
    },

    followShape(e) {
        if (!this.settings.quickShape || !this.shapePoints)
            return;
        this.shapePoints.push({ x: e.x, y: e.y });
        // Quieto (moviéndose menos de unos píxeles en pantalla): sigue contando.
        if (Math.hypot(e.x - this.holdAt.x, e.y - this.holdAt.y) * (this.view?.zoom || 1) > 3) {
            this.holdAt = { x: e.x, y: e.y };
            clearTimeout(this.holdTimer);
            this.holdTimer = setTimeout(() => this.snapShape(), 550);
        }
    },

    endShape() {
        clearTimeout(this.holdTimer);
        this.shapePoints = null;
        this.snapped = false;
    },

    snapShape() {
        if (!this.stroke || this.snapped || !this.shapePoints || this.shapePoints.length < 3)
            return;
        const shape = recognize(this.shapePoints);
        if (!shape.kind)
            return;
        if (this.pixelShape)
            this.stroke.replacePixelsWith(shape.points);
        else
            this.stroke.replaceWith(shape.points);
        this.snapped = true;
        navigator.vibrate?.(10);
        this.notice(tr(shapeNames[shape.kind]));
        this.repaint();
    },

    // Las guías de la simetría (la línea del espejo o los radios) sobre la imagen.
    paintOverlay(ctx, view) {
        const s = this.settings, doc = this.doc;
        if (!s.symmetry || !doc || !this.options.includes("symmetry"))
            return;
        const w = doc.width, h = doc.height, cx = w / 2, cy = h / 2, lines = [];
        if (s.symmetry === 1 || s.symmetry === 3)
            lines.push([cx, 0, cx, h]);
        if (s.symmetry === 2 || s.symmetry === 3)
            lines.push([0, cy, w, cy]);
        if (s.symmetry === 4) {
            const n = Math.min(32, Math.max(2, s.symmetryCount || 6)), r = Math.hypot(w, h);
            for (let i = 0; i < n; ++i) {
                const a = (2 * Math.PI * i) / n - Math.PI / 2;
                lines.push([cx, cy, cx + Math.cos(a) * r, cy + Math.sin(a) * r]);
            }
        }
        ctx.save();
        ctx.beginPath();
        const p0 = view.imageToView(0, 0), p1 = view.imageToView(w, 0), p2 = view.imageToView(w, h), p3 = view.imageToView(0, h);
        ctx.moveTo(p0.x, p0.y); ctx.lineTo(p1.x, p1.y); ctx.lineTo(p2.x, p2.y); ctx.lineTo(p3.x, p3.y); ctx.closePath();
        ctx.clip();
        for (const [x0, y0, x1, y1] of lines) {
            const u = view.imageToView(x0, y0), v = view.imageToView(x1, y1);
            for (const [color, width, dash] of [["rgba(255,255,255,0.65)", 3, []], ["rgba(30,130,230,0.9)", 1.2, [6, 4]]]) {
                ctx.strokeStyle = color;
                ctx.lineWidth = width;
                ctx.setLineDash(dash);
                ctx.beginPath();
                ctx.moveTo(u.x, u.y);
                ctx.lineTo(v.x, v.y);
                ctx.stroke();
            }
        }
        ctx.restore();
    },
};

export class StrokeTool extends Tool {
    // Como en Kaint: las flechas mueven el pincel un píxel (Canvas.nudgePointer).
    arrowKeysMovePointer() { return true; }

    get options() {
        return ["brushSize", "hardness", "spacing", "fill", "smoothing", "pressure", "antialias", "blend", "advanced", "symmetry", "selectionQuality"];
    }

    brushDiameter() {
        return this.settings.brushSize;
    }

    // La presión (0..1) del puntero, o null sin ella: la del lápiz (si está activada en
    // Configuración) o, con el pincel avanzado y la presión simulada, la velocidad del ratón o del
    // dedo (los tramos largos, de un movimiento rápido, adelgazan el trazo; suavizada para que no salte).
    pointerPressure(e) {
        const s = this.settings;
        if (e.pointerType === "pen" && Settings.get("penPressure") !== false)
            return Math.min(1, Math.max(0, e.pressure));
        if (!s.advancedBrush || !s.speedPressure)
            return null;
        const last = this.speedLast, step = last ? Math.hypot(e.x - last.x, e.y - last.y) : 0;
        const target = Math.min(1, Math.max(0.25, 1.15 - step / 45));
        this.speedPressure = last ? this.speedPressure + (target - this.speedPressure) * 0.3 : target;
        this.speedLast = { x: e.x, y: e.y };
        return this.speedPressure;
    }

    size(e) {
        const s = this.settings, p = this.pointerPressure(e);
        this.lastPressure = p ?? 1;
        // La presión ajusta el tamaño si está activada en la barra; con el pincel avanzado, con su
        // curva y su tamaño mínimo.
        if (p === null || s.pressure === false)
            return s.brushSize;
        if (s.advancedBrush) {
            const lo = Math.min(1, Math.max(0.01, (s.minSize ?? 5) / 100)), gamma = Math.min(5, Math.max(0.2, (s.pressureCurve ?? 100) / 100));
            return Math.max(0.1, s.brushSize * (lo + (1 - lo) * Math.pow(p, gamma)));
        }
        return Math.max(0.1, s.brushSize * Math.min(1, Math.max(0.05, p)));
    }

    // Sigue el trazo hasta el puntero.
    strokeTo(e) {
        const size = this.size(e);
        this.stroke.lineTo(e.x, e.y, size, this.lastPressure);
    }

    // Las opciones avanzadas del pincel (Configuración > Herramientas), o null si están apagadas.
    advancedOptions() {
        const s = this.settings;
        if (!s.advancedBrush)
            return null;
        return { tipShape: s.tipShape, angle: s.tipAngle, roundness: s.tipRoundness / 100, flow: s.flow / 100, sizeJitter: s.sizeJitter / 100,
                 opacityJitter: s.opacityJitter / 100, angleJitter: s.angleJitter / 100, scatter: s.scatter / 100, pressureOpacity: !!s.pressureOpacity,
                 stabilizer: s.stabilizer, taperIn: s.taperIn, taperOut: s.taperOut };
    }

    // Con la trayectoria suavizada, el trazo pasa por los puntos del puntero con una curva (sin
    // quedarse por detrás de él).
    strokeOptions(e) {
        return { mode: "paint", color: this.colorFor(e.button), blend: this.settings.blend, antialias: this.settings.antialias,
                 hardness: this.settings.hardness, spacing: this.settings.spacing, smoothing: !!this.settings.smoothing, advanced: this.advancedOptions(),
                 ...this.symmetryOptions() };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.speedLast = null;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, this.strokeOptions(e));
        this.strokeTo(e);
        this.startShape(e);
    }

    onPointerMove(e) {
        if (!this.stroke || this.snapped)
            return;
        this.strokeTo(e);
        this.followShape(e);
    }

    onPointerUp(e) {
        if (!this.stroke)
            return;
        if (e && !this.snapped)
            this.strokeTo(e);
        this.endShape();
        const stroke = this.stroke;
        this.stroke = null;
        stroke.commit(this.name, this.icon);
    }

    isBusy() {
        return !!this.stroke;
    }

    finish() {
        this.onPointerUp();
    }
}

Object.assign(StrokeTool.prototype, SymmetryAndShapes);

export class BrushTool extends StrokeTool {
    get id() { return "paintbrush"; }
    get name() { return tr("Pincel"); }
    get icon() { return "paintbrush"; }
    get letter() { return "B"; }
    hint() { return tr("Clic izquierdo: pintar con el color principal · clic derecho: con el secundario."); }

    // Con una trama, el otro color va donde la trama no tiene bit (BrushTool::params de Kaint).
    strokeOptions(e) {
        return { ...super.strokeOptions(e), fill: this.settings.fill || 0, background: this.colorFor(e.button === "right" ? "left" : "right") };
    }
}

export class EraserTool extends StrokeTool {
    get id() { return "eraser"; }
    get name() { return tr("Goma de borrar"); }
    get icon() { return "eraser"; }
    get letter() { return "E"; }
    get options() { return ["brushSize", "hardness", "spacing", "smoothing", "pressure", "antialias", "advanced", "symmetry", "selectionQuality"]; }
    hint() { return tr("Arrastra para borrar. La opacidad del color principal (clic derecho: secundario) regula la fuerza."); }

    strokeOptions(e) {
        return { ...super.strokeOptions(e), mode: "erase" };
    }
}

export class PencilTool extends Tool {
    arrowKeysMovePointer() { return true; }

    get id() { return "pencil"; }
    get name() { return tr("Lápiz"); }
    get icon() { return "pencil"; }
    get letter() { return "P"; }
    get options() { return ["blend", "symmetry", "selectionQuality"]; }
    get pixelShape() { return true; }
    hint() { return tr("Dibuja píxeles sueltos con el color principal (clic derecho: secundario), ideal para pixel art."); }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, { mode: "paint", color: this.colorFor(e.button),
                                                                    blend: this.settings.blend, antialias: false, ...this.symmetryOptions() });
        this.last = { x: e.x, y: e.y };
        this.stroke.pixelLine(e.x, e.y, e.x, e.y);
        this.startShape(e);
    }

    onPointerMove(e) {
        if (!this.stroke || this.snapped)
            return;
        this.stroke.pixelLine(this.last.x, this.last.y, e.x, e.y);
        this.last = { x: e.x, y: e.y };
        this.followShape(e);
    }

    onPointerUp() {
        if (!this.stroke)
            return;
        this.endShape();
        const stroke = this.stroke;
        this.stroke = null;
        stroke.commit(this.name, this.icon);
    }

    isBusy() {
        return !!this.stroke;
    }

    finish() {
        this.onPointerUp();
    }
}

Object.assign(PencilTool.prototype, SymmetryAndShapes);

export class CloneStampTool extends StrokeTool {
    get id() { return "clonestamp"; }
    get name() { return tr("Tampón de clonar"); }
    get icon() { return "clonestamp"; }
    get letter() { return "L"; }

    hint() {
        return this.source
            ? tr("Pinta para copiar desde el origen (puede estar en otra capa). Ctrl+clic: elegir otro origen.")
            : tr("Ctrl+clic para elegir el punto de origen y luego pinta donde quieras copiarlo.");
    }

    onPointerDown(e) {
        if (!this.doc)
            return;
        if (e.ctrl) {
            this.source = { x: Math.floor(e.x), y: Math.floor(e.y), layer: this.doc.activeLayer };
            this.offset = null;
            this.setHint(this.hint());
            this.repaint();
            return;
        }
        if (!this.source) {
            // Sin punto de origen no hay nada que copiar: se avisa junto al puntero.
            if (e.button === "left")
                this.notice(tr("Primero Ctrl+clic para elegir el punto de origen."));
            return;
        }
        if (e.button !== "left")
            return;
        if (!this.offset)
            this.offset = { x: this.source.x - Math.floor(e.x), y: this.source.y - Math.floor(e.y) };
        const layer = this.doc.layers.includes(this.source.layer) ? this.source.layer : this.doc.activeLayer;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, {
            mode: "clone", color: AppState.primary, blend: this.settings.blend, antialias: this.settings.antialias, hardness: this.settings.hardness,
            spacing: this.settings.spacing, smoothing: !!this.settings.smoothing, cloneOffset: this.offset, cloneSource: layer.canvas,
            advanced: this.advancedOptions(),
        });
        this.speedLast = null;
        this.strokeTo(e);
    }

    // El origen, marcado con una cruz.
    paintOverlay(ctx, view) {
        if (!this.source || !this.doc)
            return;
        let p = { x: this.source.x + 0.5, y: this.source.y + 0.5 };
        if (this.offset && view.mouse)
            p = { x: view.mouse.x + this.offset.x, y: view.mouse.y + this.offset.y };
        const v = view.imageToView(p.x, p.y);
        ctx.strokeStyle = "#fff";
        ctx.lineWidth = 3;
        for (const [color, width] of [["#fff", 3], ["#000", 1]]) {
            ctx.strokeStyle = color;
            ctx.lineWidth = width;
            ctx.beginPath();
            ctx.moveTo(v.x - 8, v.y); ctx.lineTo(v.x + 8, v.y); ctx.moveTo(v.x, v.y - 8); ctx.lineTo(v.x, v.y + 8);
            ctx.stroke();
        }
    }
}

export class RecolorTool extends StrokeTool {
    get id() { return "recolor"; }
    get name() { return tr("Volver a colorear"); }
    get icon() { return "recolor"; }
    get letter() { return "R"; }
    get options() {
        return ["brushSize", "hardness", "spacing", "tolerance", "tolAlpha", "recolor", "smoothing", "pressure", "antialias", "selectionQuality"];
    }

    hint() {
        return this.settings.recolorSampling === "once"
            ? tr("Pinta para cambiar los tonos parecidos al del primer clic por el color principal (clic derecho: secundario).")
            : tr("Clic izquierdo: cambia los tonos parecidos al color secundario por el principal · clic derecho: al revés.");
    }

    strokeOptions(e) {
        let from;
        if (this.settings.recolorSampling === "once") {
            const d = this.doc.activeLayer.ctx.getImageData(Math.floor(e.x), Math.floor(e.y), 1, 1).data;
            from = { r: d[0], g: d[1], b: d[2], a: d[3] };
        } else {
            from = e.button === "right" ? AppState.primary : AppState.secondary;
        }
        return { mode: "recolor", color: this.colorFor(e.button), antialias: this.settings.antialias, hardness: this.settings.hardness, smoothing: !!this.settings.smoothing,
                 recolorFrom: from, recolorTolerance: this.settings.tolerance, recolorStraight: !!this.settings.toleranceStraight };
    }
}
