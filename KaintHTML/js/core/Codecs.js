// Formatos de imagen que el navegador no trae, como los de Kaint: GIF animado (Gif.cpp), PNG con
// profundidad de bits y entrelazado (PngWriter.cpp), iconos con varios tamaños (Ico), BMP, TGA,
// TIFF, DDS y la lectura de Paint.NET (.pdn, Pdn.cpp). Trabajan con imágenes { width, height,
// data } (RGBA de 8 bits con alfa directo) y bytes, sin el DOM: sirven también en un Web Worker.
import { findEffect } from "./Effects.js";
import { scaleImage } from "./Resample.js";
import { crc32 } from "./Zip.js";

// --- Bytes --------------------------------------------------------------------------------

class Writer {
    constructor(size = 1 << 16) {
        this.buf = new Uint8Array(size);
        this.length = 0;
    }

    reserve(n) {
        if (this.length + n <= this.buf.length)
            return;
        let size = this.buf.length * 2;
        while (size < this.length + n)
            size *= 2;
        const next = new Uint8Array(size);
        next.set(this.buf.subarray(0, this.length));
        this.buf = next;
    }

    u8(v) { this.reserve(1); this.buf[this.length++] = v & 255; }
    u16(v) { this.u8(v); this.u8(v >> 8); }
    u32(v) { this.u16(v & 0xffff); this.u16((v >>> 16) & 0xffff); }
    u16be(v) { this.u8(v >> 8); this.u8(v); }
    u32be(v) { this.u8(v >>> 24); this.u8(v >>> 16); this.u8(v >>> 8); this.u8(v); }
    bytes(b) { this.reserve(b.length); this.buf.set(b, this.length); this.length += b.length; }
    ascii(s) { for (let i = 0; i < s.length; ++i) this.u8(s.charCodeAt(i)); }
    result() { return this.buf.slice(0, this.length); }
}

