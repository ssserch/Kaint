// Pasos editables del historial (src/app/tools/EditableSteps.cpp de Kaint): el primero al crear
// ("Línea/Curva"), uno por cada cambio terminado ("Editar…") y "Finalizar…" al acabar. Mientras uno
// de esos pasos sea el último del historial, su herramienta lo puede retomar (al deshacer o al
// pulsar en él en el Historial) para seguir editándolo.
import { clampRect, cloneCanvas, context, unionRect } from "../core/Raster.js";

export class StepRecorder {
    constructor(tool) {
        this.tool = tool;
        this.pushing = false;
        this.leave();
    }

    leave() {
        this.steps = 0;
        this.dirty = false;
        this.rect = null;
        this.last = null;
        this.liveBefore = null;
    }

    start() {
        this.leave();
    }

    changed() {
        this.dirty = true;
    }

    // Lleva a la capa lo que se ve en 'preview' (en 'rect' y lo que tocó el paso anterior) como un
    // paso nuevo; con 'live', el paso en vivo (texto) se actualiza en vez de añadir otro.
    commit({ layer, preview, rect, base, state, createText, editText, icon, live = false }) {
        if (!this.dirty)
            return false;
        this.dirty = false;
        const doc = this.tool.doc;
        const replace = live && this.last && this.top() === this.last;
        const r = clampRect(unionRect(this.rect, rect), doc.width, doc.height);
        if (!r)
            return false;
        if (live && !replace)
            this.liveBefore = cloneCanvas(layer.canvas);
        const before = replace ? context(this.liveBefore).getImageData(r.x, r.y, r.w, r.h) : layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        layer.ctx.clearRect(r.x, r.y, r.w, r.h);
        layer.ctx.drawImage(preview, r.x, r.y, r.w, r.h, r.x, r.y, r.w, r.h);
        const after = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const apply = (data) => {
            layer.ctx.putImageData(data, r.x, r.y);
            doc.pixelsChanged(layer, r);
        };
        doc.pixelsChanged(layer, r);
        this.rect = r;
        if (replace) {
            Object.assign(this.last, { rect: r, state, undo: () => apply(before), redo: () => apply(after) });
            this.pushing = true;
            doc.emit("history");
            this.pushing = false;
            return true;
        }
        const step = { name: this.steps ? editText : createText, icon, tool: this.tool.id, layer, base, rect: r, state,
                       undo: () => apply(before), redo: () => apply(after) };
        this.pushing = true;
        doc.push(step);
        this.pushing = false;
        ++this.steps;
        this.last = step;
        return true;
    }

    // "Finalizar…" si hubo pasos; devuelve false si no los hubo (lo dibujado se descarta).
    finish(text, icon) {
        const had = this.steps > 0;
        if (had) {
            this.pushing = true;
            this.tool.doc.push({ name: text, icon, undo() {}, redo() {} });
            this.pushing = false;
        }
        this.leave();
        return had;
    }

    // Paso de esta herramienta en lo alto del historial (para retomarlo), o null.
    top() {
        const doc = this.tool.doc;
        const step = doc?.history.steps[doc.history.index];
        return step && step.tool === this.tool.id && step.base ? step : null;
    }

    resume(step) {
        this.steps = 1;
        this.dirty = false;
        this.rect = step.rect;
        this.last = step;
        this.liveBefore = null;
    }
}

// Icono de mover de los tiradores (Handles::Shape::Move de Kaint): un cuadrado blanco con la cruz
// de cuatro flechas, en el punto de la vista 'v'.
export function drawMoveHandle(ctx, v) {
    ctx.save();
    ctx.translate(Math.round(v.x) + 0.5, Math.round(v.y) + 0.5);
    ctx.fillStyle = "#fff";
    ctx.strokeStyle = "#000";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.rect(-7, -7, 14, 14);
    ctx.fill();
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(-4.5, 0); ctx.lineTo(4.5, 0);
    ctx.moveTo(0, -4.5); ctx.lineTo(0, 4.5);
    ctx.stroke();
    ctx.fillStyle = "#000";
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        ctx.beginPath();
        ctx.moveTo(dx * 5.5, dy * 5.5);
        ctx.lineTo(dx * 3 - dy * 2, dy * 3 - dx * 2);
        ctx.lineTo(dx * 3 + dy * 2, dy * 3 + dx * 2);
        ctx.closePath();
        ctx.fill();
    }
    ctx.restore();
}

export function nearPoint(e, v, radius = 8) {
    return Math.hypot(v.x - e.vx, v.y - e.vy) <= radius;
}
