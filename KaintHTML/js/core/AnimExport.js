// Exportar desde el editor de GIF a otros formatos (GifExport de Kaint): PNG animado (APNG), WebP
// animado y vídeo MP4. Los fotogramas son los del GIF ({ width, height, data, delay, hidden }: las
// capas visibles con su duración y la animación de sus puntos clave, de animationFrames).
import { crc32 } from "./Zip.js";
import { zlibCompress } from "./Codecs.js";

const visible = (frames) => frames.filter((f) => !f.hidden && f.data);

class Bytes {
    constructor() {
        this.parts = [];
        this.length = 0;
    }
    push(u8) {
        this.parts.push(u8);
        this.length += u8.length;
    }
    u8(v) { this.push(Uint8Array.of(v & 255)); }
    u16(v) { this.push(Uint8Array.of((v >> 8) & 255, v & 255)); }
    u24le(v) { this.push(Uint8Array.of(v & 255, (v >> 8) & 255, (v >> 16) & 255)); }
    u32(v) { this.push(Uint8Array.of((v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255)); }
    u32le(v) { this.push(Uint8Array.of(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255)); }
    ascii(s) { this.push(Uint8Array.from(s, (c) => c.charCodeAt(0))); }
    result() {
        const out = new Uint8Array(this.length);
        let at = 0;
        for (const p of this.parts) {
            out.set(p, at);
            at += p.length;
        }
        return out;
    }
}

// Una imagen ampliada sin suavizar (pixel art) y, si se pide, sobre un color (sin transparencia).
export function preparedCanvas(frame, scale = 1, background = null) {
    const src = new OffscreenCanvas(frame.width, frame.height);
    src.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(frame.data), frame.width, frame.height), 0, 0);
    const w = frame.width * scale, h = frame.height * scale;
    const out = new OffscreenCanvas(w, h), g = out.getContext("2d");
    if (background) {
        g.fillStyle = background;
        g.fillRect(0, 0, w, h);
    }
    g.imageSmoothingEnabled = false;
    g.drawImage(src, 0, 0, w, h);
    return out;
}

// --- PNG animado ---------------------------------------------------------------------------------

function pngChunk(out, type, data) {
    const body = new Uint8Array(4 + data.length);
    body.set(Uint8Array.from(type, (c) => c.charCodeAt(0)));
    body.set(data, 4);
    out.u32(data.length);
    out.push(body);
    out.u32(crc32(body));
}

function paeth(a, b, c) {
    const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Las filas con el filtro que deja los números más pequeños, comprimidas con zlib.
async function pngData(data, w, h) {
    const stride = w * 4, raw = new Uint8Array((stride + 1) * h), f = new Uint8Array(stride), best = new Uint8Array(stride);
    for (let y = 0; y < h; ++y) {
        const row = y * stride, up = (y - 1) * stride;
        let bestSum = -1, bestType = 0;
        for (let type = 0; type < 5; ++type) {
            let sum = 0;
            for (let i = 0; i < stride; ++i) {
                const a = i >= 4 ? data[row + i - 4] : 0, b = y ? data[up + i] : 0, c = y && i >= 4 ? data[up + i - 4] : 0;
                const p = type === 1 ? a : type === 2 ? b : type === 3 ? (a + b) >> 1 : type === 4 ? paeth(a, b, c) : 0;
                f[i] = data[row + i] - p;
                sum += Math.abs((f[i] << 24) >> 24);
            }
            if (bestSum < 0 || sum < bestSum) {
                bestSum = sum;
                bestType = type;
                best.set(f);
            }
        }
        raw[y * (stride + 1)] = bestType;
        raw.set(best, y * (stride + 1) + 1);
    }
    return zlibCompress(raw);
}

// 'loops' 0: se repite siempre.
export async function encodeApng(all, { loops = 0, scale = 1 } = {}) {
    const frames = visible(all);
    if (!frames.length)
        throw new Error("No hay fotogramas que guardar.");
    const w = frames[0].width * scale, h = frames[0].height * scale;
    const out = new Bytes();
    out.push(Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a));
    const ihdr = new Bytes();
    ihdr.u32(w); ihdr.u32(h); ihdr.push(Uint8Array.of(8, 6, 0, 0, 0));
    pngChunk(out, "IHDR", ihdr.result());
    const actl = new Bytes();
    actl.u32(frames.length); actl.u32(Math.max(0, loops));
    pngChunk(out, "acTL", actl.result());
    let sequence = 0;
    for (let i = 0; i < frames.length; ++i) {
        const pixels = scale > 1 ? preparedCanvas(frames[i], scale).getContext("2d").getImageData(0, 0, w, h).data : frames[i].data;
        const fctl = new Bytes();
        fctl.u32(sequence++); fctl.u32(w); fctl.u32(h); fctl.u32(0); fctl.u32(0);
        // La duración en milésimas; cada fotograma sustituye al anterior.
        fctl.u16(Math.min(65535, Math.max(1, Math.round(frames[i].delay || 100)))); fctl.u16(1000);
        fctl.push(Uint8Array.of(0, 0));
        pngChunk(out, "fcTL", fctl.result());
        const data = await pngData(pixels, w, h);
        if (i === 0) {
            pngChunk(out, "IDAT", data);
        } else {
            const fdat = new Bytes();
            fdat.u32(sequence++);
            fdat.push(data);
            pngChunk(out, "fdAT", fdat.result());
        }
    }
    pngChunk(out, "IEND", new Uint8Array(0));
    return out.result();
}

