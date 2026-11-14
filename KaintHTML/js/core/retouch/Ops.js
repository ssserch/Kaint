// Los trabajos de las herramientas de PhotoCraft (los corre Worker.js en segundo plano o, sin Web
// Workers, Runner.js aquí mismo). Cada uno recibe datos sueltos (bytes RGBA, máscaras...) y
// devuelve el resultado; 'stop' lo cancela.
import { box, closeLoops, dilate, heal, toBytes, toFloats } from "./Common.js";
import { patchLand, spotHeal } from "./Heal.js";
import { removeFill } from "./NonLocal.js";
import { objectAt, objectInRect, selectSubject } from "./Segment.js";

const REMOVE_SEED = 0x0052e30e;

// Lee RGBA (0..1) de la imagen entera 'pixels' (bytes RGBA de width × height).
function samplerOf(pixels, width, height) {
    return (r) => {
        const w = r.x1 - r.x0, h = r.y1 - r.y0, out = new Float32Array(w * h * 4);
        for (let y = Math.max(r.y0, 0); y < Math.min(r.y1, height); ++y)
            for (let x = Math.max(r.x0, 0); x < Math.min(r.x1, width); ++x) {
                const s = (y * width + x) * 4, o = ((y - r.y0) * w + (x - r.x0)) * 4;
                out[o] = pixels[s] / 255;
                out[o + 1] = pixels[s + 1] / 255;
                out[o + 2] = pixels[s + 2] / 255;
                out[o + 3] = pixels[s + 3] / 255;
            }
        return out;
    };
}

export function runOp(op, a, stop) {
    switch (op) {
    case "heal":
        return heal(a.w, a.h, a.src, a.dst, a.cov, stop);
    case "spotHeal":
        return spotHeal(a.w, a.h, a.img, a.cov, a.kind, a.size, stop);
    case "remove": {
        // El hueco: los trazos, lo que encierran y 2 píxeles más, dentro de la selección.
        const { w, h } = a;
        let hole = new Uint8Array(a.strokes.length);
        for (let i = 0; i < hole.length; ++i)
            hole[i] = a.strokes[i] > 0 ? 1 : 0;
        closeLoops(hole, w, h);
        hole = dilate(hole, w, h, 2);
        if (a.clip)
            for (let i = 0; i < hole.length; ++i)
                hole[i] = hole[i] && a.clip[i] > 0 ? 1 : 0;
        let any = false, all = true;
        for (const v of hole) {
            any = any || !!v;
            all = all && !!v;
        }
        if (!any || all)
            return null;
        // Una capa transparente no tiene nada que quitar ni de dónde rellenar.
        let opaque = a.image;
        for (let i = 3; i < a.img.length && !opaque; i += 4)
            opaque = a.img[i] !== 0;
        if (!opaque)
            return null;
        const filled = removeFill(w, h, 4, toFloats(a.img), hole, REMOVE_SEED, stop);
        return { hole, pixels: toBytes(filled) };
    }
    case "patch": {
        if (a.mode === "contentAware") {
            const selected = new Uint8Array(a.coverage.length);
            for (let i = 0; i < selected.length; ++i)
                selected[i] = a.coverage[i] > 0 ? 1 : 0;
            return toBytes(patchLand(a.w, a.h, toFloats(a.here), toFloats(a.texture), selected, a.structure, a.color, stop));
        }
        return heal(a.w, a.h, a.texture, a.here, a.coverage, stop);
    }
    case "objectInRect":
        return objectInRect(samplerOf(a.pixels, a.width, a.height), box(a.rect[0], a.rect[1], a.rect[2], a.rect[3]),
                            box(0, 0, a.width, a.height), a.workPx, stop);
    case "objectAt":
        return objectAt(samplerOf(a.pixels, a.width, a.height), box(0, 0, a.width, a.height), a.x, a.y, a.workPx, stop);
    case "selectSubject":
        return selectSubject(samplerOf(a.pixels, a.width, a.height), box(0, 0, a.width, a.height), a.workPx, stop);
    default:
        throw new Error(`Trabajo desconocido: ${op}`);
    }
}
