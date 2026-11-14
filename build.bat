@echo off
rem Compila e instala Kaint en Windows 10 y 11 (opciones: build.bat --help).
setlocal EnableExtensions EnableDelayedExpansion
rem (chcp con <nul: si no, se come la entrada de quien responde al menú desde un archivo o una tubería.)
for /f "tokens=2 delims=:." %%C in ('chcp ^<nul') do set "KAINT_OLD_CP=%%C"
chcp 65001 >nul <nul
set "SCRIPT=%~f0"
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

rem La versión (version.json): se lee una vez y sin arrancar PowerShell (el menú sale al momento).
set "KAINT_VERSION="
if exist version.json for /f "usebackq tokens=1,2 delims=:, " %%A in (`findstr /i /c:"version" version.json 2^>nul`) do if not defined KAINT_VERSION set "KAINT_VERSION=%%~B"
if not defined KAINT_VERSION set "KAINT_VERSION=0.0.0"

rem --- Sin opciones (también con doble clic): el menú; al final espera ---------
set "INTERACTIVE=0"
if "%~1"=="" echo %CMDCMDLINE% | find /i "%~nx0" >nul && set "INTERACTIVE=1"
for /F %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
call :now START_TIME
if "%~1"=="" (
    call :menu
    if errorlevel 1 (
        set "RC=0"
        goto finish
    )
    call :now START_TIME
    call :main !MENU_ARGS!
) else (
    call :main %*
)
set "RC=%ERRORLEVEL%"
if "%SHOW_TIME%"=="1" call :elapsed
if not "%RC%"=="0" if defined STEP echo %ESC%[31mFalló: %STEP%.%ESC%[0m
:finish
if "%INTERACTIVE%"=="1" pause
if defined KAINT_OLD_CP chcp %KAINT_OLD_CP% >nul <nul
exit /b %RC%

:main

set "QT_VERSION=6.10.3"
set "MINGW_TOOL=mingw1310"
set "AQT_URL=https://github.com/miurahr/aqtinstall/releases/download/v3.3.0/aqt_x64.exe"
set "NSIS_VERSION=3.11"
set "NSIS_URL=https://downloads.sourceforge.net/project/nsis/NSIS%%203/%NSIS_VERSION%/nsis-%NSIS_VERSION%.zip"
set "NSIS_SHA256=c7d27f780ddb6cffb4730138cd1591e841f4b7edb155856901cdf5f214394fa1"
set "TOOLCHAIN=%LOCALAPPDATA%\Kaint\toolchain"
set "PREFIX=%LOCALAPPDATA%\Programs\Kaint"
set "BUILD_TYPE=Release"
set "BUILD_DIR=build-windows"
set "DO_CLEAN=0"
set "DO_TEST=0"
set "DO_INSTALL=0"
set "DO_RUN=0"
set "DO_UNINSTALL=0"
set "DO_ZIP=0"
set "DO_SETUP=0"
set "DO_AVX2=0"
set "DO_ARM64=0"
set "DO_WEB=0"
set "DO_APK="
set "USE_CCACHE=1"
set "WSL_ARGS="
set "EXTRA_CMAKE="
set "ARCH_NAME=x64"
set "ASSUME_YES=0"
set "JOBS="
set "WINSETUP=%CD%\winsetup.json"
set "APK_HTML="
set "APK_KEYSTORE="
set "APK_ALIAS="
set "STEP="
set "SHOW_TIME=0"

rem --- Opciones ---------------------------------------------------------------
:args
if "%~1"=="" goto args_done
if /i "%~1"=="-d"          (set "BUILD_TYPE=Debug" & set "BUILD_DIR=build-windows-debug" & shift & goto args)
if /i "%~1"=="--debug"     (set "BUILD_TYPE=Debug" & set "BUILD_DIR=build-windows-debug" & shift & goto args)
if /i "%~1"=="-c"          (set "DO_CLEAN=1" & shift & goto args)
if /i "%~1"=="--clean"     (set "DO_CLEAN=1" & shift & goto args)
if /i "%~1"=="-t"          (set "DO_TEST=1" & shift & goto args)
if /i "%~1"=="--test"      (set "DO_TEST=1" & shift & goto args)
if /i "%~1"=="-i"          (set "DO_INSTALL=1" & shift & goto args)
if /i "%~1"=="--install"   (set "DO_INSTALL=1" & shift & goto args)
if /i "%~1"=="--uninstall" (set "DO_UNINSTALL=1" & shift & goto args)
if /i "%~1"=="--zip"       (set "DO_ZIP=1" & shift & goto args)
if /i "%~1"=="--setup"     (set "DO_SETUP=1" & shift & goto args)
if /i "%~1"=="--avx2"      (set "DO_AVX2=1" & shift & goto args)
if /i "%~1"=="--arm64"     (set "DO_ARM64=1" & shift & goto args)
if /i "%~1"=="--web"       (set "DO_WEB=1" & shift & goto args)
if /i "%~1"=="--apk"       (set "DO_APK=apk" & shift & goto args)
if /i "%~1"=="--aab"       (set "DO_APK=aab" & shift & goto args)
if /i "%~1"=="--no-ccache" (set "USE_CCACHE=0" & shift & goto args)
if /i "%~1"=="-r"          (set "DO_RUN=1" & shift & goto args)
if /i "%~1"=="--run"       (set "DO_RUN=1" & shift & goto args)
if /i "%~1"=="-y"          (set "ASSUME_YES=1" & shift & goto args)
if /i "%~1"=="--yes"       (set "ASSUME_YES=1" & shift & goto args)
if /i "%~1"=="-h"          goto usage
if /i "%~1"=="--help"      goto usage
rem Las que llevan un valor detrás.
set "OPTION=%~1"
set "OPTION_VALUE=%~2"
set "TAKES_VALUE=0"
for %%O in (--wsl --prefix --winsetup -j --jobs --kainthtml --keystore --alias) do if /i "%OPTION%"=="%%O" set "TAKES_VALUE=1"
if "%TAKES_VALUE%"=="0" (
    echo Opción desconocida: %~1
    call :print_usage
    exit /b 1
)
if not defined OPTION_VALUE (
    echo A %OPTION% le falta su valor.
    exit /b 1
)
if /i "%OPTION%"=="--wsl"      set "WSL_ARGS=%OPTION_VALUE%"
if /i "%OPTION%"=="--prefix"   set "PREFIX=%OPTION_VALUE%"
if /i "%OPTION%"=="--winsetup" set "WINSETUP=%~f2"
if /i "%OPTION%"=="--kainthtml" set "APK_HTML=%~f2"
if /i "%OPTION%"=="--keystore" set "APK_KEYSTORE=%~f2"
if /i "%OPTION%"=="--alias" set "APK_ALIAS=%OPTION_VALUE%"
if /i "%OPTION%"=="-j"         set "JOBS=%OPTION_VALUE%"
if /i "%OPTION%"=="--jobs"     set "JOBS=%OPTION_VALUE%"
shift
shift
goto args
:usage
call :print_usage
exit /b 0
:args_done

set "JOBS_NUMBER="
if defined JOBS (
    set /a "JOBS_NUMBER=JOBS" 2>nul
    if not "!JOBS_NUMBER!"=="%JOBS%" (
        echo --jobs quiere un número de hilos, no «%JOBS%».
        exit /b 1
    )
    if !JOBS_NUMBER! lss 1 (
        echo --jobs quiere un número de hilos, no «%JOBS%».
        exit /b 1
    )
)

if "%DO_UNINSTALL%"=="1" goto uninstall
set "SHOW_TIME=1"
if "%DO_WEB%"=="1" goto web
if defined DO_APK goto apk
if defined WSL_ARGS goto wsl
rem AVX2: el nivel x86-64-v3 (Haswell, 2013, y posteriores), con su propia carpeta y nombre.
if "%DO_AVX2%"=="1" (
    set "BUILD_DIR=%BUILD_DIR%-avx2"
    set "EXTRA_CMAKE=-DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3"
    set "ARCH_NAME=x64-avx2"
)
if "%DO_ARM64%"=="1" goto arm64

rem --- Herramientas -----------------------------------------------------------
set "STEP=buscar las herramientas (Qt, MinGW, CMake y Ninja)"
call :find_tools
if errorlevel 1 (
    echo.
    echo Faltan herramientas para compilar ^(Qt %QT_VERSION% para MinGW, MinGW, CMake o Ninja^).
    echo Se pueden descargar ahora en %TOOLCHAIN% ^(unos 1,5 GB^).
    if not "%ASSUME_YES%"=="1" (
        choice /c SN /m "¿Descargarlas"
        if errorlevel 2 exit /b 1
    )
    call :download_tools || exit /b 1
    call :find_tools
    if errorlevel 1 (
        echo No se encontraron las herramientas después de descargarlas.
        exit /b 1
    )
)
echo Qt:     %QT_DIR%
echo MinGW:  %MINGW_BIN%
set "PATH=%MINGW_BIN%;%CMAKE_BIN%;%NINJA_DIR%;%QT_DIR%\bin;%PATH%"
call :find_ccache

rem --- Compilación ------------------------------------------------------------
if "%DO_CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo Borrando %BUILD_DIR%\
    rmdir /s /q "%BUILD_DIR%"
)
set "STEP=configurar con CMake"
echo Configurando (%BUILD_TYPE%)...
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_INSTALL_PREFIX="%PREFIX%" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ %EXTRA_CMAKE% %CCACHE_CMAKE% || exit /b 1
set "STEP=compilar"
if defined JOBS (echo Compilando con %JOBS% hilos...) else (echo Compilando con %NUMBER_OF_PROCESSORS% hilos...)
cmake --build "%BUILD_DIR%" --parallel %JOBS% || exit /b 1

if "%DO_TEST%"=="1" (
    set "STEP=las pruebas"
    echo Ejecutando pruebas...
    set "QT_QPA_PLATFORM=offscreen"
    ctest --test-dir "%BUILD_DIR%" --output-on-failure || exit /b 1
    set "QT_QPA_PLATFORM="
)

if "%DO_INSTALL%"=="1" call :install || exit /b 1
if "%DO_ZIP%"=="1" call :zip || exit /b 1
if "%DO_SETUP%"=="1" call :setup || exit /b 1
set "STEP="

echo %ESC%[32mListo:%ESC%[0m %BUILD_DIR%\kaint.exe
if "%DO_RUN%"=="1" (
    if "%DO_INSTALL%"=="1" (start "" "%PREFIX%\bin\kaint.exe") else (start "" "%BUILD_DIR%\kaint.exe")
)
exit /b 0

rem --- La hora, en centésimas de segundo (%1: la variable donde dejarla) --------
:now
for /f "tokens=1-4 delims=:., " %%a in ("%TIME%") do set /a "%~1=(100%%a %% 100)*360000 + (100%%b %% 100)*6000 + (100%%c %% 100)*100 + (100%%d %% 100)"
exit /b 0

rem --- Lo que ha tardado todo ----------------------------------------------------
:elapsed
call :now END_TIME
set /a "TOOK=(END_TIME - START_TIME) / 100"
if %TOOK% lss 0 set /a "TOOK+=86400"
set /a "TOOK_MIN=TOOK / 60, TOOK_SEC=TOOK %% 60"
if %TOOK_MIN% gtr 0 (echo Tiempo: %TOOK_MIN% min %TOOK_SEC% s) else (echo Tiempo: %TOOK_SEC% s)
exit /b 0

rem --- Un archivo creado, con su tamaño (%1: qué es; %2: el archivo; %3: una nota) --
:report
set "REPORT_SIZE="
for %%F in ("%~2") do if exist "%%~fF" set /a "REPORT_SIZE=%%~zF / 1048576, REPORT_REST=(%%~zF %% 1048576) * 10 / 1048576" 2>nul
rem (Sin bloques entre paréntesis: la nota y la ruta pueden llevarlos.)
if not defined REPORT_SIZE goto report_plain
echo %ESC%[32m%~1:%ESC%[0m %~2 (%REPORT_SIZE%,%REPORT_REST% MB) %~3
exit /b 0
:report_plain
echo %ESC%[32m%~1:%ESC%[0m %~2 %~3
exit /b 0

rem --- ccache o sccache, si están: recompilar tarda mucho menos -------------------
:find_ccache
set "CCACHE_CMAKE="
if "%USE_CCACHE%"=="0" exit /b 0
set "CCACHE="
for %%C in (ccache sccache) do if not defined CCACHE for /f "delims=" %%G in ('where %%C 2^>nul') do if not defined CCACHE set "CCACHE=%%~fG"
if not defined CCACHE exit /b 0
rem (Con barras normales: CMake toma las invertidas por secuencias de escape.)
set "CCACHE=%CCACHE:\=/%"
echo Caché:  %CCACHE%
set CCACHE_CMAKE=-DCMAKE_C_COMPILER_LAUNCHER="%CCACHE%" -DCMAKE_CXX_COMPILER_LAUNCHER="%CCACHE%"
exit /b 0

rem --- Instalar ---------------------------------------------------------------
:install
set "STEP=instalar en %PREFIX%"
echo Instalando en %PREFIX%...
cmake --install "%BUILD_DIR%" || exit /b 1
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%MINGW_BIN%\%%D" copy /y "%MINGW_BIN%\%%D" "%PREFIX%\bin\" >nul
)
copy /y "%SCRIPT%" "%PREFIX%\build.bat" >nul
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Programs') + '\Kaint.lnk');" ^
  "$s.TargetPath = '%PREFIX%\bin\kaint.exe'; $s.WorkingDirectory = '%PREFIX%\bin';" ^
  "$s.IconLocation = '%PREFIX%\bin\kaint.exe,0'; $s.Description = 'Kaint'; $s.Save()" || exit /b 1
set "UNINSTALL_KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kaint"
reg add "%UNINSTALL_KEY%" /v DisplayName /d "Kaint" /f >nul
reg add "%UNINSTALL_KEY%" /v DisplayVersion /d "%KAINT_VERSION%" /f >nul
reg add "%UNINSTALL_KEY%" /v Publisher /d "SSSerch" /f >nul
reg add "%UNINSTALL_KEY%" /v DisplayIcon /d "%PREFIX%\bin\kaint.exe" /f >nul
reg add "%UNINSTALL_KEY%" /v InstallLocation /d "%PREFIX%" /f >nul
rem El desinstalador (Uninstall.exe) es el que abre "Aplicaciones instaladas" o el Panel de control
rem (sin bloques entre paréntesis: la carpeta puede llevarlos, como "Program Files (x86)").
set "KAINT_UNINSTALL=\"%PREFIX%\build.bat\" --uninstall --prefix \"%PREFIX%\""
if exist "%PREFIX%\Uninstall.exe" set "KAINT_UNINSTALL=\"%PREFIX%\Uninstall.exe\""
reg add "%UNINSTALL_KEY%" /v UninstallString /d "%KAINT_UNINSTALL%" /f >nul
if exist "%PREFIX%\Uninstall.exe" reg add "%UNINSTALL_KEY%" /v QuietUninstallString /d "\"%PREFIX%\Uninstall.exe\" /S" /f >nul
if not exist "%PREFIX%\Uninstall.exe" reg delete "%UNINSTALL_KEY%" /v QuietUninstallString /f >nul 2>&1
reg add "%UNINSTALL_KEY%" /v URLInfoAbout /d "https://github.com/ssserch/Kaint" /f >nul
reg add "%UNINSTALL_KEY%" /v NoModify /t REG_DWORD /d 1 /f >nul
reg add "%UNINSTALL_KEY%" /v NoRepair /t REG_DWORD /d 1 /f >nul
echo %ESC%[32mInstalado:%ESC%[0m %PREFIX%\bin\kaint.exe (también en el menú Inicio; se desinstala con %PREFIX%\Uninstall.exe o desde "Aplicaciones instaladas")
exit /b 0

rem --- Menú (sin opciones) ----------------------------------------------------
rem Como buildexample.txt: el logo, el sistema y la versión, y qué hacer. Con «Compilar» o
rem «Depurar», otro menú con los formatos; los que Windows no crea bien salen en rojo con
rem «(no recomendado)». Deja las opciones elegidas en MENU_ARGS (errorlevel 1: salir). Una
rem respuesta que no vale vuelve a preguntar.
:menu
set "MENU_ARGS="
set "MENU_DEBUG="
set "MENU_TRIES=0"
cls
echo ▄▄▄   ▄▄▄
echo ███ ▄███▀       ▀▀         ██
echo ███████    ▀▀█▄ ██  ████▄ ▀██▀▀
echo ███▀███▄  ▄█▀██ ██  ██ ██  ██
echo ███  ▀███ ▀█▄██ ██▄ ██ ██  ██
echo.
echo Windows - Kaint %KAINT_VERSION%
echo.
echo (1) Compilar
echo (2) Depurar
echo (3) Salir
echo.
:menu_action
set "MENU_CHOICE="
set /p "MENU_CHOICE=> "
if "%MENU_CHOICE%"=="3" exit /b 1
if "%MENU_CHOICE%"=="2" set "MENU_DEBUG= --debug"
rem Sin nadie que responda, no se pregunta sin fin: a los cinco intentos, sale.
if not "%MENU_CHOICE%"=="1" if not "%MENU_CHOICE%"=="2" (
    set /a "MENU_TRIES+=1"
    if !MENU_TRIES! geq 5 exit /b 1
    echo Elige 1, 2 o 3.
    goto menu_action
)
set "NR=%ESC%[31m(no recomendado)%ESC%[0m"
set "ARM_NR= %NR%"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARM_NR="
rem Lo de Linux y Android se compila dentro de WSL: sin él, no se puede.
set "WSL_NR= %NR%"
echo.
echo (1) Instalar en este equipo
echo (2) Portable x86_64 (.zip)
echo (3) Portable x86_64 AVX2 (.zip)
echo (4) Portable arm64 (.zip)%ARM_NR%
echo (5) Instalador x86_64 (setup.exe, con los datos de winsetup.json)
echo (6) Versión web (Kaint.HTML, .zip)
echo (7) Versión móvil (Android, .apk)
echo (8) Versión móvil para Google Play (.aab)
echo (9) Paquete .deb (Debian/Ubuntu, con WSL)%WSL_NR%
echo (10) Paquete de Arch (.pkg.tar.zst, con WSL)%WSL_NR%
echo (11) AppImage x86_64 (con WSL)%WSL_NR%
echo (12) AppImage x86_64 AVX2 (con WSL)%WSL_NR%
echo (13) tar.gz Linux x86_64 (con WSL)%WSL_NR%
echo (14) tar.gz Linux musl (con WSL)%WSL_NR%
echo (15) tar.gz Linux aarch64 (con WSL)%WSL_NR%
echo (16) tar.gz FreeBSD x86_64 %NR%
echo (17) tar.gz macOS arm64 %NR%
echo (0) Volver
echo.
:menu_format
set "MENU_CHOICE="
set /p "MENU_CHOICE=> "
if "%MENU_CHOICE%"=="0" goto menu
if "%MENU_CHOICE%"=="1" set "MENU_ARGS=--install"
if "%MENU_CHOICE%"=="2" set "MENU_ARGS=--zip"
if "%MENU_CHOICE%"=="3" set "MENU_ARGS=--zip --avx2"
if "%MENU_CHOICE%"=="4" set "MENU_ARGS=--zip --arm64"
if "%MENU_CHOICE%"=="5" set "MENU_ARGS=--setup"
if "%MENU_CHOICE%"=="6" set "MENU_ARGS=--web"
if "%MENU_CHOICE%"=="7" set "MENU_ARGS=--apk"
if "%MENU_CHOICE%"=="8" set "MENU_ARGS=--aab"
if "%MENU_CHOICE%"=="9" set "MENU_ARGS=--wsl deb"
if "%MENU_CHOICE%"=="10" set "MENU_ARGS=--wsl pkg"
if "%MENU_CHOICE%"=="11" set "MENU_ARGS=--wsl appimage"
if "%MENU_CHOICE%"=="12" set "MENU_ARGS=--wsl appimage-avx2"
if "%MENU_CHOICE%"=="13" set "MENU_ARGS=--wsl tar"
if "%MENU_CHOICE%"=="14" set "MENU_ARGS=--wsl tar-musl"
if "%MENU_CHOICE%"=="15" set "MENU_ARGS=--wsl tar-arm64"
if "%MENU_CHOICE%"=="16" (
    echo El .tar.gz de FreeBSD se crea en FreeBSD con: ./build.sh --tar
    goto menu_format
)
if "%MENU_CHOICE%"=="17" (
    echo El .tar.gz de macOS se crea en un Mac con: ./build.sh --tar
    goto menu_format
)
if not defined MENU_ARGS (
    set /a "MENU_TRIES+=1"
    if !MENU_TRIES! geq 5 exit /b 1
    echo Elige un número del 0 al 17.
    goto menu_format
)
rem La versión móvil y la web no se compilan: no tienen variante de depuración.
if "%MENU_ARGS%"=="--apk" set "MENU_DEBUG="
if "%MENU_ARGS%"=="--aab" set "MENU_DEBUG="
if "%MENU_ARGS%"=="--web" set "MENU_DEBUG="
set "MENU_ARGS=%MENU_ARGS%%MENU_DEBUG%"
exit /b 0

rem --- Versión web (.zip de KaintHTML) -----------------------------------------
:web
set "STEP=crear el .zip de la versión web"
if not exist build-web mkdir build-web
set "WEBZIP=%CD%\build-web\kaint-web-%KAINT_VERSION%.zip"
echo Creando %WEBZIP%...
if exist "%WEBZIP%" del /q "%WEBZIP%"
powershell -NoProfile -Command "Get-ChildItem -Path KaintHTML -Exclude tests,node_modules,package.json,package-lock.json | Compress-Archive -DestinationPath '%WEBZIP%'" || exit /b 1
set "STEP="
call :report "Versión web" "%WEBZIP%" "(sírvela con cualquier servidor web estático)"
exit /b 0

rem --- Versión móvil (Kaint.APK) ------------------------------------------------------
rem La versión web de Kaint como aplicación de Android, aquí mismo y sin WSL: lo hace el bloque de
rem PowerShell del final de este archivo (JDK, SDK de Android y Gradle de Windows; los que falten se
rem descargan, comprobados con su suma). Las opciones pasan por variables de entorno.
:apk
set "STEP=crear la versión móvil"
set "KAINT_APK_KIND=%DO_APK%"
set "KAINT_ROOT=%CD%"
set "KAINT_TOOLCHAIN=%TOOLCHAIN%"
set "KAINT_ASSUME_YES=%ASSUME_YES%"
set "KAINT_HTML=%APK_HTML%"
set "KAINT_KEYSTORE=%APK_KEYSTORE%"
set "KAINT_KEY_ALIAS=%APK_ALIAS%"
set "KAINT_SCRIPT=%SCRIPT%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s = [IO.File]::ReadAllText($env:KAINT_SCRIPT); $a = $s.IndexOf('#KAINT-MOBILE' + '-BEGIN'); $b = $s.IndexOf('#KAINT-MOBILE' + '-END'); if ($a -lt 0 -or $b -lt $a) { Write-Host 'Falta el bloque de la versión móvil en build.bat.'; exit 1 }; & ([scriptblock]::Create($s.Substring($a, $b - $a)))" || exit /b 1
set "STEP="
exit /b 0

rem --- Formatos de Linux con WSL ------------------------------------------------
:wsl
set "STEP=compilar en WSL"
where wsl >nul 2>&1 || (
    echo Para los paquetes de Linux hace falta WSL ^(wsl --install^) o un equipo con Linux: ./build.sh %WSL_ARGS%
    exit /b 1
)
rem deb, pkg, appimage, appimage-avx2, tar, tar-musl, tar-arm64 o las opciones de build.sh.
set "WSL_FLAGS=%WSL_ARGS%"
if /i "%WSL_ARGS%"=="deb" set "WSL_FLAGS=--deb"
if /i "%WSL_ARGS%"=="pkg" set "WSL_FLAGS=--pkg"
if /i "%WSL_ARGS%"=="appimage" set "WSL_FLAGS=--appimage"
if /i "%WSL_ARGS%"=="appimage-avx2" set "WSL_FLAGS=--appimage --avx2"
if /i "%WSL_ARGS%"=="tar" set "WSL_FLAGS=--tar"
if /i "%WSL_ARGS%"=="tar-musl" set "WSL_FLAGS=--tar --musl"
if /i "%WSL_ARGS%"=="tar-arm64" set "WSL_FLAGS=--tar --arm64"
if "%BUILD_TYPE%"=="Debug" set "WSL_FLAGS=%WSL_FLAGS% --debug"
if defined JOBS set "WSL_FLAGS=%WSL_FLAGS% --jobs %JOBS%"
echo Compilando en WSL: ./build.sh %WSL_FLAGS%
wsl bash ./build.sh %WSL_FLAGS% || exit /b 1
set "STEP="
exit /b 0

rem --- Windows arm64 ------------------------------------------------------------
rem Qt no publica Qt para arm64 con MinGW: hace falta Visual Studio (o Build Tools) con las
rem herramientas de ARM64 y Qt para arm64 (msvc2022_arm64) más un Qt de x64 para las
rem herramientas (QT_HOST_PATH). Se indican con KAINT_QT_ARM64 y KAINT_QT_HOST.
:arm64
set "STEP=compilar para arm64"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath`) do set "VCVARS=%%I\VC\Auxiliary\Build\vcvarsall.bat"
if not defined VCVARS goto arm64_missing
if not exist "%VCVARS%" goto arm64_missing
if not defined KAINT_QT_ARM64 goto arm64_missing
if not exist "%KAINT_QT_ARM64%\lib\cmake\Qt6\Qt6Config.cmake" goto arm64_missing
if not defined KAINT_QT_HOST goto arm64_missing
if not exist "%KAINT_QT_HOST%\lib\cmake\Qt6\Qt6Config.cmake" goto arm64_missing
call :find_tools
set "PATH=%CMAKE_BIN%;%NINJA_DIR%;%PATH%"
set "VCARCH=x64_arm64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "VCARCH=arm64"
call "%VCVARS%" %VCARCH% >nul || exit /b 1
set "BUILD_DIR=build-windows-arm64"
if "%BUILD_TYPE%"=="Debug" set "BUILD_DIR=build-windows-arm64-debug"
set "ARCH_NAME=arm64"
echo Configurando para arm64 (%BUILD_TYPE%)...
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%KAINT_QT_ARM64%" ^
      -DQT_HOST_PATH="%KAINT_QT_HOST%" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_INSTALL_PREFIX="%PREFIX%" || exit /b 1
cmake --build "%BUILD_DIR%" --parallel %JOBS% || exit /b 1
set "PACKAGE=%CD%\%BUILD_DIR%\package\Kaint"
set "ZIP=%CD%\%BUILD_DIR%\Kaint-%KAINT_VERSION%-windows-arm64.zip"
if exist "%PACKAGE%" rmdir /s /q "%PACKAGE%"
cmake --install "%BUILD_DIR%" --prefix "%PACKAGE%" || exit /b 1
if exist "%PACKAGE%\Uninstall.exe" del /q "%PACKAGE%\Uninstall.exe"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE%' -DestinationPath '%ZIP%'" || exit /b 1
set "STEP="
call :report "Paquete" "%ZIP%"
exit /b 0
:arm64_missing
echo.
echo Para compilar para Windows arm64 hacen falta:
echo   - Visual Studio 2022 ^(o Build Tools^) con "Herramientas de compilación de MSVC para ARM64".
echo   - Qt para arm64 y Qt para x64 de MSVC, por ejemplo con aqtinstall:
echo       aqt install-qt windows desktop %QT_VERSION% win64_msvc2022_64 -m qtimageformats
echo       aqt install-qt windows desktop %QT_VERSION% win64_msvc2022_arm64_cross_compiled -m qtimageformats
echo   - set KAINT_QT_ARM64=...\%QT_VERSION%\msvc2022_arm64
echo     set KAINT_QT_HOST=...\%QT_VERSION%\msvc2022_64
exit /b 1

rem --- Copia de la instalación para empaquetar (%1: carpeta) --------------------
:stage
if exist "%~1" rmdir /s /q "%~1"
cmake --install "%BUILD_DIR%" --prefix "%~1" || exit /b 1
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    if exist "%MINGW_BIN%\%%D" copy /y "%MINGW_BIN%\%%D" "%~1\bin\" >nul
)
exit /b 0

rem --- Paquete portable (.zip) ------------------------------------------------
:zip
set "STEP=crear el .zip portable"
set "PACKAGE=%CD%\%BUILD_DIR%\package\Kaint"
set "ZIP=%CD%\%BUILD_DIR%\Kaint-%KAINT_VERSION%-windows-%ARCH_NAME%.zip"
echo Creando %ZIP%...
call :stage "%PACKAGE%" || exit /b 1
rem El portable no se instala: sin desinstalador.
if exist "%PACKAGE%\Uninstall.exe" del /q "%PACKAGE%\Uninstall.exe"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE%' -DestinationPath '%ZIP%'" || exit /b 1
call :report "Paquete" "%ZIP%" "(descomprimir y abrir Kaint\bin\kaint.exe)"
exit /b 0

rem --- Instalador con desinstalador (NSIS) ------------------------------------
rem Toda su información (nombre, versión, autor, web, iconos, licencia, carpeta, páginas, accesos
rem directos, idiomas y textos) sale de winsetup.json (u otro, con --winsetup): winsetup.ps1 lo
rem convierte en lo que lee packaging\windows\kaint.nsi.
:setup
set "STEP=crear el instalador"
call :find_nsis
if not defined MAKENSIS (
    echo.
    echo Para crear el instalador hace falta NSIS. Se puede descargar ahora en %TOOLCHAIN% ^(unos 2 MB^).
    if not "%ASSUME_YES%"=="1" (
        choice /c SN /m "¿Descargarlo"
        if errorlevel 2 exit /b 1
    )
    call :download_nsis || exit /b 1
    call :find_nsis
    if not defined MAKENSIS (
        echo No se encontró makensis.exe después de descargar NSIS.
        exit /b 1
    )
)
set "PACKAGE=%CD%\%BUILD_DIR%\setup\Kaint"
set "SETUP_NSH=%CD%\%BUILD_DIR%\setup\winsetup.nsh"
set "SETUP_NAME=%CD%\%BUILD_DIR%\setup\winsetup.txt"
call :stage "%PACKAGE%" || exit /b 1
if exist "%SETUP_NAME%" del /q "%SETUP_NAME%"
echo Leyendo %WINSETUP%...
powershell -NoProfile -ExecutionPolicy Bypass -File packaging\windows\winsetup.ps1 -Root "%CD%" -Config "%WINSETUP%" ^
    -Version "%KAINT_VERSION%" -Arch "%ARCH_NAME%" -OutDir "%CD%\%BUILD_DIR%" -Nsh "%SETUP_NSH%" -NameFile "%SETUP_NAME%" || exit /b 1
set "SETUP="
set /p "SETUP=" < "%SETUP_NAME%"
if not defined SETUP exit /b 1
echo Creando %SETUP%...
if exist "%SETUP%" del /q "%SETUP%"
"%MAKENSIS%" /V2 /INPUTCHARSET UTF8 "/DSOURCE=%PACKAGE%" "/DCONFIG=%SETUP_NSH%" packaging\windows\kaint.nsi || exit /b 1
call :report "Instalador" "%SETUP%" "(Kaint se desinstala desde «Aplicaciones instaladas»)"
exit /b 0

rem --- Buscar NSIS (makensis.exe) ---------------------------------------------
:find_nsis
set "MAKENSIS="
for /f "delims=" %%G in ('where makensis 2^>nul') do if not defined MAKENSIS set "MAKENSIS=%%~fG"
for %%N in ("%TOOLCHAIN%\nsis-%NSIS_VERSION%\makensis.exe" "%ProgramFiles(x86)%\NSIS\makensis.exe" "%ProgramFiles%\NSIS\makensis.exe") do (
    if not defined MAKENSIS if exist "%%~N" set "MAKENSIS=%%~N"
)
exit /b 0

rem --- Descargar NSIS (comprobado con su SHA-256) ------------------------------
:download_nsis
if not exist "%TOOLCHAIN%" mkdir "%TOOLCHAIN%"
set "NSIS_ZIP=%TOOLCHAIN%\nsis-%NSIS_VERSION%.zip"
echo Descargando NSIS %NSIS_VERSION%...
curl -L --fail -o "%NSIS_ZIP%" "%NSIS_URL%" || exit /b 1
powershell -NoProfile -Command "if ((Get-FileHash -Algorithm SHA256 '%NSIS_ZIP%').Hash -ne '%NSIS_SHA256%') { Write-Host 'El NSIS descargado no es el esperado (SHA-256 distinto).'; exit 1 }" || (del /q "%NSIS_ZIP%" & exit /b 1)
powershell -NoProfile -Command "Expand-Archive -Force -Path '%NSIS_ZIP%' -DestinationPath '%TOOLCHAIN%'" || exit /b 1
del /q "%NSIS_ZIP%"
exit /b 0

rem --- Desinstalar ------------------------------------------------------------
:uninstall
echo Desinstalando Kaint de %PREFIX%...
rem Con el desinstalador de la instalación (sin preguntar). Aquí mismo si no lo hay (instalaciones de
rem antes) o si este build.bat es la copia de la instalación (el desinstalador lo borraría a medias).
if not exist "%PREFIX%\Uninstall.exe" goto uninstall_here
if not exist "%PREFIX%\bin\kaint.exe" goto uninstall_here
if /i "%SCRIPT_DIR%"=="%PREFIX%\" goto uninstall_here
"%PREFIX%\Uninstall.exe" /S
if errorlevel 1 echo No se pudo desinstalar: ¿está Kaint abierto?
if errorlevel 1 exit /b 1
echo Kaint desinstalado. Tus imágenes y tu configuración no se han tocado.
exit /b 0
:uninstall_here
del /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Kaint.lnk" 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kaint" /f >nul 2>&1
if exist "%PREFIX%\bin" rmdir /s /q "%PREFIX%\bin"
if exist "%PREFIX%\share" rmdir /s /q "%PREFIX%\share"
if exist "%PREFIX%\plugins" rmdir /s /q "%PREFIX%\plugins"
if exist "%PREFIX%\translations" rmdir /s /q "%PREFIX%\translations"
if exist "%PREFIX%\include" rmdir /s /q "%PREFIX%\include"
if exist "%PREFIX%\Uninstall.exe" del /q "%PREFIX%\Uninstall.exe"
echo Kaint desinstalado. Tus imágenes y tu configuración no se han tocado.
if /i "%SCRIPT_DIR%"=="%PREFIX%\" (
    start "" /min cmd /c timeout /t 2 ^>nul ^& rmdir /s /q "%PREFIX%"
) else (
    if exist "%PREFIX%" rmdir /s /q "%PREFIX%"
)
exit /b 0

rem --- Buscar herramientas ----------------------------------------------------
:find_tools
set "QT_DIR="
set "MINGW_BIN="
set "CMAKE_BIN="
set "NINJA_DIR="
if defined KAINT_QT if exist "%KAINT_QT%\lib\cmake\Qt6\Qt6Config.cmake" set "QT_DIR=%KAINT_QT%"
if not defined QT_DIR if exist "%TOOLCHAIN%\%QT_VERSION%\mingw_64\lib\cmake\Qt6\Qt6Config.cmake" set "QT_DIR=%TOOLCHAIN%\%QT_VERSION%\mingw_64"
if not defined QT_DIR for /d %%Q in ("C:\Qt\6.*") do if exist "%%~Q\mingw_64\lib\cmake\Qt6\Qt6Config.cmake" set "QT_DIR=%%~Q\mingw_64"
for /d %%M in ("%TOOLCHAIN%\Tools\mingw*_64" "C:\Qt\Tools\mingw*_64") do if not defined MINGW_BIN if exist "%%~M\bin\g++.exe" set "MINGW_BIN=%%~M\bin"
if not defined MINGW_BIN for /f "delims=" %%G in ('where g++ 2^>nul') do if not defined MINGW_BIN set "MINGW_BIN=%%~dpG"
for %%C in ("%TOOLCHAIN%\Tools\CMake_64\bin" "C:\Qt\Tools\CMake_64\bin") do if not defined CMAKE_BIN if exist "%%~C\cmake.exe" set "CMAKE_BIN=%%~C"
if not defined CMAKE_BIN for /f "delims=" %%G in ('where cmake 2^>nul') do if not defined CMAKE_BIN set "CMAKE_BIN=%%~dpG"
for %%N in ("%TOOLCHAIN%\Tools\Ninja" "C:\Qt\Tools\Ninja") do if not defined NINJA_DIR if exist "%%~N\ninja.exe" set "NINJA_DIR=%%~N"
if not defined NINJA_DIR for /f "delims=" %%G in ('where ninja 2^>nul') do if not defined NINJA_DIR set "NINJA_DIR=%%~dpG"
if not defined QT_DIR exit /b 1
if not defined MINGW_BIN exit /b 1
if not defined CMAKE_BIN exit /b 1
if not defined NINJA_DIR exit /b 1
exit /b 0

rem --- Descargar herramientas (aqtinstall) ------------------------------------
:download_tools
set "STEP=descargar las herramientas"
if not exist "%TOOLCHAIN%" mkdir "%TOOLCHAIN%"
if not exist "%TOOLCHAIN%\aqt.exe" (
    echo Descargando aqtinstall...
    curl -L --fail -o "%TOOLCHAIN%\aqt.exe" "%AQT_URL%" || exit /b 1
)
set "AQT=%TOOLCHAIN%\aqt.exe"
pushd "%TOOLCHAIN%"
echo Descargando Qt %QT_VERSION% (MinGW)...
"%AQT%" install-qt windows desktop %QT_VERSION% win64_mingw -m qtimageformats --outputdir "%TOOLCHAIN%" || (popd & exit /b 1)
echo Descargando MinGW, CMake y Ninja...
"%AQT%" install-tool windows desktop tools_%MINGW_TOOL% qt.tools.win64_%MINGW_TOOL% --outputdir "%TOOLCHAIN%" || (popd & exit /b 1)
"%AQT%" install-tool windows desktop tools_cmake qt.tools.cmake --outputdir "%TOOLCHAIN%" || (popd & exit /b 1)
"%AQT%" install-tool windows desktop tools_ninja qt.tools.ninja --outputdir "%TOOLCHAIN%" || (popd & exit /b 1)
popd
exit /b 0

rem --- Ayuda ------------------------------------------------------------------
:print_usage
echo Uso: build.bat [opciones]
echo.
echo   -d, --debug          compila con símbolos de depuración (build-windows-debug\)
echo   -c, --clean          borra la carpeta de compilación antes de empezar
echo   -j, --jobs N         compila con N hilos (por defecto, todos los del procesador)
echo   -t, --test           ejecuta las pruebas después de compilar
echo   -i, --install        instala en %%LOCALAPPDATA%%\Programs\Kaint con acceso en el menú Inicio
echo       --uninstall      quita la instalación (también desde "Aplicaciones instaladas")
echo       --prefix RUTA    carpeta de instalación
echo       --zip            crea un .zip portable para publicar (build-windows\Kaint-VERSIÓN-windows-x64.zip)
echo       --setup          crea el instalador con desinstalador (build-windows\Kaint-VERSIÓN-windows-x64-setup.exe);
echo                        su nombre, versión, autor, web, iconos, licencia, carpeta, páginas, accesos directos,
echo                        idiomas y textos se cambian en winsetup.json
echo       --winsetup RUTA  otro archivo con la información del instalador, en vez de winsetup.json
echo       --avx2           compila para procesadores con AVX2 (x86-64-v3): más rápido, no va en los antiguos
echo       --arm64          compila para Windows arm64 (con Visual Studio y Qt para arm64: ver build.bat)
echo       --web            crea el .zip de la versión web (Kaint.HTML) en build-web\
echo       --apk            crea la versión móvil (Android, build-apk\Kaint-VERSIÓN.apk)
echo       --aab            crea el App Bundle para Google Play (build-aab\Kaint-VERSIÓN.aab)
echo       --kainthtml RUTA (con --apk o --aab) otra carpeta de KaintHTML (por defecto, la de aquí)
echo       --keystore RUTA  (con --apk o --aab) la llave (.jks o .keystore) con la que firmar
echo       --alias NOMBRE   (con --apk o --aab) el usuario (alias) de esa llave
echo       --wsl FORMATO    paquete de Linux con build.sh en WSL: deb, pkg, appimage, appimage-avx2, tar, tar-musl o tar-arm64
echo       --no-ccache      no usa ccache ni sccache aunque estén instalados
echo   -r, --run            ejecuta Kaint al terminar
echo   -y, --yes            descarga las herramientas que falten sin preguntar
echo   -h, --help           muestra esta ayuda
echo.
echo Sin opciones, un menú pregunta qué compilar (en rojo, lo que Windows no crea bien).
echo Si faltan Qt, MinGW, CMake o Ninja (o NSIS para --setup), se descargan en %%LOCALAPPDATA%%\Kaint\toolchain.
echo Con ccache o sccache instalados, se usan solos: volver a compilar tarda mucho menos.
echo Para usar un Qt propio para MinGW: set KAINT_QT=C:\Qt\6.10.3\mingw_64
echo Versión móvil: busca un JDK del 17 al 25 (si no lo hay, ofrece descargar Temurin 21), el SDK de
echo Android en %%ANDROID_HOME%% o %%LOCALAPPDATA%%\Android\Sdk y Gradle; lo que falte se descarga. Las
echo contraseñas de la llave se preguntan o se toman de KAINT_STORE_PASSWORD y KAINT_KEY_PASSWORD.
exit /b 0

rem --- Versión móvil: el bloque de PowerShell que ejecuta :apk ------------------------------
rem No es batch: cmd nunca llega aquí (todo lo de arriba acaba antes con exit /b).
#KAINT-MOBILE-BEGIN
# Versión móvil (Kaint.APK) en Windows: la versión web de Kaint (KaintHTML) como aplicación de
# Android, con el proyecto de android/. Lo ejecuta :apk con PowerShell (en batch no se pueden manejar
# bien contraseñas con ! o %, ni leer un .zip): este bloque no es batch, cmd nunca llega aquí.
# Recibe KAINT_APK_KIND (apk o aab), KAINT_ROOT, KAINT_VERSION, KAINT_TOOLCHAIN, KAINT_ASSUME_YES y,
# si se dieron, KAINT_HTML, KAINT_KEYSTORE, KAINT_KEY_ALIAS, KAINT_STORE_PASSWORD y KAINT_KEY_PASSWORD.
$ErrorActionPreference = 'Continue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$kind = $env:KAINT_APK_KIND
$root = $env:KAINT_ROOT
$version = $env:KAINT_VERSION
$yes = $env:KAINT_ASSUME_YES -eq '1'
$cache = Join-Path $env:KAINT_TOOLCHAIN 'android'
$keyDir = Join-Path $env:LOCALAPPDATA 'Kaint\android'
$platform = 'platforms;android-36'
$buildTools = 'build-tools;36.1.0'
$cmdlineUrl = 'https://dl.google.com/android/repository/commandlinetools-win-16111833_latest.zip'
$cmdlineSha1 = '57d04f2d75eb8e8fffc5000a987e5de4b5a63e9d'
$gradleVersion = '9.8.2'

function Die([string]$message) {
    Write-Host "Error: $message" -ForegroundColor Red
    exit 1
}
function Ask([string]$question) {
    $reply = Read-Host $question
    if ($null -eq $reply) { return '' }
    return $reply.Trim().Trim('"')
}
function Secret([string]$question) {
    $secure = Read-Host $question -AsSecureString
    if ($null -eq $secure -or $secure.Length -eq 0) { return '' }
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr) }
}
function Confirm([string]$question) {
    if ($yes) { return $true }
    return (Ask "$question [s/N]") -match '^[sSyY]'
}
function FullPath([string]$path) {
    $path = $path.Trim().Trim('"')
    if (-not [IO.Path]::IsPathRooted($path)) { $path = Join-Path $root $path }
    return [IO.Path]::GetFullPath($path)
}
function Download([string]$url, [string]$file) {
    & curl.exe -fL --progress-bar -o "$file.part" $url
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -Force -LiteralPath "$file.part" -ErrorAction SilentlyContinue
        Die "no se pudo descargar $url"
    }
    Move-Item -Force -LiteralPath "$file.part" -Destination $file
}
function Checked([string]$file, [string]$algorithm, [string]$expected) {
    if ((Get-FileHash -Algorithm $algorithm -LiteralPath $file).Hash -ne $expected.Trim().ToUpperInvariant()) {
        Remove-Item -Force -LiteralPath $file
        Die "$(Split-Path -Leaf $file) no coincide con su suma ($algorithm): no se usa"
    }
}
# Descomprime en una carpeta nueva (ZipFile: mucho más rápido que Expand-Archive con el SDK o el JDK).
function Unzip([string]$zip, [string]$destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Remove-Item -Recurse -Force -LiteralPath $destination -ErrorAction SilentlyContinue
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $destination)
}
function JavaMajor([string]$java) {
    $text = (& $java -XshowSettings:properties -version 2>&1 | ForEach-Object { "$_" }) -join "`n"
    if ($text -match 'java\.specification\.version = (\d+)') { return [int]$Matches[1] }
    return 0
}
function Size([string]$file) {
    return '{0:N1} MB' -f ((Get-Item -LiteralPath $file).Length / 1MB)
}

New-Item -ItemType Directory -Force -Path $cache | Out-Null
$what = if ($kind -eq 'aab') { 'App Bundle para Google Play' } else { 'APK' }
Write-Host "Kaint.APK: la versión web de Kaint como aplicación de Android ($what)."

# --- La carpeta de KaintHTML -------------------------------------------------------------------
$html = $env:KAINT_HTML
if (-not $html) {
    $html = Join-Path $root 'KaintHTML'
    if (-not $yes) {
        $reply = Ask "Carpeta de KaintHTML (Intro: $html)"
        if ($reply) { $html = $reply }
    }
}
$html = FullPath $html
if (-not (Test-Path -LiteralPath (Join-Path $html 'index.html')) -or -not (Test-Path -LiteralPath (Join-Path $html 'js\main.js'))) {
    Die "«$html» no es la carpeta de KaintHTML (falta index.html o js\main.js)"
}

# --- La llave ----------------------------------------------------------------------------------
# Google Play pide el App Bundle firmado con tu llave de subida. El APK para instalar a mano se firma
# con una llave propia de este equipo, que se conserva: una versión nueva solo se instala encima de
# la anterior si va firmada con la misma.
$keystore = $env:KAINT_KEYSTORE
$alias = $env:KAINT_KEY_ALIAS
$storePassword = $env:KAINT_STORE_PASSWORD
$keyPassword = $env:KAINT_KEY_PASSWORD
$ownKey = $false
if ($kind -eq 'aab' -or $keystore) {
    if (-not $keystore) { $keystore = Ask 'Ubicación de la llave (.jks o .keystore)' }
    if (-not $keystore) { Die 'falta la llave con la que firmar' }
    $keystore = FullPath $keystore
    if (-not (Test-Path -LiteralPath $keystore -PathType Leaf)) { Die "no existe la llave «$keystore»" }
    if (-not $alias) { $alias = Ask 'Usuario (alias) de la llave' }
    if (-not $alias) { Die 'falta el usuario (alias) de la llave' }
    if (-not $storePassword) { $storePassword = Secret 'Contraseña de la llave' }
    if (-not $keyPassword) { $keyPassword = Secret 'Contraseña del usuario (Intro: la misma)' }
    if (-not $keyPassword) { $keyPassword = $storePassword }
} else {
    $ownKey = $true
    $keystore = Join-Path $keyDir 'kaint-apk.jks'
    $alias = 'kaint'
    $storePassword = 'kaint-apk'
    $keyPassword = 'kaint-apk'
}

# --- Java (un JDK del 17 al 25; mejor el 21) ----------------------------------------------------
$candidates = New-Object System.Collections.Generic.List[string]
if ($env:JAVA_HOME) { $candidates.Add($env:JAVA_HOME) }
$patterns = @("$cache\jdk-*", "$env:ProgramFiles\Java\jdk-21*", "$env:ProgramFiles\Eclipse Adoptium\jdk-21*",
              "$env:ProgramFiles\Microsoft\jdk-21*", "$env:ProgramFiles\Android\Android Studio\jbr",
              "$env:LOCALAPPDATA\Programs\Android Studio\jbr", "$env:ProgramFiles\Java\jdk*", "$env:ProgramFiles\Eclipse Adoptium\jdk*",
              "$env:ProgramFiles\Microsoft\jdk*", "$env:ProgramFiles\Zulu\zulu*", "$env:ProgramFiles\Amazon Corretto\jdk*",
              "$env:ProgramFiles\BellSoft\*jdk*")
foreach ($pattern in $patterns) {
    Get-Item -Path $pattern -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object { $candidates.Add($_.FullName) }
}
$javac = Get-Command javac.exe -ErrorAction SilentlyContinue
if ($javac) { $candidates.Add((Split-Path -Parent (Split-Path -Parent $javac.Source))) }
# El 21 si lo hay; si no, el 17; si no, el más nuevo hasta el 25.
$javaHome = $null
$best = 0
foreach ($candidate in ($candidates | Select-Object -Unique)) {
    if (-not (Test-Path -LiteralPath "$candidate\bin\java.exe") -or -not (Test-Path -LiteralPath "$candidate\bin\javac.exe")) { continue }
    $major = JavaMajor "$candidate\bin\java.exe"
    if ($major -lt 17 -or $major -gt 25) { continue }
    $score = if ($major -eq 21) { 100 } elseif ($major -eq 17) { 90 } else { $major }
    if ($score -gt $best) { $best = $score; $javaHome = $candidate }
}
if (-not $javaHome) {
    Write-Host 'No hay un JDK del 17 al 25 (el JRE o el JDK 8 no sirven).' -ForegroundColor Yellow
    if (-not (Confirm "¿Descargar Eclipse Temurin 21 (unos 200 MB) en $cache?")) {
        Die 'hace falta un JDK del 17 al 25 (mejor el 21), por ejemplo: winget install EclipseAdoptium.Temurin.21.JDK'
    }
    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'aarch64' } else { 'x64' }
    try {
        $package = @(Invoke-RestMethod "https://api.adoptium.net/v3/assets/latest/21/hotspot?os=windows&architecture=$arch&image_type=jdk&vendor=eclipse")[0].binary.package
    } catch {
        Die "no se pudo consultar la versión de Temurin 21 ($($_.Exception.Message))"
    }
    $zip = Join-Path $cache $package.name
    Download $package.link $zip
    Checked $zip 'SHA256' $package.checksum
    Unzip $zip "$cache\jdk-tmp"
    Remove-Item -Force -LiteralPath $zip
    Remove-Item -Recurse -Force -LiteralPath "$cache\jdk-21" -ErrorAction SilentlyContinue
    Move-Item -LiteralPath (Get-ChildItem -LiteralPath "$cache\jdk-tmp" -Directory | Select-Object -First 1).FullName -Destination "$cache\jdk-21"
    Remove-Item -Recurse -Force -LiteralPath "$cache\jdk-tmp"
    $javaHome = "$cache\jdk-21"
}
$env:JAVA_HOME = $javaHome
$env:PATH = "$javaHome\bin;$env:PATH"
Write-Host "Java:   $javaHome ($(JavaMajor "$javaHome\bin\java.exe"))"

# --- SDK de Android ----------------------------------------------------------------------------
$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } elseif ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { "$env:LOCALAPPDATA\Android\Sdk" }
$sdkmanager = "$sdk\cmdline-tools\latest\bin\sdkmanager.bat"
if (-not (Test-Path -LiteralPath $sdkmanager)) {
    Write-Host "No están las herramientas del SDK de Android en $sdk (se puede elegir otra carpeta con ANDROID_HOME)." -ForegroundColor Yellow
    if (-not (Confirm '¿Descargar las herramientas de línea de órdenes del SDK de Android (unos 150 MB)?')) {
        Die 'sin el SDK de Android no se puede compilar'
    }
    $zip = Join-Path $cache (Split-Path -Leaf $cmdlineUrl)
    if (-not (Test-Path -LiteralPath $zip)) { Download $cmdlineUrl $zip }
    Checked $zip 'SHA1' $cmdlineSha1
    Unzip $zip "$cache\cmdline-tmp"
    New-Item -ItemType Directory -Force -Path "$sdk\cmdline-tools" | Out-Null
    Remove-Item -Recurse -Force -LiteralPath "$sdk\cmdline-tools\latest" -ErrorAction SilentlyContinue
    Move-Item -LiteralPath "$cache\cmdline-tmp\cmdline-tools" -Destination "$sdk\cmdline-tools\latest"
    Remove-Item -Recurse -Force -LiteralPath "$cache\cmdline-tmp"
}
$missing = @($platform, $buildTools | Where-Object { -not (Test-Path -LiteralPath (Join-Path $sdk ($_ -replace ';', '\'))) })
if ($missing.Count -gt 0) {
    Write-Host "Instalando del SDK de Android: $($missing -join ', ')"
    if (-not (Test-Path -LiteralPath "$sdk\licenses\android-sdk-license")) {
        Write-Host 'Para instalarlos hay que aceptar las licencias del SDK de Android:'
        & $sdkmanager "--sdk_root=$sdk" --licenses
    }
    & $sdkmanager "--sdk_root=$sdk" @missing
    if ($LASTEXITCODE -ne 0) { Die 'no se pudo instalar lo que falta del SDK de Android' }
}

# --- Gradle ------------------------------------------------------------------------------------
$gradle = "$cache\gradle-$gradleVersion\bin\gradle.bat"
if (-not (Test-Path -LiteralPath $gradle)) {
    if (-not (Confirm "¿Descargar Gradle $gradleVersion (unos 140 MB)?")) { Die 'sin Gradle no se puede compilar' }
    $base = "https://services.gradle.org/distributions/gradle-$gradleVersion-bin.zip"
    $zip = "$cache\gradle-$gradleVersion-bin.zip"
    Download $base $zip
    $expected = (& curl.exe -fsSL "$base.sha256") -join ''
    if ($LASTEXITCODE -ne 0 -or -not $expected) { Remove-Item -Force -LiteralPath $zip; Die 'no se pudo leer la suma de Gradle' }
    Checked $zip 'SHA256' $expected
    Unzip $zip "$cache\gradle-tmp"
    Remove-Item -Recurse -Force -LiteralPath "$cache\gradle-$gradleVersion" -ErrorAction SilentlyContinue
    Move-Item -LiteralPath "$cache\gradle-tmp\gradle-$gradleVersion" -Destination "$cache\gradle-$gradleVersion"
    Remove-Item -Recurse -Force -LiteralPath "$cache\gradle-tmp", $zip
}

# --- El proyecto, con KaintHTML dentro (en una copia: android\ no se toca) ----------------------
$out = Join-Path $root "build-$kind"
$work = Join-Path $out 'project'
Write-Host "Preparando Kaint.APK $version ($kind) en $work"
Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item -Recurse -LiteralPath (Join-Path $root 'android') -Destination $work
foreach ($generated in '.gradle', 'app\build', 'build', 'local.properties') {
    Remove-Item -Recurse -Force -LiteralPath (Join-Path $work $generated) -ErrorAction SilentlyContinue
}
$assets = "$work\app\src\main\assets\www"
Remove-Item -Recurse -Force -LiteralPath $assets -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $assets | Out-Null
# Solo lo de arriba del todo: «tools» o «tests» más adentro (js\tools, las herramientas) sí van.
$skip = @('tests', 'tools', 'node_modules', '.git', 'README.md', 'package.json', 'package-lock.json')
Get-ChildItem -LiteralPath $html -Force | Where-Object { $skip -notcontains $_.Name } |
    ForEach-Object { Copy-Item -Recurse -Force -LiteralPath $_.FullName -Destination $assets }
# El icono de Kaint (Android escala el que más se acerca a cada densidad).
foreach ($icon in @(@('mdpi', 'kaint-64.png'), @('xxxhdpi', 'kaint-256.png'))) {
    $from = Join-Path $html "assets\$($icon[1])"
    if (Test-Path -LiteralPath $from) {
        $to = "$work\app\src\main\res\mipmap-$($icon[0])"
        New-Item -ItemType Directory -Force -Path $to | Out-Null
        Copy-Item -Force -LiteralPath $from -Destination "$to\ic_launcher.png"
    }
}
if (-not (Get-ChildItem -Path "$work\app\src\main\res\mipmap-*\ic_launcher.png" -ErrorAction SilentlyContinue)) {
    Die 'falta el icono (assets\kaint-256.png de KaintHTML)'
}
# local.properties va con el formato de Java: \ y : escapados.
[IO.File]::WriteAllText("$work\local.properties", 'sdk.dir=' + $sdk.Replace('\', '\\').Replace(':', '\:') + "`n")

# --- La llave: la propia se crea la primera vez; cualquiera se comprueba antes de compilar ------
$keytool = "$javaHome\bin\keytool.exe"
if ($ownKey -and -not (Test-Path -LiteralPath $keystore)) {
    Write-Host "Creando la llave de este equipo para firmar el APK ($keystore)"
    New-Item -ItemType Directory -Force -Path $keyDir | Out-Null
    & $keytool -genkeypair -keystore $keystore -alias $alias -keyalg RSA -keysize 4096 -validity 10000 `
        -storepass $storePassword -keypass $keyPassword -dname 'CN=Kaint, O=Kaint' *> $null
    if ($LASTEXITCODE -ne 0) { Die 'no se pudo crear la llave del APK' }
}
$env:KAINT_CHECK_PASSWORD = $storePassword
& $keytool -list -keystore $keystore -alias $alias -storepass:env KAINT_CHECK_PASSWORD *> $null
$checked = $LASTEXITCODE -eq 0
Remove-Item Env:\KAINT_CHECK_PASSWORD
if (-not $checked) { Die "no se pudo abrir la llave «$keystore» con ese usuario (alias) y contraseña" }

# --- Compilar (las contraseñas van por variables de entorno, no en la línea de órdenes) ---------
$numbers = @([regex]::Matches($version, '\d+') | ForEach-Object { [int]$_.Value }) + @(0, 0, 0)
$code = $numbers[0] * 10000 + $numbers[1] * 100 + $numbers[2]
$task = if ($kind -eq 'aab') { 'bundleRelease' } else { 'assembleRelease' }
$env:KAINT_STORE_FILE = $keystore
$env:KAINT_STORE_PASSWORD = $storePassword
$env:KAINT_KEY_ALIAS = $alias
$env:KAINT_KEY_PASSWORD = $keyPassword
$env:ANDROID_HOME = $sdk
Write-Host "Compilando ($task)..."
& $gradle -p $work --no-daemon --console=plain "-PkaintVersionName=$version" "-PkaintVersionCode=$code" $task
if ($LASTEXITCODE -ne 0) { Die 'Gradle no pudo compilar la versión móvil' }

# --- Todos los archivos de la web tienen que estar dentro: si falta uno de sus módulos de
# JavaScript, la aplicación se queda en blanco al abrirse. ---------------------------------------
if ($kind -eq 'aab') {
    $result = "$work\app\build\outputs\bundle\release\app-release.aab"; $final = "$out\Kaint-$version.aab"; $prefix = 'base/assets/www/'
} else {
    $result = "$work\app\build\outputs\apk\release\app-release.apk"; $final = "$out\Kaint-$version.apk"; $prefix = 'assets/www/'
}
if (-not (Test-Path -LiteralPath $result)) { Die "Gradle terminó pero no está $result" }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($result)
try {
    $inside = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($entry in $archive.Entries) {
        if ($entry.FullName.StartsWith($prefix)) { [void]$inside.Add($entry.FullName.Substring($prefix.Length)) }
    }
} finally {
    $archive.Dispose()
}
$assetsRoot = (Get-Item -LiteralPath $assets).FullName.TrimEnd('\') + '\'
$lost = @(Get-ChildItem -LiteralPath $assets -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($assetsRoot.Length).Replace('\', '/') } |
          Where-Object { -not $inside.Contains($_) })
if ($lost.Count -gt 0) {
    Die ("faltan archivos de la web dentro de $(Split-Path -Leaf $result) ($prefix):`n" + (($lost | Select-Object -First 20) -join "`n"))
}
Copy-Item -Force -LiteralPath $result -Destination $final
Write-Host ''
Write-Host -NoNewline 'Versión móvil: ' -ForegroundColor Green
Write-Host "$final ($(Size $final))"
if ($kind -eq 'aab') {
    Write-Host '  Súbelo a Google Play Console (Versiones > Crear una versión).'
} else {
    Write-Host "  Para instalarla con el teléfono conectado (depuración USB): adb install -r `"$final`""
    Write-Host "  Para instalar encima una versión nueva, se firma con la misma llave: $keystore"
}
exit 0
#KAINT-MOBILE-END
