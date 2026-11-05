# Kaint.APK

La versión web de Kaint (`../KaintHTML`) como aplicación de Android: una sola actividad con un
WebView que sirve la web desde la propia aplicación (`https://appassets.androidplatform.net/www/`,
un origen seguro, así funcionan los módulos de JavaScript, los Web Workers e IndexedDB). Abrir usa el
selector de archivos de Android y guardar, el de «Guardar como» (`window.KaintAndroid.saveFile`).
