#pragma once

#include "app/collab/CollabProtocol.h"
#include "app/collab/WebSocket.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

// Colaboración P2P por Internet (docs/collaboration.md, «P2P»): cada invitado se conecta directo
// con el anfitrión por un canal de datos de WebRTC (con libdatachannel, el mismo que usan los
// navegadores) y por él habla el mismo protocolo que en la red local. Para encontrarse usan
// rastreadores de WebTorrent (solo intercambian las ofertas y respuestas de WebRTC, firmadas con el
// código de la sesión; la imagen no pasa por ellos).
namespace Collab {

// Si este Kaint se compiló con libdatachannel.
bool p2pAvailable();

// Lo que se deriva del código de una sesión: la «sala» en el rastreador (20 caracteres) y la clave
// con que se firman las ofertas y respuestas.
QString p2pRoom(const QString& code);
// La descripción de WebRTC con la firma (una línea "a=kaint-sig:" al final) y su comprobación
// (devuelve la descripción sin la firma, o vacía si no es válida).
QString signDescription(const QString& code, const QString& kind, const QString& sdp);
QString verifyDescription(const QString& code, const QString& kind, const QString& sdp);

class RtcLink;

// Conexión con los servidores de señalización (protocolo de los rastreadores de WebTorrent).
class Signaling : public QObject {
    Q_OBJECT
public:
    Signaling(const QStringList& servers, const QString& room, QObject* parent = nullptr);
    ~Signaling() override;

    void start();
    // Avisa a los servidores de que se va y cierra las conexiones.
    void stop();
    QString peerId() const { return m_peerId; }
    int readyCount() const;
    // A todos los servidores conectados.
    void announce(const QJsonArray& offers, int numwant);
    void sendAnswer(int server, const QString& toPeer, const QString& offerId, const QJsonObject& answer);

signals:
    void serverReady(int server);
    void offerReceived(int server, const QString& peerId, const QString& offerId, const QJsonObject& offer);
    void answerReceived(int server, const QString& peerId, const QString& offerId, const QJsonObject& answer);
    // Ninguno responde.
    void unreachable(const QString& reason);

private:
    struct Server {
        QString url;
        QPointer<WebSocket> socket;
        bool ready = false;
        int failures = 0;
        QString lastError;
    };
    void connectServer(int index);
    void onText(int index, const QByteArray& text);
    void send(int index, const QJsonObject& message);
    void checkUnreachable();

    QList<Server> m_servers;
    QString m_room;
    QString m_peerId;
    bool m_running = false;
};

// Anfitrión: atiende las ofertas de los invitados y entrega cada canal abierto al servidor.
class P2PHost : public QObject {
    Q_OBJECT
public:
    P2PHost(const Settings& settings, const QString& code, QObject* parent = nullptr);
    ~P2PHost() override;

    void start();
    void stop();
    QString code() const { return m_code; }
    // Cuántos servidores de señalización responden (para mostrarlo en la sesión).
    int signalingReady() const;
    int signalingTotal() const { return int(m_settings.signalServers.size()); }

signals:
    void linkReady(Link* link);
    void signalingChanged();

private:
    void onOffer(int server, const QString& peerId, const QString& offerId, const QJsonObject& offer);

    Settings m_settings;
    QString m_code;
    Signaling* m_signaling;
    QTimer m_announce;
    QHash<QString, QPointer<RtcLink>> m_pending;
};

// Invitado: manda ofertas hasta que el anfitrión responde y se abre el canal.
class P2PGuest : public QObject {
    Q_OBJECT
public:
    P2PGuest(const Settings& settings, const QString& code, QObject* parent = nullptr);
    ~P2PGuest() override;

    void start();
    void stop();

signals:
    void connected(Link* link);
    void failed(const QString& reason);
    void progress(const QString& text);

private:
    void newRound();
    void onAnswer(int server, const QString& peerId, const QString& offerId, const QJsonObject& answer);
    void finish(Link* link, const QString& error);

    Settings m_settings;
    QString m_code;
    Signaling* m_signaling;
    QTimer m_rounds;
    QTimer m_timeout;
    QHash<QString, QPointer<RtcLink>> m_offers;
    QJsonArray m_ready;
    bool m_done = false;
};

} // namespace Collab
