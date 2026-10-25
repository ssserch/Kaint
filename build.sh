#!/usr/bin/env bash
# Compila e instala Kaint en Arch Linux o Debian/Ubuntu (opciones: ./build.sh --help).
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

BUILD_TYPE=Release
BUILD_DIR=build
PREFIX="$HOME/.local"
DO_CLEAN=0
DO_INSTALL=0
DO_RUN=0
DO_TEST=0
DO_DEB=0
DO_PKG=0
DO_FLATPAK=0
DO_APPIMAGE=0
APP_ID=io.github.ssserch.Kaint

# --- Paquetes (necesarios y recomendados) -----------------------------------
ARCH_REQUIRED=(gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons)
ARCH_OPTIONAL=(qt6-imageformats kimageformats qt6-translations mono libgdiplus)
DEB_REQUIRED=(build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev
              qt6-tools-dev-tools qt6-l10n-tools qt6-svg-plugins qt6-wayland zlib1g-dev libgl-dev
              breeze-icon-theme)
DEB_OPTIONAL=(qt6-image-formats-plugins kimageformat6-plugins qt6-translations-l10n mono-devel libgdiplus)
# --deb: CPack calcula las dependencias del paquete con file y dpkg-shlibdeps
ARCH_DEB_TOOLS=(file dpkg)
DEB_DEB_TOOLS=(file dpkg-dev)

# --- Herramientas del AppImage (versiones fijas, comprobadas con su SHA-256) -------------------
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1
LINUXDEPLOY_QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1
declare -A APPIMAGE_TOOL_SHA256=(
    [linuxdeploy-x86_64.AppImage]=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
    [linuxdeploy-aarch64.AppImage]=620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff
    [linuxdeploy-plugin-qt-x86_64.AppImage]=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
    [linuxdeploy-plugin-qt-aarch64.AppImage]=bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1
)

c_bold=$'\e[1m'; c_red=$'\e[31m'; c_yellow=$'\e[33m'; c_green=$'\e[32m'; c_reset=$'\e[0m'
if [[ ! -t 1 ]]; then c_bold=""; c_red=""; c_yellow=""; c_green=""; c_reset=""; fi

# --- Opciones ---------------------------------------------------------------
usage() {
    cat <<EOF
Uso: $0 [opciones]

  -d, --debug          compila con símbolos de depuración (build-debug/)
  -c, --clean          borra la carpeta de compilación antes de empezar
  -t, --test           ejecuta las pruebas del núcleo después de compilar
  -i, --install        instala (por defecto en ~/.local, sin sudo)
      --prefix RUTA    prefijo de instalación (p. ej. /usr/local; usará sudo si hace falta)
  -r, --run            ejecuta Kaint al terminar
      --deb            crea el paquete .deb en build-deb/ (necesita dpkg)
      --pkg            crea el paquete de Arch en build-pkg/ (necesita makepkg)
      --flatpak        crea e instala el Flatpak con el código de esta carpeta (necesita flatpak-builder)
      --appimage       crea el AppImage en build-appimage/ (descarga linuxdeploy)
  -h, --help           muestra esta ayuda
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -d|--debug)   BUILD_TYPE=Debug; BUILD_DIR=build-debug ;;
        -c|--clean)   DO_CLEAN=1 ;;
        -t|--test)    DO_TEST=1 ;;
        -i|--install) DO_INSTALL=1 ;;
        --prefix)     PREFIX="${2:?falta la ruta de --prefix}"; shift ;;
        -r|--run)     DO_RUN=1 ;;
        --deb)        DO_DEB=1; BUILD_TYPE=Release; BUILD_DIR=build-deb; PREFIX=/usr ;;
        --pkg)        DO_PKG=1 ;;
        --flatpak)    DO_FLATPAK=1 ;;
        --appimage)   DO_APPIMAGE=1; BUILD_TYPE=Release; BUILD_DIR=build-appimage; PREFIX=/usr ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "${c_red}Opción desconocida: $1${c_reset}" >&2; usage; exit 1 ;;
    esac
    shift
