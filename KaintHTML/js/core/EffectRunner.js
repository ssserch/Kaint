// Ejecuta los efectos en segundo plano, como EffectPreview y applyEffectWithProgress de Kaint:
// los integrados reparten sus filas en franjas entre varios Web Workers (los hilos de Kaint) y
// se pueden cancelar en cualquier momento; los de los complementos se calculan aquí mismo.

export const MAX_WORKERS = Math.max(1, Math.min(8, (typeof navigator !== "undefined" && navigator.hardwareConcurrency) || 4));
const idle = [];
let supported = typeof Worker !== "undefined";
let runs = 0;

function takeWorker() {
    return idle.pop() || new Worker(new URL("./EffectWorker.js", import.meta.url), { type: "module" });
}

function release(worker) {
    worker.onmessage = worker.onerror = null;
    if (idle.length < MAX_WORKERS)
        idle.push(worker);
    else
        worker.terminate();
}

// Calcula el efecto sobre 'src' (ImageData de la capa) dentro de 'rect'. Devuelve
// { promise, cancel() }: la promesa da el ImageData resultante (o null si se canceló).
// onProgress(fracción) avisa del avance.
export function startEffect(effect, src, values, rect, info, onProgress = null) {
    let cancelled = false;
    if (!effect.builtin || !supported || rect.w <= 0 || rect.h <= 0) {
        const promise = new Promise((resolve, reject) => setTimeout(() => {
            if (cancelled)
                return resolve(null);
            try {
                resolve(effect.render(src, values, rect, info));
            } catch (e) {
                reject(e);
            }
        }, 0));
        return { promise, cancel: () => (cancelled = true) };
    }

    const run = ++runs, w = src.width, h = src.height;
    const groups = effect.serial ? 1 : Math.min(MAX_WORKERS, Math.max(1, Math.ceil(rect.h / 16)));
    const count = effect.serial ? 1 : Math.min(rect.h, groups * 4);
    const bands = [];
    for (let i = 0; i < count; ++i)
        bands.push([rect.y + Math.floor((i * rect.h) / count), rect.y + Math.floor(((i + 1) * rect.h) / count) - 1]);
    const active = new Set();
    const result = new Uint8ClampedArray(src.data);
    const plain = { primary: info?.primary, secondary: info?.secondary };
    let finish = null;
    const promise = new Promise((resolve, reject) => {
        let done = 0, finished = 0;
        finish = (value, error) => {
            for (const worker of active)
                worker.terminate();
            active.clear();
            finish = null;
            error ? reject(error) : resolve(value);
        };
        for (let g = 0; g < groups; ++g) {
            let worker;
            try {
                worker = takeWorker();
            } catch {
                supported = false;
                return finish(effect.render(src, values, rect, info));
            }
            active.add(worker);
            worker.onmessage = ({ data }) => {
                if (data.run !== run || !finish)
                    return;
                if (data.error)
                    return finish(null, new Error(data.error));
                if (data.done) {
                    active.delete(worker);
                    release(worker);
                    if (++finished === groups)
                        finish(new ImageData(result, w, h));
                    return;
                }
                result.set(data.pixels, data.y0 * w * 4);
                onProgress?.(++done / count);
            };
            // Sin módulos en los Web Workers (navegadores antiguos): se calcula aquí.
            worker.onerror = (e) => {
                e.preventDefault?.();
                if (!finish)
                    return;
                supported = false;
                try {
                    finish(effect.render(src, values, rect, info));
                } catch (err) {
                    finish(null, err);
                }
            };
            worker.postMessage({ run, id: effect.id, width: w, height: h, pixels: src.data, values, rect, info: plain,
                                 bands: bands.filter((_, i) => i % groups === g) });
        }
    });
    return {
        promise,
        cancel() {
            cancelled = true;
            finish?.(null);
        },
    };
}
