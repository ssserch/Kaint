// Prueba de humo de Kaint para la web en Chromium sin pantalla (Playwright): usa las herramientas,
// los menús y los diálogos como lo haría el ratón y comprueba el resultado.
//   npm install playwright && npx playwright install chromium
//   node tests/smoke.mjs [carpeta para capturas]
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const { chromium } = await import(process.env.PLAYWRIGHT_MODULE || "playwright");
const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const outDir = process.argv[2];
const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".png": "image/png", ".svg": "image/svg+xml" };

// Servidor de archivos (los módulos no se cargan desde file://).
const server = createServer(async (req, res) => {
    const path = normalize(decodeURIComponent(new URL(req.url, "http://x").pathname)).replace(/^(\.\.[/\\])+/, "");
    try {
        const data = await readFile(join(root, path === "/" ? "index.html" : path));
        res.writeHead(200, { "Content-Type": types[extname(path)] || "application/octet-stream" });
        res.end(data);
    } catch {
        res.writeHead(404);
        res.end();
    }
}).listen(0, "127.0.0.1");
await new Promise((r) => server.once("listening", r));
const url = `http://127.0.0.1:${server.address().port}/index.html`;

let failures = 0;
const check = (ok, what) => {
    if (!ok) {
        failures++;
        console.error(`FALLO: ${what}`);
    }
};

const browser = await chromium.launch();
const context = await browser.newContext({ viewport: { width: 1400, height: 860 }, acceptDownloads: true });
const page = await context.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push(`${e.message}\n${e.stack}`));
page.on("console", (m) => m.type() === "error" && errors.push(m.text()));
await page.goto(url);
// La primera vez sale la bienvenida (logo, versión, autor y lo básico de la configuración).
await page.evaluate(() => localStorage.setItem("kaint.settings", JSON.stringify({ language: "es", colorScheme: 1 })));
await page.reload();
await page.waitForSelector(".welcome");
const welcome = await page.evaluate(() => ({ logo: document.querySelector(".welcome .kaint-logo")?.textContent,
                                             selects: document.querySelectorAll(".welcome select").length,
                                             donate: !!document.querySelector(".welcome .donate") }));
await page.click(".dialog .pushbutton.default");
const welcomed = await page.evaluate(() => JSON.parse(localStorage.getItem("kaint.settings")).welcomed);
await page.reload();
await page.waitForFunction(() => window.kaint?.doc);
const again = await page.evaluate(() => !!document.querySelector(".welcome"));
check(welcome.logo === "kaint.html" && welcome.selects === 3 && welcome.donate && welcomed === true && !again,
      `bienvenida la primera vez (${JSON.stringify(welcome)}, ${welcomed}, ${again})`);

const at = (x, y) => page.evaluate(([x, y]) => {
    const k = window.kaint, r = k.view.canvas.getBoundingClientRect(), p = k.view.imageToView(x, y);
    return { x: r.left + p.x, y: r.top + p.y };
}, [x, y]);
const drag = async (pts, button = "left") => {
    let p = await at(...pts[0]);
    await page.mouse.move(p.x, p.y);
    await page.mouse.down({ button });
    for (const q of pts.slice(1)) {
        p = await at(...q);
        await page.mouse.move(p.x, p.y, { steps: 4 });
    }
    await page.mouse.up({ button });
};
const click = async (x, y, button = "left") => {
    const p = await at(x, y);
    await page.mouse.click(p.x, p.y, { button });
};
const pixel = (x, y, layer = null) => page.evaluate(([x, y, layer]) => {
    const d = window.kaint.doc, l = layer === null ? d.activeLayer : d.layers[layer];
    return [...l.ctx.getImageData(x, y, 1, 1).data];
}, [x, y, layer]);
const lastStep = () => page.evaluate(() => {
    const h = window.kaint.doc.history;
    return h.steps[h.index].name;
});
const tool = async (id) => {
    await page.evaluate((id) => window.kaint.tools.select(id), id);
    await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));
};
const setColors = (p, s) => page.evaluate(async ([p, s]) => {
    const { AppState } = await import("./js/AppState.js");
    AppState.setPrimary(p);
    AppState.setSecondary(s);
}, [p, s]);
const shot = (name) => outDir && page.screenshot({ path: join(outDir, name) });

