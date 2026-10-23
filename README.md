<div align="center">
  <img src="img/other/kaintqt6logo.png" alt="kaint.qt6 logo" width="460">

  <h3>The Paint.NET experience, native on Linux (and Windows!)</h3>
  <p>A free raster image editor built with C++20 and Qt 6.</p>

  [![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus)](CMakeLists.txt)
  [![Qt 6](https://img.shields.io/badge/Qt-6-41CD52.svg?logo=qt&logoColor=white)](https://www.qt.io/)
  [![Platforms: Linux | Windows](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-informational.svg)](#-install)
  [![Paint.NET plugins](https://img.shields.io/badge/Paint.NET-plugins-8a2be2.svg)](#-features)
  [![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

  <p>
    <a href="#-features">Features</a> •
    <a href="#-themes">Themes</a> •
    <a href="#-install">Install</a> •
    <a href="#-contributing">Contributing</a> •
    <a href="#-license">License</a>
  </p>
</div>

<div align="center">
  <img src="img/other/screenshot.png" alt="Kaint in the dark theme with the tools, colors, history and layers windows" width="900">
</div>

---

**Kaint** recreates the look, the tools and the workflow of [Paint.NET](https://www.getpaint.net/)
as a native application: no Wine, no .NET runtime. It is made for KDE Plasma on Wayland, it also
runs on Windows 10 and 11, and it should feel like home if you come from Paint.NET.

## Features

<table>
  <tr>
    <td width="50%" valign="top">
      <h4>Paint.NET tools</h4>
      Selections with their combine modes, move pixels or the selection (scale, rotate, movable
      pivot), brush, pencil, eraser, clone stamp, recolor, paint bucket, gradients, lines and
      curves, shapes and text, all with the same options bar.
    </td>
    <td width="50%" valign="top">
      <h4>Editable history</h4>
      Click through the history like in Paint.NET. Gradients, lines and shapes stay editable when
      you go back to one of their steps.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Layers</h4>
      Blend modes, opacity, visibility and properties, with live thumbnails.
    </td>
    <td valign="top">
      <h4>Adjustments and effects</h4>
      Live preview while you tweak them, and <b>Repeat</b> (Ctrl+F) for the last one.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Plugins</h4>
      Paint.NET effect plugins (.dll), through mono on Linux and the .NET Framework built into
      Windows, plus native plugins with a stable C API.
    </td>
    <td valign="top">
      <h4>Formats</h4>
      OpenRaster (.ora, with layers), PNG, JPEG, WebP, AVIF, JPEG XL, BMP, TIFF, TGA, animated GIF
      and ICO. Opens Paint.NET <b>.pdn</b> files.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h4>Recovery copies</h4>
      Unsaved images are kept safe in case Kaint closes unexpectedly.
    </td>
    <td valign="top">
      <h4>Languages</h4>
      English and Spanish (for now).
    </td>
  </tr>
</table>

## Themes

Light and dark (or follow the system), the real KDE theme on Linux, and your own **custom theme**:
colors, toolbar layout and a canvas background image with placement, scale, blur and tint.

<table>
  <tr>
    <td width="50%"><img src="img/other/screenshot-light.png" alt="Light theme"></td>
    <td width="50%"><img src="img/other/screenshot.png" alt="Dark theme"></td>
  </tr>
  <tr>
    <td align="center"><sub>Light</sub></td>
    <td align="center"><sub>Dark</sub></td>
  </tr>
</table>

## 📦 Install

Kaint needs **Qt 6.5 or newer**: Arch Linux, Debian 13, Ubuntu 25.04 or newer, or Windows 10/11.

<details open>
<summary><b>🐧 Arch Linux</b></summary>

```bash
sudo pacman -S --needed gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons
./build.sh --install        # installs to ~/.local (shows up in the app menu)
```

Or build a native package with `makepkg` and install it with pacman:

```bash
./build.sh --pkg            # builds build-pkg/kaint-<version>-1-x86_64.pkg.tar.zst
sudo pacman -U build-pkg/kaint-*.pkg.tar.zst
```

</details>

<details open>
<summary><b>🍥 Debian / Ubuntu</b></summary>

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev \
    qt6-tools-dev-tools qt6-l10n-tools qt6-svg-plugins qt6-wayland zlib1g-dev libgl-dev breeze-icon-theme
./build.sh --deb            # builds build-deb/kaint_<version>_amd64.deb
sudo apt install ./build-deb/kaint_*_amd64.deb
```

</details>

<details open>
<summary><b>🪟 Windows 10 / 11</b></summary>

Download the source code and double-click `build.bat` (or run `build.bat --install`). It builds
Kaint and installs it in `%LOCALAPPDATA%\Programs\Kaint` with a Start menu entry; uninstall it
from *Installed apps*. If Qt, MinGW, CMake and Ninja are missing, it offers to download them
(about 1.5 GB) into `%LOCALAPPDATA%\Kaint\toolchain`.

`build.bat --zip` makes a portable `Kaint-<version>-windows-x64.zip`: unzip it anywhere and run
`Kaint\bin\kaint.exe`.

</details>

> [!TIP]
> Optional extras for more formats and for Paint.NET plugins: `qt6-imageformats kimageformats
> mono libgdiplus` (Arch) or `qt6-image-formats-plugins kimageformat6-plugins mono-devel libgdiplus`
> (Debian/Ubuntu). On Windows, plugins work out of the box.

| Option | `build.sh` (Linux) | `build.bat` (Windows) |
| --- | :---: | :---: |
| Build and install | `--install` | `--install` |
| Build and run | `--run` | `--run` |
| Run the tests | `--test` | `--test` |
| Debug build | `--debug` | `--debug` |
| Package | `--deb` (Debian/Ubuntu) · `--pkg` (Arch) | `--zip` |
| Every option | `--help` | `--help` |

## 📄 License

Kaint is free software under the [GNU General Public License v3.0](LICENSE).
`src/pdnbridge/` contains MIT-licensed code from Paint.NET 3.36, and `data/icons/` bundles KDE's
Breeze icons (LGPL-3.0-or-later).

## ⚠️ Disclaimer

Kaint is an independent project. It is not affiliated with, endorsed by or sponsored by dotPDN
LLC or Rick Brewster. Paint.NET is a trademark of dotPDN LLC, mentioned only to describe the
software Kaint is inspired by.
