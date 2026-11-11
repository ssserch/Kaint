#include "app/collab/CollabSession.h"

#include "app/collab/P2P.h"
#include "app/collab/WebSocket.h"
#include "core/Commands.h"
#include "core/Document.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSet>
#include <QUndoStack>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Collab {

namespace {

const QString kHistoryIcon = QStringLiteral("collab");
// Imágenes que se aceptan: hasta 200 megapíxeles.
constexpr qint64 kMaxPixels = 200ll * 1000 * 1000;

QString appName()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("Kaint") : QStringLiteral("Kaint %1").arg(version);
}

// Orden nuevo de una lista de capas: las de 'listed' en ese orden, sin las de 'removed', y las
// actuales que no aparecen (las que alguien añadió a la vez) encima de la que tenían debajo.
QStringList mergeOrder(const QStringList& current, const QStringList& listed, const QSet<QString>& removed)
{
    QStringList result;
    for (const QString& id : listed)
        if (!removed.contains(id) && !result.contains(id))
            result.append(id);
    for (int i = 0; i < current.size(); ++i) {
        const QString& id = current[i];
        if (result.contains(id) || removed.contains(id))
            continue;
        int pos = 0;
        for (int j = i - 1; j >= 0; --j) {
            const int k = int(result.indexOf(current[j]));
            if (k >= 0) {
                pos = k + 1;
                break;
            }
        }
        result.insert(pos, id);
    }
    return result;
}

QJsonObject propsJson(const QString& id, const LayerProps& p)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), p.name},
                       {QStringLiteral("visible"), p.visible},
                       {QStringLiteral("opacity"), p.opacity},
                       {QStringLiteral("blend"), blendName(p.blend)}};
}

LayerProps propsFrom(const QJsonObject& o, LayerProps p = {})
{
    if (o.contains(QStringLiteral("name")))
        p.name = o.value(QStringLiteral("name")).toString().left(256);
    if (o.contains(QStringLiteral("visible")))
        p.visible = o.value(QStringLiteral("visible")).toBool(true);
    if (o.contains(QStringLiteral("opacity")))
        p.opacity = std::clamp(o.value(QStringLiteral("opacity")).toInt(255), 0, 255);
    if (o.contains(QStringLiteral("blend")))
        p.blend = blendFromName(o.value(QStringLiteral("blend")).toString());
    return p;
}

// Rectángulo que abarca los píxeles distintos de dos imágenes del mismo tamaño.
bool differingBounds(const QImage& a, const QImage& b, QRect* out)
{
    if (a.constBits() == b.constBits() || a.size() != b.size())
        return false;
    const int w = a.width(), h = a.height();
    int x0 = w, x1 = -1, y0 = -1, y1 = -1;
    const size_t rowBytes = size_t(w) * 4;
    for (int y = 0; y < h; ++y) {
        const auto* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const auto* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        if (std::memcmp(pa, pb, rowBytes) == 0)
            continue;
        int l = 0;
        while (pa[l] == pb[l])
            ++l;
        int r = w - 1;
        while (pa[r] == pb[r])
            --r;
        x0 = std::min(x0, l);
        x1 = std::max(x1, r);
        if (y0 < 0)
            y0 = y;
        y1 = y;
    }
    if (y0 < 0)
        return false;
    *out = QRect(QPoint(x0, y0), QPoint(x1, y1));
    return true;
}

Layer* newLayer(const LayerProps& props, const QImage& image)
{
    auto* l = new Layer(props.name, QSize(1, 1));
    l->image = image;
    l->props = props;
    return l;
}

} // namespace

int Session::Canon::indexOf(const QString& id) const
{
    for (int i = 0; i < layers.size(); ++i)
        if (layers[i].id == id)
            return i;
    return -1;
}

QStringList Session::Canon::ids() const
{
    QStringList out;
    for (const CanonLayer& l : layers)
        out.append(l.id);
    return out;
}

// ---------------------------------------------------------------------------

Session::Session(Role role, const Settings& settings, QObject* parent)
    : QObject(parent)
    , m_role(role)
    , m_settings(settings)
{
    m_queueTimer.setInterval(100);
    connect(&m_queueTimer, &QTimer::timeout, this, &Session::processQueue);
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(300);
    connect(&m_draftTimer, &QTimer::timeout, this, &Session::updateDraft);
    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(33);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this] {
        if (!m_cursorDirty)
            return;
        m_cursorDirty = false;
        if (isHost())
            m_server->broadcastCursor(m_cursorOut);
        else
            send(m_cursorOut);
        m_cursorTimer.start();
    });
    // El historial del anfitrión sale a los invitados un momento después de cambiar (varios pasos
    // seguidos, una sola vez).
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(250);
    connect(&m_historyTimer, &QTimer::timeout, this, [this] {
        if (isHost() && m_server && m_state == State::Joined)
            m_server->broadcast(historyMessage());
        emit historyChanged();
        emit indicatorsChanged();
    });
}

Session::~Session()
{
    if (m_state != State::Ended) {
        if (m_server)
            m_server->stop(QStringLiteral("closed"));
        else if (m_socket)
            m_socket->close();
    }
    if (m_p2p)
        m_p2p->stop();
}

Session* Session::host(Document* doc, const Settings& settings, QString* error, QObject* parent)
{
    auto* s = new Session(Role::Host, settings, parent);
    Peer me;
    me.name = settings.displayName();
    me.color = settings.color.isValid() ? settings.color : peerColor(0);
    me.platform = QStringLiteral("desktop");
    me.app = appName();
    me.host = true;
    s->m_connection = settings.mode;
    s->m_server = new Server(settings, me, s);
    connect(s->m_server, &Server::signalingChanged, s, &Session::stateChanged);
    if (!s->m_server->start(error)) {
        delete s;
        return nullptr;
    }
    s->m_idPrefix = QStringLiteral("h");
    s->m_myId = 0;
    s->m_autoAccept = settings.autoAccept;
    s->m_sessionName = settings.effectiveSessionName();
    s->m_port = s->m_server->port();
    s->m_canon.name = doc->displayName();
    s->attachDocument(doc);
    s->adoptWorking();

    Server* server = s->m_server;
    connect(server, &Server::hostMessage, s, &Session::onHostMessage);
    connect(server, &Server::peerJoined, s, &Session::onPeerJoined);
    connect(server, &Server::joinRequested, s, [s](const Peer& p) {
        s->m_waiting.insert(p.id, p);
        emit s->peersChanged();
        emit s->joinRequested(p);
    });
    connect(server, &Server::peerLeft, s, [s](int id) {
        const bool wasGuest = !s->m_waiting.contains(id);
        const Peer p = s->m_waiting.take(id);
        s->m_cursors.remove(id);
        const qsizetype before = s->m_proposals.size();
        s->m_proposals.erase(std::remove_if(s->m_proposals.begin(), s->m_proposals.end(), [id](const Proposal& pr) { return pr.from == id; }),
                             s->m_proposals.end());
        if (before != s->m_proposals.size())
            emit s->proposalsChanged();
        const QString name = s->m_peers.value(id, p).name;
        s->m_peers.remove(id);
        emit s->peersChanged();
        emit s->cursorsChanged();
        if (wasGuest && !name.isEmpty())
            emit s->notice(tr("%1 salió de la sesión.").arg(name));
    });
    connect(server, &Server::peerUpdated, s, [s](const Peer& p) {
        if (p.id != 0)
            s->m_peers.insert(p.id, p);
        emit s->peersChanged();
    });
    s->setState(State::Joined);
    return s;
}

