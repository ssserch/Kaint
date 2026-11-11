// "Guardar todos" (SaveAllDialog de Kaint): las imágenes abiertas en una lista, cada una con su
// nombre y su formato. Se puede poner el mismo nombre a todas con un número que las distingue (desde
// cuál empieza, con cuántas cifras, si la primera lo lleva...), el mismo formato a todas y el orden
// en que se numeran (arrastrando los puntos que salen al pasar por encima). Las que ya están
// guardadas empiezan sin marcar.
import { Settings } from "../Settings.js";
import { tr, trn } from "../i18n.js";
import { checkbox, Dialog, el, message, numberInput, select } from "./Dialogs.js";

// Color con que se muestra la extensión de cada formato (los mismos que en Kaint).
export function extensionColor(ext) {
    const colors = {
        png: "#e53935", jpg: "#2962ff", jpeg: "#2962ff", jpe: "#2962ff", jfif: "#2962ff", webp: "#2ea043", gif: "#9c27b0",
        bmp: "#ef6c00", ora: "#009688", pdn: "#3f51b5", tif: "#795548", tiff: "#795548", ico: "#0097a7",
    };
    if (colors[ext])
        return colors[ext];
    let h = 0;
    for (const c of ext)
        h = (h * 31 + c.charCodeAt(0)) >>> 0;
    return `hsl(${h % 360} 75% 45%)`;
}

// El número tal como queda en el nombre ('decoration' con %1 en su sitio).
export function numberText(number, digits, decoration) {
    const n = String(number).padStart(Math.max(1, digits), "0");
    return decoration.includes("%1") ? decoration.replace("%1", n) : decoration + n;
}

// El nombre común con el número en el '#' (o '##', '###'...: también son las cifras mínimas) o al
// final si no hay; 'number' < 0: sin número.
export function composeName(pattern, number, digits, decoration) {
    return composeParts(pattern, number, digits, decoration).name;
}

// Lo mismo, con dónde quedan las cifras del número (para pintarlas en amarillo).
export function composeParts(pattern, number, digits, decoration) {
    const hash = pattern.indexOf("#");
    let end = hash;
    while (hash >= 0 && end < pattern.length && pattern[end] === "#")
        ++end;
    const min = hash < 0 ? digits : Math.max(digits, end - hash);
    const token = number < 0 ? "" : numberText(number, min, decoration);
    const at = hash < 0 ? pattern.length : hash;
    const inToken = decoration.includes("%1") ? decoration.indexOf("%1") : decoration.length;
    const length = number < 0 ? 0 : String(number).padStart(Math.max(1, min), "0").length;
    const name = hash < 0 ? pattern + token : pattern.slice(0, hash) + token + pattern.slice(end);
    return { name, start: at + (number < 0 ? 0 : inToken), length };
}

const Decorations = ["%1", " %1", "_%1", "-%1", " (%1)", "(%1)", " [%1]"];

