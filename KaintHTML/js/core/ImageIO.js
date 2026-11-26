// Abrir y guardar: las imágenes que entiende el navegador (PNG, JPEG, WebP, GIF, BMP, AVIF... y, si
// puede, HEIC y JPEG XL), OpenRaster (.ora) y Photoshop (.psd y .psb) con sus capas, como
// src/core/Ora.cpp y Psd.cpp de Kaint.
import { decodeJxr, decodeWithMagick } from "./ExternalCodecs.js";
import { Document, Layer } from "./Document.js";
import { context, createCanvas, OraNames } from "./Raster.js";
import { unzip, zip } from "./Zip.js";
import { clipFromJson, clipToJson } from "./Audio.js";
import * as Codecs from "./Codecs.js";
import * as Motion from "./Motion.js";
import { iccFromImage, isSrgb, readIcc } from "./ColorProfiles.js";
import { canvasEncodes, defaultSaveOptions, encodeAnimation, encodeImage, prepare } from "./Encode.js";
import { decodePsd, encodePsd } from "./Psd.js";
import { tr } from "../i18n.js";

// Tipos de "Guardar como", en el orden de Kaint (ImageIO.cpp).
export const SaveFormats = [
    { ext: "pdn", mime: "application/octet-stream", name: "Paint.NET con capas", layered: true },
    { ext: "ora", mime: "image/openraster", name: "OpenRaster con capas", layered: true },
    { ext: "psd", mime: "image/vnd.adobe.photoshop", name: "Photoshop con capas", layered: true },
    { ext: "psb", mime: "image/vnd.adobe.photoshop", name: "Documento grande de Photoshop con capas", layered: true },
    { ext: "png", mime: "image/png", name: "PNG" },
    { ext: "jpg", mime: "image/jpeg", name: "JPEG", quality: true, aliases: ["jpeg", "jpe", "jfif", "exif"] },
    { ext: "webp", mime: "image/webp", name: "WebP", quality: true },
    { ext: "dds", mime: "image/vnd-ms.dds", name: "Superficie de DirectDraw (DDS)" },
    { ext: "tiff", mime: "image/tiff", name: "TIFF", aliases: ["tif"] },
    { ext: "gif", mime: "image/gif", name: "GIF", animation: true },
    { ext: "bmp", mime: "image/bmp", name: "BMP", aliases: ["dib", "rle"] },
    { ext: "tga", mime: "image/x-tga", name: "TGA" },
    { ext: "ico", mime: "image/x-icon", name: "Icono de Windows" },
    { ext: "qoi", mime: "image/qoi", name: "QOI" },
    { ext: "ppm", mime: "image/x-portable-pixmap", name: "Netpbm" },
    { ext: "cur", mime: "image/x-icon", name: "Cursor de Windows" },
    { ext: "pam", mime: "image/x-portable-arbitrarymap", name: "PAM (Netpbm con transparencia)" },
    { ext: "pcx", mime: "image/x-pcx", name: "PCX (Paintbrush)" },
    { ext: "sgi", mime: "image/sgi", name: "SGI (Silicon Graphics)", aliases: ["rgb", "rgba", "bw"] },
    { ext: "ff", mime: "image/x-farbfeld", name: "Farbfeld" },
];

// AVIF (y JPEG XL), si el navegador sabe codificarlos (Chrome, por ahora, no): se miran al cargar.
const browserFormats = [
    { ext: "avif", mime: "image/avif", name: "AV1 (AVIF)", quality: true },
    { ext: "jxl", mime: "image/jxl", name: "JPEG XL", quality: true },
];
for (const f of browserFormats)
    canvasEncodes(f.mime).then((ok) => ok && SaveFormats.splice(SaveFormats.findIndex((s) => s.ext === "webp"), 0, f)).catch(() => {});

