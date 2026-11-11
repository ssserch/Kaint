#include "app/collab/P2P.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QUrl>

#ifdef KAINT_HAVE_P2P
#include <rtc/rtc.hpp>
#endif

#include <functional>
#include <memory>

namespace Collab {

namespace {

// Los mensajes largos (la imagen al unirse) van en trozos: los canales de datos tienen un tamaño
// máximo por mensaje. "+…" es un trozo al que le siguen más y "=…" el último; los demás (el JSON,
// "{…") van enteros. Igual que en Kaint para la web.
constexpr qsizetype kChunk = 16 * 1024;
// Un canal que no termina de abrirse se descarta.
constexpr int kPendingMs = 40000;
// El invitado busca la sesión durante este tiempo.
constexpr int kJoinMs = 45000;
// Cada cuánto manda nuevas ofertas mientras busca, y cuántas a la vez.
constexpr int kRoundMs = 9000;
constexpr int kOffersPerRound = 2;
// Cada cuánto el anfitrión le recuerda al rastreador que sigue ahí.
constexpr int kAnnounceMs = 30000;

QString randomId()
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString s;
    for (int i = 0; i < 20; ++i)
        s += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(62)]);
    return s;
}

QByteArray signingKey(const QString& code)
{
    return ("kaint-p2p:" + normalizedSessionCode(code)).toUtf8();
}

QString withoutCarriageReturns(const QString& sdp)
{
    QString s = sdp;
    s.remove(QLatin1Char('\r'));
    return s.trimmed();
}

QString withCrlf(const QString& lfText)
{
    QString s = lfText;
    s.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    return s + QStringLiteral("\r\n");
}

QString mac(const QString& code, const QString& kind, const QString& base)
{
    return QString::fromLatin1(QMessageAuthenticationCode::hash((kind + QLatin1Char('\n') + base).toUtf8(), signingKey(code),
                                                                QCryptographicHash::Sha256)
                                   .toBase64());
}

} // namespace

bool p2pAvailable()
{
#ifdef KAINT_HAVE_P2P
    return true;
#else
    return false;
#endif
}

QString p2pRoom(const QString& code)
{
    const QByteArray digest = QCryptographicHash::hash(("kaint-p2p-room:" + normalizedSessionCode(code)).toUtf8(),
                                                       QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex().left(20));
}

QString signDescription(const QString& code, const QString& kind, const QString& sdp)
{
    const QString base = withoutCarriageReturns(sdp);
    return withCrlf(base + QStringLiteral("\na=kaint-sig:") + mac(code, kind, base));
}

QString verifyDescription(const QString& code, const QString& kind, const QString& sdp)
{
    QStringList lines = withoutCarriageReturns(sdp).split(QLatin1Char('\n'));
    QString signature;
    for (int i = int(lines.size()) - 1; i >= 0; --i)
        if (lines[i].startsWith(QLatin1String("a=kaint-sig:"))) {
            signature = lines.takeAt(i).mid(12).trimmed();
            break;
        }
    const QString base = lines.join(QLatin1Char('\n')).trimmed();
    if (signature.isEmpty() || base.isEmpty() || signature != mac(code, kind, base))
        return {};
    return withCrlf(base);
}

// ---------------------------------------------------------------------------
// Canal de datos de WebRTC

#ifdef KAINT_HAVE_P2P

namespace {

// Lo que llega de los hilos de libdatachannel se atiende en el principal.
void post(std::function<void()> f)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(f), Qt::QueuedConnection);
}

} // namespace

