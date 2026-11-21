// Archivo > Editor de GIF (src/app/GifEditor.cpp de Kaint): la imagen como una animación, con una
// interfaz de editor de vídeo sencillo: la biblioteca de medios (las imágenes abiertas y las que se
// importan o se arrastran), la vista previa con su reproductor y la línea de tiempo con un clip por
// fotograma. Es otra vista de la misma imagen: cada fotograma es una capa (la de abajo, el primero)
// con su duración en el nombre («Fotograma 1 (100ms)»), como al abrir y guardar un GIF; cada
// cambio es un paso del historial. Cada imagen recuerda en qué vista está. Como en Filmora, cada
// fotograma puede llevar puntos clave (Motion.js) que lo mueven, lo escalan, lo giran y cambian su
// opacidad: el panel de la derecha, su gráfica y los rombos bajo la línea de tiempo.
import { Layer } from "../core/Document.js";
import { delayFromName, nameWithDelay } from "../core/Codecs.js";
import { animationFrames, download, openFile, OpenAccept } from "../core/ImageIO.js";
import { canEncodeMp4, canEncodeWebp, encodeAnimatedWebp, encodeApng, encodeMp4, recordVideo } from "../core/AnimExport.js";
import { defaultSaveOptions, encodeAnimation } from "../core/Encode.js";
import * as Motion from "../core/Motion.js";
import { context, createCanvas, imageSignature, thumbnail } from "../core/Raster.js";
import { currentLanguage, tr, trn } from "../i18n.js";
import { Action } from "./Actions.js";
import { Dialog, el, message, promptNumber } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { popupMenu } from "./Menu.js";

const DefaultDelay = 100, MinDelay = 20, MediaMime = "application/x-kaint-gif-media";
const RulerHeight = 22, LeftMargin = 12;

const delayOf = (layer) => Math.max(MinDelay, delayFromName(layer?.name, DefaultDelay));
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

// Los fotogramas copiados con Ctrl+C en el editor: con su nombre (la duración va en él), si se ven,
// su opacidad, su fusión y su animación. Al portapapeles del sistema va el primero como imagen; al
// pegar, si sigue siendo esa imagen (o no se pudo escribir en él), se pegan estos.
let frameClipboard = null;

// Al copiar en el editor de imágenes, lo copiado allí es lo último (App.copy).
export function forgetCopiedFrames() {
    frameClipboard = null;
}

// Un rombo (punto clave): blanco sin elegir y del color de acento elegido.
function drawDiamond(ctx, x, y, r, selected, accent) {
    ctx.beginPath();
    ctx.moveTo(x, y - r);
    ctx.lineTo(x + r, y);
    ctx.lineTo(x, y + r);
    ctx.lineTo(x - r, y);
    ctx.closePath();
    ctx.fillStyle = selected ? accent : "#fafafa";
    ctx.fill();
    ctx.lineWidth = 1.2;
    ctx.strokeStyle = selected ? "rgba(0, 0, 0, 0.55)" : "#3c3c3c";
    ctx.stroke();
}

