#include "app/Recovery.h"

#include "core/Document.h"
#include "core/Ora.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace Recovery {

namespace {

QString g_root;

QString sessionDir(qint64 pid)
{
    return rootDir() + QLatin1Char('/') + QString::number(pid);
}

// Un proceso de Kaint con ese número sigue abierto (en Linux, por /proc).
bool kaintRunning(qint64 pid)
{
    if (pid == QCoreApplication::applicationPid())
        return true;
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly))
        return false;
    return comm.readAll().trimmed().startsWith("kaint");
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
    QSet<QString> kept;
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
    QDir(sessionDir(QCoreApplication::applicationPid())).removeRecursively();
}

QList<Entry> orphaned()
{
    QList<Entry> entries;
    for (const QFileInfo& session : QDir(rootDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const qint64 pid = session.fileName().toLongLong(&ok);
        if (!ok || kaintRunning(pid))
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
