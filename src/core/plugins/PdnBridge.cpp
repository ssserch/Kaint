#include "core/plugins/PdnBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// macOS (y algunos BSD) no tienen SOCK_CLOEXEC ni MSG_NOSIGNAL: el par de sockets se crea sin la
// marca y se le pone FD_CLOEXEC con fcntl(), y SIGPIPE se evita con SO_NOSIGPIPE.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

namespace {

// El de fábrica, OpenD2D; KAINT_PDN_SYSTEM=wine (o opend2d) elige otro al empezar (pruebas).
int initialRuntime()
{
    return qEnvironmentVariable("KAINT_PDN_SYSTEM").compare(QLatin1String("wine"), Qt::CaseInsensitive) == 0
               ? int(PdnBridge::Runtime::Wine)
               : int(PdnBridge::Runtime::OpenD2D);
}

std::atomic_int g_runtime{initialRuntime()};

#ifndef Q_OS_WIN
int makeSocketPair(int fds[2])
{
    int rc = -1;
#ifdef SOCK_CLOEXEC
    rc = socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
#endif
    if (rc != 0) {
        rc = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        if (rc != 0)
            return rc;
        for (int i = 0; i < 2; ++i)
            fcntl(fds[i], F_SETFD, fcntl(fds[i], F_GETFD) | FD_CLOEXEC);
    }
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fds[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return 0;
}

// Nombre de una biblioteca de OpenD2D en este sistema (libd2d1.so, libd2d1.dylib...).
QString openD2DLibrary(const char* name)
{
#ifdef Q_OS_MACOS
    return QStringLiteral("lib%1.dylib").arg(QLatin1String(name));
#else
    return QStringLiteral("lib%1.so").arg(QLatin1String(name));
#endif
}

// mono busca las bibliotecas nativas por el nombre del DllImport ("d2d1.dll"): un archivo de
// configuración (MONO_CONFIG) con el de mono y, además, a qué biblioteca de OpenD2D lleva cada
// nombre de Windows. Sin el de mono (las de libgdiplus...) no se toca: entonces OpenD2D se
// encuentra solo por LD_LIBRARY_PATH (mono también prueba "libd2d1.so" para "d2d1.dll").
QString monoConfigFor(const QString& openD2D)
{
    const QString mono = PdnBridge::monoPath();
    const QString prefix = mono.isEmpty() ? QString() : QFileInfo(QFileInfo(mono).canonicalFilePath()).absolutePath() + QStringLiteral("/..");
    const QStringList candidates{
        qEnvironmentVariable("MONO_CONFIG"),
        QStringLiteral("/etc/mono/config"),
        prefix + QStringLiteral("/etc/mono/config"),
        QStringLiteral("/usr/local/etc/mono/config"),
        QStringLiteral("/opt/homebrew/etc/mono/config"),
        QStringLiteral("/Library/Frameworks/Mono.framework/Versions/Current/etc/mono/config"),
    };
    QByteArray base;
    for (const QString& c : candidates) {
        QFile f(c);
        if (!c.isEmpty() && f.open(QIODevice::ReadOnly)) {
            base = f.readAll();
            if (base.contains("</configuration>"))
                break;
            base.clear();
        }
    }
    if (base.isEmpty())
        return {};
    QByteArray maps = "\n\t<!-- Kaint: OpenD2D en lugar de Direct2D, DirectWrite y WIC de Windows -->\n";
    for (const char* lib : {"d2d1", "dwrite", "windowscodecs"}) {
        const QString target = QDir(openD2D).absoluteFilePath(openD2DLibrary(lib));
        if (!QFileInfo::exists(target))
            continue;
        for (const QString& name : {QString::fromLatin1(lib), QString::fromLatin1(lib) + QStringLiteral(".dll")})
            maps += QStringLiteral("\t<dllmap dll=\"%1\" target=\"%2\" />\n").arg(name, target.toHtmlEscaped()).toUtf8();
    }
    base.replace("</configuration>", maps + "</configuration>");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return {};
    const QString path = dir + QStringLiteral("/opend2d-mono.config");
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == base)
        return path;
    current.close();
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(base) != base.size() || !out.commit())
        return {};
    return path;
}