Session* Session::join(const QString& address, quint16 port, const Settings& settings, const QString& password, QObject* parent)
{
    auto* s = new Session(Role::Guest, settings, parent);
    s->m_address = address;
    s->m_port = port;
    s->m_password = password;
    s->attachLink(WebSocket::connectTo(address, port, s));
    return s;
}

Session* Session::joinP2P(const QString& code, const Settings& settings, const QString& password, QObject* parent)
{
    auto* s = new Session(Role::Guest, settings, parent);
    s->m_connection = Mode::P2P;
    s->m_code = code;
    s->m_address = QStringLiteral("P2P");
    s->m_password = password;
    s->m_p2p = new P2PGuest(settings, code, s);
    connect(s->m_p2p, &P2PGuest::progress, s, [s](const QString& text) {
        s->m_progress = text;
        emit s->stateChanged();
    });
    connect(s->m_p2p, &P2PGuest::connected, s, [s](Link* link) {
        link->setParent(s);
        s->m_p2p->deleteLater();
        s->m_p2p = nullptr;
        s->m_progress = tr("Conectado; entrando en la sesión…");
        s->attachLink(link);
        emit s->stateChanged();
    });
    connect(s->m_p2p, &P2PGuest::failed, s, [s](const QString& reason) { s->finish(reason); });
    QTimer::singleShot(0, s, [s] {
        if (s->m_p2p)
            s->m_p2p->start();
    });
    return s;
}

void Session::attachLink(Link* link)
{
    m_socket = link;
    connect(link, &Link::textReceived, this, &Session::onGuestText);
    connect(link, &Link::failed, this, [this](const QString& reason) {
        finish(m_state == State::Connecting ? tr("No se pudo conectar: %1").arg(reason) : reasonText(QStringLiteral("lost")));
    });
    connect(link, &Link::closed, this, [this] { finish(reasonText(QStringLiteral("lost"))); });
}

QString Session::sessionCode() const
{
    return m_server ? m_server->sessionCode() : m_code;
}

int Session::signalingReady() const
{
    return m_server ? m_server->signalingReady() : 0;
}

int Session::signalingTotal() const
{
    return m_server ? m_server->signalingTotal() : 0;
}

quint16 Session::port() const
{
    return m_server ? m_server->port() : m_port;
}

QStringList Session::addresses() const
{
    if (m_connection == Mode::P2P)
        return {};
    return m_server ? Server::localAddresses() : QStringList{m_address};
}

bool Session::servesWeb() const
{
    return m_server && m_server->servesWeb();
}

QList<Peer> Session::peers() const
{
    return m_server ? m_server->peers() : m_peers.values();
}

Peer Session::peer(int id) const
{
    if (m_server)
        return m_server->peer(id);
    return m_peers.value(id);
}

QJsonObject Session::historyMessage() const
{
    QJsonArray steps;
    const QUndoStack* stack = m_doc ? m_doc->undoStack() : nullptr;
    if (stack)
        for (int i = 0; i < stack->count(); ++i)
            steps.append(stack->text(i));
    return QJsonObject{{QStringLiteral("t"), QStringLiteral("history")},
                       {QStringLiteral("steps"), steps},
                       {QStringLiteral("index"), stack ? stack->index() : 0},
                       {QStringLiteral("saved"), m_doc && !m_doc->isModified()}};
}

QStringList Session::hostHistory() const
{
    if (!isHost())
        return m_hostHistory;
    QStringList out;
    if (m_doc)
        for (int i = 0; i < m_doc->undoStack()->count(); ++i)
            out.append(m_doc->undoStack()->text(i));
    return out;
}

int Session::hostHistoryIndex() const
{
    if (!isHost())
        return m_hostHistoryIndex;
    return m_doc ? m_doc->undoStack()->index() : 0;
}

bool Session::hostSaved() const
{
    if (!isHost())
        return m_hostSaved;
    return m_doc && !m_doc->isModified();
}

bool Session::canRevert() const
{
    if (isHost())
        return true;
    return m_state == State::Joined && m_peers.value(m_myId).history;
}

void Session::revertTo(int index)
{
    if (isHost()) {
        if (m_doc && index >= 0 && index <= m_doc->undoStack()->count())
            emit revertRequested(index);
        return;
    }
    if (canRevert())
        send(QJsonObject{{QStringLiteral("t"), QStringLiteral("revert")}, {QStringLiteral("index"), index}});
}

void Session::setHistoryPermission(int id, bool allowed)
{
    if (m_server)
        m_server->setPeerHistory(id, allowed);
}

Session::CopyStamp Session::stamp() const
{
    if (!m_doc)
        return {};
    const QUndoStack* stack = m_doc->undoStack();
    const int index = stack->index();
    return {m_historyGeneration, index, index > 0 ? static_cast<const void*>(stack->command(index - 1)) : nullptr};
}

Session::LocalCopy Session::localCopy() const
{
    if (!m_exported)
        return LocalCopy::None;
    return stamp() == m_exportStamp ? LocalCopy::Current : LocalCopy::Behind;
}

void Session::markExported()
{
    m_exported = true;
    m_exportStamp = stamp();
    emit indicatorsChanged();
}

QString Session::reasonText(const QString& reason)
{
    if (reason == QLatin1String("closed"))
        return tr("El anfitrión terminó la sesión.");
    if (reason == QLatin1String("kicked"))
        return tr("El anfitrión te sacó de la sesión.");
    if (reason == QLatin1String("password"))
        return tr("La contraseña no es correcta.");
    if (reason == QLatin1String("device"))
        return tr("Este equipo no está en la lista de equipos autorizados del anfitrión.");
    if (reason == QLatin1String("rejected"))
        return tr("El anfitrión no aceptó tu solicitud.");
    if (reason == QLatin1String("version"))
        return tr("La sesión usa otra versión del modo colaborativo.");
    if (reason == QLatin1String("lost"))
        return tr("Se perdió la conexión con la sesión.");
    return reason;
}

void Session::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void Session::finish(const QString& reason)
{
    if (m_state == State::Ended)
        return;
    setState(State::Ended);
    m_queueTimer.stop();
    m_draftTimer.stop();
    m_cursorTimer.stop();
    m_queue.clear();
    if (m_server)
        m_server->stop(QStringLiteral("closed"));
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
    }
    m_cursors.clear();
    emit cursorsChanged();
    emit ended(reason);
}

void Session::leave()
{
    if (m_socket && m_state != State::Ended)
        send(QJsonObject{{QStringLiteral("t"), QStringLiteral("leave")}});
    finish(QString());
}

