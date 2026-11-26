// Animación por puntos clave de un fotograma del editor de GIF (src/core/Motion.cpp de Kaint): la
// imagen de la capa se mueve, se escala, gira y cambia de opacidad a lo largo de su duración. Es el
// mismo JSON que en Kaint (en el .ora como «kaint-motion» y en el modo colaborativo):
//   { step, lockScale, tracks: { x|y|scaleX|scaleY|rotation|opacity: { base, keys: [{ t, v, i?, in?, out? }] } } }
// 'i': 0 lineal, 1 curva, 2 curva continua, 3 congelar; 'in'/'out': entrada y salida suaves.
import { context, createCanvas } from "./Raster.js";

export const Props = ["x", "y", "scaleX", "scaleY", "rotation", "opacity"];
export const Interp = { Linear: 0, Curve: 1, Continuous: 2, Freeze: 3 };
const Defaults = { x: 0, y: 0, scaleX: 100, scaleY: 100, rotation: 0, opacity: 100 };

export const defaultValue = (prop) => Defaults[prop];

export function emptyClip() {
    return { step: 50, lockScale: true, tracks: {} };
}

// Una copia que se puede cambiar (las capas guardan su animación sin tocarla: así entra en el historial).
export function cloneClip(clip) {
    const c = clip ? JSON.parse(JSON.stringify(clip)) : emptyClip();
    c.tracks ||= {};
    c.step ??= 50;
    c.lockScale ??= true;
    return c;
}

export function track(clip, prop) {
    return clip?.tracks?.[prop] || { base: Defaults[prop], keys: [] };
}

// La pista de 'prop' de una copia (creándola si falta).
export function editTrack(clip, prop) {
    clip.tracks[prop] ||= { base: Defaults[prop], keys: [] };
    clip.tracks[prop].keys ||= [];
    return clip.tracks[prop];
}

export function isEmpty(clip) {
    if (!clip)
        return true;
    return Props.every((p) => {
        const t = clip.tracks?.[p];
        return !t || (!(t.keys?.length) && (t.base ?? Defaults[p]) === Defaults[p]);
    });
}

export function isAnimated(clip) {
    return !!clip && Props.some((p) => (clip.tracks?.[p]?.keys?.length || 0) > 1);
}

function eased(u, easeOut, easeIn) {
    if (easeOut && easeIn)
        return u * u * (3 - 2 * u);
    if (easeOut)
        return u * u;
    if (easeIn)
        return 1 - (1 - u) * (1 - u);
    return u;
}

function slope(keys, i) {
    if (i <= 0 || i + 1 >= keys.length)
        return 0;
    const dt = keys[i + 1].t - keys[i - 1].t;
    return dt <= 0 ? 0 : (keys[i + 1].v - keys[i - 1].v) / dt;
}

export function trackValue(t, time, prop) {
    const keys = t?.keys || [];
    if (!keys.length)
        return t?.base ?? Defaults[prop];
    if (time <= keys[0].t)
        return keys[0].v;
    if (time >= keys[keys.length - 1].t)
        return keys[keys.length - 1].v;
    let i = 0;
    while (i + 1 < keys.length && keys[i + 1].t <= time)
        i++;
    const a = keys[i], b = keys[i + 1], span = Math.max(1, b.t - a.t);
    const u = eased((time - a.t) / span, !!a.out, !!b.in);
    switch (a.i || 0) {
    case Interp.Freeze: return a.v;
    case Interp.Curve: return a.v + (b.v - a.v) * (u * u * (3 - 2 * u));
    case Interp.Continuous: {
        const m0 = slope(keys, i) * span, m1 = slope(keys, i + 1) * span, u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * a.v + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * b.v + (u3 - u2) * m1;
    }
    default: return a.v + (b.v - a.v) * u;
    }
}

export function sample(clip, time) {
    const out = {};
    for (const p of Props)
        out[p] = trackValue(track(clip, p), time, p);
    out.opacity = Math.max(0, Math.min(100, out.opacity));
    return out;
}

export function keyIndex(t, time) {
    return (t?.keys || []).findIndex((k) => k.t === time);
}

export function setKey(t, time, value) {
    const i = keyIndex(t, time);
    if (i >= 0) {
        t.keys[i].v = value;
        return;
    }
    let at = t.keys.findIndex((k) => k.t > time);
    if (at < 0)
        at = t.keys.length;
    const k = { t: time, v: value };
    // Con la forma del punto anterior (la que se eligió para este tramo).
    if (at > 0 && t.keys[at - 1].i)
        k.i = t.keys[at - 1].i;
    t.keys.splice(at, 0, k);
}

