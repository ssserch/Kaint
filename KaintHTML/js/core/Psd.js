// Photoshop: documentos .psd y los de tamaño grande (.psb) con sus capas de píxeles, como Psd.cpp de
// Kaint. Sin el DOM: trabaja con bytes y con imágenes RGBA de 8 bits y alfa directo.
//
// Al abrir, las capas conservan el nombre, la visibilidad, la opacidad y el modo de fusión (los diez
// que Kaint y Photoshop tienen en común), en RGB o grises de 8, 16 o 32 bits (sin comprimir, PackBits
// o ZIP con o sin predicción); los indexados y los de mapa de bits se abren sin capas. Las máscaras
// de píxeles se aplican a la transparencia y los grupos que no cambian nada al acoplarlos se deshacen.
// Lo que las capas de Kaint no pueden representar sin cambiar el aspecto (máscaras de recorte,
// efectos, máscaras vectoriales, capas de ajuste, grupos con opacidad o fusión propias u otros modos)
// abre la imagen combinada del archivo. Al guardar: PSD de 8 bits en RGB con transparencia, una capa
// por capa y la imagen combinada.
import { tr } from "../i18n.js";

const BITMAP = 0, GRAYSCALE = 1, INDEXED = 2, RGB = 3, CMYK = 4, DUOTONE = 8, LAB = 9;
const MAX_PIXELS = 2 ** 31;

class PsdError extends Error {}
const fail = (text) => {
    throw new PsdError(text);
};
const truncated = () => fail(tr("El archivo está incompleto o dañado."));
const invalid = () => fail(tr("No es un documento de Photoshop válido."));

// Lectura en big endian de un trozo de los bytes, sin salirse de él.
class Reader {
    constructor(bytes, start = 0, end = bytes.length) {
        this.b = bytes;
        this.p = start;
        this.end = end;
    }
    get remaining() {
        return this.end - this.p;
    }
    need(n) {
        if (!(n >= 0) || this.remaining < n)
            truncated();
    }
    skip(n) {
        this.need(n);
        this.p += n;
    }
    u8() {
        this.need(1);
        return this.b[this.p++];
    }
    u16() {
        this.need(2);
        const v = (this.b[this.p] << 8) | this.b[this.p + 1];
        this.p += 2;
        return v;
    }
    u32() {
        this.need(4);
        const b = this.b, p = this.p;
        this.p += 4;
        return ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0;
    }
    u64() {
        const hi = this.u32();
        return hi * 4294967296 + this.u32();
    }
    i16() {
        const v = this.u16();
        return v >= 0x8000 ? v - 0x10000 : v;
    }
    i32() {
        return this.u32() | 0;
    }
    key() {
        this.need(4);
        const b = this.b, p = this.p;
        this.p += 4;
        return String.fromCharCode(b[p], b[p + 1], b[p + 2], b[p + 3]);
    }
    peekKey() {
        const b = this.b, p = this.p;
        return this.remaining >= 4 ? String.fromCharCode(b[p], b[p + 1], b[p + 2], b[p + 3]) : "";
    }
    bytes(n) {
        this.need(n);
        const v = this.b.subarray(this.p, this.p + n);
        this.p += n;
        return v;
    }
    // Los 'n' bytes siguientes como otro lector (y este sigue después de ellos).
    sub(n) {
        if (!(n >= 0) || n > this.remaining)
            truncated();
        const r = new Reader(this.b, this.p, this.p + n);
        this.p += n;
        return r;
    }
}

// Bloques de información adicional cuya longitud ocupa 8 bytes en un PSB.
const LONG_BLOCKS = new Set(["LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn", "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD",
                             "lnkE", "lnk3"]);

// Entre bloques puede haber relleno (a 2 o a 4 bytes según quién escribió el archivo).
function skipToSignature(r) {
    for (let i = 0; i < 4 && r.remaining >= 4; ++i) {
        const sig = r.peekKey();
        if (sig === "8BIM" || sig === "8B64")
            return;
        r.skip(1);
    }
}

function rectOf(top, left, bottom, right) {
    const w = right - left, h = bottom - top;
    if (w <= 0 || h <= 0)
        return null;
    if (w > 1000000 || h > 1000000 || w * h > MAX_PIXELS)
        fail(tr("Una capa es demasiado grande."));
    return { x: left, y: top, w, h };
}

// --- Descompresión de canales ---------------------------------------------------------------

// Una fila de PackBits; lo que falte (un archivo dañado) se queda a cero.
function unpackRow(src, out, at, rowBytes) {
    let i = 0, o = 0;
    while (i < src.length && o < rowBytes) {
        const n = (src[i++] << 24) >> 24;
        if (n >= 0) {
            const count = Math.min(n + 1, src.length - i, rowBytes - o);
            out.set(src.subarray(i, i + count), at + o);
            i += n + 1;
            o += count;
        } else if (n !== -128) {
            if (i >= src.length)
                break;
            const count = Math.min(1 - n, rowBytes - o);
            out.fill(src[i++], at + o, at + o + count);
            o += count;
        }
    }
}

// Un flujo de zlib en 'outLen' bytes; si se corta, lo que haya (el resto a cero).
async function inflate(bytes, outLen) {
    const out = new Uint8Array(outLen);
    let o = 0;
    try {
        const reader = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate")).getReader();
        for (;;) {
            const { done, value } = await reader.read();
            if (done)
                break;
            const n = Math.min(value.length, outLen - o);
            out.set(value.subarray(0, n), o);
            o += n;
            if (o >= outLen) {
                reader.cancel().catch(() => {});
                break;
            }
        }
    } catch {
        // Cortado o dañado.
    }
    return out;
}

