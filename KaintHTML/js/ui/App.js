// Ventana principal (src/app/MainWindow.cpp de Kaint): imágenes abiertas, acciones y menús,
// barras, ventanas flotantes, barra de estado, teclado, portapapeles y archivos.
import { AppState } from "../AppState.js";
import { Document, Layer } from "../core/Document.js";
import { Effects, Categories } from "../core/Effects.js";
import { download, encode, formatFor, openFile, OpenAccept, SaveFormats, baseName } from "../core/ImageIO.js";
import { cloneCanvas, context, createCanvas, drawBlended, thumbnail } from "../core/Raster.js";
import { Selection } from "../core/Selection.js";
import { Settings } from "../Settings.js";
import { REPOSITORY, VERSION } from "../version.js";
import { tr } from "../i18n.js";
import { Action, keyCombo, toolButton, updateActions } from "./Actions.js";
import { applySetting as applyAnimations } from "./Animations.js";
import { CanvasView, ZoomPresets } from "./Canvas.js";
import {
    aboutDialog, canvasSizeDialog, dialogOpen, el, layerPropertiesDialog, message, newImageDialog, resizeDialog,
    saveOptionsDialog, settingsDialog, TaskDialog,
} from "./Dialogs.js";
import { effectDialog, renderEffect } from "./EffectDialog.js";
import { icon, refreshIcons, setActiveColors } from "./Icons.js";
import { MenuBar, popupMenu } from "./Menu.js";
import { colorsPanel, FloatingPanel, historyPanel, layersPanel, toolsPanel } from "./Panels.js";
import { ToolOptions } from "./ToolOptions.js";
import { ToolManager } from "../tools/Tool.js";
import { EllipseSelectTool, LassoSelectTool, MagicWandTool, RectSelectTool } from "../tools/SelectionTools.js";
import { MoveSelectedTool, MoveSelectionTool } from "../tools/MoveTools.js";
import { ColorPickerTool, PanTool, ZoomTool } from "../tools/ViewTools.js";
import { GradientTool, PaintBucketTool } from "../tools/FillTools.js";
import { BrushTool, CloneStampTool, EraserTool, PencilTool, RecolorTool } from "../tools/PaintTools.js";
import { TextTool } from "../tools/TextTool.js";
import { LineTool, ShapesTool } from "../tools/ShapeTools.js";

const openUrl = (url) => () => window.open(url, "_blank", "noopener,noreferrer");

export class App extends EventTarget {
    constructor() {
        super();
        this.docs = [];
        this.doc = null;
        this.lastEffect = null;
        this.effectValues = {};
        this.clipboard = null;
        this.view = new CanvasView(document.getElementById("view"), document.getElementById("workspace"));
        this.tools = new ToolManager(this, [
            RectSelectTool, MoveSelectedTool, LassoSelectTool, MoveSelectionTool, EllipseSelectTool, ZoomTool, MagicWandTool,
            PanTool, PaintBucketTool, GradientTool, BrushTool, EraserTool, PencilTool, ColorPickerTool, CloneStampTool,
            RecolorTool, TextTool, LineTool, ShapesTool,
        ]);
        this.actions = new Map();
        this.applyTheme();
        this.createActions();
        this.menuBar = new MenuBar(document.getElementById("menubar"));
        this.createMenus();
        this.createToolBar();
        this.createPanels();
        this.createCornerButtons();
        this.createStatusBar();
        this.toolOptions = new ToolOptions(document.getElementById("tool-options"), this);
        this.tools.addEventListener("changed", () => {
            this.view.tool = this.tools.current;
            this.view.updateCursor();
            this.showHint(this.tools.current.hint());
            this.view.update();
        });
        this.installKeyboard();
        this.installClipboard();
        this.installDrop();
        // El área de trabajo y la página no se desplazan nunca (el lienzo tiene sus barras).
        for (const box of [document.getElementById("workspace"), document.getElementById("app"), document.documentElement])
            box.addEventListener("scroll", () => {
                box.scrollTop = 0;
                box.scrollLeft = 0;
            });
        this.view.addEventListener("zoom", () => this.updateZoomBox());
        this.view.addEventListener("cursor", (e) => this.updateCursorPos(e.detail));
        AppState.addEventListener("colors", () => this.updateToolIconColors());
        window.addEventListener("kaint-settings", () => this.applySettings());
        window.addEventListener("beforeunload", (e) => {
            if (this.docs.some((d) => d.isModified())) {
                e.preventDefault();
                e.returnValue = "";
            }
        });
        this.applySettings();
        this.tools.select("paintbrush");
        this.addDocument(new Document(800, 600));
        updateActions();
    }

    // --- Tema y configuración --------------------------------------------------------------

    applyTheme() {
        const scheme = Settings.get("colorScheme");
        const dark = scheme === 2 || (scheme === 0 && matchMedia("(prefers-color-scheme: dark)").matches);
        const root = document.documentElement;
        if (root.classList.contains("dark") !== dark) {
            root.classList.toggle("dark", dark);
            refreshIcons();
            this.view?.update();
        }
        if (!this.schemeWatch) {
            this.schemeWatch = matchMedia("(prefers-color-scheme: dark)");
            this.schemeWatch.addEventListener("change", () => this.applyTheme());
        }
    }

    applySettings() {
        this.applyTheme();
        applyAnimations();
        document.getElementById("workspace").classList.toggle("translucent", !!Settings.get("translucentPanels"));
        this.updateToolIconColors();
        this.view.update();
        updateActions();
    }

    updateToolIconColors() {
        const on = Settings.get("coloredToolIcons");
        setActiveColors(on ? AppState.primary : null, on ? AppState.secondary : null);
    }

    // --- Documentos ------------------------------------------------------------------------

    addDocument(doc) {
        this.docs.push(doc);
        const forward = (type) => doc.addEventListener(type, (e) => {
            if (doc === this.doc)
                this.dispatchEvent(new CustomEvent(type, { detail: e.detail }));
            if (type === "pixels" || type === "modified")
                this.updateTab(doc);
            if (type === "history" || type === "selection" || type === "active" || type === "layers" || type === "size")
                updateActions();
            if (type === "selection" || type === "size")
                this.updateStatus();
        });
        ["history", "layers", "pixels", "active", "selection", "size", "modified"].forEach(forward);
        this.setCurrent(doc);
        this.rebuildTabs();
    }

    setCurrent(doc) {
        if (doc === this.doc)
            return;
        this.tools.current?.finish();
        this.doc = doc;
        this.view.setDocument(doc);
        this.dispatchEvent(new Event("document"));
        this.rebuildTabs();
        this.updateStatus();
        updateActions();
        document.title = doc ? `${doc.isModified() ? "*" : ""}${doc.displayName()} - Kaint` : "Kaint";
    }

