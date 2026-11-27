// Animación por puntos clave de un fotograma del editor de GIF (src/core/Motion.cpp de Kaint): la
// imagen de la capa se mueve, se escala, gira y cambia de opacidad a lo largo de su duración. Es el
// mismo JSON que en Kaint (en el .ora como «kaint-motion» y en el modo colaborativo):
//   { step, lockScale, tracks: { x|y|scaleX|scaleY|rotation|opacity: { base, keys: [{ t, v, i?, in?, out?, b? }] } } }
// 'i': 0 lineal, 1 curva, 2 curva continua, 3 congelar, 4 Bézier; 'in'/'out': entrada y salida suaves;
// 'b' (con 4): las dos asas del tramo [x1, y1, x2, y2], como cubic-bezier() de CSS.
import { context, createCanvas } from "./Raster.js";

// rotX y rotY: el giro en 3D (la imagen como una lámina fina, con perspectiva), arriba y abajo y a los
// lados. Además de los valores, cada fotograma puede llevar 'warp' (Deformar: lo que se mueve cada
// esquina, [x0, y0, ... x3, y3] desde arriba a la izquierda en el sentido del reloj) y 'smooth'
// (suavizada al girar o escalar; si no, con sus píxeles nítidos), como en Kaint.
export const Props = ["x", "y", "scaleX", "scaleY", "rotation", "opacity", "rotX", "rotY"];
export const Interp = { Linear: 0, Curve: 1, Continuous: 2, Freeze: 3, Bezier: 4 };
// La curva de un tramo Bézier por defecto (entrada y salida suaves, como «ease-in-out»).
export const DefaultBezier = [0.42, 0, 0.58, 1];

// Lo que avanza (0 al principio del tramo, 1 al final; puede pasarse) a la fracción 'u' del tiempo
// del tramo, con la curva cúbica de asas (x1, y1) y (x2, y2): como Motion::bezierProgress de Kaint.
export function bezierProgress(u, b = DefaultBezier) {
    u = Math.max(0, Math.min(1, u));
    const x1 = Math.max(0, Math.min(1, b[0])), y1 = b[1], x2 = Math.max(0, Math.min(1, b[2])), y2 = b[3];
    const curve = (s, p1, p2) => {
        const r = 1 - s;
        return 3 * r * r * s * p1 + 3 * r * s * s * p2 + s * s * s;
    };
    let s = u;
    for (let i = 0; i < 8; i++) {
        const x = curve(s, x1, x2) - u, r = 1 - s;
        const dx = 3 * r * r * x1 + 6 * r * s * (x2 - x1) + 3 * s * s * (1 - x2);
        if (Math.abs(x) < 1e-7)
            return curve(s, y1, y2);
        if (Math.abs(dx) < 1e-6)
            break;
        s = Math.max(0, Math.min(1, s - x / dx));
    }
    let lo = 0, hi = 1;
    s = u;
    for (let i = 0; i < 40; i++) {
        const x = curve(s, x1, x2);
        if (Math.abs(x - u) < 1e-7)
            break;
        if (x < u)
            lo = s;
        else
            hi = s;
        s = (lo + hi) / 2;
    }
    return curve(s, y1, y2);
}
const Defaults = { x: 0, y: 0, scaleX: 100, scaleY: 100, rotation: 0, opacity: 100, rotX: 0, rotY: 0 };

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
    if (hasWarp(clip?.warp))
        return false;
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
    if ((a.i || 0) === Interp.Bezier)
        return a.v + (b.v - a.v) * bezierProgress((time - a.t) / span, a.b || DefaultBezier);
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
    out.rotX = Math.max(-89, Math.min(89, out.rotX));
    out.rotY = Math.max(-89, Math.min(89, out.rotY));
    out.warp = hasWarp(clip?.warp) ? [...clip.warp] : null;
    out.smooth = !!clip?.smooth;
    return out;
}

export function hasWarp(warp) {
    return Array.isArray(warp) && warp.length === 8 && warp.some((v) => v);
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
    if (at > 0 && t.keys[at - 1].b)
        k.b = [...t.keys[at - 1].b];
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
            // (Bézier, con su curva: la de por defecto si aún no tiene.)
            if (interp === Interp.Bezier)
                clip.tracks[p].keys[i].b ||= [...DefaultBezier];
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
    return tr.x === 0 && tr.y === 0 && tr.scaleX === 100 && tr.scaleY === 100 && tr.rotation % 360 === 0 && tr.opacity >= 100
        && !tr.rotX && !tr.rotY && !hasWarp(tr.warp);
}