class RtcLink : public Link {
public:
    explicit RtcLink(const QStringList& iceServers, QObject* parent = nullptr)
        : Link(parent)
    {
        rtc::Configuration config;
        for (const QString& url : iceServers) {
            const QString u = url.trimmed();
            if (u.isEmpty())
                continue;
            try {
                config.iceServers.emplace_back(u.toStdString());
            } catch (const std::exception&) {
                // Una dirección mal escrita no impide usar las demás.
            }
        }
        m_pc = std::make_shared<rtc::PeerConnection>(config);
        QPointer<RtcLink> self(this);
        m_pc->onGatheringStateChange([self](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete)
                post([self] {
                    if (self)
                        self->deliverDescription();
                });
        });
        m_pc->onStateChange([self](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
                post([self] {
                    if (self)
                        self->lost();
                });
        });
        m_pc->onDataChannel([self](std::shared_ptr<rtc::DataChannel> dc) {
            // Un canal entrante ya está abierto (onOpen no se repetiría).
            post([self, dc] {
                if (self)
                    self->m_dc = dc;
                else
                    dc->close();
            });
            listen(self, dc);
            post([self] {
                if (self)
                    self->markOpen();
            });
        });
        // Si no termina de reunir candidatos (un STUN que no responde), se manda lo que haya.
        m_gather.setSingleShot(true);
        m_gather.setInterval(5000);
        QObject::connect(&m_gather, &QTimer::timeout, this, [this] { deliverDescription(); });
    }

    ~RtcLink() override { shutdown(); }

    // La descripción local, con todos los candidatos, cuando está lista.
    std::function<void(const QString& sdp)> onDescription;

    void createOffer()
    {
        m_dc = m_pc->createDataChannel("kaint");
        listen(QPointer<RtcLink>(this), m_dc);
        m_gather.start();
    }

    void acceptOffer(const QString& sdp)
    {
        try {
            m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Offer));
            m_gather.start();
        } catch (const std::exception&) {
            QTimer::singleShot(0, this, [this] { lost(); });
        }
    }

    bool acceptAnswer(const QString& sdp)
    {
        if (m_answered)
            return false;
        try {
            m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), rtc::Description::Type::Answer));
            m_answered = true;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void sendText(const QByteArray& utf8) override
    {
        if (!m_open || !m_dc)
            return;
        try {
            if (utf8.size() <= kChunk) {
                m_dc->send(utf8.toStdString());
                return;
            }
            qsizetype pos = 0;
            while (pos < utf8.size()) {
                qsizetype end = std::min(pos + kChunk, utf8.size());
                // Sin partir un carácter de UTF-8 en dos.
                while (end < utf8.size() && end > pos + 1 && (uchar(utf8[end]) & 0xc0) == 0x80)
                    --end;
                const bool last = end >= utf8.size();
                m_dc->send((last ? "=" : "+") + utf8.mid(pos, end - pos).toStdString());
                pos = end;
            }
        } catch (const std::exception&) {
            lost();
        }
    }

    void close() override
    {
        if (m_closing)
            return;
        m_closing = true;
        const bool wasOpen = m_open;
        m_open = false;
        shutdown();
        if (wasOpen)
            QTimer::singleShot(0, this, [this] { emit closed(); });
    }

    void dispose() override
    {
        // Lo pendiente sale en un momento (lo último que se manda es el aviso de cierre).
        QTimer::singleShot(300, this, [this] {
            close();
            deleteLater();
        });
    }

    bool isOpen() const override { return m_open; }
    void setMaxMessage(qint64 bytes) override { m_maxMessage = bytes; }

