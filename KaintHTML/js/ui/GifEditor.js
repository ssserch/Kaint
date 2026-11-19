// Archivo > Editor de GIF (src/app/GifEditor.cpp de Kaint): la imagen como una animación, con una
// interfaz de editor de vídeo sencillo: la biblioteca de medios (las imágenes abiertas y las que se
// importan o se arrastran), la vista previa con su reproductor y la línea de tiempo con un clip por
// fotograma. Es otra vista de la misma imagen: cada fotograma es una capa (la de abajo, el primero)
// con su duración en el nombre («Fotograma 1 (100ms)»), como al abrir y guardar un GIF; cada
// cambio es un paso del historial. Cada imagen recuerda en qué vista está.
import { Layer } from "../core/Document.js";
import { delayFromName, nameWithDelay } from "../core/Codecs.js";
import { openFile, OpenAccept } from "../core/ImageIO.js";
import { context, createCanvas, thumbnail } from "../core/Raster.js";
import { tr, trn } from "../i18n.js";
import { Action } from "./Actions.js";
import { el, message } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { popupMenu } from "./Menu.js";

const DefaultDelay = 100, MinDelay = 20, MediaMime = "application/x-kaint-gif-media";
const RulerHeight = 22, LeftMargin = 12;

const delayOf = (layer) => Math.max(MinDelay, delayFromName(layer?.name, DefaultDelay));

function timeText(ms) {
    const m = Math.floor(ms / 60000), s = Math.floor(ms / 1000) % 60, c = Math.floor((ms % 1000) / 10);
    return `${String(m).padStart(2, "0")}:${String(s).padStart(2, "0")}.${String(c).padStart(2, "0")}`;
}

// Un fotograma de otro tamaño, ajustado al lienzo (sin deformarlo, centrado y con transparente
// alrededor).
function fitted(source, width, height) {
    const canvas = createCanvas(width, height), ctx = context(canvas);
    if (source.width === width && source.height === height) {
        ctx.drawImage(source, 0, 0);
        return canvas;
    }
    const scale = Math.min(width / source.width, height / source.height);
    const w = Math.max(1, Math.round(source.width * scale)), h = Math.max(1, Math.round(source.height * scale));
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(source, Math.floor((width - w) / 2), Math.floor((height - h) / 2), w, h);
    return canvas;
}

// Los fotogramas de una imagen: sus capas, si es una animación (alguna lleva la duración en el
// nombre); si no, la imagen acoplada.
function framesOf(doc) {
    const animation = doc.layers.length > 1 && doc.layers.some((l) => /\(\d+\s*ms\)/i.test(l.name));
    if (animation)
        return doc.layers.filter((l) => l.visible).map((l) => ({ image: l.canvas, delay: delayOf(l) }));
    return [{ image: doc.flattened(), delay: DefaultDelay }];
}

function button(content, title, onclick, className = "") {
    const b = el("button", { className: `tbutton gif-button ${className}`.trim(), title, type: "button", onclick });
    if (content instanceof Node)
        b.append(content);
    else
        b.textContent = content;
    return b;
}

export class GifEditor {
    constructor(app) {
        this.app = app;
        this.doc = null;
        this.openDocs = [];
        this.imported = [];
        this.media = [];
        this.pps = 600;
        this.time = 0;
        this.playing = false;
        this.listeners = [];
        this.thumbs = new WeakMap();
        this.build();
    }