export function keyTimes(clip) {
    const times = new Set();
    for (const p of Props)
        for (const k of clip?.tracks?.[p]?.keys || [])
            times.add(k.t);
    return [...times].sort((a, b) => a - b);
}

export function moveKeys(clip, from, to) {
    if (from === to)
        return;
    for (const p of Props) {
        const t = clip.tracks[p];
        if (!t?.keys)
            continue;
        const i = keyIndex(t, from);
        if (i < 0)
            continue;
        const [k] = t.keys.splice(i, 1);
        const existing = keyIndex(t, to);
        if (existing >= 0)
            t.keys.splice(existing, 1);
        k.t = to;
        let at = t.keys.findIndex((x) => x.t > to);
        if (at < 0)
            at = t.keys.length;
        t.keys.splice(at, 0, k);
    }
}

// Los puntos clave de un fotograma cuyo principio se mueve 'shift' ms (positivo: empieza antes, al
// estirarlo por la izquierda): siguen a la imagen. Lo que queda antes del principio nuevo se quita y
// en el principio queda un punto con el valor que tenía ahí (como shiftedMotion de Kaint).
export function shiftKeys(clip, shift) {
    if (!clip || !shift)
        return;
    for (const p of Props) {
        const t = clip.tracks?.[p];
        if (!t?.keys?.length)
            continue;
        if (shift > 0) {
            for (const k of t.keys)
                k.t += shift;
            continue;
        }
        const cut = -shift, before = t.keys.filter((k) => k.t < cut);
        const keys = t.keys.filter((k) => k.t >= cut).map((k) => ({ ...k, t: k.t - cut }));
        if (before.length && (!keys.length || keys[0].t !== 0))
            keys.unshift({ ...before[before.length - 1], t: 0, v: trackValue(t, cut, p) });
        t.keys = keys;
    }
}

export function removeKeys(clip, time) {
    for (const p of Props) {
        const t = clip.tracks[p];
        if (!t?.keys)
            continue;
        const i = keyIndex(t, time);
        if (i < 0)
            continue;
        // Sin puntos, el valor que tenía se queda como el de siempre.
        if (t.keys.length === 1)
            t.base = t.keys[i].v;
        t.keys.splice(i, 1);
    }
}

export function setInterp(clip, time, interp) {
    for (const p of Props) {
        const i = keyIndex(clip.tracks[p], time);
        if (i >= 0) {
            if (interp)
                clip.tracks[p].keys[i].i = interp;
            else
                delete clip.tracks[p].keys[i].i;
        }
    }
}

export function setEase(clip, time, which, on) {
    for (const p of Props) {
        const i = keyIndex(clip.tracks[p], time);
        if (i >= 0) {
            if (on)
                clip.tracks[p].keys[i][which] = true;
            else
                delete clip.tracks[p].keys[i][which];
        }
    }
}

// La forma y las entradas del primer punto que haya en 'time'.
export function keyAt(clip, time) {
    for (const p of Props) {
        const i = keyIndex(clip?.tracks?.[p], time);
        if (i >= 0)
            return clip.tracks[p].keys[i];
    }
    return null;
}

export function isIdentity(tr) {
    return tr.x === 0 && tr.y === 0 && tr.scaleX === 100 && tr.scaleY === 100 && tr.rotation % 360 === 0 && tr.opacity >= 100;
}

// 'source' (un lienzo) con la transformación, en un lienzo transparente del mismo tamaño.
export function render(source, tr) {
    const w = source.width, h = source.height, out = createCanvas(w, h), ctx = context(out);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.globalAlpha = Math.max(0, Math.min(100, tr.opacity)) / 100;
    ctx.translate(w / 2 + tr.x, h / 2 + tr.y);
    ctx.rotate((tr.rotation * Math.PI) / 180);
    ctx.scale(tr.scaleX / 100, tr.scaleY / 100);
    ctx.translate(-w / 2, -h / 2);
    ctx.drawImage(source, 0, 0);
    return out;
}

// Los fotogramas de un fotograma animado: uno cada 'step' ms hasta 'duration' (el último, lo que
// quede). [{ canvas, delay }].
export function expand(source, clip, duration) {
    const step = Math.max(10, clip?.step || 50);
    if (!isAnimated(clip))
        return [{ canvas: render(source, sample(clip, 0)), delay: duration }];
    const out = [];
    for (let t = 0; t < duration; t += step) {
        const length = Math.min(step, duration - t);
        // Lo que sobre al final, menos de 20 ms (el mínimo de los GIF), se suma al anterior.
        if (length < 20 && out.length) {
            out[out.length - 1].delay += length;
            break;
        }
        out.push({ canvas: render(source, sample(clip, t)), delay: length });
    }
    return out;
}
