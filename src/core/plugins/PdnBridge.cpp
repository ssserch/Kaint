#include "core/plugins/PdnBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

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

} // namespace

PdnBridge& PdnBridge::instance()
{
    static PdnBridge bridge;
    return bridge;
}

PdnBridge::~PdnBridge()
{
    stop();
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

bool PdnBridge::available(QString* why)
{
    if (monoPath().isEmpty()) {
        if (why)
            *why = QCoreApplication::translate("PdnBridge", "Los complementos de Paint.NET necesitan mono "
                                                            "(sudo pacman -S mono libgdiplus).");
        return false;
    }
    if (hostPath().isEmpty()) {
        if (why)
            *why = QCoreApplication::translate("PdnBridge", "Kaint se compiló sin la capa de compatibilidad con "
                                                            "Paint.NET (hace falta mono al compilar).");
        return false;
    }
    return true;
}

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
    if (!available(&why)) {
        if (error)
            *error = why;
        return false;
    }
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) != 0) {
        if (error)
            *error = QString::fromLocal8Bit(strerror(errno));
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    if (qEnvironmentVariableIsEmpty("KAINT_PDN_DEBUG"))
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    const QByteArray mono = QFile::encodeName(monoPath());
    const QByteArray host = QFile::encodeName(hostPath());
    char* argv[] = {const_cast<char*>(mono.constData()), const_cast<char*>(host.constData()), nullptr};
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, mono.constData(), &actions, nullptr, argv, environ);
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
    m_buffer.clear();
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
        shm_unlink(QFile::encodeName(m_shmPath.mid(int(qstrlen("/dev/shm")))).constData());
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

void PdnBridge::shutdown()
{
    std::lock_guard lock(m_mutex);
    stop();
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

bool PdnBridge::readResponse(QJsonObject* out, int timeoutMs, const std::atomic_bool* cancel,
                             EffectProgress* progress, int progressTotal, QString* error)
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
        pollfd pfd{m_socket, POLLIN, 0};
        const int slice = 50;
        const int rc = ::poll(&pfd, 1, slice);
        if (rc < 0 && errno != EINTR)
            break;
        if (rc > 0) {
            char chunk[65536];
            const ssize_t n = ::recv(m_socket, chunk, sizeof chunk, 0);
            if (n <= 0)
                break;
            m_buffer.append(chunk, n);
            continue;
        }
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
    QJsonObject req{{QStringLiteral("cmd"), QStringLiteral("inspect")}, {QStringLiteral("path"), path}};
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
        if (m_shm) {
            munmap(m_shm, m_shmSize);
            m_shm = nullptr;
        }
        if (m_shmFd < 0) {
            static std::atomic_int counter{0};
            const QString name = QStringLiteral("/kaint-pdn-%1-%2").arg(getpid()).arg(++counter);
            m_shmFd = shm_open(QFile::encodeName(name).constData(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (m_shmFd < 0) {
                if (error)
                    *error = QString::fromLocal8Bit(strerror(errno));
                return false;
            }
            m_shmPath = QStringLiteral("/dev/shm") + name;
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
        {QStringLiteral("path"), assembly},
        {QStringLiteral("effect"), effectId},
        {QStringLiteral("shm"), m_shmPath},
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
    const QJsonObject req{
        {QStringLiteral("cmd"), QStringLiteral("dialog")},
        {QStringLiteral("path"), assembly},
        {QStringLiteral("effect"), effectId},
        {QStringLiteral("title"), title},
        {QStringLiteral("shm"), m_shmPath},
        {QStringLiteral("width"), src.width()},
        {QStringLiteral("height"), src.height()},
        {QStringLiteral("roi"), QJsonArray{roi.x(), roi.y(), roi.width(), roi.height()}},
        {QStringLiteral("primary"), toArgb(ctx.primary)},
        {QStringLiteral("secondary"), toArgb(ctx.secondary)},
        {QStringLiteral("palette"), paletteJson(ctx)},
    };
    const QJsonObject reply = request(req, -1, error);
    if (reply.isEmpty())
        return false;
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        if (error)
            *error = reply.value(QStringLiteral("error")).toString();
        return false;
    }
    *accepted = reply.value(QStringLiteral("accepted")).toBool();
    return true;
}