    build() {
        const root = this.root = el("section", { id: "gif-editor", className: "gif-editor", hidden: true });
        // Cabecera
        this.title = el("span", { className: "gif-title" });
        const back = button(tr("Editor de imágenes"), tr("Vuelve a editar esta imagen con las herramientas (cada fotograma es una capa)."),
                            () => this.app.toggleGifEditor(), "pushbutton");
        const exportButton = button(tr("Exportar GIF..."), tr("Guarda la animación como GIF, con la configuración de siempre (paleta, duración, repeticiones, efectos...)."),
                                    () => this.app.save(this.app.doc, true, "gif"), "pushbutton");
        exportButton.prepend(icon("document-save-as", 16));
        const head = el("div", { className: "gif-head" }, [this.title, back, exportButton]);

        // Biblioteca
        this.mediaList = el("div", { className: "gif-media-list", title: tr("Las imágenes abiertas en Kaint y las que importes. Arrástralas a la línea de tiempo (o haz doble clic para añadirlas al final).") });
        this.fileInput = el("input", { type: "file", multiple: true, accept: OpenAccept || "image/*", hidden: true,
                                      onchange: () => { this.importFiles([...this.fileInput.files]); this.fileInput.value = ""; } });
        const importButton = button(tr("Importar..."), tr("Importar imágenes o GIF"), () => this.fileInput.click());
        importButton.prepend(icon("layer-import", 16));
        const addButton = button(tr("Añadir"), tr("Añade los medios elegidos detrás del fotograma elegido."), () => {
            let at = this.selected() >= 0 ? this.selected() + 1 : -1;
            for (const item of this.mediaList.querySelectorAll(".gif-media.selected")) {
                const before = this.count();
                this.insertMedia(+item.dataset.index, at);
                if (at >= 0)
                    at += this.count() - before;
            }
        });
        const bin = el("div", { className: "gif-bin" }, [el("div", { className: "gif-section-title", textContent: tr("Medios") }), this.mediaList,
                                                        el("div", { className: "gif-row" }, [importButton, addButton, this.fileInput])]);
        this.mediaList.addEventListener("dragover", (e) => {
            if (e.dataTransfer.types.includes("Files")) {
                e.preventDefault();
                e.dataTransfer.dropEffect = "copy";
            }
        });
        this.mediaList.addEventListener("drop", (e) => {
            if (!e.dataTransfer.files.length)
                return;
            e.preventDefault();
            this.importFiles([...e.dataTransfer.files]);
        });

        // Vista previa y reproductor
        this.preview = el("canvas", { className: "gif-preview" });
        this.playButton = button(icon("media-playback-start", 16), tr("Reproducir o pausar (Espacio)"), () => this.togglePlay());
        this.timeLabel = el("span", { className: "gif-time" });
        this.loop = el("input", { type: "checkbox", checked: true });
        this.speed = el("select", { title: tr("Velocidad de la vista previa (no cambia el GIF)") },
                        [0.25, 0.5, 1, 1.5, 2, 4].map((f) => el("option", { value: f, textContent: `${f}×`, selected: f === 1 })));
        const transport = el("div", { className: "gif-row gif-transport" }, [
            button("⏮", tr("Primer fotograma (Inicio)"), () => this.seek(0)),
            button("⏪", tr("Fotograma anterior (←)"), () => this.step(-1)),
            this.playButton,
            button("⏹", tr("Detener"), () => { this.stop(); this.seek(0); }),
            button("⏩", tr("Fotograma siguiente (→)"), () => this.step(1)),
            button("⏭", tr("Último fotograma (Fin)"), () => { const s = this.starts(); if (s.length > 1) this.seek(s[s.length - 2]); }),
            this.timeLabel, el("span", { className: "gif-stretch" }),
            el("label", { className: "gif-check" }, [this.loop, tr("Repetir")]), this.speed,
        ]);
        const player = el("div", { className: "gif-player" }, [el("div", { className: "gif-preview-box" }, [this.preview]), transport]);

        // Barra y línea de tiempo
        this.delayInput = el("input", { type: "number", min: MinDelay, max: 60000, step: 10, className: "gif-delay", title: tr("Cuánto se ve el fotograma elegido") });
        this.delayInput.addEventListener("change", () => {
            const i = this.selected();
            if (i >= 0)
                this.setDelay(i, +this.delayInput.value);
        });
        this.frameControls = [];
        const frameButton = (content, title, fn) => {
            const b = button(content, title, fn);
            this.frameControls.push(b);
            return b;
        };
        const undo = button(icon("edit-undo", 16), tr("Deshacer"), () => this.doc?.undo());
        const redo = button(icon("edit-redo", 16), tr("Rehacer"), () => this.doc?.redo());
        const dup = frameButton(tr("Duplicar"), tr("Duplica el fotograma elegido"), () => this.duplicate(this.selected()));
        dup.prepend(icon("layer-duplicate", 16));
        const del = frameButton(tr("Eliminar"), tr("Quita el fotograma elegido (Supr)"), () => this.remove(this.selected()));
        del.prepend(icon("edit-delete", 16));
        const hide = frameButton(icon("eye-open", 16), tr("Ocultar o mostrar el fotograma (los ocultos no van en el GIF)"), () => this.toggleHidden(this.selected()));
        const edit = frameButton("✎", tr("Editar el fotograma con las herramientas"), () => this.editFrame(this.selected()));
        const all = frameButton(tr("A todos"), tr("La misma duración para todos los fotogramas"), () => this.setAllDelays(+this.delayInput.value));
        this.frameControls.push(this.delayInput);
        this.zoom = el("input", { type: "range", min: 40, max: 3000, value: this.pps, className: "gif-zoom",
                                  title: tr("Zum de la línea de tiempo (también con Ctrl y la rueda)"),
                                  oninput: () => { this.pps = +this.zoom.value; this.renderTimeline(); } });
        const bar = el("div", { className: "gif-row gif-toolbar" }, [
            undo, redo, el("span", { className: "gif-gap" }), dup, del, hide, edit, el("span", { className: "gif-gap" }),
            el("label", { textContent: tr("Duración:") }), this.delayInput, el("span", { textContent: tr(" ms") }), all,
            el("span", { className: "gif-gap" }),
            button(tr("Invertir"), tr("El último fotograma pasa a ser el primero"), () => this.reverse()),
            button(tr("Ida y vuelta"), tr("Añade los fotogramas al revés al final: la animación va y vuelve"), () => this.boomerang()),
            el("span", { className: "gif-stretch" }), el("label", { textContent: tr("Zum:") }), this.zoom,
        ]);
        this.ruler = el("canvas", { className: "gif-ruler" });
        this.track = el("div", { className: "gif-track" });
        this.playhead = el("div", { className: "gif-playhead" });
        this.dropMark = el("div", { className: "gif-drop-mark", hidden: true });
        this.empty = el("div", { className: "gif-empty", textContent: tr("Arrastra aquí imágenes o GIF (de la biblioteca o de tus carpetas)") });
        this.lane = el("div", { className: "gif-lane" }, [this.ruler, this.track, this.dropMark, this.playhead, this.empty]);
        this.scroller = el("div", { className: "gif-scroller" }, [this.lane]);
        const timeline = el("div", { className: "gif-timeline" }, [bar, this.scroller]);

        root.append(head, el("div", { className: "gif-top" }, [bin, player]), timeline);
        this.installTimeline();
        new ResizeObserver(() => { this.drawPreview(); this.renderTimeline(); }).observe(root);
    }

