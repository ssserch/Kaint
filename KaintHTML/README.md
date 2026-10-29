<div align="center">
  <img src="assets/kainthtml-logo.png" alt="kaint.html logo" width="460">

  <h3>Kaint, ported to the web</h3>
  <p>The Paint.NET-style image editor, running entirely in your browser.</p>
</div>

---

**KaintHTML** is the web version of [Kaint](../README.md): the same look, tools, floating
windows, menus, adjustments and effects, written in plain JavaScript with no dependencies and no
build step. Everything runs locally in the browser: images never leave your computer.

## Run it

KaintHTML is a static site. Serve this folder with any web server (ES modules don't load from
`file://`) and open it in a browser:

```bash
cd KaintHTML
python3 -m http.server 8000      # then open http://localhost:8000
```

It can also be published as-is on any static hosting (GitHub Pages, Netlify, your own server).
Chromium-based browsers get the most out of it: saving straight back to the opened file and
reading the clipboard from the menus use their File System Access and Clipboard APIs. Firefox and
Safari download the saved file and paste with Ctrl+V instead.

## What's in it

- **The Kaint interface**: menus, the main toolbar and the tool options bar with Kaint's drawn
  icons, image tabs with thumbnails, the Tools, History, Layers and Colors windows (dockable to a
  corner, resizable and translucent), and the status bar with the zoom controls.
- **All 19 tools**: rectangle, ellipse and lasso select, magic wand, move selected pixels and move
  selection (move, resize, rotate), zoom (with the pixel-snapped rectangle), pan, paint bucket,
  gradient, paintbrush, eraser, pencil, color picker, clone stamp, recolor, text, line/curve and
  the 29 shapes. Selections combine like in Paint.NET (Ctrl, Alt, right click), and shapes, lines,
  text and gradients stay editable until you press Enter.
- **Layers** with the 14 Paint.NET blend modes, opacity, visibility, drag to reorder and their
  properties dialog; full **history** you can click through.
- **Adjustments and effects** with live preview, including Curves, Levels, Hue/Saturation,
  Gaussian, motion, radial and zoom blur, distortions, noise, photo effects, clouds, fractals and
  drop shadow; **Ctrl+F** repeats the last one.
- **Files**: opens PNG, JPEG, WebP, GIF, BMP, AVIF and ICO images and **OpenRaster (.ora)** with
  its layers; saves PNG, JPEG, WebP and OpenRaster. Drop images onto the window to open them.
- **Light and dark themes** (or follow the system), **English and Spanish** (Kaint's own
  translations), and Kaint's **animations**, which you can turn off in Settings.

Not available on the web: Paint.NET and native plugins, recovery copies, rulers, `.pdn` files,
GIF animation editing and scanning. Browsers keep some shortcuts for themselves (Ctrl+N, Ctrl+W,
Ctrl+T): use the menus for those.

## Development

- `node tools/sync.mjs` copies Kaint's English translations (`../translations/kaint_en.ts`) and
  version (`../version.json`) into `js/i18n/en.js` and `js/version.js`. The interface texts are
  Kaint's Spanish source strings, so the web reuses its translations.
- `node tools/check-i18n.mjs` lists texts that have no English translation yet.
- `node tests/smoke.mjs` drives the editor in a headless Chromium (needs
  `npm install playwright && npx playwright install chromium`) and checks the tools, menus,
  dialogs, layers and saving.

## License

GPL-3.0, like Kaint. The interface icons in `assets/icons/` come from SSSerch's fork of
[paint.js](https://github.com/LabyStudio/paintdotjs), used with permission;
`assets/icons/face-smile.svg` is from KDE's Breeze icons (LGPL-3.0-or-later). Kaint is not
affiliated with dotPDN LLC; Paint.NET is a trademark of dotPDN LLC.