// --- WebP animado --------------------------------------------------------------------------------

// Si el navegador sabe comprimir WebP (los fotogramas los comprime él; aquí solo se juntan).
export async function canEncodeWebp() {
    try {
        const blob = await new OffscreenCanvas(2, 2).convertToBlob({ type: "image/webp" });
        return blob.type === "image/webp";
    } catch {
        return false;
    }
}

// Los trozos de la imagen de un WebP: ALPH + VP8 o VP8L (sin la cabecera RIFF, VP8X ni el perfil ICCP
// que añade Chrome, que dentro de un fotograma de una animación no se admiten).
function webpChunks(bytes) {
    const chunks = [];
    for (let at = 12; at + 8 <= bytes.length;) {
        const type = String.fromCharCode(...bytes.subarray(at, at + 4));
        const size = bytes[at + 4] | (bytes[at + 5] << 8) | (bytes[at + 6] << 16) | (bytes[at + 7] << 24);
        const end = at + 8 + size + (size & 1);
        if (type === "ALPH" || type === "VP8 " || type === "VP8L")
            chunks.push(bytes.subarray(at, Math.min(end, bytes.length)));
        at = end;
    }
    return chunks;
}

function riffChunk(out, type, data) {
    out.ascii(type);
    out.u32le(data.length);
    out.push(data);
    if (data.length & 1)
        out.u8(0);
}

export async function encodeAnimatedWebp(all, { quality = 90, loops = 0, scale = 1 } = {}) {
    const frames = visible(all);
    if (!frames.length)
        throw new Error("No hay fotogramas que guardar.");
    const w = frames[0].width * scale, h = frames[0].height * scale;
    const body = new Bytes();
    const vp8x = new Bytes();
    // Animación y alfa; el lienzo menos uno.
    vp8x.push(Uint8Array.of(0x12, 0, 0, 0));
    vp8x.u24le(w - 1); vp8x.u24le(h - 1);
    riffChunk(body, "VP8X", vp8x.result());
    const anim = new Bytes();
    anim.u32le(0); anim.push(Uint8Array.of(Math.max(0, loops) & 255, (Math.max(0, loops) >> 8) & 255));
    riffChunk(body, "ANIM", anim.result());
    for (const f of frames) {
        const blob = await preparedCanvas(f, scale).convertToBlob({ type: "image/webp", quality: Math.min(1, Math.max(0, quality / 100)) });
        const bytes = new Uint8Array(await blob.arrayBuffer());
        const frame = new Bytes();
        frame.u24le(0); frame.u24le(0); frame.u24le(w - 1); frame.u24le(h - 1);
        frame.u24le(Math.min(0xffffff, Math.max(1, Math.round(f.delay || 100))));
        // Sin mezclar con el anterior (cada fotograma lo sustituye entero).
        frame.u8(0x02);
        for (const c of webpChunks(bytes))
            frame.push(c);
        riffChunk(body, "ANMF", frame.result());
    }
    const out = new Bytes();
    out.ascii("RIFF");
    out.u32le(4 + body.length);
    out.ascii("WEBP");
    out.push(body.result());
    return out.result();
}