    // --- Documento ----------------------------------------------------------------------

    setDocument(doc) {
        if (doc === this.doc)
            return;
        this.stop();
        for (const [type, fn] of this.listeners)
            this.doc?.removeEventListener(type, fn);
        this.listeners = [];
        this.doc = doc;
        this.time = 0;
        if (doc) {
            const refresh = () => this.update();
            for (const type of ["layers", "size"]) {
                doc.addEventListener(type, refresh);
                this.listeners.push([type, refresh]);
            }
            // Solo se rehace la miniatura de la capa que cambió.
            const pixels = (e) => {
                if (e.detail?.layer)
                    this.thumbs.delete(e.detail.layer);
                else
                    this.thumbs = new WeakMap();
                this.update();
            };
            doc.addEventListener("pixels", pixels);
            this.listeners.push(["pixels", pixels]);
            const active = () => {
                if (!this.playing) {
                    const s = this.starts(), i = this.selected();
                    if (i >= 0 && this.frameAt(this.time) !== i)
                        this.time = s[i];
                }
                this.update(!this.playing);
            };
            doc.addEventListener("active", active);
            this.listeners.push(["active", active]);
        }
        this.rebuildMedia();
        this.update();
    }

    setOpenDocuments(docs) {
        this.openDocs = [...docs];
        if (!this.root.hidden)
            this.rebuildMedia();
    }

    show(on) {
        this.root.hidden = !on;
        if (on) {
            this.rebuildMedia();
            this.update();
        } else {
            this.stop();
        }
    }

    count() {
        return this.doc ? this.doc.layers.length : 0;
    }

    selected() {
        return this.doc && this.doc.layers.length ? this.doc.active : -1;
    }

    delay(i) {
        return delayOf(this.doc?.layers[i]);
    }

    hidden(i) {
        return !!this.doc && !this.doc.layers[i]?.visible;
    }

    // Tiempo (ms) donde empieza cada fotograma; el último valor es la duración total.
    starts() {
        const s = [0];
        for (const layer of this.doc?.layers || [])
            s.push(s[s.length - 1] + delayOf(layer));
        return s;
    }

    frameAt(ms) {
        const s = this.starts();
        for (let i = 0; i + 1 < s.length; i++)
            if (ms < s[i + 1])
                return i;
        return s.length - 2;
    }

    select(i) {
        if (!this.doc || i < 0 || i >= this.doc.layers.length)
            return;
        this.doc.setActiveIndex(i);
        if (!this.playing)
            this.time = this.starts()[i];
        this.update();
    }

    seek(ms) {
        const s = this.starts();
        this.time = s.length > 1 ? Math.max(0, Math.min(ms, s[s.length - 1] - 1)) : 0;
        const i = this.frameAt(this.time);
        if (this.doc && i >= 0 && i !== this.doc.active)
            this.doc.setActiveIndex(i);
        this.update();
    }

