// Retoque de PhotoCraft (como core/retouch/Heal.cpp de Kaint): el Pincel corrector puntual y el
// Parche. Algoritmos de PhotoCraft (licencia MIT).
import { MAX_PIXELS, NEVER, boxArea, boxEmpty, boxEquals, boxInflate, boxIntersect, boxTranslate, dilate, heal, membraneFill,
         removeWindow, samplingWindow, toBytes, toFloats } from "./Common.js";
import { bestOffset, colorAmount, colorLevel, complete, contentAwareFill, levelTransform, synthesize } from "./Inpaint.js";

// La ventana del Pincel corrector puntual (la de Relleno según el contenido o, si es demasiado
// grande, la de Quitar).
export function spotWindow(stroke, layer) {
    let win = samplingWindow(stroke, layer);
    if (boxArea(win) > MAX_PIXELS)
        win = removeWindow(stroke, layer);
    return win;
}

function proximityPatch(img, w, h, ch, mask, dx, dy) {
    const out = new Float32Array(img);
    for (let p = 0; p < mask.length; ++p) {
        if (!mask[p])
            continue;
        const x = (p % w) + dx, y = ((p / w) | 0) + dy;
        if (x < 0 || y < 0 || x >= w || y >= h)
            return null;
        const src = (y * w + x) * ch;
        for (let c = 0; c < ch; ++c)
            out[p * ch + c] = img[src + c];
    }
    return out;
}

// Rellena la ventana 'img' (bytes RGBA, w × h) donde la cobertura 'cov' no es 0 según el tipo
// ("contentAware", "createTexture" o "proximityMatch") y lo funde con lo de alrededor.
export function spotHeal(w, h, imgBytes, cov, kind, brushSize, stop = NEVER) {
    const img = toFloats(imgBytes);
    const hole = new Uint8Array(cov.length);
    for (let i = 0; i < cov.length; ++i)
        hole[i] = cov[i] > 0 ? 1 : 0;
    const domain = dilate(hole, w, h, 2);
    const contentAware = () => complete(w, h, 4, img, domain, undefined, stop) || membraneFill(w, h, 4, img, domain, stop);
    let filled;
    if (kind === "createTexture") {
        const block = Math.trunc(Math.min(Math.max(brushSize / 4, 6), 32));
        filled = synthesize(w, h, 4, img, domain, block, 0x5eed, stop);
    } else if (kind === "proximityMatch") {
        const ring = Math.trunc(Math.min(Math.max(brushSize / 8, 3), 16));
        const margin = Math.max(w, h) >> 1;
        const off = bestOffset(w, h, 4, img, domain, ring, margin);
        filled = (off && proximityPatch(img, w, h, 4, domain, off[0], off[1])) || contentAware();
    } else {
        filled = contentAware();
    }
    stop.check();
    const grown = dilate(hole, w, h, 1);
    const healCov = new Uint8Array(grown.length);
    for (let i = 0; i < grown.length; ++i)
        healCov[i] = grown[i] ? 255 : 0;
    return heal(w, h, toBytes(filled), imgBytes, healCov, stop);
}

// El Parche: qué hace con el desplazamiento. Devuelve { plan } o { refusal } ("empty", "tooLarge",
// "noOffset" u "offCanvas").
export function planPatch(area, canvas, offset, mode) {
    area = boxIntersect(area, canvas);
    if (boxEmpty(area))
        return { refusal: "empty" };
    const [dx, dy] = offset;
    if (dx === 0 && dy === 0)
        return { refusal: "noOffset" };
    const moved = boxTranslate(area, dx, dy);
    if (!boxEquals(boxIntersect(moved, canvas), moved))
        return { refusal: "offCanvas" };
    const dstShift = mode === "destination" ? [dx, dy] : [0, 0];
    const srcShift = mode === "destination" ? [-dx, -dy] : [dx, dy];
    const rect = mode === "contentAware"
        ? patchWindow(area, canvas)
        : boxIntersect(boxIntersect(boxInflate(boxTranslate(area, dstShift[0], dstShift[1]), 2), canvas),
                       boxTranslate(canvas, -srcShift[0], -srcShift[1]));
    if (boxArea(rect) > MAX_PIXELS)
        return { refusal: "tooLarge" };
    return { plan: { mode, rect, dstShift, srcShift } };
}

export function patchPreviewStep(rect, maxCells) {
    const need = Math.ceil(Math.sqrt(boxArea(rect) / Math.max(maxCells, 1)));
    return Math.min(Math.max(need, 1), 64);
}

export function patchWindow(r, canvas) {
    return boxIntersect(samplingWindow(r, canvas), boxInflate(r, 512));
}

function bandWidth(structure, minSide) {
    let w;
    if (structure <= 1)
        w = Math.max(minSide * 0.3, 12);
    else if (structure === 2)
        w = Math.max(minSide * 0.18, 9);
    else
        w = 6;
    return Math.min(Math.round(w), 128);
}

// Parche según el contenido: el centro copiado (ajustado por Color) y una banda junto al borde
// rehecha según Estructura. 'here' y 'content' en coma flotante RGBA; devuelve la ventana.
export function patchLand(w, h, here, content, selected, structure, color, stop = NEVER) {
    const N = 4;
    let sx0 = w, sy0 = h, sx1 = 0, sy1 = 0;
    for (let i = 0; i < selected.length; ++i)
        if (selected[i]) {
            const x = i % w, y = (i / w) | 0;
            sx0 = Math.min(sx0, x); sy0 = Math.min(sy0, y); sx1 = Math.max(sx1, x + 1); sy1 = Math.max(sy1, y + 1);
        }
    const side = Math.min(Math.max(sx1 - sx0, 0), Math.max(sy1 - sy0, 0));
    stop.check();
    const unset = new Uint8Array(selected.length);
    for (let i = 0; i < unset.length; ++i)
        unset[i] = selected[i] ? 0 : 1;
    const reached = dilate(unset, w, h, bandWidth(structure, side));
    const core = new Uint8Array(selected.length);
    for (let i = 0; i < core.length; ++i)
        core[i] = selected[i] && !reached[i] ? 1 : 0;
    stop.check();
    const grown = dilate(selected, w, h, 6);
    const pick = (r) => {
        const v = [];
        for (let i = 0; i < grown.length; ++i)
            if (grown[i] && !selected[i])
                for (let c = 0; c < N; ++c)
                    v.push(r[i * N + c]);
        return v;
    };
    const gb = levelTransform(N, pick(content), pick(here), colorAmount(color));
    stop.check();
    let buf = new Float32Array(here);
    for (let i = 0; i < core.length; ++i)
        if (core[i])
            for (let c = 0; c < N; ++c) {
                const v = content[i * N + c];
                buf[i * N + c] = c === N - 1 ? v : gb[c][0] * v + gb[c][1];
            }
    for (let i = 0; i < buf.length; ++i)
        buf[i] = Math.min(Math.max(buf[i], 0), 1);
    const band = new Uint8Array(selected.length);
    let any = false;
    for (let i = 0; i < band.length; ++i) {
        band[i] = selected[i] && !core[i] ? 1 : 0;
        any = any || !!band[i];
    }
    if (any) {
        const allowed = new Uint8Array(w * h).fill(1);
        const adapt = color === 0 ? colorLevel("default") : colorAmount(color);
        buf = contentAwareFill(w, h, N, buf, band, allowed, adapt, 0x00ca4e00, stop);
    }
    return buf;
}
