// Hilo de trabajo de los efectos (EffectRunner.js): calcula las franjas de filas que le tocan de
// un efecto integrado y las devuelve una a una (así se ve el avance).
import { findEffect } from "./Effects.js";

self.onmessage = ({ data }) => {
    const { run, id, width, height, pixels, values, rect, info, bands } = data;
    try {
        const effect = findEffect(id);
        if (!effect)
            throw new Error(`Unknown effect: ${id}`);
        effect.renderBands(new ImageData(pixels, width, height), values, rect, info, bands, (y0, y1, out) => {
            self.postMessage({ run, y0, y1, pixels: out }, [out.buffer]);
        });
        self.postMessage({ run, done: true });
    } catch (e) {
        self.postMessage({ run, error: String(e?.message || e) });
    }
};