function validName(name) {
    return !!name.trim() && name !== "." && name !== ".." && !/[/\\:*?"<>|\u0000-\u001f]/.test(name);
}

// Campo del nombre con la extensión detrás, en su color («prueba.png»), y partes en amarillo (el
// número de cada nombre, los '#' del nombre común). El texto se ve en la capa de encima (el del
// campo es transparente, con su cursor y su selección), que sigue al campo al desplazarse.
function nameField(value) {
    const input = el("input", { type: "text", value, spellcheck: false });
    const ext = el("span", { className: "ext" });
    const sizer = el("span", { className: "sizer" });
    const overlay = el("div", { className: "overlay" }, [sizer, ext]);
    const wrap = el("div", { className: "save-all-name" }, [input, overlay]);
    let highlights = [];
    const sync = () => {
        const text = input.value;
        const parts = [];
        let at = 0;
        for (const [start, length] of [...highlights].sort((a, b) => a[0] - b[0])) {
            if (length <= 0 || start < at || start + length > text.length)
                continue;
            if (start > at)
                parts.push(text.slice(at, start));
            parts.push(el("span", { className: "number", textContent: text.slice(start, start + length) }));
            at = start + length;
        }
        parts.push(text.slice(at));
        sizer.replaceChildren(...parts);
        ext.style.visibility = text ? "" : "hidden";
        overlay.scrollLeft = input.scrollLeft;
    };
    input.addEventListener("input", sync);
    input.addEventListener("scroll", () => (overlay.scrollLeft = input.scrollLeft));
    for (const type of ["keyup", "pointerup", "select"])
        input.addEventListener(type, () => (overlay.scrollLeft = input.scrollLeft));
    sync();
    return {
        element: wrap, input,
        setExtension(e) {
            ext.textContent = e ? `.${e}` : "";
            ext.style.color = extensionColor(e);
        },
        setValue(v, ranges = []) {
            input.value = v;
            highlights = ranges;
            sync();
        },
        setHighlights(ranges) {
            highlights = ranges;
            sync();
        },
    };
}

// entries: [{ name, ext, image (lienzo), saved (ya guardada y sin cambios) }]; formats: las
// extensiones en que se puede guardar. Devuelve [{ index, name }] en el orden de la lista (el nombre
// con su extensión), o null.
export async function saveAllDialog(entries, formats) {
    const dlg = new Dialog(tr("Guardar todos"), { iconName: "document-save-all" });
    dlg.element.classList.add("save-all");
    dlg.body.appendChild(el("p", { className: "intro", textContent: tr("Marca las imágenes que quieres guardar. Puedes darles a todas el mismo nombre con un " +
        "número (# marca dónde va; si no, va al final) y el mismo formato, o cambiar cada una. Arrastra los puntos de una imagen para cambiar el orden en que se numeran.") }));

    // Fila de "todas": el nombre y el formato comunes.
    const all = el("input", { type: "checkbox", title: tr("Marcar o desmarcar todas") });
    const pattern = nameField("");
    pattern.input.placeholder = tr("Nombre para todas (por ejemplo, foto#)");
    pattern.input.title = tr("El mismo nombre para todas las imágenes marcadas, con un número que las distingue. # marca dónde va el número; si no hay, va al final.");
    const allFormat = select([["", tr("Varios")], ...formats.map((f) => [f, f])], "");
    allFormat.title = tr("El mismo formato para todas");
    dlg.body.appendChild(el("div", { className: "save-all-row head" }, [el("span", { className: "dots" }), all,
                                                                        el("span", { className: "thumb", textContent: tr("Todas") }), pattern.element, allFormat]));

    // Cómo es el número.
    const decoration = select(Decorations.map((d) => [d, tr("nombre") + d.replace("%1", "1")]), Settings.get("saveAllDecoration") || "%1");
    decoration.title = tr("Formato del número");
    const start = numberInput(Settings.get("saveAllStart") ?? 1, 0, 999999);
    const digits = numberInput(Settings.get("saveAllDigits") ?? 1, 1, 8);
    digits.title = tr("Cifras mínimas del número: con 3, 001, 002...");
    const numberFirst = checkbox(tr("La primera también lleva número"), !!Settings.get("saveAllNumberFirst"));
    numberFirst.title = tr("Sin marcar, la primera se llama como el nombre común (foto, foto1, foto2...).");
    dlg.body.appendChild(el("div", { className: "save-all-numbers" }, [el("span", { textContent: tr("Número:") }), decoration,
                                                                         el("span", { textContent: tr("Empieza en") }), start,
                                                                         el("span", { textContent: tr("Cifras") }), digits, numberFirst]));

    // Las imágenes.
    const list = el("div", { className: "save-all-list" });
    dlg.body.appendChild(list);
    const rows = entries.map((e, index) => {
        const check = el("input", { type: "checkbox", checked: !e.saved, title: tr("Guardar esta imagen") });
        const name = nameField(e.name);
        const ext = formats.includes(e.ext) ? e.ext : formats[0];
        const format = select(formats.map((f) => [f, f]), ext);
        name.setExtension(ext);
        const dots = el("span", { className: "dots", title: tr("Arrastra para cambiar el orden") });
        const thumb = el("span", { className: "thumb" }, [e.image]);
        const element = el("div", { className: "save-all-row" }, [dots, check, thumb, name.element, format]);
        const row = { index, element, check, name, format, custom: false };
        name.input.addEventListener("input", () => {
            row.custom = true;
            // Escrito a mano: el número deja de marcarse.
            name.setHighlights([]);
        });
        format.addEventListener("change", () => {
            name.setExtension(format.value);
            sync();
        });
        check.addEventListener("change", () => {
            sync();
            applyPattern(false);
        });
        // Arrastrar los puntos mueve la fila (y renumera).
        dots.addEventListener("pointerdown", (ev) => {
            ev.preventDefault();
            dots.setPointerCapture(ev.pointerId);
            element.classList.add("dragging");
        });
        dots.addEventListener("pointermove", (ev) => {
            if (!dots.hasPointerCapture(ev.pointerId))
                return;
            const others = order.filter((r) => r !== row);
            let to = 0;
            for (const r of others) {
                const box = r.element.getBoundingClientRect();
                if (box.top + box.height / 2 < ev.clientY)
                    ++to;
            }
            const from = order.indexOf(row);
            if (to === from)
                return;
            order.splice(from, 1);
            order.splice(to, 0, row);
            list.insertBefore(element, list.children[to + (to > from ? 1 : 0)] || null);
            for (const r of order)
                list.appendChild(r.element);
            applyPattern(false);
        });
        const end = () => element.classList.remove("dragging");
        dots.addEventListener("pointerup", end);
        dots.addEventListener("lostpointercapture", end);
        list.appendChild(element);
        return row;
    });
    const order = [...rows];
    const count = el("span", { className: "count" });

    function applyPattern(resetCustom) {
        if (resetCustom)
            rows.forEach((r) => (r.custom = false));
        pattern.setExtension(allFormat.value);
        const text = pattern.input.value;
        if (!text)
            return;
        const first = numberFirst.input.checked, from = Number(start.value) || 0, d = Number(digits.value) || 1;
        let k = 0;
        for (const r of order) {
            if (!r.check.checked)
                continue;
            const number = first ? from + k : k === 0 ? -1 : from + k - 1;
            if (!r.custom) {
                const parts = composeParts(text, number, d, decoration.value);
                r.name.setValue(parts.name, [[parts.start, parts.length]]);
            }
            ++k;
        }
    }

    function sync() {
        const checked = rows.filter((r) => r.check.checked).length;
        all.checked = checked === rows.length;
        all.indeterminate = checked > 0 && checked < rows.length;
        const exts = new Set(rows.map((r) => r.format.value));
        allFormat.value = exts.size === 1 ? [...exts][0] : "";
        pattern.setExtension(allFormat.value);
        count.textContent = trn("%n imagen(es) para guardar", checked);
        ok.disabled = checked === 0;
    }

    all.addEventListener("change", () => {
        const every = rows.every((r) => r.check.checked);
        rows.forEach((r) => (r.check.checked = !every));
        sync();
        applyPattern(false);
    });
    pattern.input.addEventListener("input", () => {
        // Los '#' del nombre común, en amarillo como los números que serán.
        pattern.setHighlights([...pattern.input.value.matchAll(/#+/g)].map((m) => [m.index, m[0].length]));
        applyPattern(true);
    });
    allFormat.addEventListener("change", () => {
        if (allFormat.value)
            rows.forEach((r) => {
                r.format.value = allFormat.value;
                r.name.setExtension(allFormat.value);
            });
        sync();
    });
    for (const input of [start, digits, decoration])
        input.addEventListener("input", () => applyPattern(false));
    numberFirst.input.addEventListener("change", () => applyPattern(false));

    dlg.buttons.prepend(count);
    const ok = dlg.addButton(tr("Guardar"), "ok", { isDefault: true });
    dlg.addButton(tr("Cancelar"), null);
    sync();

    const results = () => order.filter((r) => r.check.checked).map((r) => {
        let n = r.name.input.value.trim();
        const ext = `.${r.format.value}`;
        if (n.toLowerCase().endsWith(ext))
            n = n.slice(0, -ext.length);
        return { index: r.index, name: n + ext, valid: validName(n) };
    });
    dlg.validate = () => {
        const chosen = results();
        const problems = [];
        for (const r of chosen)
            if (!r.valid)
                problems.push(tr("«%1» no es un nombre de archivo válido.", r.name));
        const seen = new Set();
        for (const r of chosen) {
            if (seen.has(r.name.toLowerCase()))
                problems.push(tr("Hay varias imágenes que se guardarían como «%1».", r.name));
            seen.add(r.name.toLowerCase());
        }
        if (problems.length) {
            message(tr("Guardar todos"), [...new Set(problems)].join("\n"));
            return false;
        }
        return true;
    };
    const answer = await dlg.exec();
    if (answer !== "ok")
        return null;
    Settings.set("saveAllDecoration", decoration.value);
    Settings.set("saveAllStart", Number(start.value) || 0);
    Settings.set("saveAllDigits", Number(digits.value) || 1);
    Settings.set("saveAllNumberFirst", numberFirst.input.checked);
    return results().map(({ index, name }) => ({ index, name }));
}
