#pragma once

#include "core/effects/Effect.h"

#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>

// Comunicación con el proceso auxiliar kaint-pdn-host.exe (mono), que carga
// los complementos de efectos de Paint.NET. Un único proceso para todos,
// arrancado a demanda; las peticiones se atienden de una en una (mutex) y se
// pueden hacer desde cualquier hilo. Los píxeles viajan por memoria
// compartida en BGRA de alfa directo, el formato de las capas de Kaint.
class PdnBridge {
public:
    static PdnBridge& instance();
    ~PdnBridge();

    // Ruta de kaint-pdn-host.exe (junto al ejecutable, en share/kaint o en
    // KAINT_PDN_HOST) y de mono; vacías si no están.
    static QString hostPath();
    static QString monoPath();
    // true si se pueden cargar complementos de Paint.NET; si no, el motivo.
    static bool available(QString* why = nullptr);

    // Descripción de un ensamblado: efectos, parámetros, autor... (bloquea).
    QJsonObject inspect(const QString& path, QString* error);
    // Calcula el efecto en 'dst' (copia de 'src') dentro de 'roi'. false si
    // se cancela (error vacío) o falla.
    bool render(const QString& assembly, const QString& effectId, const QJsonObject& values, const QImage& src,
                QImage& dst, const QRect& roi, const EffectContext& ctx, const std::atomic_bool& cancel,
                QString* error);
    // Muestra el diálogo WinForms propio del complemento. false si falla.
    bool showDialog(const QString& assembly, const QString& effectId, const QString& title, const QImage& src,
                    const QRect& roi, const EffectContext& ctx, bool* accepted, QString* error);
    // Termina el proceso auxiliar (se vuelve a arrancar en la siguiente petición).
    void shutdown();
    // Mata el proceso sin esperar al mutex: desbloquea una petición en curso
    // (al salir de Kaint).
    void abort();

private:
    PdnBridge() = default;
    bool ensureStarted(QString* error);
    void stop();
    bool writeLine(const QByteArray& line);
    // Lee la siguiente línea JSON (las de progreso las pasa a 'progress').
    // timeoutMs < 0: sin límite. Con 'cancel', al activarse envía la orden
    // de cancelar y sigue esperando la respuesta un tiempo prudente.
    bool readResponse(QJsonObject* out, int timeoutMs, const std::atomic_bool* cancel, EffectProgress* progress,
                      int progressTotal, QString* error);
    bool prepareShared(const QImage& src, QString* error);
    QJsonObject request(const QJsonObject& req, int timeoutMs, QString* error);

    std::mutex m_mutex;
    std::atomic_int m_pid{-1};
    int m_socket = -1;
    QByteArray m_buffer;
    // Memoria compartida: origen y destino seguidos.
    int m_shmFd = -1;
    uchar* m_shm = nullptr;
    size_t m_shmSize = 0;
    QString m_shmPath;
    qint64 m_srcKey = 0;
    QSize m_srcSize;
};
