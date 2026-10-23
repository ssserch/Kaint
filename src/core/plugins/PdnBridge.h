#pragma once

#include "core/effects/Effect.h"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>

// Comunicación con el proceso auxiliar kaint-pdn-host.exe (con mono en Linux, con .NET
// Framework en Windows), que carga los complementos de efectos de Paint.NET.
class PdnBridge {
public:
    static PdnBridge& instance();
    ~PdnBridge();

    // Ruta de kaint-pdn-host.exe (junto al ejecutable, en share/kaint o en KAINT_PDN_HOST) y
    // de mono; vacías si no están.
    static QString hostPath();
    static QString monoPath();
    static bool available(QString* why = nullptr);

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
    PdnBridge() = default;
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

    std::mutex m_mutex;
#ifdef Q_OS_WIN
    std::atomic<void*> m_process{nullptr};
    void* m_toHost = nullptr;
    void* m_fromHost = nullptr;
    void* m_mapping = nullptr;
#else
    std::atomic_int m_pid{-1};
    int m_socket = -1;
    int m_shmFd = -1;
#endif
    QByteArray m_buffer;
    uchar* m_shm = nullptr;
    size_t m_shmSize = 0;
    QString m_shmPath;
    qint64 m_srcKey = 0;
    QSize m_srcSize;
};
