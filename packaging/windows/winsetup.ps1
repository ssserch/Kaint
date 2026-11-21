# Lee winsetup.json (la información del instalador de Windows) y escribe lo que kaint.nsi necesita
# de él: un archivo de NSIS con sus !define y sus textos. Lo llama "build.bat --setup".
#
#   powershell -File winsetup.ps1 -Root <carpeta del código> -Version 1.2.0 -Arch x64
#              -OutDir <carpeta donde va el instalador> -Nsh <winsetup.nsh> -NameFile <archivo>
#
# Todo en winsetup.json es opcional: lo que falte toma el valor de siempre (los de abajo). En los
# textos, {name}, {version} y {arch} se sustituyen por el nombre, la versión y la arquitectura. Deja
# en -NameFile la ruta del instalador que se va a crear.
# (Este archivo va en UTF-8 con BOM: Windows PowerShell 5.1 lo necesita para leer los acentos.)
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Arch = "x64",
    [Parameter(Mandatory = $true)][string]$OutDir,
    [Parameter(Mandatory = $true)][string]$Nsh,
    [Parameter(Mandatory = $true)][string]$NameFile,
    [string]$Config = ""
)
$ErrorActionPreference = "Stop"

function Fail([string]$message) {
    [Console]::Error.WriteLine("winsetup.json: $message")
    exit 1
}

# --- Los valores de siempre ------------------------------------------------------------------
$defaults = [ordered]@{
    name              = "Kaint"
    version           = ""
    publisher         = "SSSerch"
    website           = "https://github.com/ssserch/Kaint"
    supportUrl        = "https://github.com/ssserch/Kaint/issues"
    copyright         = "GPL-3.0"
    fileDescription   = "{name} Setup"
    outputFile        = "{name}-{version}-windows-{arch}-setup.exe"
    brandingText      = "{name} {version}"
    icon              = "data/kaint.ico"
    uninstallerIcon   = "data/kaint.ico"
    license           = "LICENSE"
    installFolder     = '$LOCALAPPDATA\Programs\{name}'
    executable        = "bin\kaint.exe"
    registryKey       = "Kaint"
    compression       = "lzma"
    solidCompression  = $true
    runAfterInstall   = $true
    startMenuShortcut = $true
    desktopShortcut   = "optional"
}
$defaultPages = [ordered]@{ welcome = $true; license = $true; components = $true; folder = $true; finish = $true }
$defaultUserData = [ordered]@{
    registryKeys   = @("Software\Kaint")
    folders        = @('$APPDATA\Kaint', '$LOCALAPPDATA\Kaint\Kaint', '$LOCALAPPDATA\Kaint\Effects', '$LOCALAPPDATA\Kaint\Shapes')
    files          = @('$LOCALAPPDATA\Kaint\discord.json')
    foldersIfEmpty = @('$LOCALAPPDATA\Kaint')
}
$defaultTexts = [ordered]@{
    English = [ordered]@{
        appComponent           = "{name} (required)"
        desktopComponent       = "Desktop shortcut"
        uninstallAppComponent  = "{name}"
        uninstallDataComponent = "Also delete my settings, palettes, plugins, icon packs, fonts and recovery copies"
        running                = "{name} is running. Close it and click OK to continue."
    }
    Spanish = [ordered]@{
        appComponent           = "{name} (necesario)"
        desktopComponent       = "Acceso directo en el escritorio"
        uninstallAppComponent  = "{name}"
        uninstallDataComponent = "Borrar también mi configuración, paletas, complementos, paquetes de iconos, fuentes y copias de recuperación"
        running                = "{name} está abierto. Ciérralo y pulsa Aceptar para continuar."
    }
}

# --- winsetup.json ---------------------------------------------------------------------------
if (-not $Config) { $Config = Join-Path $Root "winsetup.json" }
$json = $null
if (Test-Path -LiteralPath $Config) {
    try {
        $json = Get-Content -LiteralPath $Config -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Fail "no es un JSON válido ($($_.Exception.Message))"
    }
} else {
    [Console]::Error.WriteLine("No está ${Config}: el instalador lleva la información de siempre.")
}

function Has($object, [string]$key) {
    return $null -ne $object -and $null -ne $object.PSObject.Properties[$key] -and $null -ne $object.$key
}
function Value([string]$key) {
    if (Has $json $key) { return $json.$key }
    return $defaults[$key]
}
function Flag($object, [string]$key, [bool]$fallback) {
    if (-not (Has $object $key)) { return $fallback }
    $v = $object.$key
    if ($v -is [bool]) { return $v }
    Fail "«${key}» tiene que ser true o false"
}
function List($object, [string]$key, $fallback) {
    if (-not (Has $object $key)) { return @($fallback) }
    return @($object.$key | ForEach-Object { [string]$_ } | Where-Object { $_ -ne "" })
}