    async closeDocument(doc, ask = true) {
        if (!doc)
            return true;
        this.tools.current?.finish();
        if (ask && doc.isModified()) {
            const dlg = new TaskDialog(tr("Cambios no guardados"), "dialog-warning",
                                       tr("«%1» tiene cambios sin guardar.\n¿Qué quiere hacer?", doc.displayName()),
                                       thumbnail(doc.flattened(), 96, 96));
            const save = dlg.addChoice("document-save", tr("Guardar"), tr("Guardar la imagen y, a continuación, cerrarla."), true);
            const discard = dlg.addChoice("edit-delete", tr("No guardar"), tr("Descartar los cambios no guardados."));
            dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Volver a Kaint."));
            const answer = await dlg.exec();
            if (answer === save) {
                if (!(await this.save(doc)))
                    return false;
            } else if (answer !== discard) {
                return false;
            }
        }
        const i = this.docs.indexOf(doc);
        this.docs.splice(i, 1);
        if (doc === this.doc) {
            this.doc = null;
            this.setCurrent(this.docs[Math.min(i, this.docs.length - 1)] || null);
        }
        this.rebuildTabs();
        return true;
    }

    async closeAll() {
        for (const doc of [...this.docs])
            if (!(await this.closeDocument(doc)))
                return;
    }

    rebuildTabs() {
        const strip = document.getElementById("image-strip");
        strip.replaceChildren();
        for (const doc of this.docs) {
            const tab = el("div", { className: "image-tab" + (doc === this.doc ? " current" : ""), title: doc.displayName() });
            const close = el("div", { className: "close", title: tr("Cerrar") });
            close.addEventListener("pointerdown", (e) => e.stopPropagation());
            close.addEventListener("click", () => this.closeDocument(doc));
            tab.addEventListener("pointerdown", (e) => {
                if (e.button === 1) {
                    e.preventDefault();
                    this.closeDocument(doc);
                } else if (e.button === 0) {
                    this.setCurrent(doc);
                }
            });
            tab.addEventListener("contextmenu", (e) => {
                e.preventDefault();
                popupMenu([this.actions.get("save"), this.actions.get("saveAs"), "-", this.actions.get("close"),
                           this.actions.get("closeAll")], e.clientX, e.clientY);
            });
            tab.append(close);
            doc.tab = tab;
            strip.appendChild(tab);
            this.updateTab(doc, true);
        }
        document.title = this.doc ? `${this.doc.isModified() ? "*" : ""}${this.doc.displayName()} - Kaint` : "Kaint";
    }

    updateTab(doc, now = false) {
        const tab = doc.tab;
        if (!tab)
            return;
        const paint = () => {
            tab.querySelector("canvas")?.remove();
            tab.querySelector(".unsaved")?.remove();
            tab.insertBefore(thumbnail(doc.flattened(), 64, 48), tab.firstChild);
            if (doc.isModified())
                tab.appendChild(el("div", { className: "unsaved", title: tr("Cambios no guardados") }));
            tab.title = doc.displayName();
            if (doc === this.doc)
                document.title = `${doc.isModified() ? "*" : ""}${doc.displayName()} - Kaint`;
        };
        clearTimeout(doc.tabTimer);
        now ? paint() : (doc.tabTimer = setTimeout(paint, 400));
    }

    // --- Acciones --------------------------------------------------------------------------

    action(id, text, iconName, shortcut, run, enabled = null, checked = null) {
        const a = new Action({ id, text, icon: iconName, shortcut, run, enabled, checked });
        this.actions.set(id, a);
        return a;
    }

    actionButton(id) {
        const a = this.actions.get(id);
        return toolButton(a, a.icon, 16);
    }

    // Antes de cualquier orden se aplica lo que la herramienta esté editando.
    command(fn) {
        return (...args) => {
            this.tools.current?.finish();
            return fn(...args);
        };
    }

