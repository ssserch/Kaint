// Perfiles de color (QColorSpace de Kaint): los que ofrece Paint.NET (sRGB, Adobe RGB, Display P3
// y ProPhoto RGB), los de matriz y curvas de un archivo .icc o de una imagen, escribirlos (ICC v4)
// y convertir los colores de uno a otro. Un perfil es { name, matrix (RGB lineal → XYZ D50),
// curves: [rojo, verde, azul], icc }.

const D50 = [0.9642, 1, 0.8249];

// --- Álgebra -----------------------------------------------------------------------------

const mul = (a, b) => a.map((row) => [0, 1, 2].map((j) => row[0] * b[0][j] + row[1] * b[1][j] + row[2] * b[2][j]));
const apply = (m, v) => m.map((row) => row[0] * v[0] + row[1] * v[1] + row[2] * v[2]);
function invert(m) {
    const [[a, b, c], [d, e, f], [g, h, i]] = m;
    const A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g, det = a * A + b * B + c * C;
    if (Math.abs(det) < 1e-12)
        return null;
    return [[A / det, -(b * i - c * h) / det, (b * f - c * e) / det],
            [B / det, (a * i - c * g) / det, -(a * f - c * d) / det],
            [C / det, -(a * h - b * g) / det, (a * e - b * d) / det]];
}
const xyToXYZ = ([x, y]) => [x / y, 1, (1 - x - y) / y];

// Adaptación cromática de Bradford de un blanco a otro.
const BRADFORD = [[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]];
function adaptation(from, to) {
    const s = apply(BRADFORD, from), d = apply(BRADFORD, to);
    return mul(mul(invert(BRADFORD), [[d[0] / s[0], 0, 0], [0, d[1] / s[1], 0], [0, 0, d[2] / s[2]]]), BRADFORD);
}

// RGB lineal → XYZ D50 a partir de los primarios y el blanco (xy).
function matrixFromPrimaries(r, g, b, white) {
    const P = [xyToXYZ(r), xyToXYZ(g), xyToXYZ(b)], W = xyToXYZ(white);
    const cols = [[P[0][0], P[1][0], P[2][0]], [P[0][1], P[1][1], P[2][1]], [P[0][2], P[1][2], P[2][2]]];
    const S = apply(invert(cols), W);
    const m = cols.map((row) => row.map((v, j) => v * S[j]));
    return mul(adaptation(W, D50), m);
}

// --- Curvas de tono ----------------------------------------------------------------------

// Función paramétrica de ICC (tipo 4): (a·x + b)^g + e si x ≥ d; si no, c·x + f.
const para = (g, a = 1, b = 0, c = 0, d = 0, e = 0, f = 0) => ({ type: "para", g, a, b, c, d, e, f });
const SRGB_TRC = para(2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045);

function evaluate(curve, x) {
    if (curve.type === "table") {
        const t = curve.values, p = Math.min(1, Math.max(0, x)) * (t.length - 1), i = Math.floor(p), f = p - i;
        return i >= t.length - 1 ? t[t.length - 1] : t[i] + (t[i + 1] - t[i]) * f;
    }
    const { g, a, b, c, d, e, f } = curve;
    return x >= d ? Math.pow(Math.max(0, a * x + b), g) + e : c * x + f;
}

// --- Perfiles con nombre -----------------------------------------------------------------

function named(name, r, g, b, white, curve) {
    const profile = { name, matrix: matrixFromPrimaries(r, g, b, white), curves: [curve, curve, curve], white };
    profile.icc = writeIcc(profile);
    return profile;
}

const D65 = [0.3127, 0.329];
export const NamedProfiles = [
    named("sRGB IEC61966-2.1", [0.64, 0.33], [0.3, 0.6], [0.15, 0.06], D65, SRGB_TRC),
    named("Adobe RGB", [0.64, 0.33], [0.21, 0.71], [0.15, 0.06], D65, para(563 / 256)),
    named("Display P3", [0.68, 0.32], [0.265, 0.69], [0.15, 0.06], D65, SRGB_TRC),
    named("ProPhoto RGB", [0.7347, 0.2653], [0.1596, 0.8404], [0.0366, 0.0001], [0.3457, 0.3585], para(1.8, 1, 0, 1 / 16, 1 / 512)),
];
export const SRGB = NamedProfiles[0];

