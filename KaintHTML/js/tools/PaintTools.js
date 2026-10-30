// Pincel, goma, lápiz, tampón de clonar y volver a colorear (src/app/tools/PaintTools.cpp).
import { Tool } from "./Tool.js";
import { Stroke } from "./Stroke.js";
import { AppState } from "../AppState.js";
import { Settings } from "../Settings.js";
import { tr } from "../i18n.js";

class StrokeTool extends Tool {
    get options() {
        return ["brushSize", "hardness", "spacing", "fill", "smoothing", "pressure", "antialias", "blend", "selectionQuality"];
    }

    brushDiameter() {
        return this.settings.brushSize;
    }

    size(e) {
        // Con lápiz, la presión ajusta el tamaño (si está activada en la barra y en Configuración).
        const pressure = e.pointerType === "pen" && this.settings.pressure !== false && Settings.get("penPressure") !== false;
        return pressure ? Math.max(1, this.settings.brushSize * e.pressure) : this.settings.brushSize;
    }

    strokeOptions(e) {
        return { mode: "paint", color: this.colorFor(e.button), blend: this.settings.blend, antialias: this.settings.antialias,
                 hardness: this.settings.hardness, spacing: this.settings.spacing };
    }

    onPointerDown(e) {
        if (!this.doc || (e.button !== "left" && e.button !== "right"))
            return;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, this.strokeOptions(e));
        this.smooth = { x: e.x, y: e.y };
        this.stroke.lineTo(e.x, e.y, this.size(e));
    }

    // Con el suavizado, el trazo sigue al puntero con algo de retraso y sale más redondo.
    smoothed(e) {
        if (!this.settings.smoothing || !this.smooth)
            return e;
        const k = 0.5;
        this.smooth = { x: this.smooth.x + (e.x - this.smooth.x) * k, y: this.smooth.y + (e.y - this.smooth.y) * k };
        return { ...e, ...this.smooth };
    }

    onPointerMove(e) {
        if (!this.stroke)
            return;
        const p = this.smoothed(e);
        this.stroke.lineTo(p.x, p.y, this.size(e));
    }

    onPointerUp(e) {
        if (!this.stroke)
            return;
        // El suavizado no deja el final del trazo por detrás del puntero.
        const stroke = this.stroke;
        this.stroke = null;
        if (e && this.settings.smoothing)
            stroke.lineTo(e.x, e.y, this.size(e));
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
    get options() { return ["brushSize", "hardness", "spacing", "smoothing", "pressure", "antialias", "selectionQuality"]; }
    hint() { return tr("Arrastra para borrar. La opacidad del color principal (clic derecho: secundario) regula la fuerza."); }

    strokeOptions(e) {
        return { ...super.strokeOptions(e), mode: "erase" };
    }
}

export class PencilTool extends Tool {
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
        if (!this.source || e.button !== "left")
            return;
        if (!this.offset)
            this.offset = { x: this.source.x - Math.floor(e.x), y: this.source.y - Math.floor(e.y) };
        const layer = this.doc.layers.includes(this.source.layer) ? this.source.layer : this.doc.activeLayer;
        this.stroke = new Stroke(this.doc, this.doc.activeLayer, {
            mode: "clone", color: AppState.primary, blend: this.settings.blend, antialias: this.settings.antialias, hardness: this.settings.hardness,
            spacing: this.settings.spacing, cloneOffset: this.offset, cloneSource: layer.canvas,
        });
        this.stroke.lineTo(e.x, e.y, this.size(e));
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
        return { mode: "recolor", color: this.colorFor(e.button), antialias: this.settings.antialias, hardness: this.settings.hardness,
                 recolorFrom: from, recolorTolerance: this.settings.tolerance, recolorStraight: !!this.settings.toleranceStraight };
    }
}
