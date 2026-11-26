// Pistas de audio del editor de GIF (van en los vídeos exportados; el GIF no tiene sonido), como las
// de Kaint de escritorio (core/Audio): cada clip es un archivo de audio guardado dentro de la imagen
// (va dentro del .ora) colocado en la línea de tiempo, con su volumen (y puntos clave de volumen) y
// fundidos. Un clip: { name, data (Uint8Array), suffix, row, start, offset, duration, length (ms),
// volume: { base, keys: [{ t, value }] } (%), fadeIn, fadeOut (ms), muted, peaks (Uint8Array) }.

// La envolvente para dibujar la onda: un valor de 0 a 255 cada PeakMs del archivo.
export const PeakMs = 20;
export const AudioExtensions = ["mp3", "wav", "ogg", "oga", "opus", "flac", "m4a", "aac", "wma", "aiff", "aif", "webm"];

export function isAudioFile(file) {
    const ext = (file.name || "").split(".").pop().toLowerCase();
    return AudioExtensions.includes(ext) || (file.type || "").startsWith("audio/");
}

// Cada archivo se decodifica una vez (para la onda, la vista previa y la exportación).
const decoded = new WeakMap();
export function decodeClip(clip, sampleRate = 48000) {
    let p = decoded.get(clip.data);
    if (!p) {
        const ctx = new OfflineAudioContext(1, 1, sampleRate);
        p = ctx.decodeAudioData(clip.data.slice().buffer);
        decoded.set(clip.data, p);
    }
    return p;
}

export async function analyzeAudio(file) {
    const data = new Uint8Array(await file.arrayBuffer());
    const clip = { data };
    const buffer = await decodeClip(clip);
    const rate = buffer.sampleRate, window = Math.max(1, Math.round((rate * PeakMs) / 1000));
    const channels = [...Array(buffer.numberOfChannels).keys()].map((c) => buffer.getChannelData(c));
    const peaks = new Uint8Array(Math.ceil(buffer.length / window));
    for (let w = 0; w < peaks.length; w++) {
        let peak = 0;
        for (let i = w * window; i < Math.min(buffer.length, (w + 1) * window); i++) {
            let s = 0;
            for (const ch of channels)
                s += ch[i];
            peak = Math.max(peak, Math.abs(s / channels.length));
        }
        peaks[w] = Math.min(255, Math.round(Math.sqrt(peak) * 255));
    }
    const ext = (file.name || "").split(".").pop().toLowerCase();
    const length = Math.max(1, Math.round((buffer.length / rate) * 1000));
    return { name: (file.name || "audio").replace(/\.[^.]+$/, ""), data, suffix: ext, row: 0, start: 0, offset: 0, duration: length, length,
             volume: { base: 100, keys: [] }, fadeIn: 0, fadeOut: 0, muted: false, peaks };
}

// El volumen (%) sin los fundidos: los puntos clave en línea recta de uno a otro.
export function keyedVolume(clip, t) {
    const keys = clip.volume.keys;
    if (!keys.length)
        return clip.volume.base;
    if (t <= keys[0].t)
        return keys[0].value;
    if (t >= keys[keys.length - 1].t)
        return keys[keys.length - 1].value;
    let i = 0;
    while (i + 1 < keys.length && keys[i + 1].t <= t)
        i++;
    const a = keys[i], b = keys[i + 1];
    return b.t > a.t ? a.value + ((b.value - a.value) * (t - a.t)) / (b.t - a.t) : b.value;
}

// El volumen (%) en 't' (ms desde el principio del clip), con los fundidos.
export function volumeAt(clip, t) {
    let v = keyedVolume(clip, t);
    if (clip.fadeIn > 0 && t < clip.fadeIn)
        v *= Math.max(0, t) / clip.fadeIn;
    if (clip.fadeOut > 0 && t > clip.duration - clip.fadeOut)
        v *= Math.max(0, clip.duration - t) / clip.fadeOut;
    return Math.max(0, Math.min(200, v));
}

export function setKey(clip, t, value) {
    const keys = clip.volume.keys.filter((k) => k.t !== t);
    keys.push({ t, value });
    keys.sort((a, b) => a.t - b.t);
    clip.volume = { ...clip.volume, keys };
}

export const copyClip = (c) => ({ ...c, volume: { base: c.volume.base, keys: c.volume.keys.map((k) => ({ ...k })) } });

// Como en el .ora de Kaint de escritorio (kaint-audio.json).
export function clipToJson(c, src) {
    let peaks = "";
    for (let i = 0; i < c.peaks.length; i += 0x8000)
        peaks += String.fromCharCode(...c.peaks.subarray(i, i + 0x8000));
    return { name: c.name, suffix: c.suffix, row: c.row, start: c.start, offset: c.offset, duration: c.duration, length: c.length,
             volume: c.volume.base, keys: c.volume.keys.map((k) => [k.t, k.value]), fadeIn: c.fadeIn, fadeOut: c.fadeOut, muted: c.muted,
             peaks: btoa(peaks), src };
}

export function clipFromJson(o, data) {
    const length = Math.max(1, o.length | 0);
    const bin = atob(o.peaks || "");
    const peaks = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++)
        peaks[i] = bin.charCodeAt(i);
    return { name: o.name || "audio", data, suffix: o.suffix || "", row: Math.max(0, Math.min(63, o.row | 0)), start: Math.max(0, o.start | 0),
             offset: Math.max(0, o.offset | 0), duration: Math.max(1, Math.min(length, o.duration | 0)), length,
             volume: { base: Math.max(0, Math.min(200, o.volume ?? 100)), keys: (o.keys || []).map(([t, value]) => ({ t, value })).sort((a, b) => a.t - b.t) },
             fadeIn: Math.max(0, o.fadeIn | 0), fadeOut: Math.max(0, o.fadeOut | 0), muted: !!o.muted, peaks };
}

// El volumen de un clip como automatización de una ganancia (cada 40 ms, en línea recta).
export function automate(gain, clip, when, from = 0, speed = 1) {
    const step = 40;
    gain.setValueAtTime(volumeAt(clip, from) / 100, when);
    for (let t = from + step; t <= clip.duration; t += step)
        gain.linearRampToValueAtTime(volumeAt(clip, Math.min(t, clip.duration)) / 100, when + (t - from) / 1000 / speed);
}

// La mezcla de todos los clips (los silenciados no suenan), de 0 a 'totalMs'.
export async function renderMix(clips, totalMs, sampleRate = 48000) {
    const active = clips.filter((c) => !c.muted && c.duration > 0 && c.start < totalMs);
    if (!active.length || typeof OfflineAudioContext === "undefined")
        return null;
    const ctx = new OfflineAudioContext(2, Math.max(1, Math.ceil((totalMs / 1000) * sampleRate)), sampleRate);
    for (const c of active) {
        const source = ctx.createBufferSource();
        source.buffer = await decodeClip(c);
        const gain = ctx.createGain();
        automate(gain.gain, c, c.start / 1000);
        source.connect(gain).connect(ctx.destination);
        source.start(c.start / 1000, c.offset / 1000, c.duration / 1000);
    }
    return ctx.startRendering();
}
