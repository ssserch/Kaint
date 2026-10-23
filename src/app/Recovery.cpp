#include "app/Recovery.h"

#include "core/Document.h"
#include "core/Ora.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>

#include <memory>

namespace Recovery {

namespace {

QString g_root;
std::unique_ptr<QLockFile> g_lock;

QString sessionDir(qint64 pid)
{
    return rootDir() + QLatin1Char('/') + QString::number(pid);
}

QString lockPath(const QString& dir)
{
    return dir + QStringLiteral("/session.lock");
}

// Mientras Kaint está abierto, su carpeta tiene un bloqueo; si se cierra de golpe, QLockFile lo
// da por abandonado (en Linux y en Windows).
bool sessionAlive(const QString& dir)
{
    if (g_lock && g_lock->isLocked() && lockPath(dir) == lockPath(sessionDir(QCoreApplication::applicationPid())))
        return true;
    QLockFile lock(lockPath(dir));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return true;
    lock.unlock();
    return false;
}

} // namespace

QString rootDir()
{
    if (!g_root.isEmpty())
        return g_root;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Recovery");
}

void setRootForTesting(const QString& dir)
{
    g_root = dir;
}

void save(const QList<Document*>& docs)
{
    const QString dir = sessionDir(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    if (!g_lock || !g_lock->isLocked()) {
        g_lock = std::make_unique<QLockFile>(lockPath(dir));
        g_lock->tryLock(0);
    }
    QSet<QString> kept{QStringLiteral("session")};
    for (Document* doc : docs) {
        if (!doc || !doc->isModified())
            continue;
        // Número fijo por documento aunque cambie el orden de las pestañas.
        static int nextId = 1;
        if (!doc->property("kaintRecoveryId").isValid())
            doc->setProperty("kaintRecoveryId", nextId++);
        const QString base = QStringLiteral("%1/%2").arg(dir).arg(doc->property("kaintRecoveryId").toInt());
        QString error;
        if (!Ora::save(doc, base + QStringLiteral(".part"), &error))
            continue;
        QFile::remove(base + QStringLiteral(".ora"));
        QFile::rename(base + QStringLiteral(".part"), base + QStringLiteral(".ora"));
        QFile meta(base + QStringLiteral(".json"));
        if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate))
            meta.write(QJsonDocument(QJsonObject{{QStringLiteral("path"), doc->filePath()},
                                                 {QStringLiteral("name"), doc->displayName()}})
                           .toJson(QJsonDocument::Compact));
        kept << QFileInfo(base).fileName();
    }
    for (const QFileInfo& f : QDir(dir).entryInfoList(QDir::Files))
        if (!kept.contains(f.completeBaseName()))
            QFile::remove(f.absoluteFilePath());
}

void clear()
{
    if (g_lock)
        g_lock->unlock();
    g_lock.reset();
    QDir(sessionDir(QCoreApplication::applicationPid())).removeRecursively();
}

QList<Entry> orphaned()
{
    QList<Entry> entries;
    for (const QFileInfo& session : QDir(rootDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        session.fileName().toLongLong(&ok);
        if (!ok || sessionAlive(session.absoluteFilePath()))
            continue;
        for (const QFileInfo& f : QDir(session.absoluteFilePath()).entryInfoList({QStringLiteral("*.ora")}, QDir::Files)) {
            Entry e;
            e.file = f.absoluteFilePath();
            QFile meta(f.absolutePath() + QLatin1Char('/') + f.completeBaseName() + QStringLiteral(".json"));
            if (meta.open(QIODevice::ReadOnly)) {
                const QJsonObject o = QJsonDocument::fromJson(meta.readAll()).object();
                e.originalPath = o.value(QStringLiteral("path")).toString();
                e.name = o.value(QStringLiteral("name")).toString();
            }
            if (e.name.isEmpty())
                e.name = f.fileName();
            entries.append(e);
        }
    }
    return entries;
}

void discard(const QList<Entry>& entries)
{
    QSet<QString> dirs;
    for (const Entry& e : entries) {
        const QFileInfo f(e.file);
        QFile::remove(e.file);
        QFile::remove(f.absolutePath() + QLatin1Char('/') + f.completeBaseName() + QStringLiteral(".json"));
        dirs << f.absolutePath();
    }
    for (const QString& dir : dirs)
        if (QDir(dir).isEmpty())
            QDir().rmdir(dir);
}

} // namespace Recovery
