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

# --- Paquetes (necesarios y recomendados) -----------------------------------
ARCH_REQUIRED=(gcc cmake ninja qt6-base qt6-tools qt6-wayland qt6-svg zlib breeze-icons)
ARCH_OPTIONAL=(qt6-imageformats kimageformats qt6-translations mono libgdiplus)
DEB_REQUIRED=(build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools qt6-tools-dev
              qt6-tools-dev-tools qt6-l10n-tools qt6-svg-plugins qt6-wayland zlib1g-dev libgl-dev
              breeze-icon-theme)
DEB_OPTIONAL=(qt6-image-formats-plugins kimageformat6-plugins qt6-translations-l10n mono-devel libgdiplus)

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
    fi
}
pacman_installed() { pacman -Qq "$1" >/dev/null 2>&1; }
dpkg_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }

if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    echo "${c_yellow}CMAKE_PREFIX_PATH definido (Qt propio): se omite la comprobación de paquetes.${c_reset}"
elif command -v pacman >/dev/null 2>&1; then
    check_packages pacman_installed "sudo pacman -S --needed" ARCH_REQUIRED ARCH_OPTIONAL
elif command -v dpkg-query >/dev/null 2>&1; then
    check_packages dpkg_installed "sudo apt install" DEB_REQUIRED DEB_OPTIONAL
else
    echo "${c_yellow}No se encontró pacman ni apt; se omite la comprobación de dependencias.${c_reset}"
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
