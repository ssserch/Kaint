// Ventana principal (src/app/MainWindow.cpp de Kaint): imágenes abiertas, acciones y menús,
// barras, ventanas flotantes, barra de estado, teclado, portapapeles y archivos.
import { AppState } from "../AppState.js";
import { Document, Layer } from "../core/Document.js";
import { Effects, Categories, defaultValues, findEffect } from "../core/Effects.js";
import { animationFrames, baseName, download, embeddedProfile, encode, flattenedImage, formatFor, openFile, OpenAccept, OpenExtensions, SaveFormats } from "../core/ImageIO.js";
import { defaultSaveOptions, hasOptions as hasSaveOptions } from "../core/Encode.js";
import { Plugins, kaintValues, pluginValues } from "../plugins/Plugins.js";
import { cloneCanvas, context, createCanvas, drawBlended, thumbnail } from "../core/Raster.js";
import { scaleImage } from "../core/Resample.js";
import { transformPixels } from "../core/ColorProfiles.js";
import { Selection, SelectionMode } from "../core/Selection.js";
import { Recovery } from "../Recovery.js";
import { Settings } from "../Settings.js";
import { REPOSITORY, VERSION } from "../version.js";
import { currentLanguage, tr, trn } from "../i18n.js";
import { Action, keyCombo, toolButton, updateActions } from "./Actions.js";
import { applySetting as applyAnimations } from "./Animations.js";
import { CanvasView, ZoomPresets } from "./Canvas.js";
import { applyThemeColors } from "./Themes.js";
import {
    aboutDialog, animationDialog, canvasSizeDialog, colorProfileDialog, Dialog, dialogOpen, el, layerPropertiesDialog, message, newImageDialog, promptNumber, resizeDialog,
    saveAsDialog, settingsDialog, TaskDialog,
} from "./Dialogs.js";
import { computeEffect, effectDialog, effectInput, renderEffect } from "./EffectDialog.js";
import { icon, iconUrl, refreshIcons, setActiveColors, unsavedStar } from "./Icons.js";
import { MenuBar, popupMenu } from "./Menu.js";
import { colorsPanel, FloatingPanel, historyPanel, layersPanel, toolsPanel } from "./Panels.js";
import { ToolOptions } from "./ToolOptions.js";
import { saveConfigDialog } from "./SaveDialog.js";
import { formatLength, pixelsPerUnit, unitSuffix } from "./Units.js";
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
        this.applyShortcuts();
        this.menuBar = new MenuBar(document.getElementById("menubar"));
        this.createMenus();
        this.createToolBar();
        this.createPanels();
        this.createCornerButtons();
        this.createImageList();
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
        // Extremo borrador del lápiz: la Goma y, al soltar, la herramienta de antes.
        this.view.addEventListener("pen-eraser", (e) => {
            if (e.detail) {
                this.view.penEraser = this.tools.current.id;
                this.tools.select("eraser");
            } else {
                const back = this.view.penEraser;
                this.view.penEraser = null;
                this.tools.select(back);
            }
        });
        AppState.addEventListener("colors", () => this.updateToolIconColors());
        window.addEventListener("kaint-settings", () => this.applySettings());
        // La miniatura del cursor de Formas sigue a la forma elegida y el contorno de los pinceles,
        // a su tamaño.
        AppState.addEventListener("tools", () => {
            if (this.tools.current?.id === "shapes" || this.tools.current?.brushDiameter)
                this.view.updateCursor();
        });
        window.addEventListener("kaint-cursor", () => this.view.updateCursor());
        window.addEventListener("kaint-iconpack", () => {
            this.view.updateCursor();
            this.refreshHint();
        });
        window.addEventListener("beforeunload", (e) => {
            if (this.docs.some((d) => d.isModified())) {
                e.preventDefault();
                e.returnValue = "";
            }
        });
        this.applySettings();
        this.tools.select(Settings.get("startTool") || Settings.get("lastTool") || "paintbrush");
        if (!this.tools.current)
            this.tools.select("paintbrush");
        AppState.currentTool = this.tools.current.id;
        this.tools.addEventListener("changed", () => {
            AppState.currentTool = this.tools.current.id;
            Settings.set("lastTool", this.tools.current.id);
        });
        // Copias de recuperación y archivos de la sesión (estos, después de restoreSession()).
        Recovery.start();
        this.sessionReady = false;
        this.addDocument(new Document(800, 600));
        updateActions();
    }

    // --- Tema y configuración --------------------------------------------------------------

    applyTheme() {
        const root = document.documentElement;
        const key = JSON.stringify([Settings.get("colorScheme"), Settings.get("customTheme"), Settings.get("canvasBorderLight"),
                                    Settings.get("canvasBorderDark"), matchMedia("(prefers-color-scheme: dark)").matches]);
        if (this.themeKey !== key) {
            this.themeKey = key;
            applyThemeColors(root, Settings.get("colorScheme"), matchMedia("(prefers-color-scheme: dark)").matches);
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
        // Ventanas como las de paint.js (Windows 10) o compactas, con el borde elegido.
        const root = document.documentElement;
        root.classList.toggle("win10", Settings.get("panelStyle") === 0);
        // Aspecto de Paint.NET: más compacto, ventanas sin borde... (solo lo visual).
        root.classList.toggle("pdn", Settings.get("uiStyle") === 1);
        // Letras de acceso de los menús: siempre subrayadas o solo con Alt.
        root.classList.toggle("mnemonics", !!Settings.get("menuMnemonics"));
        root.style.setProperty("--window-border", `${Math.max(1, Math.min(12, Number(Settings.get("panelBorder")) || 1))}px`);
        this.updateToolIconColors();
        this.view.update();
        updateActions();
        // Copias de recuperación cada tantos minutos y límite de memoria del historial.
        const minutes = Math.max(0, Math.min(120, Number(Settings.get("autosaveMinutes")) || 0));
        if (this.autosaveMinutes !== minutes) {
            this.autosaveMinutes = minutes;
            clearInterval(this.autosaveTimer);
            this.autosaveTimer = minutes > 0 ? setInterval(() => this.autosave(), minutes * 60 * 1000) : null;
        }
        this.checkHistoryMemory();
        if (this.reopenLast !== !!Settings.get("reopenLast")) {
            this.reopenLast = !!Settings.get("reopenLast");
            this.rememberSession();
        }
    }

    updateToolIconColors() {
        const on = Settings.get("coloredToolIcons");
        setActiveColors(on ? AppState.primary : null, on ? AppState.secondary : null);
        // Los cursores del pincel, el cubo... llevan los colores activos.
        this.view?.updateCursor();
    }

    // --- Documentos ------------------------------------------------------------------------

    addDocument(doc) {
        this.docs.push(doc);
        const forward = (type) => doc.addEventListener(type, (e) => {
            if (doc === this.doc)
                this.dispatchEvent(new CustomEvent(type, { detail: e.detail }));
            if (type === "pixels" || type === "modified")
                this.updateTab(doc);
            if (type === "history" && doc === this.doc) {
                this.syncToolWithHistory();
                clearTimeout(this.historyTimer);
                this.historyTimer = setTimeout(() => this.checkHistoryMemory(), 500);
            }
            if (type === "history" || type === "selection" || type === "active" || type === "layers" || type === "size")
                updateActions();
            if (type === "selection" || type === "size")
                this.updateStatus();
        });
        ["history", "layers", "pixels", "active", "selection", "size", "modified"].forEach(forward);
        this.setCurrent(doc);
        this.rebuildTabs();
        this.rememberSession();
    }

    setCurrent(doc) {
        if (doc === this.doc)
            return;
        this.tools.finishCurrent();
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
        this.tools.finishCurrent();
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
        this.rememberSession();
        return true;
    }

    // --- Sesión, recuperación e historial ---------------------------------------------------

    // Los archivos abiertos, para volver a abrirlos la próxima vez (como session/files en Kaint).
    rememberSession() {
        if (this.sessionReady)
            Recovery.rememberSession(Settings.get("reopenLast") ? this.docs : []);
    }

    // "Volver a abrir las imágenes de la última sesión" (MainWindow::restoreSessionOrNew): solo los
    // archivos que el navegador deja volver a abrir; si pide permiso, se pregunta antes.
    async restoreSession() {
        try {
            if (!Settings.get("reopenLast") || !window.showOpenFilePicker)
                return;
            const handles = await Recovery.lastSession();
            if (!handles.length)
                return;
            const blank = this.doc, granted = new Set(), pending = [];
            for (const h of handles) {
                try {
                    if ((await h.queryPermission({ mode: "read" })) === "granted")
                        granted.add(h);
                    else
                        pending.push(h);
                } catch {
                    // El archivo ya no está.
                }
            }
            if (pending.length) {
                const dlg = new TaskDialog(tr("Volver a abrir las imágenes"), "document-open",
                                           tr("El navegador pide permiso para volver a abrir las imágenes de la última sesión:\n%1",
                                              pending.map((h) => `• ${h.name}`).join("\n")));
                const reopen = dlg.addChoice("document-open", tr("Volver a abrir"), tr("Pide permiso al navegador para abrirlas."), true);
                dlg.addChoice("dialog-cancel", tr("Ahora no"), tr("Empezar con una imagen nueva."));
                if ((await dlg.exec()) === reopen) {
                    for (const h of pending) {
                        try {
                            if ((await h.requestPermission({ mode: "read" })) === "granted")
                                granted.add(h);
                        } catch {
                            // Sin permiso.
                        }
                    }
                }
            }
            const before = this.docs.length;
            for (const h of handles.filter((x) => granted.has(x))) {
                try {
                    await this.openFile(await h.getFile(), h);
                } catch {
                    // El archivo ya no está.
                }
            }
            if (this.docs.length > before && blank && this.docs.includes(blank) && !blank.isModified() && blank.history.index === 0)
                await this.closeDocument(blank, false);
        } finally {
            this.sessionReady = true;
            this.rememberSession();
        }
    }

    // Kaint se cerró de golpe: se ofrecen las copias de recuperación (MainWindow::offerRecovery).
    async offerRecovery() {
        const entries = await Recovery.orphaned();
        if (!entries.length)
            return;
        const dlg = new TaskDialog(tr("Recuperar imágenes"), "edit-undo",
                                   tr("Kaint no se cerró correctamente. Se pueden recuperar estas imágenes con cambios sin guardar:\n%1",
                                      entries.map((e) => `• ${e.name}`).join("\n")));
        const recover = dlg.addChoice("document-open", tr("Recuperar"), tr("Abre las imágenes tal como estaban en la última copia de recuperación."), true);
        dlg.addChoice("edit-delete", tr("Descartar"), tr("Borra las copias de recuperación."));
        if ((await dlg.exec()) === recover) {
            const blank = this.doc;
            for (const e of entries) {
                try {
                    this.addDocument(await Recovery.restore(e));
                } catch {
                    // Copia dañada.
                }
            }
            if (this.docs.length > 1 && blank && !blank.isModified() && blank.history.index === 0 && !blank.fileName)
                await this.closeDocument(blank, false);
        }
        await Recovery.discard(entries);
        this.autosave();
    }

    autosave() {
        return Recovery.save(this.docs);
    }

    // Si el historial de la imagen ocupa más de lo permitido, se descartan los pasos más antiguos
    // (MainWindow::checkHistoryMemory).
    checkHistoryMemory() {
        const mb = Math.max(0, Number(Settings.get("historyLimitMB")) || 0);
        if (!this.doc || mb <= 0)
            return;
        const dropped = this.doc.trimHistory(mb * 1024 * 1024);
        if (dropped > 0)
            this.showMessage(trn("El historial superaba %1 MB: se descartaron los %n pasos más antiguos.", dropped, mb), 8000);
    }

    // Un aviso en la barra de estado durante 'ms' milisegundos (QStatusBar::showMessage).
    showMessage(text, ms = 5000) {
        clearTimeout(this.messageTimer);
        this.statusMessage = text;
        this.refreshHint();
        this.messageTimer = setTimeout(() => {
            this.statusMessage = null;
            this.refreshHint();
        }, ms);
    }

    // Guarda todas las imágenes con cambios (las que no tienen archivo preguntan dónde).
    async saveAll() {
        for (const doc of [...this.docs])
            if (doc.isModified() && !(await this.save(doc)))
                return;
    }

    async closeAll() {
        for (const doc of [...this.docs])
            if (!(await this.closeDocument(doc)))
                return;
    }

    // Flecha al final de la tira (como en Kaint y Paint.NET): una franja de la altura de las
    // miniaturas que se queda pulsada mientras se ve, debajo, la lista de todas las imágenes con su
    // miniatura y su nombre, con la actual resaltada.
    createImageList() {
        const button = document.getElementById("image-list");
        button.title = tr("Imágenes abiertas");
        button.appendChild(icon("image-list-menu", 16));
        let popup = null;
        const close = () => {
            popup?.remove();
            popup = null;
            button.classList.remove("down");
            document.removeEventListener("pointerdown", outside, true);
            document.removeEventListener("keydown", keys, true);
        };
        const outside = (e) => {
            if (!popup?.contains(e.target) && !button.contains(e.target))
                close();
        };
        const choose = (doc) => {
            close();
            this.setCurrent(doc);
        };
        const keys = (e) => {
            const rows = [...popup.querySelectorAll(".row")];
            let i = rows.findIndex((r) => r.classList.contains("selected"));
            if (e.key === "Escape") {
                close();
            } else if (e.key === "ArrowDown" || e.key === "ArrowUp") {
                rows[i]?.classList.remove("selected");
                i = Math.max(0, Math.min(rows.length - 1, i + (e.key === "ArrowDown" ? 1 : -1)));
                rows[i].classList.add("selected");
                rows[i].scrollIntoView({ block: "nearest" });
            } else if (e.key === "Enter" && i >= 0) {
                choose(this.docs[i]);
            } else {
                return;
            }
            e.preventDefault();
            e.stopPropagation();
        };
        button.addEventListener("click", () => {
            if (popup) {
                close();
                return;
            }
            popup = el("div", { className: "image-list-popup" });
            for (const doc of this.docs) {
                const row = el("div", { className: "row" + (doc === this.doc ? " selected" : "") },
                               [el("span", { className: "thumb" }, [thumbnail(doc.flattened(), 64, 48)]),
                                el("span", { className: "name", textContent: (doc.isModified() ? "*" : "") + doc.displayName() })]);
                row.addEventListener("click", () => choose(doc));
                row.addEventListener("pointerenter", () => {
                    popup.querySelectorAll(".row").forEach((r) => r.classList.toggle("selected", r === row));
                });
                popup.appendChild(row);
            }
            document.body.appendChild(popup);
            // Debajo de la flecha, sin salirse de la ventana.
            const r = button.getBoundingClientRect(), w = popup.offsetWidth;
            popup.style.left = `${Math.max(0, Math.min(r.left, window.innerWidth - w - 2))}px`;
            popup.style.top = `${r.bottom + 1}px`;
            popup.querySelector(".selected")?.scrollIntoView({ block: "nearest" });
            button.classList.add("down");
            document.addEventListener("pointerdown", outside, true);
            document.addEventListener("keydown", keys, true);
        });
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
            tab.querySelector(":scope > canvas")?.remove();
            tab.querySelector(".unsaved")?.remove();
            tab.insertBefore(thumbnail(doc.flattened(), 64, 48), tab.firstChild);
            if (doc.isModified())
                tab.appendChild(el("div", { className: "unsaved", title: tr("Cambios no guardados") }, [unsavedStar(12)]));
            tab.title = doc.displayName();
            if (doc === this.doc)
                document.title = `${doc.isModified() ? "*" : ""}${doc.displayName()} - Kaint`;
        };
        clearTimeout(doc.tabTimer);
        now ? paint() : (doc.tabTimer = setTimeout(paint, 400));
    }

    // --- Acciones --------------------------------------------------------------------------

    // Atajos que eligió el usuario (Configuración > Atajos de teclado), sobre los de fábrica.
    applyShortcuts() {
        const saved = Settings.get("shortcuts") || {};
        for (const [id, keys] of Object.entries(saved)) {
            const a = this.actions.get(id);
            if (a)
                a.shortcuts = keys ? [keys] : [];
        }
    }

    // Cambia el atajo de una acción (vacío: ninguno); si otra lo usaba, se le quita y la devuelve.
    setShortcut(id, keys) {
        const action = this.actions.get(id);
        if (!action)
            return null;
        let taken = null;
        const saved = { ...(Settings.get("shortcuts") || {}) };
        const remember = (a) => {
            const same = a.shortcuts.length === a.defaultShortcuts.length && a.shortcuts.every((k, i) => k === a.defaultShortcuts[i]);
            if (same)
                delete saved[a.id];
            else
                saved[a.id] = a.shortcut;
        };
        if (keys)
            for (const other of this.actions.values())
                if (other !== action && other.shortcuts.includes(keys)) {
                    other.shortcuts = other.shortcuts.filter((k) => k !== keys);
                    remember(other);
                    taken = other;
                }
        action.shortcuts = keys ? (keys === action.defaultShortcut ? [...action.defaultShortcuts] : [keys]) : [];
        remember(action);
        Settings.set("shortcuts", saved);
        updateActions();
        return taken;
    }

    resetShortcuts() {
        for (const a of this.actions.values())
            a.shortcuts = [...a.defaultShortcuts];
        Settings.set("shortcuts", {});
        updateActions();
    }

    comboFromEvent(e) {
        return keyCombo(e);
    }

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
            this.tools.finishCurrent();
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
        A("saveAll", tr("Guardar &todo"), "document-save-all", "Ctrl+Alt+S", c(() => this.saveAll()), has);
        A("print", tr("&Imprimir..."), "document-print", "Ctrl+P", c(() => this.print()), has);
        A("close", tr("&Cerrar"), "document-close", "Ctrl+W", () => this.closeDocument(this.doc), has);
        A("closeAll", tr("Cerrar t&odas"), "document-close", "Ctrl+Shift+W", () => this.closeAll(), has);
        // Edición
        A("undo", tr("&Deshacer"), "edit-undo", "Ctrl+Z", () => this.undo(), () => this.doc?.history.canUndo() || this.tools.current?.isBusy());
        A("redo", tr("&Rehacer"), "edit-redo", "Ctrl+Y", () => this.redo(), () => this.doc?.history.canRedo());
        A("cut", tr("Cor&tar"), "edit-cut", "Ctrl+X", c(() => this.cut()), sel);
        A("copy", tr("&Copiar"), "edit-copy", "Ctrl+C", c(() => this.copy(false)), has);
        A("copyMerged", tr("Copiar co&mbinado"), "edit-copy-merged", "Ctrl+Shift+C", c(() => this.copy(true)), has);
        A("paste", tr("&Pegar"), "edit-paste", "Ctrl+V", c(() => this.pasteFromClipboard("layer")));
        A("pasteLayer", tr("Pegar en capa &nueva"), "edit-paste-new-layer", "Ctrl+Shift+V", c(() => this.pasteFromClipboard("newLayer")), has);
        A("pasteImage", tr("Pegar en una &imagen nueva"), "edit-paste-new-image", "Ctrl+Alt+V", c(() => this.pasteFromClipboard("newImage")));
        A("copySelection", tr("Copiar &selección"), "edit-copy-selection", "Ctrl+Alt+Shift+C", c(() => this.copySelection()), sel);
        for (const [mode, text, key] of [[SelectionMode.Replace, tr("&Reemplazar"), "Ctrl+Alt+Shift+V"], [SelectionMode.Union, tr("&Sumar (unión)")],
                                         [SelectionMode.Exclude, tr("&Excluir")], [SelectionMode.Intersect, tr("&Intersecar")],
                                         [SelectionMode.Xor, tr("In&vertir («xor»)")]])
            A(`pasteSelection-${mode}`, text, `selection-${mode}`, key || null, c(() => this.pasteSelection(mode)), has);
        A("growSel", tr("A&mpliar la selección..."), "zoom-in", null, c(() => this.reshapeSelection("grow")), sel);
        A("shrinkSel", tr("&Reducir la selección..."), "zoom-out", null, c(() => this.reshapeSelection("shrink")), sel);
        A("featherSel", tr("&Difuminar la selección..."), "effect-gaussianblur", null, c(() => this.reshapeSelection("feather")), sel);
        A("eraseSel", tr("&Borrar la selección"), "edit-erase-selection", "Del", c(() => this.eraseSelection()), sel);
        A("fillSel", tr("&Rellenar la selección"), "edit-fill-selection", "Backspace", c(() => this.fillSelection()), sel);
        A("invertSel", tr("&Invertir la selección"), "edit-select-invert", "Ctrl+I", c(() => this.invertSelection()), has);
        A("selectAll", tr("Seleccionar &todo"), "edit-select-all", "Ctrl+A", c(() => this.selectAll()), has);
        A("deselect", tr("&Anular la selección"), "edit-select-none", "Ctrl+D", c(() => this.deselect()), sel);
        // Ver
        A("zoomIn", tr("&Acercar"), "zoom-in", ["Ctrl++", "Ctrl+=", "Ctrl+Shift+="], () => this.view.zoomIn(), has);
        A("zoomOut", tr("A&lejar"), "zoom-out", ["Ctrl+-", "Ctrl+Shift+-"], () => this.view.zoomOut(), has);
        A("zoomFit", tr("&Zum a ventana"), "zoom-fit-best", "Ctrl+B", () => this.view.zoomToWindow(), has);
        A("zoomSel", tr("Zum a &selección"), "zoom-select", "Ctrl+Shift+B", () => this.view.zoomToRect(this.doc.selection.bounds()), sel);
        A("actualSize", tr("&Tamaño real"), "zoom-original", "Ctrl+0", () => this.view.setZoom(1), has);
        A("pixelGrid", tr("Cuadrícula de &píxeles"), "view-grid", "Ctrl+'", () => {
            this.view.pixelGrid = !this.view.pixelGrid;
            this.view.update();
            updateActions();
        }, has, () => this.view.pixelGrid);
        A("rulers", tr("&Reglas"), "measure", "Ctrl+Alt+R", () => {
            this.view.setRulersVisible(!this.view.rulersVisible);
            updateActions();
        }, null, () => this.view.rulersVisible);
        // Unidades de medida de las reglas y de la barra de estado.
        [tr("&Píxeles"), tr("P&ulgadas"), tr("&Centímetros")].forEach((text, u) =>
            A(`unit${u}`, text, null, null, () => this.setUnit(u), null, () => (Settings.get("unit") ?? 0) === u));
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
        A("colorProfile", tr("&Perfil de colores..."), "color-profile", null, c(() => this.colorProfile()), has);
        A("animation", tr("A&nimación..."), "media-playback-start", null, c(() => this.editAnimation()), has);
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
        A("rotateZoom", tr("&Giro y zum..."), "layer-rotate-zoom", "Ctrl+Shift+Z", c(() => this.runEffect(findEffect("rotatezoom"), false)), has);
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
        A("settings", tr("&Configuración..."), "configure", "Alt+X", () => settingsDialog(this));
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
            text: tr(e.name) + (e.params.length || e.configure ? "..." : ""), icon: `effect-${e.id}`, shortcut: e.shortcut || "",
            enabled: !!this.doc, run: this.command(() => this.runEffect(e, false)),
        });
        const sorted = (list) => [...list].sort((x, y) => tr(x.name).localeCompare(tr(y.name)));
        this.menuBar.setMenus([
            { title: tr("&Archivo"), entries: () => [a("new"), a("open"), "-", a("save"), a("saveAs"), a("saveAll"), "-", a("print"), "-", a("close"), a("closeAll")] },
            { title: tr("&Edición"), entries: () => [a("undo"), a("redo"), "-", a("cut"), a("copy"), a("copyMerged"), a("paste"), a("pasteLayer"),
                                                      a("pasteImage"), "-", a("copySelection"),
                                                      { text: tr("Pegar s&elección"), icon: "edit-paste-selection", enabled: !!this.doc,
                                                        submenu: () => Object.values(SelectionMode).map((m) => a(`pasteSelection-${m}`)) },
                                                      "-", a("eraseSel"), a("fillSel"), a("invertSel"), a("selectAll"), a("deselect"), "-",
                                                      a("growSel"), a("shrinkSel"), a("featherSel")] },
            { title: tr("&Ver"), entries: () => [a("zoomIn"), a("zoomOut"), a("zoomFit"), a("zoomSel"), a("actualSize"), "-", a("pixelGrid"), a("rulers"), "-",
                                                  a("unit0"), a("unit1"), a("unit2"), "-", a("fullscreen")] },
            { title: tr("&Imagen"), entries: () => [a("crop"), a("resize"), a("canvasSize"), "-", a("flipH"), a("flipV"), "-", a("rotCW"), a("rotCCW"),
                                                     a("rot180"), "-", a("colorProfile"), a("animation"), "-", a("flatten")] },
            { title: tr("&Capas"), entries: () => [a("addLayer"), a("deleteLayer"), a("duplicateLayer"), a("mergeDown"), a("toggleLayer"), "-",
                                                    a("importLayer"), "-", a("flipLayerH"), a("flipLayerV"), a("rotateLayer180"), a("rotateZoom"), "-", a("layerTop"),
                                                    a("layerAbove"), a("layerBelow"), a("layerBottom"), "-", a("layerToTop"), a("layerUp"),
                                                    a("layerDown"), a("layerToBottom"), "-", a("layerProps")] },
            { title: tr("A&justes"), entries: () => sorted(Effects.filter((e) => !e.category && !e.hidden && e.menu !== "effects")).map(effectEntry) },
            { title: tr("Efe&ctos"), entries: () => {
                // Como en Kaint: "Repetir" solo después de usar un efecto y "Complementos..." solo con
                // algún complemento activado.
                const out = [];
                if (this.lastEffect)
                    out.push({ text: tr("Repetir «%1»", tr(this.lastEffect.name)), icon: `effect-${this.lastEffect.id}`, shortcut: "Ctrl+F",
                               enabled: !!this.doc, run: this.command(() => this.runEffect(this.lastEffect, true)) });
                if (Plugins.anyEffects())
                    out.push({ text: tr("&Complementos..."), icon: "plugins", run: () => settingsDialog(this, "plugins") });
                if (out.length)
                    out.push("-");
                const cats = Object.entries(Categories).map(([key, name]) => [key, tr(name), null]);
                for (const e of Effects)
                    if (e.category?.startsWith("plugin:") && !cats.some(([key]) => key === e.category))
                        cats.push([e.category, e.categoryName, e.categoryIcon]);
                cats.sort((x, y) => x[1].localeCompare(y[1]));
                for (const [key, name, catIcon] of cats) {
                    const list = Effects.filter((e) => e.category === key && !e.hidden);
                    if (list.length)
                        out.push({ text: name, icon: catIcon ? el("img", { src: catIcon, width: 16, height: 16, alt: "" }) : undefined,
                                   submenu: () => sorted(list).map(effectEntry) });
                }
                const root = Effects.filter((e) => !e.category && e.menu === "effects");
                if (root.length)
                    out.push("-", ...sorted(root).map(effectEntry));
                return out;
            } },
        ]);
    }

    createToolBar() {
        // La barra principal lleva los mismos iconos que los menús, salvo de Cortar a Anular la
        // selección, que son como los de Paint.NET 4 (como en Kaint).
        const bar = document.getElementById("main-toolbar");
        const groups = [["new", "open", "save"], ["print"], ["cut", "copy", "paste"], ["crop", "deselect"], ["undo", "redo"], ["pixelGrid", "rulers"]];
        const barIcons = { cut: "toolbar-cut", copy: "toolbar-copy", paste: "toolbar-paste", crop: "toolbar-crop", deselect: "toolbar-deselect" };
        groups.forEach((group, i) => {
            if (i)
                bar.appendChild(el("span", { className: "sep" }));
            for (const id of group)
                bar.appendChild(toolButton(this.actions.get(id), barIcons[id] || this.actions.get(id).icon, 16));
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
            colors: new FloatingPanel(ws, "colors", tr("Colores"), colorsPanel(this), { corner: "bl", onClose: closed }),
        };
        new ResizeObserver(() => Object.values(this.panels).forEach((p) => p.reposition())).observe(ws);
        this.view.addEventListener("rulers", () => Object.values(this.panels).forEach((p) => p.reposition()));
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
        // Delante de la ayuda, como en paint.js, el icono de la herramienta (o el de la selección).
        this.hintIcon = el("span", { className: "hint-icon" });
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
        // Avance de la vista previa de un efecto (como la barra de Kaint): solo mientras calcula.
        this.effectProgress = el("progress", { className: "effect-progress", max: 1, value: 0 });
        this.effectProgress.hidden = true;
        bar.append(this.hintIcon, this.hintLabel, this.effectProgress, item("status-selection", this.selLabel, tr("Tamaño de la selección")),
                   item("status-cursor", this.posLabel, tr("Posición del cursor")), this.createUnitButton(),
                   this.zoomBox, presets, this.actionButton("zoomFit"), this.actionButton("zoomOut"), this.zoomSlider, this.actionButton("zoomIn"));
    }

    // Unidades: botón compacto («px», «in», «cm») con menú, como en Kaint.
    createUnitButton() {
        this.unitButton = el("button", { className: "tbutton unit-button", type: "button", title: tr("Unidades de medida") });
        this.unitButton.addEventListener("click", () => {
            const r = this.unitButton.getBoundingClientRect(), a = (id) => this.actions.get(id);
            popupMenu([a("unit0"), a("unit1"), a("unit2")], r.left, r.top, { above: true });
        });
        this.unitButton.textContent = unitSuffix(this.unit());
        return this.unitButton;
    }

    unit() {
        return Settings.get("unit") ?? 0;
    }

    setUnit(u) {
        Settings.set("unit", u);
        if (this.unitButton)
            this.unitButton.textContent = unitSuffix(u);
        this.view.renderRulers();
        this.updateStatus();
        updateActions();
    }

    // Fracción calculada de la vista previa (null la oculta).
    setEffectProgress(fraction) {
        if (!this.effectProgress)
            return;
        this.effectProgress.hidden = fraction === null;
        if (fraction !== null)
            this.effectProgress.value = fraction;
    }

    showHint(text) {
        this.toolHint = text;
        this.refreshHint();
    }

    // Con una herramienta de selección en reposo y algo seleccionado, como Paint.NET: posición,
    // rectángulo delimitador y área de la selección.
    refreshHint() {
        if (!this.hintLabel)
            return;
        const tool = this.tools.current, doc = this.doc;
        let iconName = tool?.icon, text = this.toolHint || "";
        if (this.statusMessage) {
            iconName = null;
            text = this.statusMessage;
        } else if (doc && tool?.highlightSelection && !tool.isBusy() && !doc.selection.isEmpty() && text === tool.hint()) {
            const u = this.unit(), ppu = pixelsPerUnit(u, doc.dpi), b = doc.selection.bounds();
            const len = (n) => `${formatLength(n, u, doc.dpi)} ${unitSuffix(u)}`;
            const area = doc.selection.area() / (ppu * ppu);
            const squares = u === 1 ? tr("pulgadas cuadradas") : u === 2 ? tr("centímetros cuadrados") : tr("píxeles cuadrados");
            text = tr("Esquina superior izquierda de la selección: %1, %2. Tamaño del rectángulo delimitador: %3 × %4. Área: %5 %6",
                      len(b.x), len(b.y), len(b.w), len(b.h),
                      area.toLocaleString(currentLanguage(), { minimumFractionDigits: u ? 2 : 0, maximumFractionDigits: u ? 2 : 0 }), squares);
            iconName = "status-selection";
        }
        if (this.hintIcon.dataset.name !== iconName) {
            this.hintIcon.dataset.name = iconName || "";
            this.hintIcon.replaceChildren(...(iconName ? [icon(iconName, 16)] : []));
        }
        this.hintLabel.textContent = text;
    }

    updateStatus() {
        if (!this.selLabel)
            return;
        this.refreshHint();
        const doc = this.doc;
        if (!doc) {
            this.selLabel.textContent = "";
            return;
        }
        const u = this.unit(), len = (n) => formatLength(n, u, doc.dpi), b = doc.selection.bounds();
        const r = b || { x: 0, y: 0, w: doc.width, h: doc.height };
        this.selLabel.textContent = `${len(r.w)} × ${len(r.h)}`;
        this.selLabel.parentElement.title = b
            ? tr("Tamaño de la selección: %1 × %2 %3 · esquina superior izquierda: %4, %5", len(b.w), len(b.h), unitSuffix(u), len(b.x), len(b.y))
            : tr("Tamaño de la imagen: %1 × %2 %3 (no hay selección)", len(doc.width), len(doc.height), unitSuffix(u));
    }

    updateCursorPos(e) {
        const u = this.unit(), dpi = this.doc?.dpi;
        this.posLabel.textContent = e && this.doc ? `${formatLength(Math.floor(e.x), u, dpi)}, ${formatLength(Math.floor(e.y), u, dpi)}` : "";
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
            const effect = Effects.find((fx) => fx.shortcut === combo && !fx.hidden);
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
        this.prepareHistoryJump();
        this.doc?.undo();
    }

    redo() {
        this.prepareHistoryJump();
        this.doc?.redo();
    }

    jumpHistory(index) {
        if (!this.doc)
            return;
        this.prepareHistoryJump();
        this.doc.goTo(Math.min(index, this.doc.history.steps.length - 1));
    }

    // Antes de moverse por el historial: las herramientas con pasos editables registran lo
    // pendiente (el historial decide después si se retoma); las demás aplican lo que tengan.
    prepareHistoryJump() {
        const tool = this.tools.current;
        if (tool?.recordsSteps)
            tool.beforeHistoryJump();
        else
            tool?.finish();
    }

    // Como en Kaint: al llegar a un paso que su herramienta puede retomar (formas, degradado...),
    // se activa esa herramienta para seguir editándolo.
    syncToolWithHistory() {
        const doc = this.doc;
        if (this.syncingHistory || !doc)
            return;
        this.syncingHistory = true;
        try {
            const top = doc.history.steps[doc.history.index];
            this.tools.current?.historyChanged?.();
            if (top?.tool && top.base && this.tools.current?.id !== top.tool) {
                this.tools.select(top.tool);
                this.tools.current?.historyChanged?.();
            }
        } finally {
            this.syncingHistory = false;
        }
    }

    // --- Archivo ---------------------------------------------------------------------------

    async newImage() {
        const size = this.clipboard ? { w: this.clipboard.width, h: this.clipboard.height } : this.doc ? { w: this.doc.width, h: this.doc.height } : { w: 800, h: 600 };
        const r = await newImageDialog(size.w, size.h, this.doc?.dpi ?? 96);
        if (!r)
            return;
        const s = AppState.secondary;
        const bg = r.background === "white" ? "#ffffff" : r.background === "secondary" ? `rgba(${s.r},${s.g},${s.b},${s.a / 255})` : null;
        this.addDocument(new Document(r.width, r.height, { background: bg, dpi: r.dpi }));
    }

    pickFiles(multiple = true) {
        return new Promise((resolve) => {
            const input = el("input", { type: "file", accept: [OpenAccept, ...Plugins.openExtensions()].join(","), multiple });
            input.addEventListener("change", () => resolve([...input.files]));
            input.click();
        });
    }

    async open() {
        if (window.showOpenFilePicker) {
            try {
                const handles = await window.showOpenFilePicker({ multiple: true, types: [{ description: tr("Todos los tipos de imagen (%1)", OpenExtensions.map((e) => `*${e}`).join(" ")), accept: { "image/*": OpenExtensions } }] });
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
        if (/\.paintjs$/i.test(file.name)) {
            await this.installPlugin(file);
            return;
        }
        try {
            const type = Plugins.fileTypeFor(file.name);
            const doc = type ? await this.decodeWithPlugin(type, file) : await openFile(file);
            doc.fileHandle = handle;
            doc.markSaved();
            this.addDocument(doc);
        } catch {
            await message(tr("Abrir"), tr("No se pudo abrir «%1».", file.name));
        }
    }

    // Archivo de un formato de complemento: una capa o varias, en RGBA.
    async decodeWithPlugin(type, file) {
        const r = await type.decode({ bytes: new Uint8Array(await file.arrayBuffer()), fileName: file.name });
        const w = Number(r.width), h = Number(r.height);
        if (!(w > 0 && h > 0 && w <= 32768 && h <= 32768))
            throw new Error(file.name);
        const doc = new Document(w, h, { name: baseName(file.name), background: null, historyName: tr("Abrir imagen"), historyIcon: "document-open" });
        const layers = r.layers || [{ name: tr("Fondo"), visible: true, opacity: 255, blendMode: "normal", data: r.data }];
        doc.layers = layers.slice(0, 64).map((l, i) => {
            const layer = new Layer(w, h, l.name || (i ? tr("Capa %1", i + 1) : tr("Fondo")));
            layer.ctx.putImageData(new ImageData(new Uint8ClampedArray(l.data), w, h), 0, 0);
            layer.visible = l.visible !== false;
            layer.opacity = Math.max(0, Math.min(255, Number(l.opacity ?? 255)));
            layer.blend = l.blendMode || "normal";
            return layer;
        });
        doc.active = Math.min(Number(r.activeLayer ?? doc.layers.length - 1), doc.layers.length - 1);
        doc.fileName = file.name;
        return doc;
    }

    async installPlugin(file) {
        try {
            const record = await Plugins.install(file);
            await message(tr("Complementos"), tr("Se instaló «%1».", record.name), "plugins");
        } catch (e) {
            await message(tr("Complementos"), e.message, "dialog-warning");
        }
    }

    // Ventana para el diálogo propio de un complemento (paintDotJs.dialogs.create).
    createPluginDialog(options) {
        const dlg = new Dialog(String(options.title || ""), { iconName: null });
        if (options.icon) {
            const img = el("img", { src: String(options.icon), width: 16, height: 16, alt: "" });
            dlg.element.querySelector(".title").prepend(img);
        }
        const form = el("form", { className: String(options.className || "") });
        form.addEventListener("submit", (e) => e.preventDefault());
        const content = el("div", { className: String(options.contentClassName || "") });
        form.appendChild(content);
        dlg.body.appendChild(form);
        dlg.buttons.remove();
        dlg.exec();
        return Object.freeze({ backdrop: dlg.backdrop, dialog: form, content, closeButton: dlg.element.querySelector(".title .close"),
                               destroy: () => dlg.close(null) });
    }

    // Valores de un efecto con su diálogo (el de Kaint o el propio del complemento); null si se cancela.
    async configureEffect(effect, preview = null) {
        if (effect.configure) {
            const doc = this.doc, layer = doc.activeLayer, sel = doc.selection;
            const source = layer.ctx.getImageData(0, 0, doc.width, doc.height).data;
            const canvas = cloneCanvas(layer.canvas);
            const values = this.effectValues[effect.id] || pluginValues(effect.params, defaultValues(effect));
            const show = async (v) => {
                if (preview)
                    return preview(v);
                if (v === null) {
                    doc.setPreview(null);
                    return;
                }
                const { rect, data } = renderEffect(doc, effect, v);
                context(canvas).putImageData(data, rect.x, rect.y);
                doc.setPreview({ layer, canvas }, rect);
            };
            try {
                return await effect.configure({
                    title: effect.dialogTitle || effect.name, icon: iconUrl(`effect-${effect.id}`), source: new Uint8ClampedArray(source),
                    width: doc.width, height: doc.height, selection: sel.mask ? new Uint8Array(sel.mask) : null,
                    selectionBounds: sel.bounds() ? { x: sel.bounds().x, y: sel.bounds().y, width: sel.bounds().w, height: sel.bounds().h }
                        : { x: 0, y: 0, width: doc.width, height: doc.height },
                    values: structuredClone(values), preview: show, showOriginal: () => show(null),
                });
            } finally {
                doc.setPreview(null);
            }
        }
        const chosen = await effectDialog(this, effect, this.effectValues[effect.id]);
        this.doc?.setPreview(null);
        if (!chosen)
            return null;
        return effect.pluginId ? pluginValues(effect.params, chosen.values) : chosen.values;
    }

    // Guarda como Kaint (MainWindow::save, saveAs y writeFile): en el archivo abierto si el
    // navegador deja escribir en él; si no, "Guardar como" con el diálogo del navegador (o el
    // nombre y el tipo, y se descarga).
    async save(doc, saveAs = false) {
        if (!doc)
            return false;
        this.tools.finishCurrent();
        if (!saveAs && doc.fileHandle && this.formatOf(doc.fileHandle.name))
            return this.writeFile(doc, doc.fileHandle.name, doc.fileHandle);
        // Sugerencia: el mismo nombre; si su formato no se puede escribir (o es nueva y tiene
        // capas), OpenRaster.
        let name = doc.fileName || `${tr("Sin título")}.${doc.layers.length > 1 ? "ora" : "png"}`;
        if (!this.formatOf(name))
            name = `${baseName(name)}.ora`;
        if (window.showSaveFilePicker) {
            const formats = this.saveFormats(), chosen = this.formatOf(name);
            try {
                const handle = await window.showSaveFilePicker({
                    suggestedName: name,
                    types: [chosen, ...formats.filter((f) => f !== chosen)].map((f) => ({
                        description: tr(f.name), accept: { [f.mime]: [`.${f.ext}`, ...(f.aliases || []).map((x) => `.${x}`)] },
                    })),
                });
                return this.writeFile(doc, handle.name, handle);
            } catch (e) {
                if (e.name === "AbortError")
                    return false;
            }
        }
        const r = await saveAsDialog(this.saveFormats(), name);
        return r ? this.writeFile(doc, r, null) : false;
    }

    saveFormats() {
        return SaveFormats;
    }

    formatOf(name) {
        return formatFor(name);
    }

    async writeFile(doc, name, handle) {
        let format = this.formatOf(name);
        if (!format.layered && !format.animation && doc.layers.length > 1) {
            // Como Paint.NET: el formato no admite capas, así que primero se acopla la imagen.
            const ask = new TaskDialog(tr("Guardar"), "document-save",
                                       tr("Para guardar en este formato de archivo primero tiene que acoplar la imagen, lo que la reducirá a una única capa."));
            const flattenChoice = ask.addChoice("image-flatten", tr("Acoplar"),
                                                tr("Se acoplará la imagen y, a continuación, se guardará. El acoplamiento queda en el historial, pero el archivo tendrá una sola capa."),
                                                true);
            const oraName = `${baseName(name)}.ora`;
            const oraChoice = ask.addChoice("document-save-as", tr("No acoplar (guardar como .ora)"), tr("Se guarda con todas sus capas en «%1».", oraName));
            ask.addChoice("dialog-cancel", tr("Cancelar"), tr("Cancelar la acción de guardado."));
            const choice = await ask.exec();
            if (choice === oraChoice) {
                name = oraName;
                format = this.formatOf(name);
                if (handle && window.showSaveFilePicker) {
                    try {
                        handle = await window.showSaveFilePicker({ suggestedName: oraName, types: [{ description: tr(format.name), accept: { [format.mime]: [".ora"] } }] });
                        name = handle.name;
                    } catch {
                        return false;
                    }
                } else {
                    handle = null;
                }
            } else if (choice === flattenChoice) {
                this.flatten(doc);
            } else {
                return false;
            }
        }
        // Las opciones de cada formato se recuerdan; el GIF abierto conserva sus repeticiones.
        let options = this.saveOptions?.[format.ext] ? structuredClone(this.saveOptions[format.ext]) : defaultSaveOptions();
        if (format.animation && !this.saveOptions?.[format.ext])
            options.loops = doc.gifLoops ?? 0;
        let edited = null;
        if (!format.plugin && hasSaveOptions(format.ext)) {
            const frames = format.animation && doc.layers.length > 1 ? animationFrames(doc, 0, true) : [];
            const r = await saveConfigDialog(format, flattenedImage(doc), options, frames, { dpi: doc.dpi, icc: embeddedProfile(doc) });
            if (!r)
                return false;
            options = r.options;
            edited = r.frames;
            this.saveOptions = { ...(this.saveOptions || {}), [format.ext]: options };
        }
        document.body.classList.add("busy");
        try {
            const blob = await encode(doc, format, options, edited);
            if (handle) {
                const writable = await handle.createWritable();
                await writable.write(blob);
                await writable.close();
            } else {
                download(blob, name);
            }
        } catch (e) {
            await message(tr("No se pudo guardar"), tr("No se pudo guardar «%1»:\n%2", name, String(e?.message || e)), "dialog-warning");
            return false;
        } finally {
            document.body.classList.remove("busy");
        }
        doc.fileHandle = handle;
        doc.fileName = name;
        doc.markSaved();
        this.rebuildTabs();
        this.rememberSession();
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
            ctx.drawImage(sel.clipCanvas(AppState.selectionAntialias()), -b.x, -b.y);
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
        layer.ctx.drawImage(sel.clipCanvas(AppState.selectionAntialias()), 0, 0);
        layer.ctx.restore();
        const after = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const none = Selection.none(doc.width, doc.height);
        const apply = (data, s) => {
            layer.ctx.putImageData(data, r.x, r.y);
            doc.setSelection(s);
            doc.pixelsChanged(layer, r);
        };
        apply(after, none);
        doc.push({ name, icon: iconName, bytes: before.data.length + after.data.length, undo: () => apply(before, selBefore), redo: () => apply(after, none) });
    }

    fillSelection() {
        const doc = this.doc, layer = doc.activeLayer, sel = doc.selection, r = sel.bounds();
        const before = layer.ctx.getImageData(r.x, r.y, r.w, r.h);
        const fill = createCanvas(doc.width, doc.height), f = context(fill), c = AppState.primary;
        f.fillStyle = `rgba(${c.r},${c.g},${c.b},${c.a / 255})`;
        f.fillRect(r.x, r.y, r.w, r.h);
        f.globalCompositeOperation = "destination-in";
        f.drawImage(sel.clipCanvas(AppState.selectionAntialias()), 0, 0);
        layer.ctx.drawImage(fill, 0, 0);
        doc.pixelsChanged(layer, r);
        doc.pushPixels(layer, r, before, tr("Rellenar la selección"), "edit-fill-selection");
    }

    // Copiar y pegar la forma de la selección (como texto JSON, el formato de Paint.NET).
    async copySelection() {
        const json = this.doc.selection.toJson();
        this.selectionClipboard = json;
        try {
            await navigator.clipboard.writeText(json);
        } catch {
            // Sin acceso al portapapeles del sistema: queda la copia de Kaint.
        }
    }

    async pasteSelection(mode) {
        const doc = this.doc;
        let text = null;
        try {
            text = await navigator.clipboard.readText();
        } catch {
            // Sin permiso: la copia de Kaint.
        }
        const pasted = (text && Selection.fromJson(text, doc.width, doc.height))
            || (this.selectionClipboard && Selection.fromJson(this.selectionClipboard, doc.width, doc.height));
        if (!pasted) {
            await message(tr("Pegar selección"), tr("El portapapeles no contiene una selección (copiada con «Copiar la selección»)."), "edit-paste-selection");
            return;
        }
        const before = doc.selection;
        doc.setSelection(mode === SelectionMode.Replace ? pasted : before.combined(pasted, mode));
        doc.pushSelection(before, tr("Pegar selección"), "edit-paste");
    }

    // Ampliar, reducir o difuminar el borde de la selección tantos píxeles.
    async reshapeSelection(kind) {
        const doc = this.doc;
        if (doc.selection.isEmpty())
            return;
        const text = kind === "grow" ? tr("Ampliar la selección") : kind === "shrink" ? tr("Reducir la selección") : tr("Difuminar la selección");
        const radius = await promptNumber(text, tr("Radio (píxeles):"), 4, 1, 1000);
        if (radius === null)
            return;
        const before = doc.selection;
        doc.setSelection(kind === "grow" ? before.grown(radius) : kind === "shrink" ? before.grown(-radius) : before.feathered(radius));
        doc.pushSelection(before, text, "edit-select");
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
                ctx.drawImage(doc.selection.clipCanvas(AppState.selectionAntialias()), -b.x, -b.y);
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
        const doc = this.doc, r = await resizeDialog(doc.width, doc.height, doc.dpi);
        if (!r || (r.width === doc.width && r.height === doc.height && Math.abs(r.dpi - doc.dpi) < 1e-9))
            return;
        const before = doc.snapshot();
        // Con el mismo tamaño solo cambia la resolución (como Kaint).
        if (r.width !== doc.width || r.height !== doc.height) {
            document.body.classList.add("busy");
            await new Promise((resolve) => setTimeout(resolve, 0));
            for (const layer of doc.layers) {
                const c = createCanvas(r.width, r.height);
                context(c).putImageData(scaleImage(layer.ctx.getImageData(0, 0, doc.width, doc.height), r.width, r.height, r.method, r.gamma), 0, 0);
                layer.replaceCanvas(c);
            }
            document.body.classList.remove("busy");
        }
        doc.dpi = r.dpi;
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
        const doc = this.doc, r = await canvasSizeDialog(doc.width, doc.height, doc.dpi);
        if (r && (r.width !== doc.width || r.height !== doc.height || Math.abs(r.dpi - doc.dpi) > 1e-9))
            this.resizeCanvas(r.width, r.height, r.anchor, tr("Tamaño del lienzo"), r.dpi);
    }

    // El lienzo de otro tamaño; anchor [0..2, 0..2] dice dónde queda la imagen.
    resizeCanvas(width, height, anchor, name, dpi = this.doc.dpi) {
        const doc = this.doc, before = doc.snapshot();
        const offset = (n, old, a) => (a === 0 ? 0 : a === 2 ? n - old : Math.trunc((n - old) / 2));
        const dx = offset(width, doc.width, anchor[0]), dy = offset(height, doc.height, anchor[1]);
        doc.dpi = dpi;
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

    // Imagen > Perfil de colores: convertir los colores a otro perfil o asignarlo.
    async colorProfile() {
        const doc = this.doc, r = await colorProfileDialog(doc.colorProfile);
        if (!r)
            return;
        const before = doc.snapshot();
        if (r.result === "convert") {
            document.body.classList.add("busy");
            await new Promise((resolve) => setTimeout(resolve, 0));
            for (const layer of doc.layers) {
                const data = layer.ctx.getImageData(0, 0, doc.width, doc.height);
                transformPixels(data.data, doc.colorProfile, r.profile);
                layer.ctx.putImageData(data, 0, 0);
            }
            document.body.classList.remove("busy");
        }
        doc.colorProfile = r.profile;
        doc.emit("size");
        doc.layersChanged();
        doc.pushSnapshot(before, r.result === "convert" ? tr("Convertir perfil de colores") : tr("Asignar perfil de colores"), "color-profile");
    }

    // Duración de los fotogramas de un GIF (en el nombre de cada capa).
    async editAnimation() {
        const doc = this.doc;
        const renamed = await animationDialog(doc.layers, this.saveOptions?.gif?.frameDelay ?? 100);
        if (!renamed?.size)
            return;
        const before = doc.layerState();
        for (const [index, name] of renamed)
            doc.layers[index].name = name;
        doc.layersChanged();
        doc.pushLayers(before, tr("Duración de los fotogramas"), "media-playback-start");
    }

    flatten(doc = this.doc) {
        const before = doc.snapshot(), flat = doc.flattened();
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
            // Como en Kaint (Configuración): al ocultar la capa activa, se elige la visible más cercana.
            if (!layer.visible && index === doc.active && Settings.get("autoSelectVisibleLayer")) {
                for (let d = 1; d < doc.layers.length; ++d) {
                    const near = [index - d, index + d].find((i) => i >= 0 && i < doc.layers.length && doc.layers[i].visible);
                    if (near !== undefined) {
                        doc.setActiveIndex(near);
                        break;
                    }
                }
            }
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
        let values = this.effectValues[effect.id], result = null, input = null;
        if (!repeat && effect.configure) {
            values = await this.configureEffect(effect);
            if (!values)
                return;
        } else if (!repeat && effect.params.length) {
            const chosen = await effectDialog(this, effect, values);
            if (!chosen)
                return;
            ({ values, result, input } = chosen);
        }
        values = values || {};
        const full = effect.configure ? values : { ...defaultValues(effect), ...values };
        // Como Kaint: si la vista previa ya está calculada con estos valores se usa tal cual; si
        // no, se calcula con una ventana de progreso (si tarda más de medio segundo).
        try {
            result ||= await this.computeWithProgress(effect, full, input || effectInput(doc));
        } catch (e) {
            doc.setPreview(null);
            message(tr(effect.name), tr("El efecto no se pudo aplicar:\n%1", String(e?.message || e)), "dialog-warning");
            return;
        }
        if (!result) {
            doc.setPreview(null);
            return;
        }
        const { rect, data } = result;
        const layer = doc.activeLayer;
        const before = layer.ctx.getImageData(rect.x, rect.y, rect.w, rect.h);
        layer.ctx.putImageData(data, rect.x, rect.y);
        doc.setPreview(null);
        doc.pixelsChanged(layer, rect);
        doc.pushPixels(layer, rect, before, tr(effect.name), `effect-${effect.id}`);
        this.effectValues[effect.id] = full;
        if (!effect.hidden)
            this.lastEffect = effect;
        updateActions();
    }

    // Calcula un efecto en segundo plano; si tarda más de medio segundo aparece la ventana de
    // progreso con Cancelar (applyEffectWithProgress de Kaint). null si se cancela.
    async computeWithProgress(effect, values, input) {
        let dlg = null, bar = null;
        const job = computeEffect(input, effect, values, (f) => bar && (bar.value = f));
        document.body.classList.add("busy");
        const timer = setTimeout(() => {
            document.body.classList.remove("busy");
            dlg = new Dialog(tr(effect.name), { iconName: `effect-${effect.id}` });
            bar = el("progress", { max: 1, value: 0 });
            dlg.body.append(el("div", { className: "progress-text", textContent: tr("Aplicando «%1»…", tr(effect.name)) }), bar);
            dlg.addButton(tr("Cancelar"), null, { isDefault: true });
            dlg.exec().then((v) => v === null && job.cancel());
        }, 500);
        try {
            return await job.promise;
        } finally {
            clearTimeout(timer);
            document.body.classList.remove("busy");
            dlg?.close("done");
        }
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
