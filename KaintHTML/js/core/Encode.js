// Lo que se escribe al guardar, como ImageIO::prepare, encode y encodeAnimation de Kaint: la
// imagen acoplada se prepara (sin alfa si el formato no lo admite, con la profundidad de bits
// elegida) y se codifica. Sin el DOM, así también funciona en el Web Worker de la vista previa de
// "Configuración de guardado".
import * as Codecs from "./Codecs.js";
import { crc32 } from "./Zip.js";

// Lo que admite cada formato (ImageIO::supportsQuality, hasBitDepth, supportsAlpha...).
export const FormatTraits = {
    png: { depth: true, interlace: true },
    jpg: { quality: true, opaque: true },
    webp: { quality: true },
    avif: { quality: true },
    bmp: { depth: true, opaque: true },
    tga: { depth: true },
    gif: { animation: true },
    ico: { icon: true },
    tiff: {},
    dds: {},
};

export const traits = (ext) => FormatTraits[ext] || {};
export const hasOptions = (ext) => {
    const t = traits(ext);
    return !!(t.quality || t.depth || t.animation || t.icon);
};

// Opciones de "Configuración de guardado" (ImageIO::SaveOptions de Kaint).
export function defaultSaveOptions() {
    return {
        quality: 95, bitDepth: 0, dither: 7, alphaThreshold: 128, quantization: 1, interlaced: false,
        animation: true, frameDelay: 100, loops: 0, gifGlobalPalette: false, gifStack: false, gifHiddenLayers: false,
        gifEffects: { crossfade: false, fadeDelay: 60, fadeFrames: 10, fadeLastToFirst: true, firstAsBackground: false, firstOnlyAsBackground: false },
        icoSizes: [...Codecs.IcoSizes],
    };
}

export const automaticBitDepth = (image) => (Codecs.hasTransparency(image) ? 32 : 24);

// La imagen tal como se escribirá: RGBA, opaca sobre blanco (24 bits) o indexada (8 bits).
export function prepare(image, ext, o) {
    const t = traits(ext);
    if (t.animation || t.icon)
        return image;
    let depth = t.depth ? o.bitDepth : 32;
    if (!depth)
        depth = automaticBitDepth(image);
    if (t.opaque && depth === 32)
        depth = 24;
    if (depth === 8)
        return Codecs.toIndexed8(image, { dither: o.dither, alphaThreshold: o.alphaThreshold, algorithm: o.quantization });
    if (depth === 24)
        return { ...Codecs.onWhite(image), depth: 24 };
    return image;
}

const MIMES = { png: "image/png", jpg: "image/jpeg", webp: "image/webp", avif: "image/avif" };

// Codificación del navegador (JPEG, WebP, AVIF y PNG de 32 bits), también dentro de un worker.
async function canvasEncode(image, mime, quality) {
    const pixels = new ImageData(new Uint8ClampedArray(image.data), image.width, image.height);
    let blob;
    if (typeof OffscreenCanvas !== "undefined") {
        const c = new OffscreenCanvas(image.width, image.height);
        c.getContext("2d").putImageData(pixels, 0, 0);
        blob = await c.convertToBlob({ type: mime, quality });
    } else {
        const c = document.createElement("canvas");
        c.width = image.width;
        c.height = image.height;
        c.getContext("2d").putImageData(pixels, 0, 0);
        blob = await new Promise((resolve, reject) => c.toBlob((b) => (b ? resolve(b) : reject(new Error(mime))), mime, quality));
    }
    if (blob.type !== mime)
        throw new Error(`El navegador no sabe escribir ${mime}.`);
    return new Uint8Array(await blob.arrayBuffer());
}

