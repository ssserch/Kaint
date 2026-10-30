// Hilo de trabajo de "Configuración de guardado": codifica con las opciones elegidas y vuelve a
// leer el resultado para la vista previa (EncodedPreview de Kaint).
import { decodeForPreview, encodeJob } from "./Encode.js";

self.onmessage = async ({ data }) => {
    const { id, job } = data;
    try {
        const bytes = await encodeJob(job);
        const frames = await decodeForPreview(bytes, job.ext);
        self.postMessage({ id, size: bytes.length, frames }, frames.map((f) => f.bitmap || f.data.buffer));
    } catch (e) {
        self.postMessage({ id, error: String(e?.message || e) });
    }
};