    createActions() {
        const has = () => !!this.doc;
        const sel = () => !!this.doc && !this.doc.selection.isEmpty();
        const c = (fn) => this.command(fn);
        const A = (...args) => this.action(...args);
        // Archivo
        A("new", tr("&Nuevo..."), "document-new", "Ctrl+N", c(() => this.newImage()));
        A("open", tr("&Abrir..."), "document-open", "Ctrl+O", c(() => this.open()));
        A("save", tr("&Guardar"), "document-save", "Ctrl+S", c(() => this.save(this.doc)), has);
        A("saveAs", tr("Guardar &como..."), "document-save-as", "Ctrl+Shift+S", c(() => this.save(this.doc, true)), has);
        A("print", tr("&Imprimir..."), "document-print", "Ctrl+P", c(() => this.print()), has);
        A("close", tr("&Cerrar"), "document-close", "Ctrl+W", () => this.closeDocument(this.doc), has);
        A("closeAll", tr("Cerrar t&odas"), "document-close", "Ctrl+Shift+W", () => this.closeAll(), has);
        // Edición
        A("undo", tr("&Deshacer"), "edit-undo", "Ctrl+Z", () => this.undo(), () => this.doc?.history.canUndo() || this.tools.current?.isBusy());
        A("redo", tr("&Rehacer"), "edit-redo", ["Ctrl+Y", "Ctrl+Shift+Z"], () => this.redo(), () => this.doc?.history.canRedo());
        A("cut", tr("Cor&tar"), "edit-cut", "Ctrl+X", c(() => this.cut()), sel);
        A("copy", tr("&Copiar"), "edit-copy", "Ctrl+C", c(() => this.copy(false)), has);
        A("copyMerged", tr("Copiar co&mbinado"), "edit-copy-merged", "Ctrl+Shift+C", c(() => this.copy(true)), has);
        A("paste", tr("&Pegar"), "edit-paste", "Ctrl+V", c(() => this.pasteFromClipboard("layer")));
        A("pasteLayer", tr("Pegar en capa &nueva"), "edit-paste-new-layer", "Ctrl+Shift+V", c(() => this.pasteFromClipboard("newLayer")), has);
        A("pasteImage", tr("Pegar en una &imagen nueva"), "edit-paste-new-image", "Ctrl+Alt+V", c(() => this.pasteFromClipboard("newImage")));
        A("eraseSel", tr("&Borrar la selección"), "edit-erase-selection", "Del", c(() => this.eraseSelection()), sel);
        A("fillSel", tr("&Rellenar la selección"), "edit-fill-selection", "Backspace", c(() => this.fillSelection()), sel);
        A("invertSel", tr("&Invertir la selección"), "edit-select-invert", "Ctrl+I", c(() => this.invertSelection()), has);
        A("selectAll", tr("Seleccionar &todo"), "edit-select-all", "Ctrl+A", c(() => this.selectAll()), has);
        A("deselect", tr("&Anular la selección"), "edit-select-none", "Ctrl+D", c(() => this.deselect()), sel);
        // Ver
        A("zoomIn", tr("&Acercar"), "zoom-in", ["Ctrl++", "Ctrl+=", "Ctrl+Shift+="], () => this.view.zoomIn(), has);
        A("zoomOut", tr("A&lejar"), "zoom-out", ["Ctrl+-", "Ctrl+Shift+-"], () => this.view.zoomOut(), has);
        A("zoomFit", tr("&Zum a ventana"), "zoom-fit-best", "Ctrl+B", () => this.view.zoomToWindow(), has);
        A("zoomSel", tr("Zum a &selección"), "zoom-select", "Ctrl+Shift+A", () => this.view.zoomToRect(this.doc.selection.bounds()), sel);
        A("actualSize", tr("&Tamaño real"), "zoom-original", "Ctrl+0", () => this.view.setZoom(1), has);
        A("pixelGrid", tr("Cuadrícula de &píxeles"), "view-grid", "Ctrl+'", () => {
            this.view.pixelGrid = !this.view.pixelGrid;
            this.view.update();
            updateActions();
        }, has, () => this.view.pixelGrid);
        A("fullscreen", tr("&Pantalla completa"), null, "F11", () => {
            document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen?.();
        }, null, () => !!document.fullscreenElement);
        // Imagen
        A("crop", tr("&Recortar según la selección"), "transform-crop", "Ctrl+Shift+X", c(() => this.cropToSelection()), sel);
        A("resize", tr("Cambiar el &tamaño..."), "transform-scale", "Ctrl+R", c(() => this.resizeImage()), has);
        A("canvasSize", tr("Tamaño del &lienzo..."), "transform-crop-and-resize", "Ctrl+Shift+R", c(() => this.canvasSize()), has);
        A("flipH", tr("Voltear &horizontalmente"), "object-flip-horizontal", null, c(() => this.transformImage("flipH")), has);
        A("flipV", tr("Voltear &verticalmente"), "object-flip-vertical", null, c(() => this.transformImage("flipV")), has);
        A("rotCW", tr("Girar 90° en sentido &horario"), "object-rotate-right", "Ctrl+H", c(() => this.transformImage("cw")), has);
        A("rotCCW", tr("Girar 90° en sentido &antihorario"), "object-rotate-left", "Ctrl+G", c(() => this.transformImage("ccw")), has);
        A("rot180", tr("Girar &180°"), "image-rotate-180", "Ctrl+J", c(() => this.transformImage("180")), has);
        A("flatten", tr("&Acoplar"), "image-flatten", "Ctrl+Shift+F", c(() => this.flatten()), () => this.doc?.layers.length > 1);
        // Capas
        const many = () => this.doc?.layers.length > 1;
        const notTop = () => this.doc && this.doc.active < this.doc.layers.length - 1;
        const notBottom = () => this.doc && this.doc.active > 0;
        A("addLayer", tr("Agregar &nueva capa"), "layer-new", "Ctrl+Shift+N", c(() => this.addLayer()), has);
        A("deleteLayer", tr("&Eliminar la capa"), "layer-delete", "Ctrl+Shift+Del", c(() => this.deleteLayer()), many);
        A("duplicateLayer", tr("&Duplicar la capa"), "layer-duplicate", "Ctrl+Shift+D", c(() => this.duplicateLayer()), has);
        A("mergeDown", tr("&Combinar capa con la inferior"), "layer-merge-down", "Ctrl+M", c(() => this.mergeDown()), notBottom);
        A("toggleLayer", tr("Alternar &visibilidad de capa"), "layer-visible-on", "Ctrl+,", c(() => this.toggleLayerVisibility(this.doc.active)), has);
        A("importLayer", tr("&Importar desde archivo..."), "layer-import", null, c(() => this.importLayer()), has);
        A("flipLayerH", tr("Voltear &horizontalmente"), "layer-flip-horizontal", null, c(() => this.transformLayer("flipH")), has);
        A("flipLayerV", tr("Voltear &verticalmente"), "layer-flip-vertical", null, c(() => this.transformLayer("flipV")), has);
        A("rotateLayer180", tr("Girar &180°"), "layer-rotate-180", null, c(() => this.transformLayer("180")), has);
        A("layerTop", tr("Ir a capa &superior"), "layer-go-top", "Ctrl+Alt+PgUp", () => this.doc.setActiveIndex(this.doc.layers.length - 1), notTop);
        A("layerAbove", tr("Ir a capa de &encima"), "layer-go-up", "Alt+PgUp", () => this.doc.setActiveIndex(this.doc.active + 1), notTop);
        A("layerBelow", tr("Ir a capa de de&bajo"), "layer-go-down", "Alt+PgDown", () => this.doc.setActiveIndex(this.doc.active - 1), notBottom);
        A("layerBottom", tr("Ir a capa in&ferior"), "layer-go-bottom", "Ctrl+Alt+PgDown", () => this.doc.setActiveIndex(0), notBottom);
        A("layerToTop", tr("Enviar capa al &principio"), "layer-top", null, c(() => this.moveLayerTo(this.doc.active, this.doc.layers.length - 1)), notTop);
        A("layerUp", tr("&Subir capa"), "layer-raise", null, c(() => this.moveLayerTo(this.doc.active, this.doc.active + 1)), notTop);
        A("layerDown", tr("&Bajar capa"), "layer-lower", null, c(() => this.moveLayerTo(this.doc.active, this.doc.active - 1)), notBottom);
        A("layerToBottom", tr("Enviar capa al &final"), "layer-bottom", null, c(() => this.moveLayerTo(this.doc.active, 0)), notBottom);
        A("layerProps", tr("&Propiedades de la capa..."), "layer-properties", "F4", c(() => this.layerProperties()), has);
        // Efectos
        A("repeatEffect", tr("Repetir el último efecto"), null, "Ctrl+F", c(() => this.runEffect(this.lastEffect, true)),
          () => !!this.doc && !!this.lastEffect);
        // Ventanas, configuración y ayuda
        A("resetPanels", tr("&Restablecer la posición de las ventanas"), null, null, () => Object.values(this.panels).forEach((p) => p.reset()));
        A("translucent", tr("Ventanas &translúcidas"), null, null, () => Settings.set("translucentPanels", !Settings.get("translucentPanels")),
          null, () => Settings.get("translucentPanels"));
        A("settings", tr("&Configuración..."), "configure", "Alt+X", () => settingsDialog());
        A("docs", tr("&Documentación"), "help-docs", "F1", openUrl("https://www.getpaint.net/doc/latest/index.html"));
        A("website", tr("Sitio web de &Kaint"), "help-website", null, openUrl(REPOSITORY));
        A("donate", tr("&Donar"), "face-smile", null, openUrl("https://ko-fi.com/opposition"));
        A("forum", tr("&Foro"), "help-forum", null, openUrl("https://forums.getpaint.net/"));
        A("tutorials", tr("&Tutoriales"), "help-tutorials", null, openUrl("https://forums.getpaint.net/forum/20-publishing-only/"));
        A("pluginsSite", tr("&Complementos"), "plugins", null, openUrl("https://www.getpaint.net/redirect/plugins_hm.html"));
        A("feedback", tr("&Enviar comentarios o informe de errores..."), "help-feedback", null, openUrl(`${REPOSITORY}/issues`));
        A("about", tr("&Acerca de"), "help-about", null, () => aboutDialog(VERSION));
    }