void Session::send(const QJsonObject& message)
{
    if (m_socket)
        m_socket->sendText(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// Anfitrión

void Session::onHostMessage(const QJsonObject& m)
{
    const QString t = m.value(QStringLiteral("t")).toString();
    const int from = m.value(QStringLiteral("from")).toInt();
    const Peer p = m_server->peer(from);
    if (t == QLatin1String("cursor")) {
        RemoteCursor& c = m_cursors[from];
        c.pos = QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble());
        c.tool = m.value(QStringLiteral("tool")).toString();
        c.inside = m.value(QStringLiteral("inside")).toBool();
        c.time = QDateTime::currentMSecsSinceEpoch();
        emit cursorsChanged();
        return;
    }
    if (t == QLatin1String("revert")) {
        // Volver a una versión anterior: solo los invitados con permiso.
        if (!p.history)
            return;
        const int index = m.value(QStringLiteral("index")).toInt(-1);
        if (m_doc && index >= 0 && index <= m_doc->undoStack()->count() && index != m_doc->undoStack()->index()) {
            emit notice(tr("%1 volvió a una versión anterior de la imagen.").arg(p.name));
            emit revertRequested(index);
        }
        return;
    }
    const QJsonArray ops = m.value(QStringLiteral("ops")).toArray();
    const QString label = m.value(QStringLiteral("label")).toString().left(200);
    if (t == QLatin1String("change") && p.mode == QLatin1String("live")) {
        Incoming in;
        in.kind = Incoming::Apply;
        in.ops = ops;
        in.label = label;
        in.from = from;
        in.name = p.name;
        enqueue(in);
        return;
    }
    if (t == QLatin1String("propose") || t == QLatin1String("change")) {
        Proposal pr;
        pr.pid = m.value(QStringLiteral("pid")).toInt();
        pr.from = from;
        pr.name = p.name;
        pr.label = label;
        pr.ops = ops;
        pr.time = QDateTime::currentDateTime();
        // La nueva incluye lo de la anterior (todo lo que no se ha aceptado aún).
        m_proposals.erase(std::remove_if(m_proposals.begin(), m_proposals.end(), [from](const Proposal& x) { return x.from == from; }),
                          m_proposals.end());
        m_proposals.append(pr);
        if (m_autoAccept) {
            acceptProposal(from);
            return;
        }
        emit proposalsChanged();
        emit notice(tr("%1 sugiere un cambio.").arg(p.name));
    }
}

void Session::onPeerJoined(const Peer& p)
{
    m_waiting.remove(p.id);
    m_peers.insert(p.id, p);
    QJsonArray peers;
    for (const Peer& x : m_server->peers())
        peers.append(x.toJson());
    m_server->sendTo(p.id, QJsonObject{{QStringLiteral("t"), QStringLiteral("welcome")},
                                       {QStringLiteral("mode"), p.mode},
                                       {QStringLiteral("autoAccept"), m_autoAccept},
                                       {QStringLiteral("peers"), peers},
                                       {QStringLiteral("doc"), snapshot(m_canon)}});
    m_server->sendTo(p.id, historyMessage());
    // Los punteros que ya se conocen.
    for (auto it = m_cursors.cbegin(); it != m_cursors.cend(); ++it) {
        if (it.key() == p.id)
            continue;
        m_server->sendTo(p.id, QJsonObject{{QStringLiteral("t"), QStringLiteral("cursor")},
                                           {QStringLiteral("from"), it.key()},
                                           {QStringLiteral("x"), it->pos.x()},
                                           {QStringLiteral("y"), it->pos.y()},
                                           {QStringLiteral("tool"), it->tool},
                                           {QStringLiteral("inside"), it->inside}});
    }
    if (!m_cursorOut.isEmpty()) {
        QJsonObject own = m_cursorOut;
        own.insert(QStringLiteral("from"), 0);
        m_server->sendTo(p.id, own);
    }
    emit peersChanged();
    emit notice(tr("%1 se unió a la sesión.").arg(p.name));
}

void Session::acceptProposal(int from)
{
    for (int i = 0; i < m_proposals.size(); ++i) {
        if (m_proposals[i].from != from)
            continue;
        const Proposal pr = m_proposals.takeAt(i);
        Incoming in;
        in.kind = Incoming::Apply;
        in.ops = pr.ops;
        in.label = pr.label;
        in.from = pr.from;
        in.name = pr.name;
        in.pid = pr.pid;
        emit proposalsChanged();
        enqueue(in);
        return;
    }
}

void Session::rejectProposal(int from)
{
    for (int i = 0; i < m_proposals.size(); ++i) {
        if (m_proposals[i].from != from)
            continue;
        const Proposal pr = m_proposals.takeAt(i);
        m_server->sendTo(from, QJsonObject{{QStringLiteral("t"), QStringLiteral("proposal")},
                                           {QStringLiteral("pid"), pr.pid},
                                           {QStringLiteral("state"), QStringLiteral("rejected")}});
        emit proposalsChanged();
        return;
    }
}

void Session::acceptAll()
{
    while (!m_proposals.isEmpty())
        acceptProposal(m_proposals.first().from);
}

void Session::admit(int id, bool ok)
{
    m_waiting.remove(id);
    if (m_server)
        m_server->admit(id, ok);
    emit peersChanged();
}

void Session::kick(int id)
{
    if (m_server)
        m_server->kick(id);
}

void Session::updateSettings(const Settings& settings)
{
    const bool nameChanged = settings.displayName() != m_settings.displayName();
    m_settings = settings;
    if (!m_server)
        return;
    m_autoAccept = settings.autoAccept;
    m_server->setSettings(settings);
    if (nameChanged)
        m_server->renameHost(settings.displayName());
    if (m_autoAccept)
        acceptAll();
    emit stateChanged();
}

void Session::broadcastCommit(const QJsonArray& ops, int from, const QString& name, const QString& label, int pid)
{
    QJsonObject m{{QStringLiteral("t"), QStringLiteral("commit")}, {QStringLiteral("rev"), ++m_rev},
                  {QStringLiteral("from"), from},          {QStringLiteral("name"), name},
                  {QStringLiteral("label"), label},        {QStringLiteral("ops"), ops}};
    if (pid)
        m.insert(QStringLiteral("pid"), pid);
    m_server->broadcast(m);
}

QImage Session::preview(const Proposal& proposal, QSize maxSize, QRect* bounds) const
{
    Canon c = m_canon;
    QRect area;
    for (const QJsonValue& v : proposal.ops) {
        const QJsonObject op = applyToCanon(c, v.toObject());
        if (op.value(QStringLiteral("op")).toString() == QLatin1String("pixels"))
            area |= QRect(op.value(QStringLiteral("x")).toInt(), op.value(QStringLiteral("y")).toInt(),
                          op.value(QStringLiteral("w")).toInt(), op.value(QStringLiteral("h")).toInt());
        else if (!op.isEmpty())
            area = QRect(QPoint(0, 0), c.size);
    }
    if (area.isEmpty())
        area = QRect(QPoint(0, 0), c.size);
    // Un poco de margen alrededor, para ver dónde está.
    const int margin = std::max(8, std::max(area.width(), area.height()) / 8);
    area = area.adjusted(-margin, -margin, margin, margin).intersected(QRect(QPoint(0, 0), c.size));
    if (bounds)
        *bounds = area;
    Document tmp(area.size());
    QList<LayerPtr> layers;
    for (const CanonLayer& l : c.layers)
        layers.append(LayerPtr(newLayer(l.props, l.image.copy(area))));
    if (layers.isEmpty())
        return {};
    tmp.replaceState(area.size(), layers, int(layers.size()) - 1);
    QImage out = tmp.flattened();
    if (out.width() > maxSize.width() || out.height() > maxSize.height())
        out = out.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return out;
}

// ---------------------------------------------------------------------------
// Invitado

void Session::onGuestText(const QByteArray& text)
{
    const QJsonObject m = QJsonDocument::fromJson(text).object();
    const QString t = m.value(QStringLiteral("t")).toString();
    if (t == QLatin1String("hello")) {
        m_sessionName = m.value(QStringLiteral("session")).toString().left(80);
        const Access auth = accessFromName(m.value(QStringLiteral("auth")).toString());
        const QByteArray nonce = QByteArray::fromBase64(m.value(QStringLiteral("nonce")).toString().toLatin1());
        if (m.value(QStringLiteral("v")).toInt() != kVersion) {
            finish(reasonText(QStringLiteral("version")));
            return;
        }
        if (auth == Access::Password && m_password.isEmpty()) {
            finish(tr("Esta sesión pide contraseña."));
            return;
        }
        const QString code = deviceCode();
        QJsonObject join{{QStringLiteral("t"), QStringLiteral("join")},
                         {QStringLiteral("v"), kVersion},
                         {QStringLiteral("name"), m_settings.displayName()},
                         {QStringLiteral("platform"), QStringLiteral("desktop")},
                         {QStringLiteral("app"), appName()},
                         {QStringLiteral("device"), deviceTag(code)},
                         {QStringLiteral("proof"), auth == Access::Password ? proof(m_password, nonce)
                                                                            : proof(normalizedCode(code), nonce)}};
        if (m_settings.color.isValid())
            join.insert(QStringLiteral("color"), m_settings.color.name());
        send(join);
    } else if (t == QLatin1String("waiting")) {
        setState(State::Waiting);
    } else if (t == QLatin1String("denied")) {
        finish(reasonText(m.value(QStringLiteral("reason")).toString()));
    } else if (t == QLatin1String("joined")) {
        m_myId = m.value(QStringLiteral("you")).toInt();
        m_idPrefix = QString::number(m_myId) + QLatin1Char('-');
        m_mode = m.value(QStringLiteral("mode")).toString(QStringLiteral("confirm"));
    } else if (t == QLatin1String("welcome")) {
        onWelcome(m);
    } else if (t == QLatin1String("peer")) {
        const Peer p = Peer::fromJson(m.value(QStringLiteral("peer")).toObject());
        const QString op = m.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("remove")) {
            m_peers.remove(p.id);
            m_cursors.remove(p.id);
            emit cursorsChanged();
            emit notice(tr("%1 salió de la sesión.").arg(p.name));
        } else {
            if (op == QLatin1String("add"))
                emit notice(tr("%1 se unió a la sesión.").arg(p.name));
            m_peers.insert(p.id, p);
        }
        emit peersChanged();
    } else if (t == QLatin1String("cursor")) {
        const int from = m.value(QStringLiteral("from")).toInt();
        if (from == m_myId)
            return;
        RemoteCursor& c = m_cursors[from];
        c.pos = QPointF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble());
        c.tool = m.value(QStringLiteral("tool")).toString();
        c.inside = m.value(QStringLiteral("inside")).toBool();
        c.time = QDateTime::currentMSecsSinceEpoch();
        emit cursorsChanged();
    } else if (t == QLatin1String("commit")) {
        if (m_state != State::Joined)
            return;
        Incoming in;
        in.kind = Incoming::Commit;
        in.ops = m.value(QStringLiteral("ops")).toArray();
        in.label = m.value(QStringLiteral("label")).toString();
        in.from = m.value(QStringLiteral("from")).toInt();
        in.name = m.value(QStringLiteral("name")).toString().left(40);
        in.pid = m.value(QStringLiteral("pid")).toInt();
        m_rev = m.value(QStringLiteral("rev")).toInt(m_rev);
        enqueue(in);
    } else if (t == QLatin1String("proposal")) {
        const int pid = m.value(QStringLiteral("pid")).toInt();
        if (m.value(QStringLiteral("state")).toString() == QLatin1String("accepted")) {
            Incoming in;
            in.kind = Incoming::Accepted;
            in.pid = pid;
            enqueue(in);
        } else {
            if (pid == m_pendingPid)
                m_pendingPid = 0;
            emit draftChanged();
            emit notice(tr("El anfitrión rechazó tus cambios. Siguen en tu borrador."));
        }
    } else if (t == QLatin1String("settings")) {
        const QString mode = m.value(QStringLiteral("mode")).toString(m_mode);
        m_autoAccept = m.value(QStringLiteral("autoAccept")).toBool();
        if (mode != m_mode) {
            m_mode = mode;
            if (!isLive() && m_doc) {
                m_draftStart = m_doc->undoStack()->index();
                m_inFlight = 0;
                m_draftTimer.start();
            }
            if (isLive() && m_doc) {
                // Lo que quedaba en el borrador pasa a ser un cambio normal.
                const QJsonArray ops = diff();
                if (!ops.isEmpty()) {
                    adoptWorking();
                    ++m_inFlight;
                    send(QJsonObject{{QStringLiteral("t"), QStringLiteral("change")},
                                     {QStringLiteral("label"), tr("Borrador")},
                                     {QStringLiteral("ops"), ops}});
                }
                m_hasDraft = false;
                emit draftChanged();
            }
        }
        emit stateChanged();
    } else if (t == QLatin1String("history")) {
        m_hostHistory.clear();
        for (const QJsonValue& v : m.value(QStringLiteral("steps")).toArray())
            m_hostHistory.append(v.toString().left(200));
        m_hostHistoryIndex = std::clamp(m.value(QStringLiteral("index")).toInt(), 0, int(m_hostHistory.size()));
        m_hostSaved = m.value(QStringLiteral("saved")).toBool();
        emit historyChanged();
        emit indicatorsChanged();
    } else if (t == QLatin1String("end")) {
        finish(reasonText(m.value(QStringLiteral("reason")).toString()));
    }
}

