// Ventanas en el teléfono. Dos modos (Configuración > Interfaz de usuario > Teléfono):
//  · Ocultarse en las esquinas (el predeterminado): cada ventana se queda escondida en su esquina,
//    con un asa alargada con su icono pegada al borde. Al tocar el asa, la ventana sale por ese lado;
//    se vuelve a esconder con el asa (que queda junto a ella), con su ×, al tocar el lienzo o, en
//    Herramientas, al elegir una herramienta.
//  · Ventanas: las flotantes de siempre, cada una con el tamaño elegido con su deslizador.
// Solo en los teléfonos (pantalla táctil y pequeña); en los demás equipos nada cambia.
import { Settings } from "../Settings.js";
import { tr } from "../i18n.js";
import { el } from "./Dialogs.js";
import { icon } from "./Icons.js";

export const PanelIcons = { tools: "window-tools", history: "window-history", layers: "window-layers", colors: "window-colors" };
export const PanelScaleRange = { min: 50, max: 150 };

// Un teléfono: pantalla táctil y pequeña (el lado corto, de móvil) o una vista del tamaño de la de
// un teléfono (también el modo de diseño adaptable de Firefox y Chrome sin pantalla táctil simulada).
export function isPhone() {
    const touchPhone = matchMedia("(pointer: coarse)").matches && Math.min(screen.width, screen.height) <= 600;
    const phoneView = Math.min(innerWidth, innerHeight) <= 500 && Math.max(innerWidth, innerHeight) <= 1000;
    return touchPhone || phoneView;
}

export function mobileMode() {
    return Settings.get("mobilePanels") === "windows" ? "windows" : "autohide";
}

export class MobilePanels {
    constructor(app, workspace) {
        this.app = app;
        this.workspace = workspace;
        this.handles = {};
        this.open = new Set();
        for (const [id, panel] of Object.entries(app.panels)) {
            const names = { tools: tr("Herramientas"), history: tr("Historial"), layers: tr("Capas"), colors: tr("Colores") };
            const handle = el("button", { className: "panel-handle", type: "button", title: names[id] || id, dataset: { panel: id } },
                              [icon(PanelIcons[id] || "configure", 20)]);
            handle.addEventListener("click", () => this.toggle(id));
            workspace.appendChild(handle);
            this.handles[id] = handle;
            panel.dockHost = this;
        }
        // Al tocar el lienzo, las ventanas abiertas se esconden; al elegir una herramienta, la de
        // Herramientas.
        app.view.canvas.addEventListener("pointerdown", () => this.collapseAll(), true);
        app.tools.addEventListener("changed", () => this.collapse("tools"));
        window.addEventListener("kaint-settings", (e) => {
            if (["mobilePanels", "mobilePanelScale"].includes(e.detail.key))
                this.apply();
        });
        const media = matchMedia("(pointer: coarse)");
        media.addEventListener?.("change", () => this.apply());
        window.addEventListener("resize", () => this.apply());
        this.apply();
    }

    // Con las ventanas escondidas en las esquinas (solo en el teléfono).
    get active() {
        return isPhone() && mobileMode() === "autohide";
    }

    isOpen(id) {
        return this.open.has(id);
    }

    apply() {
        const phone = isPhone(), docked = this.active;
        document.documentElement.classList.toggle("phone", phone);
        this.workspace.classList.toggle("mobile-autohide", docked);
        const scales = Settings.get("mobilePanelScale") || {};
        for (const [id, panel] of Object.entries(this.app.panels)) {
            const wasDocked = !!panel.dock;
            panel.dock = docked ? { y: 0 } : null;
            panel.element.classList.toggle("docked", docked);
            // Tamaño de cada ventana en el modo Ventanas del teléfono.
            const scale = phone && !docked ? Math.max(PanelScaleRange.min, Math.min(PanelScaleRange.max, Number(scales[id]) || 100)) / 100 : 1;
            panel.setScale(scale);
            if (docked) {
                panel.element.style.display = this.open.has(id) ? "" : "none";
            } else {
                this.open.delete(id);
                panel.element.classList.remove("open");
                if (wasDocked)
                    panel.element.style.display = panel.visible ? "" : "none";
            }
            panel.reposition();
        }
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    // Las asas, en el borde de la esquina de su ventana (si dos comparten esquina, una tras otra);
    // con la ventana abierta, junto a ella para volver a esconderla.
    layoutHandles() {
        const docked = this.active;
        const a = Object.values(this.app.panels)[0]?.area() || { x: 0, y: 0, w: this.workspace.clientWidth, h: this.workspace.clientHeight };
        const stack = {};
        for (const [id, panel] of Object.entries(this.app.panels)) {
            const h = this.handles[id];
            h.style.display = docked ? "" : "none";
            if (!docked)
                continue;
            const corner = panel.corner, left = corner.includes("l"), top = corner.includes("t");
            const n = (stack[corner] = (stack[corner] ?? -1) + 1);
            h.dataset.side = left ? "left" : "right";
            h.classList.toggle("active", this.open.has(id));
            const along = 10 + n * 74;
            h.style.top = top ? `${a.y + along}px` : "";
            h.style.bottom = top ? "" : `${this.workspace.clientHeight - (a.y + a.h) + along}px`;
            const out = this.open.has(id) ? panel.element.offsetWidth : 0;
            h.style.left = left ? `${a.x + out}px` : "";
            h.style.right = left ? "" : `${this.workspace.clientWidth - (a.x + a.w) + out}px`;
        }
    }

    toggle(id) {
        this.open.has(id) ? this.collapse(id) : this.expand(id);
    }

    expand(id) {
        const panel = this.app.panels[id];
        if (!panel || !this.active)
            return;
        this.open.add(id);
        panel.element.style.display = "";
        panel.element.classList.add("open");
        panel.element.dataset.side = panel.corner.includes("l") ? "left" : "right";
        panel.raise();
        panel.reposition();
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    collapse(id) {
        if (!this.open.has(id))
            return;
        const panel = this.app.panels[id];
        this.open.delete(id);
        panel.element.classList.remove("open");
        panel.element.style.display = "none";
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    collapseAll() {
        for (const id of [...this.open])
            this.collapse(id);
    }
}
