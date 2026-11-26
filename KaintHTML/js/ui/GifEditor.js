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
import { canEncodeAac, canEncodeMp4, canEncodeWebp, encodeAnimatedWebp, encodeApng, encodeMp4, recordVideo } from "../core/AnimExport.js";
import { AudioExtensions, PeakMs, analyzeAudio, automate, copyClip, decodeClip, isAudioFile, keyedVolume, renderMix, setKey, volumeAt } from "../core/Audio.js";
import { Settings } from "../Settings.js";
import { defaultSaveOptions, encodeAnimation } from "../core/Encode.js";
import * as Motion from "../core/Motion.js";
import { checkerPattern, context, createCanvas, imageSignature, thumbnail } from "../core/Raster.js";
import { currentLanguage, tr, trn } from "../i18n.js";
import { Action } from "./Actions.js";
import { Dialog, el, message, promptNumber } from "./Dialogs.js";
import { icon } from "./Icons.js";
import { popupMenu } from "./Menu.js";
import { historyPanel } from "./Panels.js";

const DefaultDelay = 100, MinDelay = 20, MediaMime = "application/x-kaint-gif-media";
const RulerHeight = 22, LeftMargin = 12;
// Las pistas: desde arriba de la primera (bajo la regla), cada una con sus clips y aire entre ellas.
const TrackTop = 28, TrackGap = 8, Snap = 14;
// La altura de las pistas (se cambia arrastrando el borde de abajo de su cabecera o con su menú).
const TrackHeights = { small: 48, medium: 74, large: 118 };
// Las pistas de audio, debajo de las de los fotogramas (con la franja «AUDIO» entre unas y otras).
const AudioHeight = 54, AudioGap = 24;

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
            const track = this.curTrack();
            let at = this.selected() >= 0 && this.trackOf(this.selected()) === track ? this.selected() + 1 : -1;
            for (const item of this.mediaList.querySelectorAll(".gif-media.selected")) {
                const before = this.count();
                this.insertMedia(+item.dataset.index, at, track);
                if (at >= 0)
                    at += this.count() - before;
            }
        });
        // Medios e historial (solo los pasos del editor de GIF), en pestañas.
        const mediaPage = el("div", { className: "gif-bin-page" }, [this.mediaList, el("div", { className: "gif-row" }, [importButton, addButton, this.fileInput])]);
        const historyPage = el("div", { className: "gif-bin-page", hidden: true }, [historyPanel(this.app, "gif")]);
        const tab = (text, page) => {
            const b = el("button", { className: "gif-tab", textContent: text });
            b.addEventListener("click", () => {
                for (const t of tabs.children)
                    t.classList.toggle("selected", t === b);
                mediaPage.hidden = page !== mediaPage;
                historyPage.hidden = page !== historyPage;
            });
            return b;
        };
        const tabs = el("div", { className: "gif-tabs" }, [tab(tr("Medios"), mediaPage), tab(tr("Historial"), historyPage)]);
        tabs.firstChild.classList.add("selected");
        const bin = el("div", { className: "gif-bin" }, [tabs, mediaPage, historyPage]);
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
            button("⏭", tr("Último fotograma (Fin)"), () => { const f = this.trackFrames(this.curTrack()); if (f.length) this.seek(this.times().start[f[f.length - 1]]); }),
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
        const globalKey = frameButton(tr("Punto clave"), tr("Punto clave global: todos los valores (posición, escala, rotación y opacidad) en el cabezal; si ya hay uno ahí, lo quita (K)"),
                                      () => this.toggleGlobalKey());
        globalKey.prepend(el("span", { className: "gif-diamond filled" }));
        const all = frameButton(tr("A todos"), tr("La misma duración para todos los fotogramas"), () => this.setAllDelays(+this.delayInput.value));
        this.frameControls.push(this.delayInput);
        this.zoom = el("input", { type: "range", min: 40, max: 3000, value: this.pps, className: "gif-zoom",
                                  title: tr("Zum de la línea de tiempo (también con Ctrl y la rueda)"),
                                  oninput: () => { this.pps = +this.zoom.value; this.renderTimeline(); } });
        const bar = el("div", { className: "gif-row gif-toolbar" }, [
            undo, redo, el("span", { className: "gif-gap" }), dup, del, hide, edit, el("span", { className: "gif-gap" }), globalKey,
            el("span", { className: "gif-gap" }),
            el("label", { textContent: tr("Duración:") }), this.delayInput, el("span", { textContent: tr(" ms") }), all,
            el("span", { className: "gif-gap" }),
            button(tr("Invertir"), tr("El último fotograma de la pista elegida pasa a ser el primero"), () => this.reverse()),
            button(tr("Ida y vuelta"), tr("Añade los fotogramas de la pista elegida al revés al final: la animación va y vuelve"), () => this.boomerang()),
            button(tr("Pista"), tr("Añade una pista encima de las demás (lo de las pistas de arriba se ve encima)"), () => this.addTrack()),
            this.audioButton(),
            el("span", { className: "gif-stretch" }), el("label", { textContent: tr("Zum:") }), this.zoom,
        ]);
        this.ruler = el("canvas", { className: "gif-ruler" });
        this.track = el("div", { className: "gif-track" });
        this.playhead = el("div", { className: "gif-playhead" });
        this.dropMark = el("div", { className: "gif-drop-mark", hidden: true });
        this.empty = el("div", { className: "gif-empty", textContent: tr("Arrastra aquí imágenes o GIF (de la biblioteca o de tus carpetas)") });
        // Los puntos clave del fotograma elegido: se arrastran (más separados, más dura el movimiento
        // entre ellos) y el clic derecho elige la forma del movimiento.
        this.keyLane = el("div", { className: "gif-keylane", title: tr("Puntos clave del fotograma elegido (sobre su imagen): arrástralos para cambiar cuándo llegan; clic derecho para la forma del movimiento") });
        this.audioArea = el("div", { className: "gif-audio-area" });
        this.lane = el("div", { className: "gif-lane" }, [this.ruler, this.track, this.audioArea, this.keyLane, this.dropMark, this.playhead, this.empty]);
        this.scroller = el("div", { className: "gif-scroller" }, [this.lane]);
        // A la izquierda, fija al desplazarse a lo largo: la cabecera de las pistas.
        this.trackHeaders = el("div", { className: "gif-track-headers" });
        this.timelineBody = el("div", { className: "gif-timeline-body" }, [this.trackHeaders, this.scroller]);
        // La regla se queda arriba a la vista al bajar entre muchas pistas.
        this.timelineBody.addEventListener("scroll", () => { this.ruler.style.top = `${this.timelineBody.scrollTop}px`; });
        const timeline = el("div", { className: "gif-timeline" }, [bar, this.timelineBody]);

        const inspector = this.buildInspector();
        inspector.append(this.buildAudioPanel());
        root.append(head, el("div", { className: "gif-top" }, [bin, player, inspector]), timeline);
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
        this.selectedAudio = -1;
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
                const i = this.selected();
                if (i >= 0)
                    this.activeTrack = this.trackOf(i);
                if (!this.playing && i >= 0 && this.frameAt(this.time) !== i)
                    this.time = this.times().start[i];
                this.update(!this.playing);
            };
            doc.addEventListener("active", active);
            this.listeners.push(["active", active]);
        }
        // El cabezal, al principio del fotograma elegido (la capa activa).
        if (doc && this.selected() >= 0) {
            this.activeTrack = this.trackOf(this.selected());
            this.time = this.times().start[this.selected()];
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
            // La primera vez: para qué está pensado.
            if (!Settings.get("gifIntroShown")) {
                Settings.set("gifIntroShown", true);
                setTimeout(() => message(tr("Editor de GIF"), `${tr("El editor de GIF está pensado para hacer GIF animados.")}\n\n${tr("También exporta vídeo (MP4 o WebM) y admite pistas de audio para esos vídeos, pero no es un editor de vídeo completo: para vídeos largos o con mucha edición, usa un programa de edición de vídeo.")}`, "dialog-information"), 0);
            }
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

    // --- Pistas (como en Kaint y en Filmora) ---------------------------------------------------
    // Cada fotograma va en una pista (layer.track); cada pista empieza en 0 y sus fotogramas van uno
    // detrás de otro. Las de número más alto se ven encima. La elegida recibe lo que se añade.
    trackOf(i) {
        return this.doc?.layers[i]?.track || 0;
    }

    trackCount() {
        let n = Math.max(1, this.doc?.gifTracks || 1);
        for (const l of this.doc?.layers || [])
            n = Math.max(n, (l.track || 0) + 1);
        return n;
    }

    trackFrames(k) {
        const out = [];
        (this.doc?.layers || []).forEach((l, i) => {
            if ((l.track || 0) === k)
                out.push(i);
        });
        return out;
    }

    curTrack() {
        return Math.max(0, Math.min(this.activeTrack || 0, this.trackCount() - 1));
    }

    // Inicio y final (ms) de cada fotograma en su pista y la duración total (la pista más larga).
    times() {
        const start = [], end = [], ends = [];
        for (const layer of this.doc?.layers || []) {
            // (Con el espacio vacío que tenga delante.)
            const k = layer.track || 0, s = (ends[k] || 0) + Math.max(0, layer.gap || 0);
            start.push(s);
            ends[k] = s + delayOf(layer);
            end.push(ends[k]);
        }
        return { start, end, total: ends.reduce((m, v) => Math.max(m, v || 0), 0) };
    }

    // El fotograma de la pista elegida en 'ms' (pasado su final, el último; -1 si está vacía).
    frameAt(ms) {
        const t = this.times(), frames = this.trackFrames(this.curTrack());
        for (const i of frames)
            if (ms < t.end[i])
                return i;
        return frames.length ? frames[frames.length - 1] : -1;
    }

    // El de la pista 'k' en 'ms' (-1 fuera de sus fotogramas).
    frameAtTrack(k, ms) {
        const t = this.times();
        return this.trackFrames(k).find((i) => ms >= t.start[i] && ms < t.end[i]) ?? -1;
    }

    setActiveTrack(k) {
        this.activeTrack = Math.max(0, Math.min(k, this.trackCount() - 1));
        // El fotograma elegido pasa a ser el de esta pista en el cabezal.
        if (this.doc && !this.playing && this.trackOf(this.selected()) !== this.activeTrack) {
            const f = this.frameAt(this.time);
            if (f >= 0) {
                this.extra = [];
                this.selectedKey = -1;
                this.doc.setActiveIndex(f);
                return;
            }
        }
        this.update();
    }

    addTrack() {
        const n = this.trackCount();
        if (!this.doc || n >= 16)
            return;
        // Las vacías se recuerdan en la imagen mientras está abierta.
        this.doc.gifTracks = n + 1;
        this.activeTrack = n;
        this.update();
    }

    removeTrack(k) {
        if (!this.doc || this.trackCount() <= 1)
            return;
        const { layers, props } = this.current(), keepLayers = [], keepProps = [];
        let removed = 0;
        layers.forEach((l, i) => {
            const p = props[i], track = p.track || 0;
            if (track === k) {
                removed++;
                return;
            }
            keepLayers.push(l);
            keepProps.push(track > k ? { ...p, track: track - 1 } : p);
        });
        // Una imagen necesita al menos una capa: la única pista con fotogramas no se quita.
        if (!keepLayers.length)
            return;
        if (removed && !window.confirm(trn("¿Eliminar la pista %1 y sus %n fotograma(s)?", removed, k + 1)))
            return;
        this.doc.gifTracks = Math.max(1, (this.doc.gifTracks || 1) - 1);
        this.activeTrack = Math.max(0, k - 1);
        if (!removed) {
            this.update();
            return;
        }
        this.apply(keepLayers, keepProps, 0, tr("Eliminar la pista %1", k + 1), "edit-delete");
    }

    trackHidden(k) {
        const f = this.trackFrames(k);
        return f.length > 0 && f.every((i) => this.hidden(i));
    }

    toggleTrackHidden(k) {
        const f = this.trackFrames(k);
        if (!f.length)
            return;
        const show = this.trackHidden(k);
        const { layers, props } = this.current();
        for (const i of f)
            props[i] = { ...props[i], visible: show };
        this.apply(layers, props, this.selected(), show ? tr("Mostrar la pista %1", k + 1) : tr("Ocultar la pista %1", k + 1), "layer-visible-on");
    }

    select(i) {
        if (!this.doc || i < 0 || i >= this.doc.layers.length)
            return;
        this.extra = [];
        // Un fotograma elegido: el panel de la derecha vuelve a su animación.
        this.selectedAudio = -1;
        this.activeTrack = this.trackOf(i);
        this.doc.setActiveIndex(i);
        if (!this.playing)
            this.time = this.times().start[i];
        this.update();
    }

    seek(ms) {
        this.time = this.count() ? Math.max(0, Math.min(ms, this.times().total - 1)) : 0;
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
        const i = this.selected(), t = this.times();
        if (i < 0)
            return 0;
        return clamp(this.time - t.start[i], 0, Math.max(0, t.end[i] - t.start[i] - 1));
    }

    seekLocal(ms) {
        const i = this.selected(), t = this.times();
        if (i < 0)
            return;
        this.stop();
        this.time = t.start[i] + clamp(Math.round(ms), 0, Math.max(0, t.end[i] - t.start[i] - 1));
        this.update(false);
    }

    step(delta) {
        this.stop();
        // Por los fotogramas de la pista elegida.
        const frames = this.trackFrames(this.curTrack()), n = frames.length;
        if (n <= 0)
            return;
        let i = frames.indexOf(this.frameAt(this.time)) + delta;
        if (i < 0)
            i = this.loop.checked ? n - 1 : 0;
        if (i >= n)
            i = this.loop.checked ? 0 : n - 1;
        this.seek(this.times().start[frames[i]]);
    }

    togglePlay() {
        if (this.playing) {
            this.stop();
            return;
        }
        if (!this.count())
            return;
        if (this.time >= this.times().total - 1)
            this.time = 0;
        this.playing = true;
        this.playFrame = this.frameAt(this.time);
        this.playButton.replaceChildren(icon("media-playback-pause", 16));
        this.startAudio();
        let last = performance.now();
        const frame = (now) => {
            if (!this.playing)
                return;
            const total = this.times().total;
            if (!this.doc || !this.count() || total <= 0) {
                this.stop();
                return;
            }
            this.time += (now - last) * +this.speed.value;
            last = now;
            if (this.time >= total) {
                if (!this.loop.checked) {
                    this.time = total - 1;
                    this.stop();
                } else {
                    this.time %= Math.max(1, total);
                    // Vuelve a empezar: el audio también.
                    this.startAudio();
                }
            }
            // Con una sola pista, los ocultos no se ven (tampoco van en el GIF): se saltan.
            let i = this.frameAt(this.time);
            if (this.trackCount() === 1) {
                const frames = this.trackFrames(0), t = this.times();
                let at = frames.indexOf(i);
                for (let tries = 0; at >= 0 && tries < frames.length && this.hidden(frames[at]); tries++) {
                    at = (at + 1) % frames.length;
                    i = frames[at];
                    this.time = t.start[i];
                }
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
        this.stopAudio();
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
        // Paso del editor de GIF: su historial lo muestra y el del editor de imágenes, no.
        doc.history.steps[doc.history.index].gif = true;
        doc.emit("history");
        doc.layersChanged();
        doc.emit("active");
        this.update();
    }

    current() {
        const layers = [...this.doc.layers];
        return { layers, props: layers.map((l) => l.props()) };
    }

    insertFrames(at, frames, name, track = -1) {
        const doc = this.doc;
        if (!doc || !frames.length)
            return;
        if (track < 0)
            track = this.curTrack();
        const { layers, props } = this.current();
        if (at < 0 || at > layers.length)
            at = layers.length;
        frames.forEach((f, i) => {
            const layer = new Layer(doc.width, doc.height, nameWithDelay(tr("Fotograma %1", at + i + 1), f.delay || DefaultDelay));
            layer.ctx.drawImage(fitted(f.image, doc.width, doc.height), 0, 0);
            layer.track = track;
            layers.splice(at + i, 0, layer);
            props.splice(at + i, 0, layer.props());
        });
        this.apply(layers, props, at, name, "layer-import");
    }

    insertMedia(index, at, track = -1) {
        const m = this.media[index];
        if (!m)
            return;
        this.insertFrames(at, m.doc ? framesOf(m.doc) : m.frames, tr("Añadir «%1»", m.name), track);
    }

    async insertFiles(files, at, track = -1) {
        const loaded = await this.load(files);
        const frames = loaded.flatMap((m) => m.frames);
        this.imported.push(...loaded);
        this.rebuildMedia();
        if (frames.length)
            this.insertFrames(at, frames, loaded.length === 1 ? tr("Añadir «%1»", loaded[0].name) : tr("Añadir imágenes"), track);
    }

    // A 'to' (índice de capa) y, si se da, a otra pista.
    move(from, to, track = -1) {
        const n = this.count();
        if (from < 0 || from >= n || to < 0 || to >= n)
            return;
        const target = track < 0 ? this.trackOf(from) : Math.min(track, this.trackCount() - 1);
        const other = target !== this.trackOf(from);
        if (from === to && !other)
            return;
        const { layers, props } = this.current();
        props[from] = { ...props[from], track: target };
        layers.splice(to, 0, ...layers.splice(from, 1));
        props.splice(to, 0, ...props.splice(from, 1));
        this.activeTrack = target;
        this.apply(layers, props, to, other ? tr("Mover a la pista %1", target + 1) : tr("Mover fotograma"), "layer-raise");
    }

    // Lleva el fotograma a la pista 'track' empezando en 'startMs' (con espacio vacío delante si hace
    // falta, como en Filmora). Los demás no se mueven salvo si se solaparían: entonces se corren.
    place(from, track, startMs) {
        const n = this.count();
        if (!this.doc || from < 0 || from >= n)
            return;
        track = Math.max(0, Math.min(track, this.trackCount() - 1));
        startMs = Math.max(0, Math.round(startMs / 10) * 10);
        const t = this.times();
        if (track === this.trackOf(from) && startMs === t.start[from])
            return;
        const start = [...t.start], dur = t.end.map((e, i) => e - t.start[i]), trackOf = this.doc.layers.map((l) => l.track || 0);
        trackOf[from] = track;
        // En la pista de destino, por orden de tiempo. Si cae dentro de otro clip, va delante o detrás
        // según en qué mitad; lo que se solaparía se corre hacia la derecha.
        const order = [];
        for (let i = 0; i < n; i++)
            if (i !== from && trackOf[i] === track)
                order.push(i);
        let key = startMs;
        for (const i of order)
            if (startMs >= start[i] && startMs < start[i] + dur[i])
                key = startMs < start[i] + dur[i] / 2 ? start[i] - 0.5 : start[i] + dur[i] - 0.5;
        let at = 0;
        while (at < order.length && start[order[at]] <= key)
            at++;
        order.splice(at, 0, from);
        start[from] = startMs;
        let cursor = 0;
        for (const i of order) {
            start[i] = Math.max(start[i], cursor);
            cursor = start[i] + dur[i];
        }
        // La lista de capas: el fotograma, detrás del que le precede en la pista (o delante del
        // siguiente); los demás, en su sitio. Los espacios vacíos salen de las posiciones nuevas.
        const original = [...this.doc.layers], layers = [...original];
        const moved = layers.splice(from, 1)[0];
        let index = layers.length;
        if (at > 0)
            index = layers.indexOf(original[order[at - 1]]) + 1;
        else if (at + 1 < order.length)
            index = layers.indexOf(original[order[at + 1]]);
        layers.splice(index, 0, moved);
        const ends = {};
        const props = layers.map((l) => {
            const i = original.indexOf(l), k = trackOf[i];
            const p = { ...l.props(), track: k, gap: Math.max(0, start[i] - (ends[k] || 0)) };
            ends[k] = start[i] + dur[i];
            return p;
        });
        const other = track !== this.trackOf(from);
        this.activeTrack = track;
        this.apply(layers, props, index, other ? tr("Mover a la pista %1", track + 1) : tr("Mover fotograma"), "layer-raise");
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

    // Los de la pista elegida (las demás se quedan como están).
    reverse() {
        const frames = this.trackFrames(this.curTrack());
        if (frames.length < 2)
            return;
        const { layers, props } = this.current(), oldLayers = [...layers], oldProps = [...props];
        frames.forEach((at, j) => {
            layers[at] = oldLayers[frames[frames.length - 1 - j]];
            props[at] = oldProps[frames[frames.length - 1 - j]];
        });
        const pos = frames.indexOf(this.selected());
        this.apply(layers, props, pos >= 0 ? frames[frames.length - 1 - pos] : this.selected(), tr("Invertir el orden"), "layer-flip-vertical");
    }

    // Como en la configuración avanzada del GIF: los de la pista elegida al revés al final de ella,
    // sin repetir el último ni el primero.
    boomerang() {
        const frames = this.trackFrames(this.curTrack());
        if (frames.length < 3)
            return;
        const { layers, props } = this.current();
        let at = frames[frames.length - 1] + 1;
        for (let j = frames.length - 2; j >= 1; j--) {
            const i = frames[j];
            layers.splice(at, 0, this.doc.layers[i].duplicate(this.doc.layers[i].name));
            props.splice(at, 0, { ...props[i] });
            at++;
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
            layer.setProps({ ...f.props, motion: f.props.motion || null, track: this.curTrack() });
            layers.splice(at + k, 0, layer);
            props.splice(at + k, 0, layer.props());
        });
        this.apply(layers, props, at, frames.length === 1 ? tr("Pegar fotograma") : tr("Pegar fotogramas"), "edit-paste");
        this.extra = frames.slice(1).map((_, k) => at + 1 + k);
        this.update();
    }

    // --- Puntos clave (Motion.js) -----------------------------------------------------------

    motion(i) {
        // Mientras se arrastra un punto en la gráfica, la animación como quedaría (así la línea de
        // tiempo y la vista previa lo siguen).
        if (this.graphDrag?.preview && i === this.selected())
            return this.graphDrag.preview;
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
        const start = this.times().start[i];
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
        const clip = this.motion(i), times = this.times();
        let t = clamp(this.time - times.start[i], 0, Math.max(0, times.end[i] - times.start[i] - 1));
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
        this.globalKeyButton = button(tr("Punto clave global"),
                                      tr("Pone un punto clave con todos los valores (posición, escala, rotación y opacidad) en el cabezal, o lo quita si ya hay uno ahí (K)"),
                                      () => this.toggleGlobalKey(), "pushbutton gif-global-key");
        this.globalKeyButton.prepend(el("span", { className: "gif-diamond" }));
        this.inspector = el("div", { className: "gif-inspector" }, [
            this.inspectorTitle, this.globalKeyButton, grid,
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
        const atKey = this.hasKeyAtPlayhead();
        this.globalKeyButton.lastChild.textContent = atKey ? tr("Quitar el punto clave global") : tr("Punto clave global");
        this.globalKeyButton.firstChild.classList.toggle("filled", atKey);
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

    // Punto clave global (como el de Filmora): todos los valores (posición, escala, rotación y
    // opacidad) en el cabezal; si ya hay uno ahí, lo quita.
    hasKeyAtPlayhead() {
        const i = this.selected();
        return i >= 0 && Motion.keyTimes(this.motion(i)).includes(this.localTime());
    }

    toggleGlobalKey() {
        const i = this.selected();
        if (!this.doc || i < 0)
            return;
        this.stop();
        const clip = Motion.cloneClip(this.motion(i)), t = this.localTime();
        if (Motion.keyTimes(clip).includes(t)) {
            Motion.removeKeys(clip, t);
            this.selectedKey = -1;
            this.setMotion(i, clip, tr("Quitar el punto clave global"));
            return;
        }
        for (const p of Motion.Props) {
            const track = Motion.editTrack(clip, p);
            Motion.setKey(track, t, Motion.trackValue(track, t, p));
        }
        this.selectedKey = t;
        this.setMotion(i, clip, tr("Punto clave global"));
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
        const clip = this.doc?.layers[this.selected()]?.motion || null, d = this.graphDrag;
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
        const [lo, hi] = this.graphRange(this.doc?.layers[this.selected()]?.motion || null, prop);
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
            d.preview = null;
            d.preview = this.graphClip();
            this.renderKeyLane();
            this.drawPreview();
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
            item.addEventListener("dblclick", () => this.insertMedia(i, -1, this.curTrack()));
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
                    const k = this.trackAtEvent(ev);
                    this.showDropMark(this.insertionAt(this.laneX(ev), k), k);
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
                const k = this.trackAtEvent(ev);
                this.insertMedia(index, this.insertionAt(this.laneX(ev), k), k);
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

    // Dónde quedaría (índice de capa) lo que se suelta en x en la pista: delante del clip cuya mitad
    // está más allá; si no, detrás del último de la pista.
    insertionAt(x, track = this.curTrack()) {
        const t = this.times(), frames = this.trackFrames(track);
        for (const i of frames)
            if (x < (this.x(t.start[i]) + this.x(t.end[i])) / 2)
                return i;
        return frames.length ? frames[frames.length - 1] + 1 : this.count();
    }

    laneX(e) {
        return e.clientX - this.lane.getBoundingClientRect().left;
    }

    laneY(e) {
        return e.clientY - this.lane.getBoundingClientRect().top;
    }

    get trackHeight() {
        const h = +localStorage.getItem("kaint.gif.trackHeight");
        return h >= 40 && h <= 160 ? h : TrackHeights.medium;
    }

    set trackHeight(h) {
        try {
            localStorage.setItem("kaint.gif.trackHeight", String(Math.max(40, Math.min(160, Math.round(h)))));
        } catch {}
        this.thumbs = new WeakMap();
        this.renderTimeline();
    }

    rowStep() {
        return this.trackHeight + TrackGap;
    }

    // Las pistas de número más alto, arriba.
    rowOffset(k) {
        return (this.trackCount() - 1 - k) * this.rowStep();
    }

    trackAtY(y) {
        if (y < TrackTop)
            return -1;
        const row = Math.floor((y - TrackTop) / this.rowStep()), n = this.trackCount();
        return row >= n ? -1 : n - 1 - row;
    }

    // La pista bajo el puntero (fuera de ellas, la elegida).
    trackAtEvent(e) {
        const k = this.trackAtY(this.laneY(e));
        return k >= 0 ? k : this.curTrack();
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
                const k = this.trackAtY(this.laneY(e));
                if (k >= 0 && k !== this.curTrack())
                    this.setActiveTrack(k);
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
                drag = { kind: "resize", id: e.pointerId, index: i, start: this.times().start[i], delay: this.delay(i) };
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
                if (e.button === 0) {
                    // El cabezal va donde se hizo clic (no al principio del clip).
                    const t = this.times();
                    this.seek(clamp(this.msAt(x), t.start[i], Math.max(t.start[i], t.end[i] - 1)));
                    drag = { kind: "press", id: e.pointerId, index: i, x, grabDx: x - this.x(t.start[i]) };
                }
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
                const sel = this.selected();
                if (drag.moved && sel >= 0) {
                    drag.to = Math.max(0, Math.round((this.msAt(x) - this.times().start[sel]) / 10) * 10);
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
                // El clip va donde se suelte, en el tiempo (con espacio delante si se deja, como en
                // Filmora) y en la pista de esa fila. Como con un imán, se pega al cabezal y a los bordes
                // de los demás clips al acercarse.
                const k = this.trackAtY(this.laneY(e));
                drag.toTrack = k >= 0 ? k : this.trackOf(drag.index);
                const t = this.times(), width = this.x(t.end[drag.index]) - this.x(t.start[drag.index]);
                const left = x - (drag.grabDx ?? 0);
                let best = Snap + 1;
                drag.ghostX = Math.max(LeftMargin, left);
                const snap = (ms) => {
                    for (const candidate of [this.x(ms), this.x(ms) - width])
                        if (candidate >= LeftMargin && Math.abs(candidate - left) < best) {
                            best = Math.abs(candidate - left);
                            drag.ghostX = candidate;
                        }
                };
                snap(0);
                snap(this.time);
                for (let i = 0; i < this.count(); i++)
                    if (i !== drag.index) {
                        snap(t.start[i]);
                        snap(t.end[i]);
                    }
                const clipEl = this.lane.querySelector(`.gif-clip[data-index="${drag.index}"]`);
                if (clipEl) {
                    clipEl.classList.add("lifted");
                    clipEl.style.left = `${drag.ghostX}px`;
                    clipEl.style.top = `${this.rowOffset(drag.toTrack)}px`;
                    clipEl.title = timeText(this.msAt(drag.ghostX));
                }
                this.dropMark.hidden = true;
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
            else if (d.kind === "reorder" && d.ghostX !== undefined)
                this.place(d.index, d.toTrack ?? this.trackOf(d.index), this.msAt(d.ghostX));
            this.renderTimeline();
        };
        lane.addEventListener("pointerup", end);
        lane.addEventListener("pointercancel", end);
        lane.addEventListener("dblclick", (e) => {
            // Sobre un rombo, el doble clic no abre la duración del fotograma.
            if (e.target.closest(".gif-key"))
                return;
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
            if (!e.ctrlKey) {
                // La rueda desplaza a lo largo de la línea de tiempo (con Mayús, o si cabe entera, entre
                // las pistas; sin nada que desplazar, acerca o aleja).
                const across = this.scroller, down = this.timelineBody;
                const delta = e.deltaY || e.deltaX;
                const canAcross = across.scrollWidth > across.clientWidth, canDown = down.scrollHeight > down.clientHeight;
                if ((e.shiftKey || !canAcross) && canDown && !e.deltaX) {
                    e.preventDefault();
                    down.scrollTop += delta;
                } else if (canAcross) {
                    e.preventDefault();
                    across.scrollLeft += delta;
                } else if (delta) {
                    e.preventDefault();
                    this.pps = Math.max(40, Math.min(3000, Math.round(this.pps * (delta < 0 ? 1.25 : 0.8))));
                    this.zoom.value = this.pps;
                    this.renderTimeline();
                }
                return;
            }
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
            const k = this.trackAtEvent(e);
            this.showDropMark(this.insertionAt(this.laneX(e), k), k);
        });
        lane.addEventListener("dragleave", () => { this.dropMark.hidden = true; });
        lane.addEventListener("drop", (e) => {
            const track = this.trackAtEvent(e), at = this.insertionAt(this.laneX(e), track);
            this.dropMark.hidden = true;
            const media = e.dataTransfer.getData(MediaMime);
            if (media) {
                e.preventDefault();
                let offset = 0;
                for (const n of media.split(" ")) {
                    const before = this.count();
                    this.insertMedia(+n, at + offset, track);
                    offset += this.count() - before;
                }
                return;
            }
            if (e.dataTransfer.files.length) {
                e.preventDefault();
                e.stopPropagation();
                const files = [...e.dataTransfer.files], audio = files.filter(isAudioFile), images = files.filter((f) => !isAudioFile(f));
                // Los audios, donde se sueltan (en su pista de audio o en la primera libre).
                if (audio.length)
                    this.addAudioFiles(audio, this.msAt(this.laneX(e)), this.audioRowAt(this.laneY(e)));
                if (images.length)
                    this.insertFiles(images, at, track);
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
            const keys = { " ": () => this.togglePlay(), ArrowLeft: () => this.step(-1), ArrowRight: () => this.step(1), Home: () => this.seek(0), k: () => this.toggleGlobalKey(), K: () => this.toggleGlobalKey(),
                           Delete: () => (this.selectedAudio >= 0 ? this.removeAudio(this.selectedAudio)
                                          : this.selectedKey >= 0 ? this.removeKeyTime(this.selectedKey) : this.removeFrames(this.selectedFrames())),
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

    showDropMark(at, track = this.curTrack()) {
        const t = this.times(), frames = this.trackFrames(track);
        const x = frames.includes(at) ? this.x(t.start[at]) - 1 : frames.length ? this.x(t.end[frames[frames.length - 1]]) : this.x(0);
        this.dropMark.style.left = `${x - 1}px`;
        this.dropMark.style.top = `${TrackTop - 4 + this.rowOffset(track)}px`;
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
        const times = this.times(), start = [...times.start], end = [...times.end];
        // Mientras se estira un clip: su duración nueva y los de detrás de su pista, corridos.
        if (resize) {
            const k = this.trackOf(resize.index), i0 = resize.index, diff = resize.delay - (end[i0] - start[i0]);
            end[i0] += diff;
            for (let i = 0; i < start.length; i++)
                if (i !== i0 && this.trackOf(i) === k && start[i] >= times.end[i0]) {
                    start[i] += diff;
                    end[i] += diff;
                }
        }
        const total = Math.max(times.total, ...end, ...(this.doc?.audioClips || []).map((c) => c.start + c.duration));
        const tracks = this.trackCount();
        this.root.style.setProperty("--gif-track-h", `${this.trackHeight}px`);
        this.lane.style.height = `${this.audioTop(this.audioRows()) + 4}px`;
        this.track.style.height = `${tracks * this.rowStep()}px`;
        this.renderTrackHeaders();
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
            const x0 = this.x(start[i]), w = Math.max(6, this.x(end[i]) - x0) - 2;
            const chosen = this.playing ? i === this.playFrame : this.isFrameSelected(i);
            const clip = el("div", { className: `gif-clip${chosen ? " selected" : ""}${i === selected ? " current" : ""}${layers[i].visible ? "" : " hidden-frame"}`,
                                     dataset: { index: i } });
            clip.style.left = `${x0}px`;
            clip.style.top = `${this.rowOffset(this.trackOf(i))}px`;
            clip.style.width = `${w}px`;
            if (w > 14) {
                const img = this.thumbFor(layers[i], Math.max(8, w - 6), Math.max(16, this.trackHeight - 24));
                img.classList.add("gif-clip-thumb");
                clip.append(img);
            }
            const mark = Motion.isEmpty(layers[i].motion) ? "" : "◆ ";
            const text = w > 70 ? `${mark}${i + 1} · ${end[i] - start[i]} ms` : w > 26 ? String(i + 1) : "";
            clip.append(el("span", { className: "gif-clip-label", textContent: text }), el("div", { className: "gif-clip-edge" }));
            clips.push(clip);
        }
        this.track.replaceChildren(...clips);
        this.renderAudio(width);
        this.renderKeyLane();
        this.empty.hidden = layers.length > 0;
        this.playhead.style.left = `${this.x(this.time) - 1}px`;
    }

    // La cabecera de las pistas (a la izquierda, como en Filmora): añadir pista y, en cada una, su
    // nombre, ocultarla y eliminarla. Las de número más alto, arriba.
    renderTrackHeaders() {
        const n = this.trackCount(), active = this.curTrack(), rows = [];
        const add = el("button", { className: "gif-track-add", title: tr("Añade una pista encima de las demás (lo de las pistas de arriba se ve encima)") },
                       [icon("list-add", 16), tr("Añadir pista")]);
        add.addEventListener("click", () => this.addTrack());
        rows.push(add);
        for (let row = 0; row < n; row++) {
            const k = n - 1 - row, frames = this.trackFrames(k).length;
            const eye = button(icon(this.trackHidden(k) ? "eye-closed" : "eye-open", 16), tr("Ocultar o mostrar la pista"), () => this.toggleTrackHidden(k), "gif-track-button");
            const trash = button(icon("edit-delete", 16), tr("Eliminar la pista %1", k + 1), () => this.removeTrack(k), "gif-track-button");
            trash.disabled = n <= 1;
            const head = el("div", { className: `gif-track-head${k === active ? " active" : ""}` }, [
                el("b", { textContent: tr("Pista %1", k + 1) }),
                el("small", { textContent: trn("%n fotograma(s)", frames) }),
                el("div", { className: "gif-row" }, [eye, trash]),
            ]);
            head.addEventListener("click", (e) => {
                if (!e.target.closest("button, .gif-track-resize"))
                    this.setActiveTrack(k);
            });
            // El borde de abajo: arrastrándolo cambia la altura de todas las pistas (las de arriba
            // crecen igual, así el borde sigue al puntero).
            const grip = el("div", { className: "gif-track-resize", title: tr("Arrastra para cambiar la altura de las pistas") });
            grip.addEventListener("pointerdown", (e) => {
                e.preventDefault();
                e.stopPropagation();
                grip.setPointerCapture(e.pointerId);
                const y0 = e.clientY, h0 = this.trackHeight;
                const move = (ev) => (this.trackHeight = h0 + (ev.clientY - y0) / (row + 1));
                const up = () => {
                    grip.removeEventListener("pointermove", move);
                    grip.removeEventListener("pointerup", up);
                    grip.removeEventListener("pointercancel", up);
                };
                grip.addEventListener("pointermove", move);
                grip.addEventListener("pointerup", up);
                grip.addEventListener("pointercancel", up);
            });
            head.append(grip);
            rows.push(head);
        }
        rows.push(el("div", { className: "gif-audio-label", textContent: tr("AUDIO") }));
        const clips = this.doc?.audioClips || [];
        for (let r = 0; r < this.audioRows(); r++) {
            const mine = clips.map((c, i) => (c.row === r ? i : -1)).filter((i) => i >= 0);
            const parts = [el("b", { textContent: tr("Audio %1", r + 1) })];
            if (mine.length) {
                const muted = mine.every((i) => clips[i].muted);
                parts.push(el("div", { className: "gif-row" }, [
                    button(muted ? "🔇" : "🔊", muted ? tr("Activar el sonido") : tr("Silenciar"), () => {
                        const next = clips.map(copyClip);
                        for (const i of mine)
                            next[i].muted = !muted;
                        this.setAudioClips(next, muted ? tr("Activar el sonido") : tr("Silenciar el audio"));
                    }, "gif-track-button"),
                    button(icon("edit-delete", 16), tr("Quitar el audio"), () => {
                        const next = clips.filter((c) => c.row !== r).map(copyClip);
                        for (const c of next)
                            if (c.row > r)
                                c.row--;
                        this.selectedAudio = -1;
                        this.setAudioClips(next, tr("Quitar el audio"));
                    }, "gif-track-button"),
                ]));
            }
            rows.push(el("div", { className: "gif-audio-head" }, parts));
        }
        this.trackHeaders.replaceChildren(...rows);
        this.trackHeaders.oncontextmenu = (e) => {
            // La altura de las pistas, como en Filmora.
            e.preventDefault();
            const h = this.trackHeight;
            popupMenu([
                { text: tr("Pistas pequeñas"), checked: h === TrackHeights.small, run: () => (this.trackHeight = TrackHeights.small) },
                { text: tr("Pistas medianas"), checked: h === TrackHeights.medium, run: () => (this.trackHeight = TrackHeights.medium) },
                { text: tr("Pistas grandes"), checked: h === TrackHeights.large, run: () => (this.trackHeight = TrackHeights.large) },
            ], e.clientX, e.clientY);
        };
    }

    // Los rombos del fotograma elegido, sobre su clip.
    renderKeyLane() {
        const i = this.selected(), children = [], times = this.times();
        // (La franja, solo si el fotograma tiene puntos: si no, tapaba su imagen sin motivo.)
        if (this.doc && i >= 0 && i < this.count() && Motion.keyTimes(this.motion(i)).length) {
            this.keyLane.style.top = `${TrackTop + this.rowOffset(this.trackOf(i)) + Math.max(2, this.trackHeight - 40)}px`;
            const band = el("div", { className: "gif-keylane-band" });
            band.style.left = `${this.x(times.start[i])}px`;
            band.style.width = `${Math.max(4, this.x(times.end[i]) - this.x(times.start[i]) - 2)}px`;
            children.push(band);
            for (const t of Motion.keyTimes(this.motion(i))) {
                const dragging = this.keyDrag?.from === t;
                const key = el("div", { className: `gif-key${t === this.selectedKey || dragging ? " selected" : ""}`, dataset: { t } });
                key.style.left = `${this.x(times.start[i] + (dragging ? this.keyDrag.to : t))}px`;
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
        ctx.fillStyle = "#000000";
        ctx.fillRect(0, 0, w, h);
        const index = this.shownFrame(), layer = this.doc?.layers[index];
        // Con varias pistas, todas una encima de otra (las de número más alto, arriba).
        const shown = this.trackCount() > 1
            ? [...Array(this.trackCount()).keys()].map((k) => this.frameAtTrack(k, this.time)).filter((i) => i >= 0 && !this.hidden(i))
            : index >= 0 ? [index] : [];
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
        // Con el brillo de Configuración, como el damero del lienzo.
        ctx.translate(x, y);
        ctx.fillStyle = checkerPattern(ctx, Settings.get("checkerBrightness") ?? 191);
        ctx.fillRect(0, 0, dw, dh);
        ctx.restore();
        this.previewGeom = { x, y, scale };
        // Con su opacidad y su animación en el cabezal, como en el GIF exportado.
        const cw = layer.canvas.width, ch = layer.canvas.height;
        for (const i of shown) {
            const lay = this.doc.layers[i], t = this.frameTransform(i);
            ctx.save();
            ctx.beginPath();
            ctx.rect(x, y, dw, dh);
            ctx.clip();
            ctx.imageSmoothingEnabled = scale < 1 || !Motion.isIdentity(t);
            ctx.imageSmoothingQuality = "high";
            ctx.globalAlpha = (lay.opacity / 255) * (t.opacity / 100);
            ctx.translate(x, y);
            ctx.scale(dw / cw, dh / ch);
            ctx.translate(cw / 2 + t.x, ch / 2 + t.y);
            ctx.rotate((t.rotation * Math.PI) / 180);
            ctx.scale(t.scaleX / 100, t.scaleY / 100);
            ctx.drawImage(lay.canvas, -cw / 2, -ch / 2);
            ctx.restore();
        }
        if (index === this.selected() && shown.includes(index))
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
            const total = this.times().total, shown = this.shownFrame();
            this.timeLabel.textContent = `${timeText(this.time)} / ${timeText(total)}` + (shown >= 0 ? `   ${tr("Fotograma %1 de %2", shown + 1, this.count())}` : "");
            this.drawPreview();
            const chosen = this.shownFrame();
            for (const clip of this.track.children)
                clip.classList.toggle("selected", this.playing ? +clip.dataset.index === chosen : this.isFrameSelected(+clip.dataset.index));
            if (!this.playing)
                this.renderKeyLane();
            if (chosen === i)
                this.refreshInspectorValues();
            const px = this.x(this.time), view = this.scroller;
            this.playhead.style.left = `${px - 1}px`;
            // (Reproducir no desplaza la línea de tiempo: solo al moverse el cabezal a mano.)
            if (!this.playing && (px < view.scrollLeft + 40 || px > view.scrollLeft + view.clientWidth - 60))
                view.scrollLeft = Math.max(0, px - 60);
            return;
        }
        this.title.textContent = this.doc ? tr("Editor de GIF: %1", this.doc.displayName()) : tr("Editor de GIF");
        for (const c of this.frameControls)
            c.disabled = !has || i < 0;
        if (has && i >= 0 && document.activeElement !== this.delayInput)
            this.delayInput.value = this.delay(i);
        this.timeLabel.textContent = `${timeText(this.time)} / ${timeText(this.times().total)}` + (i >= 0 ? `   ${tr("Fotograma %1 de %2", i + 1, this.count())}` : "");
        this.drawPreview();
        this.renderTimeline();
        this.refreshInspector();
        this.refreshAudioPanel();
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
        // Las pistas de audio, mezcladas (solo en el vídeo).
        let audio = null, mixed = null;
        const mix = async () => (mixed ??= await renderMix(doc.audioClips || [], total * Math.max(1, +repeat.value || 1)).catch(() => null));
        try {
            let blob, ext = kind === "apng" ? "png" : kind;
            const f = +scale.value;
            if (kind === "apng")
                blob = new Blob([await encodeApng(frames, { loops: doc.gifLoops ?? 0, scale: f })], { type: "image/apng" });
            else if (kind === "webp")
                blob = new Blob([await encodeAnimatedWebp(frames, { quality: +webpQuality.value, loops: doc.gifLoops ?? 0, scale: f })], { type: "image/webp" });
            else if (canEncodeMp4() && (!(audio = await mix()) || await canEncodeAac(audio.sampleRate)))
                blob = new Blob([await encodeMp4(frames, { repeat: +repeat.value || 1, scale: f, quality: +quality.value, background: background.value, onProgress: progress,
                                                           audio })],
                                { type: "video/mp4" });
            else {
                const r = await recordVideo(frames, { repeat: +repeat.value || 1, scale: f, background: background.value, onProgress: progress,
                                                      audio: await mix() });
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

    // --- Audio (para los vídeos exportados; el GIF no tiene sonido) -------------------------------

    audioButton() {
        this.audioInput = el("input", { type: "file", multiple: true, hidden: true, accept: `audio/*,${AudioExtensions.map((e) => `.${e}`).join(",")}`,
                                        onchange: () => { this.addAudioFiles([...this.audioInput.files], this.time, -1); this.audioInput.value = ""; } });
        const b = button(tr("Audio"), tr("Añade un archivo de audio (MP3, WAV, OGG, FLAC...) para los vídeos exportados. También se puede arrastrar a la línea de tiempo."),
                         () => this.audioInput.click());
        b.prepend(el("span", { className: "gif-note", textContent: "♪" }));
        b.append(this.audioInput);
        return b;
    }

    audioRows() {
        let used = 0;
        for (const c of this.doc?.audioClips || [])
            used = Math.max(used, c.row + 1);
        return used + 1;
    }

    audioTop(row) {
        return TrackTop + this.trackCount() * this.rowStep() + AudioGap + row * (AudioHeight + TrackGap);
    }

    audioRowAt(y) {
        const top = this.audioTop(0) - TrackGap / 2;
        if (y < top)
            return -1;
        const row = Math.floor((y - top) / (AudioHeight + TrackGap));
        return row < this.audioRows() ? row : -1;
    }

    setAudioClips(clips, text) {
        const doc = this.doc;
        if (!doc)
            return;
        const before = doc.audioClips || [];
        const apply = (list) => {
            doc.audioClips = list;
            if (this.selectedAudio >= list.length)
                this.selectedAudio = -1;
            this.update();
        };
        doc.audioClips = clips;
        doc.push({ name: text, icon: "audio-x-generic", gif: true, undo: () => apply(before), redo: () => apply(clips) });
        this.update();
    }

    async addAudioFiles(files, startMs = -1, row = -1) {
        if (!this.doc || !files.length)
            return;
        const clips = (this.doc.audioClips || []).map(copyClip), before = clips.length, failed = [];
        let at = Math.max(0, startMs >= 0 ? startMs : this.time);
        document.body.classList.add("busy");
        for (const file of files) {
            try {
                const c = await analyzeAudio(file);
                c.start = at;
                c.row = row;
                if (c.row < 0)
                    for (c.row = 0; clips.some((o) => o.row === c.row && o.start < c.start + c.duration && c.start < o.start + o.duration); c.row++);
                clips.push(c);
                at += c.length;
            } catch (e) {
                failed.push(`${file.name}: ${e?.message || e}`);
            }
        }
        document.body.classList.remove("busy");
        if (failed.length)
            await message(tr("Añadir audio"), failed.join("\n"), "dialog-warning");
        if (clips.length > before) {
            this.selectedAudio = clips.length - 1;
            this.setAudioClips(clips, clips.length - before > 1 ? tr("Añadir audios") : tr("Añadir audio"));
        }
    }

    removeAudio(i) {
        const clips = (this.doc?.audioClips || []).map(copyClip);
        if (i < 0 || i >= clips.length)
            return;
        clips.splice(i, 1);
        this.selectedAudio = -1;
        this.setAudioClips(clips, tr("Quitar el audio"));
    }

    selectAudio(i) {
        this.selectedAudio = i;
        this.inspector?.classList.toggle("audio-mode", i >= 0);
        this.renderTimeline();
        this.refreshAudioPanel();
    }

    toggleAudioKey(i) {
        const clips = (this.doc?.audioClips || []).map(copyClip), c = clips[i];
        if (!c)
            return;
        const t = clamp(Math.round(this.time - c.start), 0, c.duration);
        if (c.volume.keys.some((k) => k.t === t)) {
            c.volume = { ...c.volume, keys: c.volume.keys.filter((k) => k.t !== t) };
            this.setAudioClips(clips, tr("Quitar el punto de volumen"));
        } else {
            setKey(c, t, Math.round(keyedVolume(c, t)));
            this.setAudioClips(clips, tr("Añadir un punto de volumen"));
        }
    }

    audioMenu(i, x, y) {
        const clips = this.doc?.audioClips || [], c = clips[i];
        if (!c)
            return;
        const edit = (fn, text) => {
            const next = clips.map(copyClip);
            fn(next[i]);
            this.setAudioClips(next, text);
        };
        popupMenu([
            { text: c.muted ? tr("Activar el sonido") : tr("Silenciar"), run: () => edit((n) => (n.muted = !n.muted), c.muted ? tr("Activar el sonido") : tr("Silenciar el audio")) },
            { text: tr("Punto de volumen en el cabezal"), enabled: this.time >= c.start && this.time <= c.start + c.duration, run: () => this.toggleAudioKey(i) },
            { text: tr("Quitar los puntos de volumen"), enabled: c.volume.keys.length > 0, run: () => edit((n) => (n.volume = { ...n.volume, keys: [] }), tr("Quitar los puntos de volumen")) },
            "-",
            { text: tr("Quitar el audio"), icon: "edit-delete", run: () => this.removeAudio(i) },
        ], x, y);
    }

    // El volumen (0 a 200 %) a lo alto del clip, debajo de su nombre.
    static volumeY(v) {
        return AudioHeight - 4 - (v * (AudioHeight - 22)) / 200;
    }

    renderAudio(width) {
        const clips = this.doc?.audioClips || [], rows = this.audioRows();
        const bands = [el("div", { className: "gif-audio-band", style: { top: `${this.audioTop(0) - AudioGap}px`, width: `${width}px` } })];
        for (let r = 0; r < rows; r++) {
            const row = el("div", { className: "gif-audio-row", style: { top: `${this.audioTop(r)}px`, width: `${width}px` } });
            if (!clips.some((c) => c.row === r))
                row.append(el("span", { className: "gif-audio-empty", textContent: tr("Arrastra aquí un audio (MP3, WAV, OGG...) para los vídeos exportados") }));
            bands.push(row);
        }
        clips.forEach((c, i) => bands.push(this.audioClipElement(c, i)));
        this.audioArea.replaceChildren(...bands);
    }

    audioClipElement(c, i) {
        const x0 = this.x(c.start), w = Math.max(6, this.x(c.start + c.duration) - x0) - 2;
        const node = el("div", { className: `gif-audio-clip${i === this.selectedAudio ? " selected" : ""}${c.muted ? " muted" : ""}`,
                                 title: `${c.name} · ${timeText(c.duration)}` });
        Object.assign(node.style, { left: `${x0}px`, top: `${this.audioTop(c.row)}px`, width: `${w}px`, height: `${AudioHeight}px` });
        const canvas = el("canvas");
        const dpr = window.devicePixelRatio || 1;
        canvas.width = Math.max(1, Math.round(w * dpr));
        canvas.height = Math.round(AudioHeight * dpr);
        canvas.style.width = `${w}px`;
        canvas.style.height = `${AudioHeight}px`;
        const g = canvas.getContext("2d");
        g.scale(dpr, dpr);
        // La onda, a la altura de su volumen.
        const mid = (18 + AudioHeight - 3) / 2, half = (AudioHeight - 21) / 2;
        g.fillStyle = c.muted ? "rgba(139,143,151,0.6)" : "rgba(31,209,185,0.62)";
        for (let px = 0; px < w; px++) {
            const local = this.msAt(x0 + px) - c.start, idx = Math.floor((c.offset + local) / PeakMs);
            if (idx < 0 || idx >= c.peaks.length)
                continue;
            const h = Math.max(1, (c.peaks[idx] / 255) * half * Math.min(1.6, volumeAt(c, local) / 100));
            g.fillRect(px, mid - Math.min(half, h), 1, 2 * Math.min(half, h));
        }
        // La línea del volumen (con los fundidos) y sus puntos.
        g.strokeStyle = "rgba(255,255,255,0.85)";
        g.lineWidth = 1.4;
        g.beginPath();
        for (let px = 0; px <= w; px += 3) {
            const y = GifEditor.volumeY(volumeAt(c, clamp(this.msAt(x0 + px) - c.start, 0, c.duration)));
            px ? g.lineTo(px, y) : g.moveTo(px, y);
        }
        g.stroke();
        for (const k of c.volume.keys)
            drawDiamond(g, this.x(c.start + k.t) - x0, GifEditor.volumeY(k.value), 5, i === this.selectedAudio, "#1fd1b9");
        node.append(canvas, el("span", { className: "gif-audio-name", textContent: `♪ ${c.name}${c.muted ? ` · ${tr("Silenciado")}` : ""}` }),
                    el("div", { className: "gif-audio-edge left" }), el("div", { className: "gif-audio-edge right" }));
        node.addEventListener("pointerdown", (e) => this.audioPointerDown(e, node, i));
        node.addEventListener("dblclick", (e) => {
            e.stopPropagation();
            const clips = this.doc.audioClips.map(copyClip), cc = clips[i];
            const r = node.getBoundingClientRect();
            const t = clamp(Math.round((this.msAt(x0 + e.clientX - r.left) - cc.start) / 10) * 10, 0, cc.duration);
            const v = clamp(Math.round(((AudioHeight - 4 - (e.clientY - r.top)) * 200) / (AudioHeight - 22)), 0, 200);
            setKey(cc, t, v);
            this.setAudioClips(clips, tr("Añadir un punto de volumen"));
        });
        node.addEventListener("contextmenu", (e) => {
            e.preventDefault();
            e.stopPropagation();
            this.selectAudio(i);
            this.audioMenu(i, e.clientX, e.clientY);
        });
        return node;
    }

    // Mover el clip (y a otra pista de audio), recortarlo por sus bordes o arrastrar un punto de volumen.
    audioPointerDown(e, node, i) {
        if (e.button !== 0)
            return;
        e.stopPropagation();
        e.preventDefault();
        const orig = this.doc.audioClips[i];
        // Al elegirlo se rehace la línea de tiempo: su elemento nuevo.
        if (i !== this.selectedAudio) {
            this.selectAudio(i);
            node = this.audioArea.querySelectorAll(".gif-audio-clip")[i] || node;
        }
        const capture = (target, id) => {
            try {
                target.setPointerCapture(id);
            } catch {}
        };
        const r = node.getBoundingClientRect(), lx = e.clientX - r.left, ly = e.clientY - r.top;
        const key = orig.volume.keys.find((k) => Math.abs(this.x(orig.start + k.t) - this.x(orig.start) - lx) <= 6 && Math.abs(GifEditor.volumeY(k.value) - ly) <= 7);
        const mode = key ? "key" : lx >= r.width - 8 ? "end" : lx <= 8 ? "start" : "move";
        const startX = e.clientX;
        let moved = false, next = copyClip(orig);
        capture(node, e.pointerId);
        const move = (ev) => {
            const dt = Math.round(((ev.clientX - startX) * 1000) / this.pps);
            if (!moved && Math.abs(ev.clientX - startX) < 4 && mode !== "key")
                return;
            moved = true;
            next = copyClip(orig);
            if (mode === "move") {
                next.start = Math.max(0, orig.start + dt);
                const row = this.audioRowAt(this.laneY(ev));
                if (row >= 0)
                    next.row = row;
            } else if (mode === "end") {
                next.duration = clamp(orig.duration + dt, 100, Math.max(100, orig.length - orig.offset));
            } else if (mode === "start") {
                const d = clamp(dt, Math.max(-orig.offset, -orig.start), orig.duration - 100);
                next.start = orig.start + d;
                next.offset = orig.offset + d;
                next.duration = orig.duration - d;
                next.volume = { ...next.volume, keys: next.volume.keys.map((k) => ({ ...k, t: Math.max(0, k.t - d) })) };
            } else {
                const t = clamp(Math.round((key.t + dt) / 10) * 10, 0, orig.duration);
                const v = clamp(Math.round(((AudioHeight - 4 - (ev.clientY - r.top)) * 200) / (AudioHeight - 22)), 0, 200);
                next.volume = { ...next.volume, keys: next.volume.keys.filter((k) => k !== key && k.t !== t) };
                setKey(next, t, v);
                node.title = `${v} % · ${timeText(next.start + t)}`;
            }
            // Se ve mientras se arrastra (sin pasar al historial hasta soltar).
            const clips = [...this.doc.audioClips];
            clips[i] = next;
            const shown = this.audioClipElement(next, i);
            shown.classList.add("lifted");
            node.replaceWith(shown);
            node = shown;
            capture(node, ev.pointerId);
        };
        const up = () => {
            window.removeEventListener("pointermove", move);
            window.removeEventListener("pointerup", up);
            window.removeEventListener("pointercancel", up);
            if (!moved) {
                this.seek(this.msAt(this.laneX(e)));
                return;
            }
            const clips = this.doc.audioClips.map(copyClip);
            clips[i] = next;
            this.setAudioClips(clips, mode === "move" ? tr("Mover el audio") : mode === "key" ? tr("Cambiar el volumen") : tr("Recortar el audio"));
        };
        window.addEventListener("pointermove", move);
        window.addEventListener("pointerup", up);
        window.addEventListener("pointercancel", up);
    }

    buildAudioPanel() {
        const panel = this.audioPanel = el("div", { className: "gif-audio-panel" });
        this.audioTitle = el("div", { className: "gif-section-title" });
        this.audioInfo = el("p", { className: "note" });
        this.audioVolume = el("input", { type: "number", min: 0, max: 200, step: 5, className: "gif-prop-value" });
        this.audioSlider = el("input", { type: "range", min: 0, max: 200, step: 1 });
        this.audioKey = button(el("span", { className: "gif-diamond" }), tr("Punto clave de volumen en el cabezal: con varios, el volumen cambia de uno a otro"),
                               () => this.toggleAudioKey(this.selectedAudio), "gif-key-toggle");
        this.audioFadeIn = el("input", { type: "number", min: 0, max: 30, step: 0.1, className: "gif-prop-value" });
        this.audioFadeOut = el("input", { type: "number", min: 0, max: 30, step: 0.1, className: "gif-prop-value" });
        this.audioMute = el("input", { type: "checkbox" });
        const edit = (fn, text) => {
            const i = this.selectedAudio, clips = (this.doc?.audioClips || []).map(copyClip);
            if (!clips[i])
                return;
            fn(clips[i]);
            this.setAudioClips(clips, text || tr("Cambiar el audio"));
        };
        const setVolume = (v) => edit((c) => {
            v = clamp(v, 0, 200);
            if (c.volume.keys.length)
                setKey(c, clamp(Math.round(this.time - c.start), 0, c.duration), v);
            else
                c.volume = { ...c.volume, base: v };
        }, tr("Cambiar el volumen"));
        this.audioVolume.addEventListener("change", () => setVolume(+this.audioVolume.value));
        this.audioSlider.addEventListener("input", () => (this.audioVolume.value = this.audioSlider.value));
        this.audioSlider.addEventListener("change", () => setVolume(+this.audioSlider.value));
        this.audioFadeIn.addEventListener("change", () => edit((c) => (c.fadeIn = Math.round(clamp(+this.audioFadeIn.value, 0, 30) * 1000))));
        this.audioFadeOut.addEventListener("change", () => edit((c) => (c.fadeOut = Math.round(clamp(+this.audioFadeOut.value, 0, 30) * 1000))));
        this.audioMute.addEventListener("change", () => edit((c) => (c.muted = this.audioMute.checked),
                                                             this.audioMute.checked ? tr("Silenciar el audio") : tr("Activar el sonido")));
        const remove = button(tr("Quitar el audio"), "", () => this.removeAudio(this.selectedAudio), "pushbutton");
        const back = button("‹", tr("Animación"), () => this.selectAudio(-1), "gif-key-nav");
        panel.append(
            el("div", { className: "gif-row" }, [back, this.audioTitle]), this.audioInfo,
            el("div", { className: "gif-props" }, [
                el("label", { textContent: tr("Volumen:") }), el("span", { className: "gif-prop-input" }, [this.audioVolume, el("span", { textContent: "%" })]),
                el("span"), this.audioKey, el("span"),
            ]),
            this.audioSlider,
            el("div", { className: "gif-props" }, [
                el("label", { textContent: tr("Fundido de entrada:") }), el("span", { className: "gif-prop-input" }, [this.audioFadeIn, el("span", { textContent: tr(" s") })]),
                el("span"), el("span"), el("span"),
                el("label", { textContent: tr("Fundido de salida:") }), el("span", { className: "gif-prop-input" }, [this.audioFadeOut, el("span", { textContent: tr(" s") })]),
                el("span"), el("span"), el("span"),
            ]),
            el("label", { className: "gif-check" }, [this.audioMute, tr("Silenciar")]),
            remove,
            el("p", { className: "note", textContent: tr("El audio va en los vídeos exportados (MP4 y WebM): el GIF no tiene sonido. Doble clic en el clip añade un punto de volumen; arrastra los puntos para cambiarlo.") }),
        );
        return panel;
    }

    refreshAudioPanel() {
        const c = this.doc?.audioClips?.[this.selectedAudio];
        this.inspector?.classList.toggle("audio-mode", !!c);
        if (!c)
            return;
        this.audioTitle.textContent = `♪ ${c.name}`;
        this.audioInfo.textContent = tr("Empieza en %1 y suena %2 (de %3).", timeText(c.start), timeText(c.duration), timeText(c.length));
        const local = clamp(Math.round(this.time - c.start), 0, c.duration), v = Math.round(keyedVolume(c, local));
        if (document.activeElement !== this.audioVolume)
            this.audioVolume.value = v;
        this.audioSlider.value = v;
        this.audioFadeIn.value = (c.fadeIn / 1000).toFixed(1);
        this.audioFadeOut.value = (c.fadeOut / 1000).toFixed(1);
        this.audioMute.checked = c.muted;
        this.audioKey.firstChild?.classList.toggle("filled", c.volume.keys.some((k) => k.t === local));
    }

    // Lo que suena mientras se reproduce (Web Audio): cada clip desde el cabezal, con su volumen.
    async startAudio() {
        this.stopAudio();
        const token = this.audioToken;
        const clips = (this.doc?.audioClips || []).filter((c) => !c.muted && c.start + c.duration > this.time);
        if (!clips.length || typeof AudioContext === "undefined")
            return;
        const ctx = (this.audioCtx ??= new AudioContext());
        if (ctx.state === "suspended")
            await ctx.resume().catch(() => {});
        const buffers = await Promise.all(clips.map((c) => decodeClip(c).catch(() => null)));
        if (token !== this.audioToken || !this.playing)
            return;
        const speed = +this.speed.value || 1, t0 = this.time, now = ctx.currentTime + 0.02;
        clips.forEach((c, k) => {
            if (!buffers[k])
                return;
            const from = Math.max(0, t0 - c.start), when = now + Math.max(0, c.start - t0) / 1000 / speed;
            const source = ctx.createBufferSource();
            source.buffer = buffers[k];
            source.playbackRate.value = speed;
            const gain = ctx.createGain();
            automate(gain.gain, c, when, from, speed);
            source.connect(gain).connect(ctx.destination);
            source.start(when, (c.offset + from) / 1000, (c.duration - from) / 1000);
            this.audioNodes.push(source);
        });
    }

    stopAudio() {
        this.audioToken = (this.audioToken || 0) + 1;
        for (const n of this.audioNodes || [])
            try {
                n.stop();
            } catch {}
        this.audioNodes = [];
    }
}