// Lo que se puede abrir (además de lo que entienda el navegador). HEIC y JPEG XL, solo si el
// navegador los entiende (Safari).
export const OpenAccept = "image/*,.jxr,.wdp,.hdp,.wmp,.ora,.pdn,.psd,.psb,.tga,.tif,.tiff,.dds,.ico,.cur,.qoi,.ppm,.pgm,.pbm,.pnm,.pam,.pcx,.sgi,.rgb,.rgba,.bw,.ff," +
    ".exif,.dib,.rle,.heic,.heif,.jxl,.svg,.svgz,.hdr,.exr,.jp2,.j2k,.jpf,.pfm,.ras,.sun,.xcf,.tim,.icns";
export const OpenExtensions = [".jxr", ".wdp", ".hdp", ".wmp", ".png", ".jpg", ".jpeg", ".jpe", ".jfif", ".exif", ".webp", ".gif", ".bmp", ".dib", ".rle", ".avif", ".heic",
                               ".heif", ".jxl", ".ico", ".cur", ".ora", ".pdn", ".psd", ".psb", ".tga", ".tif", ".tiff", ".dds", ".qoi", ".ppm", ".pgm",
                               ".pbm", ".pnm", ".pam", ".pcx", ".sgi", ".rgb", ".rgba", ".bw", ".ff", ".svg", ".svgz", ".hdr", ".exr", ".jp2", ".j2k",
                               ".jpf", ".pfm", ".ras", ".sun", ".xcf", ".tim", ".icns"];

// Los que abre ImageMagick (magick-wasm) cuando el navegador no puede.
const MagickExtensions = ["hdr", "exr", "jp2", "j2k", "jpf", "pfm", "ras", "sun", "xcf", "tim", "icns"];

export function extensionOf(name) {
    return (name.split(".").pop() || "").toLowerCase();
}

export function formatFor(name) {
    const ext = extensionOf(name);
    return SaveFormats.find((f) => f.ext === ext || f.aliases?.includes(ext)) || null;
}

export function baseName(name) {
    return name.replace(/\.[^.]+$/, "");
}

// Con un perfil de color propio, los píxeles tal cual (se convierten al mostrarlos, como Kaint).
async function decodeImage(blob, colorSpaceConversion = "default") {
    if ("createImageBitmap" in window)
        return createImageBitmap(blob, { colorSpaceConversion });
    const url = URL.createObjectURL(blob);
    try {
        const img = new Image();
        img.src = url;
        await img.decode();
        return img;
    } finally {
        URL.revokeObjectURL(url);
    }
}

// Documento de una o varias imágenes RGBA ({ name, data, ... } de abajo arriba).
function documentFrom(width, height, name, layers) {
    const doc = new Document(width, height, { name, background: null, historyName: tr("Abrir imagen"), historyIcon: "document-open" });
    doc.layers = layers.map((l) => {
        const layer = new Layer(width, height, l.name);
        layer.ctx.putImageData(new ImageData(new Uint8ClampedArray(l.data), width, height), 0, 0);
        layer.visible = l.visible ?? true;
        layer.alphaLock = !!l.alphaLock;
        layer.opacity = l.opacity ?? 255;
        layer.blend = l.blend || "normal";
        return layer;
    });
    return doc;
}

