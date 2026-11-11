#include "app/collab/WebSocket.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <cstring>

namespace {

constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
// Lo que puede ocupar la petición HTTP del saludo.
constexpr int kMaxHeader = 16 * 1024;

QByteArray acceptKey(const QByteArray& key)
{
    return QCryptographicHash::hash(key.trimmed() + kGuid, QCryptographicHash::Sha1).toBase64();
}

// Valor de una cabecera HTTP (sin distinguir mayúsculas en el nombre).
QByteArray header(const QList<QByteArray>& lines, const QByteArray& name)
{
    for (const QByteArray& line : lines) {
        const int colon = int(line.indexOf(':'));
        if (colon > 0 && line.left(colon).trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QByteArray randomBytes(int n)
{
    QByteArray out(n, '\0');
    for (int i = 0; i < n; ++i)
        out[i] = char(QRandomGenerator::system()->bounded(256));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

WebSocket::WebSocket(QTcpSocket* socket, bool client, QObject* parent)
    : QObject(parent)
    , m_socket(socket)
    , m_client(client)
{
    socket->setParent(this);
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket, &QTcpSocket::readyRead, this, &WebSocket::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this] {
        const bool wasOpen = m_open;
        m_open = false;
        if (wasOpen || m_closing)
            emit closed();
        else
            emit failed(tr("Se cortó la conexión."));
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError)
            return;
        if (!m_open)
            fail(m_socket ? m_socket->errorString() : tr("No se pudo conectar."));
    });
}

WebSocket::~WebSocket()
{
    if (m_socket)
        m_socket->disconnect(this);
}

WebSocket* WebSocket::connectTo(const QString& host, quint16 port, QObject* parent)
{
    auto* socket = new QTcpSocket;
    auto* ws = new WebSocket(socket, true, parent);
    ws->m_key = randomBytes(16).toBase64();
    connect(socket, &QTcpSocket::connected, ws, [ws, socket, host, port] {
        const QByteArray request = "GET /kaint-collab HTTP/1.1\r\nHost: " + host.toUtf8() + ':' + QByteArray::number(port)
                                   + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + ws->m_key
                                   + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        socket->write(request);
    });
    // Sin respuesta en un rato, se da por imposible.
    QTimer::singleShot(10000, ws, [ws] {
        if (!ws->m_open && !ws->m_closing)
            ws->fail(tr("No respondió a tiempo."));
    });
    socket->connectToHost(host, port);
    return ws;
}

QHostAddress WebSocket::peerAddress() const
{
    return m_socket ? m_socket->peerAddress() : QHostAddress();
}

void WebSocket::fail(const QString& reason)
{
    if (m_closing)
        return;
    m_closing = true;
    m_open = false;
    emit failed(reason);
    if (m_socket)
        m_socket->abort();
}

void WebSocket::onReadyRead()
{
    if (!m_socket)
        return;
    m_buffer += m_socket->readAll();
    if (m_client && !m_open && !m_closing) {
        if (!readHandshake())
            return;
    }
    if (m_open || m_closing)
        readFrames();
}

bool WebSocket::readHandshake()
{
    const int end = int(m_buffer.indexOf("\r\n\r\n"));
    if (end < 0) {
        if (m_buffer.size() > kMaxHeader)
            fail(tr("Respuesta no válida."));
        return false;
    }
    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    m_buffer.remove(0, end + 4);
    if (lines.isEmpty() || !lines.first().contains(" 101") || header(lines, "Sec-WebSocket-Accept") != acceptKey(m_key)) {
        fail(tr("No es una sesión de Kaint."));
        return false;
    }
    m_open = true;
    emit opened();
    return true;
}

bool WebSocket::readFrames()
{
    while (m_socket) {
        if (m_buffer.size() < 2)
            return true;
        const auto* b = reinterpret_cast<const uchar*>(m_buffer.constData());
        const bool fin = b[0] & 0x80;
        const int opcode = b[0] & 0x0f;
        const bool masked = b[1] & 0x80;
        quint64 length = b[1] & 0x7f;
        int pos = 2;
        if (length == 126) {
            if (m_buffer.size() < 4)
                return true;
            length = (quint64(b[2]) << 8) | b[3];
            pos = 4;
        } else if (length == 127) {
            if (m_buffer.size() < 10)
                return true;
            length = 0;
            for (int i = 0; i < 8; ++i)
                length = (length << 8) | b[2 + i];
            pos = 10;
        }
        if (length > quint64(m_maxMessage)) {
            fail(tr("Mensaje demasiado grande."));
            return false;
        }
        // El cliente siempre enmascara lo que envía; el servidor nunca.
        if (masked == m_client) {
            fail(tr("Mensaje no válido."));
            return false;
        }
        const int maskPos = pos;
        if (masked)
            pos += 4;
        if (quint64(m_buffer.size()) < pos + length)
            return true;
        QByteArray payload = m_buffer.mid(pos, qsizetype(length));
        if (masked) {
            const uchar* key = reinterpret_cast<const uchar*>(m_buffer.constData()) + maskPos;
            char* data = payload.data();
            for (qsizetype i = 0; i < payload.size(); ++i)
                data[i] = char(uchar(data[i]) ^ key[i % 4]);
        }
        m_buffer.remove(0, qsizetype(pos + length));
        switch (opcode) {
        case 0x0: // continuación
        case 0x1: // texto
        case 0x2: // binario
            if (opcode != 0)
                m_messageOpcode = opcode;
            m_message += payload;
            if (m_message.size() > m_maxMessage) {
                fail(tr("Mensaje demasiado grande."));
                return false;
            }
            if (fin) {
                const QByteArray message = std::exchange(m_message, {});
                if (m_messageOpcode == 0x1 && m_open)
                    emit textReceived(message);
            }
            break;
        case 0x8: // cierre
            if (!m_closing) {
                m_closing = true;
                sendFrame(0x8, payload.left(2));
            }
            m_open = false;
            if (m_socket)
                m_socket->disconnectFromHost();
            return false;
        case 0x9: // ping
            sendFrame(0xA, payload);
            break;
        default: // pong y otros
            break;
        }
    }
    return true;
}

void WebSocket::sendFrame(int opcode, const QByteArray& payload)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray frame;
    frame.reserve(payload.size() + 14);
    frame.append(char(0x80 | opcode));
    const char maskBit = m_client ? char(0x80) : char(0);
    const quint64 n = quint64(payload.size());
    if (n < 126) {
        frame.append(char(maskBit | char(n)));
    } else if (n < 65536) {
        frame.append(char(maskBit | 126));
        frame.append(char(n >> 8));
        frame.append(char(n & 0xff));
    } else {
        frame.append(char(maskBit | 127));
        for (int i = 7; i >= 0; --i)
            frame.append(char((n >> (8 * i)) & 0xff));
    }
    if (m_client) {
        const QByteArray key = randomBytes(4);
        frame.append(key);
        QByteArray data = payload;
        char* d = data.data();
        for (qsizetype i = 0; i < data.size(); ++i)
            d[i] = char(d[i] ^ key[i % 4]);
        frame.append(data);
    } else {
        frame.append(payload);
    }
    m_socket->write(frame);
}

void WebSocket::sendText(const QByteArray& utf8)
{
    if (m_open)
        sendFrame(0x1, utf8);
}

void WebSocket::close()
{
    if (!m_socket || m_closing)
        return;
    m_closing = true;
    if (m_open) {
        sendFrame(0x8, QByteArray("\x03\xe8", 2));
        m_open = false;
        QTimer::singleShot(1000, m_socket, [s = m_socket] {
            if (s)
                s->abort();
        });
        m_socket->disconnectFromHost();
    } else {
        m_socket->abort();
    }
}

void WebSocket::dispose()
{
    close();
    if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState) {
        deleteLater();
        return;
    }
    connect(m_socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
    QTimer::singleShot(3000, this, &QObject::deleteLater);
}

// ---------------------------------------------------------------------------

WebSocketServer::WebSocketServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &WebSocketServer::onNewConnection);
}

