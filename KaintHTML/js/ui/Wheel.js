// La rueda del ratón sobre los deslizadores (BarSlider::setWheelBehavior de Kaint): sube o baja su
// valor, o, si se desactiva en Configuración > Herramientas, deja que la rueda desplace la ventana.
// El paso: 0, el 1 % del recorrido; 1, una unidad; 2, el 5 %; 3, el 10 %. Con Mayús, de uno en uno;
// con Ctrl, diez veces más.
import { Settings } from "../Settings.js";

export function wheelEnabled() {
    return Settings.get("wheelChangesValues") !== false;
}

export function wheelStep(min, max, e) {
    const range = Math.max(1, max - min);
    if (e?.shiftKey)
        return 1;
    let step;
    switch (Number(Settings.get("wheelStep")) || 0) {
    case 1: step = 1; break;
    case 2: step = Math.max(1, Math.floor(range * 5 / 100)); break;
    case 3: step = Math.max(1, Math.floor(range / 10)); break;
    default: step = Math.max(1, Math.floor(range / 100)); break;
    }
    return e?.ctrlKey ? step * 10 : step;
}

// Cuántos pasos de rueda trae el evento (los touchpads mandan muchos trozos pequeños: se suman).
let rest = 0;
export function wheelSteps(e) {
    const delta = Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? -e.deltaY : e.deltaX;
    // Por líneas (Firefox) o por píxeles (100 por paso en Chrome).
    rest += e.deltaMode === 1 ? delta * 33.4 : delta;
    const steps = Math.trunc(rest / 100);
    rest -= steps * 100;
    return steps;
}

// Los <input type="range"> de los diálogos (efectos, propiedades...): con la rueda encima, su valor.
export function installRangeWheel() {
    document.addEventListener("wheel", (e) => {
        const range = e.target?.closest?.('input[type="range"]');
        if (!range || range.disabled || !wheelEnabled())
            return;
        e.preventDefault();
        const steps = wheelSteps(e);
        if (!steps)
            return;
        const min = Number(range.min) || 0, max = Number(range.max) || 100;
        const unit = Number(range.step) > 0 ? Number(range.step) : 1;
        const units = (max - min) / unit;
        const next = Math.min(max, Math.max(min, Number(range.value) + steps * wheelStep(0, units, e) * unit));
        if (next === Number(range.value))
            return;
        range.value = String(next);
        range.dispatchEvent(new Event("input", { bubbles: true }));
        range.dispatchEvent(new Event("change", { bubbles: true }));
    }, { passive: false, capture: true });
}
