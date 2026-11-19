#pragma once

#include "core/effects/Effect.h"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>

// Comunicación con el proceso auxiliar kaint-pdn-host.exe, que carga los complementos de efectos
// de Paint.NET (.dll). En Windows lo ejecuta el .NET Framework del sistema; en Linux, macOS y
// FreeBSD, según el sistema de complementos elegido (Configuración → Complementos):
//   - OpenD2D: mono, con las bibliotecas de OpenD2D (Direct2D, DirectWrite y
//     WIC de código abierto) en lugar de las de Windows que piden los complementos.
//   - Wine: el mismo proceso dentro de Wine (con su wine-mono), como un programa de Windows.
class PdnBridge {
public:
    enum class Runtime { OpenD2D = 0, Wine = 1 };

    // El proceso del sistema elegido; forRuntime, el de otro (con el respaldo de Wine pueden estar
    // abiertos los dos a la vez). En Windows hay uno solo.
    static PdnBridge& instance();
    static PdnBridge& forRuntime(Runtime runtime);
    ~PdnBridge();

    // Ruta de kaint-pdn-host.exe (junto al ejecutable, en share/kaint o en KAINT_PDN_HOST), de
    // mono y de wine; vacías si no están.
    static QString hostPath();
    static QString monoPath();
    static QString winePath();
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
    // Cierra (o mata) los procesos de los dos sistemas.
    static void shutdownAll();
    static void abortAll();

    // Saludo al proceso auxiliar: con qué se ejecuta y si los complementos tienen Direct2D
    // ("runtime", "direct2d"...). Vacío si no arranca (el porqué, en 'error').
    QJsonObject hello(QString* error);

    // Descripción de un ensamblado: efectos, parámetros, autor... (bloquea).
    QJsonObject inspect(const QString& path, QString* error);
    bool render(const QString& assembly, const QString& effectId, const QJsonObject& values, const QImage& src,
                QImage& dst, const QRect& roi, const EffectContext& ctx, const std::atomic_bool& cancel,
                QString* error);
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
    void stop();
    bool writeLine(const QByteArray& line);
    bool readResponse(QJsonObject* out, int timeoutMs, const std::atomic_bool* cancel, EffectProgress* progress,
                      int progressTotal, QString* error);
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
