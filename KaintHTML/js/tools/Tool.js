// Base de las herramientas (src/app/tools/Tool.h de Kaint) y el gestor que las cambia con sus
// atajos de una letra (pulsar la misma letra pasa a la siguiente herramienta que la comparte).
import { AppState } from "../AppState.js";
import { iconCursor, miniCursor, toolPixelCursor } from "../ui/Icons.js";
import { tr } from "../i18n.js";

const miniCursorTools = new Set(["rectangleselect", "ellipseselect", "lassoselect", "gradient", "linecurve"]);

export class Tool {
    constructor(app) {
        this.app = app;
    }

    get doc() {
        return this.app.doc;
    }

    get view() {
        return this.app.view;
    }

    get settings() {
        return AppState.tools;
    }

    // Opciones de la barra (ToolOptions.js) que usa la herramienta.
    get options() {
        return [];
    }

    get highlightSelection() {
        return false;
    }

    hint() {
        return "";
    }

    // Como en Kaint, el icono actual de la herramienta con el punto activo en su punta; la
    // selección, el degradado y la línea dejan ver el puntero con su icono en miniatura al lado.
    cursor() {
        return toolPixelCursor(this.id) || (miniCursorTools.has(this.id) ? miniCursor(this.icon) : iconCursor(this.icon));
    }

    activate() {}

    deactivate() {
        this.finish();
    }

    // Edición en curso (formas, texto, mover...): se aplica con finish() o Intro.
    isBusy() {
        return false;
    }

    finish() {}

    cancel() {
        this.finish();
    }

    wantsKeyboard() {
        return false;
    }

    onKeyDown() {
        return false;
    }

    setHint(text) {
        this.app.showHint(text);
    }

    repaint() {
        this.view.update();
    }

    // Color del botón: izquierdo, el principal; derecho, el secundario.
    colorFor(button) {
        return button === "right" ? AppState.secondary : AppState.primary;
    }
}

export function selectHint() {
    return tr("Ctrl: añadir · Alt: restar · Ctrl+clic derecho: invertir (XOR) · Alt+clic derecho: intersección");
}

export class ToolManager extends EventTarget {
    constructor(app, classes) {
        super();
        this.app = app;
        this.tools = classes.map((C) => new C(app));
        this.current = null;
        this.previous = null;
    }

    find(id) {
        return this.tools.find((t) => t.id === id) || null;
    }

    select(id) {
        const tool = this.find(id);
        if (!tool || tool === this.current)
            return;
        if (this.current) {
            // Un error al aplicar lo pendiente no puede dejar atrapada la herramienta (ningún
            // botón volvería a responder): se registra y se cambia igual.
            try {
                this.current.deactivate();
            } catch (err) {
                console.error(err);
            }
            this.previous = this.current;
        }
        this.current = tool;
        tool.activate();
        this.dispatchEvent(new Event("changed"));
    }

    // Aplica lo pendiente de la herramienta actual antes de un comando; como al cambiar de
    // herramienta, un error se registra y el comando sigue.
    finishCurrent() {
        try {
            this.current?.finish();
        } catch (err) {
            console.error(err);
        }
    }

    // Atajo de una letra: la primera herramienta con esa letra o, si ya es la actual, la
    // siguiente que la comparta.
    selectByLetter(letter) {
        const matching = this.tools.filter((t) => t.letter === letter.toUpperCase());
        if (!matching.length)
            return false;
        const i = matching.indexOf(this.current);
        this.select(matching[(i + 1) % matching.length].id);
        return true;
    }
}