// Lápiz y goma
await setColors({ r: 0, g: 0, b: 255, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("pencil");
await drag([[10, 10], [40, 10]]);
check(JSON.stringify(await pixel(20, 10)) === "[0,0,255,255]", "el lápiz pinta píxeles del color principal");
await tool("eraser");
await drag([[20, 10], [21, 10]]);
check((await pixel(20, 10))[3] < 255, "la goma borra");

// El bloqueo de la goma: degradado con un clic fuera del lienzo, goma con Ctrl+clic fuera del
// lienzo y luego cualquier herramienta (antes el trazo vacío fallaba y no dejaba cambiar).
{
    const toolButton = (name) => page.locator(`.tools-grid .tbutton[title^="${name}"]`);
    await toolButton("Degradado").click();
    await click(-30, -30);
    await toolButton("Goma").click();
    const steps = await page.evaluate(() => window.kaint.doc.history.steps.length);
    await page.keyboard.down("Control");
    await click(-40, -40);
    await page.keyboard.up("Control");
    await toolButton("Lápiz").click();
    const after = await page.evaluate(() => ({ tool: window.kaint.tools.current.id, steps: window.kaint.doc.history.steps.length }));
    check(after.tool === "pencil" && after.steps === steps, `tras Ctrl+clic con la goma fuera del lienzo se cambia de herramienta (${JSON.stringify(after)})`);
}

// Selección, degradado, línea, texto y formas: el puntero con la herramienta en miniatura al lado
// (en Formas, la forma elegida): el del sistema con la miniatura aparte (por defecto) o el de Kaint.
{
    const cursors = await page.evaluate(async () => {
        const { AppState } = await import("./js/AppState.js");
        const { Settings } = await import("./js/Settings.js");
        const k = window.kaint, out = { system: {}, kaint: {} };
        for (const mode of ["system", "kaint"]) {
            Settings.set("systemPointer", mode === "system");
            const read = () => ({ cursor: k.view.canvas.style.cursor, mini: k.view.mini?.name ?? null });
            for (const id of ["rectangleselect", "lassoselect", "ellipseselect", "gradient", "linecurve", "text", "shapes"]) {
                k.tools.select(id);
                out[mode][id] = read();
            }
            const old = AppState.tools.shape;
            AppState.updateTools({ shape: "heart" });
            out[mode].heart = read();
            AppState.updateTools({ shape: old });
        }
        Settings.set("systemPointer", true);
        k.tools.select("pencil");
        return out;
    });
    const arrows = ["rectangleselect", "lassoselect", "ellipseselect", "gradient", "linecurve", "shapes"];
    const sys = cursors.system, own = cursors.kaint;
    check(arrows.every((id) => sys[id].cursor === "default" && sys[id].mini) && sys.text.cursor === "text" && sys.text.mini === "text"
          && sys.heart.mini !== sys.shapes.mini && sys.gradient.mini !== sys.linecurve.mini,
          "con el puntero del sistema, las herramientas con miniatura la llevan al lado");
    check(arrows.every((id) => own[id].cursor.includes("image-set") && own[id].cursor.endsWith(" 0 0, default") && !own[id].mini)
          && own.text.cursor.endsWith(" 4 8, text") && own.heart.cursor !== own.shapes.cursor && own.gradient.cursor !== own.linecurve.cursor,
          "las herramientas con miniatura muestran el puntero de Kaint con su icono al lado");
}

// Cubo de pintura (relleno global y contiguo) y varita mágica
await setColors({ r: 0, g: 160, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("paintbucket");
await click(400, 300);
check(JSON.stringify(await pixel(400, 300)) === "[0,160,0,255]", "el cubo rellena con el color principal");
await tool("magicwand");
await click(400, 300);
check(await page.evaluate(() => !window.kaint.doc.selection.isEmpty()), "la varita mágica selecciona la zona del color");
await page.keyboard.press("Control+d");

// Selección elíptica y de lazo
await tool("ellipseselect");
await drag([[100, 100], [160, 140], [200, 180]]);
const ellipse = await page.evaluate(() => {
    const s = window.kaint.doc.selection;
    return { inside: s.value(150, 140), corner: s.value(101, 101), bounds: s.bounds(), tool: window.kaint.tools.current.id };
});
check(ellipse.inside === 255 && ellipse.corner === 0, `la selección elíptica deja fuera las esquinas (${JSON.stringify(ellipse)})`);
await tool("lassoselect");
await drag([[300, 300], [400, 300], [400, 400], [300, 400]]);
check(await page.evaluate(() => window.kaint.doc.selection.value(350, 350) === 255), "el lazo selecciona su interior");
await page.keyboard.press("Control+d");

// Selector de color y volver a colorear
await tool("colorpicker");
await click(400, 300, "right");
check(await page.evaluate(async () => (await import("./js/AppState.js")).AppState.secondary.g === 160), "el selector toma el color secundario con el botón derecho");
await setColors({ r: 200, g: 0, b: 0, a: 255 }, { r: 0, g: 160, b: 0, a: 255 });
await tool("recolor");
await drag([[400, 300], [420, 300]]);
check((await pixel(410, 300))[0] > 150, "volver a colorear cambia el color secundario por el principal");

// Tampón de clonar
await page.evaluate(() => window.kaint.addLayer());
await tool("clonestamp");
await page.evaluate(() => window.kaint.doc.setActiveIndex(0));
const src = await at(410, 300);
await page.keyboard.down("Control");
await page.mouse.click(src.x, src.y);
await page.keyboard.up("Control");
await drag([[100, 400], [105, 400]]);
check((await pixel(102, 400, 0))[0] > 150, "el tampón copia desde el origen");

// Capas: propiedades (con vista previa), subir, combinar y la animación de las filas
await page.evaluate(() => window.kaint.doc.setActiveIndex(1));
const propsDone = page.evaluate(() => window.kaint.layerProperties());
await page.waitForSelector(".dialog");
await page.fill(".dialog input[type=text]", "Capa de prueba");
await page.selectOption(".dialog select", "multiply");
await page.click(".dialog .pushbutton.default");
await propsDone;
check(await page.evaluate(() => window.kaint.doc.activeLayer.name === "Capa de prueba" && window.kaint.doc.activeLayer.blend === "multiply"),
      "las propiedades de la capa cambian el nombre y el modo");
check(await lastStep() === "Propiedades de la capa", "las propiedades quedan en el historial");
await page.evaluate(() => window.kaint.moveLayerTo(1, 0));
check(await page.evaluate(() => document.querySelector(".layer-list .row").getAnimations().length > 0), "las filas de las capas se deslizan al moverlas");
await page.evaluate(() => window.kaint.doc.setActiveIndex(1));
await page.evaluate(() => window.kaint.mergeDown());
check(await page.evaluate(() => window.kaint.doc.layers.length === 1), "combinar con la inferior deja una capa");

// Imagen: girar, voltear, recortar y tamaño del lienzo
await page.evaluate(() => window.kaint.transformImage("cw"));
check(await page.evaluate(() => window.kaint.doc.width === 600 && window.kaint.doc.height === 800), "girar 90° intercambia el ancho y el alto");
await page.keyboard.press("Control+z");
check(await page.evaluate(() => window.kaint.doc.width === 800), "deshacer el giro");
await tool("rectangleselect");
await drag([[50, 50], [100, 90], [150, 150]]);
const cropSel = await page.evaluate(() => window.kaint.doc.selection.bounds());
check(cropSel && cropSel.w === 100 && cropSel.h === 100, `la selección rectangular mide 100 × 100 (${JSON.stringify(cropSel)})`);
await page.keyboard.press("Control+Shift+x");
check(await page.evaluate(() => window.kaint.doc.width === 100 && window.kaint.doc.height === 100), "recortar según la selección");
await page.keyboard.press("Control+z");
const sizeDone = page.evaluate(() => window.kaint.canvasSize());
await page.waitForSelector(".dialog");
// Como en Kaint: empieza por porcentaje; por tamaño absoluto se escribe la anchura.
await page.check(".dialog .size-editor input[type=radio] >> nth=1");
await page.fill(".dialog .size-grid input[type=number] >> nth=0", "900");
await page.click(".dialog .pushbutton.default");
await sizeDone;
check(await page.evaluate(() => window.kaint.doc.width === 900), "tamaño del lienzo");

// Efectos con diálogo (Curvas, Matiz y saturación) y de un clic (Sepia con su atajo)
for (const id of ["huesat", "curves", "dropshadow", "clouds"]) {
    const done = page.evaluate(async (id) => {
        const { findEffect } = await import("./js/core/Effects.js");
        return window.kaint.runEffect(findEffect(id), false);
    }, id);
    await page.waitForSelector(".dialog");
    await page.waitForTimeout(150);
    await page.click(".dialog .pushbutton.default");
    await done;
}
check(await lastStep() === "Nubes", "los efectos con diálogo se aplican");
await page.keyboard.press("Control+Shift+i");
// Los efectos se calculan en hilos de trabajo: el paso llega un poco después.
await page.waitForFunction(() => { const h = window.kaint.doc.history; return h.steps[h.index].name === "Invertir colores"; }, null, { timeout: 5000 }).catch(() => {});
check(await lastStep() === "Invertir colores", "el atajo de Invertir colores aplica el efecto");
await shot("efectos.png");

// Menús: Alt+letra abre Archivo; Ayuda con Documentación, Foro y Tutoriales
await page.locator("#view").focus();
await page.keyboard.press("Alt+a");
check(await page.evaluate(() => document.querySelectorAll(".popup-menu").length === 1), "Alt+A abre el menú Archivo");
await page.keyboard.press("Escape");
await page.click("#corner-buttons .tbutton:last-child");
const help = await page.evaluate(() => [...document.querySelectorAll(".popup-menu .item")].map((i) => i.textContent));
check(help.some((t) => t.startsWith("Documentación")) && help.some((t) => t.startsWith("Foro")) && help.some((t) => t.startsWith("Tutoriales")),
      "la Ayuda tiene Documentación, Foro y Tutoriales");
check(await page.evaluate(() => document.querySelector(".popup-menu img[src$='face-smile.svg']") !== null), "Donar lleva la carita");
await page.keyboard.press("Escape");

// Guardar en OpenRaster y volver a abrirlo con sus capas
await page.evaluate(() => window.kaint.addLayer());
await page.evaluate(() => { window.showSaveFilePicker = undefined; });
const download = page.waitForEvent("download");
await page.keyboard.press("Control+s");
await page.waitForSelector(".dialog");
await page.selectOption(".dialog select", "ora");
await page.click(".dialog .pushbutton.default");
const file = await download;
const path = await file.path();
const reopened = await page.evaluate(async (bytes) => {
    const { openFile } = await import("./js/core/ImageIO.js");
    const doc = await openFile(new File([new Uint8Array(bytes)], "prueba.ora"));
    return { layers: doc.layers.length, w: doc.width };
}, [...(await readFile(path))]);
check(reopened.layers === 2 && reopened.w === 900, "el .ora guardado se abre con sus capas");

// Imagen nueva, Configuración (ventanas translúcidas) y Acerca de
const newDone = page.evaluate(() => window.kaint.newImage());
await page.waitForSelector(".dialog");
await page.fill(".dialog input[type=number] >> nth=0", "320");
await page.fill(".dialog input[type=number] >> nth=1", "200");
await page.click(".dialog .pushbutton.default");
await newDone;
check(await page.evaluate(() => window.kaint.docs.length === 2 && window.kaint.doc.width === 320 && window.kaint.doc.height === 200),
      "imagen nueva de 320 × 200 en otra pestaña");
check(await page.evaluate(() => document.querySelectorAll(".image-tab").length === 2), "una pestaña por imagen");
page.evaluate(async () => (await import("./js/ui/Dialogs.js")).settingsDialog());
await page.waitForSelector(".dialog .settings");
const translucent = page.locator(".dialog .settings label", { hasText: "Ventanas translúcidas" }).locator("input");
await translucent.uncheck();
check(await page.evaluate(() => !document.getElementById("workspace").classList.contains("translucent")), "desactivar las ventanas translúcidas");
await translucent.check();
await page.click(".dialog .pushbutton.default");
page.evaluate(() => window.kaint.actions.get("about").trigger());
await page.waitForSelector(".dialog .about .kaint-logo img");
await page.keyboard.press("Escape");
check(await page.evaluate(() => !document.querySelector(".dialog")), "Esc cierra Acerca de");
await page.evaluate(() => window.kaint.closeDocument(window.kaint.doc, false));
const swapped = await page.evaluate(async () => {
    const { AppState } = await import("./js/AppState.js");
    const before = AppState.primary;
    document.getElementById("view").focus();
    document.getElementById("view").dispatchEvent(new KeyboardEvent("keydown", { key: "x", code: "KeyX", bubbles: true }));
    return AppState.secondary === before;
});
check(swapped, "X intercambia los colores");

// Animaciones desactivadas: sin deslizamiento
await page.evaluate(async () => (await import("./js/Settings.js")).Settings.set("animations", false));
await page.evaluate(() => window.kaint.moveLayerTo(1, 0));
check(await page.evaluate(() => document.documentElement.classList.contains("no-animations")
                          && document.querySelector(".layer-list .row").getAnimations().length === 0), "sin animaciones no se deslizan");

// Como Kaint de escritorio: iconos de herramientas dibujados (Formas ocupa su fila), barras con
// los PNG de los menús salvo de Cortar a Anular la selección (dibujados como los de Paint.NET 4,
// en gris los que no se pueden usar) e icono de la herramienta en la barra de estado.
await page.evaluate(() => window.kaint.addDocument(new window.kaint.doc.constructor(400, 300)));
await tool("paintbrush");
const icons = await page.evaluate(() => {
    const buttons = [...document.querySelectorAll(".tools-grid .tbutton")];
    const last = buttons[buttons.length - 1].getBoundingClientRect(), first = buttons[0].getBoundingClientRect();
    return { drawn: buttons.every((b) => b.querySelector(".kicon").tagName === "CANVAS"), lastWide: last.width > first.width * 1.5,
             toolbar: [...document.querySelectorAll("#main-toolbar .kicon")].every((i) => i.tagName === (i.dataset.icon.startsWith("toolbar-") ? "CANVAS" : "IMG")),
             drawnBar: [...document.querySelectorAll("#main-toolbar .kicon")].filter((i) => i.tagName === "CANVAS").map((i) => i.dataset.icon).join(),
             cutGray: getComputedStyle(document.querySelector('#main-toolbar .kicon[data-icon="toolbar-cut"]')).filter.includes("grayscale")
                      && document.querySelector('#main-toolbar .kicon[data-icon="toolbar-cut"]').closest("button").disabled,
             pasteColored: getComputedStyle(document.querySelector('#main-toolbar .kicon[data-icon="toolbar-paste"]')).filter === "none",
             status: document.querySelector(".status-bar .hint-icon .kicon")?.dataset.icon };
});
check(icons.drawn && icons.lastWide && icons.toolbar && icons.drawnBar === "toolbar-cut,toolbar-copy,toolbar-paste,toolbar-crop,toolbar-deselect"
      && icons.cutGray && icons.pasteColored && icons.status === "paintbrush", `iconos de herramientas y barras (${JSON.stringify(icons)})`);

// Mover píxeles seleccionados: sin selección, un clic selecciona todo; cada arrastre es un paso
await setColors({ r: 0, g: 0, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await tool("pencil");
await drag([[50, 50], [60, 50]]);
await tool("moveselectedpixels");
await click(200, 150);
check(await lastStep() === "Seleccionar todo", "un clic con Mover píxeles seleccionados sin selección selecciona todo");
await drag([[55, 50], [55, 80]]);
// Se mueve toda la capa (con su fondo blanco): arriba queda el hueco transparente.
check(await lastStep() === "Mover píxeles seleccionados" && JSON.stringify(await pixel(55, 80)) === "[0,0,0,255]" && (await pixel(55, 10))[3] === 0,
      "mover píxeles deja un paso al soltar");
await page.keyboard.press("Control+z");
check(JSON.stringify(await pixel(55, 50)) === "[0,0,0,255]" && (await pixel(55, 10))[3] === 255, "deshacer el movimiento");
await page.keyboard.press("Control+d");

// Formas, cubo, degradado y texto: un paso al crear, "Editar…" al cambiar y "Finalizar…" con
// Intro; al volver a un paso se sigue editando.
await tool("shapes");
await drag([[100, 100], [160, 160]]);
check(await lastStep() === "Rectángulo", "la forma entra en el historial al soltar");
await drag([[130, 130], [150, 150]]);
check(await lastStep() === "Editar forma", "mover la forma es «Editar forma»");
await page.keyboard.press("Control+z");
check(await lastStep() === "Rectángulo" && await page.evaluate(() => window.kaint.tools.current.editing), "deshacer vuelve a la forma y sigue editable");
await page.keyboard.press("Enter");
check(await lastStep() === "Finalizar forma", "Intro termina la forma");
await tool("paintbucket");
await click(300, 250);
check(await lastStep() === "Cubo de pintura", "el cubo entra en el historial al momento");
await setColors({ r: 200, g: 0, b: 0, a: 255 }, { r: 255, g: 255, b: 255, a: 255 });
await page.keyboard.press("Enter");
check(JSON.stringify(await pixel(300, 250)) === "[200,0,0,255]" && await lastStep() === "Finalizar cubo de pintura",
      "el relleno sigue a los colores hasta terminar");
await tool("text");
await click(20, 200);
await page.keyboard.type("Kaint");
check(await lastStep() === "Texto", "lo escrito aparece en el historial al momento");
await page.keyboard.press("Escape");
check(await lastStep() === "Finalizar texto", "Esc termina el texto");

// Varita mágica: la selección sigue a la tolerancia hasta pulsar Intro
await tool("magicwand");
await click(380, 20);
const wand1 = await page.evaluate(() => window.kaint.doc.selection.area());
await page.evaluate(async () => (await import("./js/AppState.js")).AppState.updateTools({ tolerance: 100 }));
const wand2 = await page.evaluate(() => window.kaint.doc.selection.area());
await page.keyboard.press("Enter");
check(wand2 > wand1 && await lastStep() === "Varita mágica", `la varita sigue a la tolerancia (${wand1} → ${wand2})`);
await page.evaluate(async () => (await import("./js/AppState.js")).AppState.updateTools({ tolerance: 50 }));
await page.keyboard.press("Control+d");

// Zum a un rectángulo: justo ese rectángulo, sin margen
await tool("zoom");
await drag([[100, 100], [200, 150]]);
const fit = await page.evaluate(() => {
    const v = window.kaint.view, s = v.viewSize(), a = v.imageToView(100, 100), b = v.imageToView(200, 150);
    return Math.abs(b.x - a.x - s.w) < 2 || Math.abs(b.y - a.y - s.h) < 2;
});
check(fit, "el zum llena la vista con el rectángulo");
await page.evaluate(() => window.kaint.view.zoomToWindow());

// Reglas: oscuras como las de paint.js y las ventanas no las tapan
await page.evaluate(() => window.kaint.actions.get("rulers").trigger());
await page.evaluate(() => new Promise((r) => setTimeout(r, 100)));
const rulers = await page.evaluate(() => {
    const ruler = document.querySelector("canvas.ruler.h"), d = ruler.getContext("2d").getImageData(1, 1, 1, 1).data;
    return { dark: d[0] < 60, panels: [...document.querySelectorAll(".floating-panel")].every((p) => p.offsetLeft >= 20 && p.offsetTop >= 20) };
});
check(rulers.dark && rulers.panels, `reglas (${JSON.stringify(rulers)})`);
await page.evaluate(() => window.kaint.actions.get("rulers").trigger());

// Atajos de teclado editables
const keys = await page.evaluate(() => {
    const app = window.kaint, taken = app.setShortcut("save", "Ctrl+O");
    const r = taken?.id === "open" && app.actions.get("open").shortcuts.length === 0;
    app.resetShortcuts();
    return r && app.actions.get("open").shortcuts[0] === "Ctrl+O";
});
check(keys, "cambiar un atajo se lo quita a la acción que lo usaba");

// Paquete de iconos de paint.js: cambia el icono de Abrir y el de la herramienta Texto
const pack = await page.evaluate(async () => {
    const { zip } = await import("./js/core/Zip.js");
    const { IconPacks } = await import("./js/IconPacks.js");
    const png = async (color) => {
        const c = new OffscreenCanvas(16, 16), x = c.getContext("2d");
        x.fillStyle = color;
        x.fillRect(0, 0, 16, 16);
        return new Uint8Array(await (await c.convertToBlob({ type: "image/png" })).arrayBuffer());
    };
    const enc = new TextEncoder();
    const file = new File([zip([
        { name: "p/asset-pack.json", data: enc.encode(JSON.stringify({ formatVersion: 1, id: "smoke.pack", name: "Prueba" })) },
        { name: "p/icons/menu_file_open_icon.png", data: await png("#f00") },
        { name: "p/icons/text_tool_icon.png", data: await png("#00f") },
    ])], "p.zip");
    const { id } = await IconPacks.install(file);
    await IconPacks.activate(id);
    const text = document.querySelectorAll(".tools-grid .tbutton")[16].querySelector(".kicon");
    const ok = text.tagName === "IMG" && document.querySelector('#main-toolbar img[data-icon="document-open"]').src.startsWith("blob:");
    await IconPacks.remove(id);
    return ok && document.querySelectorAll(".tools-grid .tbutton")[16].querySelector(".kicon").tagName === "CANVAS";
});
check(pack, "un paquete de iconos cambia los iconos y al quitarlo vuelven los de Kaint");

// Complementos (.paintjs): uno de prueba con un efecto por píxel y un formato de archivo.
const plugin = await page.evaluate(async () => {
    const { zip } = await import("./js/core/Zip.js");
    const { Plugins } = await import("./js/plugins/Plugins.js");
    const { Effects } = await import("./js/core/Effects.js");
    const enc = new TextEncoder();
    const manifest = { id: "smoke.invert", name: "Prueba", version: "1.0.0", apiVersion: 1, author: "Kaint", website: "https://example.com/",
                       description: "Prueba", category: "Effects", license: "MIT" };
    const source = `paintDotJs.registerPlugin({ effects: [{ id: "invert-red", name: "Invert Red", category: "Color",
        controls: [paintDotJs.controls.range("amount", "Amount", 0, 1, 1, 0.01)],
        renderPixel({ r, g, b, a, values }) { return [r + (255 - 2 * r) * values.amount, g, b, a]; } }],
        fileTypes: [{ id: "raw", name: "Raw", extensions: [".rawsmoke"], decode() { return { width: 2, height: 1, data: new Uint8Array([255,0,0,255, 0,0,255,255]) }; },
                      encode({ document }) { return new Uint8Array(document.layers[0].data); } }] });`;
    const file = new File([zip([{ name: "plugin.json", data: enc.encode(JSON.stringify(manifest)) }, { name: "plugin.js", data: enc.encode(source) }])], "p.paintjs");
    await Plugins.install(file);
    const app = window.kaint, layer = app.doc.activeLayer;
    layer.ctx.fillStyle = "#ff0000";
    layer.ctx.fillRect(0, 0, 4, 4);
    await app.runEffect(Effects.find((e) => e.id === "plugin.smoke.invert.invert-red"), true);
    const px = [...layer.ctx.getImageData(1, 1, 1, 1).data];
    await app.openFile(new File([new Uint8Array(4)], "x.rawsmoke"));
    const opened = app.doc.width === 2 && [...app.doc.activeLayer.ctx.getImageData(0, 0, 1, 1).data][0] === 255;
    await app.closeDocument(app.doc, false);
    return { px, opened, any: Plugins.anyEffects(), step: app.doc.history.steps[app.doc.history.index].name };
});
check(plugin.px[0] === 0 && plugin.any && plugin.step === "Invert Red" && plugin.opened, `complementos (${JSON.stringify(plugin)})`);
await page.click("#menubar >> text=Efectos");
const effectsMenu = await page.evaluate(() => [...document.querySelectorAll(".popup-menu")].pop()?.textContent || "");
check(effectsMenu.includes("Complementos") && effectsMenu.includes("Color"), "el menú Efectos muestra «Complementos...» y la categoría del complemento");
await page.keyboard.press("Escape");

// Paleta como la de Kaint: dos filas (32 colores) y el menú con las paletas de muestra.
const palette = await page.evaluate(async () => {
    const before = document.querySelectorAll(".colors-panel .palette .swatch").length;
    document.querySelector(".colors-panel .palette-menu").click();
    const items = [...document.querySelectorAll(".popup-menu .item")].map((i) => i.textContent);
    [...document.querySelectorAll(".popup-menu .item")].find((i) => i.textContent.includes("Arcoíris"))?.click();
    const { AppState, rainbowPalette, samePalette } = await import("./js/AppState.js");
    const rainbow = samePalette(AppState.palette, rainbowPalette());
    AppState.resetPalette();
    return { before, items: items.length, rainbow };
});
check(palette.before === 32 && palette.items >= 6 && palette.rainbow, `paleta (${JSON.stringify(palette)})`);

// Tablero de transparencia como el de Kaint, temas Windows 98 y personalizado, cursores con icono
// y overscroll.
const misc = await page.evaluate(async () => {
    const { checkerPattern } = await import("./js/core/Raster.js");
    const { Settings } = await import("./js/Settings.js");
    const c = document.createElement("canvas");
    c.width = c.height = 16;
    const ctx = c.getContext("2d");
    ctx.fillStyle = checkerPattern(ctx, 200);
    ctx.fillRect(0, 0, 16, 16);
    const light = ctx.getImageData(12, 2, 1, 1).data[0], dark = ctx.getImageData(2, 2, 1, 1).data[0];
    Settings.set("colorScheme", 4);
    const classic = document.documentElement.classList.contains("classic");
    Settings.set("customTheme", { base: "dark", colors: { accent: "#ff0000" } });
    Settings.set("colorScheme", 5);
    const custom = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim() === "#ff0000"
        && document.documentElement.classList.contains("dark");
    Settings.set("colorScheme", 1);
    window.kaint.tools.select("pencil");
    const cursor = document.getElementById("view").style.cursor;
    const view = window.kaint.view;
    view.zoomToWindow();
    view.scrollBy(-5000, 0);
    const over = view.origin.x > 24;
    view.zoomToWindow();
    return { light, dark, classic, custom, cursor: cursor.includes("image-set") && cursor.includes(" 2 18"), over };
});
check(misc.light === 200 && misc.dark === 150 && misc.classic && misc.custom && misc.cursor && misc.over, `apariencia y cámara (${JSON.stringify(misc)})`);

// Ventanas como las de Kaint: Herramientas empieza estrecha, con dos columnas, y al ensancharla se
// queda con las columnas enteras que caben.
{
    await page.evaluate(() => Object.values(window.kaint.panels).forEach((p) => p.reset()));
    const columns = () => page.evaluate(() => getComputedStyle(document.querySelector(".tools-grid")).getPropertyValue("--columns").trim());
    const before = await columns();
    const b = await page.evaluate(() => {
        const r = document.querySelector('.floating-panel[data-panel="tools"]').getBoundingClientRect();
        return { x: r.right, y: r.top + r.height / 2, w: r.width };
    });
    await page.mouse.move(b.x + 1, b.y);
    await page.mouse.down();
    await page.mouse.move(b.x + 64, b.y, { steps: 6 });
    await page.mouse.up();
    const after = await columns();
    await page.evaluate(() => window.kaint.panels.tools.reset());
    check(before === "2" && b.w < 90 && after === "4", `Herramientas: dos columnas al empezar y columnas enteras al ensancharla (${before}, ${b.w}, ${after})`);
}

// Colores con «Más», como en Kaint: RGB, MSV y opacidad con un deslizador y una caja por canal, el
// código hexadecimal y los 96 colores.
{
    const colors = await page.evaluate(async () => {
        const { AppState } = await import("./js/AppState.js");
        AppState.setSecondaryActive(false);
        const panel = document.querySelector(".colors-panel"), more = panel.querySelector(".more-button");
        more.click();
        const ext = panel.querySelector(".extended");
        const out = { shown: getComputedStyle(ext).display !== "none", sliders: ext.querySelectorAll(".channel-slider").length,
                      spins: ext.querySelectorAll('input[type="number"]').length, sections: ext.querySelectorAll(".section").length,
                      palette: panel.querySelectorAll(".palette .swatch").length, wide: panel.offsetWidth };
        const hex = ext.querySelector(".hex");
        hex.value = "12AB34";
        hex.dispatchEvent(new Event("input"));
        hex.dispatchEvent(new Event("change"));
        out.hex = AppState.primary.r === 0x12 && AppState.primary.g === 0xab && AppState.primary.b === 0x34;
        const red = ext.querySelector('input[type="number"]');
        red.value = "200";
        red.dispatchEvent(new Event("input"));
        out.red = AppState.primary.r === 200;
        more.click();
        out.back = getComputedStyle(ext).display === "none" && panel.querySelectorAll(".palette .swatch").length === 32;
        AppState.resetColors();
        return out;
    });
    check(colors.shown && colors.sliders === 7 && colors.spins === 7 && colors.sections === 3 && colors.palette === 96 && colors.wide > 400
          && colors.hex && colors.red && colors.back, `Colores ampliada como en Kaint (${JSON.stringify(colors)})`);
}

// Capas: al arrastrar una, se mueve con el ratón y queda donde se suelta.
{
    await page.evaluate(() => {
        window.kaint.actions.get("addLayer").run();
        window.kaint.actions.get("addLayer").run();
    });
    await page.waitForTimeout(200);
    const names = () => page.evaluate(() => window.kaint.doc.layers.map((l) => l.id));
    const before = await names();
    const rows = await page.evaluate(() => [...document.querySelectorAll(".layer-list .row")].map((r) => {
        const b = r.getBoundingClientRect();
        return { x: b.left + b.width / 2, y: b.top + b.height / 2 };
    }));
    await page.mouse.move(rows[0].x, rows[0].y);
    await page.mouse.down();
    await page.mouse.move(rows[0].x, rows[2].y, { steps: 10 });
    const live = await page.evaluate(() => !!document.querySelector(".layer-list .row.live"));
    await page.mouse.up();
    const after = await names();
    for (let i = 0; i < 3; ++i)
        await page.evaluate(() => window.kaint.undo());
    const n = before.length;
    check(live && after[n - 3] === before[n - 1] && after[n - 2] === before[n - 3] && after[n - 1] === before[n - 2],
          `las capas se arrastran en vivo (${before} → ${after})`);
}

// Seleccionando, la rueda desplaza la vista y la selección sigue al puntero sobre la imagen.
{
    await tool("rectangleselect");
    await page.evaluate(() => {
        window.kaint.view.setZoom(4);
        window.kaint.view.centerOn(20, 20);
    });
    const p = await at(20, 20);
    await page.mouse.move(p.x, p.y);
    await page.mouse.down();
    await page.mouse.move(p.x + 40, p.y + 40, { steps: 4 });
    const before = await page.evaluate(() => window.kaint.doc.selection.bounds()?.h ?? 0);
    await page.mouse.wheel(0, 120);
    await page.waitForTimeout(50);
    const after = await page.evaluate(() => window.kaint.doc.selection.bounds()?.h ?? 0);
    await page.mouse.up();
    await page.evaluate(() => {
        window.kaint.view.zoomToWindow();
        window.kaint.undo();
    });
    check(after > before, `la rueda desplaza la vista durante la selección y esta sigue al puntero (${before} → ${after})`);
}

// Mover píxeles seleccionados: al cambiar el tamaño, píxel a píxel (las esquinas en píxeles
// enteros).
{
    const corners = await page.evaluate(async () => {
        const { Selection } = await import("./js/core/Selection.js");
        const k = window.kaint, doc = k.doc;
        doc.setSelection(Selection.rect(doc.width, doc.height, 10, 10, 1, 20));
        k.tools.select("moveselectedpixels");
        const tool = k.tools.current, view = k.view;
        const ev = (x, y, button = "left") => { const v = view.imageToView(x, y); return { x, y, vx: v.x, vy: v.y, button, shift: false, ctrl: false, alt: false }; };
        tool.onPointerDown(ev(11, 20));
        tool.drag = { ...tool.drag, kind: "resize", handle: 3 };
        tool.onPointerMove(ev(14.37, 20));
        tool.onPointerMove(ev(16.71, 20.4));
        const box = tool.drag.session.box, m = box.matrix(), b = box.base;
        const pts = [m.transformPoint({ x: b.x, y: b.y }), m.transformPoint({ x: b.x + b.w, y: b.y + b.h })];
        tool.onPointerUp(ev(16.71, 20.4));
        k.undo();
        k.undo();
        return pts.flatMap((q) => [q.x, q.y]);
    });
    check(corners.every((v) => Math.abs(v - Math.round(v)) < 1e-6) && corners[2] - corners[0] > 1,
          `al cambiar el tamaño con Mover, píxel a píxel (${corners})`);
}

// Pincel y goma: el contorno va en el propio cursor (sin el retraso del lienzo) si cabe.
{
    const brush = await page.evaluate(async () => {
        const { AppState } = await import("./js/AppState.js");
        const k = window.kaint;
        k.view.setZoom(1);
        const size = AppState.tools.brushSize;
        AppState.updateTools({ brushSize: 30 });
        k.tools.select("eraser");
        const small = { inCursor: k.view.outlineInCursor, cursor: k.view.canvas.style.cursor };
        AppState.updateTools({ brushSize: 400 });
        const big = k.view.outlineInCursor;
        AppState.updateTools({ brushSize: size });
        k.view.zoomToWindow();
        k.tools.select("pencil");
        return { small: small.inCursor && small.cursor.includes("image-set"), big };
    });
    check(brush.small && !brush.big, `el contorno del pincel va en el cursor si cabe (${JSON.stringify(brush)})`);
}

// Estilo de la interfaz como Paint.NET: más compacto, ventanas sin borde, sin ir al principio ni al
// final en Historial, Colores sin el selector de ranura y «Más >>» junto a la rueda.
{
    const pdn = await page.evaluate(async () => {
        const { Settings } = await import("./js/Settings.js");
        Settings.set("uiStyle", 1);
        const panel = document.querySelector('.floating-panel[data-panel="history"]');
        const out = {
            root: document.documentElement.classList.contains("pdn"),
            border: getComputedStyle(panel).borderTopWidth,
            jumps: [...document.querySelectorAll(".history-jump")].every((b) => getComputedStyle(b).display === "none"),
            select: getComputedStyle(document.querySelector(".colors-panel .top")).display,
            more: document.querySelector(".colors-panel .more-button").textContent,
            beside: !!document.querySelector(".colors-panel .wheel-column .more-button"),
            tool: document.querySelector(".tools-grid .tbutton").offsetWidth,
        };
        Settings.set("uiStyle", 0);
        out.back = !document.documentElement.classList.contains("pdn") && document.querySelector(".colors-panel .more-button").textContent === "Más »"
            && !document.querySelector(".colors-panel .wheel-column .more-button");
        return out;
    });
    check(pdn.root && pdn.border === "0px" && pdn.jumps && pdn.select === "none" && pdn.more === "Más >>" && pdn.beside && pdn.tool === 24 && pdn.back,
          `estilo de Paint.NET (${JSON.stringify(pdn)})`);
}

// Acerca de como el de Kaint: el logo «kaint.html», la firma con la ola y las contribuciones.
{
    await page.evaluate(() => { void window.kaint.actions.get("about").run(); });
    await page.waitForSelector(".about .credits");
    const about = await page.evaluate(() => ({ logo: document.querySelector(".about .kaint-logo")?.textContent,
                                               wave: document.querySelectorAll(".about .wave span").length,
                                               credits: document.querySelectorAll(".about .credits > div").length }));
    await page.keyboard.press("Escape");
    check(about.logo === "kaint.html" && about.wave > 8 && about.credits >= 3, `Acerca de (${JSON.stringify(about)})`);
}

// La flecha de la tira, como en Kaint: se queda pulsada y debajo salen las imágenes con su
// miniatura y su nombre (la actual resaltada); se elige con el ratón o con el teclado.
{
    const list = await page.evaluate(async () => {
        const k = window.kaint;
        if (k.docs.length < 2) {
            const { Document } = await import("./js/core/Document.js");
            k.addDocument(new Document(64, 48));
        }
        document.getElementById("image-list").click();
        const popup = document.querySelector(".image-list-popup");
        const out = { rows: popup?.querySelectorAll(".row").length, thumbs: popup?.querySelectorAll(".row canvas").length,
                      selected: [...(popup?.querySelectorAll(".row") || [])].findIndex((r) => r.classList.contains("selected")),
                      current: k.docs.indexOf(k.doc), down: document.getElementById("image-list").classList.contains("down") };
        return out;
    });
    await page.keyboard.press("ArrowUp");
    await page.keyboard.press("Enter");
    const chosen = await page.evaluate(() => ({ current: window.kaint.docs.indexOf(window.kaint.doc), open: !!document.querySelector(".image-list-popup"),
                                               down: document.getElementById("image-list").classList.contains("down") }));
    check(list.rows >= 2 && list.thumbs === list.rows && list.selected === list.current && list.down
          && chosen.current === list.current - 1 && !chosen.open && !chosen.down, `lista de imágenes (${JSON.stringify(list)}, ${JSON.stringify(chosen)})`);
    await page.evaluate(() => window.kaint.closeDocument(window.kaint.docs[window.kaint.docs.length - 1], false));
}

// Letras de acceso de los menús: sin subrayar salvo con Alt (o siempre, en Configuración).
{
    const deco = () => page.evaluate(() => getComputedStyle(document.querySelector(".menubar u")).textDecorationLine);
    const off = await deco();
    await page.keyboard.down("Alt");
    const alt = await deco();
    await page.keyboard.up("Alt");
    const released = await deco();
    await page.evaluate(async () => (await import("./js/Settings.js")).Settings.set("menuMnemonics", true));
    const always = await deco();
    await page.evaluate(async () => (await import("./js/Settings.js")).Settings.set("menuMnemonics", false));
    check(off === "none" && alt === "underline" && released === "none" && always === "underline",
          `subrayado de las letras de acceso (${off}, ${alt}, ${released}, ${always})`);
}

// Efectos como los de Kaint: los mismos 55 (con Cuantizar, Boceto borroso, Enderezar y Giro y
// zum), las mismas opciones (el desenfoque gaussiano con Potenciar Gamma y Calidad), el diálogo con
// sus controles, Ctrl+Z dentro del diálogo y el cálculo en segundo plano igual al de aquí.
{
    const fx = await page.evaluate(async () => {
        const { Effects, findEffect, defaultValues } = await import("./js/core/Effects.js");
        const { startEffect } = await import("./js/core/EffectRunner.js");
        const gauss = findEffect("gaussianblur");
        const w = 64, h = 48, data = new Uint8ClampedArray(w * h * 4);
        for (let i = 0; i < data.length; i += 4) {
            data[i] = (i / 4) % 256; data[i + 1] = (i / 4 / w) * 5; data[i + 2] = (i * 3) % 256; data[i + 3] = 128 + ((i / 4) % 128);
        }
        const src = new ImageData(data, w, h), values = { ...defaultValues(gauss), radius: 6, gamma: 1.5, quality: 3 };
        const rect = { x: 3, y: 5, w: 50, h: 37 };
        const here = gauss.render(src, values, rect, {}).data;
        const there = (await startEffect(gauss, src, values, rect, {}).promise).data;
        return {
            count: Effects.filter((e) => e.builtin).length, gaussian: gauss.params.map((p) => p.key).join(","),
            extra: ["quantize", "sketchblur", "straighten", "rotatezoom"].every((id) => findEffect(id)),
            same: here.every((v, i) => v === there[i]),
        };
    });
    check(fx.count === 55 && fx.gaussian === "radius,gamma,quality" && fx.extra && fx.same, `efectos como los de Kaint (${JSON.stringify(fx)})`);
    const done = page.evaluate(async () => {
        const { findEffect } = await import("./js/core/Effects.js");
        return window.kaint.runEffect(findEffect("gaussianblur"), false);
    });
    await page.waitForSelector(".effect-dialog");
    const ui = await page.evaluate(() => ({ sliders: document.querySelectorAll(".effect-dialog input[type=range]").length,
                                             resets: document.querySelectorAll(".effect-dialog .fx-reset").length }));
    const radius = page.locator(".effect-dialog input[type=number] >> nth=0");
    await radius.fill("12");
    await radius.dispatchEvent("change");
    await page.waitForTimeout(700);
    await page.keyboard.press("Control+z");
    const undone = await radius.inputValue();
    await page.click(".effect-dialog .pushbutton.default");
    await done;
    check(ui.sliders === 3 && ui.resets === 3 && undone === "2.0" && await lastStep() === "Desenfoque gaussiano",
          `diálogo del desenfoque gaussiano (${JSON.stringify(ui)}, deshacer: ${undone})`);
    // Controles especiales: ángulo, centro con miniatura, globo, color, pestañas, curvas y niveles.
    const special = {};
    for (const [id, selector] of [["motionblur", ".fx-angle-chooser"], ["zoomblur", ".fx-pan"], ["rotatezoom", ".fx-rollball"],
                                  ["dropshadow", ".fx-color"], ["clouds", ".fx-tab"], ["curves", ".fx-curves"], ["levels", ".fx-histogram"]]) {
        const run = page.evaluate(async (id) => {
            const { findEffect } = await import("./js/core/Effects.js");
            return window.kaint.runEffect(findEffect(id), false);
        }, id);
        await page.waitForSelector(".effect-dialog");
        special[id] = await page.evaluate((sel) => document.querySelectorAll(`.effect-dialog ${sel}`).length, selector);
        await page.keyboard.press("Escape");
        await run;
    }
    check(Object.values(special).every((n) => n > 0) && special.clouds === 2 && special.levels === 2, `controles de los efectos (${JSON.stringify(special)})`);
    await page.click("#menubar >> text=Capas");
    const layersMenu = await page.evaluate(() => [...document.querySelectorAll(".popup-menu .item")].map((i) => i.textContent));
    await page.keyboard.press("Escape");
    check(layersMenu.some((t) => t.startsWith("Giro y zum")), "el menú Capas tiene «Giro y zum...»");
}

// Cambiar el tamaño como en Kaint: remuestreo (con supermuestreo y Lanczos), por porcentaje y la
// resolución, que se guarda en el archivo.
{
    const before = await page.evaluate(() => ({ w: window.kaint.doc.width, h: window.kaint.doc.height }));
    const done = page.evaluate(() => window.kaint.resizeImage());
    await page.waitForSelector(".dialog .size-editor");
    const methods = await page.evaluate(() => [...document.querySelector(".dialog select").options].map((o) => o.value));
    await page.fill(".dialog .size-editor input[type=number] >> nth=0", "50");
    await page.click(".dialog .pushbutton.default");
    await done;
    const after = await page.evaluate(() => ({ w: window.kaint.doc.width, h: window.kaint.doc.height, step: window.kaint.doc.history.steps[window.kaint.doc.history.index].name }));
    check(methods.length === 8 && methods.includes("supersampling") && methods.includes("lanczos")
          && after.w === Math.round(before.w / 2) && after.h === Math.round(before.h / 2) && after.step === "Cambiar el tamaño",
          `cambiar el tamaño al 50 % (${JSON.stringify(before)} → ${JSON.stringify(after)})`);
    await page.keyboard.press("Control+z");
    const dpi = await page.evaluate(async () => {
        const { encode, openFile, SaveFormats } = await import("./js/core/ImageIO.js");
        const doc = window.kaint.doc, old = doc.dpi;
        doc.dpi = 300;
        const back = await openFile(new File([await encode(doc, SaveFormats.find((f) => f.ext === "png"))], "x.png"));
        doc.dpi = old;
        return back.dpi;
    });
    check(dpi === 300, `la resolución se guarda en el PNG (${dpi})`);
}

// Edición > Copiar selección, Pegar selección y Ampliar la selección; Ver > unidades.
{
    const r = await page.evaluate(async () => {
        const app = window.kaint, doc = app.doc;
        const { Selection, SelectionMode } = await import("./js/core/Selection.js");
        doc.setSelection(Selection.rect(doc.width, doc.height, 10, 20, 30, 40));
        await app.copySelection();
        const json = app.selectionClipboard;
        doc.setSelection(Selection.rect(doc.width, doc.height, 100, 100, 10, 10));
        await app.pasteSelection(SelectionMode.Union);
        const pasted = doc.selection.area();
        const grown = doc.selection.grown(2).bounds();
        const feathered = doc.selection.feathered(4).bounds();
        app.setUnit(2);
        const cm = app.unitButton.textContent;
        app.setUnit(0);
        app.deselect();
        return { json: JSON.parse(json).polygonList[0], pasted, grown, feathered, cm };
    });
    check(r.json === "10,20,40,20,40,60,10,60,10,20" && r.pasted === 1300 && r.grown.x === 8 && r.grown.w === 104 && r.feathered.x < 10 && r.cm === "cm",
          `copiar y pegar la selección, ampliar, difuminar y unidades (${JSON.stringify(r)})`);
}

// Formatos como los de Kaint: GIF animado (una capa por fotograma, la duración en el nombre),
// TIFF, TGA, DDS, BMP e ICO de ida y vuelta, Paint.NET (.pdn) con sus capas y "Configuración de
// guardado" con la vista previa del archivo.
{
    const pdn = [...(await readFile(join(root, "tests/fixtures/capas.pdn")))];
    const formats = await page.evaluate(async (pdnBytes) => {
        const { encode, openFile, SaveFormats } = await import("./js/core/ImageIO.js");
        const { Document } = await import("./js/core/Document.js");
        const doc = new Document(40, 30, { background: "#336699" });
        doc.layers[0].name = "Uno (120ms)";
        const { Layer } = await import("./js/core/Document.js");
        const second = new Layer(40, 30, "Dos (300ms)");
        second.ctx.fillStyle = "#ff0000";
        second.ctx.fillRect(5, 5, 10, 10);
        doc.layers.push(second);
        const out = {};
        for (const ext of ["gif", "tiff", "tga", "dds", "bmp", "ico", "png"]) {
            const f = SaveFormats.find((x) => x.ext === ext);
            const back = await openFile(new File([await encode(doc, f)], `prueba.${ext}`));
            out[ext] = { w: back.width, layers: back.layers.map((l) => l.name), px: [...back.layers.at(-1).ctx.getImageData(7, 7, 1, 1).data] };
        }
        const p = await openFile(new File([new Uint8Array(pdnBytes)], "capas.pdn"));
        out.pdn = { w: p.width, h: p.height, layers: p.layers.map((l) => `${l.name}/${l.opacity}/${l.blend}/${l.visible ? 1 : 0}`) };
        return out;
    }, pdn);
    check(formats.gif.layers.join() === "Fotograma 1 (120ms),Fotograma 2 (300ms)" && formats.gif.px[0] > 200 && formats.gif.px[1] < 40,
          `GIF animado de ida y vuelta (${JSON.stringify(formats.gif)})`);
    check(["tiff", "tga", "dds", "bmp", "png"].every((e) => formats[e].w === 40 && formats[e].px[0] > 200 && formats[e].px[1] < 40) && formats.ico.w === 256,
          `TIFF, TGA, DDS, BMP, ICO y PNG (${JSON.stringify(formats)})`);
    check(formats.pdn.w === 40 && formats.pdn.h === 30 && formats.pdn.layers.join() === "Fondo/255/normal/1,Capa 2/96/overlay/1,Capa 3/255/screen/0",
          `abrir .pdn con sus capas (${JSON.stringify(formats.pdn)})`);
    await page.evaluate(async () => {
        window.showSaveFilePicker = undefined;
        const { Document } = await import("./js/core/Document.js");
        window.kaint.addDocument(new Document(64, 48));
    });
    const saving = page.evaluate(() => window.kaint.writeFile(window.kaint.doc, "prueba.webp", null));
    await page.waitForSelector(".save-config .barslider");
    await page.waitForFunction(() => /\d/.test(document.querySelector(".save-config .save-status")?.textContent || ""));
    const status = await page.textContent(".save-config .save-status");
    const download = page.waitForEvent("download");
    await page.click(".save-config .pushbutton.default");
    const file = await download;
    check(await saving && file.suggestedFilename() === "prueba.webp" && /Tamaño del archivo: [\d,.]+ (bytes|KiB|MiB)/.test(status),
          `configuración de guardado con la vista previa (${status})`);
    await page.evaluate(() => window.kaint.closeDocument(window.kaint.doc, false));
}

// Perfil de colores como en Kaint: convertir de Adobe RGB a sRGB da los mismos números que Qt, y
// el perfil se guarda dentro del PNG, el JPEG, el WebP y el TIFF y vuelve al abrirlos.
{
    const icc = await page.evaluate(async () => {
        const P = await import("./js/core/ColorProfiles.js");
        const { encode, openFile, SaveFormats } = await import("./js/core/ImageIO.js");
        const { Document } = await import("./js/core/Document.js");
        const adobe = P.NamedProfiles.find((n) => n.name === "Adobe RGB");
        const px = P.transformPixels(new Uint8ClampedArray([200, 100, 50, 255]), adobe, null);
        const doc = new Document(16, 16, { background: "#c86432" });
        doc.colorProfile = adobe;
        const back = {};
        for (const ext of ["png", "jpg", "webp", "tiff"]) {
            const f = SaveFormats.find((x) => x.ext === ext);
            const d = await openFile(new File([await encode(doc, f)], `perfil.${ext}`));
            back[ext] = P.describe(d.colorProfile);
        }
        return { px: [...px].slice(0, 3), reread: P.describe(P.readIcc(adobe.icc)), back };
    });
    check(icc.px.join() === "227,100,42" && icc.reread === "Adobe RGB" && Object.values(icc.back).every((n) => n === "Adobe RGB"),
          `perfil de colores (${JSON.stringify(icc)})`);
}

// Límite de memoria del historial (se descartan los pasos más antiguos y se avisa) y copias de
// recuperación: una pestaña con cambios sin guardar se cuelga y Kaint ofrece recuperarlos.
{
    const trimmed = await page.evaluate(async () => {
        const k = window.kaint, { Document } = await import("./js/core/Document.js");
        const { Settings } = await import("./js/Settings.js");
        const doc = new Document(800, 600);
        k.addDocument(doc);
        Settings.set("historyLimitMB", 10);
        for (let i = 0; i < 5; ++i) {
            const before = doc.snapshot();
            doc.activeLayer.ctx.fillStyle = `rgb(${i * 50},0,0)`;
            doc.activeLayer.ctx.fillRect(0, 0, 800, 600);
            doc.pushSnapshot(before, `Paso ${i + 1}`, "edit-fill");
        }
        await new Promise((r) => setTimeout(r, 800));
        const out = { steps: doc.history.steps.map((st) => st.name).join(), hint: document.querySelector("#status-bar .hint").textContent };
        Settings.set("historyLimitMB", 4096);
        await k.closeDocument(doc, false);
        return out;
    });
    check(trimmed.steps === "Paso 3,Paso 4,Paso 5" && /3 pasos más antiguos/.test(trimmed.hint), `límite de memoria del historial (${JSON.stringify(trimmed)})`);

    const other = await context.newPage();
    await other.goto(url);
    await other.waitForFunction(() => window.kaint?.doc);
    await other.evaluate(async () => {
        const doc = window.kaint.doc;
        doc.name = "Recuperable";
        const before = doc.snapshot();
        doc.activeLayer.ctx.fillStyle = "#00ff00";
        doc.activeLayer.ctx.fillRect(0, 0, 50, 50);
        doc.pushSnapshot(before, "Relleno", "edit-fill");
        await window.kaint.autosave();
    });
    const cdp = await context.newCDPSession(other);
    cdp.send("Page.crash").catch(() => {});
    await page.waitForTimeout(1000);
    const offered = page.evaluate(() => window.kaint.offerRecovery());
    await page.waitForSelector(".dialog .choice-button");
    const text = await page.textContent(".dialog");
    await page.click(".dialog .choice-button >> nth=0");
    await offered;
    const recovered = await page.evaluate(() => {
        const d = window.kaint.doc;
        return { name: d.displayName(), modified: d.isModified(), px: [...d.activeLayer.ctx.getImageData(10, 10, 1, 1).data].join() };
    });
    check(/Recuperable/.test(text) && recovered.name === "Recuperable" && recovered.modified && recovered.px === "0,255,0,255",
          `copias de recuperación (${JSON.stringify(recovered)})`);
    await page.evaluate(() => window.kaint.closeDocument(window.kaint.doc, false));
    await other.close().catch(() => {});
}

// Configuración > Herramientas: las opciones predeterminadas (con ellas empieza cada sesión) se
// cargan desde la barra y se restablecen, sin cambiar la barra.
{
    await page.evaluate(async () => {
        (await import("./js/AppState.js")).AppState.updateTools({ hardness: 33 });
        (await import("./js/ui/Dialogs.js")).settingsDialog(window.kaint, "tools");
    });
    await page.waitForSelector(".tool-options-panel .flow");
    await page.click(".settings .page .row-buttons .pushbutton >> nth=0");
    const loaded = await page.evaluate(() => JSON.parse(localStorage.getItem("kaint.toolDefaults")).hardness);
    await page.click(".settings .page .row-buttons .pushbutton >> nth=1");
    const reset = await page.evaluate(async () => [JSON.parse(localStorage.getItem("kaint.toolDefaults")).hardness,
                                                   (await import("./js/AppState.js")).AppState.tools.hardness]);
    await page.keyboard.press("Escape");
    check(loaded === 33 && reset[0] === 75 && reset[1] === 33, `opciones predeterminadas de las herramientas (${loaded}, ${reset})`);
}

// Opciones de la barra como las de Kaint: "Sobrescribir" con un color transparente abre un hueco,
// el cubo desmezcla el borde suavizado y Mover píxeles seleccionados gira con el muestreo bicúbico.
{
    const opts = await page.evaluate(async () => {
        const k = window.kaint, { Document } = await import("./js/core/Document.js"), { AppState } = await import("./js/AppState.js");
        const doc = new Document(300, 200);
        k.addDocument(doc);
        const px = (x, y) => [...doc.activeLayer.ctx.getImageData(x, y, 1, 1).data].join();
        const out = {};
        k.tools.select("paintbrush");
        const saved = { ...AppState.tools }, primary = { ...AppState.primary };
        AppState.updateTools({ blend: "overwrite", brushSize: 20, antialias: false });
        AppState.setPrimary({ r: 0, g: 0, b: 0, a: 0 });
        const brush = k.tools.current;
        brush.onPointerDown({ x: 50, y: 50, button: "left" });
        brush.onPointerUp({ x: 50, y: 50, button: "left" });
        out.hole = px(50, 50);
        AppState.updateTools({ blend: "normal", antialias: true });
        const c = doc.activeLayer.ctx;
        c.fillStyle = "#fff"; c.fillRect(0, 0, 300, 200); c.fillStyle = "#000";
        c.beginPath(); c.arc(100, 100, 40, 0, Math.PI * 2); c.fill();
        AppState.setPrimary({ r: 255, g: 0, b: 0, a: 255 });
        k.tools.select("paintbucket");
        const bucket = k.tools.current;
        bucket.onPointerDown({ x: 100.5, y: 100.5, button: "left" });
        bucket.onPointerUp({});
        out.edge = px(140, 100);
        out.inside = px(100, 100);
        bucket.finish();
        const { drawTransformed } = await import("./js/core/Resample.js");
        const src = new ImageData(new Uint8ClampedArray([0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255]), 2, 2);
        const dst = new ImageData(4, 4);
        drawTransformed(dst, src, new DOMMatrix().scale(2, 2), "bicubic", false);
        out.bicubic = [...dst.data.slice(4, 8)].join();
        AppState.updateTools(saved);
        AppState.setPrimary(primary);
        await k.closeDocument(doc, false);
        return out;
    });
    check(opts.hole === "0,0,0,0" && opts.inside === "255,0,0,255" && /^255,\d+,\d+,255$/.test(opts.edge) && opts.edge !== "255,0,0,255" &&
          opts.bicubic !== "0,0,0,255" && opts.bicubic !== "255,255,255,255",
          `Sobrescribir, cubo de pintura y muestreo bicúbico (${JSON.stringify(opts)})`);
}

// Formas personalizadas de Paint.NET (.xaml) como las de Kaint: se añaden, aparecen en su grupo
// del desplegable y se dibujan estiradas al rectángulo (la geometría combinada también).
{
    const xaml = `<ps:SimpleGeometryShape xmlns="clr-namespace:PaintDotNet.UI.Media;assembly=PaintDotNet.Framework"
        xmlns:ps="clr-namespace:PaintDotNet.Shapes;assembly=PaintDotNet.Framework" DisplayName="Marco">
      <CombinedGeometry GeometryCombineMode="Exclude" Geometry1="M 0,0 L 100,0 L 100,100 L 0,100 Z">
        <CombinedGeometry.Geometry2><RectangleGeometry Rect="25,25,50,50" /></CombinedGeometry.Geometry2>
      </CombinedGeometry></ps:SimpleGeometryShape>`;
    const custom = await page.evaluate(async (xaml) => {
        const k = window.kaint, { Document } = await import("./js/core/Document.js"), { AppState } = await import("./js/AppState.js");
        const { CustomShapes } = await import("./js/core/CustomShapes.js");
        const errors = await CustomShapes.add([new File([xaml], "marco.xaml"), new File(["<Nada/>"], "mala.xaml")]);
        const shape = CustomShapes.all().find((c) => c.name === "Marco");
        const doc = new Document(200, 200);
        k.addDocument(doc);
        k.tools.select("shapes");
        const saved = { ...AppState.tools }, primary = { ...AppState.primary };
        AppState.updateTools({ shape: `custom:${shape.id}`, shapeStyle: "fill", fill: 0, blend: "normal" });
        AppState.setPrimary({ r: 0, g: 0, b: 255, a: 255 });
        const t = k.tools.current;
        t.onPointerDown({ x: 0, y: 0, button: "left" });
        t.onPointerMove({ x: 200, y: 200, button: "left" });
        t.onPointerUp({ x: 200, y: 200, button: "left" });
        t.finish();
        const px = (x, y) => [...doc.activeLayer.ctx.getImageData(x, y, 1, 1).data].join();
        const out = { errors: errors.length, frame: px(20, 20), hole: px(100, 100), step: doc.history.steps[1]?.name };
        AppState.updateTools(saved);
        AppState.setPrimary(primary);
        await k.closeDocument(doc, false);
        CustomShapes.remove([shape.id]);
        return out;
    }, xaml);
    check(custom.errors === 1 && custom.frame === "0,0,255,255" && custom.hole === "255,255,255,255" && custom.step === "Marco",
          `formas personalizadas (${JSON.stringify(custom)})`);
}

// Todas las páginas de Configuración se abren sin errores.
for (const id of ["interface", "canvas", "tools", "tablet", "performance", "plugins", "updates", "diagnostics", "iconpacks", "fonts", "shapes", "keyboard"]) {
    // Sin esperar al diálogo (se resuelve al cerrarlo).
    await page.evaluate(async (id) => {
        (await import("./js/ui/Dialogs.js")).settingsDialog(window.kaint, id);
    }, id);
    await page.waitForSelector(".settings .page > *");
    await page.keyboard.press("Escape");
}

await shot("final.png");
for (const e of errors) {
    failures++;
    console.error(`ERROR: ${e}`);
}
await browser.close();
server.close();
console.log(failures ? `${failures} fallos` : "La prueba de humo de la web pasó.");
process.exitCode = failures ? 1 : 0;
