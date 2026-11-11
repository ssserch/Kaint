#include "app/collab/CollabServer.h"

#include "app/collab/Discovery.h"
#include "app/collab/P2P.h"
#include "app/collab/WebSocket.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QUdpSocket>

#include <algorithm>
#include <cmath>

namespace Collab {

namespace {

// Hasta unirse, los mensajes de un desconocido no pueden ser grandes.
constexpr qint64 kMaxGuestHello = 64 * 1024;
constexpr int kMaxClients = 64;

QString cleanName(const QString& name, int id)
{
    QString out;
    for (const QChar c : name.trimmed())
        if (c.isPrint())
            out += c;
    out = out.left(40);
    return out.isEmpty() ? QObject::tr("Invitado %1").arg(id) : out;
}

double colorDistance(const QColor& a, const QColor& b)
{
    const double dr = a.red() - b.red(), dg = a.green() - b.green(), db = a.blue() - b.blue();
    return std::sqrt(dr * dr + dg * dg + db * db);
}

} // namespace

QJsonObject Peer::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("color"), color.name()},
                       {QStringLiteral("platform"), platform},
                       {QStringLiteral("app"), app},
                       {QStringLiteral("host"), host},
                       {QStringLiteral("mode"), mode}};
}

Peer Peer::fromJson(const QJsonObject& o)
{
    Peer p;
    p.id = o.value(QStringLiteral("id")).toInt();
    p.name = o.value(QStringLiteral("name")).toString().left(40);
    p.color = QColor(o.value(QStringLiteral("color")).toString());
    if (!p.color.isValid())
        p.color = peerColor(p.id);
    p.platform = o.value(QStringLiteral("platform")).toString();
    p.app = o.value(QStringLiteral("app")).toString().left(60);
    p.host = o.value(QStringLiteral("host")).toBool();
    p.mode = o.value(QStringLiteral("mode")).toString();
    return p;
}

// ---------------------------------------------------------------------------

Server::Server(const Settings& settings, const Peer& host, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_host(host)
    , m_server(new WebSocketServer(this))
{
    m_host.id = 0;
    m_host.host = true;
    if (!m_host.color.isValid())
        m_host.color = peerColor(0);
    connect(m_server, &WebSocketServer::newConnection, this, [this](WebSocket* socket) { onConnection(socket); });
    m_beaconTimer.setInterval(2000);
    connect(&m_beaconTimer, &QTimer::timeout, this, &Server::sendBeacon);
}

Server::~Server()
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->socket)
            it->socket->disconnect(this);
}

bool Server::start(QString* error)
{
    if (m_settings.mode == Mode::P2P) {
        // P2P: sin servidor en la red local; los invitados llegan por los servidores de señalización.
        if (!p2pAvailable()) {
            if (error)
                *error = tr("Este Kaint se compiló sin la conexión P2P. Usa la red local (LAN).");
            return false;
        }
        m_code = newSessionCode();
        m_p2p = new P2PHost(m_settings, m_code, this);
        connect(m_p2p, &P2PHost::linkReady, this, [this](Link* link) {
            link->setParent(this);
            onConnection(link);
        });
        connect(m_p2p, &P2PHost::signalingChanged, this, &Server::signalingChanged);
        m_p2p->start();
        setSettings(m_settings);
        return true;
    }
    if (!m_server->listen(quint16(m_settings.port), error))
        return false;
    m_server->setWebRoot(m_settings.serveWeb ? findWebRoot() : QString());
    // La lista de sesiones cercanas de Kaint para la web servida desde aquí (un navegador no puede
    // escuchar los anuncios de la red).
    if (servesWeb()) {
        auto* discovery = new Discovery(this);
        discovery->start();
        m_server->setSessionsProvider([discovery] {
            QJsonArray list;
            for (const FoundSession& f : discovery->sessions())
                list.append(QJsonObject{{QStringLiteral("name"), f.name}, {QStringLiteral("host"), f.host},
                                        {QStringLiteral("address"), f.address}, {QStringLiteral("port"), int(f.port)},
                                        {QStringLiteral("auth"), f.auth}, {QStringLiteral("app"), f.app},
                                        {QStringLiteral("web"), f.web}, {QStringLiteral("peers"), f.peers}});
            return QJsonDocument(list).toJson(QJsonDocument::Compact);
        });
    }
    setSettings(m_settings);
    return true;
}

