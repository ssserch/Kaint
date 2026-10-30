// "Configuración de guardado" de Kaint (SaveConfigDialog y EncodedPreview): las opciones del
// formato con una vista previa del archivo tal como se guardará (codificado en segundo plano y
// vuelto a leer) y lo que ocupará; en el GIF animado, también la configuración avanzada, con los
// fotogramas uno por uno, como ezgif.
import { IcoSizes, MIN_DELAY } from "../core/Codecs.js";
import { decodeForPreview, defaultSaveOptions, encodeJob, traits } from "../core/Encode.js";
import { checkerPattern, createCanvas } from "../core/Raster.js";
import { Dialog, el } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { currentLanguage, tr } from "../i18n.js";

const clone = (v) => structuredClone(v);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Tamaño de un archivo (QLocale::formattedDataSize de Kaint).
export function formattedSize(bytes) {
    if (bytes < 1024)
        return tr("%1 bytes", bytes);
    const units = ["KiB", "MiB", "GiB"];
    let v = bytes / 1024, u = 0;
    while (v >= 1024 && u < units.length - 1) {
        v /= 1024;
        ++u;
    }
    return `${v.toLocaleString(currentLanguage(), { minimumFractionDigits: 2, maximumFractionDigits: 2 })} ${units[u]}`;
}

// Barra con el valor dentro (BarSlider de Kaint).
function barSlider(min, max, value, onChange) {
    const bar = el("div", { className: "barslider", tabIndex: 0 });
    const fill = el("div", { className: "fill" }), label = el("div", { className: "label" });
    bar.append(fill, label);
    let current = value;
    const paint = () => {
        const f = max > min ? (current - min) / (max - min) : 0;
        fill.style.width = `${f * 100}%`;
        label.textContent = String(current);
        label.classList.toggle("inside", f > 0.25);
    };
    const set = (v, emit = true) => {
        v = clamp(Math.round(v), min, max);
        if (v === current)
            return;
        current = v;
        paint();
        if (emit)
            onChange(v);
    };
    const fromEvent = (e) => {
        const r = bar.getBoundingClientRect();
        set(min + clamp((e.clientX - r.left) / Math.max(1, r.width), 0, 1) * (max - min));
    };
    bar.addEventListener("pointerdown", (e) => {
        if (bar.classList.contains("disabled"))
            return;
        bar.setPointerCapture(e.pointerId);
        fromEvent(e);
    });
    bar.addEventListener("pointermove", (e) => bar.hasPointerCapture(e.pointerId) && fromEvent(e));
    bar.addEventListener("wheel", (e) => {
        e.preventDefault();
        if (!bar.classList.contains("disabled"))
            set(current + (e.deltaY < 0 ? 1 : -1));
    }, { passive: false });
    bar.addEventListener("keydown", (e) => {
        const step = e.key === "ArrowRight" || e.key === "ArrowUp" ? 1 : e.key === "ArrowLeft" || e.key === "ArrowDown" ? -1 : 0;
        if (step && !bar.classList.contains("disabled")) {
            e.preventDefault();
            set(current + step);
        }
    });
    paint();
    return Object.assign(bar, {
        value: () => current,
        setValue: (v) => set(v, false),
        setEnabled: (on) => bar.classList.toggle("disabled", !on),
    });
}

function spin(min, max, value, step = 1) {
    const input = el("input", { type: "number", min, max, step, value });
    input.read = () => clamp(Math.round(Number(input.value) || 0), min, max);
    return input;
}

const withUnit = (field, unit, note = "") => el("span", { className: "with-unit" }, [field, unit && el("span", { textContent: unit }),
                                                                                    note && el("span", { className: "note", textContent: note })]);

// --- Vista previa -------------------------------------------------------------------------

// Lo que se va a guardar: se codifica en un Web Worker y se muestra lo que sale al volver a leerlo
// (en el GIF, todos sus fotogramas, con un botón para reproducirlo).
export class EncodedPreview {
    constructor(ext, onStatus) {
        this.ext = ext;
        this.onStatus = onStatus;
        this.frames = [];
        this.index = 0;
        this.loops = 0;
        this.generation = 0;
        this.zoom = el("select", {}, [el("option", { value: 0, textContent: tr("Ajustar") }),
                                      ...[100, 200, 400].map((z) => el("option", { value: z, textContent: `${z} %` }))]);
        this.zoom.addEventListener("change", () => this.showFrame(this.index));
        this.canvas = el("canvas", { className: "encoded-image" });
        this.play = el("button", { className: "tbutton encoded-play", type: "button", disabled: true });
        this.play.addEventListener("click", () => this.setPlaying(!this.playing));
        this.frameLabel = el("span");
        const animation = !!traits(ext).animation;
        this.element = el("div", { className: "encoded-preview" }, [
            el("div", { className: "row" }, [el("span", { textContent: tr("Vista previa:") }), el("span", { style: { flex: 1 } }), this.zoom]),
            el("div", { className: "encoded-scroll" }, [this.canvas]),
            el("div", { className: "row", style: { display: animation ? "" : "none" } }, [this.play, this.frameLabel]),
        ]);
        this.setPlaying(false);
    }

