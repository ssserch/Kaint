// Utilidades de imagen: lienzos, damero, miniaturas y la composición de las capas con los modos
// de fusión de Paint.NET (los mismos que src/core/BlendMode.cpp de Kaint).

export function createCanvas(width, height) {
    const canvas = document.createElement("canvas");
    canvas.width = Math.max(1, Math.round(width));
    canvas.height = Math.max(1, Math.round(height));
    return canvas;
}

export function context(canvas) {
    return canvas.getContext("2d", { willReadFrequently: true });
}

export function cloneCanvas(source) {
    const canvas = createCanvas(source.width, source.height);
    context(canvas).drawImage(source, 0, 0);
    return canvas;
}

export function clampRect(r, width, height) {
    const x0 = Math.max(0, Math.floor(r.x));
    const y0 = Math.max(0, Math.floor(r.y));
    const x1 = Math.min(width, Math.ceil(r.x + r.w));
    const y1 = Math.min(height, Math.ceil(r.y + r.h));
    return { x: x0, y: y0, w: Math.max(0, x1 - x0), h: Math.max(0, y1 - y0) };
}

export function unionRect(a, b) {
    if (!a || a.w <= 0 || a.h <= 0)
        return b ? { ...b } : null;
    if (!b || b.w <= 0 || b.h <= 0)
        return { ...a };
    const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y);
    return { x, y, w: Math.max(a.x + a.w, b.x + b.w) - x, h: Math.max(a.y + a.h, b.y + b.h) - y };
}

export function intersectRect(a, b) {
    const x = Math.max(a.x, b.x), y = Math.max(a.y, b.y);
    const w = Math.min(a.x + a.w, b.x + b.w) - x, h = Math.min(a.y + a.h, b.y + b.h) - y;
    return { x, y, w: Math.max(0, w), h: Math.max(0, h) };
}

// Damero de la transparencia: cuadros de 8 px blancos y grises (el gris es el brillo elegido en
// Configuración, 191 por omisión, como en Kaint).
let checkerCache = null;
// Damero de transparencia como el de Kaint: el gris claro es el brillo elegido (191 por defecto,
// como Paint.NET) y el oscuro, sus tres cuartas partes.
export function checkerPattern(ctx, brightness = 191) {
    if (!checkerCache || checkerCache.brightness !== brightness) {
        const light = Math.max(0, Math.min(255, Math.round(brightness))), dark = Math.round(light * 0.75);
        const tile = createCanvas(16, 16);
        const c = tile.getContext("2d");
        c.fillStyle = `rgb(${light},${light},${light})`;
        c.fillRect(0, 0, 16, 16);
        c.fillStyle = `rgb(${dark},${dark},${dark})`;
        c.fillRect(0, 0, 8, 8);
        c.fillRect(8, 8, 8, 8);
        checkerCache = { brightness, tile };
    }
    return ctx.createPattern(checkerCache.tile, "repeat");
}

// Miniatura que cabe en w × h conservando la proporción (con el damero debajo si se pide).
export function thumbnail(source, w, h, checker = true) {
    const scale = Math.min(w / source.width, h / source.height);
    const tw = Math.max(1, Math.round(source.width * scale));
    const th = Math.max(1, Math.round(source.height * scale));
    const canvas = createCanvas(tw, th);
    const c = canvas.getContext("2d");
    if (checker) {
        c.fillStyle = checkerPattern(c);
        c.save();
        c.scale(0.5, 0.5);
        c.fillRect(0, 0, tw * 2, th * 2);
        c.restore();
    }
    c.imageSmoothingQuality = "high";
    c.drawImage(source, 0, 0, tw, th);
    return canvas;
}

// --- Modos de fusión -----------------------------------------------------------------------

export const BlendModes = [
    "normal", "multiply", "additive", "colorburn", "colordodge", "reflect", "glow", "overlay",
    "difference", "negation", "lighten", "darken", "screen", "xor",
];

// Nombres de la lista de las capas y de la barra de herramientas (Paint.NET los traduce distinto).
export const BlendNames = {
    normal: ["Normal", "Normal"], multiply: ["Multiplicar", "Multiplicar"], additive: ["Adición", "Aditivo"],
    colorburn: ["Sobreexposición de color", "Sobreexposición de color"],
    colordodge: ["Subexposición de color", "Subexposición de color"], reflect: ["Reflejo", "Reflejar"],
    glow: ["Brillo", "Resplandor"], overlay: ["Superponer", "Superposición"], difference: ["Diferencia", "Diferencia"],
    negation: ["Negativo", "Negación"], lighten: ["Aclarar", "Aclarar"], darken: ["Oscurecer", "Oscurecer"],
    screen: ["Pantalla", "Trama"], xor: ["O exclusivo", "O exclusivo"],
};

