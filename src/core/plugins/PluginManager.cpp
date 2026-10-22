#include "core/plugins/PluginManager.h"

#include "core/effects/Effect.h"
#include "core/plugins/KaintPlugin.h"
#include "core/plugins/PdnBridge.h"
#include "core/plugins/PluginEffects.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

struct PluginManager::Loaded {
    QString id;
    QLibrary* library = nullptr;
    QList<Effect*> effects;
};

namespace {

// Versión del formato de la caché: al cambiarla se vuelven a examinar todos.
constexpr int kCacheVersion = 1;

QStringList pluginFiles(const QString& dir)
{
    return QDir(dir).entryList({QStringLiteral("*.so"), QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
}

// Firma del archivo para saber si la caché sigue valiendo.
QString fileStamp(const QString& path)
{
    const QFileInfo fi(path);
    return QStringLiteral("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

} // namespace

PluginManager* PluginManager::instance()
{
    static PluginManager* manager = new PluginManager;
    return manager;
}

PluginManager::PluginManager(QObject* parent)
    : QObject(parent)
{
    if (QCoreApplication::instance())
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &PluginManager::shutdown);
}

PluginManager::~PluginManager()
{
    shutdown();
}

QString PluginManager::userPath() const
{
    if (m_testing && !m_testUserPaths.isEmpty())
        return m_testUserPaths.first();
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Kaint/Effects");
}

QStringList PluginManager::searchPaths() const
{
    return m_testing ? m_testUserPaths : QStringList{userPath()};
}

QStringList PluginManager::installedAlready(const QStringList& files) const
{
    QStringList found;
    for (const QString& f : files)
        if (QFileInfo::exists(userPath() + QLatin1Char('/') + QFileInfo(f).fileName()))
            found << f;
    return found;
}

QStringList PluginManager::install(const QStringList& files, bool replace, QStringList* errors)
{
    QStringList copied;
    const QString dir = userPath();
    QDir().mkpath(dir);
    for (const QString& f : files) {
        const QString dest = dir + QLatin1Char('/') + QFileInfo(f).fileName();
        if (QFileInfo(f).absoluteFilePath() == QFileInfo(dest).absoluteFilePath())
            continue;
        if (QFileInfo::exists(dest) && !replace)
            continue;
        // Se copia aparte y luego se renombra: una biblioteca ya cargada sigue
        // usando el archivo anterior hasta que se descarga.
        const QString tmp = dest + QStringLiteral(".part");
        QFile::remove(tmp);
        if (!QFile::copy(f, tmp) || (QFileInfo::exists(dest) && !QFile::remove(dest)) || !QFile::rename(tmp, dest)) {
            QFile::remove(tmp);
            if (errors)
                *errors << QFileInfo(f).fileName();
            continue;
        }
        copied << dest;
    }
    return copied;
}

void PluginManager::setPathsForTesting(const QStringList& userPaths, const QString& cacheFile)
{
    m_testing = true;
    m_testUserPaths = userPaths;
    m_testCache = cacheFile;
    m_cacheLoaded = false;
}

QList<PluginManager::Candidate> PluginManager::findCandidates() const
{
    // Cada carpeta y un nivel de subcarpetas (una por complemento); las
    // bibliotecas de apoyo más profundas no se listan. Un archivo repetido
    // (mismo nombre) solo cuenta la primera vez.
    QList<Candidate> found;
    QSet<QString> names;
    for (const QString& root : searchPaths()) {
        if (!QFileInfo(root).isDir())
            continue;
        QStringList dirs{root};
        for (const QString& sub : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            dirs << root + QLatin1Char('/') + sub;
        for (const QString& dir : std::as_const(dirs)) {
            for (const QString& file : pluginFiles(dir)) {
                if (names.contains(file.toLower()))
                    continue;
                names.insert(file.toLower());
                Candidate c;
                c.path = QFileInfo(dir + QLatin1Char('/') + file).absoluteFilePath();
                c.root = root;
                found << c;
            }
        }
    }
    return found;
}

QString PluginManager::settingsKey(const PluginInfo& info) const
{
    // Ruta relativa a su carpeta: no cambia si Kaint se instala en otro sitio.
    const QString relative = QDir(m_roots.value(info.id)).relativeFilePath(info.id);
    const QByteArray encoded = (QStringLiteral("u:") + relative)
                                   .toUtf8()
                                   .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QStringLiteral("plugins/enabled/") + QString::fromLatin1(encoded);
}

// --- Caché de descripciones de Paint.NET ------------------------------------

QString PluginManager::cacheFile() const
{
    if (m_testing)
        return m_testCache;
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/pdn-plugins.json");
}

void PluginManager::loadCache()
{
    if (m_cacheLoaded)
        return;
    m_cacheLoaded = true;
    m_cache = {};
    QFile f(cacheFile());
    if (cacheFile().isEmpty() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    // Si cambia el proceso auxiliar (otra versión de Kaint) se vuelve a examinar todo.
    if (root.value(QStringLiteral("version")).toInt() != kCacheVersion
        || root.value(QStringLiteral("host")).toString() != fileStamp(PdnBridge::hostPath()))
        return;
    m_cache = root.value(QStringLiteral("plugins")).toObject();
}

void PluginManager::saveCache() const
{
    if (cacheFile().isEmpty())
        return;
    QDir().mkpath(QFileInfo(cacheFile()).absolutePath());
    QSaveFile f(cacheFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    const QJsonObject root{{QStringLiteral("version"), kCacheVersion},
                           {QStringLiteral("host"), fileStamp(PdnBridge::hostPath())},
                           {QStringLiteral("plugins"), m_cache}};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}

QJsonObject PluginManager::cachedReply(const QString& path) const
{
    const QJsonObject entry = m_cache.value(path).toObject();
    if (entry.value(QStringLiteral("stamp")).toString() != fileStamp(path))
        return {};
    return entry.value(QStringLiteral("reply")).toObject();
}

void PluginManager::storeReply(const QString& path, const QJsonObject& reply)
{
    m_cache.insert(path, QJsonObject{{QStringLiteral("stamp"), fileStamp(path)}, {QStringLiteral("reply"), reply}});
}

// --- Búsqueda ----------------------------------------------------------------

void PluginManager::scan()
{
    loadCache();
    const QList<Candidate> candidates = findCandidates();
    QSet<QString> present;
    for (const Candidate& c : candidates)
        present.insert(c.path);

    // Los que ya no están se descargan.
    for (PluginInfo& p : m_plugins)
        if (!present.contains(p.id) && p.loaded)
            unload(p);

    QList<PluginInfo> plugins;
    QStringList toInspect;
    QString unavailable;
    const bool pdnAvailable = PdnBridge::available(&unavailable);
    for (const Candidate& c : candidates) {
        auto existing = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const PluginInfo& p) { return p.id == c.path; });
        if (existing != m_plugins.end() && (existing->loaded || existing->pending)) {
            plugins << *existing;
            continue;
        }
        PluginInfo info;
        info.id = c.path;
        info.file = QFileInfo(c.path).fileName();
        m_roots.insert(info.id, c.root);
        info.enabled = QSettings().value(settingsKey(info), true).toBool();
        if (c.path.endsWith(QLatin1String(".so"))) {
            info.kind = QStringLiteral("Kaint");
            readNative(info, false);
        } else {
            info.kind = QStringLiteral("Paint.NET");
            const QJsonObject reply = cachedReply(c.path);
            if (!reply.isEmpty()) {
                // Las bibliotecas de apoyo (sin efectos) no se muestran.
                if (reply.value(QStringLiteral("library")).toBool())
                    continue;
                fillFromInspection(info, reply);
            } else if (pdnAvailable) {
                info.pending = true;
                toInspect << c.path;
            } else {
                info.errors << unavailable;
            }
        }
        if (info.name.isEmpty())
            info.name = QFileInfo(c.path).completeBaseName();
        plugins << info;
    }
    m_plugins = plugins;
    for (PluginInfo& p : m_plugins)
        if (p.enabled && !p.loaded && !p.pending)
            load(p);
    if (!toInspect.isEmpty())
        startInspection(toInspect);
    emit changed();
}

void PluginManager::readNative(PluginInfo& info, bool keepLoaded)
{
    auto* library = new QLibrary(info.id);
    if (!library->load()) {
        info.errors << library->errorString();
        delete library;
        return;
    }
    auto entry = reinterpret_cast<KaintPluginInfoFunction>(library->resolve(KAINT_PLUGIN_ENTRY));
    const KaintPluginInfo* pi = entry ? entry() : nullptr;
    if (!pi) {
        info.errors << tr("No exporta la función %1: no es un complemento de Kaint.").arg(QLatin1String(KAINT_PLUGIN_ENTRY));
    } else if (pi->api_version != KAINT_PLUGIN_API_VERSION) {
        info.errors << tr("Usa la versión %1 de la API de complementos; Kaint admite la %2.")
                           .arg(pi->api_version)
                           .arg(KAINT_PLUGIN_API_VERSION);
    } else {
        auto str = [](const char* s) { return s ? QString::fromUtf8(s) : QString(); };
        info.name = str(pi->name);
        info.version = str(pi->version);
        info.author = str(pi->author);
        info.description = str(pi->description);
        info.website = str(pi->website);
        info.effects.clear();
        auto* loaded = keepLoaded ? new Loaded : nullptr;
        for (int i = 0; i < pi->effect_count && pi->effects; ++i) {
            info.effects << str(pi->effects[i].name);
            if (loaded)
                loaded->effects << new NativeEffect(pi->effects[i], info.name.isEmpty() ? info.file : info.name);
        }
        if (loaded) {
            loaded->id = info.id;
            loaded->library = library;
            for (Effect* e : std::as_const(loaded->effects))
                Effects::addExternal(e);
            m_loaded << loaded;
            info.loaded = true;
            return;
        }
    }
    library->unload();
    delete library;
}

void PluginManager::fillFromInspection(PluginInfo& info, const QJsonObject& reply)
{
    auto str = [&](const char* key) { return reply.value(QLatin1String(key)).toString(); };
    info.name = str("name").simplified(); // algunos traen saltos de línea
    info.version = str("version");
    info.author = str("author");
    info.copyright = str("copyright");
    info.website = str("website");
    info.description = str("description");
    info.effects.clear();
    for (const QJsonValue& e : reply.value(QStringLiteral("effects")).toArray())
        info.effects << e.toObject().value(QStringLiteral("name")).toString().simplified();
    info.errors.clear();
    for (const QJsonValue& e : reply.value(QStringLiteral("errors")).toArray())
        info.errors << e.toString();
    if (!reply.value(QStringLiteral("ok")).toBool(true) && reply.contains(QStringLiteral("error")))
        info.errors << str("error");
}

void PluginManager::load(PluginInfo& info)
{
    if (info.loaded)
        return;
    if (info.kind == QLatin1String("Kaint")) {
        info.errors.clear();
        readNative(info, true);
        return;
    }
    const QJsonObject reply = cachedReply(info.id);
    if (reply.isEmpty())
        return;
    QString why;
    if (!PdnBridge::available(&why)) {
        if (!info.errors.contains(why))
            info.errors << why;
        return;
    }
    auto* loaded = new Loaded;
    loaded->id = info.id;
    const QString pluginName = info.name.isEmpty() ? info.file : info.name;
    for (const QJsonValue& e : reply.value(QStringLiteral("effects")).toArray())
        loaded->effects << new PdnEffect(info.id, pluginName, e.toObject());
    for (Effect* e : std::as_const(loaded->effects))
        Effects::addExternal(e);
    m_loaded << loaded;
    info.loaded = !loaded->effects.isEmpty();
}

void PluginManager::unload(PluginInfo& info)
{
    for (int i = int(m_loaded.size()) - 1; i >= 0; --i) {
        Loaded* l = m_loaded[i];
        if (l->id != info.id)
            continue;
        for (Effect* e : std::as_const(l->effects)) {
            Effects::removeExternal(e);
            delete e;
        }
        if (l->library) {
            l->library->unload();
            delete l->library;
        }
        delete l;
        m_loaded.removeAt(i);
    }
    info.loaded = false;
}

void PluginManager::setEnabled(const QString& id, bool on)
{
    for (PluginInfo& p : m_plugins) {
        if (p.id != id || p.enabled == on)
            continue;
        p.enabled = on;
        QSettings().setValue(settingsKey(p), on);
        if (on)
            load(p);
        else
            unload(p);
        emit changed();
        return;
    }
}

// --- Examen en segundo plano ---------------------------------------------------

void PluginManager::startInspection(const QStringList& paths)
{
    if (m_inspecting) {
        // Se examinarán al terminar el examen en curso.
        for (const QString& p : paths)
            if (!m_queued.contains(p))
                m_queued << p;
        return;
    }
    if (m_inspector.joinable())
        m_inspector.join();
    m_inspecting = true;
    m_stopInspection = false;
    m_inspector = std::thread([this, paths] {
        for (const QString& path : paths) {
            if (m_stopInspection)
                break;
            QString error;
            const QJsonObject reply = PdnBridge::instance().inspect(path, &error);
            if (m_stopInspection)
                break;
            QMetaObject::invokeMethod(this, [this, path, reply, error] { onInspected(path, reply, error); },
                                      Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(
            this,
            [this] {
                m_inspecting = false;
                saveCache();
                emit changed();
                if (!m_queued.isEmpty()) {
                    const QStringList next = m_queued;
                    m_queued.clear();
                    startInspection(next);
                }
            },
            Qt::QueuedConnection);
    });
}

void PluginManager::onInspected(const QString& path, const QJsonObject& reply, const QString& error)
{
    QJsonObject stored = reply;
    if (stored.isEmpty())
        stored = QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    // Un fallo de comunicación no se guarda: se reintenta la próxima vez.
    if (!reply.isEmpty())
        storeReply(path, stored);
    for (int i = 0; i < m_plugins.size(); ++i) {
        PluginInfo& p = m_plugins[i];
        if (p.id != path)
            continue;
        p.pending = false;
        if (stored.value(QStringLiteral("library")).toBool()) {
            m_plugins.removeAt(i);
            break;
        }
        fillFromInspection(p, stored);
        if (p.name.isEmpty())
            p.name = QFileInfo(path).completeBaseName();
        if (p.enabled)
            load(p);
        break;
    }
    // Avisos agrupados para no reconstruir los menús por cada archivo.
    static QTimer* coalesce = nullptr;
    if (!coalesce) {
        coalesce = new QTimer(this);
        coalesce->setSingleShot(true);
        coalesce->setInterval(250);
        connect(coalesce, &QTimer::timeout, this, [this] {
            saveCache(); // también a medias: si se cierra Kaint no se repite lo examinado
            emit changed();
        });
    }
    coalesce->start();
}

void PluginManager::waitForInspection()
{
    while (m_inspecting) {
        if (m_inspector.joinable())
            m_inspector.join();
        QCoreApplication::processEvents();
    }
    QCoreApplication::processEvents();
}

void PluginManager::shutdown()
{
    m_stopInspection = true;
    PdnBridge::instance().abort();
    if (m_inspector.joinable())
        m_inspector.join();
    PdnBridge::instance().shutdown();
}