void Server::stop(const QString& reason)
{
    const QJsonObject end{{QStringLiteral("t"), QStringLiteral("end")}, {QStringLiteral("reason"), reason}};
    const QList<Link*> sockets = m_clients.keys();
    for (Link* s : sockets) {
        send(s, end);
        s->disconnect(this);
        s->dispose();
    }
    m_clients.clear();
    m_beaconTimer.stop();
    if (m_beacon) {
        m_beacon->deleteLater();
        m_beacon = nullptr;
    }
    if (m_p2p)
        m_p2p->stop();
    m_server->close();
}

int Server::signalingReady() const
{
    return m_p2p ? m_p2p->signalingReady() : 0;
}

int Server::signalingTotal() const
{
    return m_p2p ? m_p2p->signalingTotal() : 0;
}

quint16 Server::port() const
{
    return m_server->port();
}

bool Server::servesWeb() const
{
    return !m_server->webRoot().isEmpty();
}

QStringList Server::localAddresses()
{
    // Primero las de las tarjetas de red de verdad (Ethernet, Wi-Fi) con direcciones de red local;
    // después las de máquinas virtuales, contenedores y VPN; al final las que no son de red local.
    struct Found {
        QString ip;
        int rank;
    };
    QList<Found> found;
    static const char* const kVirtual[] = {"virtual", "vmware", "vbox", "vethernet", "hyper-v", "docker", "br-", "virbr",
                                           "veth", "wsl", "tailscale", "zerotier", "hamachi", "vpn", "tun", "tap", "utun"};
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QString names = (iface.name() + QLatin1Char(' ') + iface.humanReadableName()).toLower();
        bool virtualIface = iface.type() == QNetworkInterface::Virtual;
        for (const char* word : kVirtual)
            virtualIface = virtualIface || names.contains(QLatin1String(word));
        for (const QNetworkAddressEntry& e : iface.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback())
                continue;
            const bool lan = a.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16)
                             || a.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8)
                             || a.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12);
            const int rank = (lan ? 0 : 4) + (virtualIface ? 2 : 0)
                             + (iface.type() == QNetworkInterface::Ethernet || iface.type() == QNetworkInterface::Wifi ? 0 : 1);
            if (std::none_of(found.cbegin(), found.cend(), [&](const Found& f) { return f.ip == a.toString(); }))
                found.append({a.toString(), rank});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& x, const Found& y) { return x.rank < y.rank; });
    QStringList out;
    for (const Found& f : found)
        out.append(f.ip);
    return out;
}

QList<Peer> Server::peers() const
{
    QList<Peer> out{m_host};
    QList<Peer> guests;
    for (const Client& c : m_clients)
        if (c.joined)
            guests.append(c.peer);
    std::sort(guests.begin(), guests.end(), [](const Peer& a, const Peer& b) { return a.id < b.id; });
    return out + guests;
}

Peer Server::peer(int id) const
{
    if (id == 0)
        return m_host;
    for (const Client& c : m_clients)
        if (c.peer.id == id)
            return c.peer;
    return {};
}

void Server::setSettings(const Settings& settings)
{
    m_settings = settings;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        Client& c = *it;
        if (!c.joined)
            continue;
        const QString mode = (c.peer.platform == QLatin1String("desktop") && m_settings.realtime) ? QStringLiteral("live")
                                                                                                    : QStringLiteral("confirm");
        send(c.socket, QJsonObject{{QStringLiteral("t"), QStringLiteral("settings")},
                                   {QStringLiteral("mode"), mode},
                                   {QStringLiteral("autoAccept"), m_settings.autoAccept}});
        if (mode != c.peer.mode) {
            c.peer.mode = mode;
            broadcast(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                                  {QStringLiteral("op"), QStringLiteral("update")},
                                  {QStringLiteral("peer"), c.peer.toJson()}});
            emit peerUpdated(c.peer);
        }
    }
    const QJsonObject info{{QStringLiteral("kaint-collab"), kVersion},
                           {QStringLiteral("session"), m_settings.effectiveSessionName()},
                           {QStringLiteral("host"), m_host.name},
                           {QStringLiteral("auth"), accessName(m_settings.access)},
                           {QStringLiteral("port"), int(port())},
                           {QStringLiteral("addresses"), QJsonArray::fromStringList(localAddresses())}};
    m_server->setInfo(QJsonDocument(info).toJson(QJsonDocument::Compact));
    if (m_settings.announce && m_server->isListening()) {
        if (!m_beacon)
            m_beacon = new QUdpSocket(this);
        if (!m_beaconTimer.isActive()) {
            m_beaconTimer.start();
            sendBeacon();
        }
    } else {
        m_beaconTimer.stop();
    }
}