    step(delta) {
        this.stop();
        const s = this.starts(), n = s.length - 1;
        if (n <= 0)
            return;
        let i = this.frameAt(this.time) + delta;
        if (i < 0)
            i = this.loop.checked ? n - 1 : 0;
        if (i >= n)
            i = this.loop.checked ? 0 : n - 1;
        this.seek(s[i]);
    }

    togglePlay() {
        if (this.playing) {
            this.stop();
            return;
        }
        if (!this.count())
            return;
        const s = this.starts();
        if (this.time >= s[s.length - 1] - 1)
            this.time = 0;
        this.playing = true;
        this.playButton.replaceChildren(icon("media-playback-pause", 16));
        let last = performance.now();
        const frame = (now) => {
            if (!this.playing)
                return;
            const s = this.starts(), n = s.length - 1;
            if (!this.doc || n <= 0) {
                this.stop();
                return;
            }
            this.time += (now - last) * +this.speed.value;
            last = now;
            if (this.time >= s[n]) {
                if (!this.loop.checked) {
                    this.time = s[n - 1];
                    this.stop();
                } else {
                    this.time %= Math.max(1, s[n]);
                }
            }
            // Los ocultos no se ven (tampoco van en el GIF): se saltan.
            let i = this.frameAt(this.time);
            for (let tries = 0; tries < n && this.hidden(i); tries++) {
                i = (i + 1) % n;
                this.time = s[i];
            }
            if (i !== this.doc.active)
                this.doc.setActiveIndex(i);
            else
                this.update(false);
            if (this.playing)
                this.raf = requestAnimationFrame(frame);
        };
        this.raf = requestAnimationFrame(frame);
    }

    stop() {
        this.playing = false;
        cancelAnimationFrame(this.raf);
        this.playButton?.replaceChildren(icon("media-playback-start", 16));
    }

    // --- Cambios (un paso del historial cada uno) ------------------------------------------

    apply(layers, props, active, name, iconName) {
        const doc = this.doc;
        if (!doc)
            return;
        this.stop();
        const before = doc.layerState();
        doc.layers = layers;
        layers.forEach((l, i) => l.setProps(props[i]));
        doc.active = Math.max(0, Math.min(active, layers.length - 1));
        doc.pushLayers(before, name, iconName);
        doc.layersChanged();
        doc.emit("active");
        this.update();
    }

    current() {
        const layers = [...this.doc.layers];
        return { layers, props: layers.map((l) => l.props()) };
    }

    insertFrames(at, frames, name) {
        const doc = this.doc;
        if (!doc || !frames.length)
            return;
        const { layers, props } = this.current();
        if (at < 0 || at > layers.length)
            at = layers.length;
        frames.forEach((f, i) => {
            const layer = new Layer(doc.width, doc.height, nameWithDelay(tr("Fotograma %1", at + i + 1), f.delay || DefaultDelay));
            layer.ctx.drawImage(fitted(f.image, doc.width, doc.height), 0, 0);
            layers.splice(at + i, 0, layer);
            props.splice(at + i, 0, layer.props());
        });
        this.apply(layers, props, at, name, "layer-import");
    }

    insertMedia(index, at) {
        const m = this.media[index];
        if (!m)
            return;
        this.insertFrames(at, m.doc ? framesOf(m.doc) : m.frames, tr("Añadir «%1»", m.name));
    }

    async insertFiles(files, at) {
        const loaded = await this.load(files);
        const frames = loaded.flatMap((m) => m.frames);
        this.imported.push(...loaded);
        this.rebuildMedia();
        if (frames.length)
            this.insertFrames(at, frames, loaded.length === 1 ? tr("Añadir «%1»", loaded[0].name) : tr("Añadir imágenes"));
    }

    move(from, to) {
        const n = this.count();
        if (from < 0 || from >= n || to < 0 || to >= n || from === to)
            return;
        const { layers, props } = this.current();
        layers.splice(to, 0, ...layers.splice(from, 1));
        props.splice(to, 0, ...props.splice(from, 1));
        this.apply(layers, props, to, tr("Mover fotograma"), "layer-raise");
    }

    setDelay(i, ms) {
        if (i < 0 || i >= this.count() || !ms || ms === this.delay(i))
            return;
        const { layers, props } = this.current();
        props[i] = { ...props[i], name: nameWithDelay(props[i].name, Math.max(MinDelay, Math.round(ms))) };
        this.apply(layers, props, i, tr("Duración del fotograma"), "layer-properties");
    }