void Session::onWelcome(const QJsonObject& m)
{
    Canon c;
    if (!readSnapshot(m.value(QStringLiteral("doc")).toObject(), c)) {
        finish(tr("La imagen de la sesión no es válida."));
        return;
    }
    m_canon = c;
    m_mode = m.value(QStringLiteral("mode")).toString(m_mode);
    m_autoAccept = m.value(QStringLiteral("autoAccept")).toBool();
    m_peers.clear();
    for (const QJsonValue& v : m.value(QStringLiteral("peers")).toArray()) {
        const Peer p = Peer::fromJson(v.toObject());
        m_peers.insert(p.id, p);
    }
    auto* doc = new Document(c.size);
    doc->setDpi(c.dpi);
    QList<LayerPtr> layers;
    for (const CanonLayer& cl : std::as_const(m_canon.layers)) {
        LayerPtr l(newLayer(cl.props, cl.image));
        m_idOf.insert(l.get(), cl.id);
        m_layerById.insert(cl.id, l);
        layers.append(l);
    }
    doc->replaceState(c.size, layers, int(layers.size()) - 1);
    doc->setUntitledName(c.name.isEmpty() ? m_sessionName : c.name);
    doc->setHistoryRootLabel(tr("Unirse a la sesión"));
    attachDocument(doc);
    setState(State::Joined);
    emit peersChanged();
    emit documentReady(doc);
}