// El entorno del proceso auxiliar: el de Kaint con algunas variables cambiadas.
class SpawnEnvironment {
public:
    SpawnEnvironment()
    {
        for (char** e = environ; e && *e; ++e)
            m_vars.append(QByteArray(*e));
    }
    QByteArray value(const QByteArray& name) const
    {
        for (const QByteArray& v : m_vars)
            if (v.startsWith(name + '='))
                return v.mid(name.size() + 1);
        return {};
    }
    void set(const QByteArray& name, const QByteArray& value)
    {
        m_vars.removeIf([&](const QByteArray& v) { return v.startsWith(name + '='); });
        m_vars.append(name + '=' + value);
    }
    // Una carpeta delante de una lista separada por ':'.
    void prepend(const QByteArray& name, const QByteArray& dir)
    {
        const QByteArray old = value(name);
        set(name, old.isEmpty() ? dir : dir + ':' + old);
    }
    char** data()
    {
        m_pointers.clear();
        for (QByteArray& v : m_vars)
            m_pointers.push_back(v.data());
        m_pointers.push_back(nullptr);
        return m_pointers.data();
    }

private:
    QList<QByteArray> m_vars;
    std::vector<char*> m_pointers;
};
#endif

// Tiempo que se espera a que un complemento atienda la cancelación antes de terminar el
// proceso auxiliar.
constexpr int kCancelGraceMs = 3000;
constexpr int kInspectTimeoutMs = 60000;

qint64 toArgb(const QColor& c)
{
    return qint64(c.rgba());
}

// Paleta para IPalettesService (vacía: el proceso usa la de Paint.NET).
QJsonArray paletteJson(const EffectContext& ctx)
{
    QJsonArray list;
    for (const QColor& c : ctx.palette)
        list.append(toArgb(c));
    return list;
}

#ifdef Q_OS_WIN
QString windowsError(DWORD code = GetLastError())
{
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    const QString message = text ? QString::fromWCharArray(text).trimmed() : QString::number(code);
    LocalFree(text);
    return message;
}
#endif

} // namespace

PdnBridge& PdnBridge::instance()
{
    return forRuntime(runtime());
}

PdnBridge& PdnBridge::forRuntime(Runtime runtime)
{
#ifdef Q_OS_WIN
    Q_UNUSED(runtime);
    static PdnBridge bridge(Runtime::OpenD2D);
    return bridge;
#else
    static PdnBridge openD2D(Runtime::OpenD2D);
    static PdnBridge wine(Runtime::Wine);
    return runtime == Runtime::Wine ? wine : openD2D;
#endif
}

namespace {
std::atomic_bool g_wineFallback{true};
std::mutex g_wineEffectsMutex;
} // namespace

void PdnBridge::setWineFallback(bool on)
{
    g_wineFallback = on;
}

bool PdnBridge::wineFallback()
{
#ifdef Q_OS_WIN
    return false;
#else
    return g_wineFallback && runtime() == Runtime::OpenD2D && !winePath().isEmpty() && !hostPath().isEmpty();
#endif
}

QString PdnBridge::cacheTag()
{
    return runtimeName(runtime()) + (wineFallback() ? QStringLiteral("+Wine") : QString());
}

bool PdnBridge::effectNeedsWine(const QString& effectId)
{
    std::lock_guard lock(g_wineEffectsMutex);
    return QSettings().value(QStringLiteral("plugins/wineEffects")).toStringList().contains(effectId);
}

void PdnBridge::rememberWineEffect(const QString& effectId)
{
    std::lock_guard lock(g_wineEffectsMutex);
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("plugins/wineEffects")).toStringList();
    if (!list.contains(effectId)) {
        list << effectId;
        settings.setValue(QStringLiteral("plugins/wineEffects"), list);
    }
}

void PdnBridge::shutdownAll()
{
    forRuntime(Runtime::OpenD2D).shutdown();
    forRuntime(Runtime::Wine).shutdown();
}

void PdnBridge::abortAll()
{
    forRuntime(Runtime::OpenD2D).abort();
    forRuntime(Runtime::Wine).abort();
}

PdnBridge::~PdnBridge()
{
    stop();
}

