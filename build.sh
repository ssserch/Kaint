#!/usr/bin/env bash
# Compila Kaint en Arch Linux (KDE Plasma / Wayland).
#
#   ./build.sh               compila en modo Release (build/kaint)
#   ./build.sh --run         compila y ejecuta
#   ./build.sh --install     compila e instala en ~/.local (aparece en el menú de KDE)
#   ./build.sh --help        todas las opciones
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

BUILD_TYPE=Release
BUILD_DIR=build
PREFIX="$HOME/.local"
DO_CLEAN=0
DO_INSTALL=0
DO_RUN=0
DO_TEST=0

# Paquetes de Arch necesarios para compilar y ejecutar.
REQUIRED=(gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons)
# Recomendados: más formatos (WebP, TIFF, AVIF, JPEG XL, HEIF, PSD...), diálogos de Qt en español
# y mono + libgdiplus para los complementos de Paint.NET (.dll).
OPTIONAL=(qt6-imageformats kimageformats qt6-translations mono libgdiplus)

c_bold=$'\e[1m'; c_red=$'\e[31m'; c_yellow=$'\e[33m'; c_green=$'\e[32m'; c_reset=$'\e[0m'
if [[ ! -t 1 ]]; then c_bold=""; c_red=""; c_yellow=""; c_green=""; c_reset=""; fi

usage() {
    cat <<EOF
Uso: $0 [opciones]

  -d, --debug          compila con símbolos de depuración (build-debug/)
  -c, --clean          borra la carpeta de compilación antes de empezar
  -t, --test           ejecuta las pruebas del núcleo después de compilar
  -i, --install        instala (por defecto en ~/.local, sin sudo)
      --prefix RUTA    prefijo de instalación (p. ej. /usr/local; usará sudo si hace falta)
  -r, --run            ejecuta Kaint al terminar
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
        -h|--help)    usage; exit 0 ;;
        *) echo "${c_red}Opción desconocida: $1${c_reset}" >&2; usage; exit 1 ;;
    esac
    shift
done

# --- Dependencias -----------------------------------------------------------
if command -v pacman >/dev/null 2>&1; then
    missing=()
    for pkg in "${REQUIRED[@]}"; do
        pacman -Qq "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if (( ${#missing[@]} )); then
        echo "${c_red}${c_bold}Faltan paquetes necesarios:${c_reset} ${missing[*]}" >&2
        echo "Instálalos con:" >&2
        echo "  sudo pacman -S --needed ${missing[*]}" >&2
        exit 1
    fi
    optional_missing=()
    for pkg in "${OPTIONAL[@]}"; do
        pacman -Qq "$pkg" >/dev/null 2>&1 || optional_missing+=("$pkg")
    done
    if (( ${#optional_missing[@]} )); then
        echo "${c_yellow}Recomendado (opcional):${c_reset} sudo pacman -S --needed ${optional_missing[*]}"
        if [[ " ${optional_missing[*]} " == *" mono "* ]]; then
            echo "  Sin mono no se compila la capa de compatibilidad con los complementos de Paint.NET."
        elif [[ " ${optional_missing[*]} " == *" libgdiplus "* ]]; then
            echo "  Sin libgdiplus muchos complementos de Paint.NET no pueden cargarse (usan System.Drawing)."
        fi
    fi
else
    echo "${c_yellow}No se encontró pacman; se omite la comprobación de dependencias.${c_reset}"
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

if (( DO_INSTALL )); then
    echo "${c_bold}Instalando en $PREFIX...${c_reset}"
    mkdir -p "$PREFIX" 2>/dev/null || true
    if [[ -w "$PREFIX" ]]; then
        cmake --install "$BUILD_DIR"
    else
        sudo cmake --install "$BUILD_DIR"
    fi
    # Refresca el menú de aplicaciones de KDE y la caché de iconos.
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
    echo "${c_green}Instalado:${c_reset} $PREFIX/bin/kaint (también en el menú de aplicaciones)"
fi

echo "${c_green}${c_bold}Listo:${c_reset} $BUILD_DIR/kaint"

if (( DO_RUN )); then
    exec "$BUILD_DIR/kaint"
fi