    createMenus() {
        const a = (id) => this.actions.get(id);
        const effectEntry = (e) => ({
            text: tr(e.name) + (e.params.length ? "..." : ""), icon: `effect-${e.id}`, shortcut: e.shortcut || "",
            enabled: !!this.doc, run: this.command(() => this.runEffect(e, false)),
        });
        const sorted = (list) => [...list].sort((x, y) => tr(x.name).localeCompare(tr(y.name)));
        this.menuBar.setMenus([
            { title: tr("&Archivo"), entries: () => [a("new"), a("open"), "-", a("save"), a("saveAs"), "-", a("print"), "-", a("close"), a("closeAll")] },
            { title: tr("&Edición"), entries: () => [a("undo"), a("redo"), "-", a("cut"), a("copy"), a("copyMerged"), a("paste"), a("pasteLayer"),
                                                      a("pasteImage"), "-", a("eraseSel"), a("fillSel"), a("invertSel"), a("selectAll"), a("deselect")] },
            { title: tr("&Ver"), entries: () => [a("zoomIn"), a("zoomOut"), a("zoomFit"), a("zoomSel"), a("actualSize"), "-", a("pixelGrid"), "-", a("fullscreen")] },
            { title: tr("&Imagen"), entries: () => [a("crop"), a("resize"), a("canvasSize"), "-", a("flipH"), a("flipV"), "-", a("rotCW"), a("rotCCW"),
                                                     a("rot180"), "-", a("flatten")] },
            { title: tr("&Capas"), entries: () => [a("addLayer"), a("deleteLayer"), a("duplicateLayer"), a("mergeDown"), a("toggleLayer"), "-",
                                                    a("importLayer"), "-", a("flipLayerH"), a("flipLayerV"), a("rotateLayer180"), "-", a("layerTop"),
                                                    a("layerAbove"), a("layerBelow"), a("layerBottom"), "-", a("layerToTop"), a("layerUp"),
                                                    a("layerDown"), a("layerToBottom"), "-", a("layerProps")] },
            { title: tr("A&justes"), entries: () => sorted(Effects.filter((e) => !e.category)).map(effectEntry) },
            { title: tr("Efe&ctos"), entries: () => {
                // "Repetir" solo después de usar un efecto (la web no tiene complementos).
                const out = [];
                if (this.lastEffect)
                    out.push({ text: tr("Repetir «%1»", tr(this.lastEffect.name)), icon: `effect-${this.lastEffect.id}`, shortcut: "Ctrl+F",
                               enabled: !!this.doc, run: this.command(() => this.runEffect(this.lastEffect, true)) }, "-");
                const cats = Object.entries(Categories).sort((x, y) => tr(x[1]).localeCompare(tr(y[1])));
                for (const [key, name] of cats) {
                    const list = Effects.filter((e) => e.category === key);
                    if (list.length)
                        out.push({ text: tr(name), submenu: () => sorted(list).map(effectEntry) });
                }
                return out;
            } },
        ]);
    }

    createToolBar() {
        // La barra principal lleva los iconos dibujados de Kaint (los menús, los PNG).
        const bar = document.getElementById("main-toolbar");
        const groups = [["new", "open", "save"], ["print"], ["cut", "copy", "paste"], ["crop", "deselect"], ["undo", "redo"], ["pixelGrid"]];
        groups.forEach((group, i) => {
            if (i)
                bar.appendChild(el("span", { className: "sep" }));
            for (const id of group)
                bar.appendChild(toolButton(this.actions.get(id), `toolbar-${this.actions.get(id).icon}`, 16));
        });
    }

    createPanels() {
        const ws = document.getElementById("workspace");
        const panelAction = (id, text, iconName, key) => this.action(id, text, iconName, key, () => {
            const p = this.panels[id];
            p.setVisible(!p.visible, true);
            updateActions();
        }, null, () => this.panels[id]?.visible);
        panelAction("tools", tr("Herramientas"), "window-tools", "F5");
        panelAction("history", tr("Historial"), "window-history", "F6");
        panelAction("layers", tr("Capas"), "window-layers", "F7");
        panelAction("colors", tr("Colores"), "window-colors", "F8");
        const closed = () => updateActions();
        this.panels = {
            tools: new FloatingPanel(ws, "tools", tr("Herramientas"), toolsPanel(this), { corner: "tl", onClose: closed }),
            history: new FloatingPanel(ws, "history", tr("Historial"), historyPanel(this), { corner: "tr", size: { w: 210, h: 240 }, onClose: closed }),
            layers: new FloatingPanel(ws, "layers", tr("Capas"), layersPanel(this, ["addLayer", "deleteLayer", "duplicateLayer", "mergeDown",
                                                                                         "layerUp", "layerDown", "layerProps"]),
                                      { corner: "br", size: { w: 230, h: 260 }, onClose: closed }),
            colors: new FloatingPanel(ws, "colors", tr("Colores"), colorsPanel(this), { corner: "bl", size: { w: 230, h: 330 }, onClose: closed }),
        };
        new ResizeObserver(() => Object.values(this.panels).forEach((p) => p.reposition())).observe(ws);
    }

    createCornerButtons() {
        const box = document.getElementById("corner-buttons");
        for (const id of ["tools", "history", "layers", "colors"])
            box.appendChild(this.actionButton(id));
        box.appendChild(el("span", { className: "gap" }));
        box.appendChild(this.actionButton("settings"));
        // Ayuda, como la de paint.js: documentación y web; donar, foro, tutoriales, complementos y
        // comentarios; acerca de.
        const help = el("button", { className: "tbutton", title: tr("Ayuda") }, [icon("help-contents", 16)]);
        help.addEventListener("click", () => {
            const r = help.getBoundingClientRect();
            const a = (id) => this.actions.get(id);
            popupMenu([a("docs"), a("website"), "-", a("donate"), a("forum"), a("tutorials"), a("pluginsSite"), a("feedback"), "-", a("about")],
                      r.right - 260, r.bottom, { minWidth: 260 });
        });
        box.appendChild(help);
    }

