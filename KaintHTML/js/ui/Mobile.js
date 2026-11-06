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

// Pellizcar con dos dedos sobre 'element' cambia su tamaño (su escala): 'begin' devuelve la escala
// de partida (o null si ahora no se puede), 'update' la recibe mientras se pellizca y 'end' al
// soltar. Con eventos táctiles que se cancelan: así el navegador no se queda el gesto (desplazar
// el contenido) y un dedo sigue desplazando lo de dentro como siempre.
export function installPinch(element, { begin, update, end, min = 0.5, max = 1.5 }) {
    let start = null;
    const distance = (t) => Math.hypot(t[0].clientX - t[1].clientX, t[0].clientY - t[1].clientY);
    element.addEventListener("touchstart", (e) => {
        if (e.touches.length !== 2)
            return;
        const scale = begin();
        if (scale == null)
            return;
        start = { distance: Math.max(1, distance(e.touches)), scale, current: scale };
        e.preventDefault();
    }, { passive: false });
    element.addEventListener("touchmove", (e) => {
        if (!start || e.touches.length < 2)
            return;
        e.preventDefault();
        const scale = Math.min(max, Math.max(min, (start.scale * distance(e.touches)) / start.distance));
        start.current = Math.round(scale * 100) / 100;
        update(start.current);
    }, { passive: false });
    const finish = (e) => {
        if (!start || e.touches.length >= 2)
            return;
        const scale = start.current;
        start = null;
        end?.(scale);
    };
    element.addEventListener("touchend", finish);
    element.addEventListener("touchcancel", finish);
}

// Asas: su tamaño (kaint.css) y el carril que se deja en cada borde para ellas.
const kHandle = { w: 30, h: 64 }, kLane = kHandle.w + 2;

