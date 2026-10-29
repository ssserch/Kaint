// Base de las herramientas (src/app/tools/Tool.h de Kaint) y el gestor que las cambia con sus
// atajos de una letra (pulsar la misma letra pasa a la siguiente herramienta que la comparte).
import { AppState } from "../AppState.js";
import { iconCursor } from "../ui/Icons.js";
import { tr } from "../i18n.js";

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

    cursor() {
        return "crosshair";
    }

    // Cursor con el icono de la herramienta (punto activo en hx, hy de 24 px).
    iconCursor(hx, hy) {
        return iconCursor(this.icon, hx, hy);
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
            this.current.deactivate();
            this.previous = this.current;
        }
        this.current = tool;
        tool.activate();
        this.dispatchEvent(new Event("changed"));
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