    createStatusBar() {
        const bar = document.getElementById("status-bar");
        this.hintLabel = el("span", { className: "hint" });
        this.selLabel = el("span");
        this.posLabel = el("span");
        const item = (iconName, label, tip) => el("span", { className: "item", title: tip }, [icon(iconName, 16), label]);
        this.zoomBox = el("input", { className: "zoom-box", type: "text", list: "zoom-presets" });
        const presets = el("datalist", { id: "zoom-presets" }, [el("option", { value: tr("Ventana") }),
                           ...[...ZoomPresets].reverse().map((z) => el("option", { value: `${Math.round(z * 100)} %` }))]);
        this.zoomBox.addEventListener("change", () => {
            const v = this.zoomBox.value.trim();
            if (v === tr("Ventana")) this.view.zoomToWindow();
            else if (parseFloat(v) > 0) this.view.setZoom(parseFloat(v) / 100);
            this.updateZoomBox();
        });
        this.zoomSlider = el("input", { type: "range", min: 0, max: 1000, step: 1, title: tr("Zum") });
        this.zoomSlider.addEventListener("input", () => {
            const t = Number(this.zoomSlider.value) / 1000;
            this.view.setZoom(Math.exp(Math.log(0.01) + t * (Math.log(64) - Math.log(0.01))));
        });
        bar.append(this.hintLabel, item("status-selection", this.selLabel, tr("Tamaño de la selección")),
                   item("status-cursor", this.posLabel, tr("Posición del cursor")), el("span", { textContent: "px", style: { padding: "0 6px" } }),
                   this.zoomBox, presets, this.actionButton("zoomFit"), this.actionButton("zoomOut"), this.zoomSlider, this.actionButton("zoomIn"));
    }

    showHint(text) {
        if (this.hintLabel)
            this.hintLabel.textContent = text;
    }

    updateStatus() {
        if (!this.selLabel)
            return;
        const doc = this.doc;
        if (!doc) {
            this.selLabel.textContent = "";
            return;
        }
        const b = doc.selection.bounds();
        this.selLabel.textContent = b ? `${b.w} × ${b.h}` : `${doc.width} × ${doc.height}`;
    }

    updateCursorPos(e) {
        this.posLabel.textContent = e && this.doc ? `${Math.floor(e.x)}, ${Math.floor(e.y)}` : "";
    }

    updateZoomBox() {
        if (document.activeElement !== this.zoomBox)
            this.zoomBox.value = `${Math.round(this.view.zoom * 100)} %`;
        const t = (Math.log(this.view.zoom) - Math.log(0.01)) / (Math.log(64) - Math.log(0.01));
        this.zoomSlider.value = String(Math.round(t * 1000));
        updateActions();
    }

    // --- Teclado ---------------------------------------------------------------------------

    installKeyboard() {
        document.addEventListener("keydown", (e) => {
            if (dialogOpen())
                return;
            const target = e.target;
            const typing = target.matches?.("input, select, textarea") && target.id !== "view";
            if (typing)
                return;
            const tool = this.tools.current;
            if (e.key === " " && !tool?.wantsKeyboard()) {
                if (!this.view.spaceDown) {
                    this.view.spaceDown = true;
                    this.view.updateCursor();
                }
                e.preventDefault();
                return;
            }
            if (tool?.onKeyDown(e)) {
                e.preventDefault();
                return;
            }
            const combo = keyCombo(e);
            if (!combo)
                return;
            for (const a of this.actions.values())
                if (a.shortcuts.includes(combo)) {
                    e.preventDefault();
                    a.trigger();
                    updateActions();
                    return;
                }
            // Atajos de los ajustes y efectos (Ctrl+Mayús+I, Ctrl+L...).
            const effect = Effects.find((fx) => fx.shortcut === combo);
            if (effect && this.doc) {
                e.preventDefault();
                this.command(() => this.runEffect(effect, false))();
                return;
            }
            // Alt + letra: el menú con esa tecla de acceso.
            if (e.altKey && !e.ctrlKey && e.key.length === 1 && this.menuBar.openByMnemonic(e.key)) {
                e.preventDefault();
                return;
            }
            if (!e.ctrlKey && !e.altKey && !e.metaKey && e.key.length === 1) {
                const k = e.key.toLowerCase();
                if (k === "x") AppState.swapColors();
                else if (k === "c") AppState.setSecondaryActive(!AppState.secondaryActive);
                else if (k === "[" || k === "]") {
                    const s = AppState.tools.brushSize;
                    AppState.updateTools({ brushSize: Math.max(1, Math.min(2000, k === "]" ? Math.ceil(s * 1.1 + 0.5) : Math.floor(s / 1.1))) });
                } else if (!this.tools.selectByLetter(k)) return;
                e.preventDefault();
            }
        });
        document.addEventListener("keyup", (e) => {
            if (e.key === " " && this.view.spaceDown) {
                this.view.spaceDown = false;
                this.view.updateCursor();
            }
        });
    }

    // --- Historial ---------------------------------------------------------------------------

    undo() {
        const tool = this.tools.current;
        if (tool?.isBusy()) {
            tool.cancel();
            updateActions();
            return;
        }
        this.doc?.undo();
    }

    redo() {
        this.tools.current?.finish();
        this.doc?.redo();
    }

    jumpHistory(index) {
        if (!this.doc)
            return;
        this.tools.current?.finish();
        this.doc.goTo(Math.min(index, this.doc.history.steps.length - 1));
    }

    // --- Archivo ---------------------------------------------------------------------------

    async newImage() {
        const size = this.clipboard ? { w: this.clipboard.width, h: this.clipboard.height } : this.doc ? { w: this.doc.width, h: this.doc.height } : { w: 800, h: 600 };
        const r = await newImageDialog(size.w, size.h);
        if (!r)
            return;
        const s = AppState.secondary;
        const bg = r.background === "white" ? "#ffffff" : r.background === "secondary" ? `rgba(${s.r},${s.g},${s.b},${s.a / 255})` : null;
        this.addDocument(new Document(r.width, r.height, { background: bg }));
    }

    pickFiles(multiple = true) {
        return new Promise((resolve) => {
            const input = el("input", { type: "file", accept: OpenAccept, multiple });
            input.addEventListener("change", () => resolve([...input.files]));
            input.click();
        });
    }

    async open() {
        if (window.showOpenFilePicker) {
            try {
                const handles = await window.showOpenFilePicker({ multiple: true, types: [{ description: "Images", accept: { "image/*": [".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp", ".avif", ".ico", ".ora"] } }] });
                for (const h of handles)
                    await this.openFile(await h.getFile(), h);
                return;
            } catch (e) {
                if (e.name === "AbortError")
                    return;
            }
        }
        for (const f of await this.pickFiles())
            await this.openFile(f);
    }

    async openFile(file, handle = null) {
        try {
            const doc = await openFile(file);
            doc.fileHandle = handle;
            doc.markSaved();
            this.addDocument(doc);
        } catch {
            await message(tr("Abrir"), tr("No se pudo abrir «%1».", file.name));
        }
    }

