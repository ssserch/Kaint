#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <thread>

class Effect;

// Información de un complemento (Configuración > Complementos).
struct PluginInfo {
    QString id;
    QString file;
    QString name;
    QString kind;
    QString version;
    QString author;
    QString copyright;
    QString website;
    QString description;
    bool enabled = false;
    bool loaded = false;
    bool pending = false;
    QStringList effects;
    QStringList errors;
};

// Busca, carga y descarga los complementos de efectos: · Bibliotecas .so con la API C de Kaint
// (KaintPlugin.h).
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager* instance();
    ~PluginManager() override;

    // Carpetas donde se buscan complementos.
    QStringList searchPaths() const;
    QString userPath() const;

    // Copia complementos (.so o .dll, y sus bibliotecas de apoyo) a la carpeta del usuario.
    QStringList install(const QStringList& files, bool replace, QStringList* errors = nullptr);
    QStringList installedAlready(const QStringList& files) const;

    // Busca los complementos y carga los activados. Se puede volver a llamar.
    void scan();
    const QList<PluginInfo>& plugins() const { return m_plugins; }
    void setEnabled(const QString& id, bool on);

    // Pruebas: sustituye las carpetas de búsqueda y la caché.
    void setPathsForTesting(const QStringList& userPaths, const QString& cacheFile);
    void waitForInspection();
    void shutdown();

signals:
    void changed();

private:
    explicit PluginManager(QObject* parent = nullptr);
    struct Candidate {
        QString path;
        QString root;
    };
    QList<Candidate> findCandidates() const;
    QString settingsKey(const PluginInfo& info) const;
    void readNative(PluginInfo& info, bool keepLoaded);
    void fillFromInspection(PluginInfo& info, const QJsonObject& reply);
    void load(PluginInfo& info);
    void unload(PluginInfo& info);
    void startInspection(const QStringList& paths);
    void onInspected(const QString& path, const QJsonObject& reply, const QString& error);
    QJsonObject cachedReply(const QString& path) const;
    void storeReply(const QString& path, const QJsonObject& reply);
    QString cacheFile() const;
    void loadCache();
    void saveCache() const;

    QList<PluginInfo> m_plugins;
    QHash<QString, QString> m_roots;
    QStringList m_testUserPaths;
    QString m_testCache;
    bool m_testing = false;
    struct Loaded;
    QList<Loaded*> m_loaded;
    QJsonObject m_cache;
    bool m_cacheLoaded = false;
    std::thread m_inspector;
    std::atomic_bool m_stopInspection{false};
    std::atomic_bool m_inspecting{false};
    QStringList m_queued;
};