bool Session::propose()
{
    if (!m_doc || isHost() || m_state != State::Joined)
        return false;
    const QJsonArray ops = diff();
    if (ops.isEmpty()) {
        m_hasDraft = false;
        emit draftChanged();
        emit notice(tr("No hay cambios que sugerir."));
        return false;
    }
    // Nombre: los pasos propios del borrador (no los de otros).
    QStringList steps;
    const QUndoStack* stack = m_doc->undoStack();
    const QString remote = tr("Cambio hecho por %1").arg(QString());
    for (int i = std::min(m_draftStart, stack->index()); i < stack->index(); ++i) {
        const QString text = stack->text(i);
        if (!text.isEmpty() && !text.startsWith(remote) && !steps.contains(text))
            steps.append(text);
    }
    QString label = steps.mid(0, 3).join(QStringLiteral(", "));
    if (steps.size() > 3)
        label += QStringLiteral("…");
    if (label.isEmpty())
        label = tr("Cambios");
    m_pendingPid = ++m_nextPid;
    m_hasDraft = true;
    send(QJsonObject{{QStringLiteral("t"), QStringLiteral("propose")},
                     {QStringLiteral("pid"), m_pendingPid},
                     {QStringLiteral("label"), label},
                     {QStringLiteral("ops"), ops}});
    emit draftChanged();
    emit notice(tr("Cambios enviados al anfitrión."));
    return true;
}

void Session::discardDraft()
{
    if (!m_doc || isHost())
        return;
    m_applying = true;
    DocumentState work = DocumentState::capture(m_doc);
    const Layer* active = m_doc->activeLayer();
    QList<LayerPtr> layers;
    QList<QImage> images;
    QList<LayerProps> props;
    QHash<QString, LayerPtr> byId;
    QHash<const Layer*, QString> idOf;
    int activeIndex = -1;
    for (const CanonLayer& cl : std::as_const(m_canon.layers)) {
        LayerPtr l = m_layerById.value(cl.id);
        if (!l)
            l = LayerPtr(newLayer(cl.props, cl.image));
        if (l.get() == active)
            activeIndex = int(layers.size());
        byId.insert(cl.id, l);
        idOf.insert(l.get(), cl.id);
        layers.append(l);
        images.append(cl.image);
        props.append(cl.props);
    }
    m_layerById = byId;
    m_idOf = idOf;
    if (work.size != m_canon.size)
        work.selection = Selection();
    work.size = m_canon.size;
    work.dpi = m_canon.dpi;
    work.layers = layers;
    work.images = images;
    work.props = props;
    work.active = activeIndex >= 0 ? activeIndex : int(layers.size()) - 1;
    work.apply(m_doc);
    m_doc->undoStack()->clear();
    ++m_historyGeneration;
    m_lastIndex = 0;
    m_draftStart = 0;
    m_applying = false;
    m_hasDraft = false;
    emit draftChanged();
}

void Session::updateDraft()
{
    if (isHost() || isLive() || !m_doc)
        return;
    bool differs = m_doc->size() != m_canon.size || m_doc->layerCount() != m_canon.layers.size();
    for (int i = 0; !differs && i < m_doc->layerCount(); ++i) {
        const Layer* l = m_doc->layer(i);
        const CanonLayer& cl = m_canon.layers[i];
        QRect r;
        differs = m_idOf.value(l) != cl.id || l->props != cl.props || differingBounds(cl.image, l->image, &r);
    }
    if (differs != m_hasDraft) {
        m_hasDraft = differs;
        emit draftChanged();
    }
}

// ---------------------------------------------------------------------------
// Documento

void Session::attachDocument(Document* doc)
{
    m_doc = doc;
    m_lastIndex = doc->undoStack()->index();
    connect(doc->undoStack(), &QUndoStack::indexChanged, this, &Session::onLocalStep);
    // Anfitrión: su historial y si está guardada, para los invitados (y su pestaña). Invitado: si su
    // copia propia sigue al día.
    connect(doc->undoStack(), &QUndoStack::indexChanged, this, [this] {
        if (isHost())
            m_historyTimer.start();
        else
            emit indicatorsChanged();
    });
    connect(doc->undoStack(), &QUndoStack::cleanChanged, this, [this] {
        if (isHost())
            m_historyTimer.start();
    });
    connect(doc, &QObject::destroyed, this, [this] {
        if (m_state != State::Ended)
            leave();
    });
}

void Session::onLocalStep()
{
    if (m_applying || !m_doc || m_state != State::Joined)
        return;
    const QUndoStack* stack = m_doc->undoStack();
    const int index = stack->index();
    QString label;
    if (index > m_lastIndex)
        label = stack->text(index - 1);
    else if (index < m_lastIndex)
        label = tr("Deshacer: %1").arg(stack->text(index));
    m_lastIndex = index;
    if (isHost()) {
        const QJsonArray ops = diff();
        if (ops.isEmpty())
            return;
        adoptWorking();
        broadcastCommit(ops, 0, m_server->peer(0).name, label, 0);
    } else if (isLive()) {
        const QJsonArray ops = diff();
        if (ops.isEmpty())
            return;
        adoptWorking();
        ++m_inFlight;
        send(QJsonObject{{QStringLiteral("t"), QStringLiteral("change")}, {QStringLiteral("label"), label}, {QStringLiteral("ops"), ops}});
    } else {
        m_draftTimer.start();
    }
}

QString Session::idFor(const LayerPtr& layer)
{
    if (const auto it = m_idOf.constFind(layer.get()); it != m_idOf.constEnd())
        return *it;
    QString id;
    do
        id = m_idPrefix + QString::number(++m_idCounter);
    while (m_layerById.contains(id));
    m_idOf.insert(layer.get(), id);
    m_layerById.insert(id, layer);
    return id;
}

void Session::adoptWorking()
{
    Canon c;
    c.size = m_doc->size();
    c.dpi = m_doc->dpi();
    c.name = m_canon.name;
    QHash<QString, LayerPtr> byId;
    QHash<const Layer*, QString> idOf;
    for (const LayerPtr& l : m_doc->layers()) {
        const QString id = idFor(l);
        c.layers.append({id, l->props, l->image});
        byId.insert(id, l);
        idOf.insert(l.get(), id);
    }
    m_canon = c;
    m_layerById = byId;
    m_idOf = idOf;
}

QString Session::pngFor(const QImage& image) const
{
    const qint64 key = image.cacheKey();
    if (const auto it = m_pngCache.constFind(key); it != m_pngCache.constEnd())
        return *it;
    const QString png = encodePng(image);
    if (m_pngCache.size() > 64)
        m_pngCache.clear();
    m_pngCache.insert(key, png);
    return png;
}

