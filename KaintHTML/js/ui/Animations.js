// Animaciones (src/app/Animations.cpp de Kaint): las mismas duraciones de paint.js y un único
// interruptor en Configuración > Animaciones (la clase "no-animations" apaga las de CSS).
import { Settings } from "../Settings.js";

export const kListMove = 140;
export const kIndicator = 200;

export function enabled() {
    return !!Settings.get("animations");
}

export function applySetting() {
    document.documentElement.classList.toggle("no-animations", !enabled());
}

// cubic-bezier(.2, .8, .2, 1), la curva de las filas que se mueven en paint.js.
export function listMoveEasing(t) {
    // Resolución de la curva de Bézier por bisección (x → y).
    const bez = (p1, p2, u) => 3 * (1 - u) * (1 - u) * u * p1 + 3 * (1 - u) * u * u * p2 + u * u * u;
    let lo = 0, hi = 1;
    for (let i = 0; i < 20; ++i) {
        const mid = (lo + hi) / 2;
        bez(0.2, 0.2, mid) < t ? (lo = mid) : (hi = mid);
    }
    return bez(0.8, 1, (lo + hi) / 2);
}

export const outCubic = (t) => 1 - (1 - t) ** 3;

// Anima de 'from' a 'to' llamando a step(valor); sin animaciones, directamente al final.
export function run(from, to, ms, easing, step, done) {
    if (!enabled() || ms <= 0) {
        step(to);
        done?.();
        return { stop() {} };
    }
    const start = performance.now();
    let stopped = false;
    const frame = (now) => {
        if (stopped)
            return;
        const t = Math.min(1, (now - start) / ms);
        step(from + (to - from) * easing(t));
        if (t < 1)
            requestAnimationFrame(frame);
        else
            done?.();
    };
    step(from);
    requestAnimationFrame(frame);
    return { stop() { stopped = true; } };
}

// Las filas de una lista que cambian de sitio se deslizan desde donde estaban (como la lista de
// capas de paint.js): before es un Map(clave → posición vertical anterior).
export function slideRows(rows, before) {
    if (!enabled() || !before.size)
        return;
    for (const row of rows) {
        const old = before.get(row.dataset.key);
        if (old === undefined)
            continue;
        const diff = old - row.offsetTop;
        if (Math.abs(diff) <= 1)
            continue;
        row.animate([{ transform: `translateY(${diff}px)` }, { transform: "translateY(0)" }],
                    { duration: kListMove, easing: "cubic-bezier(.2,.8,.2,1)" });
    }
}