void Server::setPeerMode(int id, const QString& mode)
{
    if (Client* c = clientFor(id); c && c->peer.mode != mode) {
        c->peer.mode = mode;
        send(c->socket, QJsonObject{{QStringLiteral("t"), QStringLiteral("settings")},
                                    {QStringLiteral("mode"), mode},
                                    {QStringLiteral("autoAccept"), m_settings.autoAccept}});
        broadcast(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                              {QStringLiteral("op"), QStringLiteral("update")},
                              {QStringLiteral("peer"), c->peer.toJson()}});
        emit peerUpdated(c->peer);
    }
}

void Server::renameHost(const QString& name)
{
    m_host.name = cleanName(name, 0);
    broadcast(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                          {QStringLiteral("op"), QStringLiteral("update")},
                          {QStringLiteral("peer"), m_host.toJson()}});
    emit peerUpdated(m_host);
    setSettings(m_settings);
}

void Server::onConnection(Link* socket)
{
    if (m_clients.size() >= kMaxClients) {
        socket->dispose();
        return;
    }
    socket->setMaxMessage(kMaxGuestHello);
    Client c;
    c.socket = socket;
    c.nonce = randomNonce();
    m_clients.insert(socket, c);
    connect(socket, &Link::textReceived, this, [this, socket](const QByteArray& text) { onText(socket, text); });
    connect(socket, &Link::closed, this, [this, socket] { dropClient(socket); });
    connect(socket, &Link::failed, this, [this, socket] { dropClient(socket); });
    send(socket, QJsonObject{{QStringLiteral("t"), QStringLiteral("hello")},
                             {QStringLiteral("v"), kVersion},
                             {QStringLiteral("session"), m_settings.effectiveSessionName()},
                             {QStringLiteral("host"), m_host.name},
                             {QStringLiteral("auth"), accessName(m_settings.access)},
                             {QStringLiteral("nonce"), QString::fromLatin1(c.nonce.toBase64())},
                             {QStringLiteral("server"), QStringLiteral("kaint")},
                             {QStringLiteral("app"), m_host.app}});
}

void Server::onText(Link* socket, const QByteArray& text)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    Client& client = *it;
    const QJsonObject m = QJsonDocument::fromJson(text).object();
    const QString t = m.value(QStringLiteral("t")).toString();
    if (!client.joined) {
        if (t == QLatin1String("join") && !client.waiting)
            onJoin(client, m);
        return;
    }
    const int id = client.peer.id;
    if (t == QLatin1String("change") || t == QLatin1String("propose")) {
        QJsonObject out = m;
        out.insert(QStringLiteral("from"), id);
        emit hostMessage(out);
    } else if (t == QLatin1String("cursor")) {
        QJsonObject out{{QStringLiteral("t"), t},
                        {QStringLiteral("from"), id},
                        {QStringLiteral("x"), m.value(QStringLiteral("x")).toDouble()},
                        {QStringLiteral("y"), m.value(QStringLiteral("y")).toDouble()},
                        {QStringLiteral("tool"), m.value(QStringLiteral("tool")).toString().left(32)},
                        {QStringLiteral("inside"), m.value(QStringLiteral("inside")).toBool()}};
        broadcastExcept(out, id);
        emit hostMessage(out);
    } else if (t == QLatin1String("rename")) {
        client.peer.name = cleanName(m.value(QStringLiteral("name")).toString(), id);
        broadcast(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                              {QStringLiteral("op"), QStringLiteral("update")},
                              {QStringLiteral("peer"), client.peer.toJson()}});
        emit peerUpdated(client.peer);
    } else if (t == QLatin1String("leave")) {
        socket->close();
    }
}

