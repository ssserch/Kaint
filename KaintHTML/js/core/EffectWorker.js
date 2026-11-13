// Hilo de trabajo de los efectos (EffectRunner.js): calcula las franjas de filas que le tocan de
// un efecto integrado y las devuelve una a una (así se ve el avance).
//
// La imagen se queda aquí entre un cálculo y el siguiente (con su copia premultiplicada): mientras
// se mueve un deslizador, cada vista previa nueva solo manda los valores. Cancelar no termina el
// hilo: entre franja y franja se mira si llegó un «cancel» y se deja ese cálculo.
import { findEffect } from "./Effects.js";

let source = null; // { id, image, cache }
const cancelled = new Set();
// Vuelve al bucle de eventos (para recibir los «cancel») sin la espera mínima de setTimeout.
const channel = new MessageChannel();
const waiting = [];
channel.port1.onmessage = () => waiting.shift()?.();
const yieldToEvents = () => new Promise((resolve) => {
    waiting.push(resolve);
    channel.port2.postMessage(0);
});

self.onmessage = async ({ data }) => {
    if (data.type === "cancel") {
        cancelled.add(data.run);
        return;
    }
    if (data.type === "forget") {
        source = null;
        return;
    }
    const { run, id, srcId, width, height, pixels, values, rect, info, bands } = data;
    try {
        const effect = findEffect(id);
        if (!effect)
            throw new Error(`Unknown effect: ${id}`);
        if (pixels)
            source = { id: srcId, image: new ImageData(pixels, width, height), cache: {} };
        if (!source || source.id !== srcId)
            throw new Error("Effect worker: missing source image");
        if (cancelled.has(run))
            return;
        const band = effect.beginBands(source.image, values, rect, info, source.cache);
        for (const [y0, y1] of bands) {
            if (cancelled.has(run))
                return;
            const out = band(y0, y1);
            self.postMessage({ run, y0, y1, pixels: out }, [out.buffer]);
            await yieldToEvents();
        }
        if (!cancelled.has(run))
            self.postMessage({ run, done: true });
    } catch (e) {
        self.postMessage({ run, error: String(e?.message || e) });
    } finally {
        cancelled.delete(run);
    }
};