// --- Transformación (3×3, con perspectiva; por filas, p' = M·p, como QTransform de Kaint) ------

const mul = (a, b) => [
    a[0] * b[0] + a[1] * b[3] + a[2] * b[6], a[0] * b[1] + a[1] * b[4] + a[2] * b[7], a[0] * b[2] + a[1] * b[5] + a[2] * b[8],
    a[3] * b[0] + a[4] * b[3] + a[5] * b[6], a[3] * b[1] + a[4] * b[4] + a[5] * b[7], a[3] * b[2] + a[4] * b[5] + a[5] * b[8],
    a[6] * b[0] + a[7] * b[3] + a[8] * b[6], a[6] * b[1] + a[7] * b[4] + a[8] * b[7], a[6] * b[2] + a[7] * b[5] + a[8] * b[8],
];
const translation = (x, y) => [1, 0, x, 0, 1, y, 0, 0, 1];

export function applyMatrix(m, x, y) {
    const w = m[6] * x + m[7] * y + m[8];
    return [(m[0] * x + m[1] * y + m[2]) / w, (m[3] * x + m[4] * y + m[5]) / w];
}

export function invertMatrix(m) {
    const [a, b, c, d, e, f, g, h, i] = m;
    const A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, det = a * A + b * B + c * C;
    if (Math.abs(det) < 1e-12)
        return null;
    return [A / det, -(b * i - c * h) / det, (b * f - c * e) / det, B / det, (a * i - c * g) / det, -(a * f - c * d) / det,
            C / det, -(a * h - b * g) / det, (a * e - b * d) / det];
}

// Del cuadrado unidad al cuadrilátero q ([x0, y0, ... x3, y3]).
function squareToQuad(q) {
    const [x0, y0, x1, y1, x2, y2, x3, y3] = q;
    const sx = x0 - x1 + x2 - x3, sy = y0 - y1 + y2 - y3;
    if (Math.abs(sx) < 1e-12 && Math.abs(sy) < 1e-12)
        return [x1 - x0, x2 - x1, x0, y1 - y0, y2 - y1, y0, 0, 0, 1];
    const dx1 = x1 - x2, dx2 = x3 - x2, dy1 = y1 - y2, dy2 = y3 - y2, den = dx1 * dy2 - dx2 * dy1;
    const g = (sx * dy2 - dx2 * sy) / den, h = (dx1 * sy - sx * dy1) / den;
    return [x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h, 1];
}

// La inclinación en 3D de 'ax' grados (arriba y abajo) y 'ay' (a los lados) de un rectángulo w×h,
// alrededor de su centro y con la cámara a 1,5 veces su tamaño (Motion::tiltTransform de Kaint).
export function tiltMatrix(w, h, ax, ay) {
    if (!ax && !ay)
        return translation(0, 0);
    const d = 1.5 * Math.max(w, h, 1), a = (ax * Math.PI) / 180, b = (ay * Math.PI) / 180;
    const ry = [Math.cos(b), 0, 0, 0, 1, 0, -Math.sin(b) / d, 0, 1];
    const rx = [1, 0, 0, 0, Math.cos(a), 0, 0, -Math.sin(a) / d, 1];
    return mul(translation(w / 2, h / 2), mul(rx, mul(ry, translation(-w / 2, -h / 2))));
}

// Todo lo que se le hace a una imagen w×h (Motion::matrix de Kaint): deformar, el giro en 3D,
// escalar, girar y mover.
export function matrix(w, h, tr) {
    const r = ((tr.rotation || 0) * Math.PI) / 180, sx = (tr.scaleX ?? 100) / 100, sy = (tr.scaleY ?? 100) / 100;
    const affine = mul(translation(w / 2 + (tr.x || 0), h / 2 + (tr.y || 0)),
                       mul([Math.cos(r) * sx, -Math.sin(r) * sy, 0, Math.sin(r) * sx, Math.cos(r) * sy, 0, 0, 0, 1], translation(-w / 2, -h / 2)));
    let m = mul(affine, tiltMatrix(w, h, tr.rotX || 0, tr.rotY || 0));
    if (hasWarp(tr.warp)) {
        const o = tr.warp;
        const quad = squareToQuad([o[0], o[1], w + o[2], o[3], w + o[4], h + o[5], o[6], h + o[7]]);
        m = mul(m, mul(quad, [1 / w, 0, 0, 0, 1 / h, 0, 0, 0, 1]));
    }
    return m;
}