    setAllDelays(ms) {
        if (!this.count() || !ms)
            return;
        const { layers, props } = this.current();
        const all = props.map((p) => ({ ...p, name: nameWithDelay(p.name, Math.max(MinDelay, Math.round(ms))) }));
        this.apply(layers, all, this.selected(), tr("Duración de todos los fotogramas"), "layer-properties");
    }

    remove(i) {
        if (i < 0 || i >= this.count())
            return;
        // Una imagen necesita al menos una capa.
        if (this.count() === 1)
            return;
        const { layers, props } = this.current();
        layers.splice(i, 1);
        props.splice(i, 1);
        this.apply(layers, props, Math.min(i, layers.length - 1), tr("Eliminar fotograma"), "layer-delete");
    }

    duplicate(i) {
        if (i < 0 || i >= this.count())
            return;
        const { layers, props } = this.current();
        const copy = layers[i].duplicate(layers[i].name);
        layers.splice(i + 1, 0, copy);
        props.splice(i + 1, 0, { ...props[i] });
        this.apply(layers, props, i + 1, tr("Duplicar fotograma"), "layer-duplicate");
    }

    toggleHidden(i) {
        if (i < 0 || i >= this.count())
            return;
        const { layers, props } = this.current();
        props[i] = { ...props[i], visible: !props[i].visible };
        this.apply(layers, props, i, props[i].visible ? tr("Mostrar fotograma") : tr("Ocultar fotograma"), "layer-visible-on");
    }

    reverse() {
        if (this.count() < 2)
            return;
        const { layers, props } = this.current();
        layers.reverse();
        props.reverse();
        this.apply(layers, props, this.count() - 1 - this.selected(), tr("Invertir el orden"), "layer-flip-vertical");
    }

    // Como en la configuración avanzada del GIF: al revés al final, sin repetir el último ni el primero.
    boomerang() {
        if (this.count() < 3)
            return;
        const { layers, props } = this.current();
        for (let i = this.count() - 2; i >= 1; i--) {
            layers.push(this.doc.layers[i].duplicate(this.doc.layers[i].name));
            props.push({ ...props[i] });
        }
        this.apply(layers, props, this.selected(), tr("Ida y vuelta"), "layer-duplicate");
    }

    editFrame(i) {
        if (i < 0)
            return;
        this.doc.setActiveIndex(i);
        this.app.toggleGifEditor(false);
    }

    async editDelay(i) {
        const value = prompt(tr("Duración del fotograma %1 (ms):", i + 1), String(this.delay(i)));
        if (value !== null && +value > 0)
            this.setDelay(i, +value);
    }

    // --- Biblioteca -----------------------------------------------------------------------

    async load(files) {
        const out = [], failed = [];
        for (const file of files) {
            try {
                const doc = await openFile(file);
                out.push({ name: file.name, frames: framesOf(doc) });
            } catch (e) {
                failed.push(`${file.name}: ${e?.message || tr("no es una imagen")}`);
            }
        }
        if (failed.length)
            message(tr("Importar"), tr("No se pudieron importar:\n%1", failed.join("\n")));
        return out;
    }

    async importFiles(files) {
        this.imported.push(...(await this.load(files)));
        this.rebuildMedia();
    }

    rebuildMedia() {
        this.media = [...this.openDocs.filter((d) => d !== this.doc).map((d) => ({ name: d.displayName(), doc: d })), ...this.imported];
        const items = this.media.map((m, i) => {
            const source = m.doc ? m.doc.flattened() : m.frames[0].image;
            const label = m.doc ? tr("%1 (abierta)", m.name) : m.frames.length > 1 ? trn("%1 (%n fotogramas)", m.frames.length, m.name) : m.name;
            const item = el("div", { className: "gif-media", draggable: true, title: label, dataset: { index: i } },
                            [thumbnail(source, 84, 64), el("span", { textContent: label })]);
            item.addEventListener("click", (e) => {
                if (!e.ctrlKey && !e.metaKey && !e.shiftKey)
                    for (const other of this.mediaList.querySelectorAll(".gif-media.selected"))
                        other.classList.remove("selected");
                item.classList.toggle("selected");
            });
            item.addEventListener("dblclick", () => this.insertMedia(i, -1));
            item.addEventListener("dragstart", (e) => {
                if (!item.classList.contains("selected")) {
                    for (const other of this.mediaList.querySelectorAll(".gif-media.selected"))
                        other.classList.remove("selected");
                    item.classList.add("selected");
                }
                const chosen = [...this.mediaList.querySelectorAll(".gif-media.selected")].map((x) => x.dataset.index);
                e.dataTransfer.setData(MediaMime, chosen.join(" "));
                e.dataTransfer.effectAllowed = "copy";
            });
            return item;
        });
        this.mediaList.replaceChildren(...items);
    }