// --- Vídeo MP4 (H.264) ---------------------------------------------------------------------------

export const canEncodeMp4 = () => typeof VideoEncoder !== "undefined" && typeof VideoFrame !== "undefined";

function box(type, ...parts) {
    const b = new Bytes();
    let size = 8;
    for (const p of parts)
        size += p.length;
    b.u32(size);
    b.ascii(type);
    for (const p of parts)
        b.push(p);
    return b.result();
}

function fields(fn) {
    const b = new Bytes();
    fn(b);
    return b.result();
}

const fullBox = (type, version, flags, ...parts) => box(type, fields((b) => { b.u8(version); b.push(Uint8Array.of((flags >> 16) & 255, (flags >> 8) & 255, flags & 255)); }), ...parts);
const matrix = () => fields((b) => [0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000].forEach((v) => b.u32(v)));

// Un descriptor de MPEG-4 (esds): etiqueta, tamaño (en 4 bytes) y contenido.
function descriptor(tag, ...parts) {
    const body = new Bytes();
    for (const p of parts)
        body.push(p);
    const n = body.length;
    return Uint8Array.of(tag, 0x80 | ((n >> 21) & 127), 0x80 | ((n >> 14) & 127), 0x80 | ((n >> 7) & 127), n & 127, ...body.result());
}

// Las pistas de audio del editor de GIF, comprimidas en AAC (WebCodecs) para el MP4: { frames, asc, rate }.
export async function canEncodeAac(rate = 48000) {
    if (typeof AudioEncoder === "undefined" || typeof AudioData === "undefined")
        return false;
    try {
        return (await AudioEncoder.isConfigSupported({ codec: "mp4a.40.2", sampleRate: rate, numberOfChannels: 2, bitrate: 192000 })).supported;
    } catch {
        return false;
    }
}

async function encodeAac(buffer) {
    const rate = buffer.sampleRate, frames = [];
    let asc = null, failure = null;
    const encoder = new AudioEncoder({
        output: (chunk, meta) => {
            const d = meta?.decoderConfig?.description;
            if (d && !asc)
                asc = new Uint8Array(d instanceof ArrayBuffer ? d : d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength));
            const data = new Uint8Array(chunk.byteLength);
            chunk.copyTo(data);
            frames.push(data);
        },
        error: (e) => { failure = e; },
    });
    encoder.configure({ codec: "mp4a.40.2", sampleRate: rate, numberOfChannels: 2, bitrate: 192000 });
    const left = buffer.getChannelData(0), right = buffer.numberOfChannels > 1 ? buffer.getChannelData(1) : left;
    const block = 4096;
    for (let i = 0; i < buffer.length; i += block) {
        const n = Math.min(block, buffer.length - i);
        const planar = new Float32Array(n * 2);
        planar.set(left.subarray(i, i + n), 0);
        planar.set(right.subarray(i, i + n), n);
        const data = new AudioData({ format: "f32-planar", sampleRate: rate, numberOfFrames: n, numberOfChannels: 2,
                                     timestamp: Math.round((i / rate) * 1e6), data: planar });
        encoder.encode(data);
        data.close();
        while (encoder.encodeQueueSize > 8)
            await new Promise((resolve) => setTimeout(resolve, 5));
        if (failure)
            throw failure;
    }
    await encoder.flush();
    encoder.close();
    if (failure)
        throw failure;
    // AAC-LC, 48 kHz, estéreo, si el navegador no dio su configuración.
    return { frames, asc: asc || Uint8Array.of(0x11, 0x90), rate };
}