private:
    static void listen(QPointer<RtcLink> self, const std::shared_ptr<rtc::DataChannel>& dc)
    {
        dc->onOpen([self] {
            post([self] {
                if (self)
                    self->markOpen();
            });
        });
        dc->onClosed([self] {
            post([self] {
                if (self)
                    self->lost();
            });
        });
        dc->onMessage([self](rtc::message_variant data) {
            if (const auto* text = std::get_if<std::string>(&data)) {
                QByteArray bytes = QByteArray::fromStdString(*text);
                post([self, bytes] {
                    if (self)
                        self->receive(bytes);
                });
            }
        });
    }

    void deliverDescription()
    {
        if (m_described || !onDescription)
            return;
        const auto description = m_pc->localDescription();
        if (!description)
            return;
        m_described = true;
        m_gather.stop();
        onDescription(QString::fromStdString(std::string(*description)));
    }

    void markOpen()
    {
        if (m_open || m_closing)
            return;
        m_open = true;
        emit opened();
    }

    void lost()
    {
        if (m_closing)
            return;
        m_closing = true;
        const bool wasOpen = m_open;
        m_open = false;
        shutdown();
        if (wasOpen)
            emit closed();
        else
            emit failed(Link::tr("No se pudo establecer la conexión directa."));
    }

    void receive(const QByteArray& bytes)
    {
        if (m_closing || bytes.isEmpty())
            return;
        const char kind = bytes[0];
        if (kind == '+' || kind == '=') {
            m_partial += bytes.mid(1);
            if (m_partial.size() > m_maxMessage) {
                m_partial.clear();
                lost();
                return;
            }
            if (kind == '+')
                return;
            const QByteArray whole = m_partial;
            m_partial.clear();
            emit textReceived(whole);
            return;
        }
        if (bytes.size() > m_maxMessage) {
            lost();
            return;
        }
        emit textReceived(bytes);
    }

    void shutdown()
    {
        m_gather.stop();
        if (m_dc) {
            m_dc->resetCallbacks();
            try {
                m_dc->close();
            } catch (const std::exception&) {
            }
            m_dc.reset();
        }
        if (m_pc) {
            m_pc->resetCallbacks();
            try {
                m_pc->close();
            } catch (const std::exception&) {
            }
            m_pc.reset();
        }
    }

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;
    QTimer m_gather;
    QByteArray m_partial;
    qint64 m_maxMessage = kMaxMessage;
    bool m_open = false;
    bool m_closing = false;
    bool m_described = false;
    bool m_answered = false;
};

#else

// Sin libdatachannel: nunca llega a abrirse.
class RtcLink : public Link {
public:
    explicit RtcLink(const QStringList&, QObject* parent = nullptr)
        : Link(parent)
    {
    }
    std::function<void(const QString&)> onDescription;
    void createOffer() {}
    void acceptOffer(const QString&) {}
    bool acceptAnswer(const QString&) { return false; }
    void sendText(const QByteArray&) override {}
    void close() override {}
    void dispose() override { deleteLater(); }
    bool isOpen() const override { return false; }
};

#endif

// ---------------------------------------------------------------------------
// Señalización: el protocolo de los rastreadores de WebTorrent por WebSocket. La sala es el
// "info_hash" y cada uno se presenta con un "peer_id"; el rastreador reenvía las ofertas de uno a
// otros de la sala y las respuestas al que hizo la oferta.

Signaling::Signaling(const QStringList& servers, const QString& room, QObject* parent)
    : QObject(parent)
    , m_room(room)
    , m_peerId(randomId())
{
    for (const QString& url : servers)
        if (!url.trimmed().isEmpty())
            m_servers.append(Server{url.trimmed(), {}, false, 0, {}});
}

Signaling::~Signaling()
{
    for (Server& s : m_servers)
        if (s.socket) {
            s.socket->disconnect(this);
            s.socket->dispose();
        }
}

void Signaling::start()
{
    m_running = true;
    if (m_servers.isEmpty()) {
        QTimer::singleShot(0, this, [this] { emit unreachable(tr("No hay servidores de señalización en la configuración.")); });
        return;
    }
    for (int i = 0; i < m_servers.size(); ++i)
        connectServer(i);
}