WebSocketServer::~WebSocketServer() = default;

bool WebSocketServer::listen(quint16 port, QString* error)
{
    if (m_server->listen(QHostAddress::Any, port))
        return true;
    if (error)
        *error = m_server->errorString();
    return false;
}

void WebSocketServer::close()
{
    m_server->close();
}

quint16 WebSocketServer::port() const
{
    return m_server->serverPort();
}

bool WebSocketServer::isListening() const
{
    return m_server->isListening();
}

void WebSocketServer::onNewConnection()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        socket->setParent(this);
        // Si en 10 segundos no llega una petición completa, fuera.
        QTimer::singleShot(10000, socket, [socket] {
            if (socket->property("kaintHandled").toBool())
                return;
            socket->abort();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            if (socket->property("kaintHandled").toBool())
                return;
            const QByteArray buffer = socket->property("kaintRequest").toByteArray() + socket->readAll();
            if (buffer.size() > kMaxHeader) {
                socket->abort();
                return;
            }
            socket->setProperty("kaintRequest", buffer);
            if (buffer.contains("\r\n\r\n"))
                handleRequest(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, [socket] {
            if (!socket->property("kaintWebSocket").toBool())
                socket->deleteLater();
        });
    }
}

void WebSocketServer::handleRequest(QTcpSocket* socket)
{
    socket->setProperty("kaintHandled", true);
    const QByteArray request = socket->property("kaintRequest").toByteArray();
    const int end = int(request.indexOf("\r\n\r\n"));
    const QList<QByteArray> lines = request.left(end).split('\n');
    const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    const QByteArray method = first.value(0), target = first.value(1);
    if (method != "GET") {
        socket->write("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        socket->disconnectFromHost();
        return;
    }
    if (header(lines, "Upgrade").compare("websocket", Qt::CaseInsensitive) == 0) {
        const QByteArray key = header(lines, "Sec-WebSocket-Key");
        if (key.isEmpty()) {
            socket->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
            return;
        }
        socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
                      + acceptKey(key) + "\r\n\r\n");
        socket->setProperty("kaintWebSocket", true);
        socket->disconnect(this);
        auto* ws = new WebSocket(socket, false, this);
        // Lo que llegó tras la petición ya es del WebSocket.
        ws->m_buffer = request.mid(end + 4);
        ws->m_open = true;
        emit newConnection(ws);
        if (!ws->m_buffer.isEmpty())
            QTimer::singleShot(0, ws, [ws] { ws->readFrames(); });
        return;
    }
    QString path = QUrl::fromPercentEncoding(target.split('?').value(0));
    if (path == QLatin1String("/kaint-collab.json")) {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: "
                      + QByteArray::number(m_info.size()) + "\r\nConnection: close\r\n\r\n" + m_info);
        socket->disconnectFromHost();
        return;
    }
    serveFile(socket, path);
}

