// Ejecuta los efectos en segundo plano, como EffectPreview y applyEffectWithProgress de Kaint:
// los integrados reparten sus filas en franjas entre varios Web Workers (los hilos de Kaint) y
// se pueden cancelar en cualquier momento; los de los complementos se calculan aquí mismo.
//
// Los hilos se quedan con la imagen entre un cálculo y el siguiente: mientras se mueve un
// deslizador, cada vista previa nueva solo manda los valores (antes se copiaba la imagen entera a
// cada hilo y, al cancelar, se cerraban los hilos y había que crearlos y cargar los efectos otra
// vez: con un efecto lento, como Contorno, eso se notaba en cada cambio).

export const MAX_WORKERS = Math.max(1, Math.min(8, (typeof navigator !== "undefined" && navigator.hardwareConcurrency) || 4));
const idle = [];
let supported = typeof Worker !== "undefined";
let runs = 0;

function takeWorker() {
    return idle.pop() || new Worker(new URL("./EffectWorker.js", import.meta.url), { type: "module" });
}

function release(worker) {
    worker.onmessage = worker.onerror = null;
    if (idle.length < MAX_WORKERS) {
        idle.push(worker);
    } else {
        worker.terminate();
    }
}

// Cada imagen de origen tiene su número: un hilo que ya la tiene no la vuelve a recibir.
const sourceIds = new WeakMap();
let nextSource = 0;
function sourceId(src) {
    let id = sourceIds.get(src);
    if (!id)
        sourceIds.set(src, (id = ++nextSource));
    return id;
}

// Un rato después del último cálculo, los hilos sueltan la imagen (cada uno guarda una copia).
let forgetTimer = 0;
function scheduleForget() {
    clearTimeout(forgetTimer);
    forgetTimer = setTimeout(() => {
        for (const worker of idle) {
            worker.postMessage({ type: "forget" });
            worker.kaintSource = 0;
        }
    }, 20000);
}

// Calcula el efecto sobre 'src' (ImageData de la capa) dentro de 'rect'. Devuelve
// { promise, cancel() }: la promesa da el ImageData resultante (o null si se canceló).
// onProgress(fracción) avisa del avance.
export function startEffect(effect, src, values, rect, info, onProgress = null) {
    // Los que se calculan a su manera (los filtros de PhotoCraft, en su propio Worker).
    if (effect.start && rect.w > 0 && rect.h > 0)
        return effect.start(src, values, rect, info, onProgress);
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

    clearTimeout(forgetTimer);
    const run = ++runs, w = src.width, h = src.height, srcId = sourceId(src);
    const groups = effect.serial ? 1 : Math.min(MAX_WORKERS, Math.max(1, Math.ceil(rect.h / 16)));
    // Franjas pequeñas: un hilo deja un cálculo cancelado al terminar la franja que tiene entre manos.
    const count = effect.serial ? 1 : Math.min(rect.h, groups * 16);
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
            // Los que siguen con este cálculo lo dejan en la próxima franja y vuelven a la reserva
            // (con una sola franja no hay próxima: ese hilo se cierra).
            for (const worker of active) {
                if (count > groups) {
                    worker.postMessage({ type: "cancel", run });
                    release(worker);
                } else {
                    worker.terminate();
                }
            }
            active.clear();
            finish = null;
            scheduleForget();
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
                active.delete(worker);
                worker.terminate();
                if (!finish)
                    return;
                supported = false;
                try {
                    finish(effect.render(src, values, rect, info));
                } catch (err) {
                    finish(null, err);
                }
            };
            // La imagen, solo si ese hilo no la tiene ya.
            const message = { run, id: effect.id, srcId, width: w, height: h, values, rect, info: plain,
                              bands: bands.filter((_, i) => i % groups === g) };
            if (worker.kaintSource !== srcId) {
                message.pixels = src.data;
                worker.kaintSource = srcId;
            }
            worker.postMessage(message);
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