$name = [string](Value "name")
if (-not $name.Trim()) { Fail "«name» está vacío" }
$appVersion = [string](Value "version")
if (-not $appVersion.Trim()) { $appVersion = $Version }
# Windows quiere cuatro números en la versión del archivo: 1.2 -> 1.2.0.0 (lo que no sea número, fuera).
$numbers = @([regex]::Matches($appVersion, "\d+") | ForEach-Object { $_.Value } | Select-Object -First 4)
if ($numbers.Count -eq 0) { Fail "«version» ($appVersion) no tiene ningún número" }
while ($numbers.Count -lt 4) { $numbers += "0" }
$fileVersion = $numbers -join "."

function Fill([string]$text) {
    return $text.Replace("{name}", $name).Replace("{version}", $appVersion).Replace("{arch}", $Arch)
}
# Un texto dentro de comillas dobles de NSIS: $ y comillas escapados, saltos de línea como $\n.
function Text([string]$text) {
    $t = (Fill $text).Replace('$', '$$').Replace('"', '$\"')
    return ($t -replace "\r?\n", '$\n')
}
# Una ruta de instalación: puede llevar variables de NSIS ($LOCALAPPDATA, $PROGRAMFILES64...).
function NsisPath([string]$text) {
    return (Fill $text).Replace('"', '').Replace('/', '\')
}
# Un archivo del código (icono, licencia): relativo a la carpeta del código o con su ruta entera.
function SourceFile([string]$key) {
    $value = Fill ([string](Value $key))
    if (-not $value.Trim()) { return "" }
    $path = $value
    if (-not [System.IO.Path]::IsPathRooted($path)) { $path = Join-Path $Root $path }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Fail "«${key}»: no existe el archivo ${path}" }
    return (Resolve-Path -LiteralPath $path).Path
}

