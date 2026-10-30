// Abrir y guardar: las imágenes que entiende el navegador (PNG, JPEG, WebP, GIF, BMP, AVIF...) y
// OpenRaster (.ora) con sus capas, como src/core/Ora.cpp de Kaint.
import { Document, Layer } from "./Document.js";
import { OraNames } from "./Raster.js";
import { crc32, unzip, zip } from "./Zip.js";
import { tr } from "../i18n.js";

export const SaveFormats = [
    { ext: "png", mime: "image/png", name: "PNG" },
    { ext: "jpg", mime: "image/jpeg", name: "JPEG", quality: true },
    { ext: "webp", mime: "image/webp", name: "WebP", quality: true },
    { ext: "ora", mime: "image/openraster", name: "OpenRaster", layered: true },
];

export const OpenAccept = "image/*,.ora";

export function formatFor(name) {
    const ext = (name.split(".").pop() || "").toLowerCase();
    return SaveFormats.find((f) => f.ext === ext || (ext === "jpeg" && f.ext === "jpg")) || SaveFormats[0];
}

export function baseName(name) {
    return name.replace(/\.[^.]+$/, "");
}

async function decodeImage(blob) {
    if ("createImageBitmap" in window)
        return createImageBitmap(blob);
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

export async function openFile(file) {
    if (/\.ora$/i.test(file.name))
        return openOra(file);
    const image = await decodeImage(file);
    const doc = Document.fromImage(image, baseName(file.name));
    doc.fileName = file.name;
    doc.dpi = readResolution(new Uint8Array(await file.slice(0, 65536).arrayBuffer())) || 96;
    return doc;
}

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

async function withResolution(blob, mime, dpi) {
    if (!dpi)
        return blob;
    const b = new Uint8Array(await blob.arrayBuffer());
    if (mime === "image/png" && b[12] === 0x49 && b[13] === 0x48) {
        // pHYs justo después de IHDR (en píxeles por metro).
        const ppm = Math.round(dpi / 0.0254), chunk = new Uint8Array(21), view = new DataView(chunk.buffer);
        view.setUint32(0, 9);
        chunk.set([0x70, 0x48, 0x59, 0x73], 4);
        view.setUint32(8, ppm);
        view.setUint32(12, ppm);
        chunk[16] = 1;
        view.setUint32(17, crc32(chunk.subarray(4, 17)));
        const at = 8 + 25;
        return new Blob([b.subarray(0, at), chunk, b.subarray(at)], { type: mime });
    }
    if (mime === "image/jpeg" && b[2] === 0xff && b[3] === 0xe0 && String.fromCharCode(...b.slice(6, 11)) === "JFIF\0") {
        const d = Math.min(65535, Math.round(dpi));
        b[13] = 1;
        b[14] = d >> 8; b[15] = d & 255;
        b[16] = d >> 8; b[17] = d & 255;
        return new Blob([b], { type: mime });
    }
    return blob;
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

// El archivo de un documento en el formato elegido (quality de 0 a 100).
export async function encode(doc, format, quality = 95) {
    if (format.layered)
        return encodeOra(doc);
    if (format.plugin)
        return encodePlugin(doc, format);
    const flat = doc.flattened();
    if (format.mime === "image/jpeg") {
        // Sin transparencia: sobre blanco, como al guardar JPEG en Kaint.
        const ctx = flat.getContext("2d");
        ctx.globalCompositeOperation = "destination-over";
        ctx.fillStyle = "#fff";
        ctx.fillRect(0, 0, flat.width, flat.height);
    }
    return withResolution(await canvasBlob(flat, format.mime, format.quality ? quality / 100 : undefined), format.mime, doc.dpi);
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