const u16le = (b, i) => b[i] | (b[i + 1] << 8);
const u32le = (b, i) => (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0;
const u16be = (b, i) => (b[i] << 8) | b[i + 1];
const u32be = (b, i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
const ascii = (b, i, n) => String.fromCharCode(...b.subarray(i, i + n));

async function streamThrough(bytes, transform) {
    const stream = new Blob([bytes]).stream().pipeThrough(transform);
    return new Uint8Array(await new Response(stream).arrayBuffer());
}
// zlib (RFC 1950), como compress2 de Kaint.
export const zlibCompress = (bytes) => streamThrough(bytes, new CompressionStream("deflate"));
export const zlibDecompress = (bytes) => streamThrough(bytes, new DecompressionStream("deflate"));
export const gunzip = (bytes) => streamThrough(bytes, new DecompressionStream("gzip"));

export function newImage(width, height) {
    return { width, height, data: new Uint8ClampedArray(width * height * 4) };
}

// --- Paleta de 8 bits (ImageIO::toIndexed8 de Kaint) --------------------------------------

// Paleta óptima con el mismo cuantificador que el efecto Cuantizar (255 colores y uno
// transparente si hace falta; con 'transparentSlot' siempre se reserva el transparente).
// Devuelve { width, height, indices, palette: [[r, g, b, a], ...] }.
export function toIndexed8(image, { dither = 7, alphaThreshold = 128, transparentSlot = false, algorithm = 1 } = {}) {
    const { width, height } = image, n = width * height;
    const work = new Uint8ClampedArray(image.data);
    let transparent = false;
    for (let i = 0; i < n * 4; i += 4) {
        if (work[i + 3] < alphaThreshold) {
            work[i] = work[i + 1] = work[i + 2] = work[i + 3] = 0;
            transparent = true;
        } else {
            work[i + 3] = 255;
        }
    }
    const quantize = findEffect("quantize");
    const values = { algorithm: Math.min(1, Math.max(0, algorithm)), colors: transparent || transparentSlot ? 255 : 256,
                     dither: Math.min(8, Math.max(0, dither)), threshold: 128 };
    const out = quantize.render(new ImageData(work, width, height), values, { x: 0, y: 0, w: width, h: height }, {}).data;
    const indices = new Uint8Array(n), palette = [], map = new Map();
    for (let p = 0, i = 0; p < n; ++p, i += 4) {
        const key = out[i + 3] === 0 ? -1 : (out[i] << 16) | (out[i + 1] << 8) | out[i + 2];
        let index = map.get(key);
        if (index === undefined) {
            index = palette.length;
            map.set(key, index);
            palette.push(key === -1 ? [0, 0, 0, 0] : [out[i], out[i + 1], out[i + 2], 255]);
        }
        indices[p] = Math.min(255, index);
    }
    if (transparentSlot && !transparent && palette.length < 256)
        palette.push([0, 0, 0, 0]);
    return { width, height, indices, palette };
}

// Píxeles opacos sobre blanco (24 bits), como el guardado de Kaint.
export function onWhite(image) {
    const out = newImage(image.width, image.height), s = image.data, d = out.data;
    for (let i = 0; i < s.length; i += 4) {
        const a = s[i + 3];
        for (let c = 0; c < 3; ++c)
            d[i + c] = Math.round((s[i + c] * a + 255 * (255 - a)) / 255);
        d[i + 3] = 255;
    }
    return out;
}

export function hasTransparency(image) {
    const d = image.data;
    for (let i = 3; i < d.length; i += 4)
        if (d[i] !== 255)
            return true;
    return false;
}

// --- PNG (PngWriter.cpp de Kaint) ---------------------------------------------------------

function pngChunk(w, type, data) {
    w.u32be(data.length);
    const start = w.length;
    w.ascii(type);
    w.bytes(data);
    w.u32be(crc32(w.buf.subarray(start, w.length)));
}

function paeth(a, b, c) {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Filtra una fila con el filtro que deja la suma de valores (con signo) más pequeña.
function filterRow(row, prev, bpp, out) {
    const n = row.length;
    let best = -1, bestType = 0, bestData = null;
    for (let type = 0; type < 5; ++type) {
        const f = new Uint8Array(n);
        let sum = 0;
        for (let i = 0; i < n; ++i) {
            const a = i >= bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0, c = i >= bpp && prev ? prev[i - bpp] : 0;
            const predicted = type === 1 ? a : type === 2 ? b : type === 3 ? (a + b) >> 1 : type === 4 ? paeth(a, b, c) : 0;
            f[i] = row[i] - predicted;
            sum += Math.abs((f[i] << 24) >> 24);
        }
        if (best < 0 || sum < best) {
            best = sum;
            bestType = type;
            bestData = f;
        }
    }
    out.u8(bestType);
    out.bytes(bestData);
}

// image: RGBA ({ data }) o indexada ({ indices, palette }). depth 32 (RGBA) o 24 (RGB).
// extra: { dpi, iccProfile (bytes, ya comprimidos o no) }.
export async function encodePng(image, { depth = 32, interlaced = false, dpi = 0, icc = null } = {}) {
    const { width: w, height: h } = image;
    const indexed = !!image.indices, colorType = indexed ? 3 : depth === 24 ? 2 : 6, bpp = indexed ? 1 : depth === 24 ? 3 : 4;
    const out = new Writer();
    out.bytes([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
    const ihdr = new Writer(13);
    ihdr.u32be(w); ihdr.u32be(h); ihdr.u8(8); ihdr.u8(colorType); ihdr.u8(0); ihdr.u8(0); ihdr.u8(interlaced ? 1 : 0);
    pngChunk(out, "IHDR", ihdr.result());
    if (icc) {
        const c = new Writer();
        c.ascii("ICC Profile"); c.u8(0); c.u8(0);
        c.bytes(await zlibCompress(icc));
        pngChunk(out, "iCCP", c.result());
    }
    if (indexed) {
        const plte = new Writer(), trns = new Writer();
        let anyAlpha = false;
        for (const [r, g, b, a] of image.palette) {
            plte.u8(r); plte.u8(g); plte.u8(b);
            trns.u8(a);
            anyAlpha ||= a < 255;
        }
        pngChunk(out, "PLTE", plte.result());
        if (anyAlpha)
            pngChunk(out, "tRNS", trns.result());
    }
    if (dpi > 0) {
        const ppm = Math.round(dpi / 0.0254), phys = new Writer(9);
        phys.u32be(ppm); phys.u32be(ppm); phys.u8(1);
        pngChunk(out, "pHYs", phys.result());
    }
    // Filas filtradas, en las siete pasadas de Adam7 o de arriba abajo.
    const X0 = [0, 4, 0, 2, 0, 1, 0], Y0 = [0, 0, 4, 0, 2, 0, 1], DX = [8, 8, 4, 4, 2, 2, 1], DY = [8, 8, 8, 4, 4, 2, 2];
    const raw = new Writer(w * h * bpp + h * 2 + 64);
    const passes = interlaced ? 7 : 1;
    for (let pass = 0; pass < passes; ++pass) {
        const sx = interlaced ? X0[pass] : 0, sy = interlaced ? Y0[pass] : 0, stepX = interlaced ? DX[pass] : 1, stepY = interlaced ? DY[pass] : 1;
        const pw = Math.ceil((w - sx) / stepX), ph = Math.ceil((h - sy) / stepY);
        if (pw <= 0 || ph <= 0)
            continue;
        let prev = null;
        for (let y = sy; y < h; y += stepY) {
            const row = new Uint8Array(pw * bpp);
            for (let i = 0, x = sx; x < w; x += stepX, ++i) {
                const p = y * w + x;
                if (indexed)
                    row[i] = image.indices[p];
                else
                    for (let k = 0; k < bpp; ++k)
                        row[i * bpp + k] = image.data[p * 4 + k];
            }
            filterRow(row, prev, bpp, raw);
            prev = row;
        }
    }
    pngChunk(out, "IDAT", await zlibCompress(raw.result()));
    pngChunk(out, "IEND", new Uint8Array(0));
    return out.result();
}

// --- BMP ----------------------------------------------------------------------------------

// 24 bits (RGB) u 8 bits (indexada), de abajo arriba, como el BMP que guarda Kaint.
export function encodeBmp(image, { dpi = 96 } = {}) {
    const { width: w, height: h } = image, indexed = !!image.indices, bpp = indexed ? 8 : 24;
    const stride = Math.ceil((w * bpp) / 32) * 4, colors = indexed ? 256 : 0;
    const offset = 14 + 40 + colors * 4, size = offset + stride * h, ppm = Math.round(dpi / 0.0254);
    const out = new Writer(size);
    out.ascii("BM"); out.u32(size); out.u32(0); out.u32(offset);
    out.u32(40); out.u32(w); out.u32(h); out.u16(1); out.u16(bpp); out.u32(0); out.u32(stride * h);
    out.u32(ppm); out.u32(ppm); out.u32(colors); out.u32(0);
    if (indexed)
        for (let i = 0; i < 256; ++i) {
            const [r, g, b] = image.palette[i] || [0, 0, 0];
            out.u8(b); out.u8(g); out.u8(r); out.u8(0);
        }
    for (let y = h - 1; y >= 0; --y) {
        const start = out.length;
        for (let x = 0; x < w; ++x) {
            const p = y * w + x;
            if (indexed) {
                out.u8(image.indices[p]);
            } else {
                out.u8(image.data[p * 4 + 2]); out.u8(image.data[p * 4 + 1]); out.u8(image.data[p * 4]);
            }
        }
        while (out.length - start < stride)
            out.u8(0);
    }
    return out.result();
}

// --- TGA ----------------------------------------------------------------------------------

// 32 bits (con alfa), 24 bits u 8 bits (con mapa de colores), sin comprimir y de arriba abajo.
export function encodeTga(image, { depth = 32 } = {}) {
    const { width: w, height: h } = image, indexed = !!image.indices;
    const out = new Writer(18 + w * h * 4 + 1024);
    const mapAlpha = indexed && image.palette.some((c) => c[3] < 255);
    out.u8(0);
    out.u8(indexed ? 1 : 0);
    out.u8(indexed ? 1 : 2);
    out.u16(0); out.u16(indexed ? 256 : 0); out.u8(indexed ? (mapAlpha ? 32 : 24) : 0);
    out.u16(0); out.u16(0); out.u16(w); out.u16(h);
    out.u8(indexed ? 8 : depth);
    out.u8(0x20 | (depth === 32 && !indexed ? 8 : mapAlpha ? 8 : 0));
    if (indexed)
        for (let i = 0; i < 256; ++i) {
            const [r, g, b, a] = image.palette[i] || [0, 0, 0, 255];
            out.u8(b); out.u8(g); out.u8(r);
            if (mapAlpha)
                out.u8(a);
        }
    for (let p = 0; p < w * h; ++p) {
        if (indexed) {
            out.u8(image.indices[p]);
            continue;
        }
        const d = image.data;
        out.u8(d[p * 4 + 2]); out.u8(d[p * 4 + 1]); out.u8(d[p * 4]);
        if (depth === 32)
            out.u8(d[p * 4 + 3]);
    }
    // Pie de TGA 2.0 (sin él, algunos lectores, como el de Qt, no lo abren).
    out.u32(0); out.u32(0);
    out.ascii("TRUEVISION-XFILE.");
    out.u8(0);
    return out.result();
}

export function decodeTga(b) {
    const idLength = b[0], mapType = b[1], type = b[2];
    const mapFirst = u16le(b, 3), mapLength = u16le(b, 5), mapBits = b[7];
    const w = u16le(b, 12), h = u16le(b, 14), bits = b[16], desc = b[17];
    if (!w || !h || ![1, 2, 3, 9, 10, 11].includes(type))
        throw new Error("TGA");
    let pos = 18 + idLength;
    const readColor = (bitsPer, at) => {
        if (bitsPer === 8)
            return [b[at], b[at], b[at], 255];
        if (bitsPer === 15 || bitsPer === 16) {
            const v = u16le(b, at);
            const c = (s) => (((v >> s) & 31) * 255 / 31) | 0;
            return [c(10), c(5), c(0), bitsPer === 16 && !(v & 0x8000) && (desc & 15) ? 0 : 255];
        }
        return [b[at + 2], b[at + 1], b[at], bitsPer === 32 ? b[at + 3] : 255];
    };
    const map = [];
    if (mapType === 1) {
        const bytes = Math.ceil(mapBits / 8);
        for (let i = 0; i < mapLength; ++i)
            map.push(readColor(mapBits, pos + i * bytes));
        pos += mapLength * bytes;
    }
    const pixelBytes = Math.ceil(bits / 8), n = w * h;
    const pixel = (at) => (type === 1 || type === 9 ? map[b[at] - mapFirst] || [0, 0, 0, 255] : readColor(type === 3 || type === 11 ? 8 : bits, at));
    const pixels = new Array(n);
    if (type < 9) {
        for (let i = 0; i < n; ++i)
            pixels[i] = pixel(pos + i * pixelBytes);
    } else {
        for (let i = 0; i < n;) {
            const head = b[pos++], count = (head & 127) + 1;
            if (head & 128) {
                const c = pixel(pos);
                pos += pixelBytes;
                for (let k = 0; k < count && i < n; ++k) pixels[i++] = c;
            } else {
                for (let k = 0; k < count && i < n; ++k, pos += pixelBytes) pixels[i++] = pixel(pos);
            }
        }
    }
    // Sin bits de alfa declarados, el cuarto byte no es transparencia.
    const alphaBits = desc & 15;
    const out = newImage(w, h), top = (desc & 0x20) !== 0, right = (desc & 0x10) !== 0;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const c = pixels[y * w + x], ty = top ? y : h - 1 - y, tx = right ? w - 1 - x : x, o = (ty * w + tx) * 4;
            out.data[o] = c[0]; out.data[o + 1] = c[1]; out.data[o + 2] = c[2];
            out.data[o + 3] = bits === 32 && type !== 1 && type !== 9 && !alphaBits ? 255 : c[3];
        }
    return out;
}

// --- TIFF ---------------------------------------------------------------------------------

// LZW de TIFF: códigos de 9 a 12 bits, del bit más significativo, con el cambio de anchura un
// código antes ("early change").
function tiffLzwEncode(data) {
    const out = new Writer(data.length / 2 + 64);
    let bits = 0, count = 0, width = 9;
    const put = (code) => {
        bits = (bits << width) | code;
        count += width;
        while (count >= 8) {
            out.u8((bits >>> (count - 8)) & 255);
            count -= 8;
        }
        bits &= (1 << count) - 1;
    };
    let dict = new Map(), next = 258;
    put(256);
    if (!data.length) {
        put(257);
    } else {
        let prefix = data[0];
        for (let i = 1; i < data.length; ++i) {
            const k = data[i], key = prefix * 256 + k, found = dict.get(key);
            if (found !== undefined) {
                prefix = found;
                continue;
            }
            put(prefix);
            dict.set(key, next++);
            if (next + 1 > (1 << width) && width < 12)
                ++width;
            if (next >= 4094) {
                put(256);
                dict = new Map();
                next = 258;
                width = 9;
            }
            prefix = k;
        }
        put(prefix);
        if (next + 1 > (1 << width) && width < 12)
            ++width;
        put(257);
    }
    if (count > 0)
        out.u8((bits << (8 - count)) & 255);
    return out.result();
}

function tiffLzwDecode(src, expected) {
    const out = new Uint8Array(expected);
    let pos = 0, bitPos = 0, width = 9;
    let table = [];
    const reset = () => {
        table = [];
        for (let i = 0; i < 256; ++i) table.push(Uint8Array.of(i));
        table.push(null, null);
        width = 9;
    };
    const read = () => {
        let v = 0;
        for (let i = 0; i < width; ++i, ++bitPos) {
            const byte = src[bitPos >> 3];
            if (byte === undefined)
                return 257;
            v = (v << 1) | ((byte >> (7 - (bitPos & 7))) & 1);
        }
        return v;
    };
    reset();
    let old = null;
    while (pos < expected) {
        const code = read();
        if (code === 257)
            break;
        if (code === 256) {
            reset();
            old = null;
            continue;
        }
        let entry;
        if (code < table.length && table[code])
            entry = table[code];
        else if (old)
            entry = Uint8Array.from([...old, old[0]]);
        else
            break;
        out.set(entry.subarray(0, Math.min(entry.length, expected - pos)), pos);
        pos += entry.length;
        if (old)
            table.push(Uint8Array.from([...old, entry[0]]));
        old = entry;
        if (table.length + 1 >= 1 << width && width < 12)
            ++width;
    }
    return out;
}

function packBitsDecode(src, expected) {
    const out = new Uint8Array(expected);
    let i = 0, o = 0;
    while (o < expected && i < src.length) {
        const n = (src[i++] << 24) >> 24;
        if (n >= 0) {
            for (let k = 0; k <= n && o < expected; ++k) out[o++] = src[i++];
        } else if (n !== -128) {
            const v = src[i++];
            for (let k = 0; k < 1 - n && o < expected; ++k) out[o++] = v;
        }
    }
    return out;
}

// RGBA de 8 bits por muestra, LZW y alfa sin premultiplicar (como el TIFF de Kaint).
export function encodeTiff(image, { dpi = 96, icc = null } = {}) {
    const { width: w, height: h, data } = image;
    const rowsPerStrip = Math.max(1, Math.floor(65536 / (w * 4)));
    const strips = [];
    for (let y = 0; y < h; y += rowsPerStrip)
        strips.push(tiffLzwEncode(new Uint8Array(data.buffer, data.byteOffset + y * w * 4, Math.min(rowsPerStrip, h - y) * w * 4)));
    const entries = [];
    const tag = (id, type, values) => entries.push({ id, type, values: [].concat(values) });
    tag(256, 4, w); tag(257, 4, h); tag(258, 3, [8, 8, 8, 8]); tag(259, 3, 5); tag(262, 3, 2);
    tag(273, 4, strips.map(() => 0)); tag(277, 3, 4); tag(278, 4, rowsPerStrip); tag(279, 4, strips.map((s) => s.length));
    tag(282, 5, [Math.round(dpi * 100), 100]); tag(283, 5, [Math.round(dpi * 100), 100]); tag(284, 3, 1); tag(296, 3, 2);
    tag(338, 3, 2);
    if (icc)
        tag(34675, 7, [...icc]);
    const sizes = { 3: 2, 4: 4, 5: 8, 7: 1 };
    const valueBytes = (e) => (e.type === 5 ? 8 * (e.values.length / 2) : sizes[e.type] * e.values.length);
    // Cabecera, datos de las franjas, valores largos y el directorio.
    let offset = 8;
    const stripOffsets = strips.map((s) => {
        const at = offset;
        offset += s.length + (s.length & 1);
        return at;
    });
    entries.find((e) => e.id === 273).values = stripOffsets;
    const extra = [];
    for (const e of entries)
        if (valueBytes(e) > 4) {
            e.offset = offset;
            extra.push(e);
            offset += valueBytes(e);
        }
    const ifd = offset;
    const out = new Writer(ifd + 2 + entries.length * 12 + 4);
    out.ascii("II"); out.u16(42); out.u32(ifd);
    for (const s of strips) {
        out.bytes(s);
        if (s.length & 1)
            out.u8(0);
    }
    const writeValues = (e) => {
        for (const v of e.values) {
            if (e.type === 7) out.u8(v);
            else if (e.type === 3) out.u16(v);
            else out.u32(v);
        }
    };
    for (const e of extra)
        writeValues(e);
    out.u16(entries.length);
    for (const e of entries) {
        out.u16(e.id); out.u16(e.type); out.u32(e.type === 5 ? e.values.length / 2 : e.values.length);
        if (valueBytes(e) > 4) {
            out.u32(e.offset);
        } else {
            const start = out.length;
            writeValues(e);
            while (out.length - start < 4) out.u8(0);
        }
    }
    out.u32(0);
    return out.result();
}

export async function decodeTiff(b) {
    const little = b[0] === 0x49;
    const r16 = (i) => (little ? u16le(b, i) : u16be(b, i)), r32 = (i) => (little ? u32le(b, i) : u32be(b, i));
    if (r16(2) !== 42)
        throw new Error("TIFF");
    const ifd = r32(4), count = r16(ifd), tags = {};
    for (let i = 0; i < count; ++i) {
        const e = ifd + 2 + i * 12, id = r16(e), type = r16(e + 2), n = r32(e + 4);
        const size = { 1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 6: 1, 7: 1, 8: 2, 9: 4, 10: 8, 16: 8 }[type] || 1;
        const at = size * n > 4 ? r32(e + 8) : e + 8;
        const values = [];
        for (let k = 0; k < Math.min(n, 1 << 20); ++k) {
            const p = at + k * size;
            values.push(type === 3 || type === 8 ? r16(p) : type === 4 || type === 9 ? r32(p) : type === 5 ? r32(p) / Math.max(1, r32(p + 4)) : b[p]);
        }
        tags[id] = values;
    }
    const w = tags[256][0], h = tags[257][0], bits = tags[258]?.[0] ?? 1, samples = tags[277]?.[0] ?? 1;
    const compression = tags[259]?.[0] ?? 1, photometric = tags[262]?.[0] ?? 1, predictor = tags[317]?.[0] ?? 1;
    const rowsPerStrip = tags[278]?.[0] ?? h, offsets = tags[273], counts = tags[279];
    if (!offsets || (tags[284]?.[0] ?? 1) !== 1 || ![1, 4, 8, 16].includes(bits))
        throw new Error("TIFF");
    const rowBytes = Math.ceil((w * samples * bits) / 8);
    const raw = new Uint8Array(rowBytes * h);
    for (let s = 0; s < offsets.length; ++s) {
        const rows = Math.min(rowsPerStrip, h - s * rowsPerStrip), expected = rows * rowBytes;
        if (rows <= 0)
            break;
        const chunk = b.subarray(offsets[s], offsets[s] + (counts?.[s] ?? expected));
        let data;
        if (compression === 1) data = chunk;
        else if (compression === 5) data = tiffLzwDecode(chunk, expected);
        else if (compression === 8 || compression === 32946) data = await zlibDecompress(chunk);
        else if (compression === 32773) data = packBitsDecode(chunk, expected);
        else throw new Error("TIFF");
        raw.set(data.subarray(0, Math.min(expected, data.length)), s * rowsPerStrip * rowBytes);
    }
    // Predictor horizontal.
    if (predictor === 2 && bits === 8)
        for (let y = 0; y < h; ++y)
            for (let i = samples; i < w * samples; ++i)
                raw[y * rowBytes + i] = (raw[y * rowBytes + i] + raw[y * rowBytes + i - samples]) & 255;
    const sample = (y, i) => {
        if (bits === 8) return raw[y * rowBytes + i];
        if (bits === 16) {
            const p = y * rowBytes + i * 2;
            return (little ? raw[p + 1] : raw[p]);
        }
        const bit = i * bits, byte = raw[y * rowBytes + (bit >> 3)];
        return (byte >> (8 - bits - (bit & 7))) & ((1 << bits) - 1);
    };
    const map = tags[320], extra = tags[338]?.[0] ?? 0, out = newImage(w, h), d = out.data;
    for (let y = 0; y < h; ++y)
        for (let x = 0; x < w; ++x) {
            const o = (y * w + x) * 4, base = x * samples;
            let r, g, bl, a = 255;
            if (photometric === 2) {
                r = sample(y, base); g = sample(y, base + 1); bl = sample(y, base + 2);
                if (samples > 3) a = sample(y, base + 3);
                if (samples > 3 && extra === 1 && a > 0) {
                    r = Math.min(255, Math.round((r * 255) / a)); g = Math.min(255, Math.round((g * 255) / a)); bl = Math.min(255, Math.round((bl * 255) / a));
                }
            } else if (photometric === 3 && map) {
                const i = sample(y, base), n = 1 << bits;
                r = map[i] >> 8; g = map[n + i] >> 8; bl = map[2 * n + i] >> 8;
            } else {
                let v = sample(y, base);
                if (bits < 8) v = Math.round((v * 255) / ((1 << bits) - 1));
                if (photometric === 0) v = 255 - v;
                r = g = bl = v;
                if (samples > 1) a = sample(y, base + 1);
            }
            d[o] = r; d[o + 1] = g; d[o + 2] = bl; d[o + 3] = a;
        }
    const xres = tags[282]?.[0], unit = tags[296]?.[0] ?? 2;
    out.dpi = xres ? (unit === 3 ? xres * 2.54 : xres) : 0;
    return out;
}

// --- DDS ----------------------------------------------------------------------------------

// Superficie de DirectDraw sin comprimir (A8R8G8B8), como la que escribe Qt.
export function encodeDds(image) {
    const { width: w, height: h, data } = image;
    const out = new Writer(128 + w * h * 4);
    out.ascii("DDS ");
    out.u32(124); out.u32(0x1 | 0x2 | 0x4 | 0x8 | 0x1000); out.u32(h); out.u32(w); out.u32(w * 4); out.u32(0); out.u32(0);
    for (let i = 0; i < 11; ++i) out.u32(0);
    out.u32(32); out.u32(0x41); out.u32(0); out.u32(32);
    out.u32(0x00ff0000); out.u32(0x0000ff00); out.u32(0x000000ff); out.u32(0xff000000);
    out.u32(0x1000); out.u32(0); out.u32(0); out.u32(0); out.u32(0);
    for (let i = 0; i < w * h; ++i) {
        out.u8(data[i * 4 + 2]); out.u8(data[i * 4 + 1]); out.u8(data[i * 4]); out.u8(data[i * 4 + 3]);
    }
    return out.result();
}

export function decodeDds(b) {
    if (ascii(b, 0, 4) !== "DDS ")
        throw new Error("DDS");
    const h = u32le(b, 12), w = u32le(b, 16), pfFlags = u32le(b, 80), fourCC = ascii(b, 84, 4);
    const bitCount = u32le(b, 88), masks = [u32le(b, 92), u32le(b, 96), u32le(b, 100), u32le(b, 104)];
    const out = newImage(w, h), d = out.data;
    let pos = 128;
    if (pfFlags & 0x4) {
        // DXT1, DXT3 y DXT5.
        const kind = fourCC === "DXT1" ? 1 : fourCC === "DXT3" ? 3 : fourCC === "DXT5" ? 5 : 0;
        if (!kind)
            throw new Error("DDS");
        const c565 = (v) => [((v >> 11) & 31) * 255 / 31 | 0, ((v >> 5) & 63) * 255 / 63 | 0, (v & 31) * 255 / 31 | 0];
        for (let by = 0; by < Math.ceil(h / 4); ++by)
            for (let bx = 0; bx < Math.ceil(w / 4); ++bx) {
                let alpha = null;
                if (kind === 3) {
                    alpha = [];
                    for (let i = 0; i < 16; ++i) alpha.push(((b[pos + (i >> 1)] >> ((i & 1) * 4)) & 15) * 17);
                    pos += 8;
                } else if (kind === 5) {
                    const a0 = b[pos], a1 = b[pos + 1], table = [a0, a1];
                    if (a0 > a1) for (let i = 1; i < 7; ++i) table.push(((7 - i) * a0 + i * a1) / 7 | 0);
                    else { for (let i = 1; i < 5; ++i) table.push(((5 - i) * a0 + i * a1) / 5 | 0); table.push(0, 255); }
                    let bitsA = 0n;
                    for (let i = 5; i >= 0; --i) bitsA = (bitsA << 8n) | BigInt(b[pos + 2 + i]);
                    alpha = [];
                    for (let i = 0; i < 16; ++i) alpha.push(table[Number((bitsA >> BigInt(i * 3)) & 7n)]);
                    pos += 8;
                }
                const v0 = u16le(b, pos), v1 = u16le(b, pos + 2), c0 = c565(v0), c1 = c565(v1), idx = u32le(b, pos + 4);
                pos += 8;
                const colors = [c0, c1];
                if (v0 > v1 || kind !== 1) {
                    colors.push(c0.map((c, i) => (2 * c + c1[i]) / 3 | 0), c0.map((c, i) => (c + 2 * c1[i]) / 3 | 0));
                } else {
                    colors.push(c0.map((c, i) => (c + c1[i]) / 2 | 0), null);
                }
                for (let i = 0; i < 16; ++i) {
                    const x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                    if (x >= w || y >= h)
                        continue;
                    const c = colors[(idx >>> (i * 2)) & 3], o = (y * w + x) * 4;
                    d[o] = c ? c[0] : 0; d[o + 1] = c ? c[1] : 0; d[o + 2] = c ? c[2] : 0;
                    d[o + 3] = alpha ? alpha[i] : c ? 255 : 0;
                }
            }
        return out;
    }
    const bytes = bitCount / 8, shift = (m) => (m ? 31 - Math.clz32(m & -m) : 0), size = (m) => (m ? 32 - Math.clz32(m >>> shift(m)) : 0);
    const channel = (v, m) => {
        if (!m) return null;
        const s = size(m), raw = (v & m) >>> shift(m);
        return s >= 8 ? raw >> (s - 8) : Math.round((raw * 255) / ((1 << s) - 1));
    };
    const hasAlpha = (pfFlags & 0x1) !== 0;
    for (let i = 0; i < w * h; ++i, pos += bytes) {
        let v = 0;
        for (let k = bytes - 1; k >= 0; --k) v = v * 256 + b[pos + k];
        v >>>= 0;
        const o = i * 4;
        d[o] = channel(v, masks[0]) ?? 0; d[o + 1] = channel(v, masks[1]) ?? 0; d[o + 2] = channel(v, masks[2]) ?? 0;
        d[o + 3] = hasAlpha ? channel(v, masks[3]) ?? 255 : 255;
    }
    return out;
}

// --- Iconos (Ico de Kaint) ----------------------------------------------------------------

export const IcoSizes = [16, 24, 32, 48, 64, 128, 256];

// Un icono con la imagen en cada tamaño (cuadrada, centrada). El de 256 va comprimido en PNG y
// el resto como mapa de bits de 32 bits.
export async function encodeIco(image, sizes) {
    if (!sizes?.length)
        throw new Error("No hay tamaños que guardar en el icono.");
    const side = Math.max(image.width, image.height), square = newImage(side, side);
    const ox = Math.floor((side - image.width) / 2), oy = Math.floor((side - image.height) / 2);
    for (let y = 0; y < image.height; ++y)
        square.data.set(image.data.subarray(y * image.width * 4, (y + 1) * image.width * 4), ((oy + y) * side + ox) * 4);
    const entries = [], used = [];
    for (let s of sizes) {
        s = Math.min(256, Math.max(1, s));
        if (used.includes(s))
            continue;
        used.push(s);
        const scaled = s === side ? square : scaleImage(new ImageData(square.data, side, side), s, s, "adaptive");
        if (s === 256) {
            entries.push(await encodePng({ width: s, height: s, data: scaled.data }));
            continue;
        }
        const maskStride = Math.ceil(s / 32) * 4, out = new Writer(40 + s * s * 4 + maskStride * s);
        out.u32(40); out.u32(s); out.u32(s * 2); out.u16(1); out.u16(32); out.u32(0); out.u32(s * s * 4 + maskStride * s);
        out.u32(0); out.u32(0); out.u32(0); out.u32(0);
        for (let y = s - 1; y >= 0; --y)
            for (let x = 0; x < s; ++x) {
                const o = (y * s + x) * 4;
                out.u8(scaled.data[o + 2]); out.u8(scaled.data[o + 1]); out.u8(scaled.data[o]); out.u8(scaled.data[o + 3]);
            }
        for (let y = s - 1; y >= 0; --y) {
            const mask = new Uint8Array(maskStride);
            for (let x = 0; x < s; ++x)
                if (scaled.data[(y * s + x) * 4 + 3] === 0)
                    mask[x >> 3] |= 0x80 >> (x & 7);
            out.bytes(mask);
        }
        entries.push(out.result());
    }
    const out = new Writer();
    out.u16(0); out.u16(1); out.u16(entries.length);
    let offset = 6 + 16 * entries.length;
    entries.forEach((e, i) => {
        const s = used[i];
        out.u8(s >= 256 ? 0 : s); out.u8(s >= 256 ? 0 : s); out.u8(0); out.u8(0);
        out.u16(1); out.u16(32); out.u32(e.length); out.u32(offset);
        offset += e.length;
    });
    for (const e of entries)
        out.bytes(e);
    return out.result();
}

// --- GIF (Gif.cpp de Kaint) ---------------------------------------------------------------

export const MIN_DELAY = 20;

// Duración escrita en el nombre de una capa ("(120ms)"); 'fallback' si no hay.
export function delayFromName(name, fallback) {
    const m = /\((\d+)\s*ms\)/i.exec(name || "");
    return m ? Math.max(1, parseInt(m[1], 10)) : fallback;
}

// El nombre con la duración puesta (o cambiada).
export function nameWithDelay(name, delay) {
    return `${String(name).replace(/\s*\(\d+\s*ms\)/gi, "").trim()} (${delay}ms)`;
}

// Compresor LZW de GIF: códigos de longitud variable (hasta 12 bits), desde el bit menos
// significativo y en bloques de 255 bytes.
function gifLzw(out, minCodeSize, data) {
    const clear = 1 << minCodeSize;
    let dict = new Map(), next = clear + 2, codeSize = minCodeSize + 1, bits = 0, bitCount = 0;
    const block = new Uint8Array(255);
    let blockLength = 0;
    const flush = () => {
        if (!blockLength)
            return;
        out.u8(blockLength);
        out.bytes(block.subarray(0, blockLength));
        blockLength = 0;
    };
    const push = (b) => {
        block[blockLength++] = b;
        if (blockLength === 255)
            flush();
    };
    const write = (code) => {
        bits |= code << bitCount;
        bitCount += codeSize;
        while (bitCount >= 8) {
            push(bits & 255);
            bits >>>= 8;
            bitCount -= 8;
        }
    };
    const reset = () => {
        dict = new Map();
        next = clear + 2;
        codeSize = minCodeSize + 1;
    };
    write(clear);
    if (data.length) {
        let prefix = data[0];
        for (let i = 1; i < data.length; ++i) {
            const k = data[i], key = prefix * 256 + k, found = dict.get(key);
            if (found !== undefined) {
                prefix = found;
                continue;
            }
            write(prefix);
            const assigned = next++;
            dict.set(key, assigned);
            if (assigned >= 1 << codeSize && codeSize < 12)
                ++codeSize;
            if (assigned === 4095) {
                write(clear);
                reset();
            }
            prefix = k;
        }
        write(prefix);
    }
    write(clear + 1);
    if (bitCount > 0)
        push(bits & 255);
    flush();
    out.u8(0);
}

// frames: [{ width, height, data, delay }]. options: { loops (0 sin fin, -1 una vez, n más),
// dither, alphaThreshold, quantization, globalPalette, stack }.
export function encodeGif(frames, { loops = 0, dither = 7, alphaThreshold = 128, quantization = 1, globalPalette = false, stack = false } = {}) {
    if (!frames.length)
        throw new Error("No hay fotogramas que guardar.");
    const { width: w, height: h } = frames[0];
    if (w > 65535 || h > 65535)
        throw new Error("El formato GIF no admite imágenes de más de 65535 píxeles.");
    // Si algún fotograma tiene transparencia, todos reservan un color transparente: así los
    // lectores borran el fotograma anterior en vez de dejarlo debajo, como los navegadores.
    const anyTransparent = frames.some((f) => {
        for (let i = 3; i < f.data.length; i += 4)
            if (f.data[i] < alphaThreshold)
                return true;
        return false;
    });
    const slot = anyTransparent && frames.length > 1;
    const quant = { dither, alphaThreshold, transparentSlot: slot, algorithm: quantization };
    const global = globalPalette && frames.length > 1;
    let indexed;
    if (global) {
        const strip = newImage(w, h * frames.length);
        frames.forEach((f, i) => strip.data.set(f.data, i * w * h * 4));
        const all = toIndexed8(strip, quant);
        indexed = frames.map((_, i) => ({ width: w, height: h, indices: all.indices.subarray(i * w * h, (i + 1) * w * h), palette: all.palette }));
    } else {
        indexed = frames.map((f) => toIndexed8(f, quant));
    }
    const bitsFor = (n) => {
        let bits = 1;
        while (1 << bits < n) ++bits;
        return bits;
    };
    const writeTable = (out, table, bits) => {
        for (let i = 0; i < 1 << bits; ++i) {
            const c = table[i] || [0, 0, 0, 0];
            out.u8(c[0]); out.u8(c[1]); out.u8(c[2]);
        }
    };
    const globalTable = global ? indexed[0].palette : [], globalBits = bitsFor(globalTable.length);
    const out = new Writer();
    out.ascii("GIF89a");
    out.u16(w); out.u16(h);
    out.u8(global ? 0xf0 | (globalBits - 1) : 0x70);
    out.u8(0); out.u8(0);
    if (global)
        writeTable(out, globalTable, globalBits);
    if (frames.length > 1 && loops >= 0) {
        out.bytes([0x21, 0xff, 0x0b]);
        out.ascii("NETSCAPE2.0");
        out.bytes([0x03, 0x01]);
        out.u16(Math.min(65535, Math.max(0, loops)));
        out.u8(0);
    }
    frames.forEach((frame, f) => {
        const image = indexed[f], table = image.palette;
        const transparent = table.findIndex((c) => c[3] === 0);
        const sizeBits = global ? globalBits : bitsFor(table.length);
        // Eliminación: 1 deja el fotograma debajo del siguiente (apilar), 2 lo borra.
        out.bytes([0x21, 0xf9, 0x04]);
        out.u8(((stack ? 1 : 2) << 2) | (transparent >= 0 ? 1 : 0));
        out.u16(Math.trunc((Math.max(MIN_DELAY, frame.delay ?? 100) + 5) / 10));
        out.u8(transparent >= 0 ? transparent : 0);
        out.u8(0);
        out.u8(0x2c);
        out.u16(0); out.u16(0); out.u16(w); out.u16(h);
        if (global) {
            out.u8(0);
        } else {
            out.u8(0x80 | (sizeBits - 1));
            writeTable(out, table, sizeBits);
        }
        const minCodeSize = Math.max(2, sizeBits);
        out.u8(minCodeSize);
        gifLzw(out, minCodeSize, image.indices);
    });
    out.u8(0x3b);
    return out.result();
}

// Lee todos los fotogramas ya compuestos (con su duración) → { frames, loops }; loops: 0 sin fin,
// -1 una sola vez, n más.
export function decodeGif(b) {
    if (ascii(b, 0, 3) !== "GIF")
        throw new Error("GIF");
    const w = u16le(b, 6), h = u16le(b, 8), flags = b[10];
    let pos = 13, globalTable = null;
    const readTable = (bits) => {
        const n = 1 << (bits + 1), t = [];
        for (let i = 0; i < n; ++i) t.push([b[pos + i * 3], b[pos + i * 3 + 1], b[pos + i * 3 + 2]]);
        pos += n * 3;
        return t;
    };
    if (flags & 0x80)
        globalTable = readTable(flags & 7);
    const canvas = new Uint8ClampedArray(w * h * 4), frames = [];
    let loops = -1, gce = null;
    const subBlocks = () => {
        const parts = [];
        let total = 0;
        while (pos < b.length) {
            const n = b[pos++];
            if (!n) break;
            parts.push(b.subarray(pos, pos + n));
            total += n;
            pos += n;
        }
        const data = new Uint8Array(total);
        let o = 0;
        for (const p of parts) {
            data.set(p, o);
            o += p.length;
        }
        return data;
    };
    while (pos < b.length) {
        const code = b[pos++];
        if (code === 0x3b)
            break;
        if (code === 0x21) {
            const label = b[pos++];
            if (label === 0xf9) {
                const block = subBlocks();
                gce = { disposal: (block[0] >> 2) & 7, transparent: block[0] & 1 ? block[3] : -1, delay: u16le(block, 1) * 10 };
            } else if (label === 0xff) {
                const id = ascii(b, pos + 1, 11);
                const block = subBlocks();
                if (id.startsWith("NETSCAPE") && block.length >= 14 && block[11] === 1)
                    loops = u16le(block, 12);
            } else {
                subBlocks();
            }
            continue;
        }
        if (code !== 0x2c)
            break;
        const fx = u16le(b, pos), fy = u16le(b, pos + 2), fw = u16le(b, pos + 4), fh = u16le(b, pos + 6), fflags = b[pos + 8];
        pos += 9;
        const table = fflags & 0x80 ? readTable(fflags & 7) : globalTable || [];
        const interlaced = (fflags & 0x40) !== 0;
        const minCode = b[pos++];
        const indices = gifLzwDecode(subBlocks(), minCode, fw * fh);
        const g = gce || { disposal: 0, transparent: -1, delay: 0 };
        gce = null;
        const before = g.disposal === 3 ? canvas.slice() : null;
        // Filas en el orden del entrelazado.
        const rows = [];
        if (interlaced)
            for (const [start, step] of [[0, 8], [4, 8], [2, 4], [1, 2]])
                for (let y = start; y < fh; y += step) rows.push(y);
        else
            for (let y = 0; y < fh; ++y) rows.push(y);
        rows.forEach((ry, i) => {
            const y = fy + ry;
            if (y >= h)
                return;
            for (let x = 0; x < fw; ++x) {
                const k = indices[i * fw + x];
                if (k === g.transparent || fx + x >= w)
                    continue;
                const c = table[k] || [0, 0, 0], o = (y * w + fx + x) * 4;
                canvas[o] = c[0]; canvas[o + 1] = c[1]; canvas[o + 2] = c[2]; canvas[o + 3] = 255;
            }
        });
        frames.push({ width: w, height: h, data: canvas.slice(), delay: g.delay > 0 ? g.delay : 100 });
        if (g.disposal === 2) {
            for (let y = fy; y < Math.min(h, fy + fh); ++y)
                canvas.fill(0, (y * w + fx) * 4, (y * w + Math.min(w, fx + fw)) * 4);
        } else if (g.disposal === 3 && before) {
            canvas.set(before);
        }
    }
    if (!frames.length)
        throw new Error("GIF");
    return { frames, loops: loops < 0 ? -1 : loops };
}

function gifLzwDecode(data, minCode, count) {
    const out = new Uint8Array(count), clear = 1 << minCode, eoi = clear + 1;
    let size = minCode + 1, mask = (1 << size) - 1, next = clear + 2, prev = -1, o = 0, bits = 0, bitCount = 0, pos = 0;
    const prefix = new Int32Array(4096), suffix = new Uint8Array(4096), stack = new Uint8Array(4097);
    for (let i = 0; i < clear; ++i) suffix[i] = i;
    let first = 0;
    while (o < count) {
        while (bitCount < size && pos < data.length) {
            bits |= data[pos++] << bitCount;
            bitCount += 8;
        }
        if (bitCount < size)
            break;
        const code = bits & mask;
        bits >>>= size;
        bitCount -= size;
        if (code === clear) {
            size = minCode + 1;
            mask = (1 << size) - 1;
            next = clear + 2;
            prev = -1;
            continue;
        }
        if (code === eoi)
            break;
        if (prev < 0) {
            out[o++] = suffix[code];
            first = suffix[code];
            prev = code;
            continue;
        }
        let c = code, sp = 0;
        if (code >= next) {
            stack[sp++] = first;
            c = prev;
        }
        while (c >= clear) {
            stack[sp++] = suffix[c];
            c = prefix[c];
        }
        first = suffix[c];
        stack[sp++] = first;
        while (sp > 0 && o < count) out[o++] = stack[--sp];
        if (next < 4096) {
            prefix[next] = prev;
            suffix[next] = first;
            ++next;
            if ((next & mask) === 0 && next < 4096) {
                ++size;
                mask = (1 << size) - 1;
            }
        }
        prev = code;
    }
    return out;
}

// Efectos de la configuración avanzada del GIF (como los de ezgif).
export function applyGifEffects(frames, effects = {}) {
    if (frames.length < 2)
        return frames;
    frames = frames.map((f) => ({ ...f }));
    const premul = (d) => {
        const p = new Uint8ClampedArray(d.length);
        for (let i = 0; i < d.length; i += 4) {
            const a = d[i + 3];
            p[i] = (d[i] * a + 127) / 255; p[i + 1] = (d[i + 1] * a + 127) / 255; p[i + 2] = (d[i + 2] * a + 127) / 255; p[i + 3] = a;
        }
        return p;
    };
    const unpremul = (p) => {
        const d = new Uint8ClampedArray(p.length);
        for (let i = 0; i < p.length; i += 4) {
            const a = p[i + 3];
            if (a) {
                d[i] = Math.min(255, Math.round((p[i] * 255) / a)); d[i + 1] = Math.min(255, Math.round((p[i + 1] * 255) / a));
                d[i + 2] = Math.min(255, Math.round((p[i + 2] * 255) / a));
            }
            d[i + 3] = a;
        }
        return d;
    };
    // El primero, debajo de los demás.
    if (effects.firstAsBackground) {
        const bg = frames[0].data;
        for (let i = 1; i < frames.length; ++i) {
            const top = frames[i].data, out = new Uint8ClampedArray(top.length);
            for (let k = 0; k < top.length; k += 4) {
                const sa = top[k + 3] / 255, da = bg[k + 3] / 255, oa = sa + da * (1 - sa);
                for (let c = 0; c < 3; ++c)
                    out[k + c] = oa > 0 ? (top[k + c] * sa + bg[k + c] * da * (1 - sa)) / oa : 0;
                out[k + 3] = Math.round(oa * 255);
            }
            frames[i] = { ...frames[i], data: out };
        }
        if (effects.firstOnlyAsBackground)
            frames.shift();
    }
    if (!effects.crossfade || frames.length < 2)
        return frames;
    // Fundidos intermedios (mezcla premultiplicada: también de transparente a opaco).
    const steps = Math.min(20, Math.max(1, effects.fadeFrames ?? 10)), delay = Math.max(MIN_DELAY, effects.fadeDelay ?? 60);
    const out = [], n = frames.length;
    for (let i = 0; i < n; ++i) {
        out.push(frames[i]);
        if (i === n - 1 && effects.fadeLastToFirst === false)
            break;
        const a = premul(frames[i].data), b = premul(frames[(i + 1) % n].data);
        for (let k = 1; k <= steps; ++k) {
            const t = Math.trunc((256 * k) / (steps + 1)), mixed = new Uint8ClampedArray(a.length);
            for (let p = 0; p < a.length; ++p)
                mixed[p] = (a[p] * (256 - t) + b[p] * t) >> 8;
            out.push({ width: frames[i].width, height: frames[i].height, data: unpremul(mixed), delay });
        }
    }
    return out;
}

// --- Paint.NET (.pdn, Pdn.cpp de Kaint) ---------------------------------------------------

class NrbfError extends Error {}

// Lector de la serialización binaria de .NET (MS-NRBF) con lo que usan los .pdn.
class Nrbf {
    constructor(data, pos) {
        this.d = data;
        this.pos = pos;
        this.view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        this.root = 1;
        this.end = false;
        this.nulls = 0;
        this.classes = new Map();
        this.objects = new Map();
        this.blocks = [];
    }

    need(n) {
        if (this.pos + n > this.d.length)
            throw new NrbfError("El archivo está incompleto.");
    }
    u8() { this.need(1); return this.d[this.pos++]; }
    i16() { this.need(2); const v = this.view.getInt16(this.pos, true); this.pos += 2; return v; }
    i32() { this.need(4); const v = this.view.getInt32(this.pos, true); this.pos += 4; return v; }
    u32() { this.need(4); const v = this.view.getUint32(this.pos, true); this.pos += 4; return v; }
    i64() { this.need(8); const v = Number(this.view.getBigInt64(this.pos, true)); this.pos += 8; return v; }
    f32() { this.need(4); const v = this.view.getFloat32(this.pos, true); this.pos += 4; return v; }
    f64() { this.need(8); const v = this.view.getFloat64(this.pos, true); this.pos += 8; return v; }
    string() {
        let length = 0;
        for (let shift = 0; shift < 35; shift += 7) {
            const b = this.u8();
            length |= (b & 0x7f) << shift;
            if (!(b & 0x80))
                break;
        }
        this.need(length);
        const s = new TextDecoder().decode(this.d.subarray(this.pos, this.pos + length));
        this.pos += length;
        return s;
    }

    primitive(type) {
        switch (type) {
        case 1: return !!this.u8();
        case 2: return this.u8();
        case 3: return String.fromCharCode(this.u8());
        case 5: return this.string();
        case 6: return this.f64();
        case 7: return this.i16();
        case 8: return this.i32();
        case 9: case 12: case 13: case 16: return this.i64();
        case 10: { const v = this.u8(); return v > 127 ? v - 256 : v; }
        case 11: return this.f32();
        case 14: { this.need(2); const v = this.view.getUint16(this.pos, true); this.pos += 2; return v; }
        case 15: return this.u32();
        case 17: return null;
        case 18: return this.string();
        default: throw new NrbfError(`Tipo primitivo desconocido (${type}).`);
        }
    }

    memberTypes(c, count) {
        c.binaryTypes = [];
        c.extra = [];
        for (let i = 0; i < count; ++i) c.binaryTypes.push(this.u8());
        for (let i = 0; i < count; ++i) {
            const bt = c.binaryTypes[i];
            if (bt === 0 || bt === 7) c.extra[i] = this.u8();
            else if (bt === 3) this.string();
            else if (bt === 4) { this.string(); this.i32(); }
        }
    }

    objectFrom(id, c) {
        const obj = { id, cls: c.name, members: new Map() };
        this.objects.set(id, obj);
        if (c.name === "PaintDotNet.MemoryBlock")
            this.blocks.push(obj);
        c.members.forEach((name, i) => obj.members.set(name, c.binaryTypes[i] === 0 ? this.primitive(c.extra[i]) : this.record()));
        return obj;
    }

    objectArray(id, count) {
        const arr = [];
        this.objects.set(id, arr);
        while (arr.length < count) {
            this.nulls = 0;
            const v = this.record();
            if (this.nulls > 0) {
                for (let i = 0; i < this.nulls && arr.length < count; ++i) arr.push(null);
                this.nulls = 0;
            } else {
                arr.push(v);
            }
        }
        return arr;
    }

    record() {
        const type = this.u8();
        switch (type) {
        case 0:
            this.root = this.i32();
            this.i32(); this.i32(); this.i32();
            return null;
        case 12:
            this.i32();
            this.string();
            return this.record();
        case 1: {
            const id = this.i32(), meta = this.i32(), c = this.classes.get(meta);
            if (!c)
                throw new NrbfError("Referencia a una clase desconocida.");
            this.classes.set(id, c);
            return this.objectFrom(id, c);
        }
        case 4:
        case 5: {
            const id = this.i32(), c = { name: this.string(), members: [] }, count = this.i32();
            for (let i = 0; i < count; ++i) c.members.push(this.string());
            this.memberTypes(c, count);
            if (type === 5)
                this.i32();
            this.classes.set(id, c);
            return this.objectFrom(id, c);
        }
        case 6: {
            const id = this.i32(), s = this.string();
            this.objects.set(id, s);
            return s;
        }
        case 7: {
            const id = this.i32(), arrayType = this.u8(), rank = this.i32();
            let count = 1;
            for (let i = 0; i < rank; ++i) count *= this.i32();
            if (arrayType >= 3 && arrayType <= 5)
                for (let i = 0; i < rank; ++i) this.i32();
            const bt = this.u8();
            let prim = 0;
            if (bt === 0 || bt === 7) prim = this.u8();
            else if (bt === 3) this.string();
            else if (bt === 4) { this.string(); this.i32(); }
            if (bt === 0) {
                const arr = [];
                for (let i = 0; i < count; ++i) arr.push(this.primitive(prim));
                this.objects.set(id, arr);
                return arr;
            }
            return this.objectArray(id, count);
        }
        case 8: return this.primitive(this.u8());
        case 9: return { ref: this.i32() };
        case 10: return null;
        case 11: this.end = true; return null;
        case 13: this.nulls = this.u8(); return null;
        case 14: this.nulls = this.i32(); return null;
        case 15: {
            const id = this.i32(), count = this.i32(), t = this.u8();
            if (t === 2) {
                this.need(count);
                const bytes = this.d.subarray(this.pos, this.pos + count);
                this.pos += count;
                this.objects.set(id, bytes);
                return bytes;
            }
            const arr = [];
            for (let i = 0; i < count; ++i) arr.push(this.primitive(t));
            this.objects.set(id, arr);
            return arr;
        }
        case 16:
        case 17: {
            const id = this.i32(), count = this.i32();
            return this.objectArray(id, count);
        }
        default:
            throw new NrbfError(`Registro desconocido (${type}) en la serialización.`);
        }
    }

    parse() {
        while (!this.end)
            this.record();
        return this.deref({ ref: this.root });
    }

    deref(v) {
        return v && typeof v === "object" && "ref" in v ? this.objects.get(v.ref) ?? null : v;
    }

    member(o, name) {
        return o && o.members ? this.deref(o.members.get(name)) : null;
    }
}

const integer = (v, def = 0) => (typeof v === "number" ? Math.trunc(v) : typeof v === "boolean" ? Number(v) : def);

// Datos diferidos de un MemoryBlock: versión (0 = gzip, 1 = sin comprimir), tamaño de bloque y
// los bloques numerados, en cualquier orden.
async function readDeferred(d, state, length) {
    if (state.pos + 5 > d.length)
        throw new NrbfError("Faltan los píxeles de una capa.");
    const format = d[state.pos], chunkSize = u32be(d, state.pos + 1);
    state.pos += 5;
    if ((format !== 0 && format !== 1) || chunkSize === 0)
        throw new NrbfError("Formato de píxeles desconocido.");
    const count = Math.ceil(length / chunkSize), out = new Uint8Array(length), jobs = [];
    for (let i = 0; i < count; ++i) {
        if (state.pos + 8 > d.length)
            throw new NrbfError("Faltan los píxeles de una capa.");
        const number = u32be(d, state.pos), size = u32be(d, state.pos + 4);
        state.pos += 8;
        if (number >= count || state.pos + size > d.length)
            throw new NrbfError("Los píxeles de una capa están dañados.");
        const chunk = d.subarray(state.pos, state.pos + size), offset = number * chunkSize, expected = Math.min(chunkSize, length - offset);
        state.pos += size;
        if (format === 1) {
            out.set(chunk.subarray(0, expected), offset);
            continue;
        }
        jobs.push(gunzip(chunk).then((raw) => {
            if (raw.length !== expected)
                throw new NrbfError("No se pudieron descomprimir los píxeles de una capa.");
            out.set(raw, offset);
        }, () => {
            throw new NrbfError("No se pudieron descomprimir los píxeles de una capa.");
        }));
    }
    await Promise.all(jobs);
    return out;
}

const PDN_BLENDS = ["normal", "multiply", "additive", "colorburn", "colordodge", "reflect", "glow", "overlay", "difference",
                    "negation", "lighten", "darken", "screen", "xor"];

function blendFromPdn(n, layer, props) {
    // Paint.NET 4/5: LayerProperties.blendMode (enum LayerBlendMode).
    const mode = n.member(props, "blendMode");
    if (mode && mode.members) {
        const i = integer(n.member(mode, "value__"), -1);
        if (i >= 0 && i < PDN_BLENDS.length)
            return PDN_BLENDS[i];
    }
    const op = n.member(n.member(layer, "properties"), "blendOp");
    const m = op?.cls && /\+(\w+)BlendOp$/.exec(op.cls);
    const names = { Normal: "normal", Multiply: "multiply", Additive: "additive", ColorBurn: "colorburn", ColorDodge: "colordodge",
                    Reflect: "reflect", Glow: "glow", Overlay: "overlay", Difference: "difference", Negation: "negation",
                    Lighten: "lighten", Darken: "darken", Screen: "screen", Xor: "xor" };
    return (m && names[m[1]]) || "normal";
}

// Una imagen de Paint.NET → { width, height, layers: [{ name, visible, opacity, blend, data }] }
// (la de abajo primero).
export async function decodePdn(bytes) {
    let data = bytes, pos = 0;
    try {
        if (ascii(data, 0, 4) === "PDN3") {
            if (data.length < 7)
                throw new NrbfError("El archivo está incompleto.");
            pos = 7 + (data[4] | (data[5] << 8) | (data[6] << 16));
        }
        if (pos + 2 > data.length)
            throw new NrbfError("El archivo está incompleto.");
        if (data[pos] === 0x00 && data[pos + 1] === 0x01) {
            pos += 2;
        } else if (data[pos] === 0x1f && data[pos + 1] === 0x8b) {
            data = await gunzip(data.subarray(pos)).catch(() => {
                throw new NrbfError("No se pudo descomprimir el archivo.");
            });
            pos = 0;
        } else {
            throw new NrbfError("No es una imagen de Paint.NET válida.");
        }
        const n = new Nrbf(data, pos), doc = n.parse();
        if (!doc?.cls?.endsWith("Document"))
            throw new NrbfError("No es una imagen de Paint.NET válida.");
        const width = integer(n.member(doc, "width")), height = integer(n.member(doc, "height"));
        if (width <= 0 || height <= 0 || width * height > 2 ** 31)
            throw new NrbfError("Tamaño de imagen no válido.");
        const state = { pos: n.pos }, pixels = new Map();
        for (const block of n.blocks) {
            const length = integer(n.member(block, "length64"));
            if (integer(n.member(block, "hasParent")))
                continue;
            if (integer(n.member(block, "deferred")))
                pixels.set(block.id, await readDeferred(data, state, length));
            else if (n.member(block, "pointerData") instanceof Uint8Array)
                pixels.set(block.id, n.member(block, "pointerData"));
        }
        const list = n.member(doc, "layers"), items = n.member(list, "ArrayList+_items"), count = integer(n.member(list, "ArrayList+_size"));
        if (!Array.isArray(items) || count <= 0)
            throw new NrbfError("La imagen no tiene capas.");
        const layers = [];
        for (let i = 0; i < count && i < items.length; ++i) {
            const layer = n.deref(items[i]), props = n.member(layer, "Layer+properties"), surface = n.member(layer, "surface");
            const block = n.member(surface, "scan0");
            if (!layer || !surface || !block || !pixels.has(block.id))
                throw new NrbfError("Una capa no tiene píxeles (¿capa de un tipo desconocido?).");
            const w = integer(n.member(surface, "width")), h = integer(n.member(surface, "height")), stride = integer(n.member(surface, "stride"));
            const px = pixels.get(block.id);
            if (w !== width || h !== height || stride < w * 4 || stride * h > px.length)
                throw new NrbfError("Las dimensiones de una capa no coinciden con las de la imagen.");
            // ColorBgra → RGBA.
            const out = new Uint8ClampedArray(w * h * 4);
            for (let y = 0; y < h; ++y)
                for (let x = 0, s = y * stride, o = y * w * 4; x < w; ++x, s += 4, o += 4) {
                    out[o] = px[s + 2]; out[o + 1] = px[s + 1]; out[o + 2] = px[s]; out[o + 3] = px[s + 3];
                }
            const name = n.member(props, "name");
            layers.push({
                name: typeof name === "string" && name ? name : null, visible: integer(n.member(props, "visible"), 1) !== 0,
                opacity: Math.min(255, Math.max(0, integer(n.member(props, "opacity"), 255))), blend: blendFromPdn(n, layer, props), data: out,
            });
        }
        if (!layers.length)
            throw new NrbfError("La imagen no tiene capas.");
        return { width, height, layers };
    } catch (e) {
        if (e instanceof NrbfError)
            throw e;
        throw new NrbfError(String(e?.message || e));
    }
}