$output = Fill ([string](Value "outputFile"))
if ($output -notmatch "\.exe$") { $output += ".exe" }
if ($output.IndexOfAny([System.IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    Fail "«outputFile» ($output) es solo el nombre del instalador, sin carpetas ni caracteres que Windows no admite"
}
$outPath = Join-Path $OutDir $output

$compression = ([string](Value "compression")).ToLowerInvariant()
if (@("lzma", "zlib", "bzip2") -notcontains $compression) { Fail "«compression» tiene que ser lzma, zlib o bzip2" }
$desktop = ([string](Value "desktopShortcut")).ToLowerInvariant()
# (true y false también valen: la casilla sin marcar, o sin acceso directo.)
if ($desktop -eq "true") { $desktop = "optional" }
if ($desktop -eq "false") { $desktop = "never" }
if (@("optional", "checked", "always", "never") -notcontains $desktop) {
    Fail "«desktopShortcut» tiene que ser optional (casilla sin marcar), checked (marcada), always o never"
}
$registryKey = (Fill ([string](Value "registryKey"))) -replace '[\\/"$]', ''
if (-not $registryKey.Trim()) { Fail "«registryKey» está vacío" }
$executable = (Fill ([string](Value "executable"))).Replace('/', '\').TrimStart('\')
if (-not $executable.Trim()) { Fail "«executable» está vacío" }

$pages = if (Has $json "pages") { $json.pages } else { $null }
$userData = if (Has $json "userData") { $json.userData } else { $null }
$icon = SourceFile "icon"
$uninstallerIcon = SourceFile "uninstallerIcon"
$license = SourceFile "license"

# --- El archivo de NSIS ----------------------------------------------------------------------
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("; Generado por packaging\windows\winsetup.ps1 a partir de winsetup.json: no se edita a mano.")
function Set-Define([string]$key, [string]$value) { $script:lines.Add("!define $key `"$value`"") }
function Set-DefineIf([string]$key, [bool]$on) { if ($on) { $script:lines.Add("!define $key") } }

Set-Define "APP" (Text $name)
Set-Define "APP_VERSION" (Text $appVersion)
Set-Define "FILE_VERSION" $fileVersion
Set-Define "PUBLISHER" (Text ([string](Value "publisher")))
Set-Define "WEBSITE" (Text ([string](Value "website")))
Set-Define "SUPPORT_URL" (Text ([string](Value "supportUrl")))
Set-Define "COPYRIGHT" (Text ([string](Value "copyright")))
Set-Define "FILE_DESCRIPTION" (Text ([string](Value "fileDescription")))
Set-Define "BRANDING" (Text ([string](Value "brandingText")))
Set-Define "OUTFILE" $outPath
Set-Define "INSTALL_FOLDER" (NsisPath ([string](Value "installFolder")))
Set-Define "APP_EXE" $executable
Set-Define "UNINSTALL_KEY" "Software\Microsoft\Windows\CurrentVersion\Uninstall\$registryKey"
Set-Define "COMPRESSION" $compression
Set-DefineIf "SOLID_COMPRESSION" (Flag $json "solidCompression" $defaults.solidCompression)
if ($icon) { Set-Define "APP_ICON" $icon }
if ($uninstallerIcon) { Set-Define "UNINSTALLER_ICON" $uninstallerIcon }
if ($license) { Set-Define "LICENSE_FILE" $license }
Set-DefineIf "PAGE_WELCOME" (Flag $pages "welcome" $defaultPages.welcome)
Set-DefineIf "PAGE_LICENSE" ((Flag $pages "license" $defaultPages.license) -and $license)
Set-DefineIf "PAGE_COMPONENTS" (Flag $pages "components" $defaultPages.components)
Set-DefineIf "PAGE_FOLDER" (Flag $pages "folder" $defaultPages.folder)
Set-DefineIf "PAGE_FINISH" (Flag $pages "finish" $defaultPages.finish)
Set-DefineIf "RUN_AFTER_INSTALL" (Flag $json "runAfterInstall" $defaults.runAfterInstall)
Set-DefineIf "START_MENU_SHORTCUT" (Flag $json "startMenuShortcut" $defaults.startMenuShortcut)
Set-Define "DESKTOP_SHORTCUT" $desktop

# Lo que borra «Borrar también mi configuración...» al desinstalar.
$lines.Add("!macro KAINT_DELETE_USER_DATA")
foreach ($key in (List $userData "registryKeys" $defaultUserData.registryKeys)) {
    $lines.Add("    DeleteRegKey HKCU `"$(NsisPath $key)`"")
}
foreach ($folder in (List $userData "folders" $defaultUserData.folders)) {
    $lines.Add("    RMDir /r `"$(NsisPath $folder)`"")
}
foreach ($file in (List $userData "files" $defaultUserData.files)) {
    $lines.Add("    Delete `"$(NsisPath $file)`"")
}
foreach ($folder in (List $userData "foldersIfEmpty" $defaultUserData.foldersIfEmpty)) {
    $lines.Add("    RMDir `"$(NsisPath $folder)`"")
}
$lines.Add("!macroend")

# Idiomas (los nombres de NSIS: English, Spanish, French, German, Italian, PortugueseBR...): el
# primero es el del instalador cuando Windows está en uno que no se incluye.
$languages = [ordered]@{}
if (Has $json "languages") {
    foreach ($property in $json.languages.PSObject.Properties) { $languages[$property.Name] = $property.Value }
}
if ($languages.Count -eq 0) {
    foreach ($language in $defaultTexts.Keys) { $languages[$language] = $null }
}
$lines.Add("!macro KAINT_LANGUAGES")
foreach ($language in $languages.Keys) {
    if ($language -notmatch "^[A-Za-z]+$") { Fail "«languages»: $language no es un idioma de NSIS (English, Spanish, French...)" }
    $lines.Add("    !insertmacro MUI_LANGUAGE `"$language`"")
}
$lines.Add("!macroend")
$strings = [ordered]@{
    appComponent = "SecApp"; desktopComponent = "SecDesktop"; uninstallAppComponent = "SecUnApp"
    uninstallDataComponent = "SecUnData"; running = "Running"
}
$lines.Add("!macro KAINT_STRINGS")
foreach ($language in $languages.Keys) {
    # Lo que no se traduzca sale con el texto de siempre de ese idioma o, si no lo hay, en inglés.
    $fallback = if ($defaultTexts.Contains($language)) { $defaultTexts[$language] } else { $defaultTexts.English }
    foreach ($key in $strings.Keys) {
        $text = if (Has $languages[$language] $key) { [string]$languages[$language].$key } else { $fallback[$key] }
        $lines.Add("    LangString $($strings[$key]) `${LANG_$($language.ToUpperInvariant())} `"$(Text $text)`"")
    }
}
$lines.Add("!macroend")

# UTF-8 con BOM: así NSIS lee bien los acentos.
$encoding = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllLines($Nsh, $lines, $encoding)
[System.IO.File]::WriteAllText($NameFile, $outPath, (New-Object System.Text.UTF8Encoding($false)))
