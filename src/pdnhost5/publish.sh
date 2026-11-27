#!/usr/bin/env sh
# Kaint: publica el proceso auxiliar de .NET (kaint-pdn5-host) en una carpeta.
#
#   publish.sh <carpeta> [rid]
#
# Sin rid: dependiente del .NET instalado (dotnet PdnBridge.dll). Con rid (linux-x64, win-x64...):
# para ese sistema, todavía dependiente del .NET instalado allí. Basado en publish.sh de paint.rs
# (third_party/pdn5/bridge), sin OpenD2D (Kaint lo compila aparte) ni avisos de licencias.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
pdn="$here/../../third_party/pdn5"
out="${1:?uso: publish.sh <carpeta> [rid]}"
rid="${2:-}"
dotnet="${DOTNET:-dotnet}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# MSBUILDTERMINALLOGGER=off: con el registro «de terminal» (el de serie desde .NET 9), -clp:ErrorsOnly
# no se respeta y salen cientos de avisos de C# de third_party/pdn5.
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 DOTNET_NOLOGO=1 DOTNET_CLI_TELEMETRY_OPTOUT=1 \
       MSBUILDDISABLENODEREUSE=1 DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER=1 MSBUILDTERMINALLOGGER=off

nuget_dir="${NUGET_PACKAGES:-$HOME/.nuget/packages}"
if [ ! -d "$nuget_dir/system.drawing.common" ]; then
    echo "La primera vez se descargan los paquetes de .NET desde nuget.org: puede tardar unos minutos sin mostrar nada."
fi

# -m:1: con varios nodos, la restauración en paralelo falla sin decir por qué en algunos SDK de Arch.
common="-c Release -m:1 -nodeReuse:false -p:UseSharedCompilation=false -v:q -clp:ErrorsOnly"
if [ -n "$rid" ]; then
    "$dotnet" publish "$here/KaintPdnHost.csproj" $common -o "$out" -r "$rid" --self-contained false
else
    "$dotnet" publish "$here/KaintPdnHost.csproj" $common -o "$out"
fi

# El registro que ven los complementos (un archivo por complemento, nunca el del sistema).
"$dotnet" build "$pdn/assemblies/Win32Registry/Win32Registry.csproj" $common -o "$out/plugin-shims" >/dev/null

case "$rid" in win-*) exit 0 ;; esac
case "$(uname -s)" in Linux|Darwin|FreeBSD) ;; *) exit 0 ;; esac

# GDI+ fuera de Windows: System.Drawing.Common 6.0 para Unix (el último que usa libgdiplus).
nuget="${NUGET_PACKAGES:-$HOME/.nuget/packages}"
sdc="$nuget/system.drawing.common/6.0.0/runtimes/unix/lib/net6.0/System.Drawing.Common.dll"
if [ -f "$sdc" ]; then
    cp "$sdc" "$out/System.Drawing.Common.dll"
    chmod u+w "$out/System.Drawing.Common.dll"
    # Con la versión que piden los ensamblados (la del paquete de .NET 10).
    "$dotnet" "$out/PdnBridge.dll" stamp-version "$out/System.Drawing.Common.dll" 10.0.0.0
else
    echo "aviso: falta $sdc; los complementos que dibujan con System.Drawing no funcionarán" >&2
fi
# SystemEvents que no falla fuera de Windows, y los tipos de WPF que usan algunos complementos.
"$dotnet" build "$pdn/assemblies/Win32SystemEvents/Win32SystemEvents.csproj" $common -o "$out" >/dev/null
"$dotnet" build "$pdn/assemblies/PresentationCore/PresentationCore.csproj" $common -o "$out/plugin-shims" >/dev/null
"$dotnet" build "$pdn/assemblies/PresentationFramework/PresentationFramework.csproj" $common -o "$out/plugin-shims" >/dev/null
# Solo los sustitutos que .NET no tiene: una segunda copia de PaintDotNet.* o de WinForms rompería los tipos.
rm -f "$out/plugin-shims"/PaintDotNet.* "$out/plugin-shims"/System.Windows.Forms*
# user32, uxtheme y kernel32 de mentira (GetSystemMetrics y compañía) para los diálogos.
case "$(uname -s)" in
    Darwin) cc -dynamiclib -O2 -o "$out/libuser32.dylib" "$pdn/bridge/native/user32.c" ;;
    *) cc -shared -fPIC -O2 -o "$out/libuser32.so" "$pdn/bridge/native/user32.c" ;;
esac
echo "kaint-pdn5-host -> $out"