// Un MP4 con una pista de vídeo H.264 ('samples': { data, duration (ms), key }), si se da, una de
// audio AAC ('audio': { frames, asc, rate }) y el índice delante (se puede ver mientras se descarga).
function muxMp4(samples, width, height, avcc, audio = null) {
    const total = samples.reduce((a, s) => a + s.duration, 0);
    const videoBytes = samples.reduce((a, s) => a + s.data.length, 0);
    const ftyp = box("ftyp", fields((b) => { b.ascii("isom"); b.u32(0x200); b.ascii("isomiso2avc1mp41"); }));
    const stts = [];
    for (const s of samples) {
        const last = stts[stts.length - 1];
        if (last && last[1] === s.duration)
            last[0]++;
        else
            stts.push([1, s.duration]);
    }
    const keys = samples.map((s, i) => (s.key ? i + 1 : 0)).filter(Boolean);
    const moovFor = (offset) => {
        const avc1 = box("avc1", fields((b) => {
            b.push(new Uint8Array(6)); b.u16(1); b.push(new Uint8Array(16)); b.u16(width); b.u16(height);
            b.u32(0x00480000); b.u32(0x00480000); b.u32(0); b.u16(1); b.push(new Uint8Array(32)); b.u16(0x18); b.u16(0xffff);
        }), box("avcC", avcc));
        const stbl = box("stbl",
            fullBox("stsd", 0, 0, fields((b) => b.u32(1)), avc1),
            fullBox("stts", 0, 0, fields((b) => { b.u32(stts.length); for (const [n, d] of stts) { b.u32(n); b.u32(d); } })),
            fullBox("stss", 0, 0, fields((b) => { b.u32(keys.length); keys.forEach((k) => b.u32(k)); })),
            fullBox("stsz", 0, 0, fields((b) => { b.u32(0); b.u32(samples.length); samples.forEach((s) => b.u32(s.data.length)); })),
            fullBox("stsc", 0, 0, fields((b) => { b.u32(1); b.u32(1); b.u32(samples.length); b.u32(1); })),
            fullBox("stco", 0, 0, fields((b) => { b.u32(1); b.u32(offset); })));
        const minf = box("minf", fullBox("vmhd", 0, 1, new Uint8Array(8)),
                         box("dinf", fullBox("dref", 0, 0, fields((b) => b.u32(1)), fullBox("url ", 0, 1))), stbl);
        const mdia = box("mdia",
            fullBox("mdhd", 0, 0, fields((b) => { b.u32(0); b.u32(0); b.u32(1000); b.u32(total); b.u16(0x55c4); b.u16(0); })),
            fullBox("hdlr", 0, 0, fields((b) => { b.u32(0); b.ascii("vide"); b.push(new Uint8Array(12)); b.ascii("Kaint\0"); })), minf);
        const tkhd = fullBox("tkhd", 0, 3, fields((b) => {
            b.u32(0); b.u32(0); b.u32(1); b.u32(0); b.u32(total); b.push(new Uint8Array(8)); b.u16(0); b.u16(0); b.u16(0); b.u16(0);
            b.push(matrix()); b.u32(width << 16); b.u32(height << 16);
        }));
        const mvhd = fullBox("mvhd", 0, 0, fields((b) => {
            b.u32(0); b.u32(0); b.u32(1000); b.u32(total); b.u32(0x10000); b.u16(0x100); b.push(new Uint8Array(10));
            b.push(matrix()); b.push(new Uint8Array(24)); b.u32(audio ? 3 : 2);
        }));
        if (!audio)
            return box("moov", mvhd, box("trak", tkhd, mdia));
        // El audio, detrás de los fotogramas en el mismo bloque de datos.
        const n = audio.frames.length, samplesTotal = n * 1024, ms = Math.round((samplesTotal / audio.rate) * 1000);
        const esds = fullBox("esds", 0, 0, descriptor(3, Uint8Array.of(0, 2, 0),
            descriptor(4, Uint8Array.of(0x40, 0x15, 0, 0, 0), fields((b) => { b.u32(192000); b.u32(192000); }), descriptor(5, audio.asc)),
            descriptor(6, Uint8Array.of(2))));
        const mp4a = box("mp4a", fields((b) => {
            b.push(new Uint8Array(6)); b.u16(1); b.u32(0); b.u32(0); b.u16(2); b.u16(16); b.u16(0); b.u16(0); b.u32(audio.rate * 65536);
        }), esds);
        const astbl = box("stbl",
            fullBox("stsd", 0, 0, fields((b) => b.u32(1)), mp4a),
            fullBox("stts", 0, 0, fields((b) => { b.u32(1); b.u32(n); b.u32(1024); })),
            fullBox("stsz", 0, 0, fields((b) => { b.u32(0); b.u32(n); audio.frames.forEach((f) => b.u32(f.length)); })),
            fullBox("stsc", 0, 0, fields((b) => { b.u32(1); b.u32(1); b.u32(n); b.u32(1); })),
            fullBox("stco", 0, 0, fields((b) => { b.u32(1); b.u32(offset + videoBytes); })));
        const aminf = box("minf", fullBox("smhd", 0, 0, new Uint8Array(4)),
                          box("dinf", fullBox("dref", 0, 0, fields((b) => b.u32(1)), fullBox("url ", 0, 1))), astbl);
        const amdia = box("mdia",
            fullBox("mdhd", 0, 0, fields((b) => { b.u32(0); b.u32(0); b.u32(audio.rate); b.u32(samplesTotal); b.u16(0x55c4); b.u16(0); })),
            fullBox("hdlr", 0, 0, fields((b) => { b.u32(0); b.ascii("soun"); b.push(new Uint8Array(12)); b.ascii("Kaint\0"); })), aminf);
        const atkhd = fullBox("tkhd", 0, 3, fields((b) => {
            b.u32(0); b.u32(0); b.u32(2); b.u32(0); b.u32(Math.min(ms, total)); b.push(new Uint8Array(8)); b.u16(0); b.u16(1); b.u16(0x100); b.u16(0);
            b.push(matrix()); b.u32(0); b.u32(0);
        }));
        return box("moov", mvhd, box("trak", tkhd, mdia), box("trak", atkhd, amdia));
    };
    const moovSize = moovFor(0).length;
    const moov = moovFor(ftyp.length + moovSize + 8);
    const audioBytes = audio ? audio.frames.reduce((a, f) => a + f.length, 0) : 0;
    const out = new Bytes();
    out.push(ftyp);
    out.push(moov);
    out.u32(8 + videoBytes + audioBytes);
    out.ascii("mdat");
    for (const s of samples)
        out.push(s.data);
    for (const f of audio?.frames || [])
        out.push(f);
    return out.result();
}