// Deshace la predicción de ZIP: cada muestra se guardó como la diferencia con la anterior de su fila
// (las de 32 bits, además, separadas en cuatro planos de bytes).
function unpredict(rows, w, h, depth) {
    const rowBytes = (w * depth) / 8;
    const tmp = depth === 32 ? new Uint8Array(rowBytes) : null;
    for (let y = 0; y < h; ++y) {
        const at = y * rowBytes;
        if (depth === 8) {
            for (let x = 1; x < w; ++x)
                rows[at + x] = (rows[at + x] + rows[at + x - 1]) & 255;
        } else if (depth === 16) {
            let prev = (rows[at] << 8) | rows[at + 1];
            for (let x = 1; x < w; ++x) {
                const i = at + 2 * x;
                const v = (((rows[i] << 8) | rows[i + 1]) + prev) & 0xffff;
                rows[i] = v >> 8;
                rows[i + 1] = v & 255;
                prev = v;
            }
        } else if (depth === 32) {
            for (let i = 1; i < rowBytes; ++i)
                rows[at + i] = (rows[at + i] + rows[at + i - 1]) & 255;
            tmp.set(rows.subarray(at, at + rowBytes));
            for (let x = 0; x < w; ++x)
                for (let k = 0; k < 4; ++k)
                    rows[at + 4 * x + k] = tmp[k * w + x];
        }
    }
}

const linearToSrgb8 = (v) => {
    if (!(v > 0))
        return 0;
    if (v >= 1)
        return 255;
    const s = v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055;
    return Math.round(Math.min(1, Math.max(0, s)) * 255);
};

// Filas big endian → 8 bits por muestra. 16 bits se redondean; 32 bits (lineales, en coma flotante)
// se recortan a 0..1 y, si son de color, pasan a sRGB. 1 bit: 1 es negro.
function toEight(src, w, h, depth, color) {
    const out = new Uint8Array(w * h);
    const rowBytes = Math.ceil((w * depth) / 8);
    const view = new DataView(src.buffer, src.byteOffset, src.byteLength);
    for (let y = 0; y < h; ++y) {
        const row = y * rowBytes, o = y * w;
        switch (depth) {
        case 1:
            for (let x = 0; x < w; ++x)
                out[o + x] = (src[row + (x >> 3)] >> (7 - (x & 7))) & 1 ? 0 : 255;
            break;
        case 8:
            out.set(src.subarray(row, row + w), o);
            break;
        case 16:
            for (let x = 0; x < w; ++x)
                out[o + x] = Math.floor((((src[row + 2 * x] << 8) | src[row + 2 * x + 1]) * 255 + 32767) / 65535);
            break;
        case 32:
            for (let x = 0; x < w; ++x) {
                const f = view.getFloat32(row + 4 * x, false);
                out[o + x] = color ? linearToSrgb8(f) : Math.round(Math.min(1, Math.max(0, Number.isFinite(f) ? f : 0)) * 255);
            }
            break;
        }
    }
    return out;
}

function readCounts(r, n, psb) {
    r.need(n * (psb ? 4 : 2));
    const counts = new Uint32Array(n);
    for (let i = 0; i < n; ++i)
        counts[i] = psb ? r.u32() : r.u16();
    return counts;
}

// Un plano (w × h) a 8 bits desde los datos de un canal ya sin los 2 bytes de la compresión.
async function readPlane(r, compression, w, h, depth, psb, color) {
    if (w <= 0 || h <= 0)
        return null;
    const rowBytes = Math.ceil((w * depth) / 8);
    let raw = new Uint8Array(rowBytes * h);
    switch (compression) {
    case 0:
        raw.set(r.bytes(Math.min(r.remaining, rowBytes * h)));
        break;
    case 1: {
        const counts = readCounts(r, h, psb);
        for (let y = 0; y < h; ++y)
            unpackRow(r.bytes(Math.min(counts[y], r.remaining)), raw, y * rowBytes, rowBytes);
        break;
    }
    case 2:
    case 3:
        raw = await inflate(r.bytes(r.remaining), rowBytes * h);
        if (compression === 3)
            unpredict(raw, w, h, depth);
        break;
    default:
        fail(tr("El archivo usa una compresión desconocida."));
    }
    return toEight(raw, w, h, depth, color);
}

// --- Estructura ----------------------------------------------------------------------------

// Los modos de fusión que Kaint y Photoshop tienen igual.
const BLEND_KEYS = { normal: "norm", multiply: "mul ", additive: "lddg", colorburn: "idiv", colordodge: "div ", overlay: "over",
                     difference: "diff", lighten: "lite", darken: "dark", screen: "scrn" };
const BLEND_FROM_KEY = Object.fromEntries(Object.entries(BLEND_KEYS).map(([k, v]) => [v, k]));

export const psdBlendSupported = (blend) => blend in BLEND_KEYS;

// Nombre de un modo de Photoshop que Kaint no tiene (para el aviso).
function photoshopBlendName(key) {
    const names = { diss: "Disolver", lbrn: "Subexposición lineal", dkCl: "Color más oscuro", lgCl: "Color más claro", sLit: "Luz suave",
                    hLit: "Luz fuerte", vLit: "Luz intensa", lLit: "Luz lineal", pLit: "Luz focal", hMix: "Mezcla definida",
                    smud: "Exclusión", fsub: "Restar", fdiv: "Dividir", "hue ": "Tono", "sat ": "Saturación", colr: "Color",
                    "lum ": "Luminosidad" };
    return names[key] ? tr(names[key]) : key.trim();
}

