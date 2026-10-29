// Acciones como QAction: texto (con "&" de tecla de acceso), icono, atajo, si está habilitada o
// marcada y qué hacen. Los botones de las barras se actualizan con "kaint-update".
import { icon } from "./Icons.js";
import { plain, tr } from "../i18n.js";

const isMac = /Mac|iPhone|iPad/.test(navigator.platform);

export class Action {
    constructor({ id, text, icon: iconName = null, shortcut = null, run, enabled = null, checked = null }) {
        this.isAction = true;
        this.id = id;
        this.text = text;
        this.icon = iconName;
        this.shortcuts = shortcut ? (Array.isArray(shortcut) ? shortcut : [shortcut]) : [];
        // Los de fábrica (Configuración > Atajos de teclado los puede cambiar).
        this.defaultShortcuts = [...this.shortcuts];
        this.run = run;
        this.enabledFn = enabled;
        this.checked = checked;
    }

    isEnabled() {
        return this.enabledFn ? !!this.enabledFn() : true;
    }

    trigger() {
        if (this.isEnabled())
            this.run();
    }

    get shortcut() {
        return this.shortcuts[0] || "";
    }

    get defaultShortcut() {
        return this.defaultShortcuts[0] || "";
    }

    shortcutText() {
        return this.shortcuts[0] ? shortcutLabel(this.shortcuts[0]) : "";
    }

    tooltip() {
        const s = this.shortcutText();
        return s ? `${plain(this.text)} (${s})` : plain(this.text);
    }
}

export function shortcutLabel(s) {
    return s.split("+").map((k) => ({ Ctrl: isMac ? "⌘" : tr("Ctrl"), Shift: tr("Mayús"), Alt: "Alt", Del: tr("Supr"),
                                       PgUp: tr("RePág"), PgDown: tr("AvPág"), Backspace: "⌫" })[k] ?? k).join("+");
}

// La combinación de una pulsación, con el mismo formato que los atajos ("Ctrl+Shift+N").
export function keyCombo(e) {
    const parts = [];
    if (e.ctrlKey || e.metaKey) parts.push("Ctrl");
    if (e.altKey) parts.push("Alt");
    if (e.shiftKey) parts.push("Shift");
    let key = e.key;
    const named = { Delete: "Del", PageUp: "PgUp", PageDown: "PgDown", " ": "Space", "+": "+", "=": "=" };
    if (named[key])
        key = named[key];
    else if (/^Key[A-Z]$/.test(e.code))
        key = e.code.slice(3);
    else if (/^Digit\d$/.test(e.code))
        key = e.code.slice(5);
    else if (key.length === 1)
        key = key.toUpperCase();
    if (["Control", "Shift", "Alt", "Meta"].includes(key))
        return "";
    parts.push(key);
    return parts.join("+");
}

// Botón de barra para una acción; 'iconName' cambia el icono.
export function toolButton(action, iconName = action.icon, size = 16) {
    const b = document.createElement("button");
    b.className = "tbutton";
    b.title = action.tooltip();
    b.appendChild(icon(iconName, size));
    b.addEventListener("click", () => action.trigger());
    const update = () => {
        b.title = action.tooltip();
        b.disabled = !action.isEnabled();
        b.classList.toggle("checked", !!action.checked?.());
    };
    window.addEventListener("kaint-update", update);
    update();
    return b;
}

export function updateActions() {
    window.dispatchEvent(new Event("kaint-update"));
}