// Resolución (y perfil de color) en los PNG y JPEG del navegador.
function withMetadata(bytes, ext, { dpi = 0, icc = null } = {}) {
    if (ext === "png" && bytes[12] === 0x49 && bytes[13] === 0x48) {
        const chunks = [];
        const chunk = (type, data) => {
            const c = new Uint8Array(12 + data.length), view = new DataView(c.buffer);
            view.setUint32(0, data.length);
            for (let i = 0; i < 4; ++i) c[4 + i] = type.charCodeAt(i);
            c.set(data, 8);
            view.setUint32(8 + data.length, crc32(c.subarray(4, 8 + data.length)));
            chunks.push(c);
        };
        if (dpi > 0) {
            const ppm = Math.round(dpi / 0.0254), data = new Uint8Array(9), view = new DataView(data.buffer);
            view.setUint32(0, ppm);
            view.setUint32(4, ppm);
            data[8] = 1;
            chunk("pHYs", data);
        }
        if (icc)
            chunk("iCCP", icc);
        if (!chunks.length)
            return bytes;
        const at = 8 + 25, total = chunks.reduce((n, c) => n + c.length, 0), out = new Uint8Array(bytes.length + total);
        out.set(bytes.subarray(0, at));
        let o = at;
        for (const c of chunks) {
            out.set(c, o);
            o += c.length;
        }
        out.set(bytes.subarray(at), o);
        return out;
    }
    if (ext === "jpg" && bytes[2] === 0xff && bytes[3] === 0xe0 && String.fromCharCode(...bytes.subarray(6, 11)) === "JFIF\0") {
        let out = bytes;
        if (dpi > 0) {
            out = bytes.slice();
            const d = Math.min(65535, Math.round(dpi));
            out[13] = 1;
            out[14] = d >> 8; out[15] = d & 255;
            out[16] = d >> 8; out[17] = d & 255;
        }
        if (icc) {
            // APP2 ICC_PROFILE justo después de JFIF (en trozos de 65519 bytes como mucho), en lugar
            // del sRGB que pone el navegador.
            out = withoutJpegIcc(out);
            const app0End = 4 + ((out[4] << 8) | out[5]), parts = [];
            const count = Math.ceil(icc.length / 65519);
            for (let i = 0; i < count; ++i) {
                const piece = icc.subarray(i * 65519, (i + 1) * 65519), seg = new Uint8Array(4 + 14 + piece.length);
                seg[0] = 0xff; seg[1] = 0xe2;
                const len = 2 + 14 + piece.length;
                seg[2] = len >> 8; seg[3] = len & 255;
                for (let k = 0; k < 12; ++k) seg[4 + k] = "ICC_PROFILE\0".charCodeAt(k);
                seg[16] = i + 1; seg[17] = count;
                seg.set(piece, 18);
                parts.push(seg);
            }
            const total = parts.reduce((n, p) => n + p.length, 0), merged = new Uint8Array(out.length + total);
            merged.set(out.subarray(0, app0End));
            let o = app0End;
            for (const p of parts) {
                merged.set(p, o);
                o += p.length;
            }
            merged.set(out.subarray(app0End), o);
            out = merged;
        }
        return out;
    }
    return bytes;
}

// El JPEG sin sus segmentos APP2 ICC_PROFILE.
function withoutJpegIcc(bytes) {
    const keep = [];
    let i = 2, from = 0;
    while (i + 4 <= bytes.length && bytes[i] === 0xff && bytes[i + 1] !== 0xda) {
        const len = (bytes[i + 2] << 8) | bytes[i + 3];
        if (bytes[i + 1] === 0xe2 && String.fromCharCode(...bytes.subarray(i + 4, i + 15)) === "ICC_PROFILE") {
            keep.push(bytes.subarray(from, i));
            from = i + 2 + len;
        }
        i += 2 + len;
    }
    if (!keep.length)
        return bytes;
    keep.push(bytes.subarray(from));
    const out = new Uint8Array(keep.reduce((n, p) => n + p.length, 0));
    let o = 0;
    for (const p of keep) {
        out.set(p, o);
        o += p.length;
    }
    return out;
}

// WebP con el perfil de color: formato extendido (VP8X, con la marca ICC) y el trozo ICCP justo
// después.
function webpWithIcc(bytes, image, icc) {
    if (!icc)
        return bytes;
    const chunk = (fourcc, data) => {
        const out = new Uint8Array(8 + data.length + (data.length & 1));
        for (let i = 0; i < 4; ++i) out[i] = fourcc.charCodeAt(i);
        new DataView(out.buffer).setUint32(4, data.length, true);
        out.set(data, 8);
        return out;
    };
    let head, rest;
    if (String.fromCharCode(...bytes.subarray(12, 16)) === "VP8X") {
        head = bytes.slice(12, 30);
        head[8] |= 0x20;
        rest = bytes.subarray(30);
        // Sin el sRGB que pone el navegador.
        if (String.fromCharCode(...rest.subarray(0, 4)) === "ICCP") {
            const len = rest[4] | (rest[5] << 8) | (rest[6] << 16) | (rest[7] << 24);
            rest = rest.subarray(8 + len + (len & 1));
        }
    } else {
        // VP8L lleva el bit de alfa en su cabecera; VP8 (con pérdida) simple no tiene alfa.
        const alpha = String.fromCharCode(...bytes.subarray(12, 16)) === "VP8L" && (bytes[24] & 0x10) !== 0;
        const vp8x = new Uint8Array(10), w = image.width - 1, h = image.height - 1;
        vp8x[0] = 0x20 | (alpha ? 0x10 : 0);
        vp8x.set([w & 255, (w >> 8) & 255, (w >> 16) & 255, h & 255, (h >> 8) & 255, (h >> 16) & 255], 4);
        head = chunk("VP8X", vp8x);
        rest = bytes.subarray(12);
    }
    const parts = [head, chunk("ICCP", icc), rest];
    const total = 12 + parts.reduce((n, p) => n + p.length, 0), out = new Uint8Array(total);
    out.set([0x52, 0x49, 0x46, 0x46]);
    new DataView(out.buffer).setUint32(4, total - 8, true);
    out.set([0x57, 0x45, 0x42, 0x50], 8);
    let o = 12;
    for (const p of parts) {
        out.set(p, o);
        o += p.length;
    }
    return out;
}

