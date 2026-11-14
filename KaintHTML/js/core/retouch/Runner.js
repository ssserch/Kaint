// Corre un trabajo de las herramientas de PhotoCraft en segundo plano (un Web Worker por trabajo,
// como RetouchJob de Kaint): runRetouch(op, args) → { promise, cancel() }. La promesa da el
// resultado o null si se canceló; cancelar termina el hilo. Sin Web Workers, aquí mismo (la
// interfaz se para mientras tanto).
import { Cancelled, Stop } from "./Common.js";
import { runOp } from "./Ops.js";

let workersWork = typeof Worker !== "undefined";

export function runRetouch(op, args) {
    let cancelled = false, worker = null, finish = null;
    const promise = new Promise((resolve, reject) => {
        finish = resolve;
        const local = () => setTimeout(() => {
            if (cancelled)
                return resolve(null);
            try {
                resolve(runOp(op, args, new Stop(() => cancelled)));
            } catch (err) {
                if (err instanceof Cancelled)
                    resolve(null);
                else
                    reject(err);
            }
        }, 0);
        if (!workersWork)
            return local();
        try {
            worker = new Worker(new URL("./Worker.js", import.meta.url), { type: "module" });
        } catch {
            workersWork = false;
            return local();
        }
        worker.onmessage = ({ data }) => {
            worker.terminate();
            worker = null;
            if (cancelled)
                return resolve(null);
            if (data.ok)
                resolve(data.result);
            else
                reject(new Error(data.error));
        };
        worker.onerror = (e) => {
            // Sin Workers de módulo (algunos navegadores): aquí mismo.
            worker?.terminate();
            worker = null;
            e.preventDefault?.();
            workersWork = false;
            local();
        };
        worker.postMessage({ op, args });
    });
    return {
        promise,
        cancel() {
            cancelled = true;
            if (worker) {
                worker.terminate();
                worker = null;
                finish(null);
            }
        },
    };
}
