// Deslizadores verticales de tamaño y opacidad (flujo) al lado del lienzo, como en Ibis Paint
// (BrushSideSliders de Kaint): con el pincel avanzado y su opción «Deslizadores al lado del lienzo»,
// para las herramientas de pincel. Cómodos con el dedo o el lápiz: no hace falta abrir la barra.
import { AppState } from "../AppState.js";
import { el } from "./Dialogs.js";
import { tr } from "../i18n.js";

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// Escala del tamaño que crece más deprisa arriba (pinceles pequeños precisos, grandes a mano).
const sizeFromFraction = (f) => Math.max(1, Math.round(f * f * 1000));
const fractionFromSize = (size) => Math.sqrt(clamp(size, 1, 1000) / 1000);

function verticalSlider(title, fraction, text, onFraction) {
    const fill = el("div", { className: "fill" }), knob = el("div", { className: "knob" });
    const track = el("div", { className: "track", title }, [fill, knob]);
    const value = el("div", { className: "value" });
    const box = el("div", { className: "vslider" }, [value, track]);
    const setAt = (e) => {
        const r = track.getBoundingClientRect();
        onFraction(clamp((r.bottom - e.clientY) / Math.max(1, r.height), 0, 1));
    };
    track.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        e.stopPropagation();
        track.setPointerCapture(e.pointerId);
        setAt(e);
        const move = (ev) => setAt(ev);
        track.addEventListener("pointermove", move);
        const up = () => {
            track.removeEventListener("pointermove", move);
            track.removeEventListener("pointerup", up);
            track.removeEventListener("pointercancel", up);
        };
        track.addEventListener("pointerup", up);
        track.addEventListener("pointercancel", up);
    });
    track.addEventListener("wheel", (e) => {
        e.preventDefault();
        onFraction(clamp(fraction() + (e.deltaY < 0 ? 0.02 : -0.02), 0, 1));
    }, { passive: false });
    box.refresh = () => {
        const f = fraction();
        fill.style.height = `${f * 100}%`;
        knob.style.bottom = `${f * 100}%`;
        value.textContent = text();
    };
    return box;
}

export class BrushSliders {
    constructor(app, parent) {
        this.app = app;
        const tools = () => AppState.tools;
        this.size = verticalSlider(tr("Tamaño del pincel"), () => fractionFromSize(tools().brushSize), () => String(Math.round(tools().brushSize)),
                                   (f) => AppState.updateTools({ brushSize: sizeFromFraction(f) }));
        this.flow = verticalSlider(tr("Opacidad del pincel (flujo)"), () => (tools().flow - 1) / 99, () => `${tools().flow}%`,
                                   (f) => AppState.updateTools({ flow: Math.round(1 + f * 99) }));
        this.element = el("div", { className: "brush-sliders", hidden: true }, [this.size, this.flow]);
        // Sin que el toque llegue al lienzo (no pinta ni mueve la imagen).
        for (const type of ["pointerdown", "touchstart", "mousedown"])
            this.element.addEventListener(type, (e) => e.stopPropagation());
        // Justo encima del lienzo y por debajo de las ventanas flotantes (Herramientas, Colores...).
        parent.insertBefore(this.element, parent.querySelector("#view")?.nextSibling || null);
        AppState.addEventListener("tools", () => this.refresh());
        app.tools.addEventListener("changed", () => this.refresh());
        this.refresh();
    }

    refresh() {
        const t = AppState.tools, tool = this.app.tools.current;
        const shown = !!t.advancedBrush && !!t.sideSliders && !!tool?.options?.includes("advanced");
        this.element.hidden = !shown;
        if (shown) {
            this.size.refresh();
            this.flow.refresh();
        }
    }
}