// 'repeat': cuántas veces seguidas va la animación (un vídeo no se repite solo); 'scale': aumento
// sin suavizar; 'quality': 0 alta, 1 media, 2 baja; 'background': el color de lo transparente.
// 'audio': la mezcla de las pistas de audio (AudioBuffer), comprimida en AAC si el navegador puede.
export async function encodeMp4(all, { repeat = 1, scale = 1, quality = 0, background = "#ffffff", onProgress = null, audio = null } = {}) {
    const frames = visible(all);
    if (!frames.length)
        throw new Error("No hay fotogramas que guardar.");
    // H.264 en 4:2:0: medidas pares.
    const w = Math.ceil((frames[0].width * scale) / 2) * 2, h = Math.ceil((frames[0].height * scale) / 2) * 2;
    const bitrate = Math.round(w * h * [8, 4, 2][Math.min(2, Math.max(0, quality))]);
    let config = null;
    for (const codec of ["avc1.640034", "avc1.640028", "avc1.4d0034", "avc1.4d0028", "avc1.42e01f", "avc1.42001f"]) {
        const c = { codec, width: w, height: h, bitrate, framerate: 30, avc: { format: "avc" }, latencyMode: "quality" };
        try {
            if ((await VideoEncoder.isConfigSupported(c)).supported) {
                config = c;
                break;
            }
        } catch {
            // Otro perfil.
        }
    }
    if (!config)
        throw new Error("El navegador no puede comprimir vídeo H.264 de este tamaño.");
    const samples = [];
    let avcc = null, failure = null;
    const encoder = new VideoEncoder({
        output: (chunk, meta) => {
            if (meta?.decoderConfig?.description && !avcc)
                avcc = new Uint8Array(meta.decoderConfig.description instanceof ArrayBuffer ? meta.decoderConfig.description
                                      : meta.decoderConfig.description.buffer.slice(meta.decoderConfig.description.byteOffset,
                                                                                     meta.decoderConfig.description.byteOffset + meta.decoderConfig.description.byteLength));
            const data = new Uint8Array(chunk.byteLength);
            chunk.copyTo(data);
            samples.push({ data, timestamp: chunk.timestamp, key: chunk.type === "key" });
        },
        error: (e) => { failure = e; },
    });
    encoder.configure(config);
    const canvas = new OffscreenCanvas(w, h), g = canvas.getContext("2d");
    const total = frames.length * Math.max(1, repeat);
    let time = 0, n = 0, sinceKey = Infinity;
    const durations = [];
    for (let r = 0; r < Math.max(1, repeat); ++r)
        for (const f of frames) {
            const duration = Math.max(1, Math.round(f.delay || 100));
            g.fillStyle = background;
            g.fillRect(0, 0, w, h);
            g.drawImage(preparedCanvas(f, scale), 0, 0);
            const frame = new VideoFrame(canvas, { timestamp: time * 1000, duration: duration * 1000 });
            // Un fotograma clave cada dos segundos (para poder saltar en el vídeo).
            encoder.encode(frame, { keyFrame: sinceKey >= 2000 });
            sinceKey = sinceKey >= 2000 ? duration : sinceKey + duration;
            frame.close();
            durations.push(duration);
            time += duration;
            ++n;
            onProgress?.(n / total);
            // Sin llenar la cola del codificador.
            while (encoder.encodeQueueSize > 4)
                await new Promise((resolve) => setTimeout(resolve, 5));
            if (failure)
                throw failure;
        }
    await encoder.flush();
    encoder.close();
    if (failure)
        throw failure;
    if (!avcc || samples.length !== durations.length)
        throw new Error("El navegador no devolvió el vídeo completo.");
    // (Sin fotogramas B: salen en el mismo orden en que entran.)
    samples.forEach((s, i) => (s.duration = durations[i]));
    return muxMp4(samples, w, h, avcc, audio ? await encodeAac(audio) : null);
}