    // --- Línea de tiempo --------------------------------------------------------------------

    x(ms) {
        return LeftMargin + Math.round((ms * this.pps) / 1000);
    }

    msAt(x) {
        return Math.max(0, Math.round(((x - LeftMargin) * 1000) / this.pps));
    }

    insertionAt(x) {
        const s = this.starts(), n = s.length - 1;
        for (let i = 0; i < n; i++)
            if (x < (this.x(s[i]) + this.x(s[i + 1])) / 2)
                return i;
        return Math.max(0, n);
    }

    laneX(e) {
        return e.clientX - this.lane.getBoundingClientRect().left;
    }

    installTimeline() {
        const lane = this.lane;
        let drag = null;
        lane.addEventListener("pointerdown", (e) => {
            if (!this.doc || e.button > 2)
                return;
            const x = this.laneX(e), clip = e.target.closest(".gif-clip");
            if (!clip || e.target === this.ruler) {
                if (e.button === 0) {
                    drag = { kind: "scrub" };
                    lane.setPointerCapture(e.pointerId);
                    this.seek(this.msAt(x));
                }
                return;
            }
            const i = +clip.dataset.index;
            this.select(i);
            if (e.button === 2)
                return;
            lane.setPointerCapture(e.pointerId);
            if (e.target.classList.contains("gif-clip-edge"))
                drag = { kind: "resize", index: i, start: this.starts()[i], delay: this.delay(i) };
            else
                drag = { kind: "press", index: i, x };
        });
        lane.addEventListener("pointermove", (e) => {
            if (!drag)
                return;
            const x = this.laneX(e);
            if (drag.kind === "scrub") {
                this.seek(this.msAt(x));
            } else if (drag.kind === "resize") {
                // De 10 en 10 ms, como la duración de los GIF (centésimas de segundo).
                drag.delay = Math.max(MinDelay, Math.round((this.msAt(x) - drag.start) / 10) * 10);
                this.renderTimeline(drag);
            } else if (drag.kind === "press" && Math.abs(x - drag.x) > 4) {
                drag.kind = "reorder";
            }
            if (drag.kind === "reorder") {
                drag.to = this.insertionAt(x);
                this.showDropMark(drag.to);
            }
        });
        const end = () => {
            if (!drag)
                return;
            const d = drag;
            drag = null;
            this.dropMark.hidden = true;
            if (d.kind === "resize")
                this.setDelay(d.index, d.delay);
            else if (d.kind === "reorder") {
                let to = d.to;
                if (to > d.index)
                    to--;
                this.move(d.index, to);
            }
            this.renderTimeline();
        };
        lane.addEventListener("pointerup", end);
        lane.addEventListener("pointercancel", end);
        lane.addEventListener("dblclick", (e) => {
            const clip = e.target.closest(".gif-clip");
            if (clip)
                this.editDelay(+clip.dataset.index);
        });
        lane.addEventListener("contextmenu", (e) => {
            const clip = e.target.closest(".gif-clip");
            if (!clip)
                return;
            e.preventDefault();
            const i = +clip.dataset.index;
            const item = (id, text, iconName, run, enabled = null) => new Action({ id, text, icon: iconName, run, enabled });
            popupMenu([
                item("gif-delay", tr("Duración..."), null, () => this.editDelay(i)),
                item("gif-dup", tr("Duplicar"), "layer-duplicate", () => this.duplicate(i)),
                item("gif-hide", this.hidden(i) ? tr("Mostrar") : tr("Ocultar"), "layer-visible-on", () => this.toggleHidden(i)),
                item("gif-edit", tr("Editar con las herramientas"), null, () => this.editFrame(i)),
                "-",
                item("gif-del", tr("Eliminar"), "edit-delete", () => this.remove(i), () => this.count() > 1),
            ], e.clientX, e.clientY);
        });
        lane.addEventListener("wheel", (e) => {
            if (!e.ctrlKey)
                return;
            e.preventDefault();
            this.pps = Math.max(40, Math.min(3000, Math.round(this.pps * (e.deltaY < 0 ? 1.25 : 0.8))));
            this.zoom.value = this.pps;
            this.renderTimeline();
        }, { passive: false });
        // Soltar medios de la biblioteca o archivos.
        lane.addEventListener("dragover", (e) => {
            const types = e.dataTransfer.types;
            if (!types.includes(MediaMime) && !types.includes("Files"))
                return;
            e.preventDefault();
            e.dataTransfer.dropEffect = "copy";
            this.showDropMark(this.insertionAt(this.laneX(e)));
        });
        lane.addEventListener("dragleave", () => { this.dropMark.hidden = true; });
        lane.addEventListener("drop", (e) => {
            const at = this.insertionAt(this.laneX(e));
            this.dropMark.hidden = true;
            const media = e.dataTransfer.getData(MediaMime);
            if (media) {
                e.preventDefault();
                let offset = 0;
                for (const n of media.split(" ")) {
                    const before = this.count();
                    this.insertMedia(+n, at + offset);
                    offset += this.count() - before;
                }
                return;
            }
            if (e.dataTransfer.files.length) {
                e.preventDefault();
                e.stopPropagation();
                this.insertFiles([...e.dataTransfer.files], at);
            }
        });
        // Teclado: espacio reproduce, las flechas pasan de fotograma y Supr lo quita.
        document.addEventListener("keydown", (e) => {
            if (this.root.hidden || !this.doc || e.target.matches?.("input, select, textarea") || document.querySelector(".dialog-backdrop, dialog[open]"))
                return;
            const keys = { " ": () => this.togglePlay(), ArrowLeft: () => this.step(-1), ArrowRight: () => this.step(1), Home: () => this.seek(0),
                           Delete: () => this.remove(this.selected()) };
            const fn = !e.ctrlKey && !e.altKey && !e.metaKey && keys[e.key];
            if (fn) {
                e.preventDefault();
                e.stopImmediatePropagation();
                fn();
            }
        }, true);
    }

