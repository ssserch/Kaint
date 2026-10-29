#include "app/UserFonts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QStandardPaths>

namespace UserFonts {

namespace {

// Archivo → identificador de QFontDatabase.
QHash<QString, int>& loaded()
{
    static QHash<QString, int> ids;
    return ids;
}

const QStringList kPatterns{QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc"),
                            QStringLiteral("*.otc"), QStringLiteral("*.woff"), QStringLiteral("*.woff2")};

int load(const QString& path)
{
    if (const auto it = loaded().constFind(path); it != loaded().cend())
        return *it;
    const int id = QFontDatabase::addApplicationFont(path);
    if (id >= 0)
        loaded().insert(path, id);
    return id;
}

} // namespace

QString folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Fonts");
}

void loadAll()
{
    for (const QFileInfo& info : QDir(folder()).entryInfoList(kPatterns, QDir::Files))
        load(info.absoluteFilePath());
}

QList<Font> installed()
{
    QList<Font> fonts;
    for (const QFileInfo& info : QDir(folder()).entryInfoList(kPatterns, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        const int id = load(info.absoluteFilePath());
        fonts.append({info.absoluteFilePath(), id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList()});
    }
    return fonts;
}

QStringList add(const QStringList& files, QStringList* errors)
{
    QStringList families;
    QDir().mkpath(folder());
    for (const QString& file : files) {
        const QFileInfo info(file);
        const QString target = QDir(folder()).filePath(info.fileName());
        if (QFileInfo(target) == info)
            continue;
        // Primero se comprueba que Qt la puede leer; después se copia.
        const int probe = QFontDatabase::addApplicationFont(file);
        if (probe < 0) {
            if (errors)
                *errors << info.fileName();
            continue;
        }
        QFontDatabase::removeApplicationFont(probe);
        if (const auto it = loaded().constFind(target); it != loaded().cend()) {
            QFontDatabase::removeApplicationFont(*it);
            loaded().remove(target);
        }
        QFile::remove(target);
        if (!QFile::copy(file, target)) {
            if (errors)
                *errors << info.fileName();
            continue;
        }
        const int id = load(target);
        if (id >= 0)
            families << QFontDatabase::applicationFontFamilies(id);
    }
    return families;
}

bool remove(const QString& file)
{
    if (const auto it = loaded().constFind(file); it != loaded().cend()) {
        QFontDatabase::removeApplicationFont(*it);
        loaded().remove(file);
    }
    return QFile::remove(file);
}

void removeAll()
{
    for (const Font& f : installed())
        remove(f.file);
}

} // namespace UserFonts