// Dibuja 'source' con la transformación 'tr' (y la que ya tenga 'ctx'). Con perspectiva (giro 3D o
// Deformar), por triángulos pequeños (el lienzo 2D solo hace transformaciones afines). 'smooth':
// suavizado aunque la transformación no lo pida (al reducir la vista previa).
export function drawTransformed(ctx, source, tr, smooth = false) {
    const w = source.width, h = source.height, m = matrix(w, h, tr);
    ctx.save();
    ctx.imageSmoothingEnabled = smooth || !!tr.smooth;
    ctx.imageSmoothingQuality = "high";
    if (Math.abs(m[6]) < 1e-12 && Math.abs(m[7]) < 1e-12) {
        ctx.transform(m[0] / m[8], m[3] / m[8], m[1] / m[8], m[4] / m[8], m[2] / m[8], m[5] / m[8]);
        ctx.drawImage(source, 0, 0);
        ctx.restore();
        return;
    }
    const n = 16, cw = w / n, ch = h / n;
    const tri = (s0, s1, s2) => {
        const d0 = applyMatrix(m, ...s0), d1 = applyMatrix(m, ...s1), d2 = applyMatrix(m, ...s2);
        // El triángulo de destino, un poco más grande (así no quedan rendijas entre ellos).
        const cx = (d0[0] + d1[0] + d2[0]) / 3, cy = (d0[1] + d1[1] + d2[1]) / 3;
        const grow = (p) => {
            const dx = p[0] - cx, dy = p[1] - cy, len = Math.hypot(dx, dy) || 1;
            return [p[0] + (dx / len) * 1.0, p[1] + (dy / len) * 1.0];
        };
        ctx.save();
        ctx.beginPath();
        const [g0, g1, g2] = [grow(d0), grow(d1), grow(d2)];
        ctx.moveTo(g0[0], g0[1]);
        ctx.lineTo(g1[0], g1[1]);
        ctx.lineTo(g2[0], g2[1]);
        ctx.closePath();
        ctx.clip();
        // La afín que lleva el triángulo de origen al de destino.
        const [x0, y0] = s0, [x1, y1] = s1, [x2, y2] = s2;
        const den = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (Math.abs(den) > 1e-12) {
            const a = ((d1[0] - d0[0]) * (y2 - y0) - (d2[0] - d0[0]) * (y1 - y0)) / den;
            const c = ((d2[0] - d0[0]) * (x1 - x0) - (d1[0] - d0[0]) * (x2 - x0)) / den;
            const b = ((d1[1] - d0[1]) * (y2 - y0) - (d2[1] - d0[1]) * (y1 - y0)) / den;
            const d = ((d2[1] - d0[1]) * (x1 - x0) - (d1[1] - d0[1]) * (x2 - x0)) / den;
            ctx.transform(a, b, c, d, d0[0] - a * x0 - c * y0, d0[1] - b * x0 - d * y0);
            const sx = Math.max(0, Math.floor(Math.min(x0, x1, x2)) - 1), sy = Math.max(0, Math.floor(Math.min(y0, y1, y2)) - 1);
            const ex = Math.min(w, Math.ceil(Math.max(x0, x1, x2)) + 1), ey = Math.min(h, Math.ceil(Math.max(y0, y1, y2)) + 1);
            if (ex > sx && ey > sy)
                ctx.drawImage(source, sx, sy, ex - sx, ey - sy, sx, sy, ex - sx, ey - sy);
        }
        ctx.restore();
    };
    for (let j = 0; j < n; j++)
        for (let i = 0; i < n; i++) {
            const p00 = [i * cw, j * ch], p10 = [(i + 1) * cw, j * ch], p11 = [(i + 1) * cw, (j + 1) * ch], p01 = [i * cw, (j + 1) * ch];
            tri(p00, p10, p11);
            tri(p00, p11, p01);
        }
    ctx.restore();
}

// 'source' (un lienzo) con la transformación, en un lienzo transparente del mismo tamaño.
export function render(source, tr) {
    const w = source.width, h = source.height, out = createCanvas(w, h), ctx = context(out);
    // (Suavizada solo si se pide: si no, al girar o escalar el pixel art se veía borroso.)
    ctx.globalAlpha = Math.max(0, Math.min(100, tr.opacity)) / 100;
    drawTransformed(ctx, source, tr);
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