const indexOfBytes = (haystack, text) => {
    const needle = [...text].map((c) => c.charCodeAt(0));
    outer: for (let i = 0; i + needle.length <= haystack.length; ++i) {
        for (let k = 0; k < needle.length; ++k)
            if (haystack[i + k] !== needle[k])
                continue outer;
        return i;
    }
    return -1;
};

// Efectos de capa (descriptor 'lfx2' o 'lmfx'): encendidos si el interruptor general no está
// apagado y alguno tiene «enab» a verdadero.
function effectsOn(descriptor) {
    const master = indexOfBytes(descriptor, "masterFXSwitchbool");
    if (master >= 0 && master + 18 < descriptor.length && descriptor[master + 18] === 0)
        return false;
    return indexOfBytes(descriptor, "enabbool\x01") >= 0;
}

function readResources(r, res) {
    while (r.remaining >= 12) {
        const sig = r.key();
        if (!["8BIM", "MeSa", "AgHg", "PHUT", "DCSR"].includes(sig))
            break;
        const id = r.u16();
        const nameLen = r.u8();
        r.skip(nameLen + ((nameLen + 1) & 1));
        const size = r.u32();
        const d = r.sub(Math.min(size, r.remaining));
        if (size & 1 && r.remaining > 0)
            r.skip(1);
        if (id === 1005 && d.remaining >= 4)
            res.dpi = d.u32() / 65536;
        else if (id === 1039)
            res.icc = d.bytes(d.remaining).slice();
        else if (id === 1057 && d.remaining >= 5) {
            d.skip(4);
            res.realMergedData = d.u8();
        } else if (id === 1024 && d.remaining >= 2)
            res.targetLayer = d.u16();
        else if (id === 1047 && d.remaining >= 2)
            res.transparentIndex = d.u16();
        else if ((id === 1006 || id === 1045) && d.remaining > 0)
            res.alphaNames = true;
    }
}

const ADJUSTMENTS = new Set(["SoCo", "GdFl", "PtFl", "brit", "levl", "curv", "expA", "vibA", "hue ", "hue2", "blnc", "blwh", "phfl", "mixr",
                             "clrL", "nvrt", "post", "thrs", "grdm", "selc", "CgEd"]);

function readRecord(r, psb) {
    const rec = { rect: null, channels: [], blend: "norm", opacity: 255, clipping: 0, flags: 0, name: "", mask: null, section: 0,
                  sectionBlend: "", fillOpacity: 255, effects: false, vectorMask: false, adjustment: false, text: false,
                  smartObject: false, knockout: false };
    const top = r.i32(), left = r.i32(), bottom = r.i32(), right = r.i32();
    rec.rect = rectOf(top, left, bottom, right);
    const count = r.u16();
    if (count > 56)
        invalid();
    for (let i = 0; i < count; ++i)
        rec.channels.push({ id: r.i16(), length: psb ? r.u64() : r.u32(), at: 0 });
    if (r.key() !== "8BIM")
        invalid();
    rec.blend = r.key();
    rec.opacity = r.u8();
    rec.clipping = r.u8();
    rec.flags = r.u8();
    r.skip(1);
    const extra = r.sub(r.u32());

    // Máscara de capa.
    const maskLen = extra.u32();
    if (maskLen > 0) {
        const m = extra.sub(maskLen);
        if (m.remaining >= 18) {
            const mask = { rect: null, background: 0, flags: 0, density: 255, feather: 0, real: null };
            const t = m.i32(), l = m.i32(), b = m.i32(), rt = m.i32();
            mask.rect = rectOf(t, l, b, rt);
            mask.background = m.u8();
            mask.flags = m.u8();
            if (maskLen > 20) {
                if (mask.flags & 0x10 && m.remaining >= 1) {
                    const params = m.u8();
                    if (params & 1)
                        mask.density = m.u8();
                    if (params & 2)
                        mask.feather = new DataView(m.bytes(8).slice().buffer).getFloat64(0, false);
                    if (params & 4)
                        m.skip(1);
                    if (params & 8)
                        m.skip(8);
                }
                if (m.remaining >= 18) {
                    const flags = m.u8(), background = m.u8();
                    const t2 = m.i32(), l2 = m.i32(), b2 = m.i32(), r2 = m.i32();
                    mask.real = { flags, background, rect: rectOf(t2, l2, b2, r2) };
                }
            }
            rec.mask = mask;
        }
    }
    // Rangos de fusión (no se usan).
    extra.skip(extra.u32());
    // Nombre en Pascal (Latin-1), con relleno hasta un múltiplo de 4.
    const nameLen = extra.u8();
    rec.name = String.fromCharCode(...extra.bytes(nameLen));
    extra.skip(Math.min((4 - ((nameLen + 1) % 4)) % 4, extra.remaining));

    while (extra.remaining >= 12) {
        const sig = extra.key();
        if (sig !== "8BIM" && sig !== "8B64")
            break;
        const key = extra.key();
        const len = psb && LONG_BLOCKS.has(key) ? extra.u64() : extra.u32();
        const b = extra.sub(Math.min(len, extra.remaining));
        if (key === "luni" && b.remaining >= 4) {
            const n = b.u32();
            if (n <= b.remaining / 2) {
                let name = "";
                for (let i = 0; i < n; ++i)
                    name += String.fromCharCode(b.u16());
                rec.name = name.replace(/\0+$/, "");
            }
        } else if ((key === "lsct" || key === "lsdk") && b.remaining >= 4) {
            rec.section = b.u32();
            if (b.remaining >= 8 && b.key() === "8BIM")
                rec.sectionBlend = b.key();
        } else if (key === "lfx2" || key === "lmfx") {
            rec.effects = rec.effects || effectsOn(b.bytes(b.remaining));
        } else if ((key === "vmsk" || key === "vsms") && b.remaining >= 8) {
            b.skip(4);
            rec.vectorMask = rec.vectorMask || !(b.u32() & 4);
        } else if (key === "iOpa" && b.remaining >= 1) {
            rec.fillOpacity = b.u8();
        } else if (key === "TySh" || key === "tySh") {
            rec.text = true;
        } else if (["SoLd", "PlLd", "plLd", "SoLE"].includes(key)) {
            rec.smartObject = true;
        } else if (key === "knko" && b.remaining >= 1) {
            rec.knockout = b.u8() !== 0;
        } else if (ADJUSTMENTS.has(key)) {
            rec.adjustment = true;
        }
        skipToSignature(extra);
    }
    return rec;
}