// Las opciones de la forma del movimiento (la cuarta imagen de Filmora): lineal, curva, curva
// continua, congelar, y la entrada y la salida suaves.
function interpEntries(key, setInterp, setEase) {
    const current = key?.i || 0;
    return [
        { text: tr("Lineal"), checked: current === Motion.Interp.Linear, run: () => setInterp(Motion.Interp.Linear) },
        { text: tr("Curva"), checked: current === Motion.Interp.Curve, run: () => setInterp(Motion.Interp.Curve) },
        { text: tr("Curva continua"), checked: current === Motion.Interp.Continuous, run: () => setInterp(Motion.Interp.Continuous) },
        { text: tr("Congelar"), checked: current === Motion.Interp.Freeze, run: () => setInterp(Motion.Interp.Freeze) },
        "-",
        { text: tr("Entrada suave"), checked: !!key?.in, run: () => setEase("in", !key?.in) },
        { text: tr("Salida suave"), checked: !!key?.out, run: () => setEase("out", !key?.out) },
    ];
}

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
        // Con el dedo, los clips más anchos (más fáciles de tocar y de estirar).
        this.pps = matchMedia("(pointer: coarse)").matches ? 900 : 600;
        this.time = 0;
        this.playing = false;
        // El fotograma que se ve al reproducir (sin tocar la capa activa: eso movía el panel de capas
        // y la ventana en cada fotograma), los otros elegidos (Ctrl o Mayús + clic), el punto clave
        // elegido (su tiempo dentro del fotograma) y lo que se arrastra en la vista previa.
        this.playFrame = -1;
        this.extra = [];
        this.selectedKey = -1;
        this.keyDrag = null;
        this.dragOffset = null;
        this.lastActive = -1;
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
        // En el teléfono, el panel de la animación es una hoja que se abre desde aquí.
        const inspectorToggle = button(tr("Animación"), tr("Puntos clave del fotograma elegido"),
                                       () => { this.root.classList.toggle("inspector-open"); this.refreshInspector(); }, "gif-inspector-toggle");
        inspectorToggle.prepend(el("span", { className: "gif-diamond" }));
        // Otros formatos: PNG animado, WebP animado y vídeo MP4.
        const exportMore = button("▾", tr("Exportar en otro formato: PNG animado, WebP animado o vídeo MP4"), () => {
            const r = exportMore.getBoundingClientRect();
            popupMenu([
                { text: tr("GIF..."), run: () => this.app.save(this.app.doc, true, "gif") },
                { text: tr("PNG animado (APNG)..."), run: () => this.exportAs("apng") },
                { text: tr("WebP animado..."), run: () => this.exportAs("webp") },
                { text: tr("Vídeo MP4..."), run: () => this.exportAs("mp4") },
            ], r.left, r.bottom);
        }, "pushbutton gif-export-more");
        // Cuánto pesará el GIF y cuánto dura, con una barrita que avisa si pesa demasiado.
        this.sizeFill = el("div", { className: "fill" });
        this.sizeText = el("span", { textContent: "…" });
        this.sizeMeter = el("div", { className: "gif-size" }, [el("div", { className: "bar" }, [this.sizeFill]), this.sizeText]);
        const head = el("div", { className: "gif-head" }, [this.title, this.sizeMeter, inspectorToggle, back,
                                                            el("span", { className: "gif-split" }, [exportButton, exportMore])]);

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
        const undo = button(icon("edit-undo", 16), tr("Deshacer"), () => this.app.undo());
        const redo = button(icon("edit-redo", 16), tr("Rehacer"), () => this.app.redo());
        const dup = frameButton(tr("Duplicar"), tr("Duplica los fotogramas elegidos (Ctrl+D)"), () => this.duplicateFrames(this.selectedFrames()));
        dup.prepend(icon("layer-duplicate", 16));
        const del = frameButton(tr("Eliminar"), tr("Quita los fotogramas elegidos (Supr)"), () => this.removeFrames(this.selectedFrames()));
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
        // Los puntos clave del fotograma elegido: se arrastran (más separados, más dura el movimiento
        // entre ellos) y el clic derecho elige la forma del movimiento.
        this.keyLane = el("div", { className: "gif-keylane", title: tr("Puntos clave del fotograma elegido: arrástralos para cambiar cuándo llegan; clic derecho para la forma del movimiento") });
        this.lane = el("div", { className: "gif-lane" }, [this.ruler, this.track, this.keyLane, this.dropMark, this.playhead, this.empty]);
        this.scroller = el("div", { className: "gif-scroller" }, [this.lane]);
        const timeline = el("div", { className: "gif-timeline" }, [bar, this.scroller]);

        root.append(head, el("div", { className: "gif-top" }, [bin, player, this.buildInspector()]), timeline);
        this.installTimeline();
        this.installPreview();
        new ResizeObserver(() => { this.drawPreview(); this.renderTimeline(); this.drawGraph(); }).observe(root);
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
        this.extra = [];
        this.selectedKey = -1;
        this.lastActive = doc ? doc.active : -1;
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
                // Otro fotograma: el punto clave elegido era del anterior.
                if (doc.active !== this.lastActive) {
                    this.lastActive = doc.active;
                    this.selectedKey = -1;
                }
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
        this.extra = [];
        this.doc.setActiveIndex(i);
        if (!this.playing)
            this.time = this.starts()[i];
        this.update();
    }

    seek(ms) {
        const s = this.starts();
        this.time = s.length > 1 ? Math.max(0, Math.min(ms, s[s.length - 1] - 1)) : 0;
        const i = this.frameAt(this.time);
        if (this.playing) {
            this.playFrame = i;
            this.update(false);
        } else if (this.doc && i >= 0 && i !== this.doc.active) {
            this.extra = [];
            this.doc.setActiveIndex(i);
        } else {
            this.update(false);
        }
    }

    // El fotograma que se ve: el que se reproduce o el elegido.
    shownFrame() {
        return this.playing && this.playFrame >= 0 ? this.playFrame : this.selected();
    }

    // El cabezal dentro del fotograma elegido (ms desde su comienzo).
    localTime() {
        const i = this.selected(), s = this.starts();
        if (i < 0 || i + 1 >= s.length)
            return 0;
        return clamp(this.time - s[i], 0, Math.max(0, s[i + 1] - s[i] - 1));
    }

    seekLocal(ms) {
        const i = this.selected(), s = this.starts();
        if (i < 0 || i + 1 >= s.length)
            return;
        this.stop();
        this.time = s[i] + clamp(Math.round(ms), 0, Math.max(0, s[i + 1] - s[i] - 1));
        this.update(false);
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
        this.playFrame = this.frameAt(this.time);
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
            this.playFrame = i;
            this.update(false);
            if (this.playing)
                this.raf = requestAnimationFrame(frame);
        };
        this.raf = requestAnimationFrame(frame);
    }

    stop() {
        const wasPlaying = this.playing, frame = this.playFrame;
        this.playing = false;
        this.playFrame = -1;
        cancelAnimationFrame(this.raf);
        this.playButton?.replaceChildren(icon("media-playback-start", 16));
        // Al parar, el fotograma del cabezal pasa a ser el elegido (la capa activa).
        if (wasPlaying && this.doc && frame >= 0 && frame < this.count()) {
            this.extra = [];
            if (frame !== this.doc.active)
                this.doc.setActiveIndex(frame);
            else
                this.update();
        }
    }

    // --- Cambios (un paso del historial cada uno) ------------------------------------------

    apply(layers, props, active, name, iconName) {
        const doc = this.doc;
        if (!doc)
            return;
        this.stop();
        this.extra = [];
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

    // --- Varios fotogramas a la vez (Ctrl o Mayús + clic, Ctrl+A) ----------------------------

    selectedFrames() {
        const active = this.selected(), list = active >= 0 ? [active] : [];
        for (const i of this.extra)
            if (i >= 0 && i < this.count() && !list.includes(i))
                list.push(i);
        return list.sort((a, b) => a - b);
    }

    isFrameSelected(i) {
        return i >= 0 && (i === this.selected() || this.extra.includes(i));
    }

    toggleFrameSelection(i) {
        if (!this.doc || i < 0 || i >= this.count())
            return;
        if (i === this.selected()) {
            // Quitar el elegido: pasa a serlo otro de la selección (si queda alguno).
            if (!this.extra.length)
                return;
            const next = this.extra.shift(), rest = this.extra;
            this.doc.setActiveIndex(next);
            this.extra = rest;
        } else if (this.extra.includes(i)) {
            this.extra = this.extra.filter((x) => x !== i);
        } else {
            this.extra.push(i);
        }
        this.update();
    }

    selectFrameRange(i) {
        if (!this.doc || i < 0 || i >= this.count())
            return;
        const anchor = Math.max(0, this.selected());
        this.extra = [];
        for (let k = Math.min(anchor, i); k <= Math.max(anchor, i); k++)
            if (k !== anchor)
                this.extra.push(k);
        this.update();
    }

    selectAllFrames() {
        this.extra = [];
        for (let i = 0; i < this.count(); i++)
            if (i !== this.selected())
                this.extra.push(i);
        this.update();
    }

    removeFrames(indexes) {
        if (!this.doc || !indexes.length)
            return;
        if (indexes.length === 1) {
            this.remove(indexes[0]);
            return;
        }
        // Una imagen necesita al menos una capa.
        if (indexes.length >= this.count())
            return;
        const { layers, props } = this.current();
        const keep = layers.map((_, i) => !indexes.includes(i));
        this.apply(layers.filter((_, i) => keep[i]), props.filter((_, i) => keep[i]), Math.min(Math.min(...indexes), this.count() - indexes.length - 1),
                   tr("Eliminar fotogramas"), "edit-delete");
    }

    duplicateFrames(indexes) {
        if (!this.doc || !indexes.length)
            return;
        if (indexes.length === 1) {
            this.duplicate(indexes[0]);
            return;
        }
        // Las copias, juntas, detrás del último elegido.
        const { layers, props } = this.current();
        const at = Math.max(...indexes) + 1;
        indexes.forEach((i, k) => {
            layers.splice(at + k, 0, this.doc.layers[i].duplicate(this.doc.layers[i].name));
            props.splice(at + k, 0, { ...props[i] });
        });
        this.apply(layers, props, at, tr("Duplicar fotogramas"), "layer-duplicate");
        this.extra = indexes.slice(1).map((_, k) => at + 1 + k);
        this.update();
    }

    // --- Portapapeles (Ctrl+C, Ctrl+X, Ctrl+V) ----------------------------------------------

    copyFrames() {
        const indexes = this.selectedFrames();
        if (!this.doc || !indexes.length)
            return;
        const frames = indexes.map((i) => {
            const layer = this.doc.layers[i], canvas = createCanvas(layer.canvas.width, layer.canvas.height);
            context(canvas).drawImage(layer.canvas, 0, 0);
            return { canvas, props: layer.props() };
        });
        const entry = frameClipboard = { frames, signature: imageSignature(frames[0].canvas), system: false };
        // El primero también para el editor de imágenes (Pegar) y para otros programas.
        this.app.clipboard = frames[0].canvas;
        this.app.copySignature = entry.signature;
        this.app.copyOrigin = { x: 0, y: 0 };
        this.app.lastClipboardSize = { w: frames[0].canvas.width, h: frames[0].canvas.height };
        frames[0].canvas.toBlob(async (blob) => {
            try {
                await navigator.clipboard.write([new ClipboardItem({ "image/png": blob })]);
                entry.system = true;
            } catch {
                // Sin acceso al portapapeles del sistema: queda el de Kaint.
            }
        }, "image/png");
    }

    cutFrames() {
        const indexes = this.selectedFrames();
        if (!this.doc || !indexes.length)
            return;
        this.copyFrames();
        // No se pueden quitar todos: se copian.
        if (indexes.length < this.count())
            this.removeFrames(indexes);
    }

    // 'image': la imagen del portapapeles del sistema (del evento «paste»), null si no trae ninguna;
    // sin ella (desde el menú), se intenta leer.
    async pasteFrames(image = undefined) {
        if (!this.doc)
            return;
        if (image === undefined) {
            image = null;
            try {
                for (const item of await navigator.clipboard.read()) {
                    const type = item.types.find((t) => t.startsWith("image/"));
                    if (type) {
                        image = await createImageBitmap(await item.getType(type));
                        break;
                    }
                }
            } catch {
                // Sin permiso: lo copiado en Kaint.
            }
        }
        const selected = this.selectedFrames();
        const at = selected.length ? selected[selected.length - 1] + 1 : this.count();
        let frames;
        if (frameClipboard && (!image || !frameClipboard.system || imageSignature(image) === frameClipboard.signature)) {
            frames = frameClipboard.frames;
        } else {
            // Una imagen de otro programa (o del editor de imágenes): un fotograma con la duración del elegido.
            const source = image || this.app.clipboard;
            if (!source)
                return;
            const delay = selected.length ? this.delay(selected[selected.length - 1]) : DefaultDelay;
            frames = [{ canvas: source, props: { name: nameWithDelay(tr("Fotograma %1", at + 1), delay) } }];
        }
        const { layers, props } = this.current();
        frames.forEach((f, k) => {
            const layer = new Layer(this.doc.width, this.doc.height, f.props.name);
            layer.ctx.drawImage(fitted(f.canvas, this.doc.width, this.doc.height), 0, 0);
            layer.setProps({ ...f.props, motion: f.props.motion || null });
            layers.splice(at + k, 0, layer);
            props.splice(at + k, 0, layer.props());
        });
        this.apply(layers, props, at, frames.length === 1 ? tr("Pegar fotograma") : tr("Pegar fotogramas"), "edit-paste");
        this.extra = frames.slice(1).map((_, k) => at + 1 + k);
        this.update();
    }

    // --- Puntos clave (Motion.js) -----------------------------------------------------------

    motion(i) {
        return this.doc?.layers[i]?.motion || null;
    }

    setMotion(i, clip, text) {
        if (!this.doc || i < 0 || i >= this.count())
            return;
        const value = clip && !Motion.isEmpty(clip) ? clip : null;
        if (JSON.stringify(value) === JSON.stringify(this.motion(i)))
            return;
        const { layers, props } = this.current();
        props[i] = { ...props[i], motion: value };
        this.apply(layers, props, i, text, "transform-move");
    }

    selectKeyTime(ms) {
        this.selectedKey = ms;
        this.seekLocal(ms);
    }

    // Un punto más allá del final: el fotograma dura hasta él (y un poco más, para que se vea).
    moveKeyTime(from, to) {
        const i = this.selected();
        if (!this.doc || i < 0)
            return;
        to = Math.max(0, to);
        const clip = Motion.cloneClip(this.motion(i));
        Motion.moveKeys(clip, from, to);
        const { layers, props } = this.current();
        props[i] = { ...props[i], motion: Motion.isEmpty(clip) ? null : clip };
        if (to >= this.delay(i))
            props[i].name = nameWithDelay(props[i].name, to + 10);
        const start = this.starts()[i];
        this.apply(layers, props, i, tr("Mover punto clave"), "transform-move");
        this.selectedKey = to;
        this.time = start + to;
        this.update(false);
    }

    removeKeyTime(ms) {
        const i = this.selected(), clip = Motion.cloneClip(this.motion(i));
        Motion.removeKeys(clip, ms);
        this.selectedKey = -1;
        this.setMotion(i, clip, tr("Quitar punto clave"));
    }

    keyMenu(ms, x, y) {
        const i = this.selected();
        const change = (fn) => {
            const clip = Motion.cloneClip(this.motion(i));
            fn(clip);
            this.setMotion(i, clip, tr("Forma del movimiento"));
        };
        popupMenu([
            ...interpEntries(Motion.keyAt(this.motion(i), ms), (kind) => change((c) => Motion.setInterp(c, ms, kind)),
                             (which, on) => change((c) => Motion.setEase(c, ms, which, on))),
            "-",
            { text: tr("Quitar el punto clave"), icon: "edit-delete", shortcut: "Supr", run: () => this.removeKeyTime(ms) },
        ], x, y);
    }

    // Sustituye el fotograma por los que salen de su animación (como al exportar), para editarlos uno a uno.
    bakeMotion(i) {
        const clip = this.motion(i);
        if (!this.doc || Motion.isEmpty(clip))
            return;
        const parts = Motion.expand(this.doc.layers[i].canvas, clip, this.delay(i));
        const { layers, props } = this.current();
        const original = props[i];
        const added = parts.map((part, k) => {
            const layer = new Layer(this.doc.width, this.doc.height, nameWithDelay(tr("Fotograma %1", i + k + 1), part.delay));
            layer.ctx.drawImage(part.canvas, 0, 0);
            return layer;
        });
        layers.splice(i, 1, ...added);
        props.splice(i, 1, ...added.map((l) => ({ ...original, name: l.name, motion: null })));
        this.selectedKey = -1;
        this.apply(layers, props, i, tr("Convertir la animación en fotogramas"), "layer-duplicate");
    }

    // La transformación del fotograma 'i' en el cabezal, como en el GIF exportado (de 'step' en 'step' ms).
    frameTransform(i) {
        const clip = this.motion(i), s = this.starts();
        let t = clamp(this.time - s[i], 0, Math.max(0, s[i + 1] - s[i] - 1));
        if (Motion.isAnimated(clip)) {
            const step = Math.max(10, clip.step || 50);
            t = Math.floor(t / step) * step;
        }
        const transform = Motion.sample(clip, t);
        if (i === this.selected() && this.dragOffset) {
            transform.x += this.dragOffset.x;
            transform.y += this.dragOffset.y;
        }
        return transform;
    }

    // Arrastrar la imagen de la vista previa la mueve (con puntos clave de posición, pone uno en el cabezal).
    installPreview() {
        let drag = null;
        this.preview.addEventListener("pointerdown", (e) => {
            if (!this.doc || e.button !== 0 || this.selected() < 0)
                return;
            this.stop();
            if (!this.previewGeom)
                return;
            drag = { id: e.pointerId, x: e.clientX, y: e.clientY };
            this.preview.setPointerCapture(e.pointerId);
        });
        this.preview.addEventListener("pointermove", (e) => {
            if (!drag || e.pointerId !== drag.id)
                return;
            const scale = this.previewGeom?.scale || 1;
            this.dragOffset = { x: (e.clientX - drag.x) / scale, y: (e.clientY - drag.y) / scale };
            this.drawPreview();
        });
        const end = (e) => {
            if (!drag || e.pointerId !== drag.id)
                return;
            drag = null;
            const offset = this.dragOffset;
            this.dragOffset = null;
            if (e.type === "pointercancel" || !offset || (Math.abs(offset.x) < 0.5 && Math.abs(offset.y) < 0.5)) {
                this.drawPreview();
                return;
            }
            const i = this.selected(), clip = Motion.cloneClip(this.motion(i)), t = this.localTime();
            for (const [prop, d] of [["x", offset.x], ["y", offset.y]]) {
                const track = Motion.editTrack(clip, prop), v = Math.round(Motion.trackValue(track, t, prop) + d);
                if (track.keys.length)
                    Motion.setKey(track, t, v);
                else
                    track.base = v;
            }
            this.setMotion(i, clip, tr("Mover la imagen"));
        };
        this.preview.addEventListener("pointerup", end);
        this.preview.addEventListener("pointercancel", end);
    }

    // --- Panel de la animación (derecha) ----------------------------------------------------

    buildInspector() {
        this.rows = {};
        this.inspectorTitle = el("div", { className: "gif-section-title" });
        const grid = el("div", { className: "gif-props" });
        const section = (text) => grid.append(el("div", { className: "gif-prop-section", textContent: text }));
        const row = (prop, label, suffix, min, max, step) => {
            const input = el("input", { type: "number", min, max, step, className: "gif-prop-value" });
            input.addEventListener("change", () => {
                if (input.value !== "")
                    this.valueEdited(prop, clamp(+input.value, min, max));
            });
            const prev = button("◀", tr("Punto clave anterior"), () => this.jumpKey(prop, -1), "gif-key-nav");
            const diamond = button(el("span", { className: "gif-diamond" }), tr("Pone o quita un punto clave en el cabezal"), () => this.toggleKey(prop), "gif-key-toggle");
            const next = button("▶", tr("Punto clave siguiente"), () => this.jumpKey(prop, 1), "gif-key-nav");
            this.rows[prop] = { input, prev, diamond, next, decimals: step < 1 ? 1 : 0 };
            grid.append(el("label", { textContent: label }), el("span", { className: "gif-prop-input" }, [input, el("span", { textContent: suffix })]),
                        prev, diamond, next);
        };
        section(tr("Escala"));
        row("scaleX", "X", "%", 1, 2000, 1);
        row("scaleY", "Y", "%", 1, 2000, 1);
        this.lockScale = el("input", { type: "checkbox" });
        this.lockScale.addEventListener("change", () => {
            const i = this.selected(), clip = Motion.cloneClip(this.motion(i));
            clip.lockScale = this.lockScale.checked;
            this.setMotion(i, clip, tr("Escala proporcional"));
        });
        grid.append(el("label", { className: "gif-check gif-prop-wide", title: tr("Al cambiar la escala X o Y, la otra cambia igual") },
                        [this.lockScale, tr("Proporcional")]));
        section(tr("Posición"));
        row("x", "X", "px", -20000, 20000, 1);
        row("y", "Y", "px", -20000, 20000, 1);
        section(tr("Rotación"));
        row("rotation", "", "°", -3600, 3600, 1);
        section(tr("Opacidad"));
        row("opacity", "", "%", 0, 100, 1);

        const names = { x: tr("Posición X"), y: tr("Posición Y"), scaleX: tr("Escala X"), scaleY: tr("Escala Y"), rotation: tr("Rotación"), opacity: tr("Opacidad") };
        this.graphProp = el("select", { onchange: () => this.drawGraph() }, Motion.Props.map((p) => el("option", { value: p, textContent: names[p] })));
        this.graph = el("canvas", { className: "gif-graph", title: tr("Cómo cambia el valor a lo largo del fotograma. Arrastra un rombo para cambiar su valor o su momento; clic derecho para la forma de la curva.") });
        this.installGraph();

        this.fps = el("select", { title: tr("Cuántas imágenes por segundo tiene el GIF mientras este fotograma se mueve") },
                      [10, 15, 20, 25, 30, 50].map((f) => el("option", { value: Math.trunc(1000 / f), textContent: tr("%1 fps", f) })));
        this.fps.addEventListener("change", () => {
            const i = this.selected(), clip = Motion.cloneClip(this.motion(i));
            clip.step = +this.fps.value;
            this.setMotion(i, clip, tr("Fotogramas por segundo"));
        });
        this.resetButton = button(tr("Restablecer"), tr("Quita la animación de este fotograma"), () => this.setMotion(this.selected(), null, tr("Quitar la animación")));
        this.resetButton.prepend(icon("edit-undo", 16));
        this.bakeButton = button(tr("Convertir en fotogramas"), tr("Sustituye este fotograma por los que salen de su animación (como al exportar), para editarlos uno a uno"),
                                 () => this.bakeMotion(this.selected()));
        this.bakeButton.prepend(icon("layer-duplicate", 16));
        this.inspector = el("div", { className: "gif-inspector" }, [
            this.inspectorTitle, grid,
            el("div", { className: "gif-row" }, [el("label", { textContent: tr("Gráfica:") }), this.graphProp]),
            el("div", { className: "gif-graph-box" }, [this.graph]),
            el("div", { className: "gif-row" }, [el("label", { textContent: tr("Al exportar:") }), this.fps]),
            el("div", { className: "gif-row gif-wrap" }, [this.resetButton, this.bakeButton]),
            el("div", { className: "gif-hint", textContent: tr("El rombo pone o quita un punto clave en el cabezal. Con puntos, cambiar un valor pone uno ahí. En la línea de tiempo, separa los rombos para que el movimiento dure más.") }),
        ]);
        return this.inspector;
    }

    refreshInspector() {
        if (!this.inspector)
            return;
        const i = this.selected(), has = i >= 0, clip = this.motion(i);
        for (const c of this.inspector.querySelectorAll("input, select, button"))
            c.disabled = !has;
        this.inspectorTitle.textContent = has ? tr("Animación del fotograma %1", i + 1) : tr("Animación");
        this.lockScale.checked = clip ? clip.lockScale !== false : true;
        const step = clip?.step || 50;
        this.fps.value = [...this.fps.options].some((o) => +o.value === step) ? step : 50;
        this.resetButton.disabled = !has || Motion.isEmpty(clip);
        this.bakeButton.disabled = !has || Motion.isEmpty(clip);
        this.refreshInspectorValues();
    }

    refreshInspectorValues() {
        if (!this.inspector)
            return;
        const i = this.selected(), clip = this.motion(i), t = this.localTime();
        for (const p of Motion.Props) {
            const r = this.rows[p], track = Motion.track(clip, p), keys = track.keys || [];
            if (document.activeElement !== r.input)
                r.input.value = (Math.round(Motion.trackValue(track, t, p) * 10) / 10).toString();
            r.diamond.classList.toggle("filled", Motion.keyIndex(track, t) >= 0);
            r.prev.disabled = i < 0 || !keys.some((k) => k.t < t);
            r.next.disabled = i < 0 || !keys.some((k) => k.t > t);
        }
        this.drawGraph();
    }

    valueEdited(prop, value) {
        const i = this.selected();
        if (i < 0 || !Number.isFinite(value))
            return;
        const clip = Motion.cloneClip(this.motion(i)), t = this.localTime();
        // Con puntos clave, el valor se pone como un punto en el cabezal; sin ellos, es el de siempre.
        const set = (p, v) => {
            const track = Motion.editTrack(clip, p);
            if (track.keys.length)
                Motion.setKey(track, t, v);
            else
                track.base = v;
        };
        set(prop, value);
        if (clip.lockScale && (prop === "scaleX" || prop === "scaleY"))
            set(prop === "scaleX" ? "scaleY" : "scaleX", value);
        this.setMotion(i, clip, tr("Cambiar la animación"));
    }

    toggleKey(prop) {
        const i = this.selected();
        if (i < 0)
            return;
        const clip = Motion.cloneClip(this.motion(i)), t = this.localTime();
        const toggle = (p) => {
            const track = Motion.editTrack(clip, p), k = Motion.keyIndex(track, t);
            if (k >= 0) {
                if (track.keys.length === 1)
                    track.base = track.keys[k].v;
                track.keys.splice(k, 1);
            } else {
                Motion.setKey(track, t, Motion.trackValue(track, t, p));
            }
        };
        const adding = Motion.keyIndex(Motion.track(clip, prop), t) < 0;
        toggle(prop);
        if (clip.lockScale && (prop === "scaleX" || prop === "scaleY")) {
            const other = prop === "scaleX" ? "scaleY" : "scaleX";
            if ((Motion.keyIndex(Motion.track(clip, other), t) < 0) === adding)
                toggle(other);
        }
        this.selectedKey = t;
        this.setMotion(i, clip, adding ? tr("Añadir punto clave") : tr("Quitar punto clave"));
    }

    jumpKey(prop, direction) {
        const t = this.localTime();
        let best = -1;
        for (const k of Motion.track(this.motion(this.selected()), prop).keys || [])
            if ((direction < 0 && k.t < t && (best < 0 || k.t > best)) || (direction > 0 && k.t > t && (best < 0 || k.t < best)))
                best = k.t;
        if (best >= 0)
            this.selectKeyTime(best);
    }

    // --- Gráfica de un valor a lo largo del fotograma ---------------------------------------

    graphPlot() {
        const w = this.graph.clientWidth, h = this.graph.clientHeight;
        return { left: 34, top: 8, right: w - 10, bottom: h - 16, w, h };
    }

    graphRange(clip, prop) {
        const track = Motion.track(clip, prop);
        let a = track.base ?? Motion.defaultValue(prop), b = a;
        for (const k of track.keys || []) {
            a = Math.min(a, k.v);
            b = Math.max(b, k.v);
        }
        if (this.graphDrag) {
            a = Math.min(a, this.graphDrag.lo);
            b = Math.max(b, this.graphDrag.hi);
        }
        const pad = Math.max(1, (b - a) * 0.15);
        return [a - pad, b + pad];
    }

    graphPoint(t, v, lo, hi, r = this.graphPlot()) {
        const duration = Math.max(1, this.delay(this.selected()));
        return [r.left + (r.right - r.left) * clamp(t / duration, 0, 1), r.bottom - ((r.bottom - r.top) * (v - lo)) / (hi - lo)];
    }

    // La animación con el punto que se arrastra en la gráfica donde va.
    graphClip() {
        const clip = this.motion(this.selected()), d = this.graphDrag;
        if (!d)
            return clip;
        const c = Motion.cloneClip(clip), track = Motion.editTrack(c, d.prop);
        const [k] = track.keys.splice(d.index, 1);
        if (k) {
            // Sobre otro punto, lo sustituye.
            const existing = Motion.keyIndex(track, d.t);
            if (existing >= 0)
                track.keys.splice(existing, 1);
            k.t = d.t;
            k.v = d.v;
            const at = track.keys.findIndex((x) => x.t > d.t);
            track.keys.splice(at < 0 ? track.keys.length : at, 0, k);
        }
        return c;
    }

    graphKeyAt(x, y) {
        const prop = this.graphProp.value, track = Motion.track(this.motion(this.selected()), prop);
        const [lo, hi] = this.graphRange(this.motion(this.selected()), prop);
        return (track.keys || []).findIndex((k) => {
            const [px, py] = this.graphPoint(k.t, k.v, lo, hi);
            return Math.hypot(px - x, py - y) <= 8;
        });
    }

    drawGraph() {
        const canvas = this.graph;
        if (!canvas || this.root.hidden || !canvas.clientWidth)
            return;
        const dpr = window.devicePixelRatio || 1, r = this.graphPlot();
        canvas.width = Math.round(r.w * dpr);
        canvas.height = Math.round(r.h * dpr);
        const ctx = context(canvas), style = getComputedStyle(this.root);
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
        ctx.fillStyle = style.getPropertyValue("--base").trim() || "#ffffff";
        ctx.fillRect(0, 0, r.w, r.h);
        const i = this.selected();
        if (i < 0)
            return;
        const prop = this.graphProp.value, clip = this.graphClip(), track = Motion.track(clip, prop);
        const [lo, hi] = this.graphRange(clip, prop), text = style.color || "#000", accent = style.getPropertyValue("--accent").trim() || "#2e8def";
        // Cuadrícula y escala
        ctx.font = "10px sans-serif";
        ctx.textAlign = "right";
        ctx.textBaseline = "middle";
        const decimals = hi - lo < 8 ? 1 : 0;
        for (let k = 0; k <= 4; k++) {
            const y = r.bottom - ((r.bottom - r.top) * k) / 4;
            ctx.strokeStyle = style.getPropertyValue("--line").trim() || "#888";
            ctx.setLineDash([2, 3]);
            ctx.beginPath();
            ctx.moveTo(r.left, y + 0.5);
            ctx.lineTo(r.right, y + 0.5);
            ctx.stroke();
            ctx.setLineDash([]);
            ctx.globalAlpha = 0.7;
            ctx.fillStyle = text;
            ctx.fillText((lo + ((hi - lo) * k) / 4).toFixed(decimals), r.left - 4, y);
            ctx.globalAlpha = 1;
        }
        // La curva
        const duration = Math.max(1, this.delay(i));
        ctx.strokeStyle = "rgb(64, 200, 170)";
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (let x = 0; x <= r.right - r.left; x++) {
            const t = Math.round((duration * x) / (r.right - r.left));
            const [px, py] = this.graphPoint(t, Motion.trackValue(track, t, prop), lo, hi, r);
            if (x === 0)
                ctx.moveTo(px, py);
            else
                ctx.lineTo(px, py);
        }
        ctx.stroke();
        // El cabezal
        const [hx] = this.graphPoint(this.localTime(), lo, lo, hi, r);
        ctx.strokeStyle = "#e63c3c";
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(hx, r.top);
        ctx.lineTo(hx, r.bottom);
        ctx.stroke();
        // Los puntos
        for (const k of track.keys || []) {
            const [px, py] = this.graphPoint(k.t, k.v, lo, hi, r);
            drawDiamond(ctx, px, py, 5.5, k.t === this.selectedKey, accent);
        }
        if (!(track.keys || []).length) {
            ctx.globalAlpha = 0.65;
            ctx.fillStyle = text;
            ctx.textAlign = "center";
            ctx.fillText(tr("Sin puntos clave: pulsa el rombo de un valor para poner uno en el cabezal."), (r.left + r.right) / 2, (r.top + r.bottom) / 2, r.right - r.left - 8);
            ctx.globalAlpha = 1;
        }
    }

    installGraph() {
        const g = this.graph, pos = (e) => {
            const b = g.getBoundingClientRect();
            return [e.clientX - b.left, e.clientY - b.top];
        };
        g.addEventListener("contextmenu", (e) => e.preventDefault());
        g.addEventListener("pointerdown", (e) => {
            const i = this.selected();
            if (i < 0)
                return;
            const [x, y] = pos(e), k = this.graphKeyAt(x, y), prop = this.graphProp.value;
            if (k < 0) {
                // Clic en la gráfica: el cabezal va a ese momento.
                const r = this.graphPlot();
                if (e.button === 0 && r.right > r.left)
                    this.seekLocal(this.delay(i) * clamp((x - r.left) / (r.right - r.left), 0, 1));
                return;
            }
            const key = Motion.track(this.motion(i), prop).keys[k];
            this.selectKeyTime(key.t);
            if (e.button === 2) {
                const change = (fn, text) => {
                    const clip = Motion.cloneClip(this.motion(i)), track = Motion.editTrack(clip, prop);
                    if (k < track.keys.length)
                        fn(track, track.keys[k]);
                    this.setMotion(i, clip, text);
                };
                popupMenu([
                    ...interpEntries(key, (kind) => change((_, kk) => { if (kind) kk.i = kind; else delete kk.i; }, tr("Forma de la curva")),
                                     (which, on) => change((_, kk) => { if (on) kk[which] = true; else delete kk[which]; }, tr("Forma de la curva"))),
                    "-",
                    { text: tr("Quitar el punto"), icon: "edit-delete", run: () => change((track) => {
                        if (track.keys.length === 1)
                            track.base = track.keys[k].v;
                        track.keys.splice(k, 1);
                    }, tr("Quitar punto clave")) },
                ], e.clientX, e.clientY);
                return;
            }
            if (e.button !== 0)
                return;
            const [lo, hi] = this.graphRange(this.motion(i), prop);
            this.graphDrag = { id: e.pointerId, prop, index: k, t: key.t, v: key.v, lo, hi };
            g.setPointerCapture(e.pointerId);
        });
        g.addEventListener("pointermove", (e) => {
            const d = this.graphDrag;
            if (!d || e.pointerId !== d.id)
                return;
            const [x, y] = pos(e), r = this.graphPlot(), duration = Math.max(1, this.delay(this.selected()));
            // De 10 en 10 ms y con la escala fija mientras se arrastra (si no, la gráfica se movía sola).
            d.t = clamp(Math.round((duration * (x - r.left)) / (r.right - r.left) / 10) * 10, 0, duration);
            d.v = d.lo + ((d.hi - d.lo) * (r.bottom - y)) / (r.bottom - r.top);
            if (d.prop === "opacity")
                d.v = clamp(d.v, 0, 100);
            d.v = Math.round(d.v * 10) / 10;
            this.drawGraph();
        });
        const end = (e) => {
            const d = this.graphDrag;
            if (!d || e.pointerId !== d.id)
                return;
            const clip = e.type === "pointerup" ? this.graphClip() : null;
            this.graphDrag = null;
            if (!clip) {
                this.drawGraph();
                return;
            }
            this.selectedKey = d.t;
            this.setMotion(this.selected(), clip, tr("Mover punto clave"));
            this.seekLocal(d.t);
        };
        g.addEventListener("pointerup", end);
        g.addEventListener("pointercancel", end);
    }

    // Con el diálogo de Kaint (en Kaint.APK, el prompt() del navegador no sale).
    async editDelay(i) {
        const value = await promptNumber(tr("Duración del fotograma"), tr("Duración del fotograma %1 (ms):", i + 1), this.delay(i), MinDelay, 60000);
        if (value !== null && value > 0)
            this.setDelay(i, value);
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
            // Con el ratón, arrastrar y soltar del navegador (también desde las carpetas); con el dedo,
            // el de mediaPointerDown (el del navegador no funciona bien en Android).
            const item = el("div", { className: "gif-media", draggable: !matchMedia("(pointer: coarse)").matches, title: label, dataset: { index: i } },
                            [thumbnail(source, 84, 64, true, true), el("span", { textContent: label })]);
            item.addEventListener("click", (e) => {
                if (!e.ctrlKey && !e.metaKey && !e.shiftKey)
                    for (const other of this.mediaList.querySelectorAll(".gif-media.selected"))
                        other.classList.remove("selected");
                item.classList.toggle("selected");
            });
            item.addEventListener("dblclick", () => this.insertMedia(i, -1));
            item.addEventListener("pointerdown", (e) => this.mediaPointerDown(e, item, i));
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

    // Con el dedo o el lápiz: deslizar desplaza la biblioteca; mantener pulsado un medio lo levanta
    // y se lleva a la línea de tiempo (sale una miniatura bajo el dedo y la marca de dónde caerá).
    mediaPointerDown(e, item, index) {
        if (e.pointerType === "mouse")
            return;
        const list = this.mediaList;
        const d = { id: e.pointerId, startX: e.clientX, startY: e.clientY, left: list.scrollLeft, top: list.scrollTop, kind: "press" };
        item.setPointerCapture(e.pointerId);
        d.timer = setTimeout(() => {
            if (d.kind !== "press")
                return;
            d.kind = "drag";
            navigator.vibrate?.(15);
            const thumb = item.querySelector("canvas");
            d.ghost = el("div", { className: "gif-ghost" }, [thumb ? thumbnail(thumb, 64, 48) : el("span", { textContent: "+" })]);
            document.body.append(d.ghost);
            move(e);
        }, 350);
        const overLane = (ev) => {
            const r = this.scroller.getBoundingClientRect();
            return ev.clientX >= r.left && ev.clientX <= r.right && ev.clientY >= r.top - 20 && ev.clientY <= r.bottom + 20;
        };
        const move = (ev) => {
            if (ev.pointerId !== d.id)
                return;
            if (d.kind === "press" && Math.hypot(ev.clientX - d.startX, ev.clientY - d.startY) > 8) {
                clearTimeout(d.timer);
                d.kind = "pan";
            }
            if (d.kind === "pan") {
                list.scrollLeft = d.left - (ev.clientX - d.startX);
                list.scrollTop = d.top - (ev.clientY - d.startY);
            } else if (d.kind === "drag") {
                d.ghost.style.left = `${ev.clientX}px`;
                d.ghost.style.top = `${ev.clientY}px`;
                if (overLane(ev)) {
                    this.showDropMark(this.insertionAt(this.laneX(ev)));
                    this.autoScroll(ev.clientX);
                } else {
                    this.dropMark.hidden = true;
                }
            }
        };
        const up = (ev) => {
            if (ev.pointerId !== d.id)
                return;
            clearTimeout(d.timer);
            item.removeEventListener("pointermove", move);
            item.removeEventListener("pointerup", up);
            item.removeEventListener("pointercancel", up);
            d.ghost?.remove();
            this.dropMark.hidden = true;
            if (d.kind === "press" && ev.type === "pointerup") {
                // Un toque: lo elige (para «Añadir»).
                for (const other of list.querySelectorAll(".gif-media.selected"))
                    other.classList.remove("selected");
                item.classList.add("selected");
            } else if (d.kind === "drag" && ev.type === "pointerup" && overLane(ev)) {
                this.insertMedia(index, this.insertionAt(this.laneX(ev)));
            }
        };
        item.addEventListener("pointermove", move);
        item.addEventListener("pointerup", up);
        item.addEventListener("pointercancel", up);
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

    frameMenu(i, clientX, clientY) {
        const item = (id, text, iconName, run, enabled = null, shortcut = null) => new Action({ id, text, icon: iconName, run, enabled, shortcut });
        const chosen = this.isFrameSelected(i) ? this.selectedFrames() : [i];
        popupMenu([
            item("gif-delay", tr("Duración..."), null, () => this.editDelay(i)),
            item("gif-dup", tr("Duplicar"), "layer-duplicate", () => this.duplicateFrames(chosen), null, "Ctrl+D"),
            item("gif-hide", this.hidden(i) ? tr("Mostrar") : tr("Ocultar"), "layer-visible-on", () => this.toggleHidden(i)),
            item("gif-edit", tr("Editar con las herramientas"), null, () => this.editFrame(i)),
            "-",
            item("gif-cut", tr("Cortar"), "edit-cut", () => this.cutFrames(), () => chosen.length < this.count(), "Ctrl+X"),
            item("gif-copy", tr("Copiar"), "edit-copy", () => this.copyFrames(), null, "Ctrl+C"),
            item("gif-paste", tr("Pegar"), "edit-paste", () => this.pasteFrames(), null, "Ctrl+V"),
            item("gif-all", tr("Elegir todos"), "edit-select-all", () => this.selectAllFrames(), null, "Ctrl+A"),
            "-",
            item("gif-bake", tr("Convertir la animación en fotogramas"), null, () => this.bakeMotion(i), () => !Motion.isEmpty(this.motion(i))),
            "-",
            item("gif-del", tr("Eliminar"), "edit-delete", () => this.removeFrames(chosen), () => this.count() > chosen.length),
        ], clientX, clientY);
    }

    // Al llevar un clip o un medio cerca de un borde de la línea de tiempo, se desplaza sola.
    autoScroll(clientX) {
        const r = this.scroller.getBoundingClientRect(), margin = 40;
        if (clientX < r.left + margin)
            this.scroller.scrollLeft -= Math.ceil((r.left + margin - clientX) / 4);
        else if (clientX > r.right - margin)
            this.scroller.scrollLeft += Math.ceil((clientX - (r.right - margin)) / 4);
    }

    installTimeline() {
        const lane = this.lane;
        let drag = null;
        // Con el dedo (o el lápiz), como en los editores de vídeo del móvil: deslizar desplaza la
        // línea de tiempo, mantener pulsado un clip lo levanta para moverlo (o, sin moverlo, abre su
        // menú) y su borde derecho, más ancho, lo estira. El navegador no se queda el gesto
        // (touch-action: none): antes cancelaba el arrastre a medias y todo funcionaba raro.
        lane.addEventListener("pointerdown", (e) => {
            if (!this.doc || e.button > 2 || drag)
                return;
            const x = this.laneX(e), clip = e.target.closest(".gif-clip"), touch = e.pointerType !== "mouse";
            if (touch)
                this.lastTouch = performance.now();
            lane.setPointerCapture(e.pointerId);
            const key = e.target.closest(".gif-key");
            if (key) {
                const t = +key.dataset.t;
                this.selectKeyTime(t);
                if (e.button === 0) {
                    drag = { kind: "key", id: e.pointerId, from: t, to: t, startX: e.clientX };
                    // Con el dedo, mantenerlo pulsado abre su menú.
                    if (touch)
                        drag.timer = setTimeout(() => {
                            if (drag?.kind === "key" && !drag.moved) {
                                drag.kind = "keyhold";
                                navigator.vibrate?.(15);
                            }
                        }, 450);
                }
                return;
            }
            if (e.target === this.keyLane || e.target.closest(".gif-keylane")) {
                if (e.button === 0) {
                    drag = { kind: "scrub", id: e.pointerId };
                    this.seek(this.msAt(x));
                }
                return;
            }
            if (!clip || e.target === this.ruler) {
                if (touch && e.target !== this.ruler) {
                    drag = { kind: "pan", id: e.pointerId, startX: e.clientX, scroll: this.scroller.scrollLeft };
                } else if (e.button === 0) {
                    drag = { kind: "scrub", id: e.pointerId };
                    this.seek(this.msAt(x));
                }
                return;
            }
            const i = +clip.dataset.index;
            const r = clip.getBoundingClientRect();
            const edge = e.target.classList.contains("gif-clip-edge") || (touch && e.clientX >= r.right - 18);
            if (edge) {
                this.select(i);
                drag = { kind: "resize", id: e.pointerId, index: i, start: this.starts()[i], delay: this.delay(i) };
                return;
            }
            if (!touch) {
                // Ctrl+clic añade o quita de la selección; Mayús+clic elige desde el elegido hasta aquí.
                if (e.button === 0 && (e.ctrlKey || e.metaKey)) {
                    this.toggleFrameSelection(i);
                    return;
                }
                if (e.button === 0 && e.shiftKey) {
                    this.selectFrameRange(i);
                    return;
                }
                // El clic derecho sobre uno de los elegidos conserva la selección (el menú es para todos).
                if (!(e.button === 2 && this.isFrameSelected(i)))
                    this.select(i);
                if (e.button === 0)
                    drag = { kind: "press", id: e.pointerId, index: i, x };
                return;
            }
            drag = { kind: "touch", id: e.pointerId, index: i, x, startX: e.clientX, scroll: this.scroller.scrollLeft, clientY: e.clientY };
            drag.timer = setTimeout(() => {
                if (drag?.kind !== "touch")
                    return;
                drag.kind = "hold";
                this.select(i);
                navigator.vibrate?.(15);
                this.lane.querySelector(`.gif-clip[data-index="${i}"]`)?.classList.add("lifted");
            }, 380);
        });
        lane.addEventListener("pointermove", (e) => {
            if (!drag || e.pointerId !== drag.id)
                return;
            const x = this.laneX(e);
            if (drag.kind === "key" || drag.kind === "keyhold") {
                // Más lejos un rombo del otro, más dura el movimiento entre ellos (de 10 en 10 ms). Más
                // allá del final, el fotograma dura más.
                if (Math.abs(e.clientX - drag.startX) > 3) {
                    drag.moved = true;
                    drag.kind = "key";
                    clearTimeout(drag.timer);
                }
                const sel = this.selected(), s = this.starts();
                if (drag.moved && sel >= 0) {
                    drag.to = Math.max(0, Math.round((this.msAt(x) - s[sel]) / 10) * 10);
                    this.keyDrag = { from: drag.from, to: drag.to };
                    this.keyLane.title = `${drag.to} ms`;
                    this.renderKeyLane();
                    this.autoScroll(e.clientX);
                }
                return;
            }
            if (drag.kind === "touch" && Math.abs(e.clientX - drag.startX) > 8) {
                clearTimeout(drag.timer);
                drag.kind = "pan";
            }
            if (drag.kind === "pan") {
                this.scroller.scrollLeft = drag.scroll - (e.clientX - drag.startX);
            } else if (drag.kind === "scrub") {
                this.seek(this.msAt(x));
            } else if (drag.kind === "resize") {
                // De 10 en 10 ms, como la duración de los GIF (centésimas de segundo).
                drag.delay = Math.max(MinDelay, Math.round((this.msAt(x) - drag.start) / 10) * 10);
                this.renderTimeline(drag);
            } else if ((drag.kind === "press" && Math.abs(x - drag.x) > 4) || (drag.kind === "hold" && Math.abs(x - drag.x) > 6)) {
                drag.kind = "reorder";
            }
            if (drag.kind === "reorder") {
                drag.to = this.insertionAt(x);
                this.showDropMark(drag.to);
                this.autoScroll(e.clientX);
            }
        });
        const end = (e) => {
            if (!drag || (e && e.pointerId !== drag.id))
                return;
            const d = drag;
            drag = null;
            clearTimeout(d.timer);
            this.dropMark.hidden = true;
            this.keyDrag = null;
            if (e?.type === "pointercancel") {
                this.renderTimeline();
                return;
            }
            if (d.kind === "key") {
                if (d.moved && d.to !== d.from)
                    this.moveKeyTime(d.from, d.to);
                else
                    this.renderKeyLane();
                return;
            }
            if (d.kind === "keyhold") {
                const swallow = (ev) => {
                    ev.preventDefault();
                    ev.stopPropagation();
                };
                document.addEventListener("click", swallow, true);
                setTimeout(() => document.removeEventListener("click", swallow, true), 450);
                this.keyMenu(d.from, e.clientX, e.clientY);
                return;
            }
            if (d.kind === "touch")
                this.select(d.index);
            else if (d.kind === "hold") {
                // El clic que sigue a soltar el dedo caería en una opción del menú recién abierto
                // (salía del editor con «Editar con las herramientas»): se descarta.
                const swallow = (ev) => {
                    ev.preventDefault();
                    ev.stopPropagation();
                };
                document.addEventListener("click", swallow, true);
                setTimeout(() => document.removeEventListener("click", swallow, true), 450);
                this.frameMenu(d.index, e.clientX, e.clientY);
            }
            else if (d.kind === "resize")
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
            const key = e.target.closest(".gif-key");
            if (key) {
                e.preventDefault();
                if (e.pointerType !== "touch" && performance.now() - (this.lastTouch || -1e9) >= 1500)
                    this.keyMenu(+key.dataset.t, e.clientX, e.clientY);
                return;
            }
            const clip = e.target.closest(".gif-clip");
            // Con el dedo, el menú lo abre la pulsación larga (arriba) al soltar.
            if (!clip || e.pointerType === "touch" || performance.now() - (this.lastTouch || -1e9) < 1500) {
                e.preventDefault();
                return;
            }
            e.preventDefault();
            this.frameMenu(+clip.dataset.index, e.clientX, e.clientY);
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
        // Teclado: espacio reproduce, las flechas pasan de fotograma, Supr quita el punto clave elegido
        // (o los fotogramas elegidos) y Ctrl+C, X, V, D y A copian, cortan, pegan, duplican y eligen
        // todos los fotogramas (no el lienzo oculto del editor de imágenes).
        document.addEventListener("keydown", (e) => {
            if (this.root.hidden || !this.doc || e.target.matches?.("input, select, textarea") || document.querySelector(".dialog-backdrop, dialog[open]"))
                return;
            if ((e.ctrlKey || e.metaKey) && !e.altKey && !e.shiftKey) {
                const k = e.key.toLowerCase();
                if (k === "v") {
                    // Llega el evento «paste» con lo que haya en el portapapeles (App.installClipboard
                    // lo trae a pasteFrames).
                    e.stopImmediatePropagation();
                    return;
                }
                const ctrl = { c: () => this.copyFrames(), x: () => this.cutFrames(), d: () => this.duplicateFrames(this.selectedFrames()),
                               a: () => this.selectAllFrames() }[k];
                if (ctrl) {
                    e.preventDefault();
                    e.stopImmediatePropagation();
                    ctrl();
                }
                return;
            }
            const keys = { " ": () => this.togglePlay(), ArrowLeft: () => this.step(-1), ArrowRight: () => this.step(1), Home: () => this.seek(0),
                           Delete: () => (this.selectedKey >= 0 ? this.removeKeyTime(this.selectedKey) : this.removeFrames(this.selectedFrames())),
                           Escape: () => {
                               this.extra = [];
                               this.selectedKey = -1;
                               this.update();
                           } };
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
            t = { w, h, canvas: layer.canvas, image: thumbnail(layer.canvas, w, h, true, true) };
            this.thumbs.set(layer, t);
        }
        // Un mismo elemento no puede estar en dos clips: se clona el lienzo de la miniatura.
        const copy = createCanvas(t.image.width, t.image.height);
        copy.style.width = t.image.style.width;
        copy.style.height = t.image.style.height;
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
            const chosen = this.playing ? i === this.playFrame : this.isFrameSelected(i);
            const clip = el("div", { className: `gif-clip${chosen ? " selected" : ""}${i === selected ? " current" : ""}${layers[i].visible ? "" : " hidden-frame"}`,
                                     dataset: { index: i } });
            clip.style.left = `${x0}px`;
            clip.style.width = `${w}px`;
            if (w > 14) {
                const img = this.thumbFor(layers[i], Math.max(8, w - 6), 50);
                img.classList.add("gif-clip-thumb");
                clip.append(img);
            }
            const mark = Motion.isEmpty(layers[i].motion) ? "" : "◆ ";
            const text = w > 70 ? `${mark}${i + 1} · ${s[i + 1] - s[i]} ms` : w > 26 ? String(i + 1) : "";
            clip.append(el("span", { className: "gif-clip-label", textContent: text }), el("div", { className: "gif-clip-edge" }));
            clips.push(clip);
        }
        this.track.replaceChildren(...clips);
        this.renderKeyLane(s);
        this.empty.hidden = layers.length > 0;
        this.playhead.style.left = `${this.x(this.time) - 1}px`;
        if (this.playing) {
            const px = this.x(this.time), view = this.scroller;
            if (px < view.scrollLeft + 40 || px > view.scrollLeft + view.clientWidth - 60)
                view.scrollLeft = Math.max(0, px - 60);
        }
    }

    // Los rombos del fotograma elegido ('s': los comienzos, si ya se calcularon).
    renderKeyLane(s = this.starts()) {
        const i = this.selected(), children = [];
        if (this.doc && i >= 0 && i + 1 < s.length) {
            const band = el("div", { className: "gif-keylane-band" });
            band.style.left = `${this.x(s[i])}px`;
            band.style.width = `${Math.max(4, this.x(s[i + 1]) - this.x(s[i]) - 2)}px`;
            children.push(band);
            for (const t of Motion.keyTimes(this.motion(i))) {
                const dragging = this.keyDrag?.from === t;
                const key = el("div", { className: `gif-key${t === this.selectedKey || dragging ? " selected" : ""}`, dataset: { t } });
                key.style.left = `${this.x(s[i] + (dragging ? this.keyDrag.to : t))}px`;
                children.push(key);
            }
        }
        this.keyLane.replaceChildren(...children);
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
        const index = this.shownFrame(), layer = this.doc?.layers[index];
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
        this.previewGeom = { x, y, scale };
        // Con su opacidad y su animación en el cabezal, como en el GIF exportado.
        const t = this.frameTransform(index), cw = layer.canvas.width, ch = layer.canvas.height;
        ctx.save();
        ctx.beginPath();
        ctx.rect(x, y, dw, dh);
        ctx.clip();
        ctx.imageSmoothingEnabled = scale < 1 || !Motion.isIdentity(t);
        ctx.imageSmoothingQuality = "high";
        ctx.globalAlpha = (layer.opacity / 255) * (t.opacity / 100);
        ctx.translate(x, y);
        ctx.scale(dw / cw, dh / ch);
        ctx.translate(cw / 2 + t.x, ch / 2 + t.y);
        ctx.rotate((t.rotation * Math.PI) / 180);
        ctx.scale(t.scaleX / 100, t.scaleY / 100);
        ctx.drawImage(layer.canvas, -cw / 2, -ch / 2);
        ctx.restore();
        if (index === this.selected())
            this.drawMotionPath(ctx, index, x, y, dw / cw);
    }

    // La trayectoria del movimiento (punteada) con sus puntos clave, como «Path Curve» de Filmora.
    drawMotionPath(ctx, index, x0, y0, scale) {
        const clip = this.motion(index);
        if (!clip || ((clip.tracks?.x?.keys?.length || 0) < 2 && (clip.tracks?.y?.keys?.length || 0) < 2))
            return;
        const cx = this.doc.width / 2, cy = this.doc.height / 2, duration = this.delay(index);
        const point = (t) => {
            const m = Motion.sample(clip, t);
            return [x0 + (cx + m.x) * scale, y0 + (cy + m.y) * scale];
        };
        ctx.save();
        ctx.lineWidth = 1.5;
        for (const [color, dash] of [["rgba(0, 0, 0, 0.6)", []], ["#ffffff", [5, 4]]]) {
            ctx.strokeStyle = color;
            ctx.setLineDash(dash);
            ctx.beginPath();
            for (let t = 0; t <= duration; t += 10) {
                const [px, py] = point(t);
                if (t === 0)
                    ctx.moveTo(px, py);
                else
                    ctx.lineTo(px, py);
            }
            ctx.stroke();
        }
        ctx.setLineDash([]);
        const accent = getComputedStyle(this.root).getPropertyValue("--accent").trim() || "#2e8def";
        for (const t of Motion.keyTimes(clip)) {
            const [px, py] = point(t);
            drawDiamond(ctx, px, py, 5, t === this.selectedKey, accent);
        }
        ctx.restore();
    }

    // 'full' en falso (mientras se reproduce): solo la vista previa, el cabezal, el tiempo y el clip
    // elegido, sin rehacer la línea de tiempo en cada fotograma.
    update(full = true) {
        if (this.root.hidden)
            return;
        const has = this.count() > 0, i = this.selected();
        if (!full) {
            const s = this.starts(), shown = this.shownFrame();
            this.timeLabel.textContent = `${timeText(this.time)} / ${timeText(s[s.length - 1])}` + (shown >= 0 ? `   ${tr("Fotograma %1 de %2", shown + 1, this.count())}` : "");
            this.drawPreview();
            const chosen = this.shownFrame();
            for (const clip of this.track.children)
                clip.classList.toggle("selected", this.playing ? +clip.dataset.index === chosen : this.isFrameSelected(+clip.dataset.index));
            if (!this.playing)
                this.renderKeyLane(s);
            if (chosen === i)
                this.refreshInspectorValues();
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
        this.refreshInspector();
        clearTimeout(this.sizeTimer);
        this.sizeTimer = setTimeout(() => this.estimateSize(), 600);
    }

    // --- Peso del GIF y exportar en otros formatos -------------------------------------------

    // El peso aproximado del GIF exportado (unos fotogramas de muestra codificados) y lo que dura,
    // con la barrita que se llena hacia los 10 MB (el límite de Discord sin Nitro).
    estimateSize() {
        const doc = this.doc;
        if (!doc || this.root.hidden)
            return;
        const key = `${doc.history.index}:${doc.width}x${doc.height}:${doc.layers.map((l) => `${l.name}${l.visible ? 1 : 0}`).join("|")}`;
        if (key === this.sizeKey)
            return;
        this.sizeKey = key;
        const shown = doc.layers.filter((l) => l.visible);
        let frames = 0, duration = 0;
        for (const l of shown) {
            const d = delayOf(l), step = Math.max(10, l.motion?.step || 50);
            duration += d;
            frames += Motion.isAnimated(l.motion) ? Math.max(1, Math.ceil(d / step)) : 1;
        }
        let bytes = 0;
        const samples = Math.min(4, shown.length);
        if (samples) {
            // Las grandes, reducidas (el peso crece con el área).
            const factor = Math.max(1, (doc.width * doc.height) / 200000);
            const w = Math.max(1, Math.round(doc.width / Math.sqrt(factor))), h = Math.max(1, Math.round(doc.height / Math.sqrt(factor)));
            let sampled = 0;
            for (let i = 0; i < samples; ++i) {
                const layer = shown[Math.floor((i * shown.length) / samples)];
                const c = createCanvas(w, h), g = context(c);
                g.drawImage(layer.canvas, 0, 0, w, h);
                const data = g.getImageData(0, 0, w, h).data;
                sampled += encodeAnimation([{ width: w, height: h, data, delay: 100 }], defaultSaveOptions()).length * factor;
            }
            bytes = 800 + (sampled / samples) * frames;
        }
        const limit = 10 * 1024 * 1024, mb = bytes / (1024 * 1024);
        const lang = currentLanguage() === "es" || currentLanguage() === "pt-BR" ? currentLanguage() : "en";
        const fmt = (v) => v.toLocaleString(lang, { minimumFractionDigits: 1, maximumFractionDigits: 1 });
        this.sizeFill.style.width = `${Math.min(100, (bytes / limit) * 100)}%`;
        this.sizeMeter.classList.toggle("warn", bytes >= limit * 0.6 && bytes < limit);
        this.sizeMeter.classList.toggle("over", bytes >= limit);
        this.sizeText.textContent = `${bytes >= limit ? "⚠" : "≈"} ${fmt(mb)} MB · ${fmt(duration / 1000)} s`;
        let tip = tr("Peso aproximado del GIF exportado: %1 MB, %2 fotogramas, %3 s.", fmt(mb), frames, fmt(duration / 1000));
        if (bytes >= limit)
            tip += `\n${tr("Pesa mucho: Discord no deja enviar más de 10 MB sin Nitro y tarda en cargar. Acorta la animación, reduce el tamaño de la imagen, usa menos fotogramas por segundo o expórtala como vídeo MP4.")}`;
        else if (bytes >= limit * 0.6)
            tip += `\n${tr("Se acerca a los 10 MB (el límite de Discord sin Nitro).")}`;
        this.sizeMeter.title = tip;
        this.sizeBytes = bytes;
    }

    // Exportar como PNG animado, WebP animado o vídeo MP4 (GifExport de Kaint): las opciones y el archivo.
    async exportAs(kind) {
        const doc = this.doc;
        if (!doc)
            return;
        this.stop();
        const frames = animationFrames(doc, 100, false).filter((f) => !f.hidden);
        if (!frames.length)
            return;
        if (kind === "webp" && !(await canEncodeWebp())) {
            await message(tr("Exportar"), tr("Este navegador no sabe comprimir WebP."), "dialog-warning");
            return;
        }
        const total = frames.reduce((a, f) => a + Math.max(1, f.delay || 100), 0);
        const names = { apng: tr("PNG animado"), webp: tr("WebP animado"), mp4: "MP4" };
        const dlg = new Dialog(tr("Exportar como %1", names[kind]), { iconName: "document-save-as" });
        const form = el("div", { className: "form-grid gif-export-form" });
        const row = (label, control) => form.append(el("label", { textContent: label }), control);
        const repeat = el("input", { type: "number", min: 1, max: 100, value: Math.min(20, Math.max(1, Math.ceil(5000 / total))), style: { width: "70px" } });
        const length = el("span");
        const syncLength = () => (length.textContent = tr("%1 s", ((total * Math.max(1, +repeat.value || 1)) / 1000).toFixed(1)));
        repeat.addEventListener("input", syncLength);
        syncLength();
        const scale = el("select", {}, [1, 2, 3, 4].map((f) => el("option", { value: f, textContent: `${f * 100} % (${doc.width * f} × ${doc.height * f})` })));
        scale.title = tr("Más grande sin suavizar (para pixel art: los vídeos pequeños se ven borrosos).");
        const quality = el("select", {}, [tr("Alta"), tr("Media"), tr("Baja (archivo más pequeño)")].map((t, i) => el("option", { value: i, textContent: t })));
        const webpQuality = el("input", { type: "number", min: 1, max: 100, value: 90, style: { width: "70px" }, title: tr("100: sin pérdida.") });
        const background = el("select", {}, [["#ffffff", tr("Blanco")], ["#000000", tr("Negro")]].map(([v, t]) => el("option", { value: v, textContent: t })));
        background.title = tr("El MP4 no tiene transparencia: lo transparente va sobre este color (blanco o negro).");
        if (kind === "mp4") {
            row(tr("Repetir la animación:"), el("span", {}, [repeat, ` ${tr("veces")} · `, length]));
            row(tr("Calidad:"), quality);
            row(tr("Fondo:"), background);
        }
        if (kind === "webp")
            row(tr("Calidad:"), webpQuality);
        row(tr("Tamaño:"), scale);
        dlg.body.append(form, el("p", { className: "note", textContent: kind === "apng" ? tr("Sin pérdida y con transparencia de 8 bits; lo abren los navegadores y Discord.")
            : kind === "webp" ? tr("Con transparencia y mucho más ligero que el GIF.") : tr("H.264: se ve en cualquier teléfono, navegador y red social.") }));
        dlg.addButton(tr("Exportar"), "ok", { isDefault: true });
        dlg.addButton(tr("Cancelar"), null);
        if ((await dlg.exec()) !== "ok")
            return;
        const base = doc.displayName().replace(/\.[^.]+$/, "");
        const title = this.title.textContent;
        const progress = (f) => (this.title.textContent = tr("Exportando… %1 %", Math.round(f * 100)));
        document.body.classList.add("busy");
        try {
            let blob, ext = kind === "apng" ? "png" : kind;
            const f = +scale.value;
            if (kind === "apng")
                blob = new Blob([await encodeApng(frames, { loops: doc.gifLoops ?? 0, scale: f })], { type: "image/apng" });
            else if (kind === "webp")
                blob = new Blob([await encodeAnimatedWebp(frames, { quality: +webpQuality.value, loops: doc.gifLoops ?? 0, scale: f })], { type: "image/webp" });
            else if (canEncodeMp4())
                blob = new Blob([await encodeMp4(frames, { repeat: +repeat.value || 1, scale: f, quality: +quality.value, background: background.value, onProgress: progress })],
                                { type: "video/mp4" });
            else {
                const r = await recordVideo(frames, { repeat: +repeat.value || 1, scale: f, background: background.value, onProgress: progress });
                blob = r.blob;
                ext = r.ext;
            }
            this.lastExport = blob;
            download(blob, `${base}.${ext}`);
        } catch (e) {
            await message(tr("Exportar"), tr("No se pudo exportar:\n%1", String(e?.message || e)), "dialog-warning");
        } finally {
            document.body.classList.remove("busy");
            this.title.textContent = title;
        }
    }
}
