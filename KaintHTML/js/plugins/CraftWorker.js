// Ejecuta un filtro de PhotoCraft (.wasm) aparte de la página (Craft.js): un módulo que tarda
// demasiado o se cuelga se detiene terminando este Web Worker, sin congelar Kaint. Cada mensaje
// usa una instancia nueva del módulo, sin nada importado (ni archivos, ni red, ni reloj).
const FORMAT_RGBA8 = 8 | (1 << 8) | (3 << 16);

self.onmessage = async ({ data }) => {
    try {
        const instance = await WebAssembly.instantiate(data.module, {});
        const ex = instance.exports, memory = ex.memory;
        if (!(memory instanceof WebAssembly.Memory))
            throw new Error("the module must export its memory as `memory`");
        if (data.op === "manifest") {
            const abi = ex.pc_abi_version();
            const packed = BigInt.asUintN(64, BigInt(ex.pc_manifest()));
            const ptr = Number(packed & 0xffffffffn), len = Number(packed >> 32n);
            if (len > 64 * 1024 || ptr + len > memory.buffer.byteLength)
                throw new Error("pc_manifest points outside memory");
            self.postMessage({ abi, manifest: new TextDecoder().decode(new Uint8Array(memory.buffer, ptr, len).slice()) });
            return;
        }
        const pixels = data.pixels, bytes = pixels.length * 4, params = new TextEncoder().encode(data.params);
        // La memoria con la que empieza el módulo (su pila y sus datos) se reserva antes: algunos
        // complementos reparten desde dentro de ella y si no pisarían sus propios datos.
        const reserve = memory.buffer.byteLength;
        if (reserve)
            ex.pc_alloc(reserve);
        const ptr = ex.pc_alloc(bytes) >>> 0, pptr = ex.pc_alloc(Math.max(1, params.length)) >>> 0;
        if (!ptr || !pptr)
            throw new Error("pc_alloc returned 0 (out of memory)");
        if (ptr + bytes > memory.buffer.byteLength || pptr + params.length > memory.buffer.byteLength)
            throw new Error("pc_alloc returned a block outside memory");
        new Uint8Array(memory.buffer, ptr, bytes).set(new Uint8Array(pixels.buffer, pixels.byteOffset, bytes));
        new Uint8Array(memory.buffer, pptr, params.length).set(params);
        const rc = ex.pc_filter(ptr, bytes, data.width, data.height, 4, FORMAT_RGBA8, pptr, params.length);
        if (rc !== 0)
            throw new Error(`pc_filter returned error code ${rc}`);
        if (ptr + bytes > memory.buffer.byteLength)
            throw new Error("the pixel buffer is no longer in memory");
        const out = new Float32Array(pixels.length);
        new Uint8Array(out.buffer).set(new Uint8Array(memory.buffer, ptr, bytes));
        self.postMessage({ pixels: out }, [out.buffer]);
    } catch (e) {
        self.postMessage({ error: String(e?.message || e) });
    }
};
