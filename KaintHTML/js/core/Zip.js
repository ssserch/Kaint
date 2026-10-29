// ZIP mínimo para OpenRaster: escribe sin comprimir (los PNG ya lo están) y lee archivos sin
// comprimir o con deflate (DecompressionStream del navegador).

const crcTable = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; ++n) {
        let c = n;
        for (let k = 0; k < 8; ++k)
            c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();

export function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; ++i)
        c = crcTable[(c ^ bytes[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

// entries: [{ name, data: Uint8Array }] → Blob (el primero, "mimetype" en OpenRaster).
export function zip(entries) {
    const enc = new TextEncoder();
    const parts = [], central = [];
    let offset = 0;
    for (const entry of entries) {
        const name = enc.encode(entry.name);
        const crc = crc32(entry.data);
        const local = new DataView(new ArrayBuffer(30));
        local.setUint32(0, 0x04034b50, true);
        local.setUint16(4, 20, true);
        local.setUint16(8, 0, true); // sin compresión
        local.setUint32(14, crc, true);
        local.setUint32(18, entry.data.length, true);
        local.setUint32(22, entry.data.length, true);
        local.setUint16(26, name.length, true);
        parts.push(new Uint8Array(local.buffer), name, entry.data);
        const dir = new DataView(new ArrayBuffer(46));
        dir.setUint32(0, 0x02014b50, true);
        dir.setUint16(4, 20, true);
        dir.setUint16(6, 20, true);
        dir.setUint32(16, crc, true);
        dir.setUint32(20, entry.data.length, true);
        dir.setUint32(24, entry.data.length, true);
        dir.setUint16(28, name.length, true);
        dir.setUint32(42, offset, true);
        central.push(new Uint8Array(dir.buffer), name);
        offset += 30 + name.length + entry.data.length;
    }
    const size = central.reduce((s, p) => s + p.length, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, size, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, new Uint8Array(end.buffer)], { type: "application/zip" });
}

// Lee un ZIP (ArrayBuffer) → Map(nombre → Uint8Array).
export async function unzip(buffer) {
    const view = new DataView(buffer);
    let end = buffer.byteLength - 22;
    while (end >= 0 && view.getUint32(end, true) !== 0x06054b50)
        --end;
    if (end < 0)
        throw new Error("ZIP");
    const count = view.getUint16(end + 10, true);
    let p = view.getUint32(end + 16, true);
    const files = new Map();
    const dec = new TextDecoder();
    for (let i = 0; i < count; ++i) {
        const method = view.getUint16(p + 10, true);
        const compressed = view.getUint32(p + 20, true);
        const nameLen = view.getUint16(p + 28, true), extraLen = view.getUint16(p + 30, true), commentLen = view.getUint16(p + 32, true);
        const local = view.getUint32(p + 42, true);
        const name = dec.decode(new Uint8Array(buffer, p + 46, nameLen));
        const dataStart = local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true);
        const raw = new Uint8Array(buffer, dataStart, compressed);
        if (method === 0) {
            files.set(name, raw);
        } else if (method === 8) {
            const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
            files.set(name, new Uint8Array(await new Response(stream).arrayBuffer()));
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    return files;
}
