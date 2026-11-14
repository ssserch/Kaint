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
    // El modo colaborativo.
    bool collab = false;
    bool collabHost = false;
    // Invitado que espera a que el anfitrión le deje entrar.
    bool collabWaiting = false;
    bool collabP2P = false;
    // Cuántas personas hay en la sesión (con el anfitrión).
    int collabPeople = 0;
    QString collabName;
    // P2P: el código de la sesión (con él se invita desde Discord; nunca se muestra).
    QString collabCode;
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
    // Modo colaborativo: que se vea la sesión (y cuántas personas hay) y su nombre.
    bool showCollab = true;
    bool showCollabName = true;
    // Invitar por Discord a una sesión P2P (y «Pedir unirse»).
    bool invites = true;
    // Quién pide unirse desde Discord: preguntar, aceptar siempre o rechazar siempre.
    enum class JoinRequests { Ask = 0, Accept = 1, Decline = 2 };
    JoinRequests joinRequests = JoinRequests::Ask;
    // Plazas del grupo que muestra Discord («2 de 8»).
    int partyMax = 8;
    // Que Discord pueda abrir Kaint al aceptar una invitación si no está abierto.
    bool registerLaunch = true;
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
    // 'minimal': solo el archivo y el tiempo (si Discord rechazó la actividad completa).
    static QJsonObject buildActivity(const PresenceInfo& info, const PresenceOptions& options, qint64 startSeconds,
                                     bool minimal = false);
    static QByteArray frame(int opcode, const QJsonObject& payload);
    static QStringList socketPaths();
    // El secreto para unirse que viaja por Discord ("kaint-p2p:CÓDIGO") y al revés: el código de la
    // sesión, o vacío si no es de Kaint.
    static QString joinSecret(const QString& code);
    static QString codeFromSecret(const QString& secret);
    // Para que Discord abra Kaint al aceptar una invitación si no está abierto (como Discord_Register
    // de la biblioteca oficial: el protocolo discord-<ID> en Windows y Linux, el .json en macOS).
    static bool registerLaunchCommand(const QString& clientId, QString* error = nullptr);

    // Responde a quien pidió unirse desde Discord (con «Pedir unirse»).
    void replyJoinRequest(const QString& userId, bool accept);

    // Intervalos (las pruebas los acortan).
    void setIntervals(int minSendMs, int retryMs);

signals:
    void statusChanged();
    // Se aceptó en Discord una invitación (o una solicitud) para unirse a una sesión P2P.
    void joinInvite(const QString& code);
    // Alguien pide unirse a la sesión desde Discord (con «Preguntar»).
    void joinRequest(const QString& userId, const QString& userName);

private:
    void setStatus(Status status, const QString& error = QString());
    void connectToDiscord();
    void disconnectFromDiscord();
    void onReadyRead();
    void handleFrame(int opcode, const QByteArray& data);
    void scheduleSend();
    void sendActivity();
    void sendCommand(const QString& cmd, const QJsonObject& args, const QString& evt = QString());
    void subscribeInvites();

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
    // Discord rechazó la actividad completa: se envía la sencilla y se muestra el motivo.
    bool m_minimal = false;
    QString m_activityError;
    // Ya se registró el protocolo para abrir Kaint (una vez por ID).
    QString m_registered;
};