QJsonObject Session::snapshot(const Canon& canon) const
{
    QJsonArray layers;
    for (const CanonLayer& l : canon.layers) {
        QJsonObject o = propsJson(l.id, l.props);
        o.insert(QStringLiteral("png"), pngFor(l.image));
        layers.append(o);
    }
    return QJsonObject{{QStringLiteral("rev"), m_rev},
                       {QStringLiteral("w"), canon.size.width()},
                       {QStringLiteral("h"), canon.size.height()},
                       {QStringLiteral("dpi"), canon.dpi},
                       {QStringLiteral("name"), canon.name},
                       {QStringLiteral("layers"), layers}};
}

bool Session::readSnapshot(const QJsonObject& doc, Canon& out)
{
    const QSize size(doc.value(QStringLiteral("w")).toInt(), doc.value(QStringLiteral("h")).toInt());
    if (size.width() < 1 || size.height() < 1 || size.width() > 65535 || size.height() > 65535
        || qint64(size.width()) * size.height() > kMaxPixels)
        return false;
    Canon c;
    c.size = size;
    c.dpi = std::clamp(doc.value(QStringLiteral("dpi")).toDouble(96), 1.0, 100000.0);
    c.name = doc.value(QStringLiteral("name")).toString().left(200);
    QSet<QString> seen;
    for (const QJsonValue& v : doc.value(QStringLiteral("layers")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || seen.contains(id))
            continue;
        QImage image = decodePng(o.value(QStringLiteral("png")).toString());
        if (image.size() != size) {
            image = QImage(size, kLayerFormat);
            image.fill(kTransparent);
        }
        seen.insert(id);
        c.layers.append({id, propsFrom(o), image});
    }
    if (c.layers.isEmpty())
        return false;
    out = c;
    return true;
}

QJsonArray Session::diff()
{
    QJsonArray ops;
    const QList<LayerPtr> layers = m_doc->layers();
    if (m_doc->size() != m_canon.size || std::abs(m_doc->dpi() - m_canon.dpi) > 1e-9) {
        Canon w;
        w.size = m_doc->size();
        w.dpi = m_doc->dpi();
        w.name = m_canon.name;
        for (const LayerPtr& l : layers)
            w.layers.append({idFor(l), l->props, l->image});
        ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("doc")}, {QStringLiteral("doc"), snapshot(w)}});
        return ops;
    }
    QStringList order;
    for (const LayerPtr& l : layers)
        order.append(idFor(l));
    const QStringList canonOrder = m_canon.ids();
    bool structural = order != canonOrder;
    for (int i = 0; !structural && i < layers.size(); ++i)
        structural = layers[i]->props != m_canon.layers[i].props;
    if (structural) {
        QJsonArray list;
        for (int i = 0; i < layers.size(); ++i) {
            QJsonObject e = propsJson(order[i], layers[i]->props);
            if (m_canon.indexOf(order[i]) < 0)
                e.insert(QStringLiteral("png"), pngFor(layers[i]->image));
            list.append(e);
        }
        QJsonArray removed;
        for (const QString& id : canonOrder)
            if (!order.contains(id))
                removed.append(id);
        ops.append(QJsonObject{{QStringLiteral("op"), QStringLiteral("layers")},
                               {QStringLiteral("layers"), list},
                               {QStringLiteral("removed"), removed}});
    }
    for (int i = 0; i < layers.size(); ++i) {
        const int ci = m_canon.indexOf(order[i]);
        if (ci < 0)
            continue;
        const QImage& before = m_canon.layers[ci].image;
        const QImage& after = layers[i]->image;
        QRect r;
        if (!differingBounds(before, after, &r))
            continue;
        // Los píxeles que cambian; el resto, transparentes (no se escriben y comprimen mejor).
        QImage patch(r.size(), QImage::Format_ARGB32);
        patch.fill(0);
        QBitArray mask(qsizetype(r.width()) * r.height());
        bool all = true;
        for (int y = 0; y < r.height(); ++y) {
            const auto* pa = reinterpret_cast<const QRgb*>(before.constScanLine(r.y() + y)) + r.x();
            const auto* pb = reinterpret_cast<const QRgb*>(after.constScanLine(r.y() + y)) + r.x();
            auto* out = reinterpret_cast<QRgb*>(patch.scanLine(y));
            for (int x = 0; x < r.width(); ++x) {
                if (pa[x] != pb[x]) {
                    out[x] = pb[x];
                    mask.setBit(qsizetype(y) * r.width() + x);
                } else {
                    all = false;
                }
            }
        }
        QJsonObject op{{QStringLiteral("op"), QStringLiteral("pixels")},
                       {QStringLiteral("layer"), order[i]},
                       {QStringLiteral("x"), r.x()},
                       {QStringLiteral("y"), r.y()},
                       {QStringLiteral("w"), r.width()},
                       {QStringLiteral("h"), r.height()},
                       {QStringLiteral("png"), encodePng(patch)}};
        if (!all)
            op.insert(QStringLiteral("mask"), encodeMask(mask));
        ops.append(op);
    }
    return ops;
}

// ---------------------------------------------------------------------------
// Lo que llega de los demás

void Session::enqueue(const Incoming& item)
{
    m_queue.append(item);
    processQueue();
}

bool Session::blocked(const QJsonArray& ops) const
{
    if (!m_busyCheck)
        return false;
    const Layer* editing = nullptr;
    if (!m_busyCheck(&editing))
        return false;
    for (const QJsonValue& v : ops) {
        const QJsonObject op = v.toObject();
        if (op.value(QStringLiteral("op")).toString() != QLatin1String("pixels"))
            return true;
        if (!editing || m_layerById.value(op.value(QStringLiteral("layer")).toString()).get() == editing)
            return true;
    }
    return false;
}

void Session::processQueue()
{
    if (m_processing)
        return;
    m_processing = true;
    while (!m_queue.isEmpty() && m_doc && m_state == State::Joined) {
        const Incoming& head = m_queue.first();
        const bool wait = head.kind == Incoming::Accepted ? (m_busyCheck && m_busyCheck(nullptr)) : blocked(head.ops);
        if (wait) {
            if (!m_queueTimer.isActive())
                m_queueTimer.start();
            m_processing = false;
            return;
        }
        const Incoming item = m_queue.takeFirst();
        switch (item.kind) {
        case Incoming::Apply: {
            // Anfitrión: el cambio de un invitado pasa a la imagen y a todos.
            const QJsonArray ops = apply(item.ops, tr("Cambio hecho por %1").arg(item.name));
            if (!ops.isEmpty())
                broadcastCommit(ops, item.from, item.name, item.label, item.pid);
            if (item.pid)
                m_server->sendTo(item.from, QJsonObject{{QStringLiteral("t"), QStringLiteral("proposal")},
                                                        {QStringLiteral("pid"), item.pid},
                                                        {QStringLiteral("state"), QStringLiteral("accepted")}});
            break;
        }
        case Incoming::Commit: {
            const bool ownEcho = item.from == m_myId;
            if (ownEcho && m_inFlight > 0)
                --m_inFlight;
            apply(item.ops, tr("Cambio hecho por %1").arg(item.name), ownEcho && m_inFlight > 0);
            if (!isLive())
                m_draftTimer.start();
            break;
        }
        case Incoming::Accepted:
            if (item.pid == m_pendingPid)
                m_pendingPid = 0;
            // El borrador ya es de todos: su historial temporal se borra.
            m_applying = true;
            m_doc->undoStack()->clear();
            ++m_historyGeneration;
            m_lastIndex = 0;
            m_draftStart = 0;
            m_applying = false;
            updateDraft();
            emit draftChanged();
            emit notice(tr("El anfitrión aceptó tus cambios."));
            break;
        }
    }
    m_queueTimer.stop();
    m_processing = false;
}

