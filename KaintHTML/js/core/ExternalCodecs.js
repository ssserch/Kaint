// Formatos que el navegador no abre por sí solo, con decodificadores de terceros que solo se cargan
// al abrir uno de esos archivos (assets/codecs, con sus licencias):
// · JPEG XR (.jxr, .wdp, .hdp, .wmp): jpegxr 0.3.0 de Brion Vibber, sobre jxrlib de Microsoft
//   (licencia BSD).
// · HEIC/HEIF, JPEG XL y AVIF cuando el navegador no puede (fuera de Safari, sobre todo):
//   @imagemagick/magick-wasm 0.0.44 de Dirk Lemstra (Apache-2.0, con sus delegados en NOTICE).
// Kaint para el escritorio abre todos estos formatos por sí mismo.

const base = new URL("../../assets/codecs/", import.meta.url);

function loadScript(url) {
    return new Promise((resolve, reject) => {
        const script = document.createElement("script");
        script.src = url.href;
        script.onload = resolve;
        script.onerror = () => reject(new Error(`No se pudo cargar ${url.pathname}`));
        document.head.appendChild(script);
    });
}

let jxr = null;
let magick = null;

const halfToFloat = (h) => {
    const s = h & 0x8000 ? -1 : 1, e = (h >> 10) & 0x1f, f = h & 0x3ff;
    if (e === 0)
        return s * Math.pow(2, -14) * (f / 1024);
    if (e === 31)
        return f ? NaN : s * Infinity;
    return s * Math.pow(2, e - 15) * (1 + f / 1024);
};
// Los formatos en coma flotante de JPEG XR son scRGB (lineales): a sRGB.
const linearTo8 = (v) => {
    if (!(v > 0))
        return 0;
    if (v >= 1)
        return 255;
    return Math.round((v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) * 255);
};

// Un .jxr → { width, height, data: Uint8ClampedArray RGBA }.
export async function decodeJxr(bytes) {
    jxr ??= loadScript(new URL("jpegxr/jpegxr.js", base)).then(() => globalThis.jpegxr());
    const codec = await jxr;
    const image = codec.decode(bytes);
    const { width, height, pixelInfo: info } = image;
    const src = image.bytes;
    const stride = Math.ceil((width * info.bitsPerPixel) / 8);
    const channels = info.channels;
    const depth = String(info.bitDepth);
    const gray = /^Y/i.test(info.colorFormat || "");
    if (!["RGB", "RGBA", "YOnly", "Y"].some((f) => String(info.colorFormat || "").startsWith(f)) && !gray)
        throw new Error(`JPEG XR: formato de color no compatible (${info.colorFormat})`);
    const out = new Uint8ClampedArray(width * height * 4);
    const view = new DataView(src.buffer, src.byteOffset, src.byteLength);
    const bytesPer = depth === "8" ? 1 : depth === "16" || depth === "16S" || depth === "16Float" ? 2 : depth === "32Float" || depth === "32S" ? 4 : 0;
    if (!bytesPer)
        throw new Error(`JPEG XR: profundidad no compatible (${depth})`);
    const sample = (offset) => {
        switch (depth) {
        case "8": return src[offset];
        case "16": return view.getUint16(offset, true) / 257;
        case "16Float": return linearTo8(halfToFloat(view.getUint16(offset, true)));
        case "32Float": return linearTo8(view.getFloat32(offset, true));
        default: return 0;
        }
    };
    const alphaSample = (offset) => (depth === "16Float" ? halfToFloat(view.getUint16(offset, true)) * 255
        : depth === "32Float" ? view.getFloat32(offset, true) * 255 : sample(offset));
    for (let y = 0; y < height; ++y)
        for (let x = 0; x < width; ++x) {
            const p = y * stride + x * channels * bytesPer, o = (y * width + x) * 4;
            if (gray) {
                const v = sample(p);
                out[o] = out[o + 1] = out[o + 2] = v;
                out[o + 3] = 255;
                continue;
            }
            const c0 = sample(p), c1 = sample(p + bytesPer), c2 = sample(p + 2 * bytesPer);
            out[o] = info.bgr ? c2 : c0;
            out[o + 1] = c1;
            out[o + 2] = info.bgr ? c0 : c2;
            let a = info.hasAlpha && channels >= 4 ? alphaSample(p + 3 * bytesPer) : 255;
            a = Math.max(0, Math.min(255, Math.round(a)));
            out[o + 3] = a;
            if (info.premultipliedAlpha && a > 0 && a < 255)
                for (let c = 0; c < 3; ++c) out[o + c] = Math.min(255, Math.round((out[o + c] * 255) / a));
        }
    // jpegxr no lee el alfa guardado en un plano aparte: llega todo a 0. Mejor opaca que invisible.
    if (info.hasAlpha) {
        let any = false;
        for (let i = 3; i < out.length && !any; i += 4)
            any = out[i] !== 0;
        if (!any)
            for (let i = 3; i < out.length; i += 4) out[i] = 255;
    }
    return { width, height, data: out };
}

// Cualquier formato que entienda ImageMagick → { width, height, data: Uint8ClampedArray RGBA }.
export async function decodeWithMagick(bytes) {
    magick ??= (async () => {
        const mod = await import(new URL("magick/index.js", base).href);
        await mod.initializeImageMagick(new URL("magick/magick.wasm", base));
        return mod;
    })();
    const mod = await magick;
    return new Promise((resolve, reject) => {
        try {
            mod.ImageMagick.read(bytes, (image) => {
                image.depth = 8;
                const width = image.width, height = image.height;
                image.write(mod.MagickFormat.Rgba, (data) => resolve({ width, height, data: new Uint8ClampedArray(data) }));
            });
        } catch (e) {
            reject(e);
        }
    });
}