// Mismos colores (matriz y curvas casi iguales).
export function sameProfile(a, b) {
    a ||= SRGB;
    b ||= SRGB;
    if (a === b)
        return true;
    for (let i = 0; i < 3; ++i)
        for (let j = 0; j < 3; ++j)
            if (Math.abs(a.matrix[i][j] - b.matrix[i][j]) > 2e-3)
                return false;
    for (let c = 0; c < 3; ++c)
        for (let k = 0; k <= 16; ++k)
            if (Math.abs(evaluate(a.curves[c], k / 16) - evaluate(b.curves[c], k / 16)) > 2e-3)
                return false;
    return true;
}

export const isSrgb = (p) => sameProfile(p, SRGB);

// El nombre que se muestra (ColorProfileDialog::describe).
export function describe(p) {
    if (!p)
        return "Ninguno (sRGB)";
    if (sameProfile(p, SRGB))
        return SRGB.name;
    return p.name || NamedProfiles.find((n) => sameProfile(n, p))?.name || "Perfil personalizado";
}

// --- Conversión ----------------------------------------------------------------------------

// Convierte los píxeles RGBA (alfa directo, en su sitio) del perfil 'from' al perfil 'to'.
export function transformPixels(data, from, to) {
    from ||= SRGB;
    to ||= SRGB;
    if (sameProfile(from, to))
        return data;
    const m = mul(invert(to.matrix), from.matrix);
    const lin = from.curves.map((curve) => Float32Array.from({ length: 256 }, (_, i) => evaluate(curve, i / 255)));
    // Inversa de cada curva de destino en 4096 pasos (búsqueda en la curva, que es monótona).
    const SIZE = 4096;
    const enc = to.curves.map((curve) => {
        const samples = Float64Array.from({ length: 1024 }, (_, i) => evaluate(curve, i / 1023));
        const out = new Uint8Array(SIZE + 1);
        let k = 0;
        for (let i = 0; i <= SIZE; ++i) {
            const y = i / SIZE;
            while (k < 1022 && samples[k + 1] < y)
                ++k;
            const y0 = samples[k], y1 = samples[k + 1], t = y1 > y0 ? Math.min(1, Math.max(0, (y - y0) / (y1 - y0))) : 0;
            out[i] = Math.round(((k + t) / 1023) * 255);
        }
        return out;
    });
    const encode = (c, v) => enc[c][Math.round(Math.min(1, Math.max(0, v)) * SIZE)];
    for (let i = 0; i < data.length; i += 4) {
        const r = lin[0][data[i]], g = lin[1][data[i + 1]], b = lin[2][data[i + 2]];
        data[i] = encode(0, m[0][0] * r + m[0][1] * g + m[0][2] * b);
        data[i + 1] = encode(1, m[1][0] * r + m[1][1] * g + m[1][2] * b);
        data[i + 2] = encode(2, m[2][0] * r + m[2][1] * g + m[2][2] * b);
    }
    return data;
}

// --- ICC ----------------------------------------------------------------------------------