// Sin WebCodecs: el vídeo se graba mientras se reproduce la animación (tarda lo que dura), en el
// formato que el navegador sepa grabar (MP4 o WebM). { blob, ext }.
// 'audio': la mezcla de las pistas de audio (AudioBuffer), que suena mientras se graba.
export async function recordVideo(all, { repeat = 1, scale = 1, background = "#ffffff", onProgress = null, audio = null } = {}) {
    const frames = visible(all);
    if (!frames.length || typeof MediaRecorder === "undefined")
        throw new Error("El navegador no puede grabar vídeo.");
    const w = Math.ceil((frames[0].width * scale) / 2) * 2, h = Math.ceil((frames[0].height * scale) / 2) * 2;
    const canvas = Object.assign(document.createElement("canvas"), { width: w, height: h }), g = canvas.getContext("2d");
    const type = ["video/mp4;codecs=avc1", "video/mp4", "video/webm;codecs=vp9", "video/webm"].find((t) => MediaRecorder.isTypeSupported(t));
    if (!type)
        throw new Error("El navegador no puede grabar vídeo.");
    const stream = canvas.captureStream(60);
    // El audio, a la grabación (sin oírse).
    let audioCtx = null, source = null;
    if (audio && typeof AudioContext !== "undefined") {
        audioCtx = new AudioContext({ sampleRate: audio.sampleRate });
        const dest = audioCtx.createMediaStreamDestination();
        source = audioCtx.createBufferSource();
        source.buffer = audio;
        source.connect(dest);
        for (const t of dest.stream.getAudioTracks())
            stream.addTrack(t);
    }
    const recorder = new MediaRecorder(stream, { mimeType: type, videoBitsPerSecond: w * h * 8 });
    const parts = [];
    recorder.ondataavailable = (e) => e.data.size && parts.push(e.data);
    const done = new Promise((resolve) => (recorder.onstop = resolve));
    const draw = (f) => {
        g.fillStyle = background;
        g.fillRect(0, 0, w, h);
        g.drawImage(preparedCanvas(f, scale), 0, 0);
    };
    draw(frames[0]);
    recorder.start();
    source?.start();
    const total = frames.length * Math.max(1, repeat);
    let n = 0;
    for (let r = 0; r < Math.max(1, repeat); ++r)
        for (const f of frames) {
            draw(f);
            await new Promise((resolve) => setTimeout(resolve, Math.max(10, f.delay || 100)));
            onProgress?.(++n / total);
        }
    recorder.stop();
    await done;
    stream.getTracks().forEach((t) => t.stop());
    source?.stop();
    audioCtx?.close();
    return { blob: new Blob(parts, { type: type.split(";")[0] }), ext: type.includes("mp4") ? "mp4" : "webm" };
}