export class MobilePanels {
    constructor(app, workspace) {
        this.app = app;
        this.workspace = workspace;
        this.handles = {};
        this.open = new Set();
        // Orden en que se sacaron (o se tocaron) las ventanas: la última, delante.
        this.order = [];
        this.handleRects = {};
        for (const [id, panel] of Object.entries(app.panels)) {
            const names = { tools: tr("Herramientas"), history: tr("Historial"), layers: tr("Capas"), colors: tr("Colores") };
            const handle = el("button", { className: "panel-handle", type: "button", title: names[id] || id, dataset: { panel: id } },
                              [icon(PanelIcons[id] || "configure", 20)]);
            handle.addEventListener("click", () => this.toggle(id));
            workspace.appendChild(handle);
            this.handles[id] = handle;
            panel.dockHost = this;
            panel.element.classList.toggle("fit-content", !!panel.fit);
            // La ventana cambió de tamaño por su cuenta («Más» y «Menos» en Colores, las columnas de
            // Herramientas...): vuelve a caber y su asa la sigue (antes se quedaba donde estaba).
            new ResizeObserver(() => this.refitSoon(id)).observe(panel.element);
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
        for (const [id, panel] of Object.entries(this.app.panels)) {
            const wasDocked = !!panel.dock;
            panel.dock = docked ? { y: 0 } : null;
            panel.element.classList.toggle("docked", docked);
            if (docked) {
                panel.element.style.display = this.open.has(id) ? "" : "none";
            } else {
                this.open.delete(id);
                panel.element.classList.remove("open");
                panel.element.style.zIndex = "";
                if (wasDocked)
                    panel.element.style.display = panel.visible ? "" : "none";
            }
            this.fit(id);
        }
        this.order = this.order.filter((id) => this.open.has(id));
        this.stack();
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    // Lo que cabe de una ventana: en el teléfono, toda la zona menos, escondidas en las esquinas,
    // un carril a cada lado para las asas (la suya, junto a ella, y las del borde de enfrente, que
    // así nunca quedan debajo); en el modo Ventanas, con el tamaño elegido para cada una.
    fit(id) {
        const panel = this.app.panels[id];
        if (!isPhone()) {
            panel.unfit();
            panel.reposition();
            return;
        }
        if (panel.element.style.display === "none" || panel.element.classList.contains("dragging"))
            return;
        const a = panel.area(), docked = !!panel.dock;
        const room = docked ? { w: a.w - 2 * kLane, h: a.h - 4 } : { w: a.w - 8, h: a.h - 8 };
        const scales = Settings.get("mobilePanelScale") || {};
        const scale = docked ? 1 : Math.max(PanelScaleRange.min, Math.min(PanelScaleRange.max, Number(scales[id]) || 100)) / 100;
        panel.fitInto(room, scale);
        panel.reposition();
    }

    // La ventana cambió de tamaño por su cuenta: que vuelva a caber, con su asa junto a ella. Dice
    // si lo hizo (solo en el teléfono). Colores la llama en el acto con «Más» y «Menos»: esperando
    // al aviso de ResizeObserver se veía un momento con el tamaño de antes, cortada.
    refit(id) {
        if (!isPhone())
            return false;
        this.fit(id);
        this.layoutHandles();
        // Si al crecer («Más» en Colores) tapa otras, también se esconden.
        if (this.open.has(id) && this.hideCovered(id)) {
            this.stack();
            this.layoutHandles();
            this.app.updateActionStates?.();
        }
        return true;
    }

    refitSoon(id) {
        // Fuera del aviso de ResizeObserver (cambiar el tamaño dentro de él da un error de bucle).
        requestAnimationFrame(() => this.refit(id));
    }

    // Esconde las ventanas abiertas que tape 'id' (ella o su asa), si así se eligió (Configuración >
    // Interfaz de usuario > Teléfono). Dice si escondió alguna.
    hideCovered(id) {
        if (!this.active || Settings.get("mobileHideCovered") === false)
            return false;
        const mine = this.zone(id);
        const hits = (r) => r.x < mine.x + mine.w && mine.x < r.x + r.w && r.y < mine.y + mine.h && mine.y < r.y + r.h;
        let any = false;
        for (const other of [...this.open]) {
            if (other !== id && hits(this.zone(other))) {
                this.collapse(other, false);
                any = true;
            }
        }
        return any;
    }

    // Lo que se ve de una ventana abierta, con su asa al lado, en coordenadas del área de trabajo.
    zone(id) {
        const r = this.app.panels[id].visualRect(), h = this.handleRects[id];
        if (!h)
            return r;
        const x = Math.min(r.x, h.x), y = Math.min(r.y, h.y);
        return { x, y, w: Math.max(r.x + r.w, h.x + h.w) - x, h: Math.max(r.y + r.h, h.y + h.h) - y };
    }

    // Delante, la última ventana que se sacó o se tocó, con su asa (antes las asas quedaban encima
    // de todas las ventanas, revueltas); las asas de las escondidas, en su borde, detrás de las
    // ventanas abiertas (kaint.css): una que la tape queda delante de ella.
    stack() {
        const docked = this.active;
        this.order.forEach((id, i) => {
            this.app.panels[id].element.style.zIndex = docked ? String(20 + 2 * i) : "";
            this.handles[id].style.zIndex = docked ? String(21 + 2 * i) : "";
        });
        for (const id of Object.keys(this.handles))
            if (!this.open.has(id))
                this.handles[id].style.zIndex = "";
    }

    bringToFront(id) {
        if (!this.open.has(id))
            return;
        this.order = [...this.order.filter((o) => o !== id), id];
        this.stack();
    }

    // Las asas, en el borde de la esquina de su ventana (si dos comparten esquina, una tras otra);
    // con la ventana abierta, junto a ella para volver a esconderla. Las de las escondidas siempre
    // en su sitio, aunque una ventana abierta de su lado las tape (antes se ponían junto a ella y,
    // con Colores ampliado, la de Herramientas acababa en medio de la pantalla).
    layoutHandles() {
        const docked = this.active;
        const panels = this.app.panels, W = this.workspace.clientWidth;
        const a = Object.values(panels)[0]?.area() || { x: 0, y: 0, w: W, h: this.workspace.clientHeight };
        const stack = {}, rects = {};
        for (const [id, panel] of Object.entries(panels)) {
            const corner = panel.corner, left = corner.includes("l"), top = corner.includes("t");
            const n = (stack[corner] = (stack[corner] ?? -1) + 1);
            const along = 10 + n * 74;
            const y = top ? a.y + along : a.y + a.h - along - kHandle.h;
            const out = docked && this.open.has(id) ? panel.visualRect().w : 0;
            rects[id] = { x: left ? a.x + out : a.x + a.w - out - kHandle.w, y, w: kHandle.w, h: kHandle.h, left, top };
        }
        this.handleRects = rects;
        for (const [id, h] of Object.entries(this.handles)) {
            h.style.display = docked ? "" : "none";
            if (!docked)
                continue;
            const r = rects[id];
            h.dataset.side = r.left ? "left" : "right";
            h.classList.toggle("active", this.open.has(id));
            h.style.top = `${r.y}px`;
            h.style.bottom = "";
            h.style.left = r.left ? `${r.x}px` : "";
            h.style.right = r.left ? "" : `${W - (r.x + r.w)}px`;
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
        this.order = [...this.order.filter((o) => o !== id), id];
        panel.element.style.display = "";
        panel.element.classList.add("open");
        panel.element.dataset.side = panel.corner.includes("l") ? "left" : "right";
        this.fit(id);
        this.layoutHandles();
        // Las que tapa se vuelven a esconder; si no se eligió así, se quedan detrás.
        this.hideCovered(id);
        this.stack();
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    collapse(id, update = true) {
        if (!this.open.has(id))
            return;
        const panel = this.app.panels[id];
        this.open.delete(id);
        this.order = this.order.filter((o) => o !== id);
        panel.element.classList.remove("open");
        panel.element.style.display = "none";
        if (!update)
            return;
        this.stack();
        this.layoutHandles();
        this.app.updateActionStates?.();
    }

    collapseAll() {
        for (const id of [...this.open])
            this.collapse(id);
    }
}