namespace {

std::mutex& effectsFolderMutex()
{
    static std::mutex m;
    return m;
}

QStringList& effectsFolderList()
{
    static QStringList list;
    return list;
}

#ifndef Q_OS_WIN
// Como en Paint.NET, algunos complementos buscan los demás efectos en la carpeta Effects junto al
// programa (Random Effect de pyrochild elige uno al azar entre ellos): el proceso auxiliar se
// ejecuta desde una copia en la caché del usuario (mono no sigue un enlace simbólico al decir dónde
// está el programa) con enlaces a sus bibliotecas, y él llena allí Effects con los complementos
// activados. Si no se puede, el de siempre.
QString launchHostPath(const QString& real)
{
    if (real.isEmpty())
        return real;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty())
        return real;
    const QString dir = cache + QStringLiteral("/pdnhost");
    if (!QDir().mkpath(dir + QStringLiteral("/Effects")))
        return real;
    const QDir source = QFileInfo(real).absoluteDir();
    for (const QFileInfo& f : source.entryInfoList(QDir::Files)) {
        const QString dest = dir + QLatin1Char('/') + f.fileName();
        const QFileInfo d(dest);
        if (f.fileName().endsWith(QLatin1String(".exe")) || f.fileName().endsWith(QLatin1String(".config"))) {
            // El programa (y su configuración), copiados si cambiaron; con un archivo temporal
            // que se renombra, por si hay otro Kaint abierto.
            QFile in(f.absoluteFilePath());
            if (!in.open(QIODevice::ReadOnly))
                return real;
            const QByteArray data = in.readAll();
            QFile current(dest);
            if (!d.isSymLink() && current.open(QIODevice::ReadOnly) && current.readAll() == data)
                continue;
            current.close();
            QSaveFile out(dest);
            if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit())
                return real;
            QFile::setPermissions(dest, f.permissions());
        } else if (d.symLinkTarget() != f.absoluteFilePath()) {
            QFile::remove(dest);
            if (!QFile::link(f.absoluteFilePath(), dest))
                return real;
        }
    }
    return dir + QStringLiteral("/kaint-pdn-host.exe");
}
#endif

} // namespace

void PdnBridge::setEffectsFolder(const QStringList& assemblies)
{
    std::lock_guard lock(effectsFolderMutex());
    effectsFolderList() = assemblies;
}

QString PdnBridge::hostPath()
{
    const QString env = qEnvironmentVariable("KAINT_PDN_HOST");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return env;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        appDir + QStringLiteral("/pdnbridge/kaint-pdn-host.exe"),
        appDir + QStringLiteral("/../share/kaint/pdnbridge/kaint-pdn-host.exe"),
        appDir + QStringLiteral("/../lib/kaint/pdnbridge/kaint-pdn-host.exe"),
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return QFileInfo(c).canonicalFilePath();
    return {};
}

QString PdnBridge::monoPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("mono"));
}

QString PdnBridge::winePath()
{
    QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
    if (wine.isEmpty())
        wine = QStandardPaths::findExecutable(QStringLiteral("wine64"));
#ifdef Q_OS_MACOS
    // Wine de Homebrew (wine-stable, wine@devel...) o el de CrossOver.
    for (const QString& c : {QStringLiteral("/opt/homebrew/bin/wine"), QStringLiteral("/usr/local/bin/wine"),
                             QStringLiteral("/Applications/Wine Stable.app/Contents/Resources/wine/bin/wine")})
        if (wine.isEmpty() && QFileInfo(c).isExecutable())
            wine = c;
#endif
    return wine;
}

QString PdnBridge::openD2DPath()
{
#ifdef Q_OS_WIN
    return {};
#else
    const QString lib = openD2DLibrary("d2d1");
    const QString env = qEnvironmentVariable("KAINT_OPEND2D");
    if (!env.isEmpty() && QFileInfo::exists(env + QLatin1Char('/') + lib))
        return QFileInfo(env).canonicalFilePath();
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef KAINT_INSTALL_LIBDIR
    const QString installed = appDir + QStringLiteral("/../" KAINT_INSTALL_LIBDIR "/kaint/opend2d");
#else
    const QString installed = appDir + QStringLiteral("/../lib/kaint/opend2d");
#endif
    for (const QString& c : {appDir + QStringLiteral("/opend2d"), installed, appDir + QStringLiteral("/../lib/kaint/opend2d"),
                             appDir + QStringLiteral("/../Frameworks/opend2d")})
        if (QFileInfo::exists(c + QLatin1Char('/') + lib))
            return QFileInfo(c).canonicalFilePath();
    return {};
#endif
}

