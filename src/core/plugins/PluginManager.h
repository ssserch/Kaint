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
    QString id;          // ruta absoluta del archivo: identifica el complemento
    QString file;        // nombre del archivo
    QString name;
    QString kind;        // "Kaint" (biblioteca .so nativa) o "Paint.NET" (ensamblado .NET)
    QString version;
    QString author;
    QString copyright;
    QString website;
    QString description;
    bool enabled = false;
    bool loaded = false;
    bool pending = false; // todavía se está examinando (Paint.NET)
    QStringList effects;  // nombres de los efectos que añade
    QStringList errors;   // problemas al cargarlo
};

// Busca, carga y descarga los complementos de efectos:
//   · Bibliotecas .so con la API C de Kaint (KaintPlugin.h).
//   · Ensamblados .dll de Paint.NET, a través del puente .NET (kaint-pdn-host).
// Los efectos cargados se añaden a Effects::all() y aparecen en los menús.
//
// Los ensamblados de Paint.NET se examinan una vez en segundo plano y la
// descripción se guarda en caché (~/.cache/Kaint), así que al arrancar no
// hace falta iniciar mono: el proceso auxiliar arranca al usar un efecto.
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager* instance();
    ~PluginManager() override;

    // Carpetas donde se buscan complementos.
    QStringList searchPaths() const;
    QString userPath() const; // ~/.local/share/Kaint/Effects

    // Copia complementos (.so o .dll, y sus bibliotecas de apoyo) a la carpeta
    // del usuario. Con 'replace' sustituye los que ya existan; si no, los omite.
    // Devuelve las rutas copiadas; los fallos van a 'errors'.
    QStringList install(const QStringList& files, bool replace, QStringList* errors = nullptr);
    // Archivos de 'files' que ya están en la carpeta del usuario.
    QStringList installedAlready(const QStringList& files) const;

    // Busca los complementos y carga los activados. Se puede volver a llamar.
    void scan();
    const QList<PluginInfo>& plugins() const { return m_plugins; }
    void setEnabled(const QString& id, bool on);

    // Pruebas: sustituye las carpetas de búsqueda y la caché.
    void setPathsForTesting(const QStringList& userPaths, const QString& cacheFile);
    // Espera a que termine el examen en segundo plano (pruebas).
    void waitForInspection();
    // Detiene el examen en segundo plano y el proceso auxiliar (al salir).
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
    QHash<QString, QString> m_roots; // id -> carpeta de búsqueda donde se encontró
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
    QStringList m_queued; // pendientes de examinar cuando acabe el examen en curso
};
