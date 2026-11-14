// Hilo de trabajo de las herramientas de PhotoCraft (Runner.js): corre un trabajo y devuelve el
// resultado. Para cancelar, Runner.js termina el hilo.
import { NEVER } from "./Common.js";
import { runOp } from "./Ops.js";

self.onmessage = ({ data }) => {
    try {
        const result = runOp(data.op, data.args, NEVER);
        self.postMessage({ ok: true, result });
    } catch (err) {
        self.postMessage({ ok: false, error: String(err?.message || err) });
    }
};