void WebSocketServer::serveFile(QTcpSocket* socket, const QString& requested)
{
    auto reply = [socket](const QByteArray& status, const QByteArray& type, const QByteArray& body) {
        socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    };
    if (m_webRoot.isEmpty()) {
        reply("404 Not Found", "text/plain; charset=utf-8",
              tr("Esta sesión de Kaint no sirve la versión web. Ábrela desde tu copia de Kaint para la web.").toUtf8());
        return;
    }
    QString path = QDir::cleanPath(requested);
    if (path.isEmpty() || path == QLatin1String("/"))
        path = QStringLiteral("/index.html");
    const QString root = QDir(m_webRoot).canonicalPath();
    const QString file = QFileInfo(root + path).canonicalFilePath();
    // Nada fuera de la carpeta.
    if (file.isEmpty() || !file.startsWith(root + QLatin1Char('/')) || !QFileInfo(file).isFile()) {
        reply("404 Not Found", "text/plain; charset=utf-8", "404");
        return;
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        reply("404 Not Found", "text/plain; charset=utf-8", "404");
        return;
    }
    static const QHash<QString, QByteArray> types{
        {QStringLiteral("html"), "text/html; charset=utf-8"}, {QStringLiteral("js"), "text/javascript; charset=utf-8"},
        {QStringLiteral("mjs"), "text/javascript; charset=utf-8"}, {QStringLiteral("css"), "text/css; charset=utf-8"},
        {QStringLiteral("json"), "application/json"}, {QStringLiteral("png"), "image/png"},
        {QStringLiteral("svg"), "image/svg+xml"}, {QStringLiteral("ico"), "image/x-icon"},
        {QStringLiteral("wasm"), "application/wasm"}, {QStringLiteral("ttf"), "font/ttf"},
        {QStringLiteral("woff2"), "font/woff2"}, {QStringLiteral("txt"), "text/plain; charset=utf-8"},
    };
    QByteArray body = f.readAll();
    // La página sabe así que la sirve una sesión (y pregunta por /kaint-collab.json para unirse).
    if (QFileInfo(file).fileName() == QLatin1String("index.html"))
        body.replace("<head>", "<head><meta name=\"kaint-collab\" content=\"1\">");
    reply("200 OK", types.value(QFileInfo(file).suffix().toLower(), "application/octet-stream"), body);
}
