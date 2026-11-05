#pragma once

#include <QHash>
#include <QImage>
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
    // Nombres, identificadores e iconos de sus efectos (en el mismo orden).
    QStringList effects;
    QStringList effectIds;
    QList<QImage> effectIcons;
    // Efectos que el usuario desactivó (el resto del complemento sigue cargado).
    QStringList disabledEffects;
    QStringList errors;

    bool effectEnabled(const QString& effectId) const { return !disabledEffects.contains(effectId); }
};

// Busca, carga y descarga los complementos de efectos: · Bibliotecas .so con la API C de Kaint
// (KaintPlugin.h). · Filtros .wasm de PhotoCraft y looks (.cube, .3dl) y ajustes de revelado
// (.xmp) de LightCraft (CraftPlugins).
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
    // Activa o desactiva un solo efecto de un complemento (sus menús lo muestran u ocultan).
    void setEffectEnabled(const QString& pluginId, const QString& effectId, bool on);

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
    QString effectsKey(const PluginInfo& info) const;
    // Añade a los menús los efectos cargados del complemento que estén activados.
    void registerEffects(const PluginInfo& info, const QList<Effect*>& effects) const;
    void readNative(PluginInfo& info, bool keepLoaded);
    void readCraft(PluginInfo& info, bool keepLoaded);
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
