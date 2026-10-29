// Menús: la barra de menús y los menús emergentes (también los contextuales y los de los botones
// con opciones). Una entrada es una acción, "-" (separador) o { text, icon, shortcut, checked,
// enabled, run, submenu }. Aparecen con el fundido de paint.js (kaint.css).
import { icon } from "./Icons.js";
import { mnemonicHtml, plain } from "../i18n.js";

let openMenus = [];

export function closeMenus() {
    for (const m of openMenus)
        m.remove();
    openMenus = [];
    document.querySelectorAll(".menubar.active").forEach((bar) => {
        bar.classList.remove("active");
        bar.querySelectorAll(".open").forEach((t) => t.classList.remove("open"));
    });
}

function normalize(entry) {
    if (entry === "-" || !entry)
        return entry;
    if (entry.isAction) {
        return {
            text: entry.text, icon: entry.icon, shortcut: entry.shortcutText(), checked: entry.checked?.(),
            enabled: entry.isEnabled(), run: () => entry.trigger(),
        };
    }
    return { enabled: true, ...entry, checked: typeof entry.checked === "function" ? entry.checked() : entry.checked };
}

// Abre un menú en (x, y) de la pantalla; 'level' es la profundidad (submenús).
export function popupMenu(entries, x, y, { level = 0, onClose, minWidth } = {}) {
    while (openMenus.length > level)
        openMenus.pop().remove();
    const menu = document.createElement("div");
    menu.className = "popup-menu";
    if (minWidth)
        menu.style.minWidth = `${minWidth}px`;
    const items = [];
    let submenuTimer = 0;
    for (const raw of entries) {
        const entry = normalize(raw);
        if (!entry)
            continue;
        if (entry === "-") {
            if (menu.lastChild && !menu.lastChild.classList.contains("separator"))
                menu.appendChild(Object.assign(document.createElement("div"), { className: "separator" }));
            continue;
        }
        const item = document.createElement("div");
        item.className = "item" + (entry.enabled === false ? " disabled" : "");
        // Icono, texto, atajo y flecha de submenú
        const iconBox = document.createElement("span");
        iconBox.className = "check";
        if (entry.icon)
            iconBox.appendChild(typeof entry.icon === "string" ? icon(entry.icon, 16) : entry.icon);
        else if (entry.checked)
            iconBox.textContent = "✓";
        if (entry.icon && entry.checked)
            item.style.background = "var(--selection)";
        item.appendChild(iconBox);
        const text = document.createElement("span");
        text.innerHTML = mnemonicHtml(entry.text);
        item.appendChild(text);
        const shortcut = document.createElement("span");
        shortcut.className = "shortcut";
        shortcut.textContent = entry.shortcut || "";
        item.appendChild(shortcut);
        const arrow = document.createElement("span");
        if (entry.submenu)
            arrow.className = "sub";
        item.appendChild(arrow);
        item.addEventListener("pointerenter", () => {
            items.forEach((i) => i.classList.remove("active"));
            item.classList.add("active");
            clearTimeout(submenuTimer);
            if (entry.submenu && entry.enabled !== false) {
                submenuTimer = setTimeout(() => {
                    const r = item.getBoundingClientRect();
                    popupMenu(entry.submenu(), r.right - 2, r.top - 4, { level: level + 1 });
                }, 120);
            } else {
                submenuTimer = setTimeout(() => {
                    while (openMenus.length > level + 1)
                        openMenus.pop().remove();
                }, 120);
            }
        });
        item.addEventListener("click", (e) => {
            e.stopPropagation();
            if (entry.enabled === false || entry.submenu)
                return;
            closeMenus();
            onClose?.();
            entry.run?.();
        });
        item.mnemonic = (entry.text.match(/&(.)/) || [])[1]?.toLowerCase();
        item.entry = entry;
        items.push(item);
        menu.appendChild(item);
    }
    document.body.appendChild(menu);
    // Dentro de la ventana
    const r = menu.getBoundingClientRect();
    menu.style.left = `${Math.max(0, Math.min(x, window.innerWidth - r.width - 2))}px`;
    menu.style.top = `${Math.max(0, Math.min(y, window.innerHeight - r.height - 2))}px`;
    menu.items = items;
    menu.onCloseCallback = onClose;
    openMenus.push(menu);
    return menu;
}

// Teclado dentro de los menús abiertos: flechas, Intro, Esc y teclas de acceso.
document.addEventListener("keydown", (e) => {
    if (!openMenus.length)
        return;
    const menu = openMenus[openMenus.length - 1];
    const items = menu.items.filter((i) => !i.classList.contains("disabled"));
    const current = items.findIndex((i) => i.classList.contains("active"));
    const select = (i) => {
        menu.items.forEach((it) => it.classList.remove("active"));
        items[(i + items.length) % items.length]?.classList.add("active");
    };
    if (e.key === "Escape") {
        openMenus.pop().remove();
        if (!openMenus.length)
            closeMenus();
    } else if (e.key === "ArrowDown") {
        select(current + 1);
    } else if (e.key === "ArrowUp") {
        select(current < 0 ? -1 : current - 1);
    } else if (e.key === "ArrowRight" && current >= 0 && items[current].entry.submenu) {
        const r = items[current].getBoundingClientRect();
        const sub = popupMenu(items[current].entry.submenu(), r.right - 2, r.top - 4, { level: openMenus.length });
        sub.items[0]?.classList.add("active");
    } else if (e.key === "ArrowLeft" && openMenus.length > 1) {
        openMenus.pop().remove();
    } else if (e.key === "Enter" && current >= 0) {
        items[current].click();
    } else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
        const match = items.find((i) => i.mnemonic === e.key.toLowerCase());
        if (match) {
            match.classList.add("active");
            match.entry.submenu ? match.dispatchEvent(new Event("pointerenter")) : match.click();
        }
    } else {
        return;
    }
    e.preventDefault();
    e.stopPropagation();
}, true);

document.addEventListener("pointerdown", (e) => {
    if (openMenus.length && !e.target.closest(".popup-menu") && !e.target.closest(".menubar"))
        closeMenus();
}, true);

window.addEventListener("blur", () => closeMenus());

export class MenuBar {
    constructor(element) {
        this.element = element;
        this.menus = [];
    }

    // menus: [{ title, entries: () => [...] }]
    setMenus(menus) {
        this.menus = menus;
        this.element.innerHTML = "";
        for (const m of menus) {
            const title = document.createElement("div");
            title.className = "menu-title";
            title.innerHTML = mnemonicHtml(m.title);
            title.title = "";
            title.addEventListener("pointerdown", (e) => {
                e.preventDefault();
                if (title.classList.contains("open")) {
                    closeMenus();
                    return;
                }
                this.open(title, m);
            });
            title.addEventListener("pointerenter", () => {
                if (this.element.classList.contains("active") && !title.classList.contains("open"))
                    this.open(title, m);
            });
            m.element = title;
            this.element.appendChild(title);
        }
    }

    open(title, m) {
        closeMenus();
        this.element.classList.add("active");
        title.classList.add("open");
        const r = title.getBoundingClientRect();
        popupMenu(m.entries(), r.left, r.bottom, { level: 0 });
    }

    // Alt + letra abre el menú con esa tecla de acceso.
    openByMnemonic(key) {
        const m = this.menus.find((menu) => (menu.title.match(/&(.)/) || [])[1]?.toLowerCase() === key.toLowerCase());
        if (!m)
            return false;
        this.open(m.element, m);
        openMenus[0]?.items[0]?.classList.add("active");
        return true;
    }

    titleOf(text) {
        return plain(text);
    }
}