// Las capas: sus registros y la posición de los datos de sus canales (se descomprimen después, solo
// si hacen falta).
function readLayerInfo(r, psb, out) {
    if (r.remaining < 2)
        return;
    let count = r.i16();
    if (count < 0) {
        out.mergedAlpha = true;
        count = -count;
    }
    const records = [];
    for (let i = 0; i < count; ++i)
        records.push(readRecord(r, psb));
    for (const rec of records)
        for (const c of rec.channels) {
            if (c.length > r.remaining)
                truncated();
            c.at = r.p;
            r.skip(c.length);
        }
    out.records = records;
}

function parse(bytes) {
    const r = new Reader(bytes);
    if (bytes.length < 26 || r.key() !== "8BPS")
        invalid();
    const version = r.u16();
    if (version !== 1 && version !== 2)
        fail(tr("Versión de Photoshop desconocida."));
    const h = { psb: version === 2 };
    r.skip(6);
    h.channels = r.u16();
    h.height = r.u32();
    h.width = r.u32();
    h.depth = r.u16();
    h.mode = r.u16();
    const maxSide = h.psb ? 300000 : 30000;
    if (h.channels < 1 || h.channels > 56 || h.width < 1 || h.height < 1 || h.width > maxSide || h.height > maxSide)
        invalid();
    // El lienzo del navegador no pasa de unos 32768 píxeles por lado.
    if (h.width > 32768 || h.height > 32768 || h.width * h.height > 2 ** 28)
        fail(tr("La imagen es demasiado grande (%1 × %2 píxeles).", h.width, h.height));
    if (![1, 8, 16, 32].includes(h.depth))
        fail(tr("Profundidad de color no admitida (%1 bits).", h.depth));
    if ([RGB, GRAYSCALE, DUOTONE].includes(h.mode)) {
        if (h.depth === 1)
            invalid();
    } else if (h.mode === INDEXED) {
        if (h.depth !== 8)
            invalid();
    } else if (h.mode === BITMAP) {
        if (h.depth !== 1)
            invalid();
    } else if (h.mode === CMYK) {
        fail(tr("Kaint no abre documentos de Photoshop en CMYK. Pásalo a RGB en Photoshop."));
    } else if (h.mode === LAB) {
        fail(tr("Kaint no abre documentos de Photoshop en color Lab. Pásalo a RGB en Photoshop."));
    } else {
        fail(tr("Kaint no abre documentos de Photoshop multicanal. Pásalo a RGB en Photoshop."));
    }

    const p = { header: h, resources: { dpi: 0, icc: null, realMergedData: -1, targetLayer: -1, transparentIndex: -1, alphaNames: false },
                palette: null, records: [], mergedAlpha: false, bytes, imageData: 0 };
    const colorData = r.sub(r.u32());
    if (h.mode === INDEXED && colorData.remaining >= 768)
        p.palette = colorData.bytes(768);
    readResources(r.sub(r.u32()), p.resources);

    const layers = r.sub(h.psb ? r.u64() : r.u32());
    if (layers.remaining > 0) {
        const infoLen = h.psb ? layers.u64() : layers.u32();
        readLayerInfo(layers.sub(Math.min(infoLen, layers.remaining)), h.psb, p);
        if (layers.remaining >= 4) {
            const globalMask = layers.u32();
            layers.skip(Math.min(globalMask, layers.remaining));
        }
        // Con 16 y 32 bits las capas van en un bloque al final ('Lr16', 'Lr32').
        skipToSignature(layers);
        while (layers.remaining >= 12) {
            const sig = layers.key();
            if (sig !== "8BIM" && sig !== "8B64")
                break;
            const key = layers.key();
            const len = h.psb && LONG_BLOCKS.has(key) ? layers.u64() : layers.u32();
            const b = layers.sub(Math.min(len, layers.remaining));
            if ((key === "Lr16" || key === "Lr32" || key === "Layr") && !p.records.length)
                readLayerInfo(b, h.psb, p);
            skipToSignature(layers);
        }
    }
    p.imageData = r.p;
    return p;
}