// Nombres de OpenRaster (composite-op de stack.xml).
export const OraNames = {
    normal: "svg:src-over", multiply: "svg:multiply", additive: "svg:plus", colorburn: "svg:color-burn",
    colordodge: "svg:color-dodge", reflect: "pdn:reflect", glow: "pdn:glow", overlay: "svg:overlay",
    difference: "svg:difference", negation: "pdn:negation", lighten: "svg:lighten", darken: "svg:darken",
    screen: "svg:screen", xor: "pdn:xor",
};

// Los modos del lienzo del navegador que coinciden con la fórmula generalizada de Paint.NET.
const nativeModes = {
    normal: "source-over", multiply: "multiply", overlay: "overlay", difference: "difference",
    lighten: "lighten", darken: "darken", screen: "screen",
};

const blendFns = {
    normal: (a, b) => b,
    multiply: (a, b) => ((a * b + 127) / 255) | 0,
    additive: (a, b) => Math.min(255, a + b),
    colorburn: (a, b) => (b === 0 ? 0 : Math.max(0, 255 - (((255 - a) * 255) / b) | 0)),
    colordodge: (a, b) => (b === 255 ? 255 : Math.min(255, ((a * 255) / (255 - b)) | 0)),
    reflect: (a, b) => (b === 255 ? 255 : Math.min(255, ((a * a) / (255 - b)) | 0)),
    glow: (a, b) => (a === 255 ? 255 : Math.min(255, ((b * b) / (255 - a)) | 0)),
    overlay: (a, b) => (a < 128 ? ((2 * a * b + 127) / 255) | 0 : 255 - (((2 * (255 - a) * (255 - b) + 127) / 255) | 0)),
    difference: (a, b) => Math.abs(a - b),
    negation: (a, b) => 255 - Math.abs(255 - a - b),
    lighten: (a, b) => Math.max(a, b),
    darken: (a, b) => Math.min(a, b),
    screen: (a, b) => a + b - (((a * b + 127) / 255) | 0),
    xor: (a, b) => a ^ b,
};

// Mezcla 'src' sobre 'dst' (ImageData del mismo tamaño, alfa directo) con la fórmula de Paint.NET.
// lerpStraight de Kaint (src/core/Pixels.h) sobre el píxel RGBA de 'o' en 'j'.
export function lerpStraight(o, j, r, g, b, a, t) {
    if (t <= 0)
        return;
    if (t >= 255) {
        o[j] = r; o[j + 1] = g; o[j + 2] = b; o[j + 3] = a;
        return;
    }
    const it = 255 - t, wa = o[j + 3] * it, wb = a * t, w = wa + wb;
    if (w === 0) {
        o[j] = Math.trunc((o[j] * it + r * t + 127) / 255);
        o[j + 1] = Math.trunc((o[j + 1] * it + g * t + 127) / 255);
        o[j + 2] = Math.trunc((o[j + 2] * it + b * t + 127) / 255);
        o[j + 3] = 0;
        return;
    }
    const ch = (ca, cb) => Math.trunc((ca * wa + cb * wb + Math.trunc(w / 2)) / w);
    o[j] = ch(o[j], r); o[j + 1] = ch(o[j + 1], g); o[j + 2] = ch(o[j + 2], b);
    o[j + 3] = Math.trunc((w + 127) / 255);
}

// scaleAlpha de Kaint: el alfa multiplicado por m (0..255), redondeado.
export function scaleAlpha(alpha, m) {
    return m >= 255 ? alpha : m <= 0 ? 0 : Math.trunc((alpha * m + 127) / 255);
}

// Compone lo que dibuja una herramienta (formas, líneas, texto) como compositeRegion de Kaint:
// 'cover' es la figura con los colores opacos (su alfa es la cobertura) y 'real', con los colores de
// verdad (el alfa de cada color sale de la proporción entre los dos). Sin suavizado, cada píxel
// queda entero o fuera; 'clip' es la cobertura de la selección (o null) en un lienzo de 'width' de
// ancho. Con "overwrite", lo de debajo se sustituye en la proporción de la cobertura.
export function compositeTool(ctx, cover, real, rect, { mode = "normal", antialias = true, clip = null, width = 0 } = {}) {
    const base = ctx.getImageData(rect.x, rect.y, rect.w, rect.h), d = base.data;
    const c = context(cover).getImageData(rect.x, rect.y, rect.w, rect.h).data;
    const r = context(real).getImageData(rect.x, rect.y, rect.w, rect.h).data;
    const overwrite = mode === "overwrite", top = overwrite ? null : new ImageData(rect.w, rect.h), t = top?.data;
    for (let y = 0; y < rect.h; ++y)
        for (let x = 0; x < rect.w; ++x) {
            const i = (y * rect.w + x) * 4, raw = c[i + 3];
            if (!raw)
                continue;
            let cov = antialias ? raw : raw >= 128 ? 255 : 0;
            if (clip)
                cov = Math.trunc((cov * clip[(rect.y + y) * width + rect.x + x]) / 255);
            if (!cov)
                continue;
            const alpha = Math.min(255, Math.round((r[i + 3] * 255) / raw));
            if (overwrite) {
                lerpStraight(d, i, c[i], c[i + 1], c[i + 2], alpha, cov);
            } else {
                t[i] = c[i]; t[i + 1] = c[i + 1]; t[i + 2] = c[i + 2]; t[i + 3] = scaleAlpha(alpha, cov);
            }
        }
    if (!overwrite)
        blendPixels(base, top, mode);
    ctx.putImageData(base, rect.x, rect.y);
}

