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
const page = await browser.newPage({ viewport: { width: 1400, height: 860 }, acceptDownloads: true });
const errors = [];
page.on("pageerror", (e) => errors.push(`${e.message}\n${e.stack}`));
page.on("console", (m) => m.type() === "error" && errors.push(m.text()));
await page.goto(url);
await page.evaluate(() => localStorage.setItem("kaint.settings", JSON.stringify({ language: "es", colorScheme: 1 })));
await page.reload();
await page.waitForFunction(() => window.kaint?.doc);

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
await page.fill(".dialog input[type=number] >> nth=0", "900");
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
await page.waitForSelector(".dialog .about img.logo");
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

await shot("final.png");
for (const e of errors) {
    failures++;
    console.error(`ERROR: ${e}`);
}
await browser.close();
server.close();
console.log(failures ? `${failures} fallos` : "La prueba de humo de la web pasó.");
process.exitCode = failures ? 1 : 0;