void Signaling::stop()
{
    if (!m_running)
        return;
    m_running = false;
    for (int i = 0; i < m_servers.size(); ++i) {
        Server& s = m_servers[i];
        if (s.socket && s.ready)
            send(i, QJsonObject{{QStringLiteral("action"), QStringLiteral("announce")},
                                {QStringLiteral("event"), QStringLiteral("stopped")},
                                {QStringLiteral("info_hash"), m_room},
                                {QStringLiteral("peer_id"), m_peerId},
                                {QStringLiteral("numwant"), 0},
                                {QStringLiteral("uploaded"), 0},
                                {QStringLiteral("downloaded"), 0}});
        if (s.socket) {
            s.socket->disconnect(this);
            s.socket->dispose();
        }
        s.socket = nullptr;
        s.ready = false;
    }
}

int Signaling::readyCount() const
{
    int n = 0;
    for (const Server& s : m_servers)
        n += s.ready ? 1 : 0;
    return n;
}

void Signaling::connectServer(int index)
{
    if (!m_running)
        return;
    Server& s = m_servers[index];
    const QUrl url(s.url);
    if (!url.isValid() || (url.scheme() != QLatin1String("ws") && url.scheme() != QLatin1String("wss"))) {
        s.lastError = tr("Dirección no válida: %1").arg(s.url);
        s.failures = 99;
        checkUnreachable();
        return;
    }
    WebSocket* socket = WebSocket::connectToUrl(url, this);
    socket->setMaxMessage(1024 * 1024);
    s.socket = socket;
    connect(socket, &WebSocket::opened, this, [this, index] {
        m_servers[index].ready = true;
        m_servers[index].failures = 0;
        emit serverReady(index);
    });
    connect(socket, &WebSocket::textReceived, this, [this, index](const QByteArray& text) { onText(index, text); });
    auto dropped = [this, index, socket](const QString& reason) {
        Server& srv = m_servers[index];
        if (srv.socket != socket)
            return;
        srv.socket = nullptr;
        srv.ready = false;
        srv.lastError = reason;
        ++srv.failures;
        socket->disconnect(this);
        socket->dispose();
        checkUnreachable();
        // Se vuelve a intentar, cada vez más despacio.
        if (m_running)
            QTimer::singleShot(std::min(30000, 2000 * srv.failures), this, [this, index] {
                if (m_running && !m_servers[index].socket)
                    connectServer(index);
            });
    };
    connect(socket, &WebSocket::failed, this, dropped);
    connect(socket, &WebSocket::closed, this, [dropped] { dropped(tr("Se cerró la conexión.")); });
}

void Signaling::checkUnreachable()
{
    for (const Server& s : std::as_const(m_servers))
        if (s.ready || s.failures == 0)
            return;
    QStringList errors;
    for (const Server& s : std::as_const(m_servers))
        errors << QStringLiteral("%1: %2").arg(s.url, s.lastError);
    emit unreachable(tr("No se pudo conectar con ningún servidor de señalización (%1).").arg(errors.join(QStringLiteral("; "))));
}