// Contenido del iCCP de PNG: nombre, método de compresión y el perfil comprimido.
async function iccChunk(icc) {
    if (!icc)
        return null;
    const name = new TextEncoder().encode("ICC Profile"), z = await Codecs.zlibCompress(icc);
    const out = new Uint8Array(name.length + 2 + z.length);
    out.set(name);
    out.set(z, name.length + 2);
    return out;
}

// Los bytes del archivo a partir de la imagen preparada. extra: { dpi, icc }.
export async function encodeImage(prepared, ext, o, extra = {}) {
    const { dpi = 0, icc = null } = extra;
    switch (ext) {
    case "gif":
        return encodeAnimation([{ ...prepared, delay: o.frameDelay }], o);
    case "ico":
        return Codecs.encodeIco(prepared, o.icoSizes);
    case "bmp":
        return Codecs.encodeBmp(prepared, { dpi: dpi || 96 });
    case "tga":
        return Codecs.encodeTga(prepared, { depth: prepared.depth || 32 });
    case "tiff":
        return Codecs.encodeTiff(prepared, { dpi: dpi || 96, icc });
    case "dds":
        return Codecs.encodeDds(prepared);
    case "png":
        if (prepared.indices || prepared.depth === 24 || o.interlaced) {
            const bytes = await Codecs.encodePng(prepared, { depth: prepared.depth || 32, interlaced: o.interlaced, dpi });
            return icc ? withMetadata(bytes, "png", { icc: await iccChunk(icc) }) : bytes;
        }
        return withMetadata(await canvasEncode(prepared, MIMES.png), "png", { dpi, icc: await iccChunk(icc) });
    case "webp":
        return webpWithIcc(await canvasEncode(prepared, MIMES.webp, o.quality / 100), prepared, icc);
    default:
        return withMetadata(await canvasEncode(prepared, MIMES[ext], traits(ext).quality ? o.quality / 100 : undefined), ext, { dpi, icc });
    }
}

// Los fotogramas sin duración llevan la predeterminada; luego, los efectos, la paleta y las
// repeticiones (ImageIO::encodeAnimation de Kaint).
export function encodeAnimation(frames, o) {
    const timed = frames.map((f) => ({ ...f, delay: f.delay > 0 ? f.delay : o.frameDelay }));
    return Codecs.encodeGif(Codecs.applyGifEffects(timed, o.gifEffects), {
        loops: o.loops, dither: o.dither, alphaThreshold: o.alphaThreshold, quantization: o.quantization,
        globalPalette: o.gifGlobalPalette, stack: o.gifStack,
    });
}

// Lo codificado, vuelto a leer (la vista previa es exactamente el archivo): [{ width, height,
// data, delay }]. Los formatos del navegador vuelven como ImageBitmap en 'bitmap'.
export async function decodeForPreview(bytes, ext) {
    if (ext === "gif")
        return Codecs.decodeGif(bytes).frames;
    if (ext === "tga")
        return [Codecs.decodeTga(bytes)];
    if (ext === "tiff")
        return [await Codecs.decodeTiff(bytes)];
    if (ext === "dds")
        return [Codecs.decodeDds(bytes)];
    const bitmap = await createImageBitmap(new Blob([bytes], { type: ext === "ico" ? "image/x-icon" : MIMES[ext] || `image/${ext}` }));
    return [{ width: bitmap.width, height: bitmap.height, bitmap, delay: 100 }];
}

// Codifica para guardar o para la vista previa. job: { image, frames, ext, options, dpi, icc }.
export async function encodeJob({ image, frames, ext, options, dpi, icc }) {
    if (frames?.length)
        return encodeAnimation(frames, options);
    return encodeImage(prepare(image, ext, options), ext, options, { dpi, icc });
}