void Server::onJoin(Client& client, const QJsonObject& m)
{
    if (m.value(QStringLiteral("v")).toInt() != kVersion) {
        deny(client, QStringLiteral("version"));
        return;
    }
    Peer& p = client.peer;
    p.id = m_nextId++;
    p.name = cleanName(m.value(QStringLiteral("name")).toString(), p.id);
    p.platform = m.value(QStringLiteral("platform")).toString() == QLatin1String("web") ? QStringLiteral("web")
                                                                                         : QStringLiteral("desktop");
    p.app = m.value(QStringLiteral("app")).toString().left(60);
    p.device = m.value(QStringLiteral("device")).toString().left(64);
    p.color = QColor(m.value(QStringLiteral("color")).toString());
    const QString proofValue = m.value(QStringLiteral("proof")).toString();

    // ¿Es uno de los equipos de la lista? (con la prueba de que tiene su código)
    bool listed = false;
    if (!p.device.isEmpty() && !proofValue.isEmpty()) {
        for (const QString& line : m_settings.allowedIds) {
            const QString code = line.trimmed().section(QLatin1Char(' '), 0, 0);
            if (normalizedCode(code).size() == 12 && deviceTag(code) == p.device && proof(normalizedCode(code), client.nonce) == proofValue) {
                listed = true;
                break;
            }
        }
    }

    switch (m_settings.access) {
    case Access::Free:
        accept(client);
        return;
    case Access::Password:
        if (!m_settings.password.isEmpty() && proof(m_settings.password, client.nonce) == proofValue) {
            accept(client);
        } else {
            // Un segundo de espera ante cada contraseña errónea: probar muchas lleva tiempo.
            QPointer<Link> s = client.socket;
            client.waiting = true;
            QTimer::singleShot(1000, this, [this, s] {
                if (!s)
                    return;
                if (auto it = m_clients.find(s.data()); it != m_clients.end())
                    deny(*it, QStringLiteral("password"));
            });
        }
        return;
    case Access::Ids:
        if (listed)
            accept(client);
        else
            deny(client, QStringLiteral("device"));
        return;
    case Access::Approve:
        if (listed) {
            accept(client);
            return;
        }
        client.waiting = true;
        if (!p.color.isValid())
            p.color = colorFor(QColor());
        send(client.socket, QJsonObject{{QStringLiteral("t"), QStringLiteral("waiting")}});
        emit joinRequested(p);
        return;
    }
}

void Server::accept(Client& client)
{
    client.waiting = false;
    client.joined = true;
    Peer& p = client.peer;
    p.color = colorFor(p.color);
    p.mode = (p.platform == QLatin1String("desktop") && m_settings.realtime) ? QStringLiteral("live") : QStringLiteral("confirm");
    client.socket->setMaxMessage(Link::kMaxMessage);
    send(client.socket, QJsonObject{{QStringLiteral("t"), QStringLiteral("joined")},
                                    {QStringLiteral("you"), p.id},
                                    {QStringLiteral("color"), p.color.name()},
                                    {QStringLiteral("mode"), p.mode}});
    broadcastExcept(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                                {QStringLiteral("op"), QStringLiteral("add")},
                                {QStringLiteral("peer"), p.toJson()}},
                    p.id);
    emit peerJoined(p);
}

void Server::deny(Client& client, const QString& reason)
{
    QPointer<Link> s = client.socket;
    const int id = client.peer.id;
    const bool wasWaiting = client.waiting;
    send(s, QJsonObject{{QStringLiteral("t"), QStringLiteral("denied")}, {QStringLiteral("reason"), reason}});
    m_clients.remove(s.data());
    if (s) {
        s->disconnect(this);
        s->dispose();
    }
    if (wasWaiting && id > 0)
        emit peerLeft(id);
}

void Server::dropClient(Link* socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    const Client c = *it;
    m_clients.erase(it);
    socket->disconnect(this);
    socket->dispose();
    if (c.joined) {
        broadcast(QJsonObject{{QStringLiteral("t"), QStringLiteral("peer")},
                              {QStringLiteral("op"), QStringLiteral("remove")},
                              {QStringLiteral("peer"), c.peer.toJson()}});
        emit peerLeft(c.peer.id);
    } else if (c.waiting && c.peer.id > 0) {
        emit peerLeft(c.peer.id);
    }
}

