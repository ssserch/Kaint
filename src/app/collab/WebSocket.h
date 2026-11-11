#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QString>

class QTcpServer;
class QTcpSocket;

// WebSocket (RFC 6455) sobre QTcpSocket, sin depender de Qt WebSockets: el mismo protocolo que
// usan los navegadores, para que Kaint y Kaint para la web se entiendan (docs/collaboration.md).
// Solo mensajes de texto (JSON), con ping/pong y cierre.
class WebSocket : public QObject {
    Q_OBJECT
public:
    // Cliente: se conecta a ws://host:port/ y hace el saludo HTTP.
    static WebSocket* connectTo(const QString& host, quint16 port, QObject* parent = nullptr);
    ~WebSocket() override;

    void sendText(const QByteArray& utf8);
    void close();
    // Cierra y se borra cuando termina de enviarse lo pendiente.
    void dispose();
    bool isOpen() const { return m_open; }
    QHostAddress peerAddress() const;

    // Tamaño máximo de un mensaje (la imagen entera al unirse puede ocupar bastante).
    static constexpr qint64 kMaxMessage = 512ll * 1024 * 1024;
    void setMaxMessage(qint64 bytes) { m_maxMessage = bytes; }

signals:
    void opened();
    void textReceived(const QByteArray& utf8);
    void closed();
    void failed(const QString& reason);

private:
    friend class WebSocketServer;
    WebSocket(QTcpSocket* socket, bool client, QObject* parent);
    void onReadyRead();
    bool readHandshake();
    bool readFrames();
    void sendFrame(int opcode, const QByteArray& payload);
    void fail(const QString& reason);

    QPointer<QTcpSocket> m_socket;
    bool m_client = false;
    bool m_open = false;
    bool m_closing = false;
    QByteArray m_buffer;
    QByteArray m_key;
    QByteArray m_message;
    int m_messageOpcode = 0;
    qint64 m_maxMessage = kMaxMessage;
};

// Servidor: recibe conexiones HTTP; las que piden WebSocket se convierten en WebSocket y las
// demás reciben los archivos de 'webRoot' (Kaint para la web, si se tiene) o un 404.
class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject* parent = nullptr);
    ~WebSocketServer() override;

    bool listen(quint16 port, QString* error = nullptr);
    void close();
    quint16 port() const;
    bool isListening() const;
    // Carpeta con Kaint para la web que se sirve a los navegadores (vacía: ninguna).
    void setWebRoot(const QString& folder) { m_webRoot = folder; }
    QString webRoot() const { return m_webRoot; }
    // Lo que responde /kaint-collab.json (la web servida desde aquí lo lee para unirse sola).
    void setInfo(const QByteArray& json) { m_info = json; }

signals:
    void newConnection(WebSocket* socket);

private:
    void onNewConnection();
    void handleRequest(QTcpSocket* socket);
    void serveFile(QTcpSocket* socket, const QString& path);

    QTcpServer* m_server;
    QString m_webRoot;
    QByteArray m_info;
};
