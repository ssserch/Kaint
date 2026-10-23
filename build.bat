@echo off
rem Compila e instala Kaint en Windows 10 y 11 (opciones: build.bat --help).
setlocal EnableExtensions EnableDelayedExpansion
for /f "tokens=2 delims=:." %%C in ('chcp') do set "KAINT_OLD_CP=%%C"
chcp 65001 >nul
set "SCRIPT=%~f0"
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

rem --- Doble clic: instala y espera al final ----------------------------------
set "INTERACTIVE=0"
if "%~1"=="" echo %CMDCMDLINE% | find /i "%~nx0" >nul && set "INTERACTIVE=1"
call :main %*
set "RC=%ERRORLEVEL%"
if "%INTERACTIVE%"=="1" pause
if defined KAINT_OLD_CP chcp %KAINT_OLD_CP% >nul
exit /b %RC%

:main

set "QT_VERSION=6.10.3"
set "MINGW_TOOL=mingw1310"
set "AQT_URL=https://github.com/miurahr/aqtinstall/releases/download/v3.3.0/aqt_x64.exe"
set "TOOLCHAIN=%LOCALAPPDATA%\Kaint\toolchain"
set "PREFIX=%LOCALAPPDATA%\Programs\Kaint"
set "BUILD_TYPE=Release"
set "BUILD_DIR=build-windows"
set "DO_CLEAN=0"
set "DO_TEST=0"
set "DO_INSTALL=0"
set "DO_RUN=0"
set "DO_UNINSTALL=0"
set "ASSUME_YES=0"
if "%~1"=="" if "%INTERACTIVE%"=="1" set "DO_INSTALL=1"

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
if /i "%~1"=="-r"          (set "DO_RUN=1" & shift & goto args)
if /i "%~1"=="--run"       (set "DO_RUN=1" & shift & goto args)
if /i "%~1"=="-y"          (set "ASSUME_YES=1" & shift & goto args)
if /i "%~1"=="--yes"       (set "ASSUME_YES=1" & shift & goto args)
if /i "%~1"=="--prefix"    (set "PREFIX=%~2" & shift & shift & goto args)
if /i "%~1"=="-h"          goto usage
if /i "%~1"=="--help"      goto usage
echo Opción desconocida: %~1
call :print_usage
exit /b 1
:usage
call :print_usage
exit /b 0
:args_done

if "%DO_UNINSTALL%"=="1" goto uninstall

rem --- Herramientas -----------------------------------------------------------
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

rem --- Compilación ------------------------------------------------------------
if "%DO_CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo Borrando %BUILD_DIR%\
    rmdir /s /q "%BUILD_DIR%"
)
echo Configurando (%BUILD_TYPE%)...
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
      -DCMAKE_INSTALL_PREFIX="%PREFIX%" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ || exit /b 1
echo Compilando...
cmake --build "%BUILD_DIR%" --parallel || exit /b 1

if "%DO_TEST%"=="1" (
    echo Ejecutando pruebas...
    set "QT_QPA_PLATFORM=offscreen"
    ctest --test-dir "%BUILD_DIR%" --output-on-failure || exit /b 1
    set "QT_QPA_PLATFORM="
)

if "%DO_INSTALL%"=="1" call :install || exit /b 1

echo Listo: %BUILD_DIR%\kaint.exe
if "%DO_RUN%"=="1" (
    if "%DO_INSTALL%"=="1" (start "" "%PREFIX%\bin\kaint.exe") else (start "" "%BUILD_DIR%\kaint.exe")
)
exit /b 0

rem --- Instalar ---------------------------------------------------------------
:install
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
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "(Get-Content version.json -Raw | ConvertFrom-Json).version"`) do set "KAINT_VERSION=%%V"
reg add "%UNINSTALL_KEY%" /v DisplayName /d "Kaint" /f >nul
reg add "%UNINSTALL_KEY%" /v DisplayVersion /d "%KAINT_VERSION%" /f >nul
reg add "%UNINSTALL_KEY%" /v Publisher /d "SSSerch" /f >nul
reg add "%UNINSTALL_KEY%" /v DisplayIcon /d "%PREFIX%\bin\kaint.exe" /f >nul
reg add "%UNINSTALL_KEY%" /v InstallLocation /d "%PREFIX%" /f >nul
reg add "%UNINSTALL_KEY%" /v UninstallString /d "\"%PREFIX%\build.bat\" --uninstall --prefix \"%PREFIX%\"" /f >nul
reg add "%UNINSTALL_KEY%" /v NoModify /t REG_DWORD /d 1 /f >nul
reg add "%UNINSTALL_KEY%" /v NoRepair /t REG_DWORD /d 1 /f >nul
echo Instalado: %PREFIX%\bin\kaint.exe (también en el menú Inicio)
exit /b 0

rem --- Desinstalar ------------------------------------------------------------
:uninstall
echo Desinstalando Kaint de %PREFIX%...
del /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Kaint.lnk" 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kaint" /f >nul 2>&1
if exist "%PREFIX%\bin" rmdir /s /q "%PREFIX%\bin"
if exist "%PREFIX%\share" rmdir /s /q "%PREFIX%\share"
if exist "%PREFIX%\plugins" rmdir /s /q "%PREFIX%\plugins"
if exist "%PREFIX%\translations" rmdir /s /q "%PREFIX%\translations"
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
echo   -t, --test           ejecuta las pruebas después de compilar
echo   -i, --install        instala en %%LOCALAPPDATA%%\Programs\Kaint con acceso en el menú Inicio
echo       --uninstall      quita la instalación (también desde "Aplicaciones instaladas")
echo       --prefix RUTA    carpeta de instalación
echo   -r, --run            ejecuta Kaint al terminar
echo   -y, --yes            descarga las herramientas que falten sin preguntar
echo   -h, --help           muestra esta ayuda
echo.
echo Si faltan Qt, MinGW, CMake o Ninja, se descargan en %%LOCALAPPDATA%%\Kaint\toolchain.
echo Para usar un Qt propio para MinGW: set KAINT_QT=C:\Qt\6.10.3\mingw_64
exit /b 0