// La imagen combinada que guarda el archivo (RGBA); null si no está o no se entiende.
async function readMerged(p, withAlpha) {
    const h = p.header;
    try {
        const r = new Reader(p.bytes, p.imageData);
        const compression = r.u16();
        const colors = h.mode === RGB ? 3 : 1;
        if (h.channels < colors)
            return null;
        const used = Math.min(h.channels, colors + (withAlpha ? 1 : 0));
        const rowBytes = Math.ceil((h.width * h.depth) / 8), planeBytes = rowBytes * h.height;
        let raw = new Uint8Array(planeBytes * used);
        if (compression === 0) {
            for (let c = 0; c < used; ++c)
                raw.set(r.bytes(Math.min(r.remaining, planeBytes)), c * planeBytes);
        } else if (compression === 1) {
            const counts = readCounts(r, h.channels * h.height, h.psb);
            for (let c = 0; c < used; ++c)
                for (let y = 0; y < h.height; ++y)
                    unpackRow(r.bytes(Math.min(counts[c * h.height + y], r.remaining)), raw, c * planeBytes + y * rowBytes, rowBytes);
        } else if (compression === 2 || compression === 3) {
            raw = await inflate(r.bytes(r.remaining), planeBytes * used);
            if (compression === 3)
                for (let c = 0; c < used; ++c)
                    unpredict(raw.subarray(c * planeBytes, (c + 1) * planeBytes), h.width, h.height, h.depth);
        } else {
            return null;
        }
        const planes = [];
        for (let c = 0; c < used; ++c)
            planes.push(toEight(raw.subarray(c * planeBytes, (c + 1) * planeBytes), h.width, h.height, h.depth, c < colors));
        const n = h.width * h.height, data = new Uint8ClampedArray(n * 4);
        const [p0] = planes, p1 = colors === 3 ? planes[1] : p0, p2 = colors === 3 ? planes[2] : p0, pa = used > colors ? planes[colors] : null;
        const pal = h.mode === INDEXED && p.palette ? p.palette : null;
        for (let i = 0; i < n; ++i) {
            const o = i * 4;
            if (pal) {
                const v = p0[i];
                data[o] = pal[v];
                data[o + 1] = pal[256 + v];
                data[o + 2] = pal[512 + v];
                data[o + 3] = v === p.resources.transparentIndex ? 0 : 255;
            } else {
                data[o] = p0[i];
                data[o + 1] = p1[i];
                data[o + 2] = p2[i];
                data[o + 3] = pa ? pa[i] : 255;
            }
        }
        return { width: h.width, height: h.height, data };
    } catch (e) {
        if (e instanceof PsdError)
            return null;
        throw e;
    }
}

// Si la imagen combinada lleva transparencia: con capas, cuando el número de capas era negativo;
// sin capas, el canal de más (si no es un canal alfa con nombre, una selección guardada).
function mergedHasAlpha(p) {
    if (p.mergedAlpha)
        return true;
    const colors = p.header.mode === RGB ? 3 : 1;
    return !p.records.length && p.header.channels > colors && !p.resources.alphaNames && (p.header.mode === RGB || p.header.mode === GRAYSCALE);
}

const channelOf = (rec, id) => rec.channels.find((c) => c.id === id) || null;

// La capa (RGBA del tamaño del lienzo) con sus canales y la máscara aplicada a la transparencia.
async function layerPixels(p, rec, changes) {
    const h = p.header;
    const data = new Uint8ClampedArray(h.width * h.height * 4);
    const r = rec.rect;
    if (!r)
        return data;
    const x0 = Math.max(0, r.x), y0 = Math.max(0, r.y), x1 = Math.min(h.width, r.x + r.w), y1 = Math.min(h.height, r.y + r.h);
    if (x0 !== r.x || y0 !== r.y || x1 !== r.x + r.w || y1 !== r.y + r.h)
        changes.cropped = true;
    if (rec.mask && rec.mask.flags & 0x02)
        changes.maskDropped = true;
    if (x0 >= x1 || y0 >= y1)
        return data;

    const gray = h.mode !== RGB;
    const plane = async (id, rect) => {
        const c = channelOf(rec, id);
        if (!c || c.length < 2 || !rect)
            return null;
        const cr = new Reader(p.bytes, c.at, c.at + c.length);
        const compression = cr.u16();
        return readPlane(cr, compression, rect.w, rect.h, h.depth, h.psb, id >= 0);
    };
    const c0 = await plane(0, r);
    const c1 = gray ? null : await plane(1, r);
    const c2 = gray ? null : await plane(2, r);
    const ca = await plane(-1, r);

    // La máscara de píxeles (con una vectorial también, es la «real», -3).
    let mask = null, maskRect = null, background = 255, flags = 0x02;
    if (rec.mask) {
        const real = rec.mask.real && channelOf(rec, -3);
        maskRect = real ? rec.mask.real.rect : rec.mask.rect;
        background = real ? rec.mask.real.background : rec.mask.background;
        flags = real ? rec.mask.real.flags : rec.mask.flags;
        if (!(flags & 0x02)) {
            mask = await plane(real ? -3 : -2, maskRect);
            // Sin píxeles y con el fondo blanco no oculta nada.
            if (!mask && background === 255 && !(flags & 0x04))
                flags |= 0x02;
            else
                changes.masked = true;
        }
    }
    const useMask = !!rec.mask && !(flags & 0x02);
    const invert = !!(flags & 0x04), density = rec.mask?.density ?? 255;

    for (let y = y0; y < y1; ++y) {
        const row = (y - r.y) * r.w;
        for (let x = x0; x < x1; ++x) {
            const i = row + (x - r.x), o = (y * h.width + x) * 4;
            const red = c0 ? c0[i] : 0;
            data[o] = red;
            data[o + 1] = gray ? red : c1 ? c1[i] : 0;
            data[o + 2] = gray ? red : c2 ? c2[i] : 0;
            let a = ca ? ca[i] : 255;
            if (useMask) {
                let m = background;
                if (mask && maskRect && x >= maskRect.x && x < maskRect.x + maskRect.w && y >= maskRect.y && y < maskRect.y + maskRect.h)
                    m = mask[(y - maskRect.y) * maskRect.w + (x - maskRect.x)];
                if (invert)
                    m = 255 - m;
                if (density < 255)
                    m = 255 - Math.floor(((255 - m) * density) / 255);
                a = Math.floor((a * m + 127) / 255);
            }
            data[o + 3] = a;
        }
    }
    return data;
}