void Signaling::send(int index, const QJsonObject& message)
{
    if (WebSocket* s = m_servers.value(index).socket)
        s->sendText(QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void Signaling::announce(const QJsonArray& offers, int numwant)
{
    for (int i = 0; i < m_servers.size(); ++i)
        if (m_servers[i].ready)
            send(i, QJsonObject{{QStringLiteral("action"), QStringLiteral("announce")},
                                {QStringLiteral("info_hash"), m_room},
                                {QStringLiteral("peer_id"), m_peerId},
                                {QStringLiteral("numwant"), numwant},
                                {QStringLiteral("uploaded"), 0},
                                {QStringLiteral("downloaded"), 0},
                                {QStringLiteral("offers"), offers}});
}

void Signaling::sendAnswer(int server, const QString& toPeer, const QString& offerId, const QJsonObject& answer)
{
    send(server, QJsonObject{{QStringLiteral("action"), QStringLiteral("announce")},
                             {QStringLiteral("info_hash"), m_room},
                             {QStringLiteral("peer_id"), m_peerId},
                             {QStringLiteral("to_peer_id"), toPeer},
                             {QStringLiteral("offer_id"), offerId},
                             {QStringLiteral("answer"), answer}});
}

void Signaling::onText(int index, const QByteArray& text)
{
    const QJsonObject m = QJsonDocument::fromJson(text).object();
    if (m.value(QStringLiteral("info_hash")).toString() != m_room)
        return;
    const QString from = m.value(QStringLiteral("peer_id")).toString();
    const QString offerId = m.value(QStringLiteral("offer_id")).toString();
    if (from.isEmpty() || from == m_peerId || offerId.isEmpty())
        return;
    if (m.value(QStringLiteral("offer")).isObject())
        emit offerReceived(index, from, offerId, m.value(QStringLiteral("offer")).toObject());
    else if (m.value(QStringLiteral("answer")).isObject())
        emit answerReceived(index, from, offerId, m.value(QStringLiteral("answer")).toObject());
}

// ---------------------------------------------------------------------------
// Anfitrión

P2PHost::P2PHost(const Settings& settings, const QString& code, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_code(code)
    , m_signaling(new Signaling(settings.signalServers, p2pRoom(code), this))
{
    connect(m_signaling, &Signaling::serverReady, this, [this] {
        m_signaling->announce({}, 0);
        emit signalingChanged();
    });
    connect(m_signaling, &Signaling::unreachable, this, &P2PHost::signalingChanged);
    connect(m_signaling, &Signaling::offerReceived, this, &P2PHost::onOffer);
    m_announce.setInterval(kAnnounceMs);
    connect(&m_announce, &QTimer::timeout, this, [this] {
        m_signaling->announce({}, 0);
        emit signalingChanged();
    });
}

P2PHost::~P2PHost()
{
    stop();
}

void P2PHost::start()
{
    m_signaling->start();
    m_announce.start();
}

void P2PHost::stop()
{
    m_announce.stop();
    m_signaling->stop();
    for (const QPointer<RtcLink>& link : std::as_const(m_pending))
        if (link)
            link->dispose();
    m_pending.clear();
}

int P2PHost::signalingReady() const
{
    return m_signaling->readyCount();
}

void P2PHost::onOffer(int server, const QString& peerId, const QString& offerId, const QJsonObject& offer)
{
    // La misma oferta llega por cada rastreador: se atiende una vez.
    if (m_pending.contains(offerId) || m_pending.size() >= 32)
        return;
    const QString sdp = verifyDescription(m_code, QStringLiteral("offer"), offer.value(QStringLiteral("sdp")).toString());
    if (sdp.isEmpty())
        return;
    auto* link = new RtcLink(m_settings.iceServers, this);
    m_pending.insert(offerId, link);
    QPointer<RtcLink> guard(link);
    link->onDescription = [this, server, peerId, offerId](const QString& answer) {
        m_signaling->sendAnswer(server, peerId, offerId,
                                QJsonObject{{QStringLiteral("type"), QStringLiteral("answer")},
                                            {QStringLiteral("sdp"), signDescription(m_code, QStringLiteral("answer"), answer)}});
    };
    connect(link, &Link::opened, this, [this, link, offerId] {
        // Ya abierto, el canal es del servidor (que también se ocupa de cerrarlo).
        m_pending.remove(offerId);
        link->disconnect(this);
        emit linkReady(link);
    });
    connect(link, &Link::failed, this, [this, link, offerId] {
        m_pending.remove(offerId);
        link->disconnect(this);
        link->dispose();
    });
    QTimer::singleShot(kPendingMs, this, [this, guard, offerId] {
        if (guard && m_pending.value(offerId) == guard) {
            m_pending.remove(offerId);
            guard->disconnect(this);
            guard->dispose();
        }
    });
    link->acceptOffer(sdp);
}

// ---------------------------------------------------------------------------
// Invitado

P2PGuest::P2PGuest(const Settings& settings, const QString& code, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_code(code)
    , m_signaling(new Signaling(settings.signalServers, p2pRoom(code), this))
{
    connect(m_signaling, &Signaling::serverReady, this, [this] {
        // Las ofertas ya listas también le sirven al que acaba de conectar.
        if (!m_ready.isEmpty())
            m_signaling->announce(m_ready, int(m_ready.size()));
    });
    connect(m_signaling, &Signaling::unreachable, this, [this](const QString& reason) { finish(nullptr, reason); });
    connect(m_signaling, &Signaling::answerReceived, this, &P2PGuest::onAnswer);
    m_rounds.setInterval(kRoundMs);
    connect(&m_rounds, &QTimer::timeout, this, &P2PGuest::newRound);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kJoinMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        finish(nullptr, tr("No se encontró la sesión. Revisa el código y que el anfitrión siga hospedándola (y que los dos usen "
                           "algún servidor de señalización en común)."));
    });
}

P2PGuest::~P2PGuest()
{
    stop();
}

void P2PGuest::start()
{
    if (!p2pAvailable()) {
        QTimer::singleShot(0, this, [this] { finish(nullptr, tr("Este Kaint se compiló sin la conexión P2P.")); });
        return;
    }
    emit progress(tr("Buscando la sesión…"));
    m_signaling->start();
    m_timeout.start();
    m_rounds.start();
    newRound();
}

void P2PGuest::stop()
{
    m_rounds.stop();
    m_timeout.stop();
    m_signaling->stop();
    for (const QPointer<RtcLink>& link : std::as_const(m_offers))
        if (link)
            link->dispose();
    m_offers.clear();
}

void P2PGuest::newRound()
{
    if (m_done)
        return;
    // Las de la ronda anterior que nadie contestó se descartan.
    for (auto it = m_offers.begin(); it != m_offers.end();) {
        if (!*it) {
            it = m_offers.erase(it);
            continue;
        }
        ++it;
    }
    if (m_offers.size() > 6) {
        auto oldest = m_offers.begin();
        if (*oldest)
            (*oldest)->dispose();
        m_offers.erase(oldest);
    }
    m_ready = {};
    for (int i = 0; i < kOffersPerRound; ++i) {
        auto* link = new RtcLink(m_settings.iceServers, this);
        const QString offerId = randomId();
        m_offers.insert(offerId, link);
        link->onDescription = [this, offerId](const QString& sdp) {
            if (m_done)
                return;
            m_ready.append(QJsonObject{{QStringLiteral("offer_id"), offerId},
                                       {QStringLiteral("offer"), QJsonObject{{QStringLiteral("type"), QStringLiteral("offer")},
                                                                             {QStringLiteral("sdp"), signDescription(m_code, QStringLiteral("offer"), sdp)}}}});
            m_signaling->announce(QJsonArray{m_ready.last()}, 1);
        };
        connect(link, &Link::opened, this, [this, link] { finish(link, {}); });
        connect(link, &Link::failed, this, [this, link, offerId] {
            m_offers.remove(offerId);
            link->disconnect(this);
            link->dispose();
        });
        link->createOffer();
    }
}

void P2PGuest::onAnswer(int, const QString&, const QString& offerId, const QJsonObject& answer)
{
    RtcLink* link = m_offers.value(offerId);
    if (!link || m_done)
        return;
    const QString sdp = verifyDescription(m_code, QStringLiteral("answer"), answer.value(QStringLiteral("sdp")).toString());
    if (sdp.isEmpty())
        return;
    if (link->acceptAnswer(sdp))
        emit progress(tr("Conectando con el anfitrión…"));
}

void P2PGuest::finish(Link* link, const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    if (link) {
        // Se queda el que se abrió; los demás, fuera.
        for (auto it = m_offers.begin(); it != m_offers.end(); ++it)
            if (*it == link) {
                m_offers.erase(it);
                break;
            }
        link->disconnect(this);
    }
    stop();
    if (link)
        emit connected(link);
    else
        emit failed(error);
}

} // namespace Collab
