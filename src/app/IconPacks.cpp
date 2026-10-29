#include "app/IconPacks.h"

#include "core/Zip.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace IconPacks {

namespace {

const QString kManifest = QStringLiteral("asset-pack.json");
// Carpetas del paquete que Kaint usa (las traducciones de paint.js, lang/, no le sirven).
const QStringList kFolders{QStringLiteral("icons"), QStringLiteral("cursors"), QStringLiteral("images")};
constexpr qint64 kMaxZip = 256LL * 1024 * 1024;
constexpr qint64 kMaxImage = 16LL * 1024 * 1024;

bool validId(const QString& id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return re.match(id).hasMatch() && !id.contains(QLatin1String(".."));
}

Pack readManifest(const QString& folder)
{
    QFile f(folder + QLatin1Char('/') + kManifest);
    Pack pack;
    if (!f.open(QIODevice::ReadOnly))
        return pack;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    pack.id = o.value(QStringLiteral("id")).toString();
    pack.name = o.value(QStringLiteral("name")).toString(pack.id);
    pack.author = o.value(QStringLiteral("author")).toString();
    pack.version = o.value(QStringLiteral("version")).toString();
    pack.description = o.value(QStringLiteral("description")).toString();
    pack.folder = folder;
    for (const QString& sub : kFolders)
        pack.images += int(QDir(folder + QLatin1Char('/') + sub).entryList(QDir::Files).size());
    return pack;
}

Pack builtin()
{
    Pack pack;
    pack.id = defaultId();
    pack.name = QCoreApplication::translate("IconPacks", "Kaint (predeterminado)");
    pack.author = QStringLiteral("SSSerch");
    pack.description = QCoreApplication::translate("IconPacks", "Los iconos de siempre de Kaint.");
    return pack;
}

// Imágenes del paquete activo ("icons/archivo.png" → ruta), leídas una vez al activarlo.
struct ActiveState {
    bool loaded = false;
    QString id;
    QHash<QString, QString> files;
    int generation = 0;
};

ActiveState& state()
{
    static ActiveState s;
    return s;
}

void load(ActiveState& s)
{
    s.loaded = true;
    s.files.clear();
    s.id = QSettings().value(QStringLiteral("settings/iconPack"), defaultId()).toString();
    if (s.id == defaultId() || !validId(s.id))
        return;
    const QString folder = root() + QLatin1Char('/') + s.id;
    if (!QFile::exists(folder + QLatin1Char('/') + kManifest)) {
        s.id = defaultId();
        return;
    }
    for (const QString& sub : kFolders) {
        const QDir dir(folder + QLatin1Char('/') + sub);
        for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.webp")}, QDir::Files))
            s.files.insert(sub + QLatin1Char('/') + info.completeBaseName(), info.absoluteFilePath());
    }
}

} // namespace

QString defaultId()
{
    return QStringLiteral("kaint-default");
}

QString root()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/IconPacks");
}

QList<Pack> installed()
{
    QList<Pack> packs;
    const QDir dir(root());
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const Pack pack = readManifest(dir.filePath(sub));
        if (!pack.id.isEmpty() && pack.id == sub)
            packs << pack;
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(packs.begin(), packs.end(), [&](const Pack& a, const Pack& b) { return collator.compare(a.name, b.name) < 0; });
    packs.prepend(builtin());
    return packs;
}

QString activeId()
{
    ActiveState& s = state();
    if (!s.loaded)
        load(s);
    return s.id;
}

Pack active()
{
    const QString id = activeId();
    for (const Pack& p : installed())
        if (p.id == id)
            return p;
    return builtin();
}

void activate(const QString& id)
{
    QSettings().setValue(QStringLiteral("settings/iconPack"), id);
    ActiveState& s = state();
    load(s);
    ++s.generation;
    QPixmapCache::clear();
    emit notifier()->changed();
}