async function build(p) {
    const h = p.header;
    const converted = [], unsupported = [];
    const note = (list, text) => list.includes(text) || list.push(text);
    if (h.psb)
        note(converted, tr("es un documento de tamaño grande (PSB), que Kaint no guarda"));
    if (h.depth === 16 || h.depth === 32)
        note(converted, tr("tiene %1 bits por canal y Kaint trabaja con 8", h.depth));
    if (h.mode === GRAYSCALE || h.mode === DUOTONE)
        note(converted, tr("está en escala de grises y se pasó a RGB"));
    if (h.mode === INDEXED)
        note(converted, tr("tiene una paleta de colores indexados y se pasó a RGB"));
    if (h.mode === BITMAP)
        note(converted, tr("es un mapa de bits en blanco y negro y se pasó a RGB"));

    // Grupos: los registros van de abajo arriba; el fin de un grupo (sección 3) va debajo de sus capas
    // y el propio grupo (1 o 2), encima, con sus propiedades.
    const stack = [[]];
    let groups = false;
    for (const rec of p.records) {
        if (rec.section === 3) {
            stack.push([]);
            continue;
        }
        if (rec.section === 1 || rec.section === 2) {
            groups = true;
            if (stack.length < 2)
                continue;
            const children = stack.pop();
            const blend = rec.sectionBlend || rec.blend;
            if (rec.flags & 0x02) {
                for (const b of children)
                    b.hidden = true;
            } else {
                const allNormal = children.every((b) => b.record.blend === "norm" || b.hidden);
                const plain = (blend === "pass" || (blend === "norm" && allNormal)) && rec.opacity === 255 && rec.fillOpacity === 255 && !rec.effects
                              && !rec.vectorMask && !(rec.mask && !(rec.mask.flags & 0x02));
                if (!plain)
                    note(unsupported, tr("grupos con opacidad, máscara, efectos o modo de fusión propios"));
            }
            stack[stack.length - 1].push(...children);
            continue;
        }
        stack[stack.length - 1].push({ record: rec, hidden: !!(rec.flags & 0x02) });
    }
    while (stack.length > 1) {
        const rest = stack.pop();
        stack[stack.length - 1].push(...rest);
    }
    const layers = stack[0];
    if (groups)
        note(converted, tr("tenía grupos de capas, que Kaint no tiene (sus capas se conservan)"));

    for (const b of layers) {
        const rec = b.record;
        if (b.hidden) {
            if (!BLEND_FROM_KEY[rec.blend])
                note(converted, tr("capas ocultas con modos de fusión que Kaint no tiene pasaron a Normal"));
            continue;
        }
        if (rec.clipping)
            note(unsupported, tr("máscaras de recorte"));
        if (rec.effects)
            note(unsupported, tr("efectos de capa (sombras, trazos, resplandores…)"));
        if (rec.vectorMask)
            note(unsupported, tr("máscaras vectoriales o capas de forma"));
        if (rec.adjustment)
            note(unsupported, tr("capas de ajuste o de relleno"));
        if (rec.knockout)
            note(unsupported, tr("capas que calan a las de debajo"));
        if (rec.mask && !(rec.mask.flags & 0x02) && rec.mask.feather > 0)
            note(unsupported, tr("máscaras de capa difuminadas"));
        if (!BLEND_FROM_KEY[rec.blend])
            note(unsupported, tr("el modo de fusión «%1»", photoshopBlendName(rec.blend)));
    }

    const alpha = mergedHasAlpha(p);
    const colors = h.mode === RGB ? 3 : 1;
    if (h.channels > colors + (alpha ? 1 : 0))
        note(converted, tr("tiene canales alfa o de tinta plana, que no se conservan"));

    const result = { width: h.width, height: h.height, dpi: p.resources.dpi >= 1 && p.resources.dpi <= 100000 ? p.resources.dpi : 0,
                     icc: p.resources.icc, layers: [], active: 0, warning: "" };
    const finish = (fallback) => {
        if (fallback || converted.length) {
            let text;
            if (fallback) {
                text = tr("Este documento de Photoshop usa cosas que las capas de Kaint no pueden representar sin cambiar su aspecto:") +
                       `\n• ${unsupported.join("\n• ")}\n\n` + tr("Se abrió la imagen combinada que guarda el archivo, en una sola capa.");
                if (converted.length)
                    text += `\n\n${tr("Además, el documento:")}\n• ${converted.join("\n• ")}`;
            } else {
                text = `${tr("Al abrir este documento de Photoshop, Kaint tuvo que convertirlo porque:")}\n• ${converted.join("\n• ")}`;
            }
            result.warning = `${text}\n\n${tr("Se abre como una imagen nueva: al guardar se pide un nombre y el archivo original no se pisa.")}`;
        }
        return result;
    };

    // Sin capas, o con algo que no se puede representar: la imagen combinada.
    if (!layers.length || unsupported.length) {
        const fallback = layers.length > 0;
        if (p.resources.realMergedData !== 0 || !layers.length) {
            const merged = await readMerged(p, alpha);
            if (merged) {
                result.layers = [{ name: tr("Fondo"), visible: true, opacity: 255, blend: "normal", data: merged.data }];
                return finish(fallback);
            }
            if (!layers.length)
                fail(tr("No se pudo leer la imagen combinada del documento."));
        }
        // Sin imagen combinada: las capas, lo más parecido posible.
        note(converted, tr("usa cosas de Photoshop que Kaint no tiene (%1), que se ignoraron", unsupported.join(", ")));
        unsupported.length = 0;
    }

    const changes = { cropped: false, masked: false, maskDropped: false };
    let rasterized = false, fill = false;
    const target = p.resources.targetLayer >= 0 ? p.records[Math.min(p.resources.targetLayer, p.records.length - 1)] : null;
    result.active = layers.length - 1;
    for (const b of layers) {
        const rec = b.record;
        let opacity = rec.opacity;
        if (rec.fillOpacity < 255) {
            opacity = Math.floor((rec.opacity * rec.fillOpacity + 127) / 255);
            fill = true;
        }
        rasterized = rasterized || rec.text || rec.smartObject;
        if (rec === target)
            result.active = result.layers.length;
        result.layers.push({ name: rec.name || tr("Capa %1", result.layers.length + 1), visible: !b.hidden, opacity,
                             blend: BLEND_FROM_KEY[rec.blend] || "normal", data: await layerPixels(p, rec, changes) });
    }
    if (changes.masked)
        note(converted, tr("tenía máscaras de capa, que se aplicaron a la transparencia"));
    if (changes.maskDropped)
        note(converted, tr("tenía máscaras de capa desactivadas, que no se conservan"));
    if (changes.cropped)
        note(converted, tr("tenía partes de capas fuera del lienzo, que se recortaron"));
    if (rasterized)
        note(converted, tr("tenía texto u objetos inteligentes, que quedan como píxeles"));
    if (fill)
        note(converted, tr("la opacidad de relleno se combinó con la opacidad de la capa"));
    return finish(false);
}

