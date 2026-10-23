#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <QTimer>

class QLocalSocket;

// Lo que se cuenta a Discord del trabajo actual.
struct PresenceInfo {
    bool hasDocument = false;
    QString fileName;
    QString fileType;
    QSize size;
    int layerIndex = 0;
    int layerCount = 0;
    QString layerName;
    QString tool;
    int documents = 0;
    bool operator==(const PresenceInfo&) const = default;
};

// Opciones de Configuración > Discord.
struct PresenceOptions {
    bool enabled = true;
    bool showFile = true;
    bool showLayer = true;
    bool showFileInfo = true;
    bool showTool = true;
    bool showElapsed = true;
    QString clientId;
    bool operator==(const PresenceOptions&) const = default;
};

// Rich Presence de Discord por el canal IPC local (el socket discord-ipc-N que abre la
// aplicación de Discord, también la de Flatpak o Snap).
class DiscordPresence : public QObject {
    Q_OBJECT
public:
    enum class Status { Disabled, NotConfigured, NoDiscord, Connecting, Connected, Error };

    explicit DiscordPresence(QObject* parent = nullptr);
    ~DiscordPresence() override;

    // ID de la aplicación de discord.json (al compilar, o el de ~/.config/Kaint/discord.json);
    // vacío si no es un ID válido.
    static QString configuredClientId();
    static QString largeImageKey();
    static bool isValidClientId(const QString& id);

    void setOptions(const PresenceOptions& options);
    void update(const PresenceInfo& info);
    Status status() const { return m_status; }
    QString statusText() const;
    QString effectiveClientId() const;

    // Partes del protocolo (también para las pruebas).
    static QJsonObject buildActivity(const PresenceInfo& info, const PresenceOptions& options, qint64 startSeconds);
    static QByteArray frame(int opcode, const QJsonObject& payload);
    static QStringList socketPaths();

    // Intervalos (las pruebas los acortan).
    void setIntervals(int minSendMs, int retryMs);

signals:
    void statusChanged();

private:
    void setStatus(Status status, const QString& error = QString());
    void connectToDiscord();
    void disconnectFromDiscord();
    void onReadyRead();
    void handleFrame(int opcode, const QByteArray& data);
    void scheduleSend();
    void sendActivity();

    QLocalSocket* m_socket = nullptr;
    QByteArray m_buffer;
    bool m_ready = false;
    bool m_dirty = true;
    Status m_status = Status::Disabled;
    QString m_error;
    PresenceOptions m_options;
    PresenceInfo m_info;
    qint64 m_start = 0;
    QTimer m_retry;
    QTimer m_send;
    QElapsedTimer m_lastSend;
    int m_minSendMs = 4000;
    int m_nonce = 0;
};