// Abre una imagen como Kaint (ImageIO::load): OpenRaster y Paint.NET con sus capas, el GIF
// animado con un fotograma por capa (la duración en el nombre), TGA, TIFF, DDS, QOI y Netpbm, y lo
// demás con el navegador.
export async function openFile(file) {
    const ext = extensionOf(file.name), name = baseName(file.name);
    const head = new Uint8Array(await file.slice(0, 16).arrayBuffer());
    const magic = String.fromCharCode(...head.subarray(0, 4));
    let doc;
    if (ext === "ora") {
        doc = await openOra(file);
    } else if (ext === "psd" || ext === "psb" || magic === "8BPS") {
        const psd = await decodePsd(new Uint8Array(await file.arrayBuffer()));
        doc = documentFrom(psd.width, psd.height, name, psd.layers);
        doc.active = psd.active;
        if (psd.dpi)
            doc.dpi = psd.dpi;
        try {
            doc.colorProfile = psd.icc ? readIcc(psd.icc) : null;
        } catch {
            doc.colorProfile = null;
        }
        // Convertido al abrirlo (un PSB, de 16 bits, con grupos...): una imagen nueva, para que guardar
        // no pise el original con menos cosas.
        if (psd.warning) {
            doc.importWarning = psd.warning;
            doc.fileName = "";
            return doc;
        }
    } else if (ext === "ico" || ext === "cur") {
        // Como GIMP (y Kaint): cada versión del icono es una capa, de la más pequeña (abajo) a la
        // más grande (arriba), arriba a la izquierda del lienzo del tamaño de la más grande.
        const images = (await Codecs.decodeIcoAll(new Uint8Array(await file.arrayBuffer()))).sort((a, b) => a.width * a.height - b.width * b.height);
        const width = Math.max(...images.map((i) => i.width)), height = Math.max(...images.map((i) => i.height));
        doc = documentFrom(width, height, name, images.map((img) => {
            const data = new Uint8ClampedArray(width * height * 4);
            for (let y = 0; y < img.height; ++y)
                data.set(img.data.subarray(y * img.width * 4, (y + 1) * img.width * 4), y * width * 4);
            return { name: images.length === 1 ? tr("Fondo") : Codecs.icoLayerName(img.width, img.height), data };
        }));
        doc.active = doc.layers.length - 1;
    } else if (ext === "pdn") {
        // (Faltaba esta rama: un PSD sin avisos seguía como si fuera un .pdn y no se abría, y los
        // .pdn tampoco.)
        const pdn = await Codecs.decodePdn(new Uint8Array(await file.arrayBuffer()));
        doc = documentFrom(pdn.width, pdn.height, name, pdn.layers.map((l, i) => ({ ...l, name: l.name || tr("Capa %1", i + 1) })));
        doc.active = doc.layers.length - 1;
        if (pdn.dpi >= 1 && pdn.dpi <= 100000)
            doc.dpi = pdn.dpi;
    } else if (magic.startsWith("GIF")) {
        const anim = Codecs.decodeGif(new Uint8Array(await file.arrayBuffer()));
        const { width, height } = anim.frames[0];
        doc = anim.frames.length === 1
            ? documentFrom(width, height, name, [{ name: tr("Fondo"), data: anim.frames[0].data }])
            : documentFrom(width, height, name, anim.frames.map((f, i) => ({ name: Codecs.nameWithDelay(tr("Fotograma %1", i + 1), f.delay), data: f.data })));
        doc.active = 0;
        // Un GIF de un solo fotograma «se repite» siempre: si luego se le añaden fotogramas, la
        // animación no debe quedarse en una sola pasada.
        doc.gifLoops = anim.frames.length > 1 ? anim.loops : 0;
    } else if (["jxr", "wdp", "hdp", "wmp"].includes(ext) || (head[0] === 0x49 && head[1] === 0x49 && head[2] === 0xbc)) {
        // JPEG XR (ExternalCodecs.js, se carga solo ahora).
        const image = await decodeJxr(new Uint8Array(await file.arrayBuffer()));
        doc = documentFrom(image.width, image.height, name, [{ name: tr("Fondo"), data: image.data }]);
    } else if (["pcx", "sgi", "rgb", "rgba", "bw", "ff", "pam"].includes(ext) || magic === "farb" || magic.startsWith("P7")) {
        // PCX, SGI, Farbfeld y PAM: los lee Kaint (Codecs.js).
        const bytes = new Uint8Array(await file.arrayBuffer());
        const image = magic === "farb" || ext === "ff" ? Codecs.decodeFarbfeld(bytes)
                    : magic.startsWith("P7") || ext === "pam" ? Codecs.decodePam(bytes)
                    : ext === "pcx" ? Codecs.decodePcx(bytes) : Codecs.decodeSgi(bytes);
        doc = documentFrom(image.width, image.height, name, [{ name: tr("Fondo"), data: image.data }]);
        if (image.dpi)
            doc.dpi = image.dpi;
    } else if (ext === "qoi" || magic === "qoif" || ["ppm", "pgm", "pbm", "pnm"].includes(ext)) {
        const bytes = new Uint8Array(await file.arrayBuffer());
        const image = magic === "qoif" ? Codecs.decodeQoi(bytes) : Codecs.decodeNetpbm(bytes);
        doc = documentFrom(image.width, image.height, name, [{ name: tr("Fondo"), data: image.data }]);
    } else if (ext === "tga" || magic === "DDS " || magic === "II*\0" || magic === "MM\0*") {
        const bytes = new Uint8Array(await file.arrayBuffer());
        const image = magic === "DDS " ? Codecs.decodeDds(bytes) : ext === "tga" ? Codecs.decodeTga(bytes) : await Codecs.decodeTiff(bytes);
        doc = documentFrom(image.width, image.height, name, [{ name: tr("Fondo"), data: image.data }]);
        if (image.dpi)
            doc.dpi = image.dpi;
        doc.colorProfile = await profileOf(bytes);
    } else if (ext === "svg" || ext === "svgz" || file.type === "image/svg+xml") {
        // SVG (como imagen, a su tamaño): createImageBitmap no lo lee; un <img> sí. El .svgz va
        // comprimido con gzip.
        let blob = file;
        if (ext === "svgz" || (head[0] === 0x1f && head[1] === 0x8b))
            blob = await new Response(file.stream().pipeThrough(new DecompressionStream("gzip"))).blob();
        const url = URL.createObjectURL(new Blob([await blob.arrayBuffer()], { type: "image/svg+xml" }));
        try {
            const img = new Image();
            img.src = url;
            await img.decode();
            const w = img.naturalWidth || 512, h = img.naturalHeight || 512;
            const canvas = createCanvas(w, h);
            context(canvas).drawImage(img, 0, 0, w, h);
            doc = Document.fromImage(canvas, name);
        } finally {
            URL.revokeObjectURL(url);
        }
    } else {
        const bytes = new Uint8Array(await file.arrayBuffer()), profile = await profileOf(bytes);
        let image;
        try {
            image = await decodeImage(file, profile ? "none" : "default");
        } catch (e) {
            // HEIC, JPEG XL (y AVIF en navegadores viejos) solo los abren algunos navegadores: si
            // no, ImageMagick (ExternalCodecs.js, se carga solo ahora). Con él también se abren los
            // que el navegador nunca lee (Radiance HDR, OpenEXR, JPEG 2000, PFM, Sun Raster, GIMP,
            // PlayStation TIM, iconos de macOS).
            const modern = ["heic", "heif", "hif", "jxl", "avif"].includes(ext);
            if (!modern && !MagickExtensions.includes(ext))
                throw e;
            let decoded;
            try {
                decoded = await decodeWithMagick(bytes, ext === "tim" ? "TIM" : null);
            } catch {
                if (!modern)
                    throw e;
                throw new Error(tr("Este navegador no puede abrir imágenes %1. Prueba con Safari o con Kaint para el escritorio.", ext === "jxl" ? "JPEG XL" : ext === "avif" ? "AVIF" : "HEIC"));
            }
            doc = documentFrom(decoded.width, decoded.height, name, [{ name: tr("Fondo"), data: decoded.data }]);
            doc.dpi = readResolution(bytes) || 96;
            doc.colorProfile = profile;
            doc.fileName = file.name;
            return doc;
        }
        doc = Document.fromImage(image, name);
        doc.dpi = readResolution(bytes) || 96;
        doc.colorProfile = profile;
    }
    doc.fileName = file.name;
    return doc;
}