    // Vuelve a codificar (con un pequeño retraso, por si llegan más cambios seguidos). 'loops':
    // 0 sin fin, -1 una vez, n más (para reproducirlo igual que el archivo).
    request(job, loops = 0) {
        this.pending = { job, loops };
        this.onStatus?.(tr("Tamaño del archivo: calculando…"));
        clearTimeout(this.debounce);
        this.debounce = setTimeout(() => this.start(), 120);
    }

    start() {
        const { job, loops } = this.pending, id = ++this.generation;
        this.worker?.terminate();
        this.worker = null;
        const done = (result) => {
            if (id !== this.generation)
                return;
            this.worker?.terminate();
            this.worker = null;
            this.encoded(result, loops);
        };
        const here = async () => {
            try {
                const bytes = await encodeJob(job);
                done({ size: bytes.length, frames: await decodeForPreview(bytes, job.ext) });
            } catch (e) {
                done({ error: String(e?.message || e) });
            }
        };
        try {
            const worker = new Worker(new URL("../core/SaveWorker.js", import.meta.url), { type: "module" });
            this.worker = worker;
            worker.onmessage = ({ data }) => done(data);
            worker.onerror = (e) => {
                e.preventDefault?.();
                if (id === this.generation)
                    here();
            };
            worker.postMessage({ id, job });
        } catch {
            here();
        }
    }

    encoded({ size, frames, error }, loops) {
        this.onStatus?.(error ? tr("No se pudo codificar: %1", error) : tr("Tamaño del archivo: %1", formattedSize(size)));
        this.frames = (frames || []).map((f) => {
            if (f.bitmap)
                return f;
            const c = createCanvas(f.width, f.height);
            c.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(f.data), f.width, f.height), 0, 0);
            return { ...f, bitmap: c };
        });
        this.loops = loops;
        this.index = clamp(this.index, 0, Math.max(0, this.frames.length - 1));
        this.play.disabled = this.frames.length < 2;
        if (this.frames.length < 2)
            this.setPlaying(false);
        this.showFrame(this.index);
    }

    showFrame(index) {
        this.frameLabel.textContent = this.frames.length > 1 ? tr("Fotograma %1 de %2", index + 1, this.frames.length) : "";
        const f = this.frames[index];
        if (!f)
            return;
        this.index = index;
        const zoom = Number(this.zoom.value);
        let w = f.width, h = f.height;
        if (!zoom) {
            const s = Math.min(1, 340 / w, 260 / h);
            w = Math.max(1, Math.round(w * s));
            h = Math.max(1, Math.round(h * s));
        } else {
            w = Math.round((w * zoom) / 100);
            h = Math.round((h * zoom) / 100);
        }
        const dpr = window.devicePixelRatio || 1;
        this.canvas.width = Math.round(w * dpr);
        this.canvas.height = Math.round(h * dpr);
        this.canvas.style.width = `${w}px`;
        this.canvas.style.height = `${h}px`;
        const p = this.canvas.getContext("2d");
        p.setTransform(dpr, 0, 0, dpr, 0, 0);
        p.fillStyle = checkerPattern(p);
        p.fillRect(0, 0, w, h);
        p.imageSmoothingEnabled = !zoom;
        p.imageSmoothingQuality = "high";
        p.drawImage(f.bitmap, 0, 0, w, h);
    }

    get playing() {
        return !!this.playTimer;
    }

    setPlaying(on) {
        clearTimeout(this.playTimer);
        this.playTimer = 0;
        if (on && this.frames.length > 1) {
            // Las repeticiones del archivo: sin fin, una vez o tantas más.
            this.playsLeft = this.loops === 0 ? -1 : this.loops < 0 ? 1 : this.loops + 1;
            if (this.index >= this.frames.length - 1)
                this.showFrame(0);
            this.playTimer = setTimeout(() => this.next(), Math.max(MIN_DELAY, this.frames[this.index].delay));
        }
        this.play.replaceChildren(icon(this.playing ? "media-playback-pause" : "media-playback-start", 16),
                                  el("span", { textContent: this.playing ? tr("Pausa") : tr("Reproducir") }));
    }

    next() {
        this.playTimer = 0;
        if (this.frames.length < 2)
            return this.setPlaying(false);
        let next = this.index + 1;
        if (next >= this.frames.length) {
            if (this.playsLeft > 0 && --this.playsLeft === 0)
                return this.setPlaying(false);
            next = 0;
        }
        this.showFrame(next);
        this.playTimer = setTimeout(() => this.next(), Math.max(MIN_DELAY, this.frames[next].delay));
        this.play.replaceChildren(icon("media-playback-pause", 16), el("span", { textContent: tr("Pausa") }));
    }

    destroy() {
        clearTimeout(this.debounce);
        clearTimeout(this.playTimer);
        this.worker?.terminate();
        ++this.generation;
    }
}