QString install(const QString& zipPath, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return QString();
    };
    QFile f(zipPath);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QCoreApplication::translate("IconPacks", "No se pudo abrir el archivo."));
    if (f.size() > kMaxZip)
        return fail(QCoreApplication::translate("IconPacks", "El archivo es demasiado grande."));
    const ZipReader zip(f.readAll());
    if (!zip.isValid())
        return fail(QCoreApplication::translate("IconPacks", "No es un archivo ZIP válido."));

    // asset-pack.json en la raíz o dentro de una sola carpeta, como los de paint.js.
    QString prefix;
    bool found = false;
    for (const QString& name : zip.names()) {
        if (name == kManifest || (name.endsWith(QLatin1Char('/') + kManifest) && name.count(QLatin1Char('/')) == 1)) {
            prefix = name.left(name.size() - kManifest.size());
            found = true;
            break;
        }
    }
    if (!found)
        return fail(QCoreApplication::translate("IconPacks", "Falta asset-pack.json: no es un paquete de iconos."));
    const QByteArray manifest = zip.read(prefix + kManifest, 1024 * 1024);
    const QJsonObject o = QJsonDocument::fromJson(manifest).object();
    const QString id = o.value(QStringLiteral("id")).toString();
    if (o.value(QStringLiteral("formatVersion")).toInt(1) != 1)
        return fail(QCoreApplication::translate("IconPacks", "El paquete usa una versión del formato que Kaint no conoce."));
    if (!validId(id))
        return fail(QCoreApplication::translate("IconPacks", "El identificador del paquete no es válido."));
    if (id == defaultId())
        return fail(QCoreApplication::translate("IconPacks", "Ese es el identificador del paquete predeterminado."));

    // Se descomprime en una carpeta temporal y sustituye a la anterior al terminar.
    const QString target = root() + QLatin1Char('/') + id;
    const QString temp = target + QStringLiteral(".installing");
    QDir(temp).removeRecursively();
    if (!QDir().mkpath(temp))
        return fail(QCoreApplication::translate("IconPacks", "No se pudo crear la carpeta del paquete."));
    {
        QSaveFile out(temp + QLatin1Char('/') + kManifest);
        if (!out.open(QIODevice::WriteOnly) || out.write(manifest) != manifest.size() || !out.commit()) {
            QDir(temp).removeRecursively();
            return fail(QCoreApplication::translate("IconPacks", "No se pudo guardar el paquete."));
        }
    }
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9._-]+\\.(png|webp)$"),
                                             QRegularExpression::CaseInsensitiveOption);
    int images = 0;
    for (const QString& name : zip.names()) {
        if (!name.startsWith(prefix))
            continue;
        const QStringList parts = name.mid(prefix.size()).split(QLatin1Char('/'));
        // Solo <carpeta>/<archivo> con un nombre sencillo: nada de rutas que salgan de la carpeta.
        if (parts.size() != 2 || !kFolders.contains(parts[0]) || !safeName.match(parts[1]).hasMatch())
            continue;
        const QByteArray data = zip.read(name, kMaxImage);
        if (data.isEmpty())
            continue;
        QDir().mkpath(temp + QLatin1Char('/') + parts[0]);
        QSaveFile out(temp + QLatin1Char('/') + parts[0] + QLatin1Char('/') + parts[1]);
        if (out.open(QIODevice::WriteOnly) && out.write(data) == data.size() && out.commit())
            ++images;
    }
    const bool wasActive = activeId() == id;
    QDir(target).removeRecursively();
    if (!QDir().rename(temp, target)) {
        QDir(temp).removeRecursively();
        return fail(QCoreApplication::translate("IconPacks", "No se pudo guardar el paquete."));
    }
    if (images == 0 && error)
        *error = QCoreApplication::translate("IconPacks", "El paquete no trae ninguna imagen que Kaint pueda usar.");
    if (wasActive)
        activate(id);
    return id;
}

bool remove(const QString& id)
{
    if (id == defaultId() || !validId(id))
        return false;
    if (activeId() == id)
        activate(defaultId());
    return QDir(root() + QLatin1Char('/') + id).removeRecursively();
}

QString file(const QString& folder, const QString& name)
{
    ActiveState& s = state();
    if (!s.loaded)
        load(s);
    if (s.files.isEmpty())
        return {};
    return s.files.value(folder + QLatin1Char('/') + name);
}

int generation()
{
    return state().generation;
}

Notifier* notifier()
{
    static Notifier n;
    return &n;
}

} // namespace IconPacks