QJsonArray Session::apply(const QJsonArray& ops, const QString& historyText, bool canonOnly)
{
    QJsonArray effective;
    if (!m_doc)
        return effective;
    if (canonOnly) {
        for (const QJsonValue& v : ops)
            if (const QJsonObject op = applyToCanon(m_canon, v.toObject()); !op.isEmpty())
                effective.append(op);
        return effective;
    }
    m_applying = true;
    const DocumentState before = DocumentState::capture(m_doc);
    DocumentState work = before;
    QHash<int, QRect> changed;
    bool structural = false;
    for (const QJsonValue& v : ops) {
        const Canon old = m_canon;
        const QJsonObject op = applyToCanon(m_canon, v.toObject());
        if (op.isEmpty())
            continue;
        effective.append(op);
        applyToWork(old, m_canon, op, work, changed, structural);
    }
    QUndoStack* stack = m_doc->undoStack();
    HistoryIcons::registerIcon(historyText, kHistoryIcon);
    // ¿Cambia algo de verdad? (el eco de lo propio, por ejemplo, no)
    auto sameState = [&] {
        if (before.size != work.size || before.layers != work.layers || before.props != work.props || std::abs(before.dpi - work.dpi) > 1e-9)
            return false;
        for (int i = 0; i < before.images.size(); ++i)
            if (before.images[i].constBits() != work.images[i].constBits() && before.images[i] != work.images[i])
                return false;
        return true;
    };
    if (structural) {
        if (!sameState())
            stack->push(new DocumentStateCommand(m_doc, before, work, historyText));
        else
            for (int i = 0; i < work.layers.size(); ++i)
                work.layers[i]->image = work.images[i];
    } else if (!changed.isEmpty()) {
        auto* macro = new QUndoCommand(historyText);
        int steps = 0;
        for (auto it = changed.cbegin(); it != changed.cend(); ++it) {
            const LayerPtr& layer = work.layers[it.key()];
            const QImage old = layer->image;
            layer->image = work.images[it.key()];
            auto* patch = new PixelPatchCommand(m_doc, layer, old, it.value(), historyText, macro);
            if (!patch->isEmpty())
                ++steps;
        }
        if (steps > 0)
            stack->push(macro);
        else
            delete macro;
    }
    m_lastIndex = stack->index();
    m_applying = false;
    return effective;
}

QJsonObject Session::applyToCanon(Canon& canon, const QJsonObject& op)
{
    const QString kind = op.value(QStringLiteral("op")).toString();
    if (kind == QLatin1String("pixels")) {
        const int ci = canon.indexOf(op.value(QStringLiteral("layer")).toString());
        const QRect r(op.value(QStringLiteral("x")).toInt(), op.value(QStringLiteral("y")).toInt(), op.value(QStringLiteral("w")).toInt(),
                      op.value(QStringLiteral("h")).toInt());
        if (ci < 0 || r.isEmpty() || !QRect(QPoint(0, 0), canon.size).contains(r))
            return {};
        const QImage patch = decodePng(op.value(QStringLiteral("png")).toString());
        if (patch.size() != r.size())
            return {};
        const bool hasMask = op.contains(QStringLiteral("mask"));
        const QBitArray mask = hasMask ? decodeMask(op.value(QStringLiteral("mask")).toString(), r.width() * r.height()) : QBitArray();
        QImage& image = canon.layers[ci].image;
        for (int y = 0; y < r.height(); ++y) {
            auto* c = reinterpret_cast<QRgb*>(image.scanLine(r.y() + y)) + r.x();
            const auto* p = reinterpret_cast<const QRgb*>(patch.constScanLine(y));
            for (int x = 0; x < r.width(); ++x)
                if (!hasMask || mask.testBit(qsizetype(y) * r.width() + x))
                    c[x] = p[x];
        }
        return op;
    }
    if (kind == QLatin1String("layers")) {
        QSet<QString> removed;
        for (const QJsonValue& v : op.value(QStringLiteral("removed")).toArray())
            if (canon.indexOf(v.toString()) >= 0)
                removed.insert(v.toString());
        QStringList listed;
        QHash<QString, QJsonObject> entries;
        QHash<QString, QImage> created;
        QJsonArray cleanList;
        for (const QJsonValue& v : op.value(QStringLiteral("layers")).toArray()) {
            QJsonObject e = v.toObject();
            const QString id = e.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || id.size() > 64 || entries.contains(id) || removed.contains(id))
                continue;
            if (canon.indexOf(id) < 0) {
                const QImage image = decodePng(e.value(QStringLiteral("png")).toString());
                if (image.size() != canon.size)
                    continue;
                created.insert(id, image);
            } else {
                e.remove(QStringLiteral("png"));
            }
            listed.append(id);
            entries.insert(id, e);
            cleanList.append(e);
        }
        const QStringList order = mergeOrder(canon.ids(), listed, removed);
        QList<CanonLayer> result;
        for (const QString& id : order) {
            const int ci = canon.indexOf(id);
            if (ci >= 0) {
                CanonLayer l = canon.layers[ci];
                if (entries.contains(id))
                    l.props = propsFrom(entries.value(id), l.props);
                result.append(l);
            } else {
                result.append({id, propsFrom(entries.value(id)), created.value(id)});
            }
        }
        // Nunca sin capas.
        if (result.isEmpty())
            return {};
        canon.layers = result;
        QJsonArray cleanRemoved;
        for (const QString& id : removed)
            cleanRemoved.append(id);
        return QJsonObject{{QStringLiteral("op"), QStringLiteral("layers")},
                           {QStringLiteral("layers"), cleanList},
                           {QStringLiteral("removed"), cleanRemoved}};
    }
    if (kind == QLatin1String("doc")) {
        Canon c;
        if (!readSnapshot(op.value(QStringLiteral("doc")).toObject(), c))
            return {};
        if (c.name.isEmpty())
            c.name = canon.name;
        canon = c;
        return op;
    }
    return {};
}

