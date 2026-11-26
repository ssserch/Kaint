// Arranque de Kaint para la web: el idioma guardado (o el del navegador) y la ventana principal.
import { Settings } from "./Settings.js";
import { setLanguage } from "./i18n.js";

await setLanguage(Settings.get("language"));
// El paquete de iconos activo, antes de crear la ventana (así sus iconos ya salen con él).
const { IconPacks } = await import("./IconPacks.js");
await IconPacks.load();
// La ventana se carga después de fijar el idioma: sus textos se traducen al crearse.
const { App } = await import("./ui/App.js");
window.kaint = new App();
// Fuentes añadidas y complementos (sus efectos y formatos), guardados en el navegador.
const { UserFonts } = await import("./UserFonts.js");
await UserFonts.load();
const { Plugins } = await import("./plugins/Plugins.js");
Plugins.installApi(window.kaint);
await Plugins.load();
// Los archivos de la última sesión, si se quiere.
await window.kaint.restoreSession();
// La primera vez, la bienvenida con lo básico de la configuración; si no, las copias de
// recuperación si Kaint se cerró de golpe.
if (!Settings.get("welcomed")) {
    const { welcomeDialog } = await import("./ui/Dialogs.js");
    welcomeDialog();
} else {
    await window.kaint.offerRecovery();
    // Kaint.APK: la pantalla de bienvenida (como la de ibis Paint), si no se quitó.
    if (window.KaintAndroid && Settings.get("androidWelcome") !== false) {
        const { showWelcome } = await import("./ui/Welcome.js");
        await showWelcome(window.kaint);
    }
}
// Kaint.APK: las imágenes recientes de la bienvenida se guardan al salir de la app.
if (window.KaintAndroid) {
    const { installRecents } = await import("./ui/Welcome.js");
    installRecents(window.kaint);
}
