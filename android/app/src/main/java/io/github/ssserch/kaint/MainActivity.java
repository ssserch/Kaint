package io.github.ssserch.kaint;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.util.Base64;
import android.view.WindowInsets;
import android.webkit.JavascriptInterface;
import android.webkit.MimeTypeMap;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.Toast;
import android.window.OnBackInvokedDispatcher;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Kaint.APK: la versión web de Kaint (KaintHTML) dentro de un WebView.
 *
 * La web va en los recursos de la aplicación (assets/www) y se sirve como
 * https://appassets.androidplatform.net/www/, un origen seguro (los módulos de JavaScript, los
 * Web Workers e IndexedDB no funcionan desde file://). Abrir usa el selector de archivos de
 * Android y guardar, el de "Guardar como" (a través de window.KaintAndroid.saveFile).
 */
public class MainActivity extends Activity {
    private static final String HOST = "appassets.androidplatform.net";
    private static final String START = "https://" + HOST + "/www/index.html";
    private static final int PICK_FILES = 1;
    private static final int SAVE_FILE = 2;

    private static final Map<String, String> TYPES = new HashMap<>();

    static {
        TYPES.put("html", "text/html");
        TYPES.put("js", "text/javascript");
        TYPES.put("mjs", "text/javascript");
        TYPES.put("css", "text/css");
        TYPES.put("json", "application/json");
        TYPES.put("png", "image/png");
        TYPES.put("jpg", "image/jpeg");
        TYPES.put("svg", "image/svg+xml");
        TYPES.put("ico", "image/x-icon");
        TYPES.put("wasm", "application/wasm");
        TYPES.put("woff", "font/woff");
        TYPES.put("woff2", "font/woff2");
        TYPES.put("ttf", "font/ttf");
        TYPES.put("otf", "font/otf");
        TYPES.put("xaml", "application/xml");
        TYPES.put("xml", "application/xml");
        TYPES.put("txt", "text/plain");
        TYPES.put("md", "text/plain");
    }