done

# --- Dependencias -----------------------------------------------------------
check_packages() {
    local installed=$1 install_cmd=$2; shift 2
    local -n required=$1 optional=$2
    local missing=() optional_missing=() pkg
    for pkg in "${required[@]}"; do $installed "$pkg" || missing+=("$pkg"); done
    if (( ${#missing[@]} )); then
        echo "${c_red}${c_bold}Faltan paquetes necesarios:${c_reset} ${missing[*]}" >&2
        echo "Instálalos con:" >&2
        echo "  $install_cmd ${missing[*]}" >&2
        exit 1
    fi
    for pkg in "${optional[@]}"; do $installed "$pkg" || optional_missing+=("$pkg"); done
    if (( ${#optional_missing[@]} )); then
        echo "${c_yellow}Recomendado (opcional):${c_reset} $install_cmd ${optional_missing[*]}"
        if [[ " ${optional_missing[*]} " == *" mono"* ]]; then
            echo "  Sin mono no se compila la capa de compatibilidad con los complementos de Paint.NET."
        elif [[ " ${optional_missing[*]} " == *" libgdiplus "* ]]; then
            echo "  Sin libgdiplus muchos complementos de Paint.NET no pueden cargarse (usan System.Drawing)."
        fi
        # El .deb se reparte: mejor decidir antes de crearlo sin esas funciones.
        if (( DO_DEB )) && [[ -t 0 ]]; then
            local answer
            read -r -p "¿Crear el .deb sin ellos? [s/N] " answer
            [[ "$answer" == [sSyY]* ]] || { echo "Cancelado."; exit 1; }
        fi
    fi
}
pacman_installed() { pacman -Qq "$1" >/dev/null 2>&1; }
dpkg_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }

if (( DO_FLATPAK )); then
    : # Flatpak trae sus propias dependencias (runtime y SDK de KDE)
elif [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    echo "${c_yellow}CMAKE_PREFIX_PATH definido (Qt propio): se omite la comprobación de paquetes.${c_reset}"
elif command -v pacman >/dev/null 2>&1; then
    (( DO_DEB )) && ARCH_REQUIRED+=("${ARCH_DEB_TOOLS[@]}")
    check_packages pacman_installed "sudo pacman -S --needed" ARCH_REQUIRED ARCH_OPTIONAL
elif command -v dpkg-query >/dev/null 2>&1; then
    (( DO_DEB )) && DEB_REQUIRED+=("${DEB_DEB_TOOLS[@]}")
    check_packages dpkg_installed "sudo apt install" DEB_REQUIRED DEB_OPTIONAL
else
    echo "${c_yellow}No se encontró pacman ni apt; se omite la comprobación de dependencias.${c_reset}"
fi

# --- Paquete de Arch ---------------------------------------------------------
if (( DO_PKG )); then
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "${c_red}Falta makepkg (paquete pacman): --pkg solo funciona en Arch Linux.${c_reset}" >&2
        exit 1
    fi
    echo "${c_bold}Creando el paquete de Arch...${c_reset}"
    mkdir -p build-pkg
    (cd packaging/arch && KAINT_SOURCE_DIR="$PWD/../.." BUILDDIR="$PWD/../../build-pkg" \
        PKGDEST="$PWD/../../build-pkg" makepkg --force --cleanbuild)
    pkg=$(ls -t build-pkg/kaint-[0-9]*.pkg.tar.* | head -n 1)
    echo "${c_green}Paquete:${c_reset} $pkg"
    echo "  Instálalo con: sudo pacman -U $pkg"
    exit 0
fi

# --- Flatpak ----------------------------------------------------------------
if (( DO_FLATPAK )); then
    if command -v flatpak-builder >/dev/null 2>&1; then
        builder=(flatpak-builder)
    elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
        builder=(flatpak run org.flatpak.Builder)
    else
        echo "${c_red}Falta flatpak-builder.${c_reset} Instálalo con uno de estos:" >&2
        echo "  sudo pacman -S flatpak-builder" >&2
        echo "  sudo apt install flatpak-builder" >&2
        echo "  flatpak install flathub org.flatpak.Builder" >&2
        exit 1
    fi
    echo "${c_bold}Creando el Flatpak...${c_reset}"
    mkdir -p build-flatpak
    # El manifiesto de Flathub descarga la versión publicada; aquí el módulo kaint (el último)
    # usa esta carpeta, sin lo que ignora git.
    {
        awk '{ line[NR] = $0 } /^    sources:$/ { last = NR } END { for (i = 1; i <= last; i++) print line[i] }' \
            "packaging/flatpak/$APP_ID.yml"
        echo "      - type: dir"
        printf '        path: "%s"\n' "$PWD"
        echo "        skip:"
        echo '          - ".git"'
        while read -r ignored || [[ -n "$ignored" ]]; do
            [[ -n "$ignored" && "$ignored" != \#* ]] && printf '          - "%s"\n' "${ignored%/}"
        done < .gitignore
    } > "build-flatpak/$APP_ID.yml"
    flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
    "${builder[@]}" --user --install --install-deps-from=flathub --force-clean \
        --state-dir=build-flatpak/.flatpak-builder build-flatpak/build-dir "build-flatpak/$APP_ID.yml"
    echo "${c_green}Instalado:${c_reset} flatpak run $APP_ID (también en el menú de aplicaciones)"
    exit 0
fi

# --- Compilación ------------------------------------------------------------
if (( DO_CLEAN )); then
    echo "${c_bold}Borrando $BUILD_DIR/${c_reset}"
    rm -rf -- "$BUILD_DIR"
fi

echo "${c_bold}Configurando ($BUILD_TYPE)...${c_reset}"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"

echo "${c_bold}Compilando con $(nproc) hilos...${c_reset}"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

if (( DO_TEST )); then
    echo "${c_bold}Ejecutando pruebas...${c_reset}"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if (( DO_DEB )); then
    echo "${c_bold}Creando el paquete .deb...${c_reset}"
    (cd "$BUILD_DIR" && cpack -G DEB)
    echo "${c_green}Paquete:${c_reset} $(ls "$BUILD_DIR"/kaint_*.deb)"
    echo "  Instálalo con: sudo apt install ./$(ls "$BUILD_DIR"/kaint_*.deb)"
fi

# --- AppImage ---------------------------------------------------------------
if (( DO_APPIMAGE )); then
    echo "${c_bold}Creando el AppImage...${c_reset}"
    arch=$(uname -m)
    tools="$PWD/$BUILD_DIR/appimage-tools"
    mkdir -p "$tools"
    for tool in linuxdeploy linuxdeploy-plugin-qt; do
        file="$tool-$arch.AppImage"
        sum=${APPIMAGE_TOOL_SHA256[$file]:-}
        if [[ -z "$sum" ]]; then
            echo "${c_red}El AppImage solo se puede crear en x86_64 o aarch64 (esto es $arch).${c_reset}" >&2
            exit 1
        fi
        if ! echo "$sum  $tools/$file" | sha256sum -c --status 2>/dev/null; then
            url=$LINUXDEPLOY_URL
            [[ "$tool" == linuxdeploy-plugin-qt ]] && url=$LINUXDEPLOY_QT_URL
            echo "Descargando $file..."
            curl -fL --progress-bar -o "$tools/$file.part" "$url/$file"
            if ! echo "$sum  $tools/$file.part" | sha256sum -c --status; then
                rm -f "$tools/$file.part"
                echo "${c_red}$file no coincide con su SHA-256: no se usa.${c_reset}" >&2
                exit 1
            fi
            mv "$tools/$file.part" "$tools/$file"
        fi
        chmod +x "$tools/$file"
    done

    # El complemento de Qt copia los plugins del Qt con el que se compiló (lo dice qmake).
    qt_dir=$(sed -n 's/^Qt6_DIR:[A-Z]*=//p' "$BUILD_DIR/CMakeCache.txt")
    qt_prefix=$(cd "$qt_dir/../../.." && pwd)
    qmake=""
    for candidate in "$qt_prefix/bin/qmake6" "$qt_prefix/bin/qmake" "$qt_prefix/lib/qt6/bin/qmake" \
                     "$(command -v qmake6 || true)"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then qmake=$candidate; break; fi
    done
    if [[ -z "$qmake" ]]; then
        echo "${c_red}No encuentro qmake de Qt 6 (paquete qt6-base o qt6-base-dev-tools).${c_reset}" >&2
        exit 1
    fi
    platforms="$("$qmake" -query QT_INSTALL_PLUGINS)/platforms"
    # Un Qt fuera del sistema (CMAKE_PREFIX_PATH): linuxdeploy busca allí sus bibliotecas.
    qt_libs=$("$qmake" -query QT_INSTALL_LIBS)
    # Wayland (Qt 6.10 o más lo tiene en un solo plugin) y offscreen para --screenpreview
    extra_platforms="libqoffscreen.so"
    if [[ -f "$platforms/libqwayland.so" ]]; then
        extra_platforms="libqwayland.so;$extra_platforms"
    elif [[ -f "$platforms/libqwayland-egl.so" ]]; then
        extra_platforms="libqwayland-egl.so;libqwayland-generic.so;$extra_platforms"
    fi

    appdir="$PWD/$BUILD_DIR/AppDir"
    rm -rf "$appdir"
    DESTDIR="$appdir" cmake --install "$BUILD_DIR" >/dev/null
    rm -rf "$appdir/usr/include"
    # Dentro del AppImage, el .desktop llama a kaint sin ruta.
    sed -i 's|^Exec=.*|Exec=kaint %F|' "$appdir/usr/share/applications/$APP_ID.desktop"
    version=$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' version.json)
    output="Kaint-$version-$arch.AppImage"
    rm -f "$BUILD_DIR/$output"
    (cd "$BUILD_DIR" && PATH="$tools:$PATH" LD_LIBRARY_PATH="$qt_libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        QMAKE="$qmake" EXTRA_QT_MODULES="svg;waylandcompositor" \
        EXTRA_PLATFORM_PLUGINS="$extra_platforms" APPIMAGE_EXTRACT_AND_RUN=1 LDAI_OUTPUT="$output" \
        "$tools/linuxdeploy-$arch.AppImage" --appdir "$appdir" --plugin qt --output appimage \
            --desktop-file "$appdir/usr/share/applications/$APP_ID.desktop" \
            --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$APP_ID.png")
    echo "${c_green}AppImage:${c_reset} $BUILD_DIR/$output"
    echo "  Ejecútalo con: chmod +x $BUILD_DIR/$output && ./$BUILD_DIR/$output"
fi

# --- Instalación ------------------------------------------------------------
if (( DO_INSTALL )); then
    echo "${c_bold}Instalando en $PREFIX...${c_reset}"
    mkdir -p "$PREFIX" 2>/dev/null || true
    if [[ -w "$PREFIX" ]]; then
        cmake --install "$BUILD_DIR"
    else
        sudo cmake --install "$BUILD_DIR"
    fi
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
    echo "${c_green}Instalado:${c_reset} $PREFIX/bin/kaint (también en el menú de aplicaciones)"
fi

echo "${c_green}${c_bold}Listo:${c_reset} $BUILD_DIR/kaint"

if (( DO_RUN )); then
    exec "$BUILD_DIR/kaint"
fi
