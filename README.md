<div align="center">
  <img src="img/other/kaintqt6logo.png" alt="kaint.qt6 logo" width="460">

  <h3>The Paint.NET experience on Linux, Windows, macOS, FreeBSD, Android and the web</h3>
  <p>A free raster image editor built with C++20 and Qt 6, with a web version in plain JavaScript.</p>

  [![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus)](CMakeLists.txt)
  [![Qt 6](https://img.shields.io/badge/Qt-6-41CD52.svg?logo=qt&logoColor=white)](https://www.qt.io/)
  [![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
  [![Paint.NET plugins](https://img.shields.io/badge/Paint.NET-plugins-8a2be2.svg)](#features)

  [![Linux](https://img.shields.io/badge/Linux-Flatpak%20%7C%20AppImage%20%7C%20deb%20%7C%20Arch-FCC624.svg?logo=linux&logoColor=black)](#-install)
  [![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6.svg?logo=windows&logoColor=white)](#-install)
  [![macOS](https://img.shields.io/badge/macOS-build.sh-999999.svg?logo=apple&logoColor=white)](#-install)
  [![FreeBSD](https://img.shields.io/badge/FreeBSD-build.sh-AB2B28.svg?logo=freebsd&logoColor=white)](#-install)
  [![Android](https://img.shields.io/badge/Android-Kaint.APK-3DDC84.svg?logo=android&logoColor=white)](#-install)
  [![Web](https://img.shields.io/badge/Web-Kaint.HTML-E34F26.svg?logo=html5&logoColor=white)](https://ssserch.github.io/kainthtml/)
  [![x86-64 | ARM64](https://img.shields.io/badge/CPU-x86--64%20%7C%20ARM64-555555.svg)](#-install)

  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/E1E5ZOEH6)

</div>

<div align="center">
  <img src="img/other/screenshot.png" alt="Kaint in the dark theme with the tools, colors, history and layers windows" width="900">
  <a href="https://ssserch.github.io/kainthtml/"><img src="img/other/tryitonline.png" alt="kainthtml" width="250"></a>
</div>

---

# Kaint

**Kaint** recreates the look, the tools and the workflow of [Paint.NET](https://www.getpaint.net/)
as a native application. It is made for KDE Plasma on Wayland, and the same code builds for other
Linux desktops, Windows 10 and 11, macOS and FreeBSD, on x86-64 and ARM64. Its web version runs in any
browser and, as Kaint.APK, on Android phones and tablets.

<table align="center">
  <tr>
    <td align="center" width="110"><a href="#the-windows"><img src="data/ui-icons/menu_window_colors_icon.png" alt="" width="32"><br><sub><b>The windows</b></sub></a></td>
    <td align="center" width="110"><a href="#make-it-yours"><img src="data/ui-icons/menu_utilities_settings_icon.png" alt="" width="32"><br><sub><b>Make it yours</b></sub></a></td>
    <td align="center" width="110"><a href="#features"><img src="img/kaint-32.png" alt="" width="32"><br><sub><b>Features</b></sub></a></td>
    <td align="center" width="110"><a href="#gif-editor"><img src="data/ui-icons/menu_window_layers_icon.png" alt="" width="32"><br><sub><b>GIF Editor</b></sub></a></td>
    <td align="center" width="110"><a href="#web-version"><img src="data/ui-icons/menu_help_pdn_website_icon.png" alt="" width="32"><br><sub><b>Web version</b></sub></a></td>
    <td align="center" width="110"><a href="#opening-and-saving"><img src="data/ui-icons/menu_file_open_icon.png" alt="" width="32"><br><sub><b>Opening and saving</b></sub></a></td>
    <td align="center" width="110"><a href="#-install"><img src="data/ui-icons/menu_file_save_icon.png" alt="" width="32"><br><sub><b>Install</b></sub></a></td>
    <td align="center" width="110"><a href="#license"><img src="data/ui-icons/menu_help_about_icon.png" alt="" width="32"><br><sub><b>License</b></sub></a></td>
  </tr>
</table>

## The windows

Like Paint.NET, the image takes the whole window and everything else floats over it in small
windows you can move, snap to the edges, make translucent or hide (F5 to F8, or the buttons in the
top right corner).

<table>
  <tr>
    <td align="center" valign="top" width="25%"><img src="img/windows/tools.png" alt="Tools window" height="300"></td>
    <td align="center" valign="top" width="25%"><img src="img/windows/history.png" alt="History window" width="200"></td>
    <td align="center" valign="top" width="25%"><img src="img/windows/layers.png" alt="Layers window" width="210"></td>
    <td align="center" valign="top" width="25%"><img src="img/windows/colors.png" alt="Colors window" width="210"></td>
  </tr>
  <tr>
    <td valign="top"><b>Tools</b><br><sub>Every tool, in the order you choose (Settings → Customization). Each one has its letter on
      the keyboard.</sub></td>
    <td valign="top"><b>History</b><br><sub>Every step, to click back and forth. Gradients, lines, shapes and text stay editable
      when you go back to them.</sub></td>
    <td valign="top"><b>Layers</b><br><sub>Thumbnails, visibility and drag and drop. Right-click a layer for everything,
      Alpha Lock included.</sub></td>
    <td valign="top"><b>Colors</b><br><sub>Primary and secondary colors, the wheel and the palette. <b>More »</b> opens
      the full editor.</sub></td>
  </tr>
</table>

<table>
  <tr>
    <td width="50%" valign="top">
      <h4>The top right corner</h4>
      <p align="center"><img src="img/windows/corner-paintnet.png" alt="The window buttons with the Paint.NET style" width="204"></p>
      One button for each window (Tools, History, Layers and Colors: lit while it's open), then
      <b>Settings</b> and <b>Help</b>, with its documentation, the website, the forum and more. With the
      Paint.NET style they look like Paint.NET's; right-click them to put the windows back in their
      corners or make them translucent.
    </td>
    <td width="50%" valign="top">
      <h4>The image strip</h4>
      Every open image, as a thumbnail, along the top: the orange star marks the unsaved ones and the
      arrow at the end lists them all. Drag a thumbnail to reorder it. <b>Ctrl+click</b> another image
      to select everything between them, and press the <b>+</b> that appears to make a <b>group</b>:
      give it a name and a border color, and click its name to collapse it to its first image and
      save space. The wheel scrolls the strip when there are many.
    </td>
  </tr>
  <tr>
    <td colspan="2" valign="top">
      <h4>The GIF editor</h4>
      <b>File → GIF Editor</b> turns the image into an animation on a timeline, on a black screen like a
      video editor: frames, tracks, keyframes and, for exported videos, audio tracks with volume
      keyframes. <a href="#gif-editor">More below</a>.
    </td>
  </tr>
</table>

## Make it yours

<div align="center">
  <img src="img/themes/windows8.png" alt="Windows 8 theme" width="32%">
  <img src="img/themes/windows7.png" alt="Windows 7 theme" width="32%">
  <img src="img/themes/gnome-dark.png" alt="GNOME dark theme" width="32%">
</div>

Kaint looks the way you want. Almost everything about its appearance can be changed from
**Settings → User interface**, and every change shows at once:

- **Themes**: light and dark (or follow the system), Paint.NET's Blue, **Windows 8** and **Windows 8
  Dark** (the wide flat frame in the window color, with the colors of Windows 8, sky blue by default),
  **Windows 7** and **Windows 7 Dark** (with 7.css's colors and glass buttons), **Windows 7 Basic**
  (light and dark, without glass), **Windows XP** (Luna, with XP.css's), the classic gray
  of **Windows 98** and **Windows 98 ALT** (the classic look of Windows 2000 and XP), the Linux
  desktops **Ubuntu** (Yaru), **GNOME** (Adwaita), **Linux Mint** (Mint-Y) and **elementary OS**, each
  light and dark, the real **KDE** theme on Linux, and a **custom theme** editor with every color,
  the toolbar layout and a canvas background (placement, scale, blur and tint), which you can
  export and share. The custom theme can start from any of them, Windows 7 (light or dark) and XP
  included with their style of buttons, menus and windows, and its background can be an image, an
  **animated GIF** (or WebP) or a **video** (MP4, WebM, MOV...), played in a loop without sound.
- **Interface style**: Kaint's own or a recreation of **Paint.NET 5** (the default): more compact,
  with borderless windows, tighter menus and buttons, gray and blue bar sliders, hollow ring handles,
  a grid of shapes and Paint.NET's Colors window.
- **Title bar**: Kaint can draw a title bar and window frame of its own (**Settings → User
  interface → Title bar**; on by default on Linux, off on Windows), with the shadow from KWin or
  from Windows; drag it from anywhere. Its buttons can be **Automatic** (the theme's: Windows 8, 7, XP
  or 98 with those themes, round GNOME-style ones with the Linux themes and Windows 10's with the
  rest) or look like those of **Windows 11, 10, 8, 7, XP or 98, macOS or GNOME** with any theme,
  on the right or on the left. Turned off, windows get the system's title bar
  (Windows', or your desktop's: Plasma's on KDE). With the Paint.NET style, the
  **Windows recreation** picks Windows 10 or Windows 11 (slightly rounded windows when not
  maximized). The top bar and the window headers can take **a color of your own**, like Windows'
  accent color, and the Light and Dark themes can use **your own selection color** instead of the
  blue (one for each, so "Follow the system" uses the right one).
- **The program itself**: its **icon** and the **name** in the title bar and the taskbar ("image.png -
  Kaint") can be your own (**Settings → User interface → Program**), and go back to Kaint's with one click.
- **Cursors**: Kaint's tool icons or Paint.NET's pixel-art cursors (the default), with the system
  pointer or Kaint's own beside the small tool icon.
- **Icons and fonts**: install **icon packs** (.zip files with a preview) or create your own with
  the **Editor**, pick the **interface
  font** and its size, and add fonts for the Text tool without installing them system-wide.
- **Floating windows**: a Windows 10 inspired frame or Kaint's compact one, the border width,
  translucency when the pointer leaves them (only while they cover the image, like Paint.NET, or
  always), and **snapping** to the edges and to each other.
- **Menus and tools**: move any effect, plugin ones too, to Adjustments, Effects or another
  submenu (say, a plugin's blur into **Effects → Blurs**), hide the ones you never use, and choose
  which tools the Tools window shows and in what order (**Settings → Customization**).
- **Canvas**: your own background color around the image (one for light themes and one for dark
  ones), the checkerboard brightness, the drop shadow and dark, toolbar-colored or **Paint.NET 5
  style rulers** (the divisions of a real ruler, with a unit button in the corner: pixels, inches or
  centimeters).
- **Image strip**: like Paint.NET, the box of the current image and of the one under the pointer can
  reach the top of the window, title bar included (or only the height of the thumbnail). Images can
  be grouped, with a name and a border color, and a group collapses to its first image.
- **Little touches**: tool icons in the active colors, Paint.NET's orange star on unsaved images
  (on their thumbnail in the strip and in the open images list),
  animations everywhere (fades, sliding layers, the active color slot) that you can turn off, and a
  welcome window on the first start to pick the theme, style, cursors and language.

#### Kaint style

Kaint's own interface: floating windows with a border and slightly rounded corners, and a Colors
window with a primary/secondary selector and a brightness slider.

<table>
  <tr>
    <td width="50%"><img src="img/other/screenshot-light.png" alt="Kaint style, light theme"></td>
    <td width="50%"><img src="img/other/screenshot.png" alt="Kaint style, dark theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Light</sub></td>
    <td align="center"><sub>Dark</sub></td>
  </tr>
</table>

#### Paint.NET 5 style

A recreation of Paint.NET 5: more compact, with borderless windows, tighter menus and buttons and
Paint.NET's Colors window, gray and blue bar sliders and hollow ring handles. On Linux the window
and its dialogs draw a Windows 10 or Windows 11 title bar of their own (black, with a shadow; its
buttons can take another look or be turned off in **Settings**). Pick it in **Settings → User interface → Interface style** (or in the
welcome window).

The floating windows snap to the edges of the workspace and to each other (hold Shift to place
one freely, or turn it off in **Settings**), the interface font can be changed, and on Linux the
top bar and the window headers can take a color of your own, like Windows' accent color on title
bars. Effect dialogs have a **Preview** box to compare with the original image.

<table>
  <tr>
    <td width="50%"><img src="img/other/netscreenshot-light.png" alt="Paint.NET 5 style, light theme"></td>
    <td width="50%"><img src="img/other/netscreenshot.png" alt="Paint.NET 5 style, dark theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Light</sub></td>
    <td align="center"><sub>Dark</sub></td>
  </tr>
</table>

<details>
<summary><b>🖼️ Every theme</b> (screenshots, with the title bar buttons of each theme)</summary>

<br>

With **Automatic** title bar buttons (the default), each theme brings its own: the flat ones with
Windows 8, the blue Luna ones with Windows XP, the glass ones with Windows 7, the raised ones with
Windows 98 and round ones with the Linux themes. Any other style can be
chosen in **Settings → User interface → Title bar** with any theme. The **KDE** theme follows the
colors of your Plasma desktop. All screenshots are 1920 × 1080, made with
`./screenpreview.sh --all --win7-colors`.

<table>
  <tr>
    <td width="33%"><img src="img/themes/light.png" alt="Light theme"></td>
    <td width="33%"><img src="img/themes/dark.png" alt="Dark theme"></td>
    <td width="33%"><img src="img/themes/blue.png" alt="Blue theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Light</sub></td>
    <td align="center"><sub>Dark</sub></td>
    <td align="center"><sub>Blue (Paint.NET)</sub></td>
  </tr>
  <tr>
    <td><img src="img/themes/windows7.png" alt="Windows 7 theme"></td>
    <td><img src="img/themes/windows7-dark.png" alt="Windows 7 Dark theme"></td>
    <td><img src="img/themes/windowsxp.png" alt="Windows XP theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Windows 7</sub></td>
    <td align="center"><sub>Windows 7 Dark</sub></td>
    <td align="center"><sub>Windows XP</sub></td>
  </tr>
  <tr>
    <td><img src="img/themes/windows7-basic.png" alt="Windows 7 Basic theme"></td>
    <td><img src="img/themes/windows7-basic-dark.png" alt="Windows 7 Basic Dark theme"></td>
    <td><img src="img/themes/windows8.png" alt="Windows 8 theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Windows 7 Basic</sub></td>
    <td align="center"><sub>Windows 7 Basic Dark</sub></td>
    <td align="center"><sub>Windows 8</sub></td>
  </tr>
  <tr>
    <td><img src="img/themes/windows8-dark.png" alt="Windows 8 Dark theme"></td>
    <td><img src="img/themes/windows98.png" alt="Windows 98 theme"></td>
    <td><img src="img/themes/windows98-alt.png" alt="Windows 98 ALT theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Windows 8 Dark</sub></td>
    <td align="center"><sub>Windows 98</sub></td>
    <td align="center"><sub>Windows 98 ALT</sub></td>
  </tr>
</table>

**Linux desktops**

<table>
  <tr>
    <td width="25%"><img src="img/themes/ubuntu.png" alt="Ubuntu theme"></td>
    <td width="25%"><img src="img/themes/gnome.png" alt="GNOME theme"></td>
    <td width="25%"><img src="img/themes/mint.png" alt="Linux Mint theme"></td>
    <td width="25%"><img src="img/themes/elementary.png" alt="elementary OS theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Ubuntu</sub></td>
    <td align="center"><sub>GNOME</sub></td>
    <td align="center"><sub>Linux Mint</sub></td>
    <td align="center"><sub>elementary OS</sub></td>
  </tr>
  <tr>
    <td><img src="img/themes/ubuntu-dark.png" alt="Ubuntu Dark theme"></td>
    <td><img src="img/themes/gnome-dark.png" alt="GNOME Dark theme"></td>
    <td><img src="img/themes/mint-dark.png" alt="Linux Mint Dark theme"></td>
    <td><img src="img/themes/elementary-dark.png" alt="elementary OS Dark theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Ubuntu Dark</sub></td>
    <td align="center"><sub>GNOME Dark</sub></td>
    <td align="center"><sub>Linux Mint Dark</sub></td>
    <td align="center"><sub>elementary OS Dark</sub></td>
  </tr>
</table>

<details>
<summary><b>Windows 7 window colors</b></summary>

<br>

Like **Personalize → Window Color** in Windows 7: the Windows 7 and Windows 7 Dark themes take the
color you choose for the glass of the windows (**Settings → User interface**).

**Windows 7**

<table>
  <tr>
    <td width="25%"><img src="img/themes/windows7-sky.png" alt="Sky"></td>
    <td width="25%"><img src="img/themes/windows7-twilight.png" alt="Twilight"></td>
    <td width="25%"><img src="img/themes/windows7-sea.png" alt="Sea"></td>
    <td width="25%"><img src="img/themes/windows7-leaf.png" alt="Leaf"></td>
  </tr>
  <tr>
    <td align="center"><sub>Sky</sub></td>
    <td align="center"><sub>Twilight</sub></td>
    <td align="center"><sub>Sea</sub></td>
    <td align="center"><sub>Leaf</sub></td>
  </tr>
  <tr>
    <td width="25%"><img src="img/themes/windows7-sun.png" alt="Sun"></td>
    <td width="25%"><img src="img/themes/windows7-pumpkin.png" alt="Pumpkin"></td>
    <td width="25%"><img src="img/themes/windows7-ruby.png" alt="Ruby"></td>
    <td width="25%"><img src="img/themes/windows7-violet.png" alt="Violet"></td>
  </tr>
  <tr>
    <td align="center"><sub>Sun</sub></td>
    <td align="center"><sub>Pumpkin</sub></td>
    <td align="center"><sub>Ruby</sub></td>
    <td align="center"><sub>Violet</sub></td>
  </tr>
</table>

**Windows 7 Dark**

<table>
  <tr>
    <td width="25%"><img src="img/themes/windows7-dark-sky.png" alt="Sky"></td>
    <td width="25%"><img src="img/themes/windows7-dark-twilight.png" alt="Twilight"></td>
    <td width="25%"><img src="img/themes/windows7-dark-sea.png" alt="Sea"></td>
    <td width="25%"><img src="img/themes/windows7-dark-leaf.png" alt="Leaf"></td>
  </tr>
  <tr>
    <td align="center"><sub>Sky</sub></td>
    <td align="center"><sub>Twilight</sub></td>
    <td align="center"><sub>Sea</sub></td>
    <td align="center"><sub>Leaf</sub></td>
  </tr>
  <tr>
    <td width="25%"><img src="img/themes/windows7-dark-sun.png" alt="Sun"></td>
    <td width="25%"><img src="img/themes/windows7-dark-pumpkin.png" alt="Pumpkin"></td>
    <td width="25%"><img src="img/themes/windows7-dark-ruby.png" alt="Ruby"></td>
    <td width="25%"><img src="img/themes/windows7-dark-violet.png" alt="Violet"></td>
  </tr>
  <tr>
    <td align="center"><sub>Sun</sub></td>
    <td align="center"><sub>Pumpkin</sub></td>
    <td align="center"><sub>Ruby</sub></td>
    <td align="center"><sub>Violet</sub></td>
  </tr>
</table>

</details>

</details>

## Features

<table>
  <tr>
    <td width="50%" valign="top">
      <h4>Paint.NET tools</h4>
      Selections with their combine modes, move pixels or the selection (scale, rotate, movable
      pivot), brush, pencil, eraser, clone stamp, recolor, paint bucket, gradients, lines and
      curves, shapes and text, all with the same options bar. Pasted pixels replace what is
      underneath, transparency included, like in Paint.NET (optional), and copied images keep
      their transparency in other apps such as Discord or a web browser.
    </td>
    <td width="50%" valign="top">
      <h4>Editable history</h4>
      Click through the history like in Paint.NET. Gradients, lines, shapes, text and paint bucket
      fills stay editable when you go back to one of their steps.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Layers</h4>
      Blend modes, opacity, visibility and properties, with live thumbnails, <b>Alpha Lock</b> (paint
      only where there already is something; in the right-click menu of each layer, or as a button if you
      turn it on in Settings) and layers that glide into place when you drag them.
    </td>
    <td valign="top">
      <h4>Adjustments and effects</h4>
      Live preview while you tweak them, and <b>Repeat</b> (Ctrl+F) for the last one.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Plugins</h4>
      Paint.NET effect plugins (.dll), through the .NET Framework built into Windows and, on Linux,
      macOS and FreeBSD, with the plugin system you choose (in <b>Settings → Plugins</b> or the welcome
      window): <b>.NET</b> (the default: modern .NET, for GPU,
      <code>BitmapEffect</code> and Paint.NET 5 plugins, their own windows shown inside Kaint's effect
      dialog, and FileType plugins that add formats to Open and Save As), <b>Wine</b> 🍷 (the plugins
      run inside Wine like a Windows program) or <b>OpenD2D</b>
      (mono with <a href="https://github.com/daijro/opend2d">OpenD2D</a>, the open source Direct2D,
      DirectWrite and WIC, instead of Windows' own). With OpenD2D, whatever doesn't work with mono
      (a plugin that doesn't load, or one of its effects that fails) can run with Wine, only that part,
      and Kaint remembers it. Old Paint.NET 3 plugins are included: the ones that use its standard Amount, Two/Three
      Amounts and Angle windows (early CodeLab plugins) open in Kaint's own effect window, with a live
      preview. Plus native plugins with a stable C API. Also PhotoCraft filter plugins (.wasm, run
      sandboxed in Kaint's own WebAssembly interpreter) and LightCraft looks (.cube, .3dl) and
      develop presets (.xmp), under <b>Effects → Plugins</b>; drop them on the window to install
      them. <b>paint.js plugins</b> (.paintjs) too, the same ones as the web version, with their
      effects and file types (Photoshop PSD with layers, for example). <b>Settings → Plugins → Browse
      Plugins…</b> is a short list of plugins chosen and tested for Kaint, installed with one click:
      Kaint.HTML's paint.js plugins, PhotoCraft filters from the ArtCraft store and Paint.NET effect
      plugins, each credited to its author (the button can be hidden in <b>Settings → User
      interface</b>). They run in a sandboxed JavaScript engine (QuickJS-ng) with no access to
      your files or the internet; plugins that open their own HTML window only work in the web
      version, and Kaint says so. Each effect of a plugin
      can be turned on or off on its own, and native plugins can bring their own menu icon. In the
      menus, plugin effects carry the green puzzle piece on the right (it can be turned off in
      <b>Settings → Plugins</b>).
    </td>
    <td valign="top">
      <h4>Formats</h4>
      Paint.NET <b>.pdn</b>, OpenRaster (.ora) and <b>Photoshop (.psd and .psb)</b>, all with layers, PNG,
      JPEG, WebP, AVIF, JPEG XL, HEIC, JPEG XR, BMP, TIFF, TGA, DDS, QOI, Netpbm (PPM, PGM, PBM, PAM), PCX,
      SGI, Farbfeld, animated GIF, ICO and CUR (the full list is in <a href="#opening-and-saving">Opening
      and saving</a>). PSD/PSB, TGA, DDS, QOI, PCX, SGI, Farbfeld and PAM are read and written by Kaint
      itself, so they work everywhere: Photoshop files keep their pixel layers with
      names, visibility, opacity and the blend modes both programs share (8, 16 and 32 bits per
      channel, RGB, grayscale, indexed or bitmap; masks are applied, and files with clipping masks,
      layer effects or adjustment layers open as their merged image, with a note saying why). AVIF,
      JPEG XL, HEIC and JPEG XR come from Qt's image plugins (kimageformats) on Linux and from the
      Windows codecs on Windows (JPEG XR is built in; HEIC, AVIF and JPEG XL need their Microsoft Store
      extensions). Animated GIFs open as layers, as separate images, as a single image or with just the frames you pick, and
      can be saved from the layers or from the open images you pick, with a playable preview and
      advanced settings: per-frame duration, show/hide and copy, hidden layers as
      frames, global palette, crossfades and a background frame. <b>File → GIF Editor</b> turns
      any image into a simple video-editor-style timeline: a media library with the open images
      and the ones you import or drag in, a player, and one clip per frame to reorder, stretch,
      duplicate, hide or edit with the tools, then export with the same GIF settings.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Icon packs, fonts and shortcuts</h4>
      Install icon packs (.zip files with an <code>asset-pack.json</code>), each with a preview, or
      make your own with the icon pack <b>Editor</b> (every icon side by side with Kaint's; save it
      or export it as a .zip), add fonts for the Text tool and Paint.NET shapes (.xaml) without installing them system-wide,
      and change any keyboard shortcut in <b>Settings</b>.
    </td>
    <td valign="top">
      <h4>Cursors, rulers and status bar</h4>
      Painting tools use their icon as the cursor, sized like the system cursor and with the hot
      spot on its tip; the selection tools, gradient, line, text and shapes keep the system pointer
      (from your cursor theme, at its size and animated if it is) with a small icon beside it (the
      chosen shape, for Shapes), like Paint.NET. To rotate, the move tools and Shapes show the hand in front of a curved double arrow that turns with it. Dark rulers (or the
      toolbar color) that the floating windows never cover, and the tool's icon in the status bar.
      Open and Save As use the system file dialog, the same one as Firefox (or Qt's, in
      <b>Settings</b>).
    </td>
  </tr>
  <tr>
    <td colspan="2" valign="top">
      <h4>PhotoCraft features (optional)</h4>
      Turn on <b>Settings → Plugins → Enable the PhotoCraft features</b> for more adjustments and
      effects (Vibrance, Gradient Map, Channel Mixer, Photo Filter, Selective Color, Auto Contrast,
      Color to Alpha, Polar Coordinates, the artistic, brush stroke, sketch and texture filter gallery
      and more) and six retouching tools: <b>Magnetic Lasso</b> (the selection border snaps to the
      edges as you trace), <b>Object Selection</b> (draw a box around an object, click the one the
      Object Finder highlights, or <b>Select Subject</b>), <b>Remove</b> (paint over or around
      something and it is filled from its surroundings), <b>Patch</b> (drag a selection to repair it
      with another area, normal or content-aware), <b>Healing Brush</b> and <b>Spot Healing Brush</b>
      (content-aware, create texture or proximity match). The long calculations run in the background
      and Esc cancels them. They work the same in the web version, and in the menus and the Tools
      window they carry the plugins' puzzle piece. Their algorithms are
      PhotoCraft's, by the ArtCraft Team and the PhotoCraft
      contributors (MIT, see <code>data/licenses/PhotoCraft-LICENSE.txt</code>).
    </td>
  </tr>
  <tr>
    <td colspan="2" valign="top">
      <h4>Collaborative mode 💻🤝🖥️</h4>
      <p align="center"><img src="img/other/collaboration.png" alt="Two guests drawing together with their pointers, names and chat bubbles" width="820"></p>
      Host the current image and edit it together with other copies of Kaint or with browsers
      running the web version (<b>File → Collaboration</b>): over the Internet with P2P (the
      default: share the session code and the image goes straight from one computer to the other,
      with WebRTC) or on your local network. Between copies
      of Kaint and the web version changes appear in real time (in any combination); guests can also
      edit a private draft with its own history and suggest it, and the host accepts it (or accepts
      everything automatically): the change then shows up for everyone and in the host's history
      as "Change made by…". Layer properties are shared too, everyone's pointer is shown as a
      colored triangle with their name and tool, and <b>you see what the others draw as they draw
      it</b> (brushes, pencil, eraser, lines and shapes show a live sketch under their pointer until
      the change arrives). A <b>session chat</b> (<b>File → Collaboration → Session chat</b>, or the
      button in the status bar) shows each message for a moment in a bubble next to its writer's
      pointer; the live sketches, the chat and the bubbles can each be turned off. Only the host saves
      the image (guests export a copy). The session image's tab turns yellow and shows whether the host's image is saved (the
      crown) and whether a guest's exported copy is up to date (the computer); from the History
      window everyone can see the host's history, and the host (and the guests it allows) can go
      back to an earlier version for everyone. Sessions can be open, approved one by one, password protected or limited to the
      computers whose ID the host added; on the local network nearby sessions are found
      automatically and you can also join by IP (addresses and codes stay blurred until you click
      them). Everything is in its own <b>Settings → Collaboration</b> section, with a simplified and
      a compact view.
    </td>
  </tr>
  <tr>
    <td colspan="2" valign="top">
      <h4>Discord</h4>
      Kaint can show what you're working on in your Discord profile (Rich Presence: the image, the
      layer, the tool...). In a collaborative session it shows the session and how many people are in
      it, and you can <b>invite people from Discord</b> to your P2P session (or let them use <b>Ask to
      Join</b>): accepting on Discord doesn't let anyone straight in, since Kaint still asks for the
      session's password or waits for your approval. Everything is in <b>Settings → Discord</b>.
    </td>
  </tr>
  <tr>
    <td colspan="2" valign="top">
      <h4>Painting like on a tablet app</h4>
      Turn on <b>Settings → Tools → Advanced brush options</b> for an <b>Advanced…</b> button on the
      paintbrush, eraser and clone stamp: round or square tip with angle and roundness, flow, pressure
      curve and minimum size, pressure that changes the opacity, <b>pressure from speed</b> for a mouse or
      a finger, a <b>stabilizer</b>, <b>tapered starts and ends</b> for manga lines,
      random jitter, and vertical size and opacity sliders beside the canvas.
      <br><br><div align="center"><img src="img/other/brush-symmetry.png" alt="Radial symmetry drawing" width="420"></div><br><br>
      <b>Symmetry</b> mirrors what you paint with the brush, the eraser and the pencil (left and right, top
      and bottom, both, or radial with 4, 6, 8 or 12 copies), with guides over the image.
      <b>Quick shapes</b>: finish a stroke and hold still for a moment, and the line, circle, ellipse,
      rectangle, triangle or polygon you drew becomes perfect. To keep a shape of your own, draw it (with
      the lasso, or with the brush and the magic wand) and use <b>Edit → Save Selection as Shape</b>: it
      appears in the Shapes tool. The view rotates with <b>Alt + wheel</b> (Shift: one degree at a time) or
      Ctrl+Alt+arrows, like turning the paper.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Recovery copies</h4>
      Unsaved images are kept safe in case Kaint closes unexpectedly.
    </td>
    <td valign="top">
      <h4>Languages</h4>
      English, Spanish and <b>Brazilian Portuguese</b> (translated by Mildanner), and your own: import,
      create, edit and export <b>custom languages</b>
      (.kaintlang) in <b>Settings → User interface</b>, with the translator's name and page in
      <b>About</b>. Anything a custom language doesn't translate appears in English.
    </td>
  </tr>
</table>

## GIF Editor

<div align="center">
  <img src="img/other/gifeditor-sample.gif" alt="A small animation made in the GIF editor" width="240">
</div>

**File → GIF Editor** turns the current image into an animation on a timeline, with a black screen
like a simple video editor (the first time, a note explains it is made for GIFs); choosing it again goes back to the image editor (each image remembers which
one it is in). Every frame is one of the image's layers, so the History, the tools and **Export
GIF...** (with the usual palette, loops and effects) all work as always. The arrow next to it exports
an **animated PNG**, an **animated WebP** or an **MP4 or WebM video** (with FFmpeg; repeat the
animation, enlarge pixel art without blur, pick the quality), and a small bar beside it shows how
heavy the GIF will be and how long it lasts, turning yellow and red as it gets close to Discord's
10 MB.

- **Media**: the images open in Kaint and the ones you import or drag in from your folders
  (still images and animated GIFs).
- **Player**: play and pause, frame by frame, loop and preview speed.
- **Timeline**: drop media or files on it, drag clips to reorder them, stretch a clip's edge to
  change its duration, duplicate, hide, delete, reverse, back and forth, the same duration for all,
  and edit any frame with the tools. Ctrl+click and Shift+click pick several frames; Ctrl+C, Ctrl+X,
  Ctrl+V and Ctrl+D copy, cut, paste and duplicate them.
- **Tracks**: each track is its own row of frames, and the upper tracks show on top of the lower
  ones.
- **Audio tracks** (for MP4 and WebM videos; a GIF has no sound): add MP3, WAV, OGG, FLAC... files
  with the **Audio** button or by dropping them on the timeline. Move and trim them, mute them, set
  their **volume** and **fade in and out**, and double-click a clip to add **volume keyframes** that you
  drag up, down and sideways. They play with the preview and are saved inside the `.ora`.
- **Keyframes**: the panel on the right sets the **scale** (proportional or not),
  **position**, **rotation** and **opacity** of the selected frame. The diamond next to each value
  adds a keyframe at the playhead, and the diamonds appear under the clip on the timeline (white, or
  in your accent color when selected): drag them apart and the motion lasts longer, right-click for
  **Linear, Curve, Continuous Curve, Freeze, Ease In or Ease Out**. The graph shows how the value
  changes and its points can be dragged too; drag the image in the preview to move it, along its
  dotted motion path. **Convert to frames** turns the motion into ordinary frames.

<div align="center">
  <img src="img/other/gifeditor-audio.png" alt="GIF editor with two audio tracks, one of them muted, and volume keyframes" width="900">
</div>

## Web version

<div align="center">
  <a href="https://ssserch.github.io/kainthtml/"><img src="KaintHTML/assets/kainthtml-logo.png" alt="kaint.html" width="420"></a>
</div>

[**KaintHTML**](KaintHTML/README.md) is Kaint ported to the web: the same interface, tools,
windows, adjustments and effects in plain JavaScript, running entirely in the browser. Serve the
`KaintHTML` folder with any static web server (`python3 -m http.server`) and open it. Browsers on
the network can also join a collaborative session hosted by Kaint by opening the address its
session window shows, and any browser can host or join a P2P session with just its code. It has the
same GIF editor (keyframes, audio tracks and MP4 export included), brush options, themes and
languages, and on phones a single Save button with every option.

<table>
  <tr>
    <td width="280" align="center" valign="top"><img src="img/other/apk-welcome.png" alt="Kaint.APK welcome screen" width="280"></td>
    <td valign="top">
      <h4>Kaint.APK, on Android</h4>
      The web version as an Android app (<code>./build.sh --apk</code>), with everything Kaint.HTML has and a
      few things of its own:
      <ul>
        <li><b>A welcome screen</b>: <b>New</b>, <b>Open</b> (your recent images and the files on the
          device) and <b>Collaboration</b>, with your last image blurred behind and the recent ones at the
          bottom. The gear hides any of it, or the whole screen.</li>
        <li><b>Recent images kept on the phone</b>, with their layers, so they open again even though
          Android can't reopen a file by its path.</li>
        <li><b>Touch first</b>: two fingers zoom, move and rotate the image (tap the angle to straighten
          it), a long press lifts a layer or a GIF clip, and the windows tuck into the corners.</li>
        <li><b>One Save button</b> with every option, saving where Android asks.</li>
        <li><b>Smooth on phones</b>: the canvas draws at up to twice the screen's resolution and never
          stays frozen, and the welcome screen only animates what the phone draws cheaply.</li>
        <li><b>Collaborative sessions</b> with other phones, browsers and copies of Kaint.</li>
      </ul>
    </td>
  </tr>
</table>

## Opening and saving

✔ yes · ❌ no · ⚪ depends on the system, the browser or a plugin, and wasn't tested for this
release. Formats marked ¹ come from Qt's image plugins (`qt6-imageformats` and `kimageformats` on
Linux) or, on Windows, from the system's codecs: without them they don't appear in **Open** and
**Save As**. Everything else is read and written by Kaint itself, so it works everywhere.

### Images

| Format | Extensions | Open | Save | Web / Android |
| --- | --- | :---: | :---: | :---: |
| Paint.NET, with its layers | `.pdn` | ✔ | ✔ | ✔ / ✔ |
| OpenRaster, with its layers | `.ora` | ✔ | ✔ | ✔ / ✔ |
| Photoshop, with its layers | `.psd` | ✔ | ✔ | ✔ / ✔ |
| Photoshop large document, with its layers | `.psb` | ✔ | ✔ | ✔ / ✔ |
| PNG (8, 24 or 32 bits, interlaced) | `.png` | ✔ | ✔ | ✔ / ✔ |
| JPEG | `.jpg` `.jpeg` `.jpe` `.jfif` `.exif` | ✔ | ✔ | ✔ / ✔ |
| WebP | `.webp` | ✔ | ✔ | ✔ / ✔ |
| GIF, animated (frames as layers or images) | `.gif` | ✔ | ✔ | ✔ / ✔ |
| Animated PNG (exported from the GIF editor) | `.png` `.apng` | ✔ | ✔ | ✔ / ✔ |
| Animated WebP (exported from the GIF editor) | `.webp` | ✔ | ✔ | ✔ / ✔ |
| Video: MP4, WebM, MOV, MKV, AVI... (opened as frames, like an animated GIF, and exported from the GIF editor with its audio tracks, with FFmpeg; MP4 in the browser) | `.mp4` `.webm` `.mov` `.mkv` ... | ✔ | ✔ | ✔ / ✔ |
| BMP | `.bmp` `.dib` `.rle` | ✔ | ✔ | ✔ / ✔ |
| TIFF | `.tif` `.tiff` | ✔ | ✔ | ✔ / ✔ |
| TGA (8, 24 or 32 bits, RLE) | `.tga` | ✔ | ✔ | ✔ / ✔ |
| DDS (DXT1, DXT3, DXT5 or uncompressed; saved uncompressed 32-bit) | `.dds` | ✔ | ✔ | ✔ / ✔ |
| Windows icon, several sizes (one layer per size) | `.ico` | ✔ | ✔ | ✔ / ✔ |
| Windows cursor | `.cur` | ✔ | ✔ | ✔ / ✔ |
| QOI | `.qoi` | ✔ | ✔ | ✔ / ✔ |
| Netpbm | `.ppm` `.pgm` `.pbm` | ✔ | ✔ | ✔ / ✔ |
| PAM (Netpbm with transparency) | `.pam` | ✔ | ✔ | ✔ / ✔ |
| PCX (Paintbrush) | `.pcx` | ✔ | ✔ | ✔ / ✔ |
| SGI (Silicon Graphics) | `.sgi` `.rgb` `.rgba` `.bw` | ✔ | ✔ | ✔ / ✔ |
| Farbfeld | `.ff` | ✔ | ✔ | ✔ / ✔ |
| AVIF ² | `.avif` | ✔ | ✔ | ✔ / ⚪ |
| JPEG XL ² | `.jxl` | ✔ | ✔ | ✔ / ⚪ |
| HEIC / HEIF ² | `.heic` `.heif` `.hif` | ✔ | ✔ | ✔ / ❌ |
| JPEG XR ¹ | `.jxr` `.wdp` `.hdp` `.wmp` | ⚪ | ⚪ | ⚪ / ❌ |
| OpenEXR ¹ | `.exr` | ✔ | ✔ | ✔ / ⚪ |
| JPEG 2000 ¹ | `.jp2` `.j2k` `.jpf` | ✔ | ✔ | ✔ / ⚪ |
| XPM, XBM, WBMP ¹ | `.xpm` `.xbm` `.wbmp` | ✔ | ✔ | ❌ / ❌ |
| Softimage PIC ¹ | `.pic` | ✔ | ✔ | ❌ / ❌ |
| macOS icon ¹ | `.icns` | ✔ | ✔ | ✔ / ⚪ |
| Radiance HDR ¹ | `.hdr` | ✔ | ❌ | ✔ / ⚪ |
| Sun Raster ¹ | `.ras` `.sun` | ✔ | ❌ | ✔ / ⚪ |
| Krita, merged image ¹ | `.kra` | ⚪ | ❌ | ❌ / ❌ |
| GIMP, merged image ¹ | `.xcf` | ✔ | ❌ | ✔ / ⚪ |
| Camera RAW ¹ | `.dng` `.cr2` `.cr3` `.nef` `.arw` `.orf` `.rw2` `.raf` `.pef` `.srw` | ⚪ | ❌ | ❌ / ❌ |
| Animated Windows cursor, first frame ¹ | `.ani` | ✔ | ❌ | ❌ / ❌ |
| Pixar ¹ | `.pxr` | ⚪ | ❌ | ❌ / ❌ |
| Amiga IFF ¹ | `.iff` `.ilbm` `.lbm` | ⚪ | ❌ | ❌ / ❌ |
| PFM (floating point) ¹ | `.pfm` | ✔ | ❌ | ✔ / ⚪ |
| PlayStation TIM ¹ | `.tim` | ✔ | ❌ | ✔ / ⚪ |
| SVG, as an image (rasterized at its size) ¹ | `.svg` `.svgz` | ✔ | ❌ | ✔ / ⚪ |
| Formats of installed paint.js plugins (Photoshop with layers...) | `.paintjs` | ⚪ | ⚪ | ⚪ / ⚪ |

¹ On Linux with `kimageformats` (and `qt6-imageformats`): the ✔ ones were opened (and, the ones
Kaint can save, saved and opened again) with the Kaint built for this release on Arch Linux. JPEG
XR also needs `libjxr` (Arch: `sudo pacman -S libjxr`): without it, kimageformats doesn't load its
JPEG XR plugin. On Windows, JPEG XR is built in and HEIC, AVIF and JPEG XL need their Microsoft
Store extensions. The web version (tested in Chromium) opens AVIF with the browser, SVG as an image,
JPEG XR with jpegxr.js, and HEIC, JPEG XL, OpenEXR, JPEG 2000, Radiance HDR, PFM, Sun Raster, GIMP,
PlayStation TIM and macOS icons with magick-wasm when the browser can't; saving AVIF or JPEG XL
depends on the browser.

² With Kaint's own codecs when it is built with libwebp, libavif, libjxl and libheif (the Arch package
and the AppImage are): HEIC is read only. Without them, from Qt's image plugins, like ¹.

Photoshop files keep each layer's name, visibility, opacity and blend mode (the ones both programs
share); PNG, JPEG, WebP and TIFF keep their color profile and resolution.

### Other files

| What | Extensions | Where |
| --- | --- | --- |
| Paint.NET effect plugins | `.dll` | Settings → Plugins |
| Kaint native plugins | `.so` (Linux, macOS, FreeBSD), `.dll` (Windows) | Settings → Plugins |
| PhotoCraft filter plugins | `.wasm` | Settings → Plugins |
| LightCraft looks and develop presets | `.cube` `.3dl` `.xmp` | Settings → Plugins |
| paint.js plugins | `.paintjs` | Settings → Plugins, or open it |
| Paint.NET palettes | `.txt` | Colors window → palette menu |
| Paint.NET shapes | `.xaml` | Settings → Shapes |
| Fonts for the Text tool | `.ttf` `.otf` `.ttc` `.otc` `.woff` `.woff2` | Settings → Fonts |
| Icon packs | `.zip` (with an `asset-pack.json`) | Settings → Icon packs |
| Custom themes | `.json`, or `.zip` with their background | Settings → User interface → Custom theme |
| Theme backgrounds | images, animated GIF or WebP, and videos (`.mp4` `.m4v` `.mov` `.webm` `.mkv` `.avi` `.wmv` `.mpg` `.mpeg` `.ogv` `.3gp`) | Custom theme editor |
| Custom languages | `.kaintlang` | Settings → User interface |

## 📦 Install

Kaint needs **Qt 6.5 or newer**: Arch Linux, Debian 13, Ubuntu 25.04 or newer, or Windows 10/11.
The Flatpak and the AppImage run on other distributions too.

<details open>
<summary><b>📦 Flatpak (any distribution)</b></summary>

Build and install the Flatpak from the source code (needs `flatpak-builder`):

```bash
./build.sh --flatpak        # builds and installs io.github.ssserch.Kaint for your user
flatpak run io.github.ssserch.Kaint
```

Paint.NET plugins work out of the box, because the Flatpak includes mono. Plugins that bring their
own settings window (made with WinForms) need X11, which the Flatpak only gets on Wayland if you
allow it:

```bash
flatpak override --user --socket=x11 io.github.ssserch.Kaint
```

</details>

<details open>
<summary><b>📦 AppImage (any distribution)</b></summary>

```bash
./build.sh --appimage       # builds build-appimage/Kaint-<version>-x86_64.AppImage
chmod +x build-appimage/Kaint-*.AppImage && ./build-appimage/Kaint-*.AppImage
```

The AppImage bundles Qt, so it runs on distributions whose glibc is at least as new as the one it
was built on. On a newer system, `build.sh` builds it inside an **Ubuntu 22.04** container (glibc
2.35, with podman or docker; Qt comes from aqtinstall and everything downloaded is kept in
`build-appimage-cache/`), so it also runs on the oldest distributions that are still supported;
`--appimage-native` builds it with this system's Qt instead. Paint.NET plugins use the system's mono
(or Wine) if it's installed, and the KDE theme option needs the native build or the Flatpak.

</details>

<details open>
<summary><b>🐧 Arch Linux</b></summary>

```bash
sudo pacman -S --needed gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons libdatachannel
./build.sh --install        # installs to ~/.local (shows up in the app menu)
```

Or build a native package with `makepkg` and install it with pacman:

```bash
./build.sh --pkg            # builds build-pkg/kaint-<version>-1-x86_64.pkg.tar.zst
sudo pacman -U build-pkg/kaint-*.pkg.tar.zst
```

</details>

<details open>
<summary><b>🍥 Debian / Ubuntu / Mint</b></summary>

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev \
    qt6-tools-dev-tools qt6-l10n-tools qt6-svg-plugins qt6-wayland zlib1g-dev libgl-dev libssl-dev breeze-icon-theme file
./build.sh --deb            # builds build-deb/kaint_<version>_amd64.deb
sudo apt install ./build-deb/kaint_*_amd64.deb
```

</details>

<details open>
<summary><b>🪟 Windows 10 / 11</b></summary>

Download the source code and double-click `build.bat` (or run `build.bat --install`). It builds
Kaint and installs it in `%LOCALAPPDATA%\Programs\Kaint` with a Start menu entry and its own
uninstaller (`Uninstall.exe`, next to the `bin` folder): uninstall it from *Installed apps* or the
*Control Panel* like any other app (or run `build.bat --uninstall`). Installs made with an older
`build.bat` get the uninstaller when you install again. If Qt, MinGW, CMake and Ninja are missing, it offers to download them
(about 1.5 GB) into `%LOCALAPPDATA%\Kaint\toolchain`.

`build.bat --setup` makes `Kaint-<version>-windows-x64-setup.exe`, a regular installer for
sharing: it installs Kaint for the current user (no administrator rights), adds it to the Start
menu (and optionally the desktop) and registers it in *Installed apps*, so it can be removed like
any other application. Its uninstaller can also delete your settings, palettes, plugins, icon packs,
fonts and recovery copies. If NSIS is missing, it offers to download it (about 2 MB).

Everything the installer shows and does comes from **`winsetup.json`** (next to `build.bat`): name,
version (empty: the one in `version.json`), publisher, website, copyright, icons, license, output
file name, install folder, executable, registry key, compression, which pages appear (welcome,
license, components, folder, finish), whether it runs Kaint at the end, the Start menu and desktop
shortcuts (`optional`, `checked`, `always` or `never`), what *delete my settings* removes, and the
languages (NSIS names: `English`, `Spanish`, `French`...) with their texts. In texts, `{name}`,
`{version}` and `{arch}` are replaced. Anything left out keeps its usual value. Use another file
with `build.bat --setup --winsetup path\to\file.json`.

`build.bat --zip` makes a portable `Kaint-<version>-windows-x64.zip`: unzip it anywhere and run
`Kaint\bin\kaint.exe`.

</details>

<details open>
<summary><b>🟢 Android (Kaint.APK)</b></summary>

Kaint.APK is the web version (`KaintHTML`) as an Android app (`android/`). It is built by
`build.sh` (Linux, macOS) or `build.bat` (Windows, natively: no WSL needed), from their menus or with
`--apk` (to install on the phone) or `--aab` (an App Bundle to publish on Google Play). They ask where
`KaintHTML` is (Enter accepts this repository's) and, for the App Bundle, your upload key, its alias
and passwords (or use `--kainthtml`, `--keystore`, `--alias` and the `KAINT_STORE_PASSWORD` /
`KAINT_KEY_PASSWORD` variables). The result goes to `build-apk/Kaint-<version>.apk` or
`build-aab/Kaint-<version>.aab`. It needs a JDK from 17 to 25 (21 is best: `jdk21-openjdk` on Arch,
`openjdk-21-jdk` on Debian/Ubuntu, `brew install openjdk@21` on macOS; on Windows, `build.bat` offers
to download Temurin 21 if there is none). The Android SDK command-line tools and Gradle are
downloaded the first time if missing (checked against their checksums), and you accept the Android
SDK licenses yourself. APKs are signed with a key kept on this computer, so newer versions install
over older ones. On phones and tablets two fingers zoom, move and **rotate** the image (tap the angle
in the corner to straighten it), the scroll bars can be hidden (they are, by default, on phones) and
the **Effects** menu has a section with Extract Line Art, Chromatic Aberration and Glitch.

```bash
./build.sh --apk            # or build.bat --apk on Windows; --aab for Google Play
adb install -r build-apk/Kaint-*.apk
```

</details>

> [!TIP]
> Optional extras for more formats and for Paint.NET plugins: `qt6-imageformats kimageformats
> mono libgdiplus` (Arch) or `qt6-image-formats-plugins kimageformat6-plugins mono-devel libgdiplus`
> (Debian/Ubuntu). On Windows, plugins work out of the box.

Run `build.sh` or `build.bat` without options for an **interactive menu**: *Compile* or *Debug*
lead to the list of formats (the ones that don't suit this system are marked *not recommended*),
and *Exit* leaves.

<p align="center">
  <img src="img/other/build-menu.png" alt="The build.bat menu: Compile, Debug or Exit, and then the list of formats" width="700">
</p>

| Option | `build.sh` (Linux, FreeBSD, macOS) | `build.bat` (Windows) |
| --- | :---: | :---: |
| Just build the executable | `--build` (in `build/`) | `--build` (in `build-windows\`) |
| Build and install | `--install` | `--install` |
| Build and run | `--run` | `--run` |
| Run the tests | `--test` | `--test` |
| Debug build | `--debug` | `--debug` |
| Package | `--deb` (Debian/Ubuntu) · `--pkg` (Arch) · `--flatpak` · `--appimage` · `--tar` | `--setup` (installer) · `--zip` |
| Variants | `--avx2` (x86-64-v3) · `--musl` (Alpine container) · `--arm64` (aarch64) | `--avx2` · `--arm64` |
| FreeBSD / macOS | `--tar` on FreeBSD · `--tar` on macOS (with Kaint.app) | |
| Linux builds from Windows | | `--wsl build`, `deb`, `pkg`, `appimage`, `appimage-avx2`, `tar`, `tar-musl` or `tar-arm64` |
| macOS build from Windows | | option 19 of the menu ([GitHub Actions](#-build-on-github-github-actions), with `gh`) |
| Web version (.zip) | `--web` | `--web` |
| Mobile version (Android) | `--apk` · `--aab` | `--apk` · `--aab` (with WSL) |
| OpenD2D for Paint.NET plugins | `--opend2d latest`, `main`, a release (`v0.1.0-beta.1`), `local` (`2dlocal/`), a folder or `off` | |
| AppImage with this system's glibc | `--appimage-native` | |
| Build threads | `--jobs N` | `--jobs N` |
| Every option | `--help` | `--help` |

Both scripts use `ccache` or `sccache` when installed (`--no-ccache` turns it off), show how long
the build took and the size of what they made, and say which step failed.

**OpenD2D**: when `cargo` (Rust) is installed, Kaint builds [OpenD2D](https://github.com/daijro/opend2d)
together with it (`build/opend2d/`, installed in `lib/kaint/opend2d`) for the OpenD2D plugin system.
In a terminal `build.sh` asks which version to use: the latest (downloaded, or updated, on every build),
one of the versions published in its repository, the local copy in `2dlocal/` or none (`--opend2d`
answers it beforehand; with CMake, `-DKAINT_OPEND2D_REF=...`, `-DKAINT_OPEND2D_SOURCE=...` or
`-DKAINT_OPEND2D=OFF`). Without cargo, Kaint is built as always and plugins use mono without OpenD2D
(or Wine).

Packagers can build with `-DKAINT_UPDATE_CHECK=OFF` so that Kaint leaves updates to the package
manager and never looks for new versions on GitHub (the Flatpak does this).

### Build on GitHub (GitHub Actions)

The **Compilar Kaint** (*build Kaint*) workflow, `.github/workflows/build.yml`, builds Kaint
on GitHub's machines with the same `build.bat` and `build.sh`, so anyone can get the files without
installing anything, and **macOS can be built from Windows or Linux**:

| System | What it makes |
| --- | --- |
| Windows x64 | portable `.zip` and installer (`-setup.exe`) |
| Linux x86_64 | AppImage (built on Ubuntu 22.04: runs on old distributions too) and `.deb` (Debian 13 / Ubuntu 25.04 or newer) |
| macOS arm64 | `.tar.gz` with **Kaint.app**, Qt included (Apple Silicon, macOS 13 or newer) |
| Web | `.zip` of Kaint.HTML (only when chosen) |

- **By hand**: on GitHub, *Actions* > *Compilar Kaint* > *Run workflow*, tick the systems and press
  *Run workflow*. In a fork it works the same way (enable *Actions* in the fork first).
- **From `build.bat`**: option 19 of the menu (*tar.gz macOS arm64*) starts the macOS build with the
  [GitHub CLI](https://cli.github.com) (`gh auth login` once). It builds what is pushed to GitHub on
  the current branch, not local changes.
- **On its own**: on every push to `main` and every pull request that changes the code (not the
  documentation), to check that it still builds everywhere.

When it finishes, the files are in the run's page under **Artifacts** (kept for 30 days). The
workflow never publishes anything: releases are uploaded by hand. The macOS app is not signed by
Apple: the first time, right-click *Kaint.app* > *Open* (or run `xattr -dr com.apple.quarantine
Kaint.app`).

## License

Kaint is free software under the [GNU General Public License v3.0](LICENSE).
`src/pdnbridge/` contains MIT-licensed code from Paint.NET 3.36, `data/icons/` bundles KDE's
Breeze icons (LGPL-3.0-or-later), and the Windows 7, Windows XP and Windows 98 themes and title bar
buttons use colors, gradients and drawings from [7.css](https://khang-nd.github.io/7.css/) by Khang
Nguyen Duy, [XP.css](https://botoxparty.github.io/XP.css/) by Adam Hammad and Jordan Scales and
[98.css](https://jdan.github.io/98.css/) by Jordan Scales (all MIT, see `data/licenses/`).
`third_party/quickjs-ng/` is [QuickJS-ng](https://github.com/quickjs-ng/quickjs) (MIT), the
JavaScript engine that runs paint.js plugins. Kaint supports the plugins of
[paint.js](https://github.com/LabyStudio/paintdotjs) by LabyStudio (.paintjs packages and their
JavaScript API); it doesn't use paint.js's own plugin system. The OpenD2D plugin system uses [OpenD2D](https://github.com/daijro/opend2d) by daijro (GPL-3.0-or-later),
built from its repository with Kaint. The P2P collaboration uses
[libdatachannel](https://github.com/paullouisageneau/libdatachannel) (MPL-2.0) with usrsctp
(BSD-3-Clause), libjuice (MPL-2.0) and plog (MIT), and Mbed TLS (Apache-2.0) where there is no
OpenSSL. The PhotoCraft features and tools reimplement algorithms of PhotoCraft by the ArtCraft Team
and the PhotoCraft contributors (MIT, `data/licenses/PhotoCraft-LICENSE.txt`); the web version opens
JPEG XR with [jpegxr](https://github.com/brion/jpegxr.js) by Brion Vibber, built on Microsoft's
jxrlib (BSD), and HEIC, JPEG XL, AVIF, OpenEXR, JPEG 2000 and other formats (where the browser can't) with
[magick-wasm](https://github.com/dlemstra/magick-wasm) by Dirk Lemstra (Apache-2.0). Plugins from
**Browse Plugins** are the work of their authors, credited next to each one. Some features were
inspired by [paint.rs](https://github.com/daijro/paint.rs) by daijro, such as having PhotoCraft's
tools in Kaint. (`third_party/pdn5/`, GPL-3.0). WebP, AVIF, JPEG XL and HEIC use libwebp (BSD), libavif
(BSD), libjxl (BSD) and libheif (LGPL-3.0), and video export runs [FFmpeg](https://ffmpeg.org) when it
is installed.

## Disclaimer

Kaint is an independent project. It is not affiliated with, endorsed by or sponsored by dotPDN
LLC or Rick Brewster. Paint.NET is a trademark of dotPDN LLC, mentioned only to describe the
software Kaint is inspired by.

---

<div align="center">
  <img src="img/kaint-128.png" alt="Kaint icon" width="96">
  <p><b>Kaint</b> · made by <a href="https://github.com/ssserch">ssserch</a></p>

  [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/E1E5ZOEH6)
</div>
