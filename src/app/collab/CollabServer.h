#pragma once

#include "app/collab/CollabProtocol.h"

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QUdpSocket;
class Link;
class WebSocketServer;

namespace Collab {

class P2PHost;

// Un participante de la sesión (el anfitrión es el 0).
struct Peer {
    int id = 0;
    QString name;
    QColor color;
    QString platform; // "desktop" o "web"
    QString app;
    bool host = false;
    QString mode;     // "live" o "confirm"
    QString device;   // huella de su ID
    bool history = false; // puede volver a versiones anteriores (lo da el anfitrión)

    QJsonObject toJson() const;
    static Peer fromJson(const QJsonObject& o);
};

// El servidor de una sesión que hospeda este Kaint: recibe a los invitados (con la contraseña, su
// ID o el permiso del anfitrión), lleva la lista de participantes y reparte los mensajes. El
// anfitrión está en este mismo programa: lo que le toca llega por hostMessage().
class Server : public QObject {
    Q_OBJECT
public:
    Server(const Settings& settings, const Peer& host, QObject* parent = nullptr);
    ~Server() override;

    bool start(QString* error);
    void stop(const QString& reason);
    quint16 port() const;
    bool servesWeb() const;
    Mode connection() const { return m_settings.mode; }
    // P2P: el código que los invitados escriben para entrar.
    QString sessionCode() const { return m_code; }
    // P2P: cuántos servidores de señalización responden y cuántos hay.
    int signalingReady() const;
    int signalingTotal() const;
    // Direcciones IPv4 de este equipo en la red local.
    static QStringList localAddresses();

    QList<Peer> peers() const;
    Peer peer(int id) const;
    void setSettings(const Settings& settings);
    void setPeerMode(int id, const QString& mode);
    // Permiso para volver a versiones anteriores del historial del anfitrión.
    void setPeerHistory(int id, bool allowed);
    void renameHost(const QString& name);

    // Del anfitrión a un invitado o a todos.
    void sendTo(int id, const QJsonObject& message);
    void broadcast(const QJsonObject& message);
    void broadcastCursor(const QJsonObject& message);
    void admit(int id, bool ok);
    void kick(int id);

signals:
    // Para el anfitrión: cambios, sugerencias y cursores de los invitados (con "from").
    void hostMessage(const QJsonObject& message);
    void joinRequested(const Collab::Peer& peer);
    void peerJoined(const Collab::Peer& peer);
    void peerLeft(int id);
    void peerUpdated(const Collab::Peer& peer);
    // P2P: cambió cuántos servidores de señalización responden.
    void signalingChanged();

private:
    struct Client {
        QPointer<Link> socket;
        QByteArray nonce;
        Peer peer;
        bool joined = false;
        bool waiting = false;
    };

    void onConnection(Link* socket);
    void onText(Link* socket, const QByteArray& text);
    void onJoin(Client& client, const QJsonObject& m);
    void accept(Client& client);
    void deny(Client& client, const QString& reason);
    void dropClient(Link* socket);
    Client* clientFor(int id);
    void send(Link* socket, const QJsonObject& message);
    void broadcastExcept(const QJsonObject& message, int except);
    void sendBeacon();
    QColor colorFor(const QColor& wanted) const;
    QString findWebRoot() const;

    Settings m_settings;
    Peer m_host;
    WebSocketServer* m_server;
    QUdpSocket* m_beacon = nullptr;
    QTimer m_beaconTimer;
    QHash<Link*, Client> m_clients;
    P2PHost* m_p2p = nullptr;
    QString m_code;
    int m_nextId = 1;
};

} // namespace Collab