// --- Configuración de guardado ------------------------------------------------------------

// image: la imagen acoplada ({ width, height, data }); frames: los fotogramas del GIF (las capas,
// también las ocultas, marcadas con 'hidden') si hay más de uno. Devuelve null o { options,
// frames }: 'frames' son los fotogramas tal como quedaron en la configuración avanzada (vacío si
// no se guarda como animación).
export async function saveConfigDialog(format, image, initial, frames = [], { dpi = 96, icc = null } = {}) {
    const ext = format.ext, t = traits(ext);
    const dlg = new Dialog(tr("Configuración de guardado: %1", ext.toUpperCase()), { iconName: "document-save" });
    dlg.element.classList.add("save-config");
    const form = el("div", { className: "save-form" });
    const row = (label, control) => {
        const r = el("div", { className: "save-row" }, [el("label", { textContent: label }), control]);
        form.appendChild(r);
        return r;
    };
    const wide = (control) => {
        form.appendChild(el("div", { className: "save-row wide" }, [control]));
        return control;
    };
    const c = {};
    const schedule = () => preview.request({ image, frames: animationFrames(), ext, options: options(), dpi, icc }, options().loops);
    if (t.quality)
        row(tr("Calidad:"), (c.quality = barSlider(1, 100, initial.quality, schedule)));
    if (t.depth) {
        const depths = [[0, tr("Automática")], ...(t.opaque ? [] : [[32, tr("32 bits")]]), [24, tr("24 bits")], [8, tr("8 bits")]];
        c.depth = el("select", {}, depths.map(([v, text]) => el("option", { value: v, textContent: text })));
        c.depth.value = depths.some(([v]) => v === initial.bitDepth) ? initial.bitDepth : 0;
        c.depth.addEventListener("change", () => {
            updateControls();
            schedule();
        });
        row(tr("Profundidad de bits:"), c.depth);
    }
    if (t.depth || t.animation) {
        c.quantization = el("select", {}, [el("option", { value: 1, textContent: tr("Octree") }), el("option", { value: 0, textContent: tr("Corte a la mediana") })]);
        c.quantization.value = initial.quantization;
        c.quantization.addEventListener("change", schedule);
    }
    if (t.depth) {
        row(tr("Algoritmo de cuantización:"), c.quantization);
        row(tr("Nivel de tramado:"), (c.dither = barSlider(0, 8, initial.dither, schedule)));
        if (!t.opaque) {
            row(tr("Umbral de transparencia:"), (c.threshold = barSlider(0, 255, initial.alphaThreshold, schedule)));
            c.thresholdNote = wide(el("div", { className: "note", textContent: tr("Los píxeles que tengan un valor del canal alfa menor que el umbral serán totalmente transparentes.") }));
        }
    }
    if (t.interlace) {
        c.interlaced = el("label", {}, [el("input", { type: "checkbox", checked: initial.interlaced }), tr("Entrelazado")]);
        c.interlaced.title = tr("La imagen se ve entera a baja resolución mientras se carga (Adam7).");
        c.interlaced.firstChild.addEventListener("change", schedule);
        row(tr("Otras opciones:"), c.interlaced);
    }

    // GIF animado
    let advanced = null, showHidden = !!initial.gifHiddenLayers, hiddenLocked = false;
    const defaultAdvanced = () => ({
        entries: frames.map((f, i) => ({ source: i, delay: f.delay, skip: false })).filter((e) => !frames[e.source].hidden || showHidden),
        quantization: 1, globalPalette: false, stack: false, effects: clone(defaultSaveOptions().gifEffects),
    });
    if (t.animation) {
        if (frames.length > 1) {
            c.animation = el("label", {}, [el("input", { type: "checkbox", checked: initial.animation }), tr("Animación: cada capa visible es un fotograma")]);
            c.animation.title = tr("La capa de abajo es el primer fotograma. La duración de cada uno va en el nombre de la capa, p. ej. «Fotograma 1 (100ms)» (Imagen > Animación).");
            c.animation.firstChild.addEventListener("change", () => {
                updateControls();
                schedule();
            });
            wide(c.animation);
            c.delay = spin(MIN_DELAY, 60000, initial.frameDelay, 10);
            c.delay.title = tr("Para las capas que no indican su duración en el nombre");
            c.delay.addEventListener("input", schedule);
            c.delayRow = row(tr("Duración de los fotogramas:"), withUnit(c.delay, tr("ms")));
            c.loopForever = el("label", {}, [el("input", { type: "checkbox", checked: initial.loops === 0 }), tr("Repetir sin fin")]);
            c.loopForever.firstChild.addEventListener("change", () => {
                updateControls();
                schedule();
            });
            wide(c.loopForever);
            c.loops = spin(0, 65535, Math.max(0, initial.loops));
            c.loops.addEventListener("input", schedule);
            c.loopsRow = row(tr("Repeticiones:"), withUnit(c.loops, tr("veces más"), tr("(0: una sola vez)")));
            // Las capas ocultas, como fotogramas solo si se pide.
            c.hidden = wide(el("button", { className: "pushbutton", type: "button" }));
            c.hidden.addEventListener("click", () => setShowHidden(!showHidden));
            advanced = defaultAdvanced();
            Object.assign(advanced, { quantization: initial.quantization, globalPalette: initial.gifGlobalPalette, stack: initial.gifStack,
                                      effects: clone(initial.gifEffects) });
            c.advanced = wide(el("button", { className: "pushbutton", type: "button" }, [icon("configure", 16), tr("Configuración avanzada…")]));
            c.advanced.title = tr("Fotogramas uno por uno (duración, omitir, copiar), paleta global, fundidos y fondo, como en ezgif.");
            c.advanced.addEventListener("click", () => showAdvanced());
        }
        row(tr("Nivel de tramado:"), (c.dither = barSlider(0, 8, initial.dither, schedule)));
        row(tr("Umbral de transparencia:"), (c.threshold = barSlider(0, 255, initial.alphaThreshold, schedule)));
    }
    if (t.icon) {
        c.icoSizes = IcoSizes.map((s) => {
            const label = el("label", {}, [el("input", { type: "checkbox", checked: initial.icoSizes.includes(s) }), `${s} × ${s}`]);
            label.side = s;
            label.firstChild.addEventListener("change", schedule);
            return label;
        });
        row(tr("Tamaños:"), el("div", { className: "ico-sizes" }, c.icoSizes));
    }

    const status = el("div", { className: "save-status" });
    const preview = new EncodedPreview(ext, (text) => (status.textContent = text));
    dlg.body.appendChild(el("div", { className: "save-columns" }, [el("div", { className: "save-side" }, [form, el("span", { style: { flex: 1 } }), status]),
                                                                   preview.element]));

    const withAdvanced = (o, a) => {
        o.quantization = a.quantization;
        o.gifGlobalPalette = a.globalPalette;
        o.gifStack = a.stack;
        o.gifEffects = clone(a.effects);
        // Como en ezgif, el primero como fondo solo cuenta si no se apilan los fotogramas.
        if (a.stack)
            o.gifEffects.firstAsBackground = o.gifEffects.firstOnlyAsBackground = false;
        return o;
    };
    function options() {
        let o = clone(initial);
        if (c.quality) o.quality = c.quality.value();
        if (c.depth) o.bitDepth = Number(c.depth.value);
        if (c.dither) o.dither = c.dither.value();
        if (c.threshold) o.alphaThreshold = c.threshold.value();
        if (c.quantization) o.quantization = Number(c.quantization.value);
        if (c.interlaced) o.interlaced = c.interlaced.firstChild.checked;
        if (c.animation) o.animation = c.animation.firstChild.checked;
        if (c.delay) o.frameDelay = c.delay.read();
        if (c.loopForever) o.loops = c.loopForever.firstChild.checked ? 0 : c.loops.read() === 0 ? -1 : c.loops.read();
        if (c.advanced) {
            o = withAdvanced(o, advanced);
            o.gifHiddenLayers = showHidden;
        }
        if (c.icoSizes) o.icoSizes = c.icoSizes.filter((l) => l.firstChild.checked).map((l) => l.side);
        return o;
    }
    const framesOf = (a) => {
        const list = a.entries.filter((e) => !e.skip).map((e) => ({ ...frames[e.source], delay: e.delay }));
        // Con todos omitidos se guarda al menos el primero.
        if (!list.length && a.entries.length)
            list.push({ ...frames[a.entries[0].source], delay: a.entries[0].delay });
        return list;
    };
    function animationFrames() {
        return c.animation?.firstChild.checked ? framesOf(advanced) : [];
    }
    const updateHiddenButton = () => {
        if (!c.hidden)
            return;
        const any = frames.some((f) => f.hidden);
        c.hidden.replaceChildren(icon(showHidden ? "eye-closed" : "eye-open", 16),
                                 showHidden ? tr("Ocultar las capas ocultas") : tr("Mostrar capas ocultas"));
        c.hidden.disabled = !any || !c.animation.firstChild.checked || (showHidden && hiddenLocked);
        c.hidden.title = !any ? tr("La imagen no tiene capas ocultas.")
            : showHidden && hiddenLocked ? tr("Editaste los fotogramas en la configuración avanzada con las capas ocultas: se quedan hasta que cierres este diálogo.")
                : tr("Las capas ocultas también son fotogramas del GIF.");
    };
    const setShowHidden = (on) => {
        if (on === showHidden || (!on && hiddenLocked))
            return;
        showHidden = on;
        if (on) {
            // Cada capa oculta entra en su sitio: tras el último fotograma de una capa de debajo.
            frames.forEach((f, i) => {
                if (!f.hidden)
                    return;
                let at = 0;
                advanced.entries.forEach((e, k) => {
                    if (e.source < i)
                        at = k + 1;
                });
                advanced.entries.splice(at, 0, { source: i, delay: f.delay, skip: false });
            });
        } else {
            advanced.entries = advanced.entries.filter((e) => !frames[e.source].hidden);
        }
        updateHiddenButton();
        schedule();
    };
    function updateControls() {
        // El GIF siempre es de 8 bits.
        const eight = (c.depth && Number(c.depth.value) === 8) || !!t.animation;
        c.dither?.setEnabled(eight);
        c.threshold?.setEnabled(eight);
        c.thresholdNote?.classList.toggle("disabled", !eight);
        if (c.quantization)
            c.quantization.disabled = !eight;
        const animated = !!c.animation?.firstChild.checked;
        c.delayRow?.classList.toggle("disabled", !animated);
        if (c.loopForever) {
            c.loopForever.classList.toggle("disabled", !animated);
            c.loopsRow.classList.toggle("disabled", !animated || c.loopForever.firstChild.checked);
        }
        if (c.advanced)
            c.advanced.disabled = !animated;
        updateHiddenButton();
    }
    const loadOptions = (o) => {
        c.quality?.setValue(o.quality);
        if (c.depth) c.depth.value = o.bitDepth;
        c.dither?.setValue(o.dither);
        c.threshold?.setValue(o.alphaThreshold);
        if (c.quantization) c.quantization.value = o.quantization;
        if (c.interlaced) c.interlaced.firstChild.checked = o.interlaced;
        if (c.animation) c.animation.firstChild.checked = o.animation;
        if (c.delay) c.delay.value = o.frameDelay;
        if (c.loopForever) {
            c.loopForever.firstChild.checked = o.loops === 0;
            c.loops.value = Math.max(0, o.loops);
        }
        if (c.advanced) {
            if (!hiddenLocked)
                showHidden = o.gifHiddenLayers;
            advanced = defaultAdvanced();
            Object.assign(advanced, { quantization: o.quantization, globalPalette: o.gifGlobalPalette, stack: o.gifStack, effects: clone(o.gifEffects) });
        }
        c.icoSizes?.forEach((l) => (l.firstChild.checked = o.icoSizes.includes(l.side)));
        updateControls();
        schedule();
    };

    async function showAdvanced() {
        preview.setPlaying(false);
        const r = await gifAdvancedDialog(frames, advanced, { image, ext, dpi, icc, delay: c.delay.read(), options: options(), defaultAdvanced });
        if (!r)
            return;
        // Con las capas ocultas a la vista y algo cambiado en los fotogramas, ya no se pueden
        // quitar de la lista hasta volver a abrir este diálogo.
        if (showHidden && JSON.stringify(r.advanced.entries) !== JSON.stringify(advanced.entries))
            hiddenLocked = true;
        advanced = r.advanced;
        c.delay.value = r.delay;
        c.loopForever.firstChild.checked = r.plays === "";
        if (r.plays !== "")
            c.loops.value = Math.max(0, Number(r.plays) - 1);
        updateControls();
        schedule();
    }

    // Predeterminado deja la ventana como recién abierta con los valores de fábrica: también los
    // fotogramas de la configuración avanzada y las capas ocultas.
    const defaults = el("button", { className: "pushbutton push-left", type: "button", textContent: tr("Predeterminado") });
    defaults.addEventListener("click", () => {
        hiddenLocked = false;
        loadOptions(defaultSaveOptions());
    });
    dlg.buttons.appendChild(defaults);
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    updateControls();
    schedule();
    const accepted = await dlg.exec();
    preview.destroy();
    return accepted ? { options: options(), frames: animationFrames() } : null;
}

