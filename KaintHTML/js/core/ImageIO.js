// Abrir y guardar: las imágenes que entiende el navegador (PNG, JPEG, WebP, GIF, BMP, AVIF...) y
// OpenRaster (.ora) con sus capas, como src/core/Ora.cpp de Kaint.
import { Document, Layer } from "./Document.js";
import { OraNames } from "./Raster.js";
import { unzip, zip } from "./Zip.js";
import * as Codecs from "./Codecs.js";
import { iccFromImage, isSrgb, readIcc } from "./ColorProfiles.js";
import { defaultSaveOptions, encodeAnimation, encodeImage, prepare } from "./Encode.js";
import { tr } from "../i18n.js";

// Tipos de "Guardar como", en el orden de Kaint (ImageIO.cpp).
export const SaveFormats = [
    { ext: "ora", mime: "image/openraster", name: "OpenRaster con capas", layered: true },
    { ext: "png", mime: "image/png", name: "PNG" },
    { ext: "jpg", mime: "image/jpeg", name: "JPEG", quality: true, aliases: ["jpeg", "jpe", "jfif"] },
    { ext: "webp", mime: "image/webp", name: "WebP", quality: true },
    { ext: "dds", mime: "image/vnd-ms.dds", name: "Superficie de DirectDraw (DDS)" },
    { ext: "tiff", mime: "image/tiff", name: "TIFF", aliases: ["tif"] },
    { ext: "gif", mime: "image/gif", name: "GIF", animation: true },
    { ext: "bmp", mime: "image/bmp", name: "BMP" },
    { ext: "tga", mime: "image/x-tga", name: "TGA" },
    { ext: "ico", mime: "image/x-icon", name: "Icono de Windows" },
];

// Lo que se puede abrir (además de lo que entienda el navegador).
export const OpenAccept = "image/*,.ora,.pdn,.tga,.tif,.tiff,.dds,.ico,.cur";
export const OpenExtensions = [".png", ".jpg", ".jpeg", ".jpe", ".jfif", ".webp", ".gif", ".bmp", ".avif", ".ico", ".ora", ".pdn", ".tga",
                               ".tif", ".tiff", ".dds"];

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
        layer.opacity = l.opacity ?? 255;
        layer.blend = l.blend || "normal";
        return layer;
    });
    return doc;
}

// Abre una imagen como Kaint (ImageIO::load): OpenRaster y Paint.NET con sus capas, el GIF
// animado con un fotograma por capa (la duración en el nombre), TGA, TIFF y DDS, y lo demás con
// el navegador.
export async function openFile(file) {
    const ext = extensionOf(file.name), name = baseName(file.name);
    const head = new Uint8Array(await file.slice(0, 16).arrayBuffer());
    const magic = String.fromCharCode(...head.subarray(0, 4));
    let doc;
    if (ext === "ora") {
        doc = await openOra(file);
    } else if (ext === "pdn" || magic === "PDN3") {
        const pdn = await Codecs.decodePdn(new Uint8Array(await file.arrayBuffer()));
        doc = documentFrom(pdn.width, pdn.height, name, pdn.layers.map((l, i) => ({ ...l, name: l.name || tr("Capa %1", i + 1) })));
        doc.active = doc.layers.length - 1;
    } else if (magic.startsWith("GIF")) {
        const anim = Codecs.decodeGif(new Uint8Array(await file.arrayBuffer()));
        const { width, height } = anim.frames[0];
        doc = anim.frames.length === 1
            ? documentFrom(width, height, name, [{ name: tr("Fondo"), data: anim.frames[0].data }])
            : documentFrom(width, height, name, anim.frames.map((f, i) => ({ name: Codecs.nameWithDelay(tr("Fotograma %1", i + 1), f.delay), data: f.data })));
        doc.active = 0;
        doc.gifLoops = anim.loops;
    } else if (ext === "tga" || magic === "DDS " || magic === "II*\0" || magic === "MM\0*") {
        const bytes = new Uint8Array(await file.arrayBuffer());
        const image = magic === "DDS " ? Codecs.decodeDds(bytes) : ext === "tga" ? Codecs.decodeTga(bytes) : await Codecs.decodeTiff(bytes);
        doc = documentFrom(image.width, image.height, name, [{ name: tr("Fondo"), data: image.data }]);
        if (image.dpi)
            doc.dpi = image.dpi;
        doc.colorProfile = await profileOf(bytes);
    } else {
        const bytes = new Uint8Array(await file.arrayBuffer()), profile = await profileOf(bytes);
        const image = await decodeImage(file, profile ? "none" : "default");
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
        doc.layers.push(layer);
    }
    if (!doc.layers.length)
        doc.layers.push(new Layer(width, height, tr("Fondo")));
    doc.active = doc.layers.length - 1;
    doc.fileName = file.name;
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
export function animationFrames(doc, defaultDelay, withHidden = false) {
    const frames = [];
    for (const layer of doc.layers) {
        if (!layer.visible && !withHidden)
            continue;
        const data = layer.ctx.getImageData(0, 0, doc.width, doc.height).data;
        if (layer.opacity < 255)
            for (let i = 3; i < data.length; i += 4)
                data[i] = Math.trunc((data[i] * layer.opacity) / 255);
        frames.push({ width: doc.width, height: doc.height, data, delay: Codecs.delayFromName(layer.name, defaultDelay), hidden: !layer.visible });
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
    if (format.layered)
        return encodeOra(doc);
    if (format.plugin)
        return encodePlugin(doc, format);
    let bytes;
    if (frames?.length)
        bytes = encodeAnimation(frames, options);
    else if (format.animation && options.animation && doc.layers.length > 1)
        bytes = encodeAnimation(animationFrames(doc, options.frameDelay, options.gifHiddenLayers), options);
    else
        bytes = await encodeImage(prepare(flattenedImage(doc), format.ext, options), format.ext, options, { dpi: doc.dpi, icc: embeddedProfile(doc) });
    return new Blob([bytes], { type: format.mime });
}

// Formato de un complemento: recibe las capas (o la imagen combinada) en RGBA y devuelve los bytes.
async function encodePlugin(doc, format) {
    const pixelsOf = (canvas) => canvas.getContext("2d").getImageData(0, 0, doc.width, doc.height).data;
    const layers = format.supportsLayers
        ? doc.layers.map((l) => ({ name: l.name, visible: l.visible, opacity: l.opacity, blendMode: l.blend, data: pixelsOf(l.canvas) }))
        : [{ name: doc.layers[0]?.name || "", visible: true, opacity: 255, blendMode: "normal", data: pixelsOf(doc.flattened()) }];
    const values = Object.fromEntries((format.saveParams || []).map((p) => [p.key, p.type === "choice" ? p.values[p.def] : p.def]));
    const result = await format.encode({ document: { width: doc.width, height: doc.height, resolution: 96, activeLayer: doc.active, layers }, values });
    if (result instanceof Blob)
        return result;
    if (result instanceof ArrayBuffer || ArrayBuffer.isView(result))
        return new Blob([result], { type: format.mime });
    throw new Error(format.name);
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
               ` visibility="${layer.visible ? "visible" : "hidden"}" composite-op="${OraNames[layer.blend]}"/>\n`;
    }
    xml += " </stack>\n</image>\n";
    entries.push({ name: "stack.xml", data: enc.encode(xml) });
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

// Descarga (o, con la API de archivos del navegador, el archivo elegido).
export function download(blob, name) {
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 5000);
}