QString PdnBridge::openD2DVersion()
{
    const QString dir = openD2DPath();
    QFile f(dir + QStringLiteral("/VERSION"));
    if (dir.isEmpty() || !f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

bool PdnBridge::runtimeSelectable()
{
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
}

void PdnBridge::setRuntime(Runtime runtime)
{
    const auto before = Runtime(g_runtime.exchange(int(runtime)));
    if (before == runtime)
        return;
#ifndef Q_OS_WIN
    // El proceso del sistema de antes ya no hace falta (salvo el de Wine, si sigue de respaldo).
    if (!(before == Runtime::Wine && wineFallback()))
        forRuntime(before).shutdown();
#endif
}

PdnBridge::Runtime PdnBridge::runtime()
{
    return Runtime(g_runtime.load());
}

QString PdnBridge::runtimeName(Runtime runtime)
{
    return runtime == Runtime::Wine ? QStringLiteral("Wine") : QStringLiteral("OpenD2D");
}

bool PdnBridge::available(QString* why)
{
    return availableFor(runtime(), why);
}

bool PdnBridge::availableFor(Runtime runtime, QString* why)
{
#ifdef Q_OS_WIN
    Q_UNUSED(runtime);
#else
    if (runtime == Runtime::Wine) {
        if (winePath().isEmpty()) {
            if (why)
                *why = QCoreApplication::translate("PdnBridge", "El sistema de complementos Wine necesita wine "
                                                                "(sudo pacman -S wine o sudo apt install wine). "
                                                                "También puedes elegir OpenD2D en Configuración → "
                                                                "Complementos.");
            return false;
        }
    } else if (monoPath().isEmpty()) {
        if (why)
            *why = QCoreApplication::translate("PdnBridge", "Los complementos de Paint.NET necesitan mono "
                                                            "(sudo pacman -S mono libgdiplus).");
        return false;
    }
#endif
    if (hostPath().isEmpty()) {
        if (why)
            *why = QCoreApplication::translate("PdnBridge", "Kaint se compiló sin la capa de compatibilidad con "
                                                            "Paint.NET (hace falta un compilador de C# al compilar).");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Proceso auxiliar, comunicación y memoria compartida, según el sistema
// ---------------------------------------------------------------------------

#ifndef Q_OS_WIN
bool PdnBridge::ensureStarted(QString* error)
{
    if (m_pid > 0) {
        int status = 0;
        if (waitpid(m_pid, &status, WNOHANG) == 0)
            return true;
        m_pid = -1;
        stop();
    }
    QString why;
    if (!availableFor(m_runtime, &why)) {
        if (error)
            *error = why;
        return false;
    }
    int fds[2];
    if (makeSocketPair(fds) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    const bool debug = !qEnvironmentVariableIsEmpty("KAINT_PDN_DEBUG");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (!debug)
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    const Runtime chosen = m_runtime;
    // Ya, para que hostSidePath dé las rutas como las verá este proceso.
    m_startedWith = chosen;
    SpawnEnvironment env;
    const QByteArray runner = QFile::encodeName(chosen == Runtime::Wine ? winePath() : monoPath());
    const QByteArray host = QFile::encodeName(launchHostPath(hostPath()));
    {
        // Los complementos de la carpeta Effects (como los ve el proceso auxiliar), uno por línea.
        std::lock_guard lock(effectsFolderMutex());
        QStringList paths;
        for (const QString& a : std::as_const(effectsFolderList()))
            paths << hostSidePath(a);
        if (!paths.isEmpty())
            env.set("KAINT_PDN_EFFECTS", paths.join(QLatin1Char('\n')).toUtf8());
    }
    const QString openD2D = openD2DPath();
    if (chosen == Runtime::Wine) {
        // Wine, sin sus mensajes de depuración (salvo con KAINT_PDN_DEBUG), y con las DLL de
        // OpenD2D para Windows si se compilaron junto al proceso auxiliar.
        if (!debug)
            env.set("WINEDEBUG", "-all");
        const QDir hostDir = QFileInfo(hostPath()).absoluteDir();
        QStringList native;
        for (const char* dll : {"d2d1", "dwrite"})
            if (hostDir.exists(QLatin1String(dll) + QStringLiteral(".dll")))
                native << QString::fromLatin1(dll);
        if (!native.isEmpty())
            env.set("WINEDLLOVERRIDES", native.join(QLatin1Char(',')).toUtf8() + "=n,b");
    } else if (!openD2D.isEmpty()) {
        // OpenD2D: sus bibliotecas, antes que nada, para los DllImport de d2d1, dwrite y windowscodecs.
#ifdef Q_OS_MACOS
        env.prepend("DYLD_LIBRARY_PATH", QFile::encodeName(openD2D));
#else
        env.prepend("LD_LIBRARY_PATH", QFile::encodeName(openD2D));
#endif
        const QString config = monoConfigFor(openD2D);
        if (!config.isEmpty())
            env.set("MONO_CONFIG", QFile::encodeName(config));
        env.set("KAINT_OPEND2D", QFile::encodeName(openD2D));
        const QString version = openD2DVersion();
        if (!version.isEmpty())
            env.set("KAINT_OPEND2D_VERSION", version.toUtf8());
    }
    env.set("KAINT_PDN_RUNTIME", runtimeName(chosen).toUtf8());
    char* argv[] = {const_cast<char*>(runner.constData()), const_cast<char*>(host.constData()), nullptr};
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, runner.constData(), &actions, nullptr, argv, env.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        if (error)
            *error = QString::fromLocal8Bit(strerror(rc));
        return false;
    }
    m_pid = pid;
    m_socket = fds[0];
    m_startedWith = chosen;
    m_buffer.clear();
    // Un archivo de memoria compartida de otra sesión no sirve (la ruta cambia dentro de Wine).
    m_srcKey = 0;
    return true;
}

void PdnBridge::stop()
{
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
    if (m_pid > 0) {
        const pid_t pid = m_pid.exchange(-1);
        int status = 0;
        pid_t done = 0;
        for (int i = 0; i < 20 && (done = waitpid(pid, &status, WNOHANG)) == 0; ++i)
            usleep(10000);
        if (done == 0) {
            ::kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
        }
    }
    if (m_shm) {
        munmap(m_shm, m_shmSize);
        m_shm = nullptr;
    }
    if (m_shmFd >= 0) {
        ::close(m_shmFd);
        m_shmFd = -1;
#ifdef Q_OS_LINUX
        shm_unlink(QFile::encodeName(m_shmPath.mid(int(qstrlen("/dev/shm")))).constData());
#else
        ::unlink(QFile::encodeName(m_shmPath).constData());
#endif
    }
    m_shmSize = 0;
    m_srcKey = 0;
}

void PdnBridge::abort()
{
    const int pid = m_pid.load();
    if (pid > 0)
        ::kill(pid, SIGKILL);
}

bool PdnBridge::writeLine(const QByteArray& line)
{
    QByteArray data = line;
    data.append('\n');
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t n = ::send(m_socket, data.constData() + done, size_t(data.size() - done), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        done += n;
    }
    return true;
}

int PdnBridge::readChunk(int timeoutMs)
{
    pollfd pfd{m_socket, POLLIN, 0};
    const int rc = ::poll(&pfd, 1, timeoutMs);
    if (rc < 0)
        return errno == EINTR ? 0 : -1;
    if (rc == 0)
        return 0;
    char chunk[65536];
    const ssize_t n = ::recv(m_socket, chunk, sizeof chunk, 0);
    if (n <= 0)
        return -1;
    m_buffer.append(chunk, n);
    return 1;
}

bool PdnBridge::mapShared(size_t needed, QString* error)
{
    if (m_shm) {
        munmap(m_shm, m_shmSize);
        m_shm = nullptr;
    }
    if (m_shmFd < 0) {
#ifdef Q_OS_LINUX
        // En Linux, /dev/shm: el proceso auxiliar abre el mismo archivo por su ruta.
        static std::atomic_int counter{0};
        const QString name = QStringLiteral("/kaint-pdn-%1-%2").arg(getpid()).arg(++counter);
        m_shmFd = shm_open(QFile::encodeName(name).constData(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (m_shmFd < 0) {
            if (error)
                *error = QString::fromLocal8Bit(strerror(errno));
            return false;
        }
        m_shmPath = QStringLiteral("/dev/shm") + name;
#else
        // macOS y FreeBSD no tienen /dev/shm (sus objetos de shm_open no son archivos): un
        // archivo temporal, proyectado en memoria por los dos procesos.
        QByteArray path = QFile::encodeName(QDir::tempPath() + QStringLiteral("/kaint-pdn-XXXXXX"));
        m_shmFd = mkstemp(path.data());
        if (m_shmFd < 0) {
            if (error)
                *error = QString::fromLocal8Bit(strerror(errno));
            return false;
        }
        fcntl(m_shmFd, F_SETFD, fcntl(m_shmFd, F_GETFD) | FD_CLOEXEC);
        m_shmPath = QFile::decodeName(path);
#endif
    }
    if (ftruncate(m_shmFd, off_t(needed)) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    void* p = mmap(nullptr, needed, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (p == MAP_FAILED) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    m_shm = static_cast<uchar*>(p);
    m_shmSize = needed;
    return true;
}

void PdnBridge::allowDialogFocus() {}

#else
// Windows: el proceso auxiliar es un programa de .NET Framework (viene con Windows); se habla
// con él por tuberías anónimas y los píxeles van por memoria compartida con nombre.

bool PdnBridge::ensureStarted(QString* error)
{
    if (HANDLE process = m_process.load()) {
        if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
            return true;
        stop();
    }
    QString why;
    if (!available(&why)) {
        if (error)
            *error = why;
        return false;
    }
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childIn = nullptr, toHost = nullptr, fromHost = nullptr, childOut = nullptr;
    if (!CreatePipe(&childIn, &toHost, &inherit, 0)) {
        if (error)
            *error = windowsError();
        return false;
    }
    if (!CreatePipe(&fromHost, &childOut, &inherit, 0)) {
        if (error)
            *error = windowsError();
        CloseHandle(childIn);
        CloseHandle(toHost);
        return false;
    }
    SetHandleInformation(toHost, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromHost, HANDLE_FLAG_INHERIT, 0);
    HANDLE errors = qEnvironmentVariableIsEmpty("KAINT_PDN_DEBUG")
                        ? CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr)
                        : GetStdHandle(STD_ERROR_HANDLE);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childIn;
    startup.hStdOutput = childOut;
    startup.hStdError = errors;
    PROCESS_INFORMATION info{};
    std::wstring command = L"\"" + QDir::toNativeSeparators(hostPath()).toStdWString() + L"\"";
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &startup, &info);
    const DWORD code = GetLastError();
    CloseHandle(childIn);
    CloseHandle(childOut);
    if (qEnvironmentVariableIsEmpty("KAINT_PDN_DEBUG") && errors != INVALID_HANDLE_VALUE)
        CloseHandle(errors);
    if (!started) {
        CloseHandle(toHost);
        CloseHandle(fromHost);
        if (error)
            *error = windowsError(code);
        return false;
    }
    CloseHandle(info.hThread);
    m_process = info.hProcess;
    m_toHost = toHost;
    m_fromHost = fromHost;
    m_buffer.clear();
    return true;
}

void PdnBridge::stop()
{
    // Al cerrar su entrada el proceso termina solo; si no, se termina a la fuerza.
    if (m_toHost) {
        CloseHandle(m_toHost);
        m_toHost = nullptr;
    }
    if (m_fromHost) {
        CloseHandle(m_fromHost);
        m_fromHost = nullptr;
    }
    if (HANDLE process = m_process.exchange(nullptr)) {
        if (WaitForSingleObject(process, 200) != WAIT_OBJECT_0) {
            TerminateProcess(process, 1);
            WaitForSingleObject(process, 2000);
        }
        CloseHandle(process);
    }
    if (m_shm) {
        UnmapViewOfFile(m_shm);
        m_shm = nullptr;
    }
    if (m_mapping) {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
    }
    m_shmSize = 0;
    m_srcKey = 0;
}

void PdnBridge::abort()
{
    if (HANDLE process = m_process.load())
        TerminateProcess(process, 1);
}

bool PdnBridge::writeLine(const QByteArray& line)
{
    QByteArray data = line;
    data.append('\n');
    qsizetype done = 0;
    while (done < data.size()) {
        DWORD written = 0;
        if (!WriteFile(m_toHost, data.constData() + done, DWORD(data.size() - done), &written, nullptr))
            return false;
        done += written;
    }
    return true;
}

int PdnBridge::readChunk(int timeoutMs)
{
    // Las tuberías anónimas no admiten espera con plazo: se mira cada pocos milisegundos.
    for (int waited = 0;; waited += 5) {
        DWORD available = 0;
        if (!PeekNamedPipe(m_fromHost, nullptr, 0, nullptr, &available, nullptr))
            return -1;
        if (available > 0) {
            char chunk[65536];
            DWORD n = 0;
            if (!ReadFile(m_fromHost, chunk, std::min<DWORD>(available, sizeof chunk), &n, nullptr) || n == 0)
                return -1;
            m_buffer.append(chunk, qsizetype(n));
            return 1;
        }
        if (waited >= timeoutMs)
            return 0;
        Sleep(5);
    }
}

bool PdnBridge::mapShared(size_t needed, QString* error)
{
    // Una memoria con nombre por tamaño (no se puede agrandar): el proceso la abre por su nombre.
    if (m_shm) {
        UnmapViewOfFile(m_shm);
        m_shm = nullptr;
    }
    if (m_mapping) {
        CloseHandle(m_mapping);
        m_mapping = nullptr;
    }
    static std::atomic_int counter{0};
    const QString name = QStringLiteral("Local\\kaint-pdn-%1-%2").arg(GetCurrentProcessId()).arg(++counter);
    const quint64 size = needed;
    m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(size >> 32),
                                   DWORD(size & 0xffffffffu), reinterpret_cast<const wchar_t*>(name.utf16()));
    if (!m_mapping) {
        if (error)
            *error = windowsError();
        return false;
    }
    void* p = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, needed);
    if (!p) {
        if (error)
            *error = windowsError();
        CloseHandle(m_mapping);
        m_mapping = nullptr;
        return false;
    }
    m_shm = static_cast<uchar*>(p);
    m_shmSize = needed;
    m_shmPath = name;
    return true;
}

void PdnBridge::allowDialogFocus()
{
    // El diálogo del complemento lo abre el otro proceso: se le deja pasar al frente.
    if (HANDLE process = m_process.load())
        AllowSetForegroundWindow(GetProcessId(process));
}
#endif

// ---------------------------------------------------------------------------
// Peticiones (comunes)
// ---------------------------------------------------------------------------

QString PdnBridge::hostSidePath(const QString& path) const
{
#ifdef Q_OS_WIN
    return path;
#else
    // Dentro de Wine, la raíz del sistema es la unidad Z:.
    if (m_startedWith != Runtime::Wine || !path.startsWith(QLatin1Char('/')))
        return path;
    return QStringLiteral("Z:") + QString(path).replace(QLatin1Char('/'), QLatin1Char('\\'));
#endif
}

QJsonObject PdnBridge::hello(QString* error)
{
    std::lock_guard lock(m_mutex);
    return request(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("hello")}}, kInspectTimeoutMs, error);
}

void PdnBridge::shutdown()
{
    std::lock_guard lock(m_mutex);
    stop();
}

bool PdnBridge::readResponse(QJsonObject* out, int timeoutMs, const std::atomic_bool* cancel,
                             EffectProgress* progress, int progressTotal, QString* error,
                             const std::function<void()>& onPreview)
{
    bool cancelSent = false;
    qint64 waited = 0;
    qint64 sinceCancel = 0;
    while (true) {
        const qsizetype nl = m_buffer.indexOf('\n');
        if (nl >= 0) {
            const QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 1);
            const QJsonObject obj = QJsonDocument::fromJson(line).object();
            if (obj.contains(QStringLiteral("ok"))) {
                *out = obj;
                return true;
            }
            // La vista previa de la ventana propia del complemento ya está en la memoria compartida.
            if (onPreview && obj.contains(QStringLiteral("preview"))) {
                onPreview();
                continue;
            }
            if (progress && obj.contains(QStringLiteral("progress"))) {
                const double total = std::max(1.0, obj.value(QStringLiteral("total")).toDouble());
                progress->done = qint64(obj.value(QStringLiteral("progress")).toDouble() / total * progressTotal);
            }
            continue;
        }
        if (cancel && cancel->load() && !cancelSent) {
            cancelSent = true;
            writeLine(QByteArrayLiteral("{\"cmd\":\"cancel\"}"));
        }
        const int slice = 50;
        const int got = readChunk(slice);
        if (got < 0)
            break;
        if (got > 0)
            continue;
        waited += slice;
        if (cancelSent)
            sinceCancel += slice;
        if ((timeoutMs >= 0 && waited > timeoutMs) || sinceCancel > kCancelGraceMs) {
            if (error && !cancelSent)
                *error = QCoreApplication::translate("PdnBridge", "El complemento no responde.");
            stop();
            return false;
        }
    }
    if (error)
        *error = QCoreApplication::translate("PdnBridge", "El proceso de compatibilidad con Paint.NET terminó "
                                                          "inesperadamente.");
    stop();
    return false;
}

QJsonObject PdnBridge::request(const QJsonObject& req, int timeoutMs, QString* error)
{
    if (!ensureStarted(error))
        return {};
    if (!writeLine(QJsonDocument(req).toJson(QJsonDocument::Compact))) {
        stop();
        if (!ensureStarted(error) || !writeLine(QJsonDocument(req).toJson(QJsonDocument::Compact))) {
            if (error && error->isEmpty())
                *error = QCoreApplication::translate("PdnBridge", "No se pudo comunicar con el proceso de "
                                                                  "compatibilidad.");
            return {};
        }
    }
    QJsonObject reply;
    if (!readResponse(&reply, timeoutMs, nullptr, nullptr, 0, error))
        return {};
    return reply;
}

QJsonObject PdnBridge::inspect(const QString& path, QString* error)
{
    std::lock_guard lock(m_mutex);
    if (!ensureStarted(error))
        return {};
    QJsonObject req{{QStringLiteral("cmd"), QStringLiteral("inspect")}, {QStringLiteral("path"), hostSidePath(path)}};
    const QJsonObject reply = request(req, kInspectTimeoutMs, error);
    if (!reply.isEmpty() && !reply.value(QStringLiteral("ok")).toBool() && error)
        *error = reply.value(QStringLiteral("error")).toString();
    return reply;
}

bool PdnBridge::prepareShared(const QImage& src, QString* error)
{
    const size_t layerBytes = size_t(src.width()) * size_t(src.height()) * 4;
    const size_t needed = layerBytes * 2;
    if (!m_shm || m_shmSize < needed) {
        if (!mapShared(needed, error))
            return false;
        m_srcKey = 0;
    }
    if (m_srcKey != src.cacheKey() || m_srcSize != src.size()) {
        const int rowBytes = src.width() * 4;
        for (int y = 0; y < src.height(); ++y)
            std::memcpy(m_shm + size_t(y) * size_t(rowBytes), src.constScanLine(y), size_t(rowBytes));
        m_srcKey = src.cacheKey();
        m_srcSize = src.size();
    }
    return true;
}

bool PdnBridge::render(const QString& assembly, const QString& effectId, const QJsonObject& values, const QImage& src,
                       QImage& dst, const QRect& roi, const EffectContext& ctx, const std::atomic_bool& cancel,
                       QString* error)
{
    std::lock_guard lock(m_mutex);
    if (!ensureStarted(error) || !prepareShared(src, error))
        return false;
    const QJsonObject req{
        {QStringLiteral("cmd"), QStringLiteral("render")},
        {QStringLiteral("path"), hostSidePath(assembly)},
        {QStringLiteral("effect"), effectId},
        {QStringLiteral("shm"), hostSidePath(m_shmPath)},
        {QStringLiteral("width"), src.width()},
        {QStringLiteral("height"), src.height()},
        {QStringLiteral("roi"), QJsonArray{roi.x(), roi.y(), roi.width(), roi.height()}},
        {QStringLiteral("primary"), toArgb(ctx.primary)},
        {QStringLiteral("secondary"), toArgb(ctx.secondary)},
        {QStringLiteral("palette"), paletteJson(ctx)},
        {QStringLiteral("values"), values},
    };
    if (!writeLine(QJsonDocument(req).toJson(QJsonDocument::Compact))) {
        stop();
        if (error)
            *error = QCoreApplication::translate("PdnBridge", "No se pudo comunicar con el proceso de compatibilidad.");
        return false;
    }
    QJsonObject reply;
    if (!readResponse(&reply, -1, &cancel, ctx.progress, roi.height(), error))
        return false;
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        if (error && !reply.value(QStringLiteral("cancelled")).toBool())
            *error = reply.value(QStringLiteral("error")).toString();
        return false;
    }
    if (cancel)
        return false;
    const uchar* out = m_shm + size_t(src.width()) * size_t(src.height()) * 4;
    const int rowBytes = src.width() * 4;
    for (int y = roi.top(); y <= roi.bottom(); ++y)
        std::memcpy(dst.scanLine(y) + roi.left() * 4, out + size_t(y) * size_t(rowBytes) + size_t(roi.left()) * 4,
                    size_t(roi.width()) * 4);
    return true;
}

