// Desinstalador de Kaint para Windows (Uninstall.exe, junto a la carpeta bin de la instalación).
//
// Lo instala "build.bat --install" y es el que abre "Aplicaciones instaladas" o "Panel de control >
// Programas y características" al desinstalar Kaint (el instalador de "build.bat --setup" lleva el
// suyo, de NSIS, con el mismo nombre). Sin Qt ni otras DLL: borra la carpeta bin, donde están.
//
//   Uninstall.exe        pregunta antes de desinstalar (y si se borran también los datos)
//   Uninstall.exe /S     sin preguntar nada (QuietUninstallString); no borra los datos
//
// Quita el acceso del menú Inicio (y el del escritorio), la entrada de "Aplicaciones instaladas" y
// las carpetas de Kaint dentro de la instalación; la carpeta solo se borra si queda vacía (lo que
// haya puesto el usuario allí no se toca). Él mismo se borra un momento después de cerrarse.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

#include <iterator>
#include <string>
#include <vector>

namespace {

const wchar_t* const kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Kaint";
const wchar_t* const kSettingsKey = L"Software\\Kaint";

bool spanish()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH;
}

const wchar_t* text(const wchar_t* es, const wchar_t* en)
{
    return spanish() ? es : en;
}

int ask(const std::wstring& message, UINT flags)
{
    return MessageBoxW(nullptr, message.c_str(), text(L"Desinstalar Kaint", L"Uninstall Kaint"), flags | MB_SETFOREGROUND);
}

std::wstring parentOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw)
        path = raw;
    CoTaskMemFree(raw);
    return path;
}

std::wstring environment(const wchar_t* name)
{
    wchar_t buffer[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buffer, DWORD(std::size(buffer)));
    return n > 0 && n < std::size(buffer) ? std::wstring(buffer, n) : std::wstring();
}

// Borra una carpeta con todo lo que tiene (sin preguntar ni mostrar nada).
void removeTree(const std::wstring& path)
{
    if (path.size() < 4 || !exists(path))
        return;
    std::vector<wchar_t> from(path.begin(), path.end());
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    SHFileOperationW(&op);
}

// Kaint abierto: su ejecutable no se puede abrir para escribir.
bool kaintRunning(const std::wstring& exe)
{
    const HANDLE h = CreateFileW(exe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h);
    return false;
}

// Lo que no puede borrar mientras está abierto (él mismo y la carpeta, si queda vacía), un momento
// después de cerrarse, con una consola oculta.
void removeAfterExit(const std::wstring& self, const std::wstring& folder, const std::wstring& temp)
{
    std::wstring command = L"cmd.exe /d /s /c \"ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\" & rmdir \"" + folder + L"\"\"";
    std::vector<wchar_t> line(command.begin(), command.end());
    line.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                       temp.empty() ? nullptr : temp.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"/S" || arg == L"/s" || arg == L"--silent")
            silent = true;
    }
    LocalFree(argv);

    wchar_t selfBuffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, selfBuffer, DWORD(std::size(selfBuffer)));
    if (length == 0 || length >= std::size(selfBuffer))
        return 1;
    const std::wstring self(selfBuffer, length);
    const std::wstring folder = parentOf(self);
    const std::wstring kaint = folder + L"\\bin\\kaint.exe";
    // Nada se borra fuera de una instalación de Kaint.
    if (folder.empty() || !exists(kaint)) {
        if (!silent)
            ask(text(L"No se encontró Kaint junto a este desinstalador.", L"Kaint was not found next to this uninstaller."),
                MB_OK | MB_ICONERROR);
        return 2;
    }
    // Fuera de la carpeta: si no, no se podría borrar mientras sigue abierto.
    const std::wstring temp = parentOf(environment(L"TEMP") + L"\\x");
    if (!temp.empty())
        SetCurrentDirectoryW(temp.c_str());

    bool removeData = false;
    if (!silent) {
        const std::wstring question = std::wstring(text(L"¿Desinstalar Kaint de esta carpeta?\n\n", L"Uninstall Kaint from this folder?\n\n"))
                                      + folder;
        if (ask(question, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return 3;
        removeData = ask(text(L"¿Borrar también tu configuración, paletas, complementos, paquetes de iconos, fuentes y copias de "
                              L"recuperación?\n\nTus imágenes no se tocan.",
                              L"Also delete your settings, palettes, plugins, icon packs, fonts and recovery copies?\n\n"
                              L"Your images are not touched."),
                         MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2)
                     == IDYES;
    }
    while (kaintRunning(kaint)) {
        if (silent || ask(text(L"Kaint está abierto. Ciérralo y pulsa Reintentar.", L"Kaint is running. Close it and click Retry."),
                          MB_RETRYCANCEL | MB_ICONEXCLAMATION)
                          != IDRETRY)
            return 4;
    }

    // Accesos directos y la entrada de "Aplicaciones instaladas".
    for (const std::wstring& dir : {knownFolder(FOLDERID_Programs), knownFolder(FOLDERID_Desktop)})
        if (!dir.empty())
            DeleteFileW((dir + L"\\Kaint.lnk").c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
    RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);

    // Lo instalado (las carpetas que ponen CMake y el despliegue de Qt) y la copia de build.bat.
    for (const wchar_t* sub : {L"bin", L"lib", L"share", L"include", L"plugins", L"translations", L"qml", L"doc"})
        removeTree(folder + L"\\" + sub);
    DeleteFileW((folder + L"\\build.bat").c_str());

    // Lo que Kaint guarda para el usuario (QSettings, QStandardPaths), si se pidió; las herramientas
    // que descarga build.bat (%LOCALAPPDATA%\Kaint\toolchain) no se tocan.
    if (removeData) {
        RegDeleteTreeW(HKEY_CURRENT_USER, kSettingsKey);
        RegDeleteKeyW(HKEY_CURRENT_USER, kSettingsKey);
        const std::wstring roaming = knownFolder(FOLDERID_RoamingAppData), local = knownFolder(FOLDERID_LocalAppData);
        if (!roaming.empty())
            removeTree(roaming + L"\\Kaint");
        if (!local.empty()) {
            for (const wchar_t* sub : {L"\\Kaint\\Kaint", L"\\Kaint\\Effects", L"\\Kaint\\Shapes"})
                removeTree(local + sub);
            DeleteFileW((local + L"\\Kaint\\discord.json").c_str());
            RemoveDirectoryW((local + L"\\Kaint").c_str());
        }
    }

    if (!silent)
        ask(text(L"Kaint se ha desinstalado.", L"Kaint has been uninstalled."), MB_OK | MB_ICONINFORMATION);
    removeAfterExit(self, folder, temp);
    return 0;
}