const s15 = (b, i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) / 65536;
const u32 = (b, i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
const sig = (b, i) => String.fromCharCode(b[i], b[i + 1], b[i + 2], b[i + 3]);

// Lee un perfil RGB de matriz y curvas (el que entiende Kaint); null si no lo es.
export function readIcc(bytes) {
    try {
        const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
        if (b.length < 132 || sig(b, 36) !== "acsp" || sig(b, 16) !== "RGB " || sig(b, 20) !== "XYZ ")
            return null;
        const count = u32(b, 128), tags = {};
        for (let i = 0; i < count; ++i) {
            const at = 132 + i * 12;
            tags[sig(b, at)] = { offset: u32(b, at + 4), size: u32(b, at + 8) };
        }
        const xyz = (name) => {
            const t = tags[name];
            return t && sig(b, t.offset) === "XYZ " ? [s15(b, t.offset + 8), s15(b, t.offset + 12), s15(b, t.offset + 16)] : null;
        };
        const curve = (name) => {
            const t = tags[name];
            if (!t)
                return null;
            const o = t.offset, type = sig(b, o);
            if (type === "curv") {
                const n = u32(b, o + 8);
                if (n === 0)
                    return para(1);
                if (n === 1)
                    return para(((b[o + 12] << 8) | b[o + 13]) / 256);
                const values = new Float32Array(n);
                for (let k = 0; k < n; ++k)
                    values[k] = ((b[o + 12 + k * 2] << 8) | b[o + 13 + k * 2]) / 65535;
                return { type: "table", values };
            }
            if (type === "para") {
                const fn = (b[o + 8] << 8) | b[o + 9], p = (k) => s15(b, o + 12 + k * 4);
                switch (fn) {
                case 0: return para(p(0));
                case 1: return para(p(0), p(1), p(2), 0, -p(2) / p(1));
                case 2: return para(p(0), p(1), p(2), 0, -p(2) / p(1), p(3), p(3));
                case 3: return para(p(0), p(1), p(2), p(3), p(4));
                case 4: return para(p(0), p(1), p(2), p(3), p(4), p(5), p(6));
                default: return null;
                }
            }
            return null;
        };
        const r = xyz("rXYZ"), g = xyz("gXYZ"), bl = xyz("bXYZ");
        const curves = [curve("rTRC"), curve("gTRC"), curve("bTRC")];
        if (!r || !g || !bl || curves.some((c) => !c))
            return null;
        const matrix = [[r[0], g[0], bl[0]], [r[1], g[1], bl[1]], [r[2], g[2], bl[2]]];
        if (!invert(matrix))
            return null;
        return { name: readDescription(b, tags.desc), matrix, curves, icc: b.slice() };
    } catch {
        return null;
    }
}

function readDescription(b, tag) {
    if (!tag)
        return "";
    const o = tag.offset, type = sig(b, o);
    if (type === "desc") {
        const n = u32(b, o + 8);
        return String.fromCharCode(...b.subarray(o + 12, o + 12 + Math.max(0, n - 1))).trim();
    }
    if (type === "mluc" && u32(b, o + 8) > 0) {
        const len = u32(b, o + 20), off = u32(b, o + 24);
        let s = "";
        for (let k = 0; k + 1 < len; k += 2)
            s += String.fromCharCode((b[o + off + k] << 8) | b[o + off + k + 1]);
        return s.trim();
    }
    return "";
}

// Perfil ICC v4 de matriz y curvas (como QColorSpace::iccProfile).
export function writeIcc(profile) {
    const tags = [];
    const fixed = (v) => {
        const x = Math.round(v * 65536);
        return [(x >>> 24) & 255, (x >>> 16) & 255, (x >>> 8) & 255, x & 255];
    };
    const be32 = (v) => [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
    const text = (s) => {
        const out = [...[..."mluc"].map((c) => c.charCodeAt(0)), 0, 0, 0, 0, ...be32(1), ...be32(12), ...[..."enUS"].map((c) => c.charCodeAt(0)),
                     ...be32(s.length * 2), ...be32(28)];
        for (const ch of s)
            out.push(ch.charCodeAt(0) >> 8, ch.charCodeAt(0) & 255);
        return out;
    };
    const xyzTag = (v) => [..."XYZ ".split("").map((c) => c.charCodeAt(0)), 0, 0, 0, 0, ...fixed(v[0]), ...fixed(v[1]), ...fixed(v[2])];
    const curveTag = (c) => {
        if (c.type === "table") {
            const out = [..."curv".split("").map((x) => x.charCodeAt(0)), 0, 0, 0, 0, ...be32(c.values.length)];
            for (const v of c.values) {
                const u = Math.round(v * 65535);
                out.push(u >> 8, u & 255);
            }
            return out;
        }
        return [..."para".split("").map((x) => x.charCodeAt(0)), 0, 0, 0, 0, 0, 4, 0, 0, ...[c.g, c.a, c.b, c.c, c.d, c.e, c.f].flatMap(fixed)];
    };
    const m = profile.matrix;
    tags.push(["desc", text(profile.name || "Perfil personalizado")], ["cprt", text("No copyright")], ["wtpt", xyzTag(D50)],
              ["rXYZ", xyzTag([m[0][0], m[1][0], m[2][0]])], ["gXYZ", xyzTag([m[0][1], m[1][1], m[2][1]])], ["bXYZ", xyzTag([m[0][2], m[1][2], m[2][2]])]);
    const same = profile.curves.every((c) => c === profile.curves[0]);
    const trc = profile.curves.map(curveTag);
    tags.push(["rTRC", trc[0]], ["gTRC", same ? trc[0] : trc[1]], ["bTRC", same ? trc[0] : trc[2]]);
    if (profile.white) {
        const a = adaptation(xyToXYZ(profile.white), D50);
        tags.push(["chad", [..."sf32".split("").map((x) => x.charCodeAt(0)), 0, 0, 0, 0, ...a.flat().flatMap(fixed)]]);
    }
    // Las curvas iguales comparten los mismos datos.
    let offset = 128 + 4 + tags.length * 12;
    const table = [], data = [], placed = new Map();
    for (const [name, bytes] of tags) {
        let at = placed.get(bytes);
        if (at === undefined) {
            at = offset;
            placed.set(bytes, at);
            data.push(...bytes);
            while (data.length % 4) data.push(0);
            offset = 128 + 4 + tags.length * 12 + data.length;
        }
        table.push(...[...name].map((c) => c.charCodeAt(0)), ...be32(at), ...be32(bytes.length));
    }
    const size = 128 + 4 + table.length + data.length;
    const header = new Array(128).fill(0);
    header.splice(0, 4, ...be32(size));
    header.splice(8, 4, 4, 0x30, 0, 0);
    header.splice(12, 4, ..."mntr".split("").map((c) => c.charCodeAt(0)));
    header.splice(16, 4, ..."RGB ".split("").map((c) => c.charCodeAt(0)));
    header.splice(20, 4, ..."XYZ ".split("").map((c) => c.charCodeAt(0)));
    header.splice(36, 4, ..."acsp".split("").map((c) => c.charCodeAt(0)));
    header.splice(68, 12, ...fixed(D50[0]), ...fixed(D50[1]), ...fixed(D50[2]));
    return Uint8Array.from([...header, ...be32(tags.length), ...table, ...data]);
}

// --- Perfiles en los archivos de imagen ----------------------------------------------------

// El perfil ICC incrustado en un PNG (iCCP), JPEG (APP2), WebP (ICCP) o TIFF; null si no hay.
export async function iccFromImage(bytes, inflate) {
    const b = bytes;
    if (b[0] === 0x89 && b[1] === 0x50) {
        for (let i = 8; i + 8 <= b.length;) {
            const len = u32(b, i), type = sig(b, i + 4);
            if (type === "iCCP") {
                let k = i + 8;
                while (b[k] !== 0 && k < i + 8 + 80) ++k;
                return inflate(b.subarray(k + 2, i + 8 + len));
            }
            if (type === "IDAT")
                break;
            i += 12 + len;
        }
        return null;
    }
    if (b[0] === 0xff && b[1] === 0xd8) {
        const parts = [];
        for (let i = 2; i + 4 <= b.length && b[i] === 0xff;) {
            const marker = b[i + 1], len = (b[i + 2] << 8) | b[i + 3];
            if (marker === 0xda)
                break;
            if (marker === 0xe2 && String.fromCharCode(...b.subarray(i + 4, i + 15)) === "ICC_PROFILE")
                parts[b[i + 16] - 1] = b.subarray(i + 18, i + 2 + len);
            i += 2 + len;
        }
        if (!parts.length || parts.some((p) => !p))
            return null;
        const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
        let o = 0;
        for (const p of parts) {
            out.set(p, o);
            o += p.length;
        }
        return out;
    }
    if (sig(b, 0) === "RIFF" && sig(b, 8) === "WEBP") {
        for (let i = 12; i + 8 <= b.length;) {
            const len = b[i + 4] | (b[i + 5] << 8) | (b[i + 6] << 16) | (b[i + 7] << 24);
            if (sig(b, i) === "ICCP")
                return b.slice(i + 8, i + 8 + len);
            i += 8 + len + (len & 1);
        }
        return null;
    }
    if ((b[0] === 0x49 && b[1] === 0x49) || (b[0] === 0x4d && b[1] === 0x4d)) {
        const le = b[0] === 0x49, r16 = (i) => (le ? b[i] | (b[i + 1] << 8) : (b[i] << 8) | b[i + 1]);
        const r32 = (i) => (le ? (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)) >>> 0 : u32(b, i));
        const ifd = r32(4), n = r16(ifd);
        for (let k = 0; k < n; ++k) {
            const e = ifd + 2 + k * 12;
            if (r16(e) === 34675)
                return b.slice(r32(e + 8), r32(e + 8) + r32(e + 4));
        }
    }
    return null;
}