    showDropMark(at) {
        const s = this.starts();
        const x = at < s.length - 1 ? this.x(s[at]) - 1 : this.x(s[s.length - 1]);
        this.dropMark.style.left = `${x - 1}px`;
        this.dropMark.hidden = false;
    }

    // Miniatura de una capa (se olvida cuando cambian sus píxeles; ver setDocument).
    thumbFor(layer, w, h) {
        let t = this.thumbs.get(layer);
        if (!t || t.w !== w || t.h !== h || t.canvas !== layer.canvas) {
            t = { w, h, canvas: layer.canvas, image: thumbnail(layer.canvas, w, h) };
            this.thumbs.set(layer, t);
        }
        // Un mismo elemento no puede estar en dos clips: se clona el lienzo de la miniatura.
        const copy = createCanvas(t.image.width, t.image.height);
        context(copy).drawImage(t.image, 0, 0);
        return copy;
    }

    renderTimeline(resize = null) {
        if (this.root.hidden)
            return;
        const s = this.starts();
        if (resize) {
            const diff = resize.delay - (s[resize.index + 1] - s[resize.index]);
            for (let i = resize.index + 1; i < s.length; i++)
                s[i] += diff;
        }
        const total = s[s.length - 1];
        const width = Math.max(this.scroller.clientWidth, LeftMargin * 2 + Math.ceil((total * this.pps) / 1000) + 160);
        this.lane.style.width = `${width}px`;
        // Regla
        const dpr = window.devicePixelRatio || 1;
        this.ruler.width = Math.round(width * dpr);
        this.ruler.height = Math.round(RulerHeight * dpr);
        this.ruler.style.width = `${width}px`;
        const ctx = context(this.ruler);
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        const style = getComputedStyle(this.root);
        ctx.clearRect(0, 0, width, RulerHeight);
        ctx.strokeStyle = style.getPropertyValue("--line") || "#888";
        ctx.fillStyle = style.color;
        ctx.font = "10px sans-serif";
        const steps = [50, 100, 200, 250, 500, 1000, 2000, 5000, 10000];
        const major = steps.find((v) => (v * this.pps) / 1000 >= 70) || 10000, minor = major / 5;
        ctx.beginPath();
        for (let t = 0; this.x(t) <= width; t += minor) {
            const x = this.x(t) + 0.5, big = t % major === 0;
            ctx.moveTo(x, RulerHeight - (big ? 10 : 5));
            ctx.lineTo(x, RulerHeight);
            if (big)
                ctx.fillText(timeText(t), x + 3, 10);
        }
        ctx.stroke();
        // Clips
        const selected = this.selected(), layers = this.doc?.layers || [];
        const clips = [];
        for (let i = 0; i < layers.length; i++) {
            const x0 = this.x(s[i]), w = Math.max(6, this.x(s[i + 1]) - x0) - 2;
            const clip = el("div", { className: `gif-clip${i === selected ? " selected" : ""}${layers[i].visible ? "" : " hidden-frame"}`, dataset: { index: i } });
            clip.style.left = `${x0}px`;
            clip.style.width = `${w}px`;
            if (w > 14) {
                const img = this.thumbFor(layers[i], Math.max(8, w - 6), 50);
                img.classList.add("gif-clip-thumb");
                clip.append(img);
            }
            const text = w > 70 ? `${i + 1} · ${s[i + 1] - s[i]} ms` : w > 26 ? String(i + 1) : "";
            clip.append(el("span", { className: "gif-clip-label", textContent: text }), el("div", { className: "gif-clip-edge" }));
            clips.push(clip);
        }
        this.track.replaceChildren(...clips);
        this.empty.hidden = layers.length > 0;
        this.playhead.style.left = `${this.x(this.time) - 1}px`;
        if (this.playing) {
            const px = this.x(this.time), view = this.scroller;
            if (px < view.scrollLeft + 40 || px > view.scrollLeft + view.clientWidth - 60)
                view.scrollLeft = Math.max(0, px - 60);
        }
    }