// Un documento de Photoshop → { width, height, dpi, icc, layers: [{ name, visible, opacity, blend,
// data }] (la de abajo primero), active, warning }. 'warning' no vacío: hubo que convertir algo y
// conviene abrirlo como imagen nueva (sin archivo).
export async function decodePsd(bytes) {
    return build(parse(bytes));
}

// --- Escritura -----------------------------------------------------------------------------

class Writer {
    constructor() {
        this.chunks = [];
        this.buf = new Uint8Array(1 << 16);
        this.n = 0;
        this.size = 0;
    }
    ensure(k) {
        if (this.n + k > this.buf.length) {
            this.chunks.push(this.buf.subarray(0, this.n));
            this.buf = new Uint8Array(Math.max(1 << 16, k));
            this.n = 0;
        }
    }
    u8(v) {
        this.ensure(1);
        this.buf[this.n++] = v & 255;
        this.size++;
    }
    u16(v) {
        this.u8(v >> 8);
        this.u8(v);
    }
    u32(v) {
        this.u16((v >>> 16) & 0xffff);
        this.u16(v & 0xffff);
    }
    raw(bytes) {
        this.ensure(bytes.length);
        this.buf.set(bytes, this.n);
        this.n += bytes.length;
        this.size += bytes.length;
    }
    text(s) {
        for (const c of s)
            this.u8(c.charCodeAt(0));
    }
    bytes() {
        const out = new Uint8Array(this.size);
        let o = 0;
        for (const c of [...this.chunks, this.buf.subarray(0, this.n)]) {
            out.set(c, o);
            o += c.length;
        }
        return out;
    }
}

// PackBits: repeticiones de 3 o más bytes como (1 - n, byte) y el resto tal cual, en tramos de 128.
function packBits(src, out) {
    const n = src.length;
    let i = 0;
    while (i < n) {
        let run = 1;
        while (i + run < n && run < 128 && src[i + run] === src[i])
            ++run;
        if (run >= 3) {
            out.u8(1 - run);
            out.u8(src[i]);
            i += run;
            continue;
        }
        const start = i;
        let len = 0;
        while (i < n && len < 128) {
            if (i + 2 < n && src[i] === src[i + 1] && src[i] === src[i + 2])
                break;
            ++i;
            ++len;
        }
        out.u8(len - 1);
        out.raw(src.subarray(start, start + len));
    }
}

// Un canal (0 rojo, 1 verde, 2 azul, 3 alfa) del rectángulo 'r' de una imagen RGBA, con PackBits:
// la longitud de cada fila y después las filas.
function packChannel(data, width, r, channel) {
    const counts = new Writer(), rows = new Writer(), row = new Uint8Array(r.w);
    for (let y = r.y; y < r.y + r.h; ++y) {
        for (let x = 0; x < r.w; ++x)
            row[x] = data[(y * width + r.x + x) * 4 + channel];
        const before = rows.size;
        packBits(row, rows);
        counts.u16(rows.size - before);
    }
    return { counts: counts.bytes(), rows: rows.bytes() };
}

// El rectángulo con algo visible (alfa > 0) de la capa.
function usedRect(data, width, height) {
    let top = -1, bottom = -1, left = width, right = -1;
    for (let y = 0; y < height; ++y) {
        let first = -1, last = -1;
        for (let x = 0; x < width; ++x)
            if (data[(y * width + x) * 4 + 3]) {
                if (first < 0)
                    first = x;
                last = x;
            }
        if (first < 0)
            continue;
        if (top < 0)
            top = y;
        bottom = y;
        left = Math.min(left, first);
        right = Math.max(right, last);
    }
    return top < 0 ? null : { x: left, y: top, w: right - left + 1, h: bottom - top + 1 };
}