bool PdnBridge::showDialog(const QString& assembly, const QString& effectId, const QString& title, const QImage& src,
                           const QRect& roi, const EffectContext& ctx, bool* accepted, QString* error)
{
    std::lock_guard lock(m_mutex);
    *accepted = false;
    if (!ensureStarted(error) || !prepareShared(src, error))
        return false;
    allowDialogFocus();
    const QJsonObject req{
        {QStringLiteral("cmd"), QStringLiteral("dialog")},
        {QStringLiteral("path"), hostSidePath(assembly)},
        {QStringLiteral("effect"), effectId},
        {QStringLiteral("title"), title},
        {QStringLiteral("shm"), hostSidePath(m_shmPath)},
        {QStringLiteral("width"), src.width()},
        {QStringLiteral("height"), src.height()},
        {QStringLiteral("roi"), QJsonArray{roi.x(), roi.y(), roi.width(), roi.height()}},
        {QStringLiteral("primary"), toArgb(ctx.primary)},
        {QStringLiteral("secondary"), toArgb(ctx.secondary)},
        {QStringLiteral("palette"), paletteJson(ctx)},
        {QStringLiteral("preview"), bool(ctx.dialogPreview)},
    };
    if (!writeLine(QJsonDocument(req).toJson(QJsonDocument::Compact))) {
        stop();
        if (error)
            *error = QCoreApplication::translate("PdnBridge", "No se pudo comunicar con el proceso de compatibilidad.");
        return false;
    }
    // Cada vista previa: la imagen con la zona calculada (la mitad de destino de la memoria
    // compartida) para el lienzo.
    std::function<void()> onPreview;
    if (ctx.dialogPreview)
        onPreview = [&] {
            QImage shown = src.copy();
            const uchar* out = m_shm + size_t(src.width()) * size_t(src.height()) * 4;
            const int rowBytes = src.width() * 4;
            for (int y = roi.top(); y <= roi.bottom(); ++y)
                std::memcpy(shown.scanLine(y) + roi.left() * 4,
                            out + size_t(y) * size_t(rowBytes) + size_t(roi.left()) * 4, size_t(roi.width()) * 4);
            ctx.dialogPreview(shown);
        };
    QJsonObject reply;
    if (!readResponse(&reply, -1, nullptr, nullptr, 0, error, onPreview))
        return false;
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = reply.value(QStringLiteral("error")).toString();
        return false;
    }
    *accepted = reply.value(QStringLiteral("accepted")).toBool();
    return true;
}