export function blendPixels(dst, src, mode, opacity = 255) {
    const F = blendFns[mode] || blendFns.normal;
    const d = dst.data, s = src.data;
    for (let i = 0; i < d.length; i += 4) {
        let sb = s[i + 3];
        if (opacity < 255)
            sb = ((sb * opacity + 127) / 255) | 0;
        if (sb === 0)
            continue;
        const da = d[i + 3];
        if (da === 0) {
            d[i] = s[i]; d[i + 1] = s[i + 1]; d[i + 2] = s[i + 2]; d[i + 3] = sb;
            continue;
        }
        const x = ((da * sb + 127) / 255) | 0;
        const y = da - x, z = sb - x, total = y + sb;
        for (let c = 0; c < 3; ++c) {
            const A = d[i + c], B = s[i + c];
            d[i + c] = Math.min(255, ((A * y + B * z + F(A, B) * x + (total >> 1)) / total) | 0);
        }
        d[i + 3] = total;
    }
}

// Dibuja 'source' sobre 'ctx' en (x, y) con el modo de fusión y la opacidad (0..255). Los modos
// que el navegador no tiene se mezclan píxel a píxel (solo en el rectángulo que cambia).
export function drawBlended(ctx, source, mode, opacity = 255, rect = null) {
    const r = rect || { x: 0, y: 0, w: source.width, h: source.height };
    if (r.w <= 0 || r.h <= 0)
        return;
    if (nativeModes[mode]) {
        ctx.save();
        ctx.globalCompositeOperation = nativeModes[mode];
        ctx.globalAlpha = opacity / 255;
        ctx.drawImage(source, r.x, r.y, r.w, r.h, r.x, r.y, r.w, r.h);
        ctx.restore();
        return;
    }
    const dst = ctx.getImageData(r.x, r.y, r.w, r.h);
    const src = context(source).getImageData(r.x, r.y, r.w, r.h);
    blendPixels(dst, src, mode, opacity);
    ctx.putImageData(dst, r.x, r.y);
}

// --- Color -----------------------------------------------------------------------------------

export function rgba(c) {
    return `rgba(${c.r},${c.g},${c.b},${(c.a ?? 255) / 255})`;
}

export function hex(c, alpha = false) {
    const h = (v) => v.toString(16).padStart(2, "0");
    return `${h(c.r)}${h(c.g)}${h(c.b)}${alpha ? h(c.a ?? 255) : ""}`.toUpperCase();
}

export function parseHex(text) {
    const t = text.replace(/^#/, "").trim();
    if (!/^([0-9a-f]{6}|[0-9a-f]{8})$/i.test(t))
        return null;
    return {
        r: parseInt(t.slice(0, 2), 16), g: parseInt(t.slice(2, 4), 16), b: parseInt(t.slice(4, 6), 16),
        a: t.length === 8 ? parseInt(t.slice(6, 8), 16) : 255,
    };
}

export function rgbToHsv(c) {
    const r = c.r / 255, g = c.g / 255, b = c.b / 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
    let h = 0;
    if (d > 0) {
        if (max === r) h = ((g - b) / d) % 6;
        else if (max === g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h *= 60;
        if (h < 0) h += 360;
    }
    return { h, s: max === 0 ? 0 : d / max, v: max };
}

export function hsvToRgb(h, s, v, a = 255) {
    const c = v * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = v - c;
    let r = 0, g = 0, b = 0;
    if (h < 60) [r, g, b] = [c, x, 0];
    else if (h < 120) [r, g, b] = [x, c, 0];
    else if (h < 180) [r, g, b] = [0, c, x];
    else if (h < 240) [r, g, b] = [0, x, c];
    else if (h < 300) [r, g, b] = [x, 0, c];
    else [r, g, b] = [c, 0, x];
    return { r: Math.round((r + m) * 255), g: Math.round((g + m) * 255), b: Math.round((b + m) * 255), a };
}

export function sameColor(a, b) {
    return a.r === b.r && a.g === b.g && a.b === b.b && (a.a ?? 255) === (b.a ?? 255);
}