    drawPreview() {
        const canvas = this.preview, box = canvas.parentElement;
        if (this.root.hidden || !box)
            return;
        const dpr = window.devicePixelRatio || 1, w = box.clientWidth, h = box.clientHeight;
        canvas.width = Math.max(1, Math.round(w * dpr));
        canvas.height = Math.max(1, Math.round(h * dpr));
        canvas.style.width = `${w}px`;
        canvas.style.height = `${h}px`;
        const ctx = context(canvas);
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.fillStyle = "#18181a";
        ctx.fillRect(0, 0, w, h);
        const layer = this.doc?.layers[this.selected()];
        if (!layer) {
            ctx.fillStyle = "#c8c8c8";
            ctx.font = "13px sans-serif";
            ctx.textAlign = "center";
            ctx.fillText(tr("Sin fotogramas: arrastra imágenes o GIF a la línea de tiempo"), w / 2, h / 2);
            return;
        }
        const scale = Math.min((w - 16) / layer.canvas.width, (h - 16) / layer.canvas.height);
        const dw = Math.max(1, Math.round(layer.canvas.width * scale)), dh = Math.max(1, Math.round(layer.canvas.height * scale));
        const x = Math.round((w - dw) / 2), y = Math.round((h - dh) / 2);
        // Cuadros de transparencia
        ctx.save();
        ctx.beginPath();
        ctx.rect(x, y, dw, dh);
        ctx.clip();
        ctx.fillStyle = "#ffffff";
        ctx.fillRect(x, y, dw, dh);
        ctx.fillStyle = "#cccccc";
        for (let yy = y; yy < y + dh; yy += 8)
            for (let xx = x + (((yy - y) / 8) % 2) * 8; xx < x + dw; xx += 16)
                ctx.fillRect(xx, yy, 8, 8);
        ctx.restore();
        ctx.imageSmoothingEnabled = scale < 1;
        ctx.imageSmoothingQuality = "high";
        ctx.drawImage(layer.canvas, x, y, dw, dh);
    }

    // 'full' en falso (mientras se reproduce): solo la vista previa, el cabezal, el tiempo y el clip
    // elegido, sin rehacer la línea de tiempo en cada fotograma.
    update(full = true) {
        if (this.root.hidden)
            return;
        const has = this.count() > 0, i = this.selected();
        if (!full) {
            const s = this.starts();
            this.timeLabel.textContent = `${timeText(this.time)} / ${timeText(s[s.length - 1])}` + (i >= 0 ? `   ${tr("Fotograma %1 de %2", i + 1, this.count())}` : "");
            this.drawPreview();
            for (const clip of this.track.children)
                clip.classList.toggle("selected", +clip.dataset.index === i);
            const px = this.x(this.time), view = this.scroller;
            this.playhead.style.left = `${px - 1}px`;
            if (px < view.scrollLeft + 40 || px > view.scrollLeft + view.clientWidth - 60)
                view.scrollLeft = Math.max(0, px - 60);
            return;
        }
        this.title.textContent = this.doc ? tr("Editor de GIF: %1", this.doc.displayName()) : tr("Editor de GIF");
        for (const c of this.frameControls)
            c.disabled = !has || i < 0;
        if (has && i >= 0 && document.activeElement !== this.delayInput)
            this.delayInput.value = this.delay(i);
        const s = this.starts();
        this.timeLabel.textContent = `${timeText(this.time)} / ${timeText(s[s.length - 1])}` + (i >= 0 ? `   ${tr("Fotograma %1 de %2", i + 1, this.count())}` : "");
        this.drawPreview();
        this.renderTimeline();
    }
}