void Session::applyToWork(const Canon& before, const Canon& after, const QJsonObject& op, DocumentState& work,
                          QHash<int, QRect>& changed, bool& structural)
{
    const QString kind = op.value(QStringLiteral("op")).toString();
    auto workIndex = [&](const QString& id) {
        for (int i = 0; i < work.layers.size(); ++i)
            if (m_idOf.value(work.layers[i].get()) == id)
                return i;
        return -1;
    };

    if (kind == QLatin1String("pixels")) {
        const QString id = op.value(QStringLiteral("layer")).toString();
        const int wi = workIndex(id);
        const int bi = before.indexOf(id), ai = after.indexOf(id);
        if (wi < 0 || bi < 0 || ai < 0)
            return;
        const QRect r(op.value(QStringLiteral("x")).toInt(), op.value(QStringLiteral("y")).toInt(), op.value(QStringLiteral("w")).toInt(),
                      op.value(QStringLiteral("h")).toInt());
        const QImage& oldC = before.layers[bi].image;
        const QImage& newC = after.layers[ai].image;
        QImage& w = work.images[wi];
        if (w.size() != oldC.size())
            return;
        if (w.constBits() == oldC.constBits()) {
            // Sin cambios propios en esta capa: queda igual que la canónica (y solo hay paso en el
            // historial si de verdad cambia algo; el eco de lo propio no cambia nada).
            bool differs = false;
            for (int y = r.top(); y <= r.bottom() && !differs; ++y)
                differs = std::memcmp(oldC.constScanLine(y) + r.left() * 4, newC.constScanLine(y) + r.left() * 4, size_t(r.width()) * 4) != 0;
            w = newC;
            if (differs)
                changed[wi] |= r;
            return;
        }
        // Solo donde el documento sigue como estaba (lo propio sin enviar se respeta).
        int x0 = r.right() + 1, x1 = -1, y0 = -1, y1 = -1;
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const auto* o = reinterpret_cast<const QRgb*>(oldC.constScanLine(y));
            const auto* n = reinterpret_cast<const QRgb*>(newC.constScanLine(y));
            const auto* wc = reinterpret_cast<const QRgb*>(w.constScanLine(y));
            QRgb* wr = nullptr;
            for (int x = r.left(); x <= r.right(); ++x) {
                if (o[x] == n[x])
                    continue;
                const QRgb cur = wr ? wr[x] : wc[x];
                if (cur != o[x])
                    continue;
                if (!wr)
                    wr = reinterpret_cast<QRgb*>(w.scanLine(y));
                wr[x] = n[x];
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                if (y0 < 0)
                    y0 = y;
                y1 = y;
            }
        }
        if (y0 >= 0)
            changed[wi] |= QRect(QPoint(x0, y0), QPoint(x1, y1));
        return;
    }

    if (kind == QLatin1String("layers")) {
        structural = true;
        // Claves del documento de trabajo: su id, o una propia para las que nunca se enviaron.
        QStringList current;
        for (int i = 0; i < work.layers.size(); ++i) {
            const QString id = m_idOf.value(work.layers[i].get());
            current.append(id.isEmpty() ? QStringLiteral("~%1").arg(i) : id);
        }
        QSet<QString> removed;
        for (const QJsonValue& v : op.value(QStringLiteral("removed")).toArray())
            removed.insert(v.toString());
        QStringList listed;
        QHash<QString, QJsonObject> entries;
        for (const QJsonValue& v : op.value(QStringLiteral("layers")).toArray()) {
            const QJsonObject e = v.toObject();
            const QString id = e.value(QStringLiteral("id")).toString();
            // Las que se borraron aquí y aún no se ha enviado, siguen borradas.
            if (!current.contains(id) && (before.indexOf(id) >= 0 || m_layerById.contains(id)))
                continue;
            listed.append(id);
            entries.insert(id, e);
        }
        const QStringList order = mergeOrder(current, listed, removed);
        const Layer* active = work.active >= 0 && work.active < work.layers.size() ? work.layers[work.active].get() : nullptr;
        DocumentState next = work;
        next.layers.clear();
        next.images.clear();
        next.props.clear();
        int activeIndex = -1;
        for (const QString& key : order) {
            const int wi = int(current.indexOf(key));
            if (wi >= 0) {
                LayerProps props = work.props[wi];
                const int bi = before.indexOf(key), ai = after.indexOf(key);
                // Las propiedades cambian si aquí no se tocaron.
                if (bi >= 0 && ai >= 0 && props == before.layers[bi].props)
                    props = after.layers[ai].props;
                if (work.layers[wi].get() == active)
                    activeIndex = int(next.layers.size());
                next.layers.append(work.layers[wi]);
                next.images.append(work.images[wi]);
                next.props.append(props);
            } else {
                const int ai = after.indexOf(key);
                if (ai < 0)
                    continue;
                const CanonLayer& cl = after.layers[ai];
                LayerPtr l(newLayer(cl.props, cl.image));
                m_idOf.insert(l.get(), key);
                m_layerById.insert(key, l);
                next.layers.append(l);
                next.images.append(cl.image);
                next.props.append(cl.props);
            }
        }
        // Las borradas dejan de tener id.
        for (int i = 0; i < work.layers.size(); ++i) {
            if (next.layers.contains(work.layers[i]))
                continue;
            const QString id = m_idOf.take(work.layers[i].get());
            if (!id.isEmpty())
                m_layerById.remove(id);
        }
        if (next.layers.isEmpty())
            return;
        next.active = activeIndex >= 0 ? activeIndex : std::clamp(work.active, 0, int(next.layers.size()) - 1);
        work = next;
        return;
    }

    if (kind == QLatin1String("doc")) {
        structural = true;
        const Layer* active = work.active >= 0 && work.active < work.layers.size() ? work.layers[work.active].get() : nullptr;
        DocumentState next = work;
        next.layers.clear();
        next.images.clear();
        next.props.clear();
        int activeIndex = -1;
        QHash<QString, LayerPtr> byId;
        QHash<const Layer*, QString> idOf;
        for (const CanonLayer& cl : after.layers) {
            LayerPtr l = m_layerById.value(cl.id);
            if (!l)
                l = LayerPtr(newLayer(cl.props, cl.image));
            if (l.get() == active)
                activeIndex = int(next.layers.size());
            byId.insert(cl.id, l);
            idOf.insert(l.get(), cl.id);
            next.layers.append(l);
            next.images.append(cl.image);
            next.props.append(cl.props);
        }
        m_layerById = byId;
        m_idOf = idOf;
        if (next.size != after.size)
            next.selection = Selection();
        next.size = after.size;
        next.dpi = after.dpi;
        next.active = activeIndex >= 0 ? activeIndex : int(next.layers.size()) - 1;
        work = next;
        changed.clear();
    }
}

// ---------------------------------------------------------------------------

void Session::setCursor(QPointF pos, bool inside, const QString& tool)
{
    if (m_state != State::Joined)
        return;
    m_cursorOut = QJsonObject{{QStringLiteral("t"), QStringLiteral("cursor")},
                              {QStringLiteral("x"), std::round(pos.x() * 10) / 10},
                              {QStringLiteral("y"), std::round(pos.y() * 10) / 10},
                              {QStringLiteral("tool"), tool},
                              {QStringLiteral("inside"), inside}};
    if (m_cursorTimer.isActive()) {
        m_cursorDirty = true;
        return;
    }
    if (isHost())
        m_server->broadcastCursor(m_cursorOut);
    else
        send(m_cursorOut);
    m_cursorTimer.start();
}

void Session::rename(const QString& name)
{
    m_settings.name = name;
    if (m_server)
        m_server->renameHost(m_settings.displayName());
    else
        send(QJsonObject{{QStringLiteral("t"), QStringLiteral("rename")}, {QStringLiteral("name"), m_settings.displayName()}});
}

} // namespace Collab