    // Guarda en el archivo abierto (si el navegador lo permite) o lo descarga.
    async save(doc, saveAs = false) {
        if (!doc)
            return false;
        this.tools.current?.finish();
        let format = formatFor(doc.fileName || "imagen.png");
        let quality = doc.saveQuality ?? 95;
        if (doc.layers.length > 1 && !format.layered && !doc.fileHandle)
            format = SaveFormats.find((f) => f.layered);
        if (saveAs || !doc.fileHandle) {
            if (window.showSaveFilePicker) {
                try {
                    const handle = await window.showSaveFilePicker({
                        suggestedName: `${baseName(doc.displayName())}.${format.ext}`,
                        types: SaveFormats.map((f) => ({ description: f.name, accept: { [f.mime]: [`.${f.ext}`] } })),
                    });
                    doc.fileHandle = handle;
                    doc.fileName = handle.name;
                    format = formatFor(handle.name);
                } catch (e) {
                    if (e.name === "AbortError")
                        return false;
                }
            }
            if (!doc.fileHandle) {
                const r = await saveOptionsDialog(SaveFormats, format, quality);
                if (!r)
                    return false;
                format = r.format;
                quality = r.quality;
                doc.saveQuality = quality;
                const blob = await encode(doc, format, quality);
                doc.fileName = `${baseName(doc.displayName())}.${format.ext}`;
                download(blob, doc.fileName);
                doc.markSaved();
                this.rebuildTabs();
                return true;
            }
        }
        format = formatFor(doc.fileHandle.name);
        const blob = await encode(doc, format, quality);
        const writable = await doc.fileHandle.createWritable();
        await writable.write(blob);
        await writable.close();
        doc.markSaved();
        this.rebuildTabs();
        return true;
    }

    print() {
        const url = this.doc.flattened().toDataURL("image/png");
        const frame = el("iframe", { style: { position: "fixed", width: "0", height: "0", border: "0" } });
        document.body.appendChild(frame);
        frame.contentDocument.write(`<img src="${url}" style="max-width:100%" onload="window.print()">`);
        frame.contentDocument.close();
        setTimeout(() => frame.remove(), 60000);
    }

    // --- Portapapeles ------------------------------------------------------------------------

    installClipboard() {
        // Ctrl+V llega como evento "paste" (sin pedir permiso): se usa la imagen que traiga.
        document.addEventListener("paste", async (e) => {
            if (dialogOpen() || e.target.matches?.("input, textarea"))
                return;
            const item = [...(e.clipboardData?.items || [])].find((i) => i.type.startsWith("image/"));
            if (!item)
                return;
            e.preventDefault();
            const bitmap = await createImageBitmap(item.getAsFile());
            this.paste(bitmap, this.pendingPasteMode || "layer");
            this.pendingPasteMode = null;
        });
    }

    selectedPixels(merged) {
        const doc = this.doc, sel = doc.selection;
        const b = sel.bounds() || { x: 0, y: 0, w: doc.width, h: doc.height };
        const source = merged ? doc.flattened() : doc.activeLayer.canvas;
        const out = createCanvas(b.w, b.h), ctx = context(out);
        ctx.drawImage(source, -b.x, -b.y);
        if (!sel.isEmpty()) {
            ctx.globalCompositeOperation = "destination-in";
            ctx.drawImage(sel.maskCanvas(), -b.x, -b.y);
        }
        return out;
    }

    async copy(merged) {
        const canvas = this.selectedPixels(merged);
        this.clipboard = canvas;
        try {
            const blob = await new Promise((r) => canvas.toBlob(r, "image/png"));
            await navigator.clipboard.write([new ClipboardItem({ "image/png": blob })]);
        } catch {
            // Sin acceso al portapapeles del sistema: queda el de Kaint.
        }
    }

    async cut() {
        await this.copy(false);
        this.eraseSelection(tr("Cortar"), "edit-cut");
    }

    // Pegar desde el menú: el portapapeles del sistema si el navegador lo deja leer; si no, el
    // de Kaint.
    async pasteFromClipboard(mode) {
        try {
            for (const item of await navigator.clipboard.read()) {
                const type = item.types.find((t) => t.startsWith("image/"));
                if (type) {
                    this.paste(await createImageBitmap(await item.getType(type)), mode);
                    return;
                }
            }
        } catch {
            // Sin permiso: se usa la copia interna.
        }
        if (this.clipboard)
            this.paste(this.clipboard, mode);
        else
            await message(tr("Pegar"), tr("No se pudo leer el portapapeles. Pega con Ctrl+V."), "dialog-warning");
    }

    async paste(image, mode) {
        if (mode === "newImage" || !this.doc) {
            const doc = new Document(image.width, image.height, { background: null, historyName: tr("Pegar en una imagen nueva"), historyIcon: "edit-paste" });
            doc.layers[0].ctx.drawImage(image, 0, 0);
            this.addDocument(doc);
            return;
        }
        const doc = this.doc;
        if (image.width > doc.width || image.height > doc.height) {
            const dlg = new TaskDialog(tr("Pegar"), "edit-paste", tr("La imagen que está pegando es más grande que el tamaño del lienzo. ¿Qué desea hacer?"));
            const expand = dlg.addChoice("transform-crop-and-resize", tr("Ampliar lienzo"), tr("Expande automáticamente el lienzo para que la imagen quepa."), true);
            const keep = dlg.addChoice("transform-crop", tr("Mantener el tamaño del lienzo"),
                                       tr("No expande el lienzo. Tendrá que mover la imagen para asegurarse de que la parte que desea se encuentra en los límites del lienzo."));
            dlg.addChoice("dialog-cancel", tr("Cancelar"), tr("Cancela la acción de pegado."));
            const answer = await dlg.exec();
            if (answer === expand)
                this.resizeCanvas(Math.max(doc.width, image.width), Math.max(doc.height, image.height), [0, 0], tr("Ampliar lienzo"));
            else if (answer !== keep)
                return;
        }
        if (mode === "newLayer")
            this.addLayer();
        // Como Paint.NET: arriba a la izquierda de lo que se ve y con Mover píxeles seleccionados.
        const tl = this.view.viewToImage(0, 0);
        const x = Math.max(0, Math.min(doc.width - image.width, Math.round(tl.x)));
        const y = Math.max(0, Math.min(doc.height - image.height, Math.round(tl.y)));
        this.tools.select("moveselectedpixels");
        this.tools.current.startFloating(image, Math.max(0, x), Math.max(0, y), tr("Pegar"));
    }

    // --- Selección ---------------------------------------------------------------------------

    eraseSelection(name = tr("Borrar la selección"), iconName = "edit-erase-selection") {
        const doc = this.doc, layer = doc.activeLayer, sel = doc.selection;
        const r = sel.bounds() || doc.rect;
        const before = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const selBefore = doc.selection;
        layer.ctx.save();
        layer.ctx.globalCompositeOperation = "destination-out";
        layer.ctx.drawImage(sel.maskCanvas(), 0, 0);
        layer.ctx.restore();
        const after = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const none = Selection.none(doc.width, doc.height);
        const apply = (data, s) => {
            layer.ctx.putImageData(data, r.x, r.y);
            doc.setSelection(s);
            doc.pixelsChanged(layer, r);
        };
        apply(after, none);
        doc.push({ name, icon: iconName, undo: () => apply(before, selBefore), redo: () => apply(after, none) });
    }

