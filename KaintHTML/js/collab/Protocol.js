// Protocolo del modo colaborativo, el mismo que Kaint (src/app/collab/CollabProtocol.cpp y
// docs/collaboration.md). Sin dependencias del navegador al cargar: el servidor de
// tools/collab-server.mjs (Node) también lo usa.

export const VERSION = 1;
export const DEFAULT_PORT = 47913;
export const BEACON_PORT = 47914;

export const BlendNames = ["normal", "multiply", "additive", "colorburn", "colordodge", "reflect", "glow", "overlay",
                           "difference", "negation", "lighten", "darken", "screen", "xor"];

export const PeerColors = ["#e53935", "#1e88e5", "#43a047", "#8e24aa", "#fb8c00", "#00acc1", "#d81b60", "#6d4c41", "#3f51b5", "#7cb342"];

export function peerColor(index) {
    return PeerColors[((index % 10) + 10) % 10];
}

// --- SHA-256 y HMAC (crypto.subtle solo existe en páginas seguras: las de la red local no lo son) --

const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);

export function sha256(bytes) {
    const length = bytes.length;
    const padded = Math.ceil((length + 9) / 64) * 64;
    const m = new Uint8Array(padded);
    m.set(bytes);
    m[length] = 0x80;
    const view = new DataView(m.buffer);
    view.setUint32(padded - 8, Math.floor((length * 8) / 2 ** 32));
    view.setUint32(padded - 4, (length * 8) >>> 0);
    const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    const w = new Uint32Array(64);
    const rotr = (x, n) => (x >>> n) | (x << (32 - n));
    for (let off = 0; off < padded; off += 64) {
        for (let i = 0; i < 16; ++i)
            w[i] = view.getUint32(off + i * 4);
        for (let i = 16; i < 64; ++i) {
            const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
            const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
            w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
        }
        let [a, b, c, d, e, f, g, hh] = h;
        for (let i = 0; i < 64; ++i) {
            const S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const ch = (e & f) ^ (~e & g);
            const t1 = (hh + S1 + ch + K[i] + w[i]) >>> 0;
            const S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const maj = (a & b) ^ (a & c) ^ (b & c);
            const t2 = (S0 + maj) >>> 0;
            hh = g;
            g = f;
            f = e;
            e = (d + t1) >>> 0;
            d = c;
            c = b;
            b = a;
            a = (t1 + t2) >>> 0;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    const out = new Uint8Array(32);
    const outView = new DataView(out.buffer);
    for (let i = 0; i < 8; ++i)
        outView.setUint32(i * 4, h[i]);
    return out;
}

export function hmacSha256(key, message) {
    let k = key.length > 64 ? sha256(key) : key;
    const padded = new Uint8Array(64);
    padded.set(k);
    const inner = new Uint8Array(64 + message.length), outer = new Uint8Array(64 + 32);
    for (let i = 0; i < 64; ++i) {
        inner[i] = padded[i] ^ 0x36;
        outer[i] = padded[i] ^ 0x5c;
    }
    inner.set(message, 64);
    outer.set(sha256(inner), 64);
    return sha256(outer);
}

const utf8 = (text) => new TextEncoder().encode(text);

export function bytesToBase64(bytes) {
    let s = "";
    for (let i = 0; i < bytes.length; i += 0x8000)
        s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
}

export function base64ToBytes(text) {
    const s = atob(text || "");
    const out = new Uint8Array(s.length);
    for (let i = 0; i < s.length; ++i)
        out[i] = s.charCodeAt(i);
    return out;
}

const hex = (bytes) => [...bytes].map((b) => b.toString(16).padStart(2, "0")).join("");

// Prueba de que se conoce una clave (contraseña o ID del equipo) sin enviarla.
export function proof(key, nonceBytes) {
    return bytesToBase64(hmacSha256(utf8(key), nonceBytes));
}

// --- ID del equipo -----------------------------------------------------------------------------

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

export function normalizedCode(code) {
    return String(code || "").toUpperCase().replace(/[^A-Z0-9]/g, "");
}

export function prettyCode(code) {
    const n = normalizedCode(code);
    return `${n.slice(0, 4)}-${n.slice(4, 8)}-${n.slice(8, 12)}`;
}

export function deviceTag(code) {
    return hex(sha256(utf8(normalizedCode(code)))).slice(0, 16);
}

export function newDeviceCode() {
    const bytes = new Uint8Array(12);
    crypto.getRandomValues(bytes);
    return prettyCode([...bytes].map((b) => CODE_ALPHABET[b % CODE_ALPHABET.length]).join(""));
}

export function randomNonce() {
    const bytes = new Uint8Array(16);
    crypto.getRandomValues(bytes);
    return bytes;
}

// --- Máscaras ----------------------------------------------------------------------------------

// Un bit por píxel de la región, fila a fila, el menos significativo primero.
export function encodeMask(bits, count) {
    const bytes = new Uint8Array(Math.ceil(count / 8));
    for (let i = 0; i < count; ++i)
        if (bits[i])
            bytes[i >> 3] |= 1 << (i & 7);
    return bytesToBase64(bytes);
}

export function decodeMask(text, count) {
    const bytes = base64ToBytes(text);
    const bits = new Uint8Array(count);
    for (let i = 0; i < count && i >> 3 < bytes.length; ++i)
        bits[i] = (bytes[i >> 3] >> (i & 7)) & 1;
    return bits;
}

// Nombres de las herramientas (los ids de la web, iguales en Kaint).
export const ToolNames = ["rectangleselect", "moveselectedpixels", "lassoselect", "moveselection", "ellipseselect", "zoom", "magicwand",
                          "pan", "paintbucket", "gradient", "paintbrush", "eraser", "pencil", "colorpicker", "clonestamp", "recolor",
                          "text", "linecurve", "shapes"];

export function cleanName(name, id) {
    const out = String(name || "").replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, 40);
    return out || `Invitado ${id}`;
}

// Orden nuevo de una lista de capas: las de 'listed' en ese orden, sin las de 'removed', y las
// actuales que no aparecen (las que alguien añadió a la vez) encima de la que tenían debajo.
export function mergeOrder(current, listed, removed) {
    const result = [];
    for (const id of listed)
        if (!removed.has(id) && !result.includes(id))
            result.push(id);
    current.forEach((id, i) => {
        if (result.includes(id) || removed.has(id))
            return;
        let pos = 0;
        for (let j = i - 1; j >= 0; --j) {
            const k = result.indexOf(current[j]);
            if (k >= 0) {
                pos = k + 1;
                break;
            }
        }
        result.splice(pos, 0, id);
    });
    return result;
}