Server::Client* Server::clientFor(int id)
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->peer.id == id && (it->joined || it->waiting))
            return &*it;
    return nullptr;
}

void Server::send(Link* socket, const QJsonObject& message)
{
    if (socket)
        socket->sendText(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void Server::sendTo(int id, const QJsonObject& message)
{
    if (Client* c = clientFor(id); c && c->joined)
        send(c->socket, message);
}

void Server::broadcast(const QJsonObject& message)
{
    broadcastExcept(message, -1);
}

void Server::broadcastExcept(const QJsonObject& message, int except)
{
    const QByteArray text = QJsonDocument(message).toJson(QJsonDocument::Compact);
    for (const Client& c : std::as_const(m_clients))
        if (c.joined && c.peer.id != except && c.socket)
            c.socket->sendText(text);
}

void Server::broadcastCursor(const QJsonObject& message)
{
    QJsonObject out = message;
    out.insert(QStringLiteral("t"), QStringLiteral("cursor"));
    out.insert(QStringLiteral("from"), 0);
    broadcast(out);
}

void Server::admit(int id, bool ok)
{
    Client* c = clientFor(id);
    if (!c || !c->waiting)
        return;
    if (ok)
        accept(*c);
    else
        deny(*c, QStringLiteral("rejected"));
}

void Server::kick(int id)
{
    Client* c = clientFor(id);
    if (!c)
        return;
    QPointer<Link> s = c->socket;
    send(s, QJsonObject{{QStringLiteral("t"), QStringLiteral("end")}, {QStringLiteral("reason"), QStringLiteral("kicked")}});
    if (s)
        dropClient(s);
}

void Server::sendBeacon()
{
    if (!m_beacon)
        return;
    const QJsonObject beacon{{QStringLiteral("kaint-collab"), kVersion},
                             {QStringLiteral("name"), m_settings.effectiveSessionName()},
                             {QStringLiteral("host"), m_host.name},
                             {QStringLiteral("port"), int(port())},
                             {QStringLiteral("auth"), accessName(m_settings.access)},
                             {QStringLiteral("app"), m_host.app},
                             {QStringLiteral("web"), servesWeb()},
                             {QStringLiteral("peers"), int(peers().size())}};
    const QByteArray data = QJsonDocument(beacon).toJson(QJsonDocument::Compact);
    QList<QHostAddress> targets;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) || !(flags & QNetworkInterface::CanBroadcast)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry& e : iface.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.broadcast().isNull() && !targets.contains(e.broadcast()))
                targets.append(e.broadcast());
    }
    if (targets.isEmpty())
        targets.append(QHostAddress(QHostAddress::Broadcast));
    // También a este equipo (otra copia de Kaint en él la encuentra).
    targets.append(QHostAddress(QHostAddress::LocalHost));
    for (const QHostAddress& a : targets)
        m_beacon->writeDatagram(data, a, kBeaconPort);
}

QColor Server::colorFor(const QColor& wanted) const
{
    QList<QColor> used{m_host.color};
    for (const Client& c : m_clients)
        if (c.joined)
            used.append(c.peer.color);
    auto free = [&used](const QColor& c) {
        for (const QColor& u : used)
            if (colorDistance(u, c) < 60)
                return false;
        return true;
    };
    if (wanted.isValid() && free(wanted))
        return wanted;
    for (int i = 0; i < 10; ++i)
        if (free(peerColor(i)))
            return peerColor(i);
    return peerColor(int(used.size()));
}

QString Server::findWebRoot() const
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("KAINT_WEB_ROOT");
    if (!env.isEmpty())
        candidates.append(env);
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString& rel : {QStringLiteral("KaintHTML"), QStringLiteral("../share/kaint/KaintHTML"),
                               QStringLiteral("../Resources/KaintHTML"), QStringLiteral("../KaintHTML"),
                               QStringLiteral("../../KaintHTML"), QStringLiteral("../../../KaintHTML")})
        candidates.append(app + QLatin1Char('/') + rel);
    for (const QString& c : candidates) {
        const QDir dir(c);
        if (QFileInfo::exists(dir.filePath(QStringLiteral("index.html"))) && QFileInfo(dir.filePath(QStringLiteral("js"))).isDir())
            return dir.canonicalPath();
    }
    return {};
}

} // namespace Collab