    fillSelection() {
        const doc = this.doc, layer = doc.activeLayer, sel = doc.selection, r = sel.bounds();
        const before = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const fill = createCanvas(doc.width, doc.height), f = context(fill), c = AppState.primary;
        f.fillStyle = `rgba(${c.r},${c.g},${c.b},${c.a / 255})`;
        f.fillRect(r.x, r.y, r.w, r.h);
        f.globalCompositeOperation = "destination-in";
        f.drawImage(sel.maskCanvas(), 0, 0);
        layer.ctx.drawImage(fill, 0, 0);
        doc.pixelsChanged(layer, r);
        doc.pushPixels(layer, r, before, tr("Rellenar la selección"), "edit-fill-selection");
    }

    invertSelection() {
        const before = this.doc.selection;
        this.doc.setSelection(before.inverted());
        this.doc.pushSelection(before, tr("Invertir la selección"), "edit-select-invert");
    }

    selectAll() {
        const doc = this.doc, before = doc.selection;
        doc.setSelection(Selection.rect(doc.width, doc.height, 0, 0, doc.width, doc.height));
        doc.pushSelection(before, tr("Seleccionar todo"), "edit-select-all");
    }

    deselect() {
        const doc = this.doc, before = doc.selection;
        doc.setSelection(Selection.none(doc.width, doc.height));
        doc.pushSelection(before, tr("Anular la selección"), "edit-select-none");
    }

    // --- Imagen ------------------------------------------------------------------------------

    cropToSelection() {
        const doc = this.doc, b = doc.selection.bounds(), before = doc.snapshot();
        for (const layer of doc.layers) {
            const c = createCanvas(b.w, b.h), ctx = context(c);
            ctx.drawImage(layer.canvas, -b.x, -b.y);
            if (!doc.selection.rect) {
                ctx.globalCompositeOperation = "destination-in";
                ctx.drawImage(doc.selection.maskCanvas(), -b.x, -b.y);
            }
            layer.replaceCanvas(c);
        }
        doc.width = b.w;
        doc.height = b.h;
        doc.selection = Selection.none(b.w, b.h);
        doc.emit("size");
        doc.emit("selection");
        doc.layersChanged();
        doc.pushSnapshot(before, tr("Recortar según la selección"), "transform-crop");
        this.view.zoomToWindow();
    }

    async resizeImage() {
        const doc = this.doc, r = await resizeDialog(doc.width, doc.height);
        if (!r || (r.width === doc.width && r.height === doc.height))
            return;
        const before = doc.snapshot();
        for (const layer of doc.layers) {
            const c = createCanvas(r.width, r.height), ctx = context(c);
            ctx.imageSmoothingEnabled = r.method !== "nearest";
            ctx.imageSmoothingQuality = r.method === "high" ? "high" : "low";
            ctx.drawImage(layer.canvas, 0, 0, r.width, r.height);
            layer.replaceCanvas(c);
        }
        doc.width = r.width;
        doc.height = r.height;
        doc.selection = Selection.none(r.width, r.height);
        doc.emit("size");
        doc.emit("selection");
        doc.layersChanged();
        doc.pushSnapshot(before, tr("Cambiar el tamaño"), "transform-scale");
        this.view.zoomToWindow();
    }

    async canvasSize() {
        const doc = this.doc, r = await canvasSizeDialog(doc.width, doc.height);
        if (r)
            this.resizeCanvas(r.width, r.height, r.anchor, tr("Tamaño del lienzo"));
    }

    // El lienzo de otro tamaño; anchor [0..2, 0..2] dice dónde queda la imagen.
    resizeCanvas(width, height, anchor, name) {
        const doc = this.doc, before = doc.snapshot();
        const dx = Math.round(((width - doc.width) * anchor[0]) / 2), dy = Math.round(((height - doc.height) * anchor[1]) / 2);
        doc.layers.forEach((layer, i) => {
            const c = createCanvas(width, height), ctx = context(c);
            // El fondo crece con el color secundario, como en Paint.NET.
            if (i === 0) {
                const s = AppState.secondary;
                ctx.fillStyle = `rgba(${s.r},${s.g},${s.b},${s.a / 255})`;
                ctx.fillRect(0, 0, width, height);
                ctx.clearRect(dx, dy, doc.width, doc.height);
            }
            ctx.drawImage(layer.canvas, dx, dy);
            layer.replaceCanvas(c);
        });
        doc.selection = doc.selection.remapped(width, height, dx, dy);
        doc.width = width;
        doc.height = height;
        doc.emit("size");
        doc.emit("selection");
        doc.layersChanged();
        doc.pushSnapshot(before, name, "transform-crop-and-resize");
        this.view.zoomToWindow();
    }

    // Voltear o girar todas las capas (y la selección).
    transformImage(kind) {
        const doc = this.doc, before = doc.snapshot();
        const swap = kind === "cw" || kind === "ccw";
        const w = doc.width, h = doc.height, nw = swap ? h : w, nh = swap ? w : h;
        const m = matrixFor(kind, w, h);
        for (const layer of doc.layers) {
            const c = createCanvas(nw, nh), ctx = context(c);
            ctx.setTransform(m);
            ctx.drawImage(layer.canvas, 0, 0);
            layer.replaceCanvas(c);
        }
        doc.selection = doc.selection.isEmpty() ? Selection.none(nw, nh) : transformSelection(doc.selection, m, nw, nh);
        doc.width = nw;
        doc.height = nh;
        if (swap)
            doc.emit("size");
        doc.emit("selection");
        doc.layersChanged();
        const names = { flipH: ["Voltear horizontalmente", "object-flip-horizontal"], flipV: ["Voltear verticalmente", "object-flip-vertical"],
                        cw: ["Girar 90° en sentido horario", "object-rotate-right"], ccw: ["Girar 90° en sentido antihorario", "object-rotate-left"],
                        180: ["Girar 180°", "image-rotate-180"] };
        doc.pushSnapshot(before, tr(names[kind][0]), names[kind][1]);
        if (swap)
            this.view.zoomToWindow();
    }

    transformLayer(kind) {
        const doc = this.doc, layer = doc.activeLayer;
        const before = layer.ctx.getImageData(0, 0, doc.width, doc.height);
        const copy = cloneCanvas(layer.canvas);
        layer.ctx.save();
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.setTransform(matrixFor(kind, doc.width, doc.height));
        layer.ctx.drawImage(copy, 0, 0);
        layer.ctx.restore();
        doc.pixelsChanged(layer, null);
        const names = { flipH: ["Voltear horizontalmente", "layer-flip-horizontal"], flipV: ["Voltear verticalmente", "layer-flip-vertical"],
                        180: ["Girar 180°", "layer-rotate-180"] };
        doc.pushPixels(layer, doc.rect, before, tr(names[kind][0]), names[kind][1]);
    }

