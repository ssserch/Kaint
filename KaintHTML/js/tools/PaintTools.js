// Pincel, goma, lápiz, tampón de clonar y volver a colorear (src/app/tools/PaintTools.cpp).
import { Tool } from "./Tool.js";
import { Stroke } from "./Stroke.js";
import { AppState } from "../AppState.js";
import { Settings } from "../Settings.js";
import { tr } from "../i18n.js";

export class StrokeTool extends Tool {
    // Como en Kaint: las flechas mueven el pincel un píxel (Canvas.nudgePointer).
    arrowKeysMovePointer() { return true; }

    get options() {
        return ["brushSize", "hardness", "spacing", "fill", "smoothing", "pressure", "antialias", "blend", "advanced", "selectionQuality"];
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
                 hardness: this.settings.hardness, spacing: this.settings.spacing, smoothing: !!this.settings.smoothing, advanced: this.advancedOptions() };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.speedLast = null;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, this.strokeOptions(e));
        this.strokeTo(e);
    }

    onPointerMove(e) {
        if (!this.stroke)
            return;
        this.strokeTo(e);
    }

    onPointerUp(e) {
        if (!this.stroke)
            return;
        if (e)
            this.strokeTo(e);
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
    get options() { return ["brushSize", "hardness", "spacing", "smoothing", "pressure", "antialias", "advanced", "selectionQuality"]; }
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
    get options() { return ["blend", "selectionQuality"]; }
    hint() { return tr("Dibuja píxeles sueltos con el color principal (clic derecho: secundario), ideal para pixel art."); }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, { mode: "paint", color: this.colorFor(e.button),
                                                                    blend: this.settings.blend, antialias: false });
        this.last = { x: e.x, y: e.y };
        this.stroke.pixelLine(e.x, e.y, e.x, e.y);
    }

    onPointerMove(e) {
        if (!this.stroke)
            return;
        this.stroke.pixelLine(this.last.x, this.last.y, e.x, e.y);
        this.last = { x: e.x, y: e.y };
    }

    onPointerUp() {
        if (!this.stroke)
            return;
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