function resource(out, id, data) {
    out.text("8BIM");
    out.u16(id);
    out.u16(0);
    out.u32(data.length);
    out.raw(data);
    if (data.length & 1)
        out.u8(0);
}

// doc: { width, height, dpi, active, icc, layers: [{ name, visible, opacity, blend, data (RGBA) }]
// (la de abajo primero), composite (RGBA) } → los bytes de un PSD de 8 bits en RGB.
export function encodePsd(doc) {
    const { width: w, height: h } = doc;
    if (w > 30000 || h > 30000)
        fail(tr("PSD admite imágenes de hasta 30000 × 30000 píxeles. Guárdala como PDN u ORA para conservar las capas."));
    for (const l of doc.layers)
        if (!psdBlendSupported(l.blend))
            fail(tr("La capa «%1» usa un modo de fusión que Photoshop no tiene. Cámbialo en sus propiedades o guarda la imagen como PDN u ORA.", l.name));
    let alpha = false;
    for (let i = 3; i < doc.composite.length && !alpha; i += 4)
        alpha = doc.composite[i] !== 255;
    const channels = alpha ? 4 : 3;

    const out = new Writer();
    out.text("8BPS");
    out.u16(1);
    for (let i = 0; i < 6; ++i)
        out.u8(0);
    out.u16(channels);
    out.u32(h);
    out.u32(w);
    out.u16(8);
    out.u16(RGB);
    out.u32(0);

    // Recursos: resolución, capa activa y perfil de color.
    const res = new Writer();
    const dpi = new Writer(), fixed = Math.round(Math.min(30000, Math.max(1, doc.dpi || 96)) * 65536);
    dpi.u32(fixed);
    dpi.u16(1);
    dpi.u16(1);
    dpi.u32(fixed);
    dpi.u16(1);
    dpi.u16(1);
    resource(res, 1005, dpi.bytes());
    const target = new Writer();
    target.u16(Math.max(0, doc.active || 0));
    resource(res, 1024, target.bytes());
    if (doc.icc?.length)
        resource(res, 1039, doc.icc);
    const resources = res.bytes();
    out.u32(resources.length);
    out.raw(resources);

    // Capas: registros y luego los canales.
    const info = new Writer();
    info.u16((alpha ? -doc.layers.length : doc.layers.length) & 0xffff);
    const ids = [-1, 0, 1, 2];
    const pixels = [];
    for (const layer of doc.layers) {
        const r = usedRect(layer.data, w, h);
        const chans = ids.map((id) => {
            const c = new Writer();
            if (!r) {
                c.u16(0);
            } else {
                c.u16(1);
                const packed = packChannel(layer.data, w, r, id < 0 ? 3 : id);
                c.raw(packed.counts);
                c.raw(packed.rows);
            }
            return c.bytes();
        });
        pixels.push(chans);
        info.u32(r ? r.y : 0);
        info.u32(r ? r.x : 0);
        info.u32(r ? r.y + r.h : 0);
        info.u32(r ? r.x + r.w : 0);
        info.u16(4);
        ids.forEach((id, k) => {
            info.u16(id & 0xffff);
            info.u32(chans[k].length);
        });
        info.text("8BIM");
        info.text(BLEND_KEYS[layer.blend]);
        info.u8(Math.min(255, Math.max(0, layer.opacity ?? 255)));
        info.u8(0);
        info.u8(0x08 | (layer.visible === false ? 0x02 : 0));
        info.u8(0);
        const extra = new Writer();
        extra.u32(0);
        extra.u32(32);
        for (let k = 0; k < 8; ++k)
            extra.u32(0x0000ffff);
        const latin = [...layer.name].map((c) => (c.charCodeAt(0) < 256 ? c.charCodeAt(0) : 63)).slice(0, 255);
        extra.u8(latin.length);
        for (const c of latin)
            extra.u8(c);
        while ((extra.size - 40) % 4)
            extra.u8(0);
        const units = [...layer.name].flatMap((c) => {
            const cp = c.codePointAt(0);
            return cp > 0xffff ? [0xd800 + ((cp - 0x10000) >> 10), 0xdc00 + ((cp - 0x10000) & 0x3ff)] : [cp];
        });
        const luniLen = 4 + units.length * 2 + (units.length % 2 ? 2 : 0);
        extra.text("8BIMluni");
        extra.u32(luniLen);
        extra.u32(units.length);
        for (const u of units)
            extra.u16(u);
        if (units.length % 2)
            extra.u16(0);
        const extraBytes = extra.bytes();
        info.u32(extraBytes.length);
        info.raw(extraBytes);
    }
    for (const chans of pixels)
        for (const c of chans)
            info.raw(c);
    while (info.size % 4)
        info.u8(0);
    const infoBytes = info.bytes();
    out.u32(4 + infoBytes.length + 4);
    out.u32(infoBytes.length);
    out.raw(infoBytes);
    out.u32(0);

    // Imagen combinada, con PackBits.
    out.u16(1);
    const all = { x: 0, y: 0, w, h };
    const packed = [];
    for (let c = 0; c < channels; ++c)
        packed.push(packChannel(doc.composite, w, all, c));
    for (const p of packed)
        out.raw(p.counts);
    for (const p of packed)
        out.raw(p.rows);
    return out.bytes();
}