    flatten() {
        const doc = this.doc, before = doc.snapshot(), flat = doc.flattened();
        const layer = doc.layers[0];
        layer.ctx.clearRect(0, 0, doc.width, doc.height);
        layer.ctx.drawImage(flat, 0, 0);
        layer.setProps({ visible: true, opacity: 255, blend: "normal" });
        doc.layers = [layer];
        doc.active = 0;
        doc.layersChanged();
        doc.emit("active");
        doc.pushSnapshot(before, tr("Acoplar"), "image-flatten");
    }

    // --- Capas -------------------------------------------------------------------------------

    addLayer() {
        const doc = this.doc, before = doc.layerState();
        let n = doc.layers.length + 1;
        while (doc.layers.some((l) => l.name === tr("Capa %1", n)))
            ++n;
        doc.layers.splice(doc.active + 1, 0, new Layer(doc.width, doc.height, tr("Capa %1", n)));
        doc.active++;
        doc.layersChanged();
        doc.emit("active");
        doc.pushLayers(before, tr("Agregar nueva capa"), "layer-new");
    }

    deleteLayer() {
        const doc = this.doc, before = doc.layerState();
        doc.layers.splice(doc.active, 1);
        doc.active = Math.max(0, doc.active - 1);
        doc.layersChanged();
        doc.emit("active");
        doc.pushLayers(before, tr("Eliminar la capa"), "layer-delete");
    }

    duplicateLayer() {
        const doc = this.doc, before = doc.layerState(), layer = doc.activeLayer;
        doc.layers.splice(doc.active + 1, 0, layer.duplicate(tr("%1 copia", layer.name)));
        doc.active++;
        doc.layersChanged();
        doc.emit("active");
        doc.pushLayers(before, tr("Duplicar la capa"), "layer-duplicate");
    }

    mergeDown() {
        const doc = this.doc, before = doc.snapshot();
        const top = doc.layers[doc.active], below = doc.layers[doc.active - 1];
        if (top.visible) {
            const merged = cloneCanvas(below.canvas);
            drawBlended(context(merged), top.canvas, top.blend, top.opacity);
            below.replaceCanvas(merged);
        }
        doc.layers.splice(doc.active, 1);
        doc.active--;
        doc.layersChanged();
        doc.emit("active");
        doc.pushSnapshot(before, tr("Combinar capa con la inferior"), "layer-merge-down");
    }

    moveLayerTo(from, to) {
        const doc = this.doc;
        to = Math.max(0, Math.min(doc.layers.length - 1, to));
        if (from === to)
            return;
        const before = doc.layerState();
        const [layer] = doc.layers.splice(from, 1);
        doc.layers.splice(to, 0, layer);
        doc.active = to;
        doc.layersChanged();
        doc.emit("active");
        doc.pushLayers(before, to > from ? tr("Subir capa") : tr("Bajar capa"), to > from ? "layer-raise" : "layer-lower");
    }

    // Ctrl+clic en la casilla: solo esa capa visible (o todas otra vez).
    toggleLayerVisibility(index, solo = false) {
        const doc = this.doc, before = doc.layerState(), layer = doc.layers[index];
        if (solo) {
            const others = doc.layers.filter((l) => l !== layer);
            const isolate = others.some((l) => l.visible) || !layer.visible;
            doc.layers.forEach((l) => (l.visible = l === layer || !isolate));
        } else {
            layer.visible = !layer.visible;
        }
        doc.layersChanged();
        doc.pushLayers(before, solo ? tr("Visibilidad de las capas") : tr("Visibilidad de la capa"), "layer-visible-on");
    }

    async layerProperties() {
        const doc = this.doc, layer = doc.activeLayer, old = layer.props();
        const result = await layerPropertiesDialog(old, (p) => {
            layer.setProps(p);
            doc.layersChanged();
        });
        layer.setProps(old);
        if (!result) {
            doc.layersChanged();
            return;
        }
        const before = doc.layerState();
        layer.setProps(result);
        doc.layersChanged();
        doc.pushLayers(before, tr("Propiedades de la capa"), "layer-properties");
    }

    async importLayer() {
        const [file] = await this.pickFiles(false);
        if (!file)
            return;
        try {
            const imported = await openFile(file);
            this.paste(imported.flattened(), "newLayer");
            this.doc.activeLayer.name = baseName(file.name);
        } catch {
            await message(tr("Importar desde archivo"), tr("No se pudo abrir «%1».", file.name));
        }
    }

    // --- Efectos -------------------------------------------------------------------------------

    async runEffect(effect, repeat) {
        const doc = this.doc;
        if (!effect || !doc)
            return;
        let values = this.effectValues[effect.id];
        if (!repeat && effect.params.length) {
            values = await effectDialog(this, effect, values);
            if (!values)
                return;
        }
        values = values || {};
        const full = { ...Object.fromEntries(effect.params.map((p) => [p.key, p.def])), ...values };
        const { rect, data } = renderEffect(doc, effect, full);
        const layer = doc.activeLayer;
        const before = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
        layer.ctx.putImageData(data, rect.x, rect.y);
        doc.pixelsChanged(layer, rect);
        doc.pushPixels(layer, rect, before, tr(effect.name), `effect-${effect.id}`);
        this.effectValues[effect.id] = full;
        this.lastEffect = effect;
        updateActions();
    }

    // --- Soltar archivos -----------------------------------------------------------------------

    installDrop() {
        const app = document.getElementById("app");
        const hint = document.querySelector(".drop-hint");
        hint.textContent = tr("Arrastra aquí una imagen o ábrela desde Archivo.");
        let depth = 0;
        app.addEventListener("dragenter", (e) => {
            if (![...(e.dataTransfer?.types || [])].includes("Files"))
                return;
            depth++;
            app.classList.add("dragging-file");
        });
        app.addEventListener("dragleave", () => {
            if (--depth <= 0) {
                depth = 0;
                app.classList.remove("dragging-file");
            }
        });
        app.addEventListener("dragover", (e) => e.preventDefault());
        app.addEventListener("drop", async (e) => {
            e.preventDefault();
            depth = 0;
            app.classList.remove("dragging-file");
            for (const file of e.dataTransfer.files)
                await this.openFile(file);
        });
    }
}

// Matriz de voltear o girar una imagen de w × h.
function matrixFor(kind, w, h) {
    switch (kind) {
    case "flipH": return new DOMMatrix([-1, 0, 0, 1, w, 0]);
    case "flipV": return new DOMMatrix([1, 0, 0, -1, 0, h]);
    case "cw": return new DOMMatrix([0, 1, -1, 0, h, 0]);
    case "ccw": return new DOMMatrix([0, -1, 1, 0, 0, w]);
    default: return new DOMMatrix([-1, 0, 0, -1, w, h]);
    }
}

function transformSelection(sel, m, nw, nh) {
    const canvas = createCanvas(nw, nh), ctx = context(canvas);
    ctx.setTransform(m);
    ctx.drawImage(sel.maskCanvas(), 0, 0);
    return Selection.fromAlpha(canvas, true);
}