    private WebView web;
    private ValueCallback<Uri[]> pendingPick;
    private byte[] pendingSave;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        web = new WebView(this);
        root.addView(web, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        // Android 15 dibuja la aplicación de borde a borde: la web queda dentro de las barras del
        // sistema, del recorte de la cámara y del teclado.
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout()
                        | WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            } else {
                legacyPadding(view, insets);
            }
            return insets;
        });

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);
        s.setMediaPlaybackRequiresUserGesture(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setTextZoom(100);

        web.addJavascriptInterface(new Bridge(), "KaintAndroid");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                Uri url = request.getUrl();
                if (!HOST.equals(url.getHost()))
                    return null;
                return asset(url.getPath());
            }

            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                // Los enlaces de fuera (web de Kaint, ayuda...) se abren en el navegador.
                Uri url = request.getUrl();
                if (HOST.equals(url.getHost()))
                    return false;
                try {
                    startActivity(new Intent(Intent.ACTION_VIEW, url));
                } catch (ActivityNotFoundException e) {
                    Toast.makeText(MainActivity.this, url.toString(), Toast.LENGTH_SHORT).show();
                }
                return true;
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (pendingPick != null)
                    pendingPick.onReceiveValue(null);
                pendingPick = callback;
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, params.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE);
                try {
                    startActivityForResult(intent, PICK_FILES);
                } catch (ActivityNotFoundException e) {
                    pendingPick = null;
                    return false;
                }
                return true;
            }
        });
        // Atrás (Android 13 y después; desde Android 16 ya no llega a onBackPressed).
        if (Build.VERSION.SDK_INT >= 33)
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::back);
        if (state != null)
            web.restoreState(state);
        else
            web.loadUrl(START);
    }

    // Android 10 y anteriores: los márgenes de las barras del sistema con la API antigua.
    @SuppressWarnings("deprecation")
    private static void legacyPadding(android.view.View view, WindowInsets insets) {
        view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
    }

    // Un archivo de assets/www con su tipo (los módulos de JavaScript lo necesitan).
    private WebResourceResponse asset(String path) {
        if (path == null || path.contains(".."))
            return notFound();
        String name = path.startsWith("/") ? path.substring(1) : path;
        if (name.isEmpty() || name.equals("www") || name.equals("www/"))
            name = "www/index.html";
        String ext = name.substring(name.lastIndexOf('.') + 1).toLowerCase(Locale.ROOT);
        String type = TYPES.get(ext);
        if (type == null)
            type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
        if (type == null)
            type = "application/octet-stream";
        try {
            InputStream in = getAssets().open(name);
            boolean text = type.startsWith("text/") || type.equals("application/json") || type.endsWith("+xml")
                    || type.equals("application/xml");
            WebResourceResponse r = new WebResourceResponse(type, text ? "utf-8" : null, in);
            Map<String, String> headers = new HashMap<>();
            headers.put("Cache-Control", "no-cache");
            headers.put("Access-Control-Allow-Origin", "https://" + HOST);
            r.setResponseHeaders(headers);
            return r;
        } catch (IOException e) {
            return notFound();
        }
    }

    private static WebResourceResponse notFound() {
        WebResourceResponse r = new WebResourceResponse("text/plain", "utf-8", null);
        r.setStatusCodeAndReasonPhrase(404, "Not Found");
        return r;
    }

    // Lo que la web llama como window.KaintAndroid.
    private class Bridge {
        // Guardar: los bytes del archivo (en base64) y Android pregunta dónde.
        @JavascriptInterface
        public void saveFile(String name, String mime, String base64) {
            final byte[] data;
            try {
                data = Base64.decode(base64, Base64.DEFAULT);
            } catch (IllegalArgumentException e) {
                return;
            }
            runOnUiThread(() -> {
                pendingSave = data;
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType(mime == null || mime.isEmpty() ? "application/octet-stream" : mime);
                intent.putExtra(Intent.EXTRA_TITLE, name);
                try {
                    startActivityForResult(intent, SAVE_FILE);
                } catch (ActivityNotFoundException e) {
                    pendingSave = null;
                    Toast.makeText(MainActivity.this, "No se puede guardar: no hay un selector de archivos.",
                            Toast.LENGTH_LONG).show();
                }
            });
        }

        @JavascriptInterface
        public String platform() {
            return "android";
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == PICK_FILES && pendingPick != null) {
            Uri[] uris = null;
            if (result == RESULT_OK && data != null) {
                if (data.getClipData() != null) {
                    uris = new Uri[data.getClipData().getItemCount()];
                    for (int i = 0; i < uris.length; ++i)
                        uris[i] = data.getClipData().getItemAt(i).getUri();
                } else if (data.getData() != null) {
                    uris = new Uri[] {data.getData()};
                }
            }
            pendingPick.onReceiveValue(uris);
            pendingPick = null;
        } else if (request == SAVE_FILE) {
            byte[] bytes = pendingSave;
            pendingSave = null;
            if (result != RESULT_OK || data == null || data.getData() == null || bytes == null)
                return;
            try (OutputStream out = getContentResolver().openOutputStream(data.getData(), "wt")) {
                if (out == null)
                    throw new IOException("sin destino");
                out.write(bytes);
                Toast.makeText(this, "Guardado", Toast.LENGTH_SHORT).show();
            } catch (IOException e) {
                Toast.makeText(this, "No se pudo guardar: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        // Copia de recuperación de las imágenes sin guardar por si Android cierra la aplicación.
        web.evaluateJavascript("window.kaint && window.kaint.autosave && window.kaint.autosave()", null);
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        web.saveState(state);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        back();
    }

    // Atrás: con imágenes sin guardar, pregunta antes de salir.
    private void back() {
        web.evaluateJavascript("!!(window.kaint && window.kaint.docs && window.kaint.docs.some(function (d) { return d.isModified(); }))",
                value -> {
                    if (!"true".equals(value)) {
                        finish();
                        return;
                    }
                    boolean spanish = Locale.getDefault().getLanguage().equals("es");
                    new AlertDialog.Builder(this)
                            .setTitle("Kaint")
                            .setMessage(spanish ? "Hay imágenes sin guardar. ¿Salir de todos modos?"
                                                : "There are unsaved images. Quit anyway?")
                            .setPositiveButton(spanish ? "Salir" : "Quit", (d, w) -> finish())
                            .setNegativeButton(spanish ? "Cancelar" : "Cancel", null)
                            .show();
                });
    }

    @Override
    protected void onDestroy() {
        if (web != null) {
            web.removeJavascriptInterface("KaintAndroid");
            web.destroy();
        }
        super.onDestroy();
    }
}