// El perfil de color incrustado (de matriz y curvas); null si no hay o no se entiende.
async function profileOf(bytes) {
    try {
        const icc = await iccFromImage(bytes, Codecs.zlibDecompress);
        return icc ? readIcc(icc) : null;
    } catch {
        return null;
    }
}

// El perfil que se guarda en el archivo (solo si no es sRGB, como Kaint).
export const embeddedProfile = (doc) => (doc.colorProfile && !isSrgb(doc.colorProfile) ? doc.colorProfile.icc : null);

// --- Resolución (píxeles por pulgada) en PNG (pHYs) y JPEG (JFIF), como al guardar en Kaint ---

function readResolution(b) {
    const u32 = (i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
    if (b[0] === 0x89 && b[1] === 0x50) {
        for (let i = 8; i + 8 <= b.length;) {
            const len = u32(i), type = String.fromCharCode(b[i + 4], b[i + 5], b[i + 6], b[i + 7]);
            if (type === "pHYs" && i + 17 <= b.length)
                return b[i + 16] === 1 ? Math.round(u32(i + 8) * 0.0254 * 100) / 100 : 0;
            if (type === "IDAT" || type === "IEND")
                break;
            i += 12 + len;
        }
    } else if (b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff && b[3] === 0xe0 && String.fromCharCode(...b.slice(6, 11)) === "JFIF\0") {
        const units = b[13], x = (b[14] << 8) | b[15];
        return units === 1 ? x : units === 2 ? Math.round(x * 2.54 * 100) / 100 : 0;
    }
    return 0;
}

async function openOra(file) {
    const files = await unzip(await file.arrayBuffer());
    const stack = new DOMParser().parseFromString(new TextDecoder().decode(files.get("stack.xml")), "application/xml");
    const root = stack.querySelector("image");
    const width = parseInt(root.getAttribute("w"), 10), height = parseInt(root.getAttribute("h"), 10);
    const doc = new Document(width, height, { name: baseName(file.name), background: null, historyName: tr("Abrir imagen"),
                                              historyIcon: "document-open" });
    doc.layers = [];
    const blendFromOra = Object.fromEntries(Object.entries(OraNames).map(([k, v]) => [v, k]));
    // En stack.xml la capa de arriba va primero.
    const nodes = [...stack.querySelectorAll("stack > layer")].reverse();
    for (const node of nodes) {
        const layer = new Layer(width, height, node.getAttribute("name") || tr("Capa"));
        const data = files.get(node.getAttribute("src"));
        if (data) {
            const image = await decodeImage(new Blob([data], { type: "image/png" }));
            layer.ctx.drawImage(image, parseInt(node.getAttribute("x") || "0", 10), parseInt(node.getAttribute("y") || "0", 10));
        }
        layer.visible = node.getAttribute("visibility") !== "hidden";
        layer.opacity = Math.round(parseFloat(node.getAttribute("opacity") ?? "1") * 255);
        layer.blend = blendFromOra[node.getAttribute("composite-op")] || "normal";
        layer.alphaLock = node.getAttribute("kaint-alpha-lock") === "true";
        layer.track = Math.max(0, Math.min(63, parseInt(node.getAttribute("kaint-gif-track") || "0", 10) || 0));
        layer.gap = Math.max(0, parseInt(node.getAttribute("kaint-gif-gap") || "0", 10) || 0);
        try {
            const motion = node.getAttribute("kaint-motion");
            if (motion)
                layer.motion = JSON.parse(motion);
        } catch {}
        doc.layers.push(layer);
    }
    if (!doc.layers.length)
        doc.layers.push(new Layer(width, height, tr("Fondo")));
    doc.active = doc.layers.length - 1;
    doc.fileName = file.name;
    // Las pistas de audio del editor de GIF.
    try {
        const audio = files.get("kaint-audio.json");
        if (audio)
            doc.audioClips = JSON.parse(new TextDecoder().decode(audio)).map((o) => files.get(o.src) && clipFromJson(o, new Uint8Array(files.get(o.src)))).filter(Boolean);
    } catch {}
    return doc;
}

function canvasBlob(canvas, mime = "image/png", quality) {
    return new Promise((resolve, reject) => canvas.toBlob((b) => (b ? resolve(b) : reject(new Error(mime))), mime, quality));
}

// Píxeles RGBA de la imagen acoplada.
export function flattenedImage(doc) {
    const data = doc.flattened().getContext("2d").getImageData(0, 0, doc.width, doc.height).data;
    return { width: doc.width, height: doc.height, data };
}

// Fotogramas de un GIF animado: las capas visibles (y las ocultas, marcadas, si se piden), la de
// abajo primero, cada una con su opacidad (ImageIO::animationFrames de Kaint).
// Con varias pistas (editor de GIF), como trackFrames de Kaint: en cada momento, lo de cada pista
// uno encima de otro (las de número más alto, arriba); un fotograma cada vez que cambia algo.
function trackFrames(doc, defaultDelay, withHidden) {
    const spans = [], ends = [], cuts = new Set([0]);
    for (const layer of doc.layers) {
        const k = layer.track || 0, delay = Math.max(20, Codecs.delayFromName(layer.name, defaultDelay));
        const start = (ends[k] || 0) + Math.max(0, layer.gap || 0);
        ends[k] = start + delay;
        if (!layer.visible && !withHidden)
            continue;
        spans.push({ layer, k, start, end: start + delay });
        cuts.add(start).add(start + delay);
        if (layer.motion && Motion.isAnimated(layer.motion)) {
            const step = Math.max(10, layer.motion.step || 50);
            for (let t = start + step; t < start + delay; t += step)
                cuts.add(t);
        }
    }
    spans.sort((a, b) => a.k - b.k);
    const times = [...cuts].sort((a, b) => a - b), frames = [];
    let lastKey = "";
    for (let c = 0; c + 1 < times.length; c++) {
        const t = times[c], canvas = createCanvas(doc.width, doc.height), ctx = context(canvas);
        const shown = spans.filter((s) => t >= s.start && t < s.end);
        for (const s of shown) {
            ctx.globalAlpha = s.layer.opacity / 255;
            ctx.drawImage(s.layer.motion && !Motion.isEmpty(s.layer.motion) ? Motion.render(s.layer.canvas, Motion.sample(s.layer.motion, t - s.start)) : s.layer.canvas, 0, 0);
        }
        // Los que no cambian respecto al anterior se juntan (dura más).
        const key = shown.map((s) => `${s.layer.id}:${s.layer.motion && Motion.isAnimated(s.layer.motion) ? t - s.start : ""}`).join("|");
        if (frames.length && key === lastKey) {
            frames[frames.length - 1].delay += times[c + 1] - t;
            continue;
        }
        lastKey = key;
        frames.push({ width: doc.width, height: doc.height, data: ctx.getImageData(0, 0, doc.width, doc.height).data, delay: times[c + 1] - t, hidden: false });
    }
    return frames;
}

export function animationFrames(doc, defaultDelay, withHidden = false) {
    // Varias pistas del editor de GIF: se combinan.
    if (doc.layers.some((l) => l.track > 0 || l.gap > 0)) {
        const combined = trackFrames(doc, defaultDelay, withHidden);
        return combined.length ? combined : [{ ...flattenedImage(doc), delay: defaultDelay }];
    }
    const frames = [];
    for (const layer of doc.layers) {
        if (!layer.visible && !withHidden)
            continue;
        const delay = Codecs.delayFromName(layer.name, defaultDelay);
        // Un fotograma con puntos clave (editor de GIF): varios, uno cada poco, con la imagen movida.
        const parts = layer.motion && !Motion.isEmpty(layer.motion)
            ? Motion.expand(layer.canvas, layer.motion, Math.max(20, delay))
            : [{ canvas: layer.canvas, delay }];
        for (const part of parts) {
            const data = context(part.canvas).getImageData(0, 0, doc.width, doc.height).data;
            if (layer.opacity < 255)
                for (let i = 3; i < data.length; i += 4)
                    data[i] = Math.trunc((data[i] * layer.opacity) / 255);
            frames.push({ width: doc.width, height: doc.height, data, delay: part.delay, hidden: !layer.visible });
        }
    }
    if (!frames.length)
        frames.push({ ...flattenedImage(doc), delay: defaultDelay });
    return frames;
}

// El archivo de un documento en el formato elegido con sus opciones (ImageIO::save de Kaint).
// 'frames': los fotogramas del GIF tal como quedaron en la configuración avanzada.
export async function encode(doc, format, options = defaultSaveOptions(), frames = null) {
    if (typeof options === "number")
        options = { ...defaultSaveOptions(), quality: options };
    if (format.ext === "pdn")
        return encodePdnFile(doc);
    if (format.ext === "psd" || format.ext === "psb")
        return encodePsdFile(doc, format.ext === "psb");
    if (format.layered)
        return encodeOra(doc);
    if (format.plugin)
        return encodePlugin(doc, format);
    // Un icono abierto con sus versiones como capas («32 × 32»...): cada capa visible, una versión.
    if ((format.ext === "ico" || format.ext === "cur") && doc.layers.length > 1 && doc.layers.every((l) => Codecs.icoSizeFromName(l.name))) {
        const images = doc.layers.filter((l) => l.visible).map((l) => {
            const size = Codecs.icoSizeFromName(l.name), w = Math.min(size.width, doc.width), h = Math.min(size.height, doc.height);
            return { width: w, height: h, data: l.ctx.getImageData(0, 0, w, h).data };
        });
        if (images.length) {
            const ico = await Codecs.encodeIcoImages(images);
            return new Blob([format.ext === "cur" ? Codecs.icoToCur(ico) : ico], { type: format.mime });
        }
    }
    let bytes;
    if (frames?.length)
        bytes = encodeAnimation(frames, options);
    else if (format.animation && options.animation && doc.layers.length > 1)
        bytes = encodeAnimation(animationFrames(doc, options.frameDelay, options.gifHiddenLayers), options);
    else
        bytes = await encodeImage(prepare(flattenedImage(doc), format.ext, options), format.ext, options, { dpi: doc.dpi, icc: embeddedProfile(doc) });
    return new Blob([bytes], { type: format.mime });
}

// Los modos de fusión con los nombres de paint.js.
const pluginBlend = { colorburn: "colorBurn", colordodge: "colorDodge" };

// Formato de un complemento: recibe las capas (o la imagen combinada) en RGBA y devuelve los bytes.
async function encodePlugin(doc, format) {
    const pixelsOf = (canvas) => canvas.getContext("2d").getImageData(0, 0, doc.width, doc.height).data;
    const layers = format.supportsLayers
        ? doc.layers.map((l) => ({ name: l.name, visible: l.visible, opacity: l.opacity, blendMode: pluginBlend[l.blend] || l.blend, data: pixelsOf(l.canvas) }))
        : [{ name: doc.layers[0]?.name || "", visible: true, opacity: 255, blendMode: "normal", data: pixelsOf(doc.flattened()) }];
    const values = Object.fromEntries((format.saveParams || []).map((p) => [p.key, p.type === "choice" ? p.values[p.def] : p.def]));
    const result = await format.encode({ document: { width: doc.width, height: doc.height, resolution: 96, activeLayer: doc.active, layers }, values });
    if (result instanceof Blob)
        return result;
    if (result instanceof ArrayBuffer || ArrayBuffer.isView(result))
        return new Blob([result], { type: format.mime });
    throw new Error(format.name);
}

// Photoshop con todas las capas y la imagen combinada (Psd.js).
function encodePsdFile(doc, psb = false) {
    const layers = doc.layers.map((l) => ({ name: l.name, visible: l.visible, opacity: l.opacity, blend: l.blend, alphaLock: l.alphaLock,
                                            data: l.ctx.getImageData(0, 0, doc.width, doc.height).data }));
    const bytes = encodePsd({ width: doc.width, height: doc.height, dpi: doc.dpi, active: doc.active, icc: embeddedProfile(doc), layers,
                              composite: flattenedImage(doc).data }, psb);
    return new Blob([bytes], { type: "image/vnd.adobe.photoshop" });
}

// Paint.NET con todas las capas (Codecs.encodePdn) y la miniatura de la cabecera.
async function encodePdnFile(doc) {
    const flat = doc.flattened();
    const thumb = document.createElement("canvas");
    const k = Math.min(1, 256 / Math.max(doc.width, doc.height));
    thumb.width = Math.max(1, Math.round(doc.width * k));
    thumb.height = Math.max(1, Math.round(doc.height * k));
    thumb.getContext("2d").drawImage(flat, 0, 0, thumb.width, thumb.height);
    const png = new Uint8Array(await (await canvasBlob(thumb)).arrayBuffer());
    const layers = doc.layers.map((l) => ({ name: l.name, visible: l.visible, opacity: l.opacity, blend: l.blend,
                                            data: l.ctx.getImageData(0, 0, doc.width, doc.height).data }));
    const bytes = await Codecs.encodePdn({ width: doc.width, height: doc.height, dpi: doc.dpi, layers }, png);
    return new Blob([bytes], { type: "application/octet-stream" });
}

async function encodeOra(doc) {
    const enc = new TextEncoder();
    const bytes = async (blob) => new Uint8Array(await blob.arrayBuffer());
    const entries = [{ name: "mimetype", data: enc.encode("image/openraster") }];
    const escape = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[c]);
    let xml = `<?xml version="1.0" encoding="UTF-8"?>\n<image version="0.0.5" w="${doc.width}" h="${doc.height}">\n <stack>\n`;
    for (let i = doc.layers.length - 1; i >= 0; --i) {
        const layer = doc.layers[i];
        const src = `data/layer${i}.png`;
        entries.push({ name: src, data: await bytes(await canvasBlob(layer.canvas)) });
        xml += `  <layer name="${escape(layer.name)}" src="${src}" x="0" y="0" opacity="${(layer.opacity / 255).toFixed(3)}"` +
               ` visibility="${layer.visible ? "visible" : "hidden"}" composite-op="${OraNames[layer.blend]}"` +
               (layer.alphaLock ? ` kaint-alpha-lock="true"` : "") +
               (layer.track > 0 ? ` kaint-gif-track="${layer.track}"` : "") +
               (layer.gap > 0 ? ` kaint-gif-gap="${layer.gap}"` : "") +
               (layer.motion ? ` kaint-motion="${escape(JSON.stringify(layer.motion))}"` : "") + `/>\n`;
    }
    xml += " </stack>\n</image>\n";
    entries.push({ name: "stack.xml", data: enc.encode(xml) });
    // Las pistas de audio del editor de GIF: cada archivo tal cual y, aparte, dónde va cada uno.
    if (doc.audioClips?.length) {
        const json = doc.audioClips.map((c, k) => {
            const src = `data/audio${k}.${c.suffix || "bin"}`;
            entries.push({ name: src, data: c.data });
            return clipToJson(c, src);
        });
        entries.push({ name: "kaint-audio.json", data: enc.encode(JSON.stringify(json)) });
    }
    const flat = doc.flattened();
    entries.push({ name: "mergedimage.png", data: await bytes(await canvasBlob(flat)) });
    const thumb = document.createElement("canvas");
    const k = Math.min(1, 256 / Math.max(doc.width, doc.height));
    thumb.width = Math.max(1, Math.round(doc.width * k));
    thumb.height = Math.max(1, Math.round(doc.height * k));
    thumb.getContext("2d").drawImage(flat, 0, 0, thumb.width, thumb.height);
    entries.push({ name: "Thumbnails/thumbnail.png", data: await bytes(await canvasBlob(thumb)) });
    return zip(entries);
}

// Descarga (o, con la API de archivos del navegador, el archivo elegido). Dentro de Kaint.APK (la
// versión para Android), Android pregunta dónde guardarlo.
export function download(blob, name) {
    if (window.KaintAndroid?.saveFile) {
        const reader = new FileReader();
        reader.onload = () => window.KaintAndroid.saveFile(name, blob.type || "application/octet-stream", String(reader.result).split(",")[1] || "");
        reader.readAsDataURL(blob);
        return;
    }
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
}
