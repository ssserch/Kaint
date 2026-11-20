#pragma once

#include "core/effects/Effect.h"

#include <QList>

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>

// Comunicación con los procesos auxiliares que cargan los complementos de efectos de Paint.NET
// (.dll), según el sistema de complementos elegido (Configuración → Complementos):
//   - .NET: kaint-pdn5-host (PdnBridge.dll) con el .NET moderno (CoreCLR) y los ensamblados de
//     Paint.NET 5 de paint.rs (third_party/pdn5): complementos de 3.x, 4.x y 5.x, también los de GPU
//     (con OpenD2D) y las ventanas propias leídas sin mostrarlas. Es el de fábrica.
//   - OpenD2D: kaint-pdn-host.exe con mono (en Windows, con su .NET Framework) y las bibliotecas de
//     OpenD2D (Direct2D, DirectWrite y WIC de código abierto).
//   - Wine: kaint-pdn-host.exe dentro de Wine (con su wine-mono), como un programa de Windows.
// Con .NET, lo que no funcione se ejecuta con mono y, con el respaldo de Wine, con Wine (chain()).
class PdnBridge {
public:
    enum class Runtime { OpenD2D = 0, Wine = 1, DotNet = 2 };

    // El proceso del sistema elegido; forRuntime, el de otro (con el respaldo de Wine pueden estar
    // abiertos los dos a la vez). En Windows hay uno solo.
    static PdnBridge& instance();
    static PdnBridge& forRuntime(Runtime runtime);
    ~PdnBridge();

    // Ruta de kaint-pdn-host.exe (junto al ejecutable, en share/kaint o en KAINT_PDN_HOST), de
    // mono y de wine; vacías si no están.
    static QString hostPath();
    // Los complementos de Paint.NET activados (PluginManager los da al cambiar): el proceso auxiliar
    // los pone en su carpeta Effects, donde los buscan complementos como Random Effect.
    static void setEffectsFolder(const QStringList& assemblies);
    static QString monoPath();
    static QString winePath();
    // dotnet y el proceso auxiliar de .NET (PdnBridge.dll en pdnhost5/, share/kaint/pdnhost5 o
    // KAINT_PDN5_HOST); vacías si no están.
    static QString dotnetPath();
    // El prefijo de Wine que se usa (vacío: el de siempre, WINEPREFIX o ~/.wine).
    static QString winePrefix();
    static QString dotnetHostPath();
    // Carpeta con las bibliotecas de OpenD2D (libd2d1, libdwrite y libwindowscodecs) que se
    // compilaron con Kaint (o la de KAINT_OPEND2D); vacía si no están.
    static QString openD2DPath();
    // Versión de OpenD2D que se compiló (el archivo VERSION junto a sus bibliotecas).
    static QString openD2DVersion();
    static bool available(QString* why = nullptr);
    static bool availableFor(Runtime runtime, QString* why = nullptr);

    // El sistema de complementos (en Windows no se usa: siempre el .NET Framework). Cambiarlo
    // cierra el proceso auxiliar: el próximo efecto lo abre con el nuevo.
    static bool runtimeSelectable();
    static void setRuntime(Runtime runtime);
    static Runtime runtime();
    static QString runtimeName(Runtime runtime);
    // Nombre corto para guardar en la caché y en la configuración: "dotnet", "mono" o "wine".
    static QString runtimeKey(Runtime runtime);
    static Runtime runtimeFromKey(const QString& key, Runtime fallback);
    // Los sistemas que se prueban, en orden: el elegido y, detrás, los de respaldo (con .NET, mono
    // si está; con el respaldo de Wine, Wine).
    static QList<Runtime> chain();

    // Respaldo de Wine (con OpenD2D): lo que no funciona con mono (un complemento que no carga, o
    // un efecto que falla al aplicarse) se ejecuta con Wine, solo eso. Activo si se pidió, el
    // sistema elegido es OpenD2D y Wine está instalado.
    static void setWineFallback(bool on);
    static bool wineFallback();
    // Lo que distingue lo que se sabe de cada complemento con este sistema (la caché de
    // PluginManager): «OpenD2D», «OpenD2D+Wine» o «Wine».
    static QString cacheTag();
    // Los efectos que ya se sabe que necesitan Wine (se recuerdan entre sesiones).
    static bool effectNeedsWine(const QString& effectId);
    static void rememberWineEffect(const QString& effectId);
    // El sistema con el que funcionó un efecto que no va con el primero de chain() (-1 si ninguno).
    static int rememberedRuntime(const QString& effectId);
    static void rememberEffectRuntime(const QString& effectId, Runtime runtime);
    // Una cadena que cambia cuando cambia algún proceso auxiliar (para la caché de PluginManager).
    static QString hostStamp();
    // Cierra (o mata) los procesos de los dos sistemas.
    static void shutdownAll();
    static void abortAll();

    // Saludo al proceso auxiliar: con qué se ejecuta y si los complementos tienen Direct2D
    // ("runtime", "direct2d"...). Vacío si no arranca (el porqué, en 'error').
    QJsonObject hello(QString* error);

    // Descripción de un ensamblado: efectos, parámetros, autor... (bloquea).
    QJsonObject inspect(const QString& path, QString* error);
    // 'selection', si no es nula, va como polígonos (los complementos que miran su forma la ven).
    bool render(const QString& assembly, const QString& effectId, const QJsonObject& values, const QImage& src,
                QImage& dst, const QRect& roi, const EffectContext& ctx, const std::atomic_bool& cancel,
                QString* error, const Selection* selection = nullptr);
    bool showDialog(const QString& assembly, const QString& effectId, const QString& title, const QImage& src,
                    const QRect& roi, const EffectContext& ctx, bool* accepted, QString* error);
    void shutdown();
    void abort();

private:
    explicit PdnBridge(Runtime runtime)
        : m_runtime(runtime)
    {
    }
    bool ensureStarted(QString* error);
    // Plazo para examinar o saludar: más largo si Wine aún tiene que crear su prefijo.
    int startTimeoutMs() const;
    void stop();
    bool writeLine(const QByteArray& line);
    bool readResponse(QJsonObject* out, int timeoutMs, const std::atomic_bool* cancel, EffectProgress* progress,
                      int progressTotal, QString* error, const std::function<void()>& onPreview = {});
    bool prepareShared(const QImage& src, QString* error);
    QJsonObject request(const QJsonObject& req, int timeoutMs, QString* error);
    // Según el sistema: leer lo que haya llegado (1 sí, 0 nada en el plazo, -1 se cerró), crear la
    // memoria compartida y dejar que el diálogo del complemento pase al frente.
    int readChunk(int timeoutMs);
    bool mapShared(size_t bytes, QString* error);
    void allowDialogFocus();
    // Una ruta de este sistema como la ve el proceso auxiliar (dentro de Wine, Z:\...).
    QString hostSidePath(const QString& path) const;

    std::mutex m_mutex;
    // El sistema de este proceso (en Windows, siempre el .NET Framework).
    const Runtime m_runtime;
#ifdef Q_OS_WIN
    std::atomic<void*> m_process{nullptr};
    void* m_toHost = nullptr;
    void* m_fromHost = nullptr;
    void* m_mapping = nullptr;
#else
    std::atomic_int m_pid{-1};
    int m_socket = -1;
    int m_shmFd = -1;
    // Con qué se arrancó el proceso que está abierto.
    Runtime m_startedWith = Runtime::OpenD2D;
#endif
    QByteArray m_buffer;
    uchar* m_shm = nullptr;
    size_t m_shmSize = 0;
    QString m_shmPath;
    qint64 m_srcKey = 0;
    QSize m_srcSize;
};
