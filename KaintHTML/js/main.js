// Arranque de Kaint para la web: el idioma guardado (o el del navegador) y la ventana principal.
import { Settings } from "./Settings.js";
import { setLanguage } from "./i18n.js";

setLanguage(Settings.get("language"));
// El paquete de iconos activo, antes de crear la ventana (así sus iconos ya salen con él).
const { IconPacks } = await import("./IconPacks.js");
await IconPacks.load();
// La ventana se carga después de fijar el idioma: sus textos se traducen al crearse.
const { App } = await import("./ui/App.js");
window.kaint = new App();