// --- Configuración avanzada del GIF (como ezgif) ------------------------------------------

async function gifAdvancedDialog(frames, initial, { image, ext, dpi, icc, delay, options, defaultAdvanced }) {
    const dlg = new Dialog(tr("Configuración avanzada del GIF"), { iconName: "configure" });
    dlg.element.classList.add("gif-advanced");
    let adv = clone(initial), allDelay = delay;
    let plays = options.loops === 0 ? "" : String(options.loops < 0 ? 1 : options.loops + 1);
    // Miniaturas de los fotogramas originales.
    const thumbs = frames.map((f) => {
        const s = Math.min(96 / f.width, 72 / f.height), w = Math.max(1, Math.round(f.width * s)), h = Math.max(1, Math.round(f.height * s));
        const src = createCanvas(f.width, f.height);
        src.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(f.data), f.width, f.height), 0, 0);
        const c = createCanvas(w, h), p = c.getContext("2d");
        p.fillStyle = checkerPattern(p);
        p.fillRect(0, 0, w, h);
        p.imageSmoothingQuality = "high";
        p.drawImage(src, 0, 0, w, h);
        return c;
    });
    const framesRow = el("div", { className: "gif-frames" });
    const group = (title, children) => el("fieldset", { className: "group" }, [el("legend", { textContent: title }), ...children]);
    const hint = (text) => el("span", { className: "note", textContent: text });
    const button = (text, kind, onClick) => {
        const b = el("button", { className: `gif-button ${kind}`, type: "button", textContent: text });
        b.addEventListener("click", onClick);
        return b;
    };

    // Rangos
    const from = spin(1, 1, 1), to = spin(1, 1, 5), every = spin(2, 2, 2);
    const mark = (skip, pick) => {
        adv.entries.forEach((e, i) => {
            if (pick(i))
                e.skip = skip;
        });
        rebuild();
    };
    const inRange = (i) => i + 1 >= Math.min(from.read(), to.read()) && i + 1 <= Math.max(from.read(), to.read());
    const isNth = (i) => (i + 1) % every.read() === 0;
    const rangeBox = group(tr("Activar u omitir varios fotogramas"), [el("div", { className: "gif-ranges" }, [
        el("span", { textContent: tr("Desde:") }), from, el("span", { textContent: tr("Hasta:") }), to,
        button(tr("Omitir rango"), "skip", () => mark(true, inRange)), button(tr("Activar rango"), "enable", () => mark(false, inRange)),
        el("span", { textContent: tr("Cada:") }), every, el("span"), el("span"),
        button(tr("Omitir cada N.º"), "skip", () => mark(true, isNth)), button(tr("Activar cada N.º"), "enable", () => mark(false, isNth)),
    ])]);

    // Opciones del GIF
    const delayAll = spin(MIN_DELAY, 60000, allDelay, 10), fps = el("span");
    const loopsEdit = el("input", { type: "text", style: { width: "90px" }, inputMode: "numeric" });
    const globalPalette = el("input", { type: "checkbox" });
    const quantization = el("select", {}, [el("option", { value: 1, textContent: tr("Octree") }), el("option", { value: 0, textContent: tr("Corte a la mediana") })]);
    quantization.title = tr("Cómo se eligen los 256 colores de la paleta. Octree es el de Paint.NET; el corte a la mediana reparte mejor los degradados.");
    const gifBox = group(tr("Opciones del GIF"), [el("div", { className: "gif-options" }, [
        el("span", { textContent: tr("Duración:") }), el("span", { className: "with-unit" }, [delayAll, el("span", { textContent: tr("ms") }), fps,
                                                                                         hint(tr("(cambiarla pone esta duración a todos los fotogramas)"))]),
        el("span", { textContent: tr("Reproducciones:") }), el("span", { className: "with-unit" }, [loopsEdit, hint(tr("(vacío: se repite sin fin)"))]),
        el("label", { className: "wide" }, [globalPalette, tr("Usar una paleta global"), " ", hint(tr("(los mismos colores en todos los fotogramas: el archivo ocupa menos)"))]),
        el("span", { textContent: tr("Cuantización:") }), el("span", {}, [quantization]),
    ])]);

    // Efectos (las opciones de cada uno solo se ven con él marcado, como en ezgif)
    const check = (text) => el("label", {}, [el("input", { type: "checkbox" }), text]);
    const crossfade = check(tr("Fundido entre fotogramas"));
    const fadeDelay = spin(MIN_DELAY, 2000, 60, 10), fadeCount = spin(2, 20, 10), fadeLast = check(tr("No fundir el último fotograma con el primero"));
    const fadeFrame = el("div", { className: "gif-indent" }, [
        el("div", { className: "gif-options" }, [el("span", { textContent: tr("Duración del fundido:") }), withUnit(fadeDelay, tr("ms")),
                                                 el("span", { textContent: tr("Fotogramas del fundido:") }), withUnit(fadeCount, "", "(2-20)")]),
        fadeLast,
    ]);
    const noStack = check(tr("No apilar los fotogramas"));
    const firstBackground = check(tr("Usar el primer fotograma como fondo"));
    const firstOnly = check(tr("Duración 0 para el primer fotograma (no mostrarlo solo)"));
    const zeroFrame = el("div", { className: "gif-indent" }, [firstOnly]);
    const bgFrame = el("div", { className: "gif-indent" }, [firstBackground, zeroFrame]);
    const effectsBox = group(tr("Efectos"), [crossfade, fadeFrame,
        el("div", {}, [noStack, " ", hint(tr("(cada fotograma se borra al mostrar el siguiente; para imágenes con fondo transparente)"))]), bgFrame]);

    const status = el("div", { className: "save-status" });
    const preview = new EncodedPreview(ext, (text) => (status.textContent = text));
    const refresh = () => {
        const o = clone(options);
        o.quantization = adv.quantization;
        o.gifGlobalPalette = adv.globalPalette;
        o.gifStack = adv.stack;
        o.gifEffects = clone(adv.effects);
        if (adv.stack)
            o.gifEffects.firstAsBackground = o.gifEffects.firstOnlyAsBackground = false;
        o.frameDelay = allDelay;
        const count = parseInt(plays, 10);
        o.loops = plays === "" ? 0 : !(count > 1) ? -1 : count - 1;
        const list = adv.entries.filter((e) => !e.skip).map((e) => ({ ...frames[e.source], delay: e.delay }));
        if (!list.length && adv.entries.length)
            list.push({ ...frames[adv.entries[0].source], delay: adv.entries[0].delay });
        preview.request({ image, frames: list, ext, options: o, dpi, icc }, o.loops);
    };

    // Tarjeta de cada fotograma (se rehacen al omitir, copiar o cambiar la duración de todos).
    function rebuild() {
        const scroll = framesRow.scrollLeft;
        framesRow.replaceChildren();
        adv.entries.forEach((e, i) => {
            const fromHidden = frames[e.source]?.hidden;
            const eye = el("button", { className: "tbutton", type: "button", title: e.skip ? tr("No se muestra: clic para mostrarlo") : tr("Se muestra: clic para ocultarlo") },
                           [icon(e.skip ? "eye-closed" : "eye-open", 16)]);
            const toggle = () => {
                e.skip = !e.skip;
                rebuild();
            };
            eye.addEventListener("click", toggle);
            const thumb = el("div", { className: "gif-thumb", title: e.skip ? tr("Omitido: no va en el GIF") : "" });
            const source = thumbs[e.source], copy = createCanvas(source.width, source.height), p = copy.getContext("2d");
            p.drawImage(source, 0, 0);
            if (e.skip) {
                p.fillStyle = "rgba(0,0,0,0.43)";
                p.fillRect(0, 0, copy.width, copy.height);
            }
            thumb.appendChild(copy);
            const d = spin(MIN_DELAY, 60000, e.delay > 0 ? e.delay : allDelay, 10);
            d.disabled = e.skip;
            d.addEventListener("input", () => {
                e.delay = d.read();
                refresh();
            });
            const twin = button(tr("Copiar"), "enable", () => {
                adv.entries.splice(i + 1, 0, { ...e, skip: false });
                rebuild();
            });
            twin.title = tr("Repite este fotograma justo después");
            framesRow.appendChild(el("div", { className: `gif-card${e.skip ? " skipped" : ""}` }, [
                el("div", { className: "row" }, [el("span", { textContent: fromHidden ? tr("%1 · capa oculta", i + 1) : String(i + 1) }), el("span", { style: { flex: 1 } }), eye]),
                thumb,
                el("div", { className: "row" }, [el("span", { textContent: tr("Duración:") }), d, el("span", { textContent: tr("ms") })]),
                el("div", { className: "row" }, [button(e.skip ? tr("Activar") : tr("Omitir"), e.skip ? "enable" : "skip", toggle), twin]),
            ]));
        });
        const n = Math.max(1, adv.entries.length);
        for (const s of [from, to])
            s.max = n;
        every.max = Math.max(2, n);
        framesRow.scrollLeft = scroll;
        refresh();
    }
    const updateEffects = () => {
        fadeFrame.style.display = crossfade.firstChild.checked ? "" : "none";
        bgFrame.style.display = noStack.firstChild.checked ? "" : "none";
        zeroFrame.style.display = noStack.firstChild.checked && firstBackground.firstChild.checked ? "" : "none";
    };
    const updateFps = () => (fps.textContent = tr("(%1 FPS)", (1000 / allDelay).toLocaleString(currentLanguage(), { minimumFractionDigits: 2, maximumFractionDigits: 2 })));
    // Pone en los controles lo de 'adv' (al abrir y con Predeterminado).
    const load = () => {
        delayAll.value = allDelay;
        loopsEdit.value = plays;
        globalPalette.checked = adv.globalPalette;
        quantization.value = adv.quantization;
        crossfade.firstChild.checked = adv.effects.crossfade;
        fadeDelay.value = adv.effects.fadeDelay;
        fadeCount.value = clamp(adv.effects.fadeFrames, 2, 20);
        fadeLast.firstChild.checked = !adv.effects.fadeLastToFirst;
        noStack.firstChild.checked = !adv.stack;
        firstBackground.firstChild.checked = adv.effects.firstAsBackground;
        firstOnly.firstChild.checked = adv.effects.firstOnlyAsBackground;
        updateEffects();
        updateFps();
        rebuild();
    };
    delayAll.addEventListener("input", () => {
        allDelay = delayAll.read();
        for (const e of adv.entries)
            e.delay = allDelay;
        updateFps();
        rebuild();
    });
    loopsEdit.addEventListener("input", () => {
        loopsEdit.value = loopsEdit.value.replace(/[^0-9]/g, "");
        plays = loopsEdit.value;
        refresh();
    });
    globalPalette.addEventListener("change", () => {
        adv.globalPalette = globalPalette.checked;
        refresh();
    });
    quantization.addEventListener("change", () => {
        adv.quantization = Number(quantization.value);
        refresh();
    });
    const on = (control, f) => control.addEventListener(control.tagName === "LABEL" ? "change" : "input", () => {
        f();
        updateEffects();
        refresh();
    });
    on(crossfade, () => (adv.effects.crossfade = crossfade.firstChild.checked));
    on(fadeDelay, () => (adv.effects.fadeDelay = fadeDelay.read()));
    on(fadeCount, () => (adv.effects.fadeFrames = fadeCount.read()));
    on(fadeLast, () => (adv.effects.fadeLastToFirst = !fadeLast.firstChild.checked));
    on(noStack, () => (adv.stack = !noStack.firstChild.checked));
    on(firstBackground, () => (adv.effects.firstAsBackground = firstBackground.firstChild.checked));
    on(firstOnly, () => (adv.effects.firstOnlyAsBackground = firstOnly.firstChild.checked));

    dlg.body.appendChild(el("div", { className: "gif-columns" }, [
        el("div", { className: "gif-page" }, [framesRow, rangeBox, gifBox, effectsBox]),
        el("div", { className: "gif-side" }, [preview.element, status]),
    ]));
    const defaults = el("button", { className: "pushbutton push-left", type: "button", textContent: tr("Predeterminado") });
    defaults.addEventListener("click", () => {
        adv = defaultAdvanced();
        allDelay = defaultSaveOptions().frameDelay;
        plays = "";
        load();
    });
    dlg.buttons.appendChild(defaults);
    dlg.addButton(tr("Aceptar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    load();
    const accepted = await dlg.exec();
    preview.destroy();
    return accepted ? { advanced: adv, delay: allDelay, plays } : null;
}
